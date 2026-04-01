# Code Conventions

## Code Style

### Formatting
- **Braces:** Allman style — each opening and closing brace on its own line
- **If blocks:** Always use braces, even for single-line bodies (no single-line if blocks)
- **Indentation:** Tabs for indentation
- **Blank lines:** Between methods, between logical sections

### Example
```cpp
if (condition)
{
    DoSomething();
}
else
{
    DoOtherThing();
}
```

## Naming Conventions

### Types
- **Classes/Structs:** `PascalCase` (e.g., `Program`, `PlayerManager`, `GameUnitS`, `NonCopyable`)
- **Enums:** `PascalCase` for type, `PascalCase` for values (e.g., `ObjectTypeId::Unit`)
- **Enum namespaces:** Sometimes `snake_case` namespace with `Type` enum inside (e.g., `unit_state::Type::Stunned`)

### Members
- **Member variables:** `m_camelCase` prefix (e.g., `m_logFile`, `m_ioService`, `m_clients`)
- **Static members:** `PascalCase` (e.g., `Program::ShouldRestart`)
- **Local variables:** `camelCase` (e.g., `logOptions`, `testString`)
- **Constants:** `PascalCase` or `camelCase` depending on context

### Functions
- **Methods:** `PascalCase` for public (e.g., `GetLevel()`, `IsAlive()`, `Start()`)
- **Private methods:** Also `PascalCase`
- **Free functions:** `camelCase` in anonymous namespaces (e.g., `generateLogFileName`)
- **Getters:** `GetPropertyName()` or `IsPropertyName()` pattern
- **Setters:** `SetPropertyName()` pattern

### Files
- **Source files:** `snake_case.cpp` and `snake_case.h` (e.g., `player_manager.cpp`, `game_unit_s.h`)
- **Server-side objects:** `game_*_s.h` suffix (e.g., `game_player_s.h`)
- **Client-side objects:** `game_*_c.h` suffix (e.g., `game_player_c.h`)
- **Test files:** `test_*.cpp` (e.g., `test_auth_protocol.cpp`) or `*_test.cpp` (e.g., `item_validator_test.cpp`)

### Namespaces
- **Root namespace:** `mmo`
- **Sub-namespaces:** `mmo::web`, `mmo::net::http`, `mmo::auth`, `mmo::io`
- **Enum pseudo-namespaces:** `namespace unit_state { enum Type { ... }; }` pattern

## Common Patterns

### Ownership & Lifecycle
- **`std::shared_ptr`** for shared ownership (e.g., network connections, clients)
- **`std::unique_ptr`** for exclusive ownership
- **`NonCopyable`** base class to prevent copying of resource-managing objects
- **RAII** for resource management throughout
- **`scoped_connection`** for signal lifetime management (auto-disconnect on destruction)

### Observer Pattern (Signals)
Custom signal/slot library in `src/shared/base/signal.h`:
```cpp
// Producer
signal<void(const LogEntry&)> m_signal;

// Consumer
scoped_connection conn = m_signal.connect([](const LogEntry& e) { ... });
```

### Asynchronous I/O
All servers use the same pattern:
```cpp
asio::io_service ioService;          // Main event loop
asio::io_service dbService;          // DB-dedicated service
auto dbWork = std::make_shared<asio::io_context::work>(dbService);
// ... setup ...
ioService.run();                     // Blocks until shutdown
```

### Protocol Packets
Binary serialization using `io::write<T>` and `io::read<T>`:
```cpp
// Writing
OutgoingPacket p{ sink };
p.Start(opCode);
p << io::write<uint32>(value);
p << io::write_dynamic_range<uint8>(stringValue);
p.Finish();

// Reading
incomingPacket >> io::read<uint32>(value);
incomingPacket >> io::read_container<uint8>(stringValue);
```

### Server Program Pattern
Each server follows the same structure:
```cpp
class Program : public NonCopyable
{
public:
    int32 run();           // Main entry point
    static bool ShouldRestart;
private:
    std::ofstream m_logFile;
};
```

### Game Object Hierarchy (Server-side)
```
GameObjectS → GameUnitS → GamePlayerS
                        → GameCreatureS
           → GameItemS → GameBagS
           → GameWorldObjectS
```

### AI State Machine
Creature AI uses a state pattern:
```
CreatureAI → CreatureAiState (base)
           → CreatureAiIdleState
           → CreatureAiCombatState
           → CreatureAiDeathState
           → CreatureAiResetState
           → CreatureAiPrepareState
```

### Inventory Command Pattern
Item operations use the command pattern:
```
InventoryCommand (base) → AddItemCommand
                        → RemoveItemCommand
                        → SwapItemsCommand
```
With `InventoryCommandFactory`, `InventoryCommandLogger`, and `InventoryUnitOfWork` for transactional operations.

### Configuration
Loaded via custom SFF (Simple File Format):
```cpp
Configuration config;
if (!config.load("config/login_server.cfg"))
{
    return 1;
}
```

## Error Handling
- **Assertions:** `ASSERT(x)` macro wrapping `assert()` — used for invariants
- **`VERIFY(x)`** — asserts in debug, evaluates in release
- **`FATAL(x, msg)`** — assertion with message (debug), plain assert (release)
- **`UNREACHABLE()`** — marks impossible code paths
- **Return codes:** `int32` return from `Program::run()` (0 = success, 1 = failure)
- **Logging:** `DLOG()`, `WLOG()`, `ELOG()` macros for debug, warning, error output
- **No exceptions** — `SIMPLE_NO_EXCEPTIONS` defined, signals use error codes instead

## Documentation
- **Doxygen format** for header file documentation
- **Copyright headers:** `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` on all source files
- **`#pragma once`** used universally for include guards
- **`[[nodiscard]]`** and `noexcept` used on modern interfaces (e.g., `IPlayerValidatorContext`)

## Type System
- Custom type aliases in `src/shared/base/typedefs.h`:
  - `uint8`, `uint16`, `uint32`, `uint64` (unsigned)
  - `int8`, `int16`, `int32`, `int64` (signed)
  - `String` (= `std::string`)
  - `ObjectGuid` (= `uint64`), `DatabaseId` (= `uint64`), `GameTime` (= `uint64`)
  - `Identifier` (= `uint32`), `PacketId` (= `uint8`), `PacketSize` (= `uint32`)
