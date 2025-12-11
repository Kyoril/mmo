# MCP Content Server

A Model Context Protocol (MCP) server that provides AI assistants with tools to manage game items in the MMO project.

## Overview

This server implements the [Model Context Protocol](https://modelcontextprotocol.io/), allowing AI assistants like Claude to interact with the game's item database through a standardized JSON-RPC 2.0 interface.

## Features

The MCP Content Server provides the following tools:

### 1. **items_list** - List All Items
Lists items with optional filtering capabilities.

**Parameters:**
- `minLevel` (optional): Minimum item level
- `maxLevel` (optional): Maximum item level
- `itemClass` (optional): Filter by item class (0=Consumable, 2=Weapon, 4=Armor, etc.)
- `quality` (optional): Filter by quality (0=Poor, 1=Common, 2=Uncommon, 3=Rare, 4=Epic, 5=Legendary)
- `limit` (optional): Maximum number of items to return (default: 100)
- `offset` (optional): Number of items to skip for pagination

### 2. **items_get** - Get Item Details
Retrieves detailed information about a specific item.

**Parameters:**
- `id` (required): The item ID

### 3. **items_create** - Create New Item
Creates a new item with specified properties.

**Parameters:**
- `name` (required): Item name
- `description` (optional): Item description
- `itemClass` (optional): Item class
- `subClass` (optional): Item subclass
- `quality` (optional): Item quality
- `itemLevel` (optional): Item level
- `requiredLevel` (optional): Required level to use
- `inventoryType` (optional): Inventory slot type
- `buyPrice` (optional): Vendor buy price in copper
- `sellPrice` (optional): Vendor sell price in copper
- `maxStack` (optional): Maximum stack size
- `bonding` (optional): Binding type

### 4. **items_update** - Update Existing Item
Updates an existing item's properties.

**Parameters:**
- `id` (required): The item ID to update
- Other parameters same as `items_create` (all optional)

### 5. **items_delete** - Delete Item
Deletes an item from the project.

**Parameters:**
- `id` (required): The item ID to delete

### 6. **items_search** - Search Items
Searches for items by name or description.

**Parameters:**
- `query` (required): Search query string
- `limit` (optional): Maximum number of results (default: 50)

## Configuration

The server is configured via `config/mcp_content_server.cfg`:

```
mcp:
{
    version: 1
    projectPath: "data/realm"
    useStdio: 1
    port: 3000
}
```

### Configuration Options

- **projectPath**: Path to the game data directory containing item protobuf files
- **useStdio**: Set to 1 for MCP stdio mode, 0 for TCP mode (only stdio currently supported)
- **port**: TCP port number (only used if useStdio is 0)

## Building

The MCP Content Server is built as part of the main MMO project:

```powershell
cmake --build build --target mcp_content_server
```

## Running

### Standalone Mode
```powershell
.\bin\Debug\mcp_content_server.exe
```

### With Claude Desktop (MCP Client)

Add to your Claude Desktop configuration file:

**Windows**: `%APPDATA%\Claude\claude_desktop_config.json`
**macOS**: `~/Library/Application Support/Claude/claude_desktop_config.json`

```json
{
  "mcpServers": {
    "mmo-content": {
      "command": "F:\\mmo\\bin\\Debug\\mcp_content_server.exe",
      "args": []
    }
  }
}
```

## Item Enumerations

### Item Classes
- 0: Consumable
- 1: Container
- 2: Weapon
- 3: Gem
- 4: Armor
- 5: Reagent
- 6: Projectile
- 7: Trade Goods
- 8: Generic
- 9: Recipe
- 10: Money
- 11: Quiver
- 12: Quest
- 13: Key
- 14: Permanent
- 15: Junk

### Item Quality
- 0: Poor (Gray)
- 1: Common (White)
- 2: Uncommon (Green)
- 3: Rare (Blue)
- 4: Epic (Purple)
- 5: Legendary (Orange)

### Binding Types
- 0: No Binding
- 1: Bind When Picked Up
- 2: Bind When Equipped
- 3: Bind When Used

## Architecture

The server consists of several components:

- **main.cpp**: Entry point, handles initialization and shutdown
- **mcp_server.h/cpp**: Implements JSON-RPC 2.0 protocol and MCP message handling
- **item_tools.h/cpp**: Implements item management operations
- **configuration.h/cpp**: Configuration file management

## Protocol Details

The server implements JSON-RPC 2.0 over stdio:

1. **Initialize**: Must be called first to establish protocol version
2. **tools/list**: Returns available tools and their schemas
3. **tools/call**: Executes a specific tool with provided arguments

All data modifications are saved to disk when the server shuts down gracefully.

## Error Handling

The server returns standard JSON-RPC 2.0 error codes:

- `-32700`: Parse error
- `-32600`: Invalid Request
- `-32601`: Method not found
- `-32602`: Invalid params
- `-32603`: Internal error
- `-32002`: Server not initialized

## Dependencies

- Protocol Buffers (libprotobuf)
- nlohmann/json
- Base library (logging, types)
- Proto_data library (game data management)
- Simple File Format library (configuration)

## Thread Safety

The current implementation is single-threaded and processes requests sequentially via stdio.

## Future Enhancements

- TCP/IP mode support
- Batch operations
- Item duplication/cloning
- Item set management
- Spell and stat validation
- Transaction support (rollback on error)
- Multi-user access control

## License

Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
