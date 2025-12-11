// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "proto_data/project.h"

#include <nlohmann/json.hpp>

namespace mmo
{
    /// Tools for managing items through MCP protocol
    class ItemTools
    {
    public:
        /// Initializes the item tools with a project reference
        /// @param project The project containing item data
        explicit ItemTools(proto::Project &project);

    public:
        /// Lists all items with optional filtering
        /// @param args JSON arguments containing optional filters
        /// @return JSON array of items
        nlohmann::json ListItems(const nlohmann::json &args);

        /// Gets detailed information about a specific item
        /// @param args JSON arguments containing item ID
        /// @return JSON object with item details
        nlohmann::json GetItemDetails(const nlohmann::json &args);

        /// Creates a new item
        /// @param args JSON arguments with item properties
        /// @return JSON object with the created item
        nlohmann::json CreateItem(const nlohmann::json &args);

        /// Updates an existing item
        /// @param args JSON arguments with item ID and properties to update
        /// @return JSON object with the updated item
        nlohmann::json UpdateItem(const nlohmann::json &args);

        /// Deletes an item
        /// @param args JSON arguments containing item ID
        /// @return JSON object with success status
        nlohmann::json DeleteItem(const nlohmann::json &args);

        /// Searches for items based on criteria
        /// @param args JSON arguments with search parameters
        /// @return JSON array of matching items
        nlohmann::json SearchItems(const nlohmann::json &args);

    private:
        /// Converts a protobuf ItemEntry to JSON
        /// @param entry The item entry to convert
        /// @param includeDetails Whether to include full details
        /// @return JSON representation of the item
        nlohmann::json ItemEntryToJson(const proto::ItemEntry &entry, bool includeDetails = false) const;

        /// Converts JSON to ItemEntry fields
        /// @param json The JSON data
        /// @param entry The entry to update
        void JsonToItemEntry(const nlohmann::json &json, proto::ItemEntry &entry);

        /// Gets the item class name as string
        /// @param itemClass The item class enum value
        /// @return String name of the class
        String GetItemClassName(uint32 itemClass) const;

        /// Gets the item quality name as string
        /// @param quality The quality enum value
        /// @return String name of the quality
        String GetItemQualityName(uint32 quality) const;

    private:
        proto::Project &m_project;
    };
}
