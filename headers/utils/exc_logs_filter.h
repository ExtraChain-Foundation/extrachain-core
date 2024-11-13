/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef FILE_FILTER_HPP
#define FILE_FILTER_HPP

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <algorithm>

enum class LogModule {
    None       = 0,
    DFS        = 1 << 0,
    Network    = 1 << 1,
    Utils      = 1 << 2,
    Block      = 1 << 3,
    Managers   = 1 << 4,
    Global     = 1 << 5,
    Encryption = 1 << 6,
    All        = (1 << 7) - 1
};

inline LogModule operator|(LogModule a, LogModule b) {
    return static_cast<LogModule>(static_cast<int>(a) | static_cast<int>(b));
}

inline LogModule operator&(LogModule a, LogModule b) {
    return static_cast<LogModule>(static_cast<int>(a) & static_cast<int>(b));
}

inline LogModule operator~(LogModule a) {
    return static_cast<LogModule>(~static_cast<int>(a) & static_cast<int>(LogModule::All));
}

class FileFilter {
public:
    using ModulePatterns = std::vector<std::string>;
    using ModuleMap      = std::unordered_map<std::string, ModulePatterns>;

private:
    ModuleMap      m_modules;
    ModulePatterns m_custom_patterns;
    ModulePatterns m_exclude_patterns;
    bool           m_case_sensitive = false;
    bool           m_inverse_mode   = false;

public:
    FileFilter() {
        m_modules["DFS"] = { "dfs_controller",     "fragment_storage", "historical_chain", "historical_sql",
                             "permission_manager", "dfs_utils",        "name_validator" };

        m_modules["Blockchain"] = { "actor",
                                    "block",
                                    "blockchain",
                                    "genesis_block",
                                    "block_variant",
                                    "index/actorindex",
                                    "index/blockindex",
                                    "transaction",
                                    "private_profile" };

        m_modules["Encryption"] = { "encryption_tools", "key_private", "key_public" };

        m_modules["Managers"] = { "account_controller",  "connections_manager", "extrachain_node",
                                  "data_mining_manager", "logs_manager",        "thread_pool",
                                  "transaction_manager", "file_data_manager",   "token_manager" };

        m_modules["Network"] = { "discovery_service", "network_manager",   "network_status", "message_body",
                                 "isocket_service",   "websocket_service", "upnpconnection" };

        m_modules["Utils"] = { "autologinhash",
                               "bignumber",
                               "bignumber_float",
                               "db_connector",
                               "db_connector_strict",
                               "db_schema",
                               "exc_utils",
                               "exc_magic",
                               "exc_logs",
                               "exc_logs_extra",
                               "exc_msgpack_describe",
                               "variant_model",
                               "safeptr",
                               "vpn_types" };

        m_modules["Global"] = { "metatypes", "extrachain_global" };
    }

    void setCaseSensitive(bool sensitive) {
        m_case_sensitive = sensitive;
    }

    void setInverseMode(bool inverse) {
        m_inverse_mode = inverse;
    }

    bool is_inverse_mode() const {
        return m_inverse_mode;
    }

    void addExcludePattern(std::string_view pattern) {
        m_exclude_patterns.push_back(std::string(pattern));
    }

    void clearExcludePatterns() {
        m_exclude_patterns.clear();
    }

    void addCustomPattern(std::string_view pattern) {
        m_custom_patterns.push_back(std::string(pattern));
    }

    void clearCustomPatterns() {
        m_custom_patterns.clear();
    }

    bool matchesPattern(std::string_view filename, std::string_view pattern) const {
        if (filename.empty() || pattern.empty())
            return false;

        std::string working_filename(filename);
        std::string working_pattern(pattern);

        if (!m_case_sensitive) {
            std::transform(
                working_filename.begin(),
                working_filename.end(),
                working_filename.begin(),
                ::tolower);
            std::transform(
                working_pattern.begin(),
                working_pattern.end(),
                working_pattern.begin(),
                ::tolower);
        }

        return working_filename.find(working_pattern) != std::string::npos;
    }

    bool matchesExcludePatterns(std::string_view filename) const {
        return std::any_of(
            m_exclude_patterns.begin(),
            m_exclude_patterns.end(),
            [this, filename](const std::string& pattern) {
                return matchesPattern(filename, pattern);
            });
    }

    bool matchesModule(std::string_view filename, std::string_view module_name) const {
        auto pos           = filename.find_last_of("/\\");
        auto base_filename = (pos == std::string::npos) ? filename : filename.substr(pos + 1);

        if (auto it = m_modules.find(std::string(module_name)); it != m_modules.end()) {
            return std::any_of(
                it->second.begin(),
                it->second.end(),
                [this, base_filename](const std::string& pattern) {
                    return matchesPattern(base_filename, pattern);
                });
        }
        return false;
    }

    bool matchesCustomPatterns(std::string_view filename) const {
        return std::any_of(
            m_custom_patterns.begin(),
            m_custom_patterns.end(),
            [this, filename](const std::string& pattern) {
                return matchesPattern(filename, pattern);
            });
    }

    bool matches(std::string_view filename) const {
        if (matchesExcludePatterns(filename))
            return false;

        bool matched = matchesCustomPatterns(filename);
        if (!matched) {
            matched = std::any_of(m_modules.begin(), m_modules.end(), [this, filename](const auto& module) {
                return matchesModule(filename, module.first);
            });
        }

        return m_inverse_mode ? !matched : matched;
    }

    LogModule determineModule(std::string_view filename) const {
        if (matchesModule(filename, "DFS"))
            return LogModule::DFS;
        if (matchesModule(filename, "Network"))
            return LogModule::Network;
        if (matchesModule(filename, "Utils"))
            return LogModule::Utils;
        if (matchesModule(filename, "Blockchain"))
            return LogModule::Block;
        if (matchesModule(filename, "Managers"))
            return LogModule::Managers;
        if (matchesModule(filename, "Global"))
            return LogModule::Global;
        if (matchesModule(filename, "Encryption"))
            return LogModule::Encryption;
        return LogModule::None;
    }

    std::vector<std::string> getModules() const {
        std::vector<std::string> result;
        result.reserve(m_modules.size());
        for (const auto& [name, _] : m_modules) {
            result.push_back(name);
        }
        return result;
    }

    ModulePatterns getModulePatterns(std::string_view module_name) const {
        if (auto it = m_modules.find(std::string(module_name)); it != m_modules.end()) {
            return it->second;
        }
        return {};
    }

    const ModulePatterns& getCustomPatterns() const {
        return m_custom_patterns;
    }
};

#endif // FILE_FILTER_HPP
