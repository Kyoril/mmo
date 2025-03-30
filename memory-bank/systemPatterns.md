# System Patterns

## System Architecture

The MMO project follows a distributed client-server architecture typical of MMORPGs, with several specialized server components working together to create a seamless game world.

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  Game Client │────▶│ Login Server │────▶│ Realm Server │
└─────────────┘     └─────────────┘     └─────────────┘
                                                ▲
                                                │
                                                |
                                       ┌─────────────┐
                                       │ World Server │
                                       └─────────────┘
```

### Component Responsibilities

1. **Login Server**
   - Authenticates user credentials
   - Manages account security
   - Provides realm list to client
   - Handles session management

2. **Realm Server**
   - Manages character data
   - Handles character creation/deletion
   - Stores character progression
   - Manages realm status information
   - Manages global features like global chat modes (party chat, guild chat) and the whole party or guild system
   - Acts as proxy server for world nodes which connect to the realm and host maps / actual gameplay

3. **World Server**
   - Simulates game world physics
   - Manages entity behavior (NPCs, monsters)
   - Processes player actions
   - Handles world state persistence
   - Manages instanced content (dungeons)
   - Can host multiple instances of certain maps
   - Connects to one or multiple realms (which allows cross realm gameplay like realm vs realm pvp combat in a battleground instance)

4. **Game Client**
   - Renders the game world
   - Processes user input
   - Communicates with servers
   - Manages local game state
   - Provides user interface

5. **Editor**
   - Creates and modifies game content
   - Manages world geometry
   - Configures entity properties
   - Designs quest and progression systems

## Key Technical Decisions

### Networking
- **Asynchronous I/O**: Using ASIO for non-blocking network operations
- **Custom Protocol**: Binary protocol optimized for game state synchronization
- **Encryption**: OpenSSL for secure communication
- **Session Management**: Stateful connections with heartbeat monitoring

### Data Storage
- **MySQL Database**: For persistent game data storage
- **In-Memory Caching**: For frequently accessed data
- **Custom File Formats**: Optimized formats for game assets (HPAK)
- **Custom binary file formats**: Custom chunked binary formats for storing assets related to the game client such as models, textures, levels etc.
- **Google protobuf files**: Protobuf files used by the editor, server and client to access static game data like spells, items, zones etc. (only stores data in a sequel-database-like format. No art related data inside, only references to such files). Used as a sort of "offline" database for static game data.
- **.cfg files (client only)**: Config files which are more like a batch file to execute console commands on the client. Used to store config values by writing a number of `SET <varname> <value>` lines to restore client game var values when restarting the client.

### Rendering
- **Multi-Platform Graphics**: Support for different rendering backends
- **Scene Graph**: Hierarchical organization of game world objects
- **Material System**: PBR (Physically Based Rendering) support

### Game Logic
- **Component-Based Design**: Entities composed of modular components
- **Scripting Support**: Lua for gameplay logic and content scripting
- **Event System**: For decoupled communication between systems

## Design Patterns

### Architectural Patterns
- **Model-View-Controller (MVC)**: Separation of game state, presentation, and control logic
- **Service-Oriented Architecture**: Server components as specialized services
- **Entity-Component System**: For flexible game object composition

### Creational Patterns
- **Factory Method**: For creating game entities and components
- **Singleton**: For system-wide managers and services
- **Object Pool**: For efficient reuse of frequently created/destroyed objects

### Structural Patterns
- **Composite**: For scene graph and UI hierarchy
- **Facade**: Simplified interfaces to complex subsystems
- **Proxy**: For network communication and resource loading

### Behavioral Patterns
- **Observer**: For event handling and UI updates
- **Command**: For action processing and input handling
- **State**: For entity behavior and game state management

## Critical Implementation Paths

### Authentication Flow
1. Client connects to Login Server
2. Credentials verified using SRP6 protocol
3. Session token generated and provided to client
4. Client uses token to connect to Realm Server

### Character Loading
1. Client requests character list from Realm Server
2. Realm Server retrieves character data from database
3. Character information sent to client for selection
4. Selected character data prepared for World Server

### World Simulation
1. World Server maintains spatial partitioning of entities
2. Updates entity states based on AI and physics
3. Processes player actions and updates game state
4. Broadcasts state changes to relevant clients

### Content Update
1. Launcher checks for updates
2. Downloads and verifies new content packages
3. Updates local game files
4. Launches updated client

## System Boundaries and Interfaces

### Client-Server Interface
- Binary protocol over TCP/IP
- Serialized game state updates
- Command processing and validation

### Database Interface
- ORM-like abstraction for database operations
- Connection pooling for efficient resource usage
- Transaction management for data consistency

### Asset Pipeline
- Content creation in editor
- Asset processing and optimization
- Packaging into efficient runtime formats
- Distribution through update system
