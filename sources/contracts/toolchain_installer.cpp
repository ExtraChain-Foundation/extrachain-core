/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "adapters/qt/toolchain_installer.h"

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <unordered_set>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOperatingSystemVersion>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSysInfo>
#include <QTemporaryDir>

#include "extrachain_version.h"
#include "core/extrachain_node.h"
#include "utils/exc_utils.h"

namespace ExtraChain::Contracts {
    namespace {
        constexpr auto StateFile                  = "installation.json";
        constexpr auto SupportedComponentsVersion = "0.3.0";
        constexpr auto SupportedCatalogVersion    = "0.3.0";
        constexpr auto SupportedTemplateVersion   = "0.3.0";

        ToolchainFailure failure(ToolchainError error, std::string detail) {
            return { error, std::move(detail) };
        }

        std::optional<QString> source_string(const std::string& value, qsizetype maximum_bytes) {
            const auto text = QString::fromUtf8(value);
            if (text.isEmpty() || text.toUtf8().size() > maximum_bytes
                || std::ranges::any_of(text, [](QChar character) {
                       return character.category() == QChar::Other_Control;
                   })) {
                return std::nullopt;
            }
            auto escaped = text;
            escaped.replace('\\', "\\\\");
            escaped.replace('"', "\\\"");
            return escaped;
        }

        QString platform_name() {
#if defined(Q_OS_WIN)
            return "windows";
#elif defined(Q_OS_MACOS)
            return "macos";
#elif defined(Q_OS_LINUX)
            return "linux";
#else
            return "unsupported";
#endif
        }

        QString architecture_name() {
            const auto architecture = QSysInfo::currentCpuArchitecture().toLower();
            if (architecture == "x86_64" || architecture == "amd64") {
                return "x86_64";
            }
            if (architecture == "arm64" || architecture == "aarch64") {
                return "aarch64";
            }
            return architecture;
        }

        std::optional<std::array<std::uint64_t, 3>> version_parts(const std::string& value) {
            std::array<std::uint64_t, 3> parts {};
            std::size_t                  start = 0;
            for (std::size_t index = 0; index < parts.size(); ++index) {
                const auto end = value.find('.', start);
                if ((index < parts.size() - 1 && end == std::string::npos)
                    || (index == parts.size() - 1 && end != std::string::npos)) {
                    return std::nullopt;
                }
                const auto part =
                    value.substr(start, end == std::string::npos ? value.size() - start : end - start);
                if (part.empty() || !std::ranges::all_of(part, [](char character) {
                        return character >= '0' && character <= '9';
                    })) {
                    return std::nullopt;
                }
                try {
                    parts[index] = std::stoull(part);
                } catch (...) {
                    return std::nullopt;
                }
                if (end != std::string::npos) {
                    start = end + 1;
                }
            }
            return parts;
        }

        bool compatible_with_core(const ToolchainManifest& manifest) {
            const auto current = version_parts(extrachain_version);
            const auto minimum = version_parts(manifest.core_min);
            const auto maximum = version_parts(manifest.core_max);
            return current.has_value() && minimum.has_value() && maximum.has_value() && *minimum <= *maximum
                   && *current >= *minimum && *current <= *maximum;
        }

        std::optional<QJsonObject> read_state(const QString& root_path) {
            QFile file(QDir(root_path).filePath(StateFile));
            if (!file.open(QIODevice::ReadOnly)) {
                return std::nullopt;
            }
            const auto document = QJsonDocument::fromJson(file.readAll());
            return document.isObject() ? std::optional(document.object()) : std::nullopt;
        }

        bool write_state(const QString& root_path, const ToolchainManifest& manifest, const QString& path) {
            QSaveFile file(QDir(root_path).filePath(StateFile));
            if (!file.open(QIODevice::WriteOnly)) {
                return false;
            }
            QJsonObject object;
            object["release_sequence"] = QString::number(manifest.release_sequence);
            object["version"]          = QString::fromStdString(manifest.version);
            object["path"]             = path;
            const auto manifest_json =
                QJsonDocument::fromJson(QByteArray::fromStdString(Json::serialize(manifest)));
            if (!manifest_json.isObject()) {
                return false;
            }
            object["manifest"]     = manifest_json.object();
            const auto state_bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
            return file.write(state_bytes) == state_bytes.size() && file.commit();
        }

        std::expected<QByteArray, ToolchainFailure> run_process(const QString&      program,
                                                                const QStringList&  arguments,
                                                                int                 timeout_ms,
                                                                const QString&      working_directory = {},
                                                                QProcessEnvironment environment       = {}) {
            QProcess process;
            process.setProgram(program);
            process.setArguments(arguments);
            process.setProcessChannelMode(QProcess::MergedChannels);
            if (!working_directory.isEmpty()) {
                process.setWorkingDirectory(working_directory);
            }
            if (environment.isEmpty()) {
                environment = QProcessEnvironment::systemEnvironment();
            }
            process.setProcessEnvironment(environment);
            process.start();
            if (!process.waitForStarted(5000) || !process.waitForFinished(timeout_ms)) {
                process.kill();
                process.waitForFinished();
                return std::unexpected(
                    failure(ToolchainError::StorageError, "Toolchain process did not finish in time"));
            }
            auto output = process.readAll();
            if (output.size() > 1024 * 1024) {
                output.truncate(1024 * 1024);
            }
            if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
                return std::unexpected(
                    failure(ToolchainError::StorageError,
                            output.isEmpty() ? "Toolchain process failed" : output.toStdString()));
            }
            return output;
        }

        bool safe_archive_entry(QString entry) {
            entry = entry.trimmed();
            if (entry.isEmpty() || QDir::isAbsolutePath(entry) || entry.contains('\\')) {
                return false;
            }
            const auto clean = QDir::cleanPath(entry);
            return clean != ".." && !clean.startsWith("../") && !clean.contains("/../");
        }

        std::expected<void, ToolchainFailure> extract_archive(const QString& archive, const QString& destination) {
            auto listing = run_process("tar", { "-tf", archive }, 30000);
            if (!listing.has_value()) {
                return std::unexpected(listing.error());
            }
            const auto entries = QString::fromUtf8(*listing).split('\n', Qt::SkipEmptyParts);
            if (entries.empty() || entries.size() > 20000
                || std::ranges::any_of(entries, [](const QString& entry) {
                       return !safe_archive_entry(entry);
                   })) {
                return std::unexpected(
                    failure(ToolchainError::InvalidManifest, "Toolchain archive contains an unsafe path"));
            }
            auto verbose = run_process("tar", { "-tvf", archive }, 30000);
            if (!verbose.has_value()) {
                return std::unexpected(verbose.error());
            }
            const auto lines = QString::fromUtf8(*verbose).split('\n', Qt::SkipEmptyParts);
            if (std::ranges::any_of(lines, [](const QString& line) {
                    return line.startsWith('l') || line.startsWith('h');
                })) {
                return std::unexpected(
                    failure(ToolchainError::InvalidManifest, "Toolchain archive contains a link"));
            }
            auto extracted = run_process("tar", { "-xf", archive, "-C", destination }, 120000);
            if (!extracted.has_value()) {
                return std::unexpected(extracted.error());
            }
            return {};
        }

        bool valid_toolchain_layout(const QString&           root,
                                    ToolchainLanguage        language,
                                    const ToolchainManifest& manifest) {
            const QDir directory(root);
            if (language == ToolchainLanguage::AssemblyScript) {
#if defined(Q_OS_WIN)
                constexpr auto NodeName = "bin/node.exe";
#else
                constexpr auto NodeName = "bin/node";
#endif
                const QFileInfo node(directory.filePath(NodeName));
                const QFileInfo compiler(directory.filePath("compiler/asc.js"));
                const QFileInfo marker(directory.filePath("compiler/mark-wasm.mjs"));
                const QFileInfo generator(directory.filePath("compiler/generate-contract.mjs"));
                const QFileInfo amount_library(directory.filePath("dependencies/as-bignum/package.json"));
                const QFileInfo sdk(directory.filePath("sdk/index.ts"));
                const QFileInfo components(directory.filePath("components/index.ts"));
                const QFileInfo catalog(directory.filePath("catalog/components.json"));
                const QFileInfo template_file(directory.filePath("templates/basic/assembly/contract.ts"));
                const QFileInfo generated_file(directory.filePath("templates/basic/assembly/generated.ts"));
                const QFileInfo entry_file(directory.filePath("templates/basic/assembly/index.ts"));
                QFile           metadata_file(directory.filePath("toolchain.json"));
                QFile           catalog_file(catalog.absoluteFilePath());
                if (!catalog_file.open(QIODevice::ReadOnly) || !metadata_file.open(QIODevice::ReadOnly)) {
                    return false;
                }
                const auto catalog_document  = QJsonDocument::fromJson(catalog_file.readAll());
                const auto metadata_document = QJsonDocument::fromJson(metadata_file.readAll());
                const auto runtime_version   = run_process(node.absoluteFilePath(), { "--version" }, 5000);
                return node.isFile() && node.isExecutable() && compiler.isFile() && marker.isFile()
                       && generator.isFile() && amount_library.isFile() && sdk.isFile() && components.isFile()
                       && catalog.isFile() && template_file.isFile() && generated_file.isFile()
                       && entry_file.isFile() && catalog_document.isObject()
                       && catalog_document.object().value("schema").toInt() == 2
                       && catalog_document.object().value("version").toString() == SupportedCatalogVersion
                       && catalog_document.object().value("language").toString() == "assemblyscript"
                       && catalog_document.object().value("components").isArray() && metadata_document.isObject()
                       && metadata_document.object().value("schema").toInt() == 1
                       && metadata_document.object().value("language").toString() == "assemblyscript"
                       && metadata_document.object().value("version").toString().toStdString() == manifest.version
                       && metadata_document.object().value("compiler_version").toString().toStdString()
                              == manifest.compiler_version
                       && metadata_document.object().value("runtime_version").toString().toStdString()
                              == manifest.runtime_version
                       && runtime_version.has_value()
                       && QString::fromUtf8(runtime_version.value()).trimmed()
                              == QString("v%1").arg(QString::fromStdString(manifest.runtime_version));
            }
#if defined(Q_OS_WIN)
            constexpr auto CargoName = "bin/cargo.exe";
            constexpr auto RustcName = "bin/rustc.exe";
#else
            constexpr auto CargoName = "bin/cargo";
            constexpr auto RustcName = "bin/rustc";
#endif
            const QFileInfo cargo(directory.filePath(CargoName));
            const QFileInfo rustc(directory.filePath(RustcName));
            const QFileInfo macros(directory.filePath("macros/Cargo.toml"));
            const QFileInfo sdk(directory.filePath("sdk/Cargo.toml"));
            const QFileInfo components(directory.filePath("components/Cargo.toml"));
            const QFileInfo catalog(directory.filePath("catalog/components.json"));
            const QFileInfo template_file(directory.filePath("templates/basic/src/lib.rs"));
            QFile           catalog_file(catalog.absoluteFilePath());
            if (!catalog_file.open(QIODevice::ReadOnly)) {
                return false;
            }
            const auto catalog_document = QJsonDocument::fromJson(catalog_file.readAll());
            return cargo.isFile() && cargo.isExecutable() && rustc.isFile() && rustc.isExecutable()
                   && macros.isFile() && sdk.isFile() && components.isFile() && catalog.isFile()
                   && template_file.isFile() && QDir(directory.filePath("rustup")).exists()
                   && QDir(directory.filePath("cargo-home")).exists() && catalog_document.isObject()
                   && catalog_document.object().value("schema").toInt() == 2
                   && catalog_document.object().value("version").toString() == SupportedCatalogVersion
                   && catalog_document.object().value("components").isArray();
        }

        bool valid_assemblyscript_source(const QString& source) {
            static const QRegularExpression import_expression(
                R"(\b(?:import|export)\b[^;]*\bfrom\s*[\"']([^\"']+)[\"'])");
            auto imports = import_expression.globalMatch(source);
            while (imports.hasNext()) {
                if (imports.next().captured(1) != "./generated") {
                    return false;
                }
            }
            static const QRegularExpression side_effect_import(R"(\bimport\s*[\"'])");
            static const QRegularExpression blocked_expression(
                R"(\b(?:declare|namespace|require)\b|@external\b|<reference\s+path|\bimport\s*\(|\bmemory\.(?:grow|size)\b)");
            return !side_effect_import.match(source).hasMatch() && !blocked_expression.match(source).hasMatch();
        }

        bool copy_file(const QString& source, const QString& target) {
            if (!QFileInfo::exists(source)) {
                return false;
            }
            QFile::remove(target);
            return QFile::copy(source, target);
        }

        bool copy_directory(const QString& source, const QString& target) {
            if (!QDir(source).exists() || !QDir().mkpath(target)) {
                return false;
            }
            QDirIterator entries(source,
                                 QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                                 QDirIterator::Subdirectories);
            while (entries.hasNext()) {
                const auto source_path = entries.next();
                const auto relative    = QDir(source).relativeFilePath(source_path);
                const auto target_path = QDir(target).filePath(relative);
                if (entries.fileInfo().isDir()) {
                    if (!QDir().mkpath(target_path)) {
                        return false;
                    }
                } else {
                    if (!QDir().mkpath(QFileInfo(target_path).absolutePath())
                        || !copy_file(source_path, target_path)) {
                        return false;
                    }
                }
            }
            return true;
        }
    } // namespace

    ToolchainInstaller::ToolchainInstaller(ExtraChain::Core::ExtraChainNode* node, QString root_path)
        : node_(node)
        , root_path_(std::move(root_path)) {
    }

    ToolchainInstaller::ToolchainInstaller(ExtraChain::Core::ExtraChainNode* node,
                                           QString                           root_path,
                                           ToolchainLanguage                 language)
        : node_(node)
        , root_path_(language == ToolchainLanguage::Rust
                         ? std::move(root_path)
                         : QDir(std::move(root_path))
                               .filePath(QString::fromStdString(std::string(toolchain_language_name(language)))))
        , language_(language) {
    }

    std::expected<ToolchainInstallation, ToolchainFailure> ToolchainInstaller::current() const {
        const auto state = read_state(root_path_);
        if (!state.has_value()) {
            return std::unexpected(failure(ToolchainError::Unavailable, "Toolchain is not installed"));
        }
        const auto                       sequence = state->value("release_sequence").toString().toULongLong();
        const auto                       path     = state->value("path").toString();
        const auto                       manifest_value = state->value("manifest");
        std::optional<ToolchainManifest> installed;
        if (manifest_value.isObject()) {
            const auto parsed = Json::deserialize<ToolchainManifest>(
                QJsonDocument(manifest_value.toObject()).toJson(QJsonDocument::Compact).toStdString());
            if (parsed.has_value()) {
                installed = *parsed;
            }
        }
        const auto root_path      = QFileInfo(root_path_).canonicalFilePath();
        const auto release        = QFileInfo(path);
        const auto canonical_path = release.canonicalFilePath();
        const auto release_parent = QFileInfo(canonical_path).dir().canonicalPath();
        const bool manifest_matches =
            installed.has_value()
            && ((language_ == ToolchainLanguage::Rust && installed->schema == 2)
                || (installed->schema == 3 && installed->language == toolchain_language_name(language_)));
        const bool compiler_matches =
            language_ == ToolchainLanguage::Rust
                ? !installed->rust_version.empty()
                : !installed->compiler_version.empty() && !installed->runtime_version.empty();
        if (!manifest_matches || installed->channel != "stable" || installed->release_sequence != sequence
            || !compiler_matches || installed->sdk_version.empty() || installed->components_version.empty()
            || installed->catalog_version.empty() || installed->template_version.empty()
            || installed->components_version != SupportedComponentsVersion
            || installed->catalog_version != SupportedCatalogVersion
            || installed->template_version != SupportedTemplateVersion
            || installed->contract_abi != std::to_string(ContractAbiVersion) || installed->created == 0
            || !version_parts(installed->version).has_value() || !compatible_with_core(*installed)
            || root_path.isEmpty() || canonical_path.isEmpty() || release_parent != root_path
            || !release.fileName().startsWith("release-r")) {
            return std::unexpected(failure(ToolchainError::Unavailable, "Installed toolchain state is invalid"));
        }
        if (std::ranges::find(installed->revoked_versions, installed->version)
            != installed->revoked_versions.end()) {
            return std::unexpected(failure(ToolchainError::Revoked, "Installed toolchain release is revoked"));
        }
        return ToolchainInstallation { .manifest = *installed, .path = canonical_path };
    }

    std::expected<ToolchainInstallation, ToolchainFailure> ToolchainInstaller::install_stable(
        bool allow_first_install) {
        if (node_ == nullptr || node_->toolchain_registry() == nullptr) {
            return std::unexpected(failure(ToolchainError::Unavailable, "ExtraChain Core is not ready"));
        }
        QDir root(root_path_);
        if (!root.exists() && !root.mkpath(".")) {
            return std::unexpected(failure(ToolchainError::StorageError, "Cannot create the toolchain directory"));
        }
        const auto state = read_state(root_path_);
        const auto accepted_sequence =
            state.has_value() ? state->value("release_sequence").toString().toULongLong() : 0;
        if (!state.has_value() && !allow_first_install) {
            return std::unexpected(
                failure(ToolchainError::Unauthorized, "Contract development must be enabled first"));
        }
        auto manifest = node_->toolchain_registry()->manifest(language_, accepted_sequence);
        if (!manifest.has_value()) {
            return std::unexpected(manifest.error());
        }
        if (state.has_value() && manifest->release_sequence == accepted_sequence) {
            const auto path = state->value("path").toString();
            if (QDir(path).exists()) {
                return ToolchainInstallation { .manifest = *manifest, .path = path };
            }
        }
        const auto platform     = platform_name().toStdString();
        const auto architecture = architecture_name().toStdString();
        const auto selected     = std::ranges::find_if(manifest->packages, [&](const ToolchainPackage& package) {
            return package.platform == platform && package.architecture == architecture
                   && (package.archive_format == "tar.zst" || package.archive_format == "tar.gz"
                       || package.archive_format == "tar.xz" || package.archive_format == "tar");
        });
        if (selected == manifest->packages.end()) {
            return std::unexpected(
                failure(ToolchainError::PackageNotFound, "No toolchain package exists for this platform"));
        }
        auto bytes = node_->toolchain_registry()->package(*selected);
        if (!bytes.has_value()) {
            return std::unexpected(bytes.error());
        }
        const auto archive_path = root.filePath(QString::fromStdString(selected->file_name));
        QSaveFile  archive(archive_path);
        if (!archive.open(QIODevice::WriteOnly)
            || archive.write(reinterpret_cast<const char*>(bytes->data()), bytes->size())
                   != static_cast<qint64>(bytes->size())
            || !archive.commit()) {
            return std::unexpected(failure(ToolchainError::StorageError, "Cannot save the toolchain package"));
        }
        const auto    target        = root.filePath(QString("release-r%1-%2")
                                              .arg(manifest->release_sequence, 20, 10, QLatin1Char('0'))
                                              .arg(QString::fromStdString(manifest->version)));
        const auto    previous_path = state.has_value() ? state->value("path").toString() : QString();
        QTemporaryDir staging(root.filePath("install-XXXXXX"));
        if (!staging.isValid()) {
            QFile::remove(archive_path);
            return std::unexpected(
                failure(ToolchainError::StorageError, "Cannot create the toolchain staging directory"));
        }
        auto extracted = extract_archive(archive_path, staging.path());
        QFile::remove(archive_path);
        if (!extracted.has_value()) {
            return std::unexpected(extracted.error());
        }
        if (!valid_toolchain_layout(staging.path(), language_, manifest.value())) {
            return std::unexpected(
                failure(ToolchainError::InvalidManifest, "Toolchain package layout is incomplete"));
        }
        if (QDir(target).exists()) {
            return std::unexpected(
                failure(ToolchainError::StorageError, "Toolchain release directory already exists"));
        }
        if (!QDir().rename(staging.path(), target)) {
            return std::unexpected(failure(ToolchainError::StorageError, "Cannot activate the toolchain release"));
        }
        if (!write_state(root_path_, *manifest, target)) {
            QDir(target).removeRecursively();
            return std::unexpected(failure(ToolchainError::StorageError, "Cannot save the toolchain state"));
        }
        const auto releases = root.entryInfoList({ "release-r*" }, QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto& release : releases) {
            if (release.absoluteFilePath() != target && release.absoluteFilePath() != previous_path) {
                QDir(release.absoluteFilePath()).removeRecursively();
            }
        }
        return ToolchainInstallation { .manifest = *manifest, .path = target };
    }

    std::expected<QString, ToolchainFailure> ToolchainInstaller::build_contract(const QString& source,
                                                                                const QString& project_name,
                                                                                int            timeout_ms) const {
        auto installation = current();
        if (!installation.has_value()) {
            return std::unexpected(installation.error());
        }
        const auto safe_name = project_name.trimmed();
        if (safe_name.isEmpty() || safe_name.size() > 64 || std::ranges::any_of(safe_name, [](QChar character) {
                return !character.isLetterOrNumber() && character != '_' && character != '-';
            })) {
            return std::unexpected(
                failure(ToolchainError::InvalidManifest, "Project name contains unsupported characters"));
        }
        static const QRegularExpression blocked_rust_source(
            R"((\b(?:include|include_bytes|include_str|env|option_env)\s*!|#\s*\[\s*path\b))");
        if (source.size() > 1024 * 1024
            || (language_ == ToolchainLanguage::Rust && blocked_rust_source.match(source).hasMatch())
            || (language_ == ToolchainLanguage::AssemblyScript && !valid_assemblyscript_source(source))) {
            return std::unexpected(
                failure(ToolchainError::InvalidManifest, "Contract source is not supported by the safe template"));
        }
        QTemporaryDir project(QDir(root_path_).filePath("build-XXXXXX"));
        const auto    source_directory = language_ == ToolchainLanguage::Rust ? "src" : "assembly";
        if (!project.isValid() || !QDir(project.path()).mkpath(source_directory)) {
            return std::unexpected(
                failure(ToolchainError::StorageError, "Cannot create the contract build directory"));
        }
        if (language_ == ToolchainLanguage::AssemblyScript) {
            const QDir release(installation->path);
            QDir       build(project.path());
            if (!build.mkpath("sdk") || !build.mkpath("components") || !build.mkpath("build")
                || !build.mkpath("node_modules")
                || !copy_file(release.filePath("sdk/index.ts"), build.filePath("sdk/index.ts"))
                || !copy_directory(release.filePath("components"), build.filePath("components"))
                || !copy_file(release.filePath("templates/basic/assembly/generated.ts"),
                              build.filePath("assembly/generated.ts"))
                || !copy_directory(release.filePath("dependencies/as-bignum"),
                                   build.filePath("node_modules/as-bignum"))) {
                return std::unexpected(
                    failure(ToolchainError::StorageError, "Cannot create the AssemblyScript project"));
            }
            QSaveFile  source_file(build.filePath("assembly/contract.ts"));
            const auto source_bytes = source.toUtf8();
            if (!source_file.open(QIODevice::WriteOnly) || source_file.write(source_bytes) != source_bytes.size()
                || !source_file.commit()) {
                return std::unexpected(failure(ToolchainError::StorageError, "Cannot write the contract source"));
            }
#if defined(Q_OS_WIN)
            const auto node_program = release.filePath("bin/node.exe");
#else
            const auto node_program = release.filePath("bin/node");
#endif
            auto generated =
                run_process(node_program,
                            { release.filePath("compiler/generate-contract.mjs"), "assembly/contract.ts" },
                            timeout_ms,
                            project.path());
            if (!generated.has_value()) {
                return std::unexpected(generated.error());
            }
            auto built = run_process(node_program,
                                     { release.filePath("compiler/asc.js"),
                                       "assembly/.exco/index.ts",
                                       "--outFile",
                                       "build/module.wasm",
                                       "--runtime",
                                       "stub",
                                       "--disable",
                                       "bulk-memory",
                                       "--optimizeLevel",
                                       "3",
                                       "--shrinkLevel",
                                       "1",
                                       "--use",
                                       "abort=assembly/.exco/index/contractAbort" },
                                     timeout_ms,
                                     project.path());
            if (!built.has_value()) {
                return std::unexpected(built.error());
            }
            const auto artifact = build.filePath("build/module.wasm");
            if (!QFile::exists(artifact)) {
                return std::unexpected(
                    failure(ToolchainError::StorageError, "AssemblyScript did not produce a WebAssembly module"));
            }
            auto marked = run_process(node_program,
                                      { release.filePath("compiler/mark-wasm.mjs"), artifact, "assemblyscript" },
                                      10000,
                                      project.path());
            if (!marked.has_value()) {
                return std::unexpected(marked.error());
            }
            const auto output = QDir(root_path_).filePath(QString("artifacts/%1.wasm").arg(safe_name));
            if (!QDir(root_path_).mkpath("artifacts")) {
                return std::unexpected(
                    failure(ToolchainError::StorageError, "Cannot create the artifact directory"));
            }
            QFile::remove(output);
            if (!QFile::copy(artifact, output)) {
                return std::unexpected(
                    failure(ToolchainError::StorageError, "Cannot save the WebAssembly module"));
            }
            return output;
        }
        const auto sdk        = QDir(installation->path).filePath("sdk");
        const auto components = QDir(installation->path).filePath("components");
        const auto cargo_file = QDir(project.path()).filePath("Cargo.toml");
        QSaveFile  manifest(cargo_file);
        const auto cargo =
            QString(
                "[package]\nname = \"%1\"\nversion = \"0.1.0\"\nedition = \"2024\"\n"
                "[lib]\ncrate-type = [\"cdylib\"]\n"
                "[dependencies]\nextrachain-contract-sdk = { path = \"%2\" }\n"
                "extrachain-contract-components = { path = \"%3\" }\n"
                "[profile.release]\npanic = \"abort\"\nlto = true\nopt-level = \"z\"\n"
                "codegen-units = 1\nstrip = true\n")
                .arg(safe_name, QDir::fromNativeSeparators(sdk), QDir::fromNativeSeparators(components));
        const auto cargo_bytes = cargo.toUtf8();
        if (!manifest.open(QIODevice::WriteOnly) || manifest.write(cargo_bytes) != cargo_bytes.size()
            || !manifest.commit()) {
            return std::unexpected(failure(ToolchainError::StorageError, "Cannot write the fixed Cargo manifest"));
        }
        QSaveFile  source_file(QDir(project.path()).filePath("src/lib.rs"));
        const auto source_bytes = source.toUtf8();
        if (!source_file.open(QIODevice::WriteOnly) || source_file.write(source_bytes) != source_bytes.size()
            || !source_file.commit()) {
            return std::unexpected(failure(ToolchainError::StorageError, "Cannot write the contract source"));
        }
        const auto cargo_program = QDir(installation->path)
                                       .filePath(
#if defined(Q_OS_WIN)
                                           "bin/cargo.exe"
#else
                                           "bin/cargo"
#endif
                                       );
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("CARGO_HOME", QDir(installation->path).filePath("cargo-home"));
        environment.insert("RUSTUP_HOME", QDir(installation->path).filePath("rustup"));
        environment.insert("RUSTC",
                           QDir(installation->path)
                               .filePath(
#if defined(Q_OS_WIN)
                                   "bin/rustc.exe"
#else
                                   "bin/rustc"
#endif
                                   ));
        auto locked = run_process(cargo_program,
                                  { "generate-lockfile", "--offline" },
                                  timeout_ms,
                                  project.path(),
                                  environment);
        if (!locked.has_value()) {
            return std::unexpected(locked.error());
        }
        auto built = run_process(cargo_program,
                                 { "build", "--release", "--offline", "--locked", "--target", "wasm32v1-none" },
                                 timeout_ms,
                                 project.path(),
                                 environment);
        if (!built.has_value()) {
            return std::unexpected(built.error());
        }
        const auto artifact =
            QDir(project.path())
                .filePath(
                    QString("target/wasm32v1-none/release/%1.wasm").arg(QString(safe_name).replace('-', '_')));
        if (!QFile::exists(artifact)) {
            return std::unexpected(
                failure(ToolchainError::StorageError, "Cargo did not produce a WebAssembly module"));
        }
        const auto output = QDir(root_path_).filePath(QString("artifacts/%1.wasm").arg(safe_name));
        if (!QDir(root_path_).mkpath("artifacts")) {
            return std::unexpected(failure(ToolchainError::StorageError, "Cannot create the artifact directory"));
        }
        QFile::remove(output);
        if (!QFile::copy(artifact, output)) {
            return std::unexpected(failure(ToolchainError::StorageError, "Cannot save the WebAssembly module"));
        }
        return output;
    }

    std::vector<ContractComponent> ToolchainInstaller::component_catalog() const {
        const auto installation = current();
        if (!installation.has_value()) {
            return {};
        }
        QFile file(QDir(installation->path).filePath("catalog/components.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        const auto document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject() || document.object().value("schema").toInt() != 2
            || document.object().value("version").toString() != SupportedCatalogVersion
            || !document.object().value("components").isArray()) {
            return {};
        }
        std::vector<ContractComponent> result;
        const auto                     components = document.object().value("components").toArray();
        result.reserve(static_cast<std::size_t>(components.size()));
        for (const auto& value : components) {
            const auto        object = value.toObject();
            ContractComponent component {
                .id          = object.value("id").toString().toStdString(),
                .name        = object.value("name").toString().toStdString(),
                .description = object.value("description").toString().toStdString(),
                .category    = object.value("category").toString().toStdString(),
                .rust_import = object.value("rust_import").toString().toStdString(),
                .source_import =
                    object.value(language_ == ToolchainLanguage::Rust ? "rust_import" : "source_import")
                        .toString()
                        .toStdString(),
            };
            const auto dependencies = object.value("dependencies").toArray();
            component.dependencies.reserve(static_cast<std::size_t>(dependencies.size()));
            for (const auto& dependency : dependencies) {
                component.dependencies.push_back(dependency.toString().toStdString());
            }
            const auto conflicts = object.value("conflicts").toArray();
            component.conflicts.reserve(static_cast<std::size_t>(conflicts.size()));
            for (const auto& conflict : conflicts) {
                component.conflicts.push_back(conflict.toString().toStdString());
            }
            if (component.id.empty() || component.name.empty() || component.description.empty()
                || component.category.empty() || component.source_import.empty()
                || std::ranges::any_of(result, [&](const ContractComponent& known) {
                       return known.id == component.id;
                   })) {
                return {};
            }
            result.push_back(std::move(component));
        }
        return result;
    }

    std::vector<ContractBlueprint> ToolchainInstaller::contract_blueprints() const {
        const auto installation = current();
        if (!installation.has_value()) {
            return {};
        }
        QFile file(QDir(installation->path).filePath("catalog/components.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        const auto document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject() || document.object().value("schema").toInt() != 2
            || document.object().value("version").toString() != SupportedCatalogVersion
            || !document.object().value("blueprints").isArray()) {
            return {};
        }
        const auto catalog = component_catalog();
        if (catalog.empty()) {
            return {};
        }
        std::vector<ContractBlueprint> result;
        const auto                     blueprints = document.object().value("blueprints").toArray();
        result.reserve(static_cast<std::size_t>(blueprints.size()));
        for (const auto& value : blueprints) {
            const auto        object = value.toObject();
            ContractBlueprint blueprint {
                .id          = object.value("id").toString().toStdString(),
                .name        = object.value("name").toString().toStdString(),
                .description = object.value("description").toString().toStdString(),
            };
            for (const auto& component : object.value("components").toArray()) {
                blueprint.components.push_back(component.toString().toStdString());
            }
            for (const auto& value : object.value("parameters").toArray()) {
                const auto parameter = value.toObject();
                blueprint.parameters.push_back({
                    .id            = parameter.value("id").toString().toStdString(),
                    .name          = parameter.value("name").toString().toStdString(),
                    .type          = parameter.value("type").toString().toStdString(),
                    .default_value = parameter.value("default").toVariant().toString().toStdString(),
                    .required      = parameter.value("required").toBool(),
                });
            }
            const auto invalid_component = std::ranges::any_of(blueprint.components, [&](const std::string& id) {
                return std::ranges::find(catalog, id, &ContractComponent::id) == catalog.end();
            });
            if (blueprint.id.empty() || blueprint.name.empty() || blueprint.description.empty()
                || invalid_component || std::ranges::any_of(result, [&](const ContractBlueprint& known) {
                       return known.id == blueprint.id;
                   })) {
                return {};
            }
            result.push_back(std::move(blueprint));
        }
        return result;
    }

    std::expected<QString, ToolchainFailure> ToolchainInstaller::compose_contract(
        std::span<const std::string>              component_ids,
        std::string_view                          blueprint_id,
        const std::map<std::string, std::string>& parameters,
        const QString&                            project_name) const {
        const auto name = project_name.trimmed();
        if (name.isEmpty() || name.size() > 64 || component_ids.empty()
            || std::ranges::any_of(name, [](QChar character) {
                   return !character.isLetterOrNumber() && character != '_' && character != '-';
               })) {
            return std::unexpected(
                failure(ToolchainError::InvalidManifest, "A project name and one component are required"));
        }
        const auto installation = current();
        if (!installation.has_value()) {
            return std::unexpected(installation.error());
        }
        const auto catalog = component_catalog();
        if (catalog.empty()) {
            return std::unexpected(
                failure(ToolchainError::InvalidManifest, "The installed component catalog is invalid"));
        }
        std::vector<std::string> selected;
        selected.reserve(component_ids.size());
        std::unordered_set<std::string>               resolving;
        const std::function<bool(const std::string&)> add_component = [&](const std::string& component_id) {
            if (std::ranges::find(selected, component_id) != selected.end()) {
                return true;
            }
            if (!resolving.insert(component_id).second) {
                return false;
            }
            const auto component = std::ranges::find(catalog, component_id, &ContractComponent::id);
            if (component == catalog.end()) {
                return false;
            }
            for (const auto& dependency : component->dependencies) {
                if (!add_component(dependency)) {
                    return false;
                }
            }
            resolving.erase(component_id);
            selected.push_back(component_id);
            return true;
        };
        for (const auto& component_id : component_ids) {
            if (!add_component(component_id)) {
                return std::unexpected(
                    failure(ToolchainError::InvalidManifest, "The component catalog dependency graph is invalid"));
            }
        }
        for (const auto& component_id : selected) {
            const auto component = std::ranges::find(catalog, component_id, &ContractComponent::id);
            if (std::ranges::any_of(component->conflicts, [&](const std::string& conflict) {
                    return std::ranges::find(selected, conflict) != selected.end();
                })) {
                return std::unexpected(
                    failure(ToolchainError::InvalidManifest, "Selected contract components conflict"));
            }
        }

        const auto blueprints = contract_blueprints();
        const auto blueprint  = std::ranges::find(blueprints, blueprint_id, &ContractBlueprint::id);
        if (blueprint == blueprints.end()) {
            return std::unexpected(
                failure(ToolchainError::InvalidManifest, "The selected contract preset is invalid"));
        }
        for (const auto& parameter : blueprint->parameters) {
            const auto value = parameters.find(parameter.id);
            if (parameter.required && (value == parameters.end() || value->second.empty())) {
                return std::unexpected(
                    failure(ToolchainError::InvalidManifest, "A required contract preset value is missing"));
            }
            if (value != parameters.end() && parameter.type == "integer"
                && (value->second.empty() || !std::ranges::all_of(value->second, [](char character) {
                        return character >= '0' && character <= '9';
                    }))) {
                return std::unexpected(
                    failure(ToolchainError::InvalidManifest, "A contract preset integer is invalid"));
            }
        }
        if (std::ranges::any_of(parameters, [&](const auto& value) {
                return std::ranges::find(blueprint->parameters, value.first, &ContractParameter::id)
                       == blueprint->parameters.end();
            })) {
            return std::unexpected(
                failure(ToolchainError::InvalidManifest, "The contract preset has an unknown value"));
        }

        const auto parameter_value = [&](std::string_view id) -> std::string {
            const auto supplied = parameters.find(std::string(id));
            if (supplied != parameters.end()) {
                return supplied->second;
            }
            const auto configured = std::ranges::find(blueprint->parameters, id, &ContractParameter::id);
            return configured == blueprint->parameters.end() ? std::string() : configured->default_value;
        };
        if (blueprint_id == "token") {
            const auto token_name   = source_string(parameter_value("name"), 64);
            const auto token_symbol = source_string(parameter_value("symbol"), 12);
            const auto decimals     = parameter_value("decimals");
            if (!token_name.has_value() || !token_symbol.has_value() || decimals.empty() || decimals.size() > 2
                || !std::ranges::all_of(decimals,
                                        [](char character) {
                                            return character >= '0' && character <= '9';
                                        })
                || std::stoul(decimals) > 18) {
                return std::unexpected(
                    failure(ToolchainError::InvalidManifest, "The fungible token preset is invalid"));
            }
            if (language_ == ToolchainLanguage::AssemblyScript) {
                return QString(
                           "import { StandardFungibleContract } from \"./generated\";\n\n"
                           "@contract({ standard: \"fungible\" })\n"
                           "export class GeneratedContract extends StandardFungibleContract {\n"
                           "  constructor() {\n"
                           "    super(\"%1\", \"%2\", %3, true);\n"
                           "  }\n"
                           "}\n")
                    .arg(*token_name, *token_symbol, QString::fromStdString(decimals));
            }
            return QString(
                       "#![no_std]\n\n"
                       "extern crate alloc;\n\n"
                       "use extrachain_contract_sdk::fungible_token;\n\n"
                       "#[fungible_token(\n"
                       "    name = \"%1\",\n"
                       "    symbol = \"%2\",\n"
                       "    decimals = %3,\n"
                       "    freeze_last_unit = true,\n"
                       ")]\n"
                       "pub struct GeneratedContract;\n")
                .arg(*token_name, *token_symbol, QString::fromStdString(decimals));
        }
        if (blueprint_id == "nft") {
            const auto collection_name   = source_string(parameter_value("name"), 64);
            const auto collection_symbol = source_string(parameter_value("symbol"), 12);
            if (!collection_name.has_value() || !collection_symbol.has_value()) {
                return std::unexpected(
                    failure(ToolchainError::InvalidManifest, "The NFT collection preset is invalid"));
            }
            if (language_ == ToolchainLanguage::AssemblyScript) {
                return QString(
                           "import { StandardNonFungibleContract } from \"./generated\";\n\n"
                           "@contract({ standard: \"nft\" })\n"
                           "export class GeneratedContract extends StandardNonFungibleContract {\n"
                           "  constructor() {\n"
                           "    super(\"%1\", \"%2\");\n"
                           "  }\n"
                           "}\n")
                    .arg(*collection_name, *collection_symbol);
            }
            return QString(
                       "#![no_std]\n\n"
                       "extern crate alloc;\n\n"
                       "use extrachain_contract_sdk::nft_collection;\n\n"
                       "#[nft_collection(name = \"%1\", symbol = \"%2\")]\n"
                       "pub struct GeneratedContract;\n")
                .arg(*collection_name, *collection_symbol);
        }

        QString component_list;
        QString component_imports;
        for (const auto& component_id : selected) {
            const auto component = std::ranges::find(catalog, component_id, &ContractComponent::id);
            component_list += QString("// - %1\n").arg(QString::fromStdString(component_id));
            if (language_ == ToolchainLanguage::Rust) {
                component_imports += QString("    pub use extrachain_contract_components::%1;\n")
                                         .arg(QString::fromStdString(component->source_import));
            } else {
                component_imports += QString("    %1,\n").arg(QString::fromStdString(component->source_import));
            }
        }
        if (language_ == ToolchainLanguage::AssemblyScript) {
            return QString(
                       "import {\n"
                       "  Context,\n"
                       "  ContractResult,\n"
                       "  EmptyValue,\n"
                       "  success,\n%2"
                       "} from \"./generated\";\n\n"
                       "// Components selected from the trusted ExtraChain catalog:\n%1"
                       "@contract({ version: 1, owner: \"owner\", upgrade: \"owner\" })\n"
                       "export class GeneratedContract {\n"
                       "  @state owner: string = \"\";\n\n"
                       "  @init\n"
                       "  initialize(context: Context): ContractResult<EmptyValue> {\n"
                       "    this.owner = context.caller();\n"
                       "    return success(new EmptyValue());\n"
                       "  }\n"
                       "}\n")
                .arg(component_list, component_imports);
        }
        return QString(
                   "#![no_std]\n\n"
                   "extern crate alloc;\n\n"
                   "use alloc::string::{String, ToString};\n"
                   "use extrachain_contract_sdk::{Context, ContractResult, contract};\n\n"
                   "// Components selected from the trusted ExtraChain catalog:\n%1\n"
                   "#[allow(unused_imports)]\n"
                   "mod components {\n%2}\n"
                   "#[contract(version = 1, owner = \"owner\", upgrade = \"owner\")]\n"
                   "#[derive(Default)]\n"
                   "pub struct GeneratedContract {\n"
                   "    owner: String,\n"
                   "}\n\n"
                   "#[contract]\n"
                   "impl GeneratedContract {\n"
                   "    #[init]\n"
                   "    fn init(&mut self, context: &Context<'_>) -> ContractResult<()> {\n"
                   "        self.owner = context.caller().to_string();\n"
                   "        Ok(())\n"
                   "    }\n"
                   "}\n")
            .arg(component_list, component_imports);
    }

} // namespace ExtraChain::Contracts
