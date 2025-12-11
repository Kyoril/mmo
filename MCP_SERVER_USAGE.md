# MCP Content Server - Usage Examples

## Testing the Server

### 1. Start the server:
```powershell
.\bin\Debug\mcp_content_server.exe
```

### 2. Send JSON-RPC requests (one per line):

#### Initialize (required first):
```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}
```

#### List available tools:
```json
{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}
```

#### List items (first 5):
```json
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"items_list","arguments":{"limit":5}}}
```

#### Get item details (item ID 1):
```json
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"items_get","arguments":{"id":1}}}
```

#### Search for items:
```json
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"items_search","arguments":{"query":"sword","limit":10}}}
```

#### Create a new item:
```json
{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"items_create","arguments":{"name":"Test Sword","description":"A test weapon","itemClass":2,"quality":2,"itemLevel":10,"requiredLevel":5}}}
```

## Configuration

Ensure `config/mcp_content_server.cfg` exists with valid paths:
```
mcp:
{
    version: 1
    projectPath: "data/realm"
    useStdio: 1
    port: 3000
}
```

## Integration with Claude Desktop

Add to Claude Desktop config:
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

Config location:
- Windows: `%APPDATA%\Claude\claude_desktop_config.json`
- macOS: `~/Library/Application Support/Claude/claude_desktop_config.json`
