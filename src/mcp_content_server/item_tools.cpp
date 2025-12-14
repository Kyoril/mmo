// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "item_tools.h"

#include "game/item.h"
#include "log/default_log_levels.h"

#include <algorithm>

namespace mmo
{
    ItemTools::ItemTools(proto::Project &project)
        : m_project(project)
    {
    }

    nlohmann::json ItemTools::ListItems(const nlohmann::json &args)
    {
        nlohmann::json result = nlohmann::json::array();

        // Optional filters
        uint32 minLevel = 0;
        uint32 maxLevel = 1000;
        int32 itemClass = -1;
        int32 quality = -1;
        uint32 limit = 100;
        uint32 offset = 0;

        if (args.contains("minLevel"))
        {
            minLevel = args["minLevel"];
        }
        if (args.contains("maxLevel"))
        {
            maxLevel = args["maxLevel"];
        }
        if (args.contains("itemClass"))
        {
            itemClass = args["itemClass"];
        }
        if (args.contains("quality"))
        {
            quality = args["quality"];
        }
        if (args.contains("limit"))
        {
            limit = args["limit"];
        }
        if (args.contains("offset"))
        {
            offset = args["offset"];
        }

        const auto &items = m_project.items.getTemplates();
        uint32 count = 0;
        uint32 skipped = 0;

        for (int i = 0; i < items.entry_size() && count < limit; ++i)
        {
            const auto &item = items.entry(i);

            // Apply filters
            if (item.has_itemlevel())
            {
                if (item.itemlevel() < minLevel || item.itemlevel() > maxLevel)
                {
                    continue;
                }
            }

            if (itemClass >= 0 && item.has_itemclass())
            {
                if (static_cast<int32>(item.itemclass()) != itemClass)
                {
                    continue;
                }
            }

            if (quality >= 0 && item.has_quality())
            {
                if (static_cast<int32>(item.quality()) != quality)
                {
                    continue;
                }
            }

            // Apply offset
            if (skipped < offset)
            {
                ++skipped;
                continue;
            }

            result.push_back(ItemEntryToJson(item, false));
            ++count;
        }

        return result;
    }

    nlohmann::json ItemTools::GetItemDetails(const nlohmann::json &args)
    {
        if (!args.contains("id"))
        {
            throw std::runtime_error("Missing required parameter: id");
        }

        const uint32 itemId = args["id"];
        const auto *item = m_project.items.getById(itemId);

        if (!item)
        {
            throw std::runtime_error("Item not found: " + std::to_string(itemId));
        }

        return ItemEntryToJson(*item, true);
    }

    nlohmann::json ItemTools::CreateItem(const nlohmann::json &args)
    {
        if (!args.contains("name"))
        {
            throw std::runtime_error("Missing required parameter: name");
        }

        // Create new item
        auto *newItem = m_project.items.add();
        if (!newItem)
        {
            throw std::runtime_error("Failed to create new item");
        }

        // Set basic required fields
        newItem->set_name(args["name"].get<std::string>());

        // Apply all provided properties
        JsonToItemEntry(args, *newItem);

        ILOG("Created new item: " << newItem->name() << " (ID: " << newItem->id() << ")");

        return ItemEntryToJson(*newItem, true);
    }

    nlohmann::json ItemTools::UpdateItem(const nlohmann::json &args)
    {
        if (!args.contains("id"))
        {
            throw std::runtime_error("Missing required parameter: id");
        }

        const uint32 itemId = args["id"];
        auto *item = m_project.items.getById(itemId);

        if (!item)
        {
            throw std::runtime_error("Item not found: " + std::to_string(itemId));
        }

        // Update item properties
        JsonToItemEntry(args, *item);

        ILOG("Updated item: " << item->name() << " (ID: " << item->id() << ")");

        return ItemEntryToJson(*item, true);
    }

    nlohmann::json ItemTools::DeleteItem(const nlohmann::json &args)
    {
        if (!args.contains("id"))
        {
            throw std::runtime_error("Missing required parameter: id");
        }

        const uint32 itemId = args["id"];
        const auto *item = m_project.items.getById(itemId);

        if (!item)
        {
            throw std::runtime_error("Item not found: " + std::to_string(itemId));
        }

        const std::string itemName = item->name();
        m_project.items.remove(itemId);

        ILOG("Deleted item: " << itemName << " (ID: " << itemId << ")");

        nlohmann::json result;
        result["success"] = true;
        result["id"] = itemId;
        result["message"] = "Item deleted successfully";

        return result;
    }

    nlohmann::json ItemTools::SearchItems(const nlohmann::json &args)
    {
        nlohmann::json result = nlohmann::json::array();

        std::string searchQuery;
        if (args.contains("query"))
        {
            searchQuery = args["query"].get<std::string>();
            // Convert to lowercase for case-insensitive search
            std::transform(searchQuery.begin(), searchQuery.end(), searchQuery.begin(), ::tolower);
        }

        uint32 limit = 50;
        if (args.contains("limit"))
        {
            limit = args["limit"];
        }

        const auto &items = m_project.items.getTemplates();
        uint32 count = 0;

        for (int i = 0; i < items.entry_size() && count < limit; ++i)
        {
            const auto &item = items.entry(i);

            // Search in name
            if (!searchQuery.empty() && item.has_name())
            {
                std::string itemName = item.name();
                std::transform(itemName.begin(), itemName.end(), itemName.begin(), ::tolower);

                if (itemName.find(searchQuery) == std::string::npos)
                {
                    // Also check description if available
                    if (item.has_description())
                    {
                        std::string desc = item.description();
                        std::transform(desc.begin(), desc.end(), desc.begin(), ::tolower);
                        if (desc.find(searchQuery) == std::string::npos)
                        {
                            continue;
                        }
                    }
                    else
                    {
                        continue;
                    }
                }
            }

            result.push_back(ItemEntryToJson(item, false));
            ++count;
        }

        return result;
    }

    nlohmann::json ItemTools::ItemEntryToJson(const proto::ItemEntry &entry, bool includeDetails) const
    {
        nlohmann::json json;

        json["id"] = entry.id();
        json["name"] = entry.name();

        if (entry.has_description())
        {
            json["description"] = entry.description();
        }

        if (entry.has_itemclass())
        {
            json["itemClass"] = entry.itemclass();
            json["itemClassName"] = GetItemClassName(entry.itemclass());
        }

        if (entry.has_quality())
        {
            json["quality"] = entry.quality();
            json["qualityName"] = GetItemQualityName(entry.quality());
        }

        if (entry.has_itemlevel())
        {
            json["itemLevel"] = entry.itemlevel();
        }

        if (entry.has_requiredlevel())
        {
            json["requiredLevel"] = entry.requiredlevel();
        }

        if (includeDetails)
        {
            // Add detailed information
            if (entry.has_subclass())
            {
                json["subClass"] = entry.subclass();
            }
            if (entry.has_inventorytype())
            {
                json["inventoryType"] = entry.inventorytype();
            }
            if (entry.has_displayid())
            {
                json["displayId"] = entry.displayid();
            }
            if (entry.has_flags())
            {
                json["flags"] = entry.flags();
            }
            if (entry.has_buycount())
            {
                json["buyCount"] = entry.buycount();
            }
            if (entry.has_buyprice())
            {
                json["buyPrice"] = entry.buyprice();
            }
            if (entry.has_sellprice())
            {
                json["sellPrice"] = entry.sellprice();
            }
            if (entry.has_allowedclasses())
            {
                json["allowedClasses"] = entry.allowedclasses();
            }
            if (entry.has_allowedraces())
            {
                json["allowedRaces"] = entry.allowedraces();
            }
            if (entry.has_maxcount())
            {
                json["maxCount"] = entry.maxcount();
            }
            if (entry.has_maxstack())
            {
                json["maxStack"] = entry.maxstack();
            }
            if (entry.has_containerslots())
            {
                json["containerSlots"] = entry.containerslots();
            }
            if (entry.has_bonding())
            {
                json["bonding"] = entry.bonding();
            }
            if (entry.has_armor())
            {
                json["armor"] = entry.armor();
            }
            if (entry.has_durability())
            {
                json["durability"] = entry.durability();
            }

            // Stats
            if (entry.stats_size() > 0)
            {
                nlohmann::json stats = nlohmann::json::array();
                for (int i = 0; i < entry.stats_size(); ++i)
                {
                    const auto &stat = entry.stats(i);
                    nlohmann::json statJson;
                    statJson["type"] = stat.type();
                    statJson["value"] = stat.value();
                    stats.push_back(statJson);
                }
                json["stats"] = stats;
            }

            // Damage
            if (entry.has_damage())
            {
                nlohmann::json damage;
                damage["minDamage"] = entry.damage().mindmg();
                damage["maxDamage"] = entry.damage().maxdmg();
                if (entry.damage().has_type())
                {
                    damage["type"] = entry.damage().type();
                }
                json["damage"] = damage;
            }

            // Delay (attack speed)
            if (entry.has_delay())
            {
                json["delay"] = entry.delay();
            }

            // Spells
            if (entry.spells_size() > 0)
            {
                nlohmann::json spells = nlohmann::json::array();
                for (int i = 0; i < entry.spells_size(); ++i)
                {
                    const auto &spell = entry.spells(i);
                    nlohmann::json spellJson;
                    spellJson["spellId"] = spell.spell();
                    if (spell.has_trigger())
                    {
                        spellJson["trigger"] = spell.trigger();
                    }
                    if (spell.has_charges())
                    {
                        spellJson["charges"] = spell.charges();
                    }
                    if (spell.has_procrate())
                    {
                        spellJson["procRate"] = spell.procrate();
                    }
                    if (spell.has_cooldown())
                    {
                        spellJson["cooldown"] = spell.cooldown();
                    }
                    spells.push_back(spellJson);
                }
                json["spells"] = spells;
            }

            // Sockets
            if (entry.sockets_size() > 0)
            {
                nlohmann::json sockets = nlohmann::json::array();
                for (int i = 0; i < entry.sockets_size(); ++i)
                {
                    const auto &socket = entry.sockets(i);
                    nlohmann::json socketJson;
                    socketJson["color"] = socket.color();
                    socketJson["content"] = socket.content();
                    sockets.push_back(socketJson);
                }
                json["sockets"] = sockets;
            }
        }

        return json;
    }

    void ItemTools::JsonToItemEntry(const nlohmann::json &json, proto::ItemEntry &entry)
    {
        // Update fields from JSON if they exist
        if (json.contains("name"))
        {
            entry.set_name(json["name"].get<std::string>());
        }
        if (json.contains("description"))
        {
            entry.set_description(json["description"].get<std::string>());
        }
        if (json.contains("itemClass"))
        {
            entry.set_itemclass(json["itemClass"]);
        }
        if (json.contains("subClass"))
        {
            entry.set_subclass(json["subClass"]);
        }
        if (json.contains("quality"))
        {
            entry.set_quality(json["quality"]);
        }
        if (json.contains("itemLevel"))
        {
            entry.set_itemlevel(json["itemLevel"]);
        }
        if (json.contains("requiredLevel"))
        {
            entry.set_requiredlevel(json["requiredLevel"]);
        }
        if (json.contains("inventoryType"))
        {
            entry.set_inventorytype(json["inventoryType"]);
        }
        if (json.contains("displayId"))
        {
            entry.set_displayid(json["displayId"]);
        }
        if (json.contains("flags"))
        {
            entry.set_flags(json["flags"]);
        }
        if (json.contains("buyCount"))
        {
            entry.set_buycount(json["buyCount"]);
        }
        if (json.contains("buyPrice"))
        {
            entry.set_buyprice(json["buyPrice"]);
        }
        if (json.contains("sellPrice"))
        {
            entry.set_sellprice(json["sellPrice"]);
        }
        if (json.contains("allowedClasses"))
        {
            entry.set_allowedclasses(json["allowedClasses"]);
        }
        if (json.contains("allowedRaces"))
        {
            entry.set_allowedraces(json["allowedRaces"]);
        }
        if (json.contains("maxCount"))
        {
            entry.set_maxcount(json["maxCount"]);
        }
        if (json.contains("maxStack"))
        {
            entry.set_maxstack(json["maxStack"]);
        }
        if (json.contains("containerSlots"))
        {
            entry.set_containerslots(json["containerSlots"]);
        }
        if (json.contains("bonding"))
        {
            entry.set_bonding(json["bonding"]);
        }
        if (json.contains("armor"))
        {
            entry.set_armor(json["armor"]);
        }
        if (json.contains("durability"))
        {
            entry.set_durability(json["durability"]);
        }
        if (json.contains("delay"))
        {
            entry.set_delay(json["delay"]);
        }

        // Handle damage
        if (json.contains("damage"))
        {
            auto *damage = entry.mutable_damage();
            if (json["damage"].contains("minDamage"))
            {
                damage->set_mindmg(json["damage"]["minDamage"]);
            }
            if (json["damage"].contains("maxDamage"))            {
                damage->set_maxdmg(json["damage"]["maxDamage"]);
            }
            if (json["damage"].contains("type"))
            {
                damage->set_type(json["damage"]["type"]);
            }
        }

        // Handle spells
        if (json.contains("spells"))
        {
            // Clear existing spells
            entry.clear_spells();
            
            const auto& spellsArray = json["spells"];
            for (const auto& spellJson : spellsArray)
            {
                auto* spell = entry.add_spells();
                
                if (spellJson.contains("spellId"))
                {
                    spell->set_spell(spellJson["spellId"]);
                }
                if (spellJson.contains("trigger"))
                {
                    spell->set_trigger(spellJson["trigger"]);
                }
                if (spellJson.contains("charges"))
                {
                    spell->set_charges(spellJson["charges"]);
                }
                if (spellJson.contains("procRate"))
                {
                    spell->set_procrate(spellJson["procRate"]);
                }
                if (spellJson.contains("cooldown"))
                {
                    spell->set_cooldown(spellJson["cooldown"]);
                }
            }
        }
    }

    String ItemTools::GetItemClassName(uint32 itemClass) const
    {
        static const std::vector<String> classNames = {
            "Consumable", "Container", "Weapon", "Gem", "Armor", "Reagent",
            "Projectile", "Trade Goods", "Generic", "Recipe", "Money",
            "Quiver", "Quest", "Key", "Permanent", "Junk"};

        if (itemClass < classNames.size())
        {
            return classNames[itemClass];
        }

        return "Unknown";
    }

    String ItemTools::GetItemQualityName(uint32 quality) const
    {
        static const std::vector<String> qualityNames = {
            "Poor", "Common", "Uncommon", "Rare", "Epic", "Legendary"};

        if (quality < qualityNames.size())
        {
            return qualityNames[quality];
        }

        return "Unknown";
    }
}
