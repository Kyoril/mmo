# Technical Context

## Technologies Used

### Programming Languages
- **C++**: Primary language for all core systems (client, servers, tools)
- **Lua**: Scripting language for UI and may be used for advanced creature and content scripting on the server
- **SQL**: Database queries and schema management
- **Protobuf**: Data definition for static game data

### Libraries and Frameworks
- **ASIO**: Asynchronous I/O for networking
- **OpenSSL**: Security and encryption
- **MySQL**: Database backend
- **Lua/Luabind**: Scripting integration
- **ImGui**: UI framework for tools and editor
- **GLFW**: Cross-platform window management for OpenGL render backends (OpenGL impl only)
- **Catch2**: Unit testing framework
- **ASSIMP**: 3D model importing (editor only)
- **Metal**: Graphics API for macOS (mac os only)
- **Direct3D 11**: Graphics API for Windows

### Build System
- **CMake**: Cross-platform build configuration
- **Git**: Version control
- **GitHub Actions**: CI/CD pipeline

### File Formats
- **HPAK**: Custom package format for game assets
- **Protobuf**: Static game data definitions
- **Custom chunked versioned binary formats**: For models, textures, and world data
- **.cfg**: Client configuration files

## Development Setup

### Windows Development
- **Visual Studio 2017+**: Primary IDE
- **Windows 10/11**: Target OS for client
- **Linux (Ubuntu / Debian)**: Target OS for servers (also used when creating docker images from server applications to host in docker compose)
- **Docker**: Server images generated as docker container images to host a whole server environment using docker compose on a remote server
- **CMake**: Build system
- **x64 architecture**: Required for all builds on windows / linux
- **ARM64 architecture**: Required when building for Mac OS
- **OpenSSL**: Required for security features like SRP6A authentication protocol
- **MySQL**: Required for database functionality

### Linux Development
- **GCC 13+**: Compiler
- **CMake**: Build system
- **x64 architecture**: Required for all builds
- **OpenSSL**: Required for security features
- **MySQL**: Required for database functionality

### macOS Development
- **Xcode**: IDE
- **Apple Clang**: Compiler
- **CMake**: Build system
- **Metal**: Graphics API

### Docker Support
- Containerized server deployment
- Separate containers for login, realm, and world servers
- Docker Compose for orchestration

## Technical Constraints

### Performance Requirements
- **Client**: 60+ FPS on target hardware
- **Server**: Support for 2000 concurrent players per realm server, support for 100+ players in an area on the same world instance on a world node
- **Database**: Low-latency queries for character and world data
- **Network**: Efficient protocol to minimize bandwidth usage

### Security Considerations
- **Authentication**: Secure credential verification (SRP6)
- **Communication**: Encrypted client-server communication
- **Data Integrity**: Validation of client actions to prevent cheating
- **Account Security**: Protection against unauthorized access

### Scalability Factors
- **Horizontal Scaling**: Multiple world servers per realm
- **Vertical Scaling**: Efficient resource usage on server hardware
- **Database Scaling**: Connection pooling and query optimization
- **Content Scaling**: Efficient asset management and loading

### Cross-Platform Compatibility
- **Client**: Windows primary, macOS secondary
- **Servers**: Windows and Linux
- **Tools**: Platform-specific implementations where necessary

## Build Configuration

### CMake Options
- **MMO_BUILD_CLIENT**: Build the game client (ON by default on Windows)
- **MMO_BUILD_EDITOR**: Build the editor (OFF by default)
- **MMO_BUILD_TOOLS**: Build additional tools (OFF by default)
- **MMO_BUILD_LAUNCHER**: Build the launcher (OFF by default)
- **MMO_BUILD_TESTS**: Build unit tests (ON by default)
- **MMO_WITH_DEV_COMMANDS**: Enable developer commands (OFF by default)
- **MMO_WITH_RELEASE_ASSERTS**: Enable assertions in release builds (OFF by default)

### Build Targets
- **login_server**: Authentication server
- **realm_server**: Character and realm management server
- **world_server**: Game world simulation server
- **mmo_client**: Game client
- **mmo_edit**: World editor
- **launcher**: Game launcher and updater
- **hpak_tool**: Asset packaging utility
- **update_compiler**: Update package creation tool

## Deployment Architecture

### Development Environment
- Local server instances
- Direct database connections
- Debug builds with additional logging

### Testing Environment
- Containerized server deployment
- Isolated database instances
- Release builds with assertions enabled

### Production Environment
- Distributed server deployment
- Optimized database configuration
- Release builds with minimal overhead

## Tool Usage Patterns

### Asset Pipeline
1. Create raw assets in external tools
2. Import into editor for configuration
3. Process into optimized formats
4. Package into HPAK files
5. Distribute through update system

### Database Management
1. Define schema in SQL scripts
2. Apply updates through versioned scripts
3. Access through ORM-like abstraction layer
4. Cache frequently accessed data in memory

### Development Workflow
1. Implement features in feature branches
2. Test with unit tests and integration tests
3. Review code through pull requests
4. Merge to main branch
5. Build and deploy through CI/CD pipeline
