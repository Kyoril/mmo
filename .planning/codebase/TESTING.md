# Testing

## Framework
- **Catch2** (v2, header-only) — located at `deps/catch/`
- **Include:** `#include "catch.hpp"` in test files, `#include "catch_with_main.hpp"` in main.cpp
- **No mocking framework** — manual mock classes used (e.g., `MockPlayerValidatorContext`)

## Test Structure

### Test Suites

| Directory | Target | Scope |
|-----------|--------|-------|
| `src/unit_tests/` | `unit_tests` | Core/shared library tests |
| `src/game_server_unit_tests/` | `game_server_unit_tests` | Game server logic tests |

### Core Unit Tests (`src/unit_tests/`)
24+ test files covering foundational and shared libraries:

| Test File | Subject |
|-----------|---------|
| `test_auth_protocol.cpp` | Auth packet serialization/deserialization |
| `test_game_protocol.cpp` | Game packet handling |
| `test_big_number.cpp` | Arbitrary precision arithmetic |
| `test_sha1.cpp` | SHA1 hashing |
| `test_crypt.cpp` | Encryption |
| `test_base64.cpp` | Base64 encoding |
| `test_binaryio.cpp` | Binary I/O framework |
| `test_simple_file_format.cpp` | SFF config parser |
| `test_field_map.cpp` | Object field maps |
| `test_game_unit_s.cpp` | Server-side unit object |
| `test_math.cpp` | Math utilities |
| `test_matrix.cpp` | Matrix operations |
| `test_terrain.cpp` | Terrain system |
| `test_box.cpp` | Bounding box |
| `test_aabb.cpp` | Axis-aligned bounding box |
| `test_clock.cpp` | Time functions |
| `test_console_var.cpp` | Console variables |
| `test_id_generator.cpp` | ID generation |
| `test_linear_set.cpp` | Linear set container |
| `test_allocation_map.cpp` | Allocation tracking |
| `test_assign_on_exit.cpp` | RAII assign utility |
| `test_erase_by_move.cpp` | Erase-by-move container operation |
| `test_non_copyable.cpp` | NonCopyable base class |
| `game_server/` | Sub-directory for game server tests within unit_tests |

### Game Server Unit Tests (`src/game_server_unit_tests/`)
7 test files focused on inventory and item systems:

| Test File | Subject |
|-----------|---------|
| `item_validator_test.cpp` | Item equip validation logic |
| `item_factory_test.cpp` | Item creation |
| `bag_manager_test.cpp` | Bag inventory management |
| `equipment_manager_test.cpp` | Equipment slot management |
| `slot_manager_test.cpp` | Inventory slot operations |
| `inventory_types_test.cpp` | Inventory type definitions |
| `main.cpp` | Test runner entry point |

## Test Style

### Test Syntax
Uses Catch2 `TEST_CASE` and `SECTION` macros with tags:
```cpp
TEST_CASE("AuthPacketCheck", "[auth_protocol]")
{
    // Arrange
    std::vector<char> buffer;
    io::VectorSink sink{ buffer };

    // Act
    auth::OutgoingPacket p{ sink };
    p.Start(opCode);
    p << io::write<uint32>(value);
    p.Finish();

    // Assert
    CHECK(buffer[0] == opCode);
}
```

### Mock Pattern
Manual mock classes implementing interfaces:
```cpp
class MockPlayerValidatorContext final : public IPlayerValidatorContext
{
public:
    MockPlayerValidatorContext()
        : m_level(1), m_isAlive(true), m_isInCombat(false), m_canDualWield(false) {}

    void SetLevel(const uint32 level) { m_level = level; }
    [[nodiscard]] uint32 GetLevel() const noexcept override { return m_level; }
    // ... other interface methods
private:
    uint32 m_level;
    bool m_isAlive;
    // ...
};
```

### Assertions
- **`CHECK()`** — non-fatal assertion (continues on failure)
- **`REQUIRE()`** — fatal assertion (stops test on failure)
- Prefer `CHECK` for most assertions

## Build & Run

### CMake Integration
```cmake
# In cmake/mmo_options.cmake
option(MMO_BUILD_TESTS "If checked, will try to test programs." ON)

# Tests registered with CTest
add_test(NAME unit_tests COMMAND unit_tests)
```

### Running Tests
```bash
# Via CMake/CTest
cd build
ctest

# Direct execution
./bin/Debug/unit_tests
./bin/Debug/game_server_unit_tests
```

### Build Task
VS Code task available: `"Build All"` → `cmake --build build`

## Coverage
- No explicit coverage tooling configured
- No CI/CD pipeline visible in the repository
- Tests build by default (`MMO_BUILD_TESTS` is ON)

## Test Dependencies
```cmake
# unit_tests links:
target_link_libraries(unit_tests
    base base64 log simple_file_format_hdrs binary_io_hdrs
    network_hdrs sql_wrapper mysql_wrapper auth_protocol
    game_protocol hpak hpak_v1_0 math game game_server
    ${OPENSSL_LIBRARIES})

# game_server_unit_tests links:
target_link_libraries(game_server_unit_tests
    base log game game_server proto_data ...)
```

## Gaps
- No integration tests for server-to-server communication
- No client-side rendering/UI tests
- No automated performance/load testing
- No mocking framework (manual mocks only)
- Test coverage concentrated on data structures and inventory — sparse coverage on combat, AI, spells, protocol encryption
