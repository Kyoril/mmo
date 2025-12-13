# MCP Content Server - Model Context Protocol server for game content management
project(mcp_content_server)

# Add source files
set(MCP_CONTENT_SERVER_SOURCES
	main.cpp
	configuration.cpp
	configuration.h
	mcp_server.cpp
	mcp_server.h
	item_tools.cpp
	item_tools.h
	spell_tools.cpp
	spell_tools.h
)

# Create the executable
add_executable(mcp_content_server ${MCP_CONTENT_SERVER_SOURCES})

# Link against required libraries
target_link_libraries(mcp_content_server
	base
	log
	simple_file_format_hdrs
	proto_data
	virtual_dir
	libprotobuf
	nlohmann_json::nlohmann_json
)

# Set target properties
set_property(TARGET mcp_content_server PROPERTY FOLDER "servers")

# Install target
install(TARGETS mcp_content_server RUNTIME DESTINATION bin)
