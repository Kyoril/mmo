// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
    /// Configuration for the MCP Content Server
    class Configuration
    {
    public:
        static const uint32 McpContentServerConfigVersion;

    public:
        /// Path to the project data directory
        String projectPath;

        /// Port to listen on for MCP connections (default: stdio)
        bool useStdio;

        /// TCP port if not using stdio
        uint16 port;

    public:
        /// Initializes a new instance of the Configuration class with default values
        explicit Configuration();

    public:
        /// Loads the configuration from a file
        /// @param fileName The configuration file path
        /// @return true on success, false on failure
        bool Load(const String &fileName);

        /// Saves the configuration to a file
        /// @param fileName The configuration file path
        /// @return true on success, false on failure
        bool Save(const String &fileName);
    };
}
