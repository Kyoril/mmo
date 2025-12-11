// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "configuration.h"

#include "simple_file_format/sff_write.h"
#include "simple_file_format/sff_write_file.h"
#include "simple_file_format/sff_write_table.h"
#include "simple_file_format/sff_read_tree.h"
#include "simple_file_format/sff_load_file.h"
#include "log/default_log_levels.h"

#include <fstream>

namespace mmo
{
    const uint32 Configuration::McpContentServerConfigVersion = 0x01;

    Configuration::Configuration()
        : projectPath("data/realm"), useStdio(true), port(3000)
    {
    }

    bool Configuration::Load(const String &fileName)
    {
        typedef String::const_iterator Iterator;
        typedef sff::read::tree::Table<Iterator> Table;

        Table global;
        std::string fileContent;

        std::ifstream file(fileName, std::ios::binary);
        if (!file)
        {
            if (Save(fileName))
            {
                ILOG("Saved default settings as " << fileName);
            }
            else
            {
                ELOG("Could not save default settings as " << fileName);
            }

            return false;
        }

        try
        {
            sff::loadTableFromFile(global, fileContent, file);

            uint32 fileVersion = 0;
            if (!global.tryGetInteger("version", fileVersion) ||
                fileVersion != McpContentServerConfigVersion)
            {
                file.close();

                if (Save(fileName + ".updated"))
                {
                    ILOG("Saved updated settings as " << fileName << ".updated");
                }

                return false;
            }

            if (const Table *const mcpTable = global.getTable("mcp"))
            {
                projectPath = mcpTable->getString("projectPath", projectPath);
                useStdio = mcpTable->getInteger("useStdio", useStdio ? 1 : 0) != 0;
                port = mcpTable->getInteger("port", port);
            }
        }
        catch (const sff::read::ParseException<Iterator> &e)
        {
            const auto line = std::count<Iterator>(fileContent.begin(), e.position.begin, '\n');
            ELOG("Error in config: " << e.what());
            ELOG("Line " << (line + 1) << ": " << e.position.str());
            return false;
        }

        return true;
    }

    bool Configuration::Save(const String &fileName)
    {
        std::ofstream file(fileName.c_str());
        if (!file)
        {
            ELOG("Could not open config file for writing: " << fileName);
            return false;
        }

        typedef char Char;
        sff::write::File<Char> global(file, sff::write::MultiLine);

        // Save file version
        global.addKey("version", McpContentServerConfigVersion);
        global.writer.newLine();

        {
            sff::write::Table<Char> mcpTable(global, "mcp", sff::write::MultiLine);
            mcpTable.addKey("projectPath", projectPath);
            mcpTable.addKey("useStdio", useStdio ? 1 : 0);
            mcpTable.addKey("port", port);
            mcpTable.Finish();
        }

        return true;
    }
}
