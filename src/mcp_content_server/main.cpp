// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "configuration.h"
#include "mcp_server.h"

#include "base/filesystem.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"
#include "proto_data/project.h"

#include <iostream>
#include <memory>

#ifdef _WIN32
#include <Windows.h>
#endif

/// Procedural entry point of the application.
int main(int argc, char *argv[])
{
    // Add cout to the list of log output streams
    mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry &entry)
                                       { mmo::printLogEntry(std::cerr, entry, mmo::g_DefaultConsoleLogOptions); });

    // Load configuration
    mmo::Configuration config;
    if (!config.Load("./config/mcp_content_server.cfg"))
    {
        ELOG("Failed to load configuration file");
        return 1;
    }

    ILOG("MCP Content Server");
    ILOG("Project path: " << config.projectPath); // Load project data
    mmo::proto::Project project;
    if (!project.load(config.projectPath))
    {
        ELOG("Failed to load project data from: " << config.projectPath);
        return 1;
    }

    ILOG("Loaded " << project.items.count() << " items from project");

    // Create and run MCP server
    try
    {
        mmo::McpServer server(project);

        if (config.useStdio)
        {
            // Run in stdio mode for MCP communication
            server.Run();
        }
        else
        {
            ELOG("TCP mode not yet implemented. Please use stdio mode.");
            return 1;
        }

        // Save project data before exiting
        ILOG("Saving project data...");
        if (!project.save(config.projectPath))
        {
            ELOG("Failed to save project data");
            return 1;
        }

        ILOG("Project data saved successfully");
    }
    catch (const std::exception &e)
    {
        ELOG("Fatal error: " << e.what());
        return 1;
    }

    return 0;
}
