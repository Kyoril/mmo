// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "mcp_server.h"

#include "log/default_log_levels.h"

#include <iostream>
#include <sstream>

namespace mmo
{
    McpServer::McpServer(proto::Project &project)
        : m_project(project), m_itemTools(std::make_unique<ItemTools>(project)), m_spellTools(std::make_unique<SpellTools>(project)), m_classTools(std::make_unique<ClassTools>(project)), m_initialized(false)
    {
        RegisterTools();
    }

    McpServer::~McpServer()
    {
    }

    void McpServer::Run()
    {
        ILOG("MCP Content Server started. Waiting for requests on stdin...");
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (line.empty())
            {
                continue;
            }

            try
            {
                // Parse JSON-RPC request
                nlohmann::json request = nlohmann::json::parse(line);

                // Process request
                nlohmann::json response = ProcessRequest(request);

                // Send response (only if not empty - notifications don't get responses)
                if (!response.empty())
                {
                    std::cout << response.dump() << std::endl;
                }
            }
            catch (const std::exception &e)
            {
                ELOG("Error processing request: " << e.what());

                // Send error response
                nlohmann::json errorResponse;
                errorResponse["jsonrpc"] = "2.0";
                errorResponse["id"] = nullptr;
                errorResponse["error"] = {
                    {"code", -32700},
                    {"message", "Parse error"},
                    {"data", e.what()}};

                std::cout << errorResponse.dump() << std::endl;
            }
        }

        ILOG("MCP Item Server shutting down.");
    }

    nlohmann::json McpServer::ProcessRequest(const nlohmann::json &request)
    {
        // Validate JSON-RPC 2.0 structure
        if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0")
        {
            return CreateErrorResponse(nullptr, -32600, "Invalid Request: missing or invalid jsonrpc field");
        }

        if (!request.contains("method"))
        {
            nlohmann::json id = request.contains("id") ? request["id"] : nullptr;
            return CreateErrorResponse(id, -32600, "Invalid Request: missing method");
        }

        const std::string method = request["method"];
        ILOG("Received method: " << method);
        
        nlohmann::json id = request.contains("id") ? request["id"] : nullptr;
        nlohmann::json params = request.contains("params") ? request["params"] : nlohmann::json::object();

        nlohmann::json result;

        try
        {
            // Handle notifications (methods starting with "notifications/" don't need responses)
            if (method.find("notifications/") == 0)
            {
                // Notifications are one-way messages, don't send a response
                // Just log and return empty response (will not be sent)
                ILOG("Received notification: " << method);
                return nlohmann::json();  // Empty response means no reply
            }

            // Route to appropriate handler
            if (method == "initialize")
            {
                result = HandleInitialize(params);
                m_initialized = true;
            }
            else if (method == "tools/list")
            {
                result = HandleToolsList(params);
            }
            else if (method == "tools/call")
            {
                if (!m_initialized)
                {
                    return CreateErrorResponse(id, -32002, "Server not initialized. Call initialize first.");
                }
                result = HandleToolsCall(params);
            }
            else
            {
                return CreateErrorResponse(id, -32601, "Method not found: " + method);
            }

            // Create successful response
            nlohmann::json response;
            response["jsonrpc"] = "2.0";
            response["id"] = id;
            response["result"] = result;

            return response;
        }
        catch (const std::exception &e)
        {
            return CreateErrorResponse(id, -32603, String("Internal error: ") + e.what());
        }
    }

    void McpServer::RegisterTools()
    {
        // Register item management tools
        m_tools["items_list"] = [this](const nlohmann::json &args)
        {
            return m_itemTools->ListItems(args);
        };

        m_tools["items_get"] = [this](const nlohmann::json &args)
        {
            return m_itemTools->GetItemDetails(args);
        };

        m_tools["items_create"] = [this](const nlohmann::json &args)
        {
            return m_itemTools->CreateItem(args);
        };

        m_tools["items_update"] = [this](const nlohmann::json &args)
        {
            return m_itemTools->UpdateItem(args);
        };

        m_tools["items_delete"] = [this](const nlohmann::json &args)
        {
            return m_itemTools->DeleteItem(args);
        };

        m_tools["items_search"] = [this](const nlohmann::json &args)
        {
            return m_itemTools->SearchItems(args);
        };

        // Register spell management tools
        m_tools["spells_list"] = [this](const nlohmann::json &args)
        {
            return m_spellTools->ListSpells(args);
        };

        m_tools["spells_get"] = [this](const nlohmann::json &args)
        {
            return m_spellTools->GetSpellDetails(args);
        };

        m_tools["spells_create"] = [this](const nlohmann::json &args)
        {
            return m_spellTools->CreateSpell(args);
        };

        m_tools["spells_update"] = [this](const nlohmann::json &args)
        {
            return m_spellTools->UpdateSpell(args);
        };

        m_tools["spells_delete"] = [this](const nlohmann::json &args)
        {
            return m_spellTools->DeleteSpell(args);
        };

        m_tools["spells_search"] = [this](const nlohmann::json &args)
        {
            return m_spellTools->SearchSpells(args);
        };

        // Register class management tools
        m_tools["classes_list"] = [this](const nlohmann::json &args)
        {
            return m_classTools->ListClasses(args);
        };

        m_tools["classes_get"] = [this](const nlohmann::json &args)
        {
            return m_classTools->GetClassDetails(args);
        };

        m_tools["classes_create"] = [this](const nlohmann::json &args)
        {
            return m_classTools->CreateClass(args);
        };

        m_tools["classes_update"] = [this](const nlohmann::json &args)
        {
            return m_classTools->UpdateClass(args);
        };

        m_tools["classes_delete"] = [this](const nlohmann::json &args)
        {
            return m_classTools->DeleteClass(args);
        };

        m_tools["classes_search"] = [this](const nlohmann::json &args)
        {
            return m_classTools->SearchClasses(args);
        };

        m_tools["classes_add_spell"] = [this](const nlohmann::json &args)
        {
            return m_classTools->AddClassSpell(args);
        };

        m_tools["classes_remove_spell"] = [this](const nlohmann::json &args)
        {
            return m_classTools->RemoveClassSpell(args);
        };
    }

    nlohmann::json McpServer::HandleInitialize(const nlohmann::json &params)
    {
        nlohmann::json result;
        result["protocolVersion"] = "2024-11-05";
        result["capabilities"] = {
            {"tools", {
                {"listChanged", true}
            }}
        };
        result["serverInfo"] = {
            {"name", "mmo-content-server"},
            {"version", "1.0.0"}};

        ILOG("MCP Server initialized with tools capability (listChanged: true)");

        return result;
    }

    nlohmann::json McpServer::HandleToolsList(const nlohmann::json &params)
    {
        ILOG("Listing tools...");
        nlohmann::json tools = nlohmann::json::array();

        // items_list tool
        tools.push_back({{"name", "items_list"},
                         {"description", "Lists all items with optional filtering by level, class, quality, etc."},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"minLevel", {{"type", "number"}, {"description", "Minimum item level"}}}, {"maxLevel", {{"type", "number"}, {"description", "Maximum item level"}}}, {"itemClass", {{"type", "number"}, {"description", "Item class filter (0=Consumable, 2=Weapon, 4=Armor, etc.)"}}}, {"quality", {{"type", "number"}, {"description", "Item quality (0=Poor, 1=Common, 2=Uncommon, 3=Rare, 4=Epic, 5=Legendary)"}}}, {"limit", {{"type", "number"}, {"description", "Maximum number of items to return (default: 100)"}}}, {"offset", {{"type", "number"}, {"description", "Number of items to skip (for pagination)"}}}}}}}});

        // items_get tool
        tools.push_back({{"name", "items_get"},
                         {"description", "Gets detailed information about a specific item by ID"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"id", {{"type", "number"}, {"description", "The item ID"}}}}}, {"required", nlohmann::json::array({"id"})}}}});

        // items_create tool
        tools.push_back({{"name", "items_create"},
                         {"description", "Creates a new item with the specified properties"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"name", {{"type", "string"}, {"description", "Item name"}}}, {"description", {{"type", "string"}, {"description", "Item description"}}}, {"itemClass", {{"type", "number"}, {"description", "Item class"}}}, {"subClass", {{"type", "number"}, {"description", "Item subclass"}}}, {"quality", {{"type", "number"}, {"description", "Item quality"}}}, {"itemLevel", {{"type", "number"}, {"description", "Item level"}}}, {"requiredLevel", {{"type", "number"}, {"description", "Required level to use"}}}, {"inventoryType", {{"type", "number"}, {"description", "Inventory slot type"}}}, {"buyPrice", {{"type", "number"}, {"description", "Vendor buy price in copper"}}}, {"sellPrice", {{"type", "number"}, {"description", "Vendor sell price in copper"}}}, {"maxStack", {{"type", "number"}, {"description", "Maximum stack size"}}}, {"bonding", {{"type", "number"}, {"description", "Binding type (0=None, 1=OnPickup, 2=OnEquip, 3=OnUse)"}}}}}, {"required", nlohmann::json::array({"name"})}}}});

        // items_update tool
        tools.push_back({{"name", "items_update"},
                         {"description", "Updates an existing item's properties"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"id", {{"type", "number"}, {"description", "The item ID to update"}}}, {"name", {{"type", "string"}, {"description", "Item name"}}}, {"description", {{"type", "string"}, {"description", "Item description"}}}, {"itemClass", {{"type", "number"}, {"description", "Item class"}}}, {"subClass", {{"type", "number"}, {"description", "Item subclass"}}}, {"quality", {{"type", "number"}, {"description", "Item quality"}}}, {"itemLevel", {{"type", "number"}, {"description", "Item level"}}}, {"requiredLevel", {{"type", "number"}, {"description", "Required level to use"}}}, {"buyPrice", {{"type", "number"}, {"description", "Vendor buy price in copper"}}}, {"sellPrice", {{"type", "number"}, {"description", "Vendor sell price in copper"}}}, {"maxStack", {{"type", "number"}, {"description", "Maximum stack size"}}}, {"spells", {{"type", "array"}, {"description", "Array of spell effects for the item"}, {"items", {{"type", "object"}, {"properties", {{"spellId", {{"type", "number"}, {"description", "The spell ID"}}}, {"trigger", {{"type", "number"}, {"description", "Spell trigger type (0=OnUse, 1=OnEquip, 2=OnHit, etc.)"}}}, {"charges", {{"type", "number"}, {"description", "Number of charges (0=unlimited)"}}}, {"procRate", {{"type", "number"}, {"description", "Proc rate percentage"}}}, {"cooldown", {{"type", "number"}, {"description", "Cooldown in milliseconds"}}}}}}}}}}}, {"required", nlohmann::json::array({"id"})}}}});

        // items_delete tool
        tools.push_back({{"name", "items_delete"},
                         {"description", "Deletes an item from the project"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"id", {{"type", "number"}, {"description", "The item ID to delete"}}}}}, {"required", nlohmann::json::array({"id"})}}}});

        // items_search tool
        tools.push_back({{"name", "items_search"},
                         {"description", "Searches for items by name or description"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"query", {{"type", "string"}, {"description", "Search query string"}}}, {"limit", {{"type", "number"}, {"description", "Maximum number of results (default: 50)"}}}}}, {"required", nlohmann::json::array({"query"})}}}});

        // spells_list tool
        tools.push_back({{"name", "spells_list"},
                         {"description", "Lists all spells with optional filtering by level, school, power type, etc."},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"minLevel", {{"type", "number"}, {"description", "Minimum spell level"}}}, {"maxLevel", {{"type", "number"}, {"description", "Maximum spell level"}}}, {"spellSchool", {{"type", "number"}, {"description", "Spell school filter (0=Physical, 1=Holy, 2=Fire, 3=Nature, 4=Frost, 5=Shadow, 6=Arcane)"}}}, {"powerType", {{"type", "number"}, {"description", "Power type (0=Mana, 1=Rage, 2=Energy, 3=Health)"}}}, {"limit", {{"type", "number"}, {"description", "Maximum number of spells to return (default: 100)"}}}, {"offset", {{"type", "number"}, {"description", "Number of spells to skip (for pagination)"}}}}}}}});

        // spells_get tool
        tools.push_back({{"name", "spells_get"},
                         {"description", "Gets detailed information about a specific spell by ID"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"id", {{"type", "number"}, {"description", "The spell ID"}}}}}, {"required", nlohmann::json::array({"id"})}}}});

        // spells_create tool
        tools.push_back({{"name", "spells_create"},
                         {"description", "Creates a new spell with the specified properties"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"name", {{"type", "string"}, {"description", "Spell name"}}}, {"description", {{"type", "string"}, {"description", "Spell description"}}}, {"spellSchool", {{"type", "number"}, {"description", "Spell school (0-6)"}}}, {"powerType", {{"type", "number"}, {"description", "Power type (0=Mana, 1=Rage, 2=Energy, 3=Health)"}}}, {"cost", {{"type", "number"}, {"description", "Spell cost"}}}, {"castTime", {{"type", "number"}, {"description", "Cast time in milliseconds"}}}, {"cooldown", {{"type", "number"}, {"description", "Cooldown in milliseconds"}}}, {"duration", {{"type", "number"}, {"description", "Duration in milliseconds"}}}, {"spellLevel", {{"type", "number"}, {"description", "Spell level"}}}, {"baseLevel", {{"type", "number"}, {"description", "Base level required"}}}, {"maxLevel", {{"type", "number"}, {"description", "Maximum level"}}}, {"rangeType", {{"type", "number"}, {"description", "Range type ID"}}}, {"effects", {{"type", "array"}, {"description", "Array of spell effects"}, {"items", {{"type", "object"}}}}}}}, {"required", nlohmann::json::array({"name"})}}}});

        // spells_update tool
        tools.push_back({{"name", "spells_update"},
                         {"description", "Updates an existing spell's properties"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"id", {{"type", "number"}, {"description", "The spell ID to update"}}}, {"name", {{"type", "string"}, {"description", "Spell name"}}}, {"description", {{"type", "string"}, {"description", "Spell description"}}}, {"cost", {{"type", "number"}, {"description", "Spell cost"}}}, {"castTime", {{"type", "number"}, {"description", "Cast time in milliseconds"}}}, {"cooldown", {{"type", "number"}, {"description", "Cooldown in milliseconds"}}}, {"duration", {{"type", "number"}, {"description", "Duration in milliseconds"}}}}}, {"required", nlohmann::json::array({"id"})}}}});

        // spells_delete tool
        tools.push_back({{"name", "spells_delete"},
                         {"description", "Deletes a spell from the project"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"id", {{"type", "number"}, {"description", "The spell ID to delete"}}}}}, {"required", nlohmann::json::array({"id"})}}}});

        // spells_search tool
        tools.push_back({{"name", "spells_search"},
                         {"description", "Searches for spells by name or description"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"query", {{"type", "string"}, {"description", "Search query string"}}}, {"limit", {{"type", "number"}, {"description", "Maximum number of results (default: 50)"}}}}}, {"required", nlohmann::json::array({"query"})}}}});

        // classes_list tool
        tools.push_back({{"name", "classes_list"},
                         {"description", "Lists all character classes with optional filtering"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"powerType", {{"type", "number"}, {"description", "Power type filter (0=Mana, 1=Rage, 2=Energy)"}}}, {"limit", {{"type", "number"}, {"description", "Maximum number of classes to return (default: 100)"}}}, {"offset", {{"type", "number"}, {"description", "Number of classes to skip (for pagination)"}}}}}}}});

        // classes_get tool
        tools.push_back({{"name", "classes_get"},
                         {"description", "Gets detailed information about a specific class by ID"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"id", {{"type", "number"}, {"description", "The class ID"}}}}}, {"required", nlohmann::json::array({"id"})}}}});

        // classes_create tool
        tools.push_back({{"name", "classes_create"},
                         {"description", "Creates a new character class with the specified properties"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"name", {{"type", "string"}, {"description", "Class name"}}}, {"internalName", {{"type", "string"}, {"description", "Internal class name"}}}, {"powerType", {{"type", "number"}, {"description", "Power type (0=Mana, 1=Rage, 2=Energy)"}}}, {"spellFamily", {{"type", "number"}, {"description", "Spell family ID"}}}, {"flags", {{"type", "number"}, {"description", "Class flags"}}}, {"attackPowerPerLevel", {{"type", "number"}, {"description", "Attack power gained per level"}}}, {"attackPowerOffset", {{"type", "number"}, {"description", "Base attack power offset"}}}}}, {"required", nlohmann::json::array({"name"})}}}});

        // classes_update tool
        tools.push_back({{"name", "classes_update"},
                         {"description", "Updates an existing class's properties including stats, XP, and regen values"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"id", {{"type", "number"}, {"description", "The class ID to update"}}}, {"name", {{"type", "string"}, {"description", "Class name"}}}, {"internalName", {{"type", "string"}, {"description", "Internal class name"}}}, {"powerType", {{"type", "number"}, {"description", "Power type"}}}, {"spellFamily", {{"type", "number"}, {"description", "Spell family ID"}}}, {"attackPowerPerLevel", {{"type", "number"}, {"description", "Attack power per level"}}}, {"attackPowerOffset", {{"type", "number"}, {"description", "Base attack power offset"}}}, {"baseManaRegenPerTick", {{"type", "number"}, {"description", "Base mana regen per tick"}}}, {"spiritPerManaRegen", {{"type", "number"}, {"description", "Spirit to mana regen conversion"}}}, {"healthRegenPerTick", {{"type", "number"}, {"description", "Health regen per tick"}}}, {"spiritPerHealthRegen", {{"type", "number"}, {"description", "Spirit to health regen conversion"}}}, {"updateBaseValues", {{"type", "object"}, {"description", "Update stats for a specific level (provide level and stat properties)"}}}, {"addBaseValues", {{"type", "object"}, {"description", "Add stats for a new level (provide stat properties)"}}}, {"updateXpToNextLevel", {{"type", "object"}, {"description", "Update XP for level (provide level and xp properties)"}}}, {"addXpToNextLevel", {{"type", "number"}, {"description", "Add XP requirement for new level"}}}}}, {"required", nlohmann::json::array({"id"})}}}});

        // classes_delete tool
        tools.push_back({{"name", "classes_delete"},
                         {"description", "Deletes a class from the project"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"id", {{"type", "number"}, {"description", "The class ID to delete"}}}}}, {"required", nlohmann::json::array({"id"})}}}});

        // classes_search tool
        tools.push_back({{"name", "classes_search"},
                         {"description", "Searches for classes by name or internal name"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"query", {{"type", "string"}, {"description", "Search query string"}}}, {"limit", {{"type", "number"}, {"description", "Maximum number of results (default: 50)"}}}}}, {"required", nlohmann::json::array({"query"})}}}});

        // classes_add_spell tool
        tools.push_back({{"name", "classes_add_spell"},
                         {"description", "Adds a spell to a class at a specific level"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"classId", {{"type", "number"}, {"description", "The class ID"}}}, {"spellId", {{"type", "number"}, {"description", "The spell ID to add"}}}, {"level", {{"type", "number"}, {"description", "Level at which the spell is learned"}}}}}, {"required", nlohmann::json::array({"classId", "spellId", "level"})}}}});

        // classes_remove_spell tool
        tools.push_back({{"name", "classes_remove_spell"},
                         {"description", "Removes a spell from a class"},
                         {"inputSchema", {{"type", "object"}, {"properties", {{"classId", {{"type", "number"}, {"description", "The class ID"}}}, {"spellId", {{"type", "number"}, {"description", "The spell ID to remove"}}}}}, {"required", nlohmann::json::array({"classId", "spellId"})}}}});

        nlohmann::json result;
        result["tools"] = tools;

        ILOG("Returning " << tools.size() << " tools");
        return result;
    }

    nlohmann::json McpServer::HandleToolsCall(const nlohmann::json &params)
    {
        if (!params.contains("name"))
        {
            throw std::runtime_error("Missing required parameter: name");
        }

        const std::string toolName = params["name"];
        const nlohmann::json arguments = params.value("arguments", nlohmann::json::object());

        // Find and execute tool
        auto it = m_tools.find(toolName);
        if (it == m_tools.end())
        {
            throw std::runtime_error("Unknown tool: " + toolName);
        }

        nlohmann::json toolResult = it->second(arguments);

        // Wrap result in MCP content format
        nlohmann::json result;
        result["content"] = nlohmann::json::array({{{"type", "text"},
                                                    {"text", toolResult.dump(2)}}});

        return result;
    }

    nlohmann::json McpServer::CreateErrorResponse(const nlohmann::json &id, int code, const String &message)
    {
        nlohmann::json response;
        response["jsonrpc"] = "2.0";
        response["id"] = id;
        response["error"] = {
            {"code", code},
            {"message", message}};

        ELOG("Error response: " << message << " (code: " << code << ")");

        return response;
    }
}
