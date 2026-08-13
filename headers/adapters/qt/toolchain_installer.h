/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <map>

#include <QString>

#include "adapters/qt/qt_compat_global.h"
#include "contracts/toolchain_registry.h"

namespace ExtraChain::Core {
    class ExtraChainNode;
}

namespace ExtraChain::Contracts {

    struct ToolchainInstallation {
        ToolchainManifest manifest;
        QString           path;
    };

    struct ContractComponent {
        std::string              id;
        std::string              name;
        std::string              description;
        std::string              category;
        std::string              rust_import;
        std::string              source_import;
        std::vector<std::string> dependencies;
        std::vector<std::string> conflicts;
    };
    BOOST_DESCRIBE_STRUCT(ContractComponent,
                          (),
                          (id, name, description, category, rust_import, source_import, dependencies, conflicts))

    struct ContractParameter {
        std::string id;
        std::string name;
        std::string type;
        std::string default_value;
        bool        required = false;
    };
    BOOST_DESCRIBE_STRUCT(ContractParameter, (), (id, name, type, default_value, required))

    struct ContractBlueprint {
        std::string                    id;
        std::string                    name;
        std::string                    description;
        std::vector<std::string>       components;
        std::vector<ContractParameter> parameters;
    };
    BOOST_DESCRIBE_STRUCT(ContractBlueprint, (), (id, name, description, components, parameters))

    class EXTRACHAIN_QT_EXPORT ToolchainInstaller {
    public:
        ToolchainInstaller(ExtraChain::Core::ExtraChainNode* node, QString root_path);
        ToolchainInstaller(ExtraChain::Core::ExtraChainNode* node, QString root_path, ToolchainLanguage language);

        std::expected<ToolchainInstallation, ToolchainFailure> install_stable(bool allow_first_install);
        std::expected<ToolchainInstallation, ToolchainFailure> current() const;
        std::expected<QString, ToolchainFailure>               build_contract(const QString& source,
                                                                              const QString& project_name,
                                                                              int            timeout_ms = 120000) const;
        [[nodiscard]] std::vector<ContractComponent>           component_catalog() const;
        [[nodiscard]] std::vector<ContractBlueprint>           contract_blueprints() const;
        [[nodiscard]] std::expected<QString, ToolchainFailure> compose_contract(
            std::span<const std::string>              component_ids,
            std::string_view                          blueprint_id,
            const std::map<std::string, std::string>& parameters,
            const QString&                            project_name) const;

    private:
        ExtraChain::Core::ExtraChainNode* node_;
        QString                           root_path_;
        ToolchainLanguage                 language_ = ToolchainLanguage::Rust;
    };

} // namespace ExtraChain::Contracts
