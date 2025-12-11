# Test script for MCP Item Server
Write-Host 'MCP Item Server Test Script' -ForegroundColor Cyan
Write-Host '============================\n' -ForegroundColor Cyan
Write-Host 'To test the server, run:' -ForegroundColor Green
Write-Host '  .\bin\Debug\mcp_item_server.exe' -ForegroundColor White
Write-Host ''
Write-Host 'Sample JSON-RPC requests:' -ForegroundColor Yellow
Write-Host ''
Write-Host '1. Initialize:'
Write-Host '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}' -ForegroundColor Gray
Write-Host ''
Write-Host '2. List tools:'
Write-Host '{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}' -ForegroundColor Gray
Write-Host ''
Write-Host '3. List first 5 items:'
Write-Host '{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"items_list\",\"arguments\":{\"limit\":5}}}' -ForegroundColor Gray
