// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "item_tools.h"
#include "spell_tools.h"
#include "class_tools.h"

#include <nlohmann/json.hpp>
#include <functional>
#include <map>
#include <memory>

namespace mmo
{
    namespace proto
    {
        class Project;
    }

    /// MCP (Model Context Protocol) server for game content management
    /// Implements JSON-RPC 2.0 protocol for AI assistant communication
    class McpServer
    {
    public:
        /// Tool handler function type
        using ToolHandler = std::function<nlohmann::json(const nlohmann::json &)>;

    public:
        /// Initializes the MCP server
        /// @param project The project containing game data
        explicit McpServer(proto::Project &project);

        /// Destructor
        ~McpServer();

    public:
        /// Starts the server (stdio mode)
        void Run();

        /// Processes a single JSON-RPC request
        /// @param request The JSON-RPC request
        /// @return JSON-RPC response
        nlohmann::json ProcessRequest(const nlohmann::json &request);

    private:
        /// Registers all available tools
        void RegisterTools();

        /// Handles the initialize request
        /// @param params Request parameters
        /// @return Response object
        nlohmann::json HandleInitialize(const nlohmann::json &params);

        /// Handles tools/list request
        /// @param params Request parameters
        /// @return Response object
        nlohmann::json HandleToolsList(const nlohmann::json &params);

        /// Handles tools/call request
        /// @param params Request parameters
        /// @return Response object
        nlohmann::json HandleToolsCall(const nlohmann::json &params);

        /// Creates a JSON-RPC error response
        /// @param id Request ID
        /// @param code Error code
        /// @param message Error message
        /// @return Error response object
        nlohmann::json CreateErrorResponse(const nlohmann::json &id, int code, const String &message);

    private:
        proto::Project &m_project;
        std::unique_ptr<ItemTools> m_itemTools;
        std::unique_ptr<ClassTools> m_classTools;
        std::unique_ptr<SpellTools> m_spellTools;
        std::map<String, ToolHandler> m_tools;
        bool m_initialized;
    };
}
