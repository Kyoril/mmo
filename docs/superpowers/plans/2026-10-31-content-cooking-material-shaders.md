# Content Cooking, Part 1: Material Shaders — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every nightly rebuilds D3D11 and Metal (precompiled `.metallib`) shaders for all materials from their graphs and ships them through a cook overlay; editors and development clients detect stale shaders by hash, and release clients abort on a missing variant.

**Architecture:** The material graph and HLSL generator move out of the editor into shared libraries (`graphics`, new `material_graph`). Freshness is recorded per platform as a `ShaderBuildInfo` in a new `.hmat` chunk and compared on load. A new `content_cook` library plus `mmo_cook` tool cook shaders per platform in the nightly's Windows and macOS jobs; a Linux `cook` job assembles them into a `client-overlay.zip` that the deployer lays over its data checkout before `update_compiler`.

**Tech Stack:** C++17, CMake (`add_lib`, `add_exe`, `mmo_add_test`), Catch2, D3DCompiler (Windows), glslang + SPIRV-Cross + Xcode `metal`/`metallib` (macOS), GitHub Actions, Python 3 (deployer only).

**Spec:** [docs/superpowers/specs/2026-10-31-content-cooking-design.md](../specs/2026-10-31-content-cooking-design.md) — sections 5.1–5.6, 5.8, 5.9 and order-of-work steps 1–4. Stripping editor-only chunks (spec step 5) and per-page entity files (spec step 6) are Part 2, a separate plan.

## Global Constraints

- C++17, Allman braces, tabs, `m_camelCase` members, `PascalCase` methods, `snake_case` files, `#pragma once`, Doxygen on public members, header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- No exceptions (`SIMPLE_NO_EXCEPTIONS`); report errors through return values and `ELOG`/`WLOG`.
- Work on branch `feature/content-cooking`; `/ship` per finished task group; never push.
- Every task must be safe to ship on its own: anything merged to `develop` reaches players the next morning. The release-client abort and `MMO_MATERIAL_RUNTIME_COMPILE=OFF` in the nightly only arrive in Task 12, after cooked shaders ship (Task 11). The Metal device keeps accepting source blobs.
- New and moved libraries must keep building on headless Linux (the server gate builds them). Graphics-compiler-specific code is behind `WIN32` (D3D11) or `APPLE` (Metal).
- Cooking writes deterministic bytes for everything it serializes itself (sorted iteration, no timestamps). Compiled shader bytes may differ across toolchain updates (accepted, spec section 6).
- The development client never writes into `data/client` outside `Cache/`. Shader cache: `Cache/Shaders/` under the data directory.
- Metal output targets arm64, macOS 11.0, MSL 2.3 (`-std=macos-metal2.3 -mmacosx-version-min=11.0`).
- Failure policy: a variant that fails to compile is left empty, never filled from older repository shaders; a material that cannot be cooked at all for a platform keeps the repository's shaders of that platform. Each case is a warning, not a failure.

## Review Focus

1. **A material whose graph references a `.hmf` that no longer exists** — cooking and dev-client compile must warn and treat the material as uncookable, not crash. Test in Task 4 (`hash with missing function`) and Task 9 (`cook reports missing function`).
2. **A legacy `.hmat` without a `SBLD` chunk (every file in the repo today)** — loads, counts as stale wherever a graph exists, and still renders with its existing D3D11 shaders in a client built with runtime compile off. Test in Task 5 and Task 7.
3. **Editing only node positions** must not change the graph hash, and re-saving an unchanged material must not change the file bytes. Test in Task 4 and Task 6.
4. **Renderer code that probes a shader variant and falls back when it is null** — the release abort must not fire there. Audit in Task 7, Step 1.
5. **Two materials with the same file name in different folders** must not collide in the shader cache or the cook output. Test in Task 7 (`cache key distinguishes folders`) and Task 9.

---

### Task 1: Portable HLSL generator and platform-aware shader compilers

**Files:**
- Move: `src/shared/graphics_d3d11/material_compiler_d3d11.{h,cpp}` → `src/shared/graphics/material_compiler_hlsl.{h,cpp}`, class `MaterialCompilerD3D11` → `MaterialCompilerHlsl`
- Modify: `src/shared/graphics/shader_compiler.h`, `src/shared/graphics/material_compiler.cpp:68-160`, `src/shared/graphics_d3d11/shader_compiler_d3d11.h`, `src/shared/graphics_d3d11/graphics_device_d3d11.cpp:1397`, `src/shared/graphics_metal/graphics_device_metal.mm:848`, `src/shared/graphics_null/*` (its shader compiler, if any), `src/metal_material_baker/main.cpp`, `src/metal_material_baker/CMakeLists.txt`
- Test: `src/tests/graphics_tests/material_compiler_platform_tests.cpp`

**Interfaces:**
- Produces: `virtual std::string_view ShaderCompiler::GetPlatform() const = 0;` returning a `shader_platform::*` id (`D3D11`, `METAL`; null compiler returns `OPENGL`). `MaterialCompiler::Compile(Material&, ShaderCompiler&)` stores every variant under `shaderCompiler.GetPlatform()` via `Material::SetVertexShaderCode(platform, type, code)` / `SetPixelShaderCode(platform, type, code)`, not under the current platform. A failed variant is stored as empty. `MaterialCompilerHlsl` in `graphics`.

- [ ] **Step 1: Write the failing test** `TEST_CASE("material compiler stores shaders under the shader compiler's platform", "[graphics]")`: a fake `ShaderCompiler` with `GetPlatform()` = `"TEST"` that returns the input text as bytes; compile an empty `MaterialCompilerHlsl` into a `Material`; `REQUIRE(!material.GetVertexShaderCode("TEST", VertexShaderType::Default).empty())` and `REQUIRE(material.GetVertexShaderCode(shader_platform::GetCurrentPlatform(), VertexShaderType::Default).empty())`. Second case: the fake fails variant `VertexShaderType::UI` (the enum value the UI variant uses) → that slot is empty, `Default` is not.
- [ ] **Step 2: Run** `cmake --build build --config Debug -t graphics_tests && bin/Debug/graphics_tests "[graphics]"` — expected FAIL (no `GetPlatform`, no `MaterialCompilerHlsl`).
- [ ] **Step 3: Implement.** `git mv` the generator, rename the class, update its two factory sites (both devices return `MaterialCompilerHlsl`), add `GetPlatform()` to every `ShaderCompiler`, switch `MaterialCompiler::Compile` to the platform overloads. The baker drops its copy of the generator source and links `graphics`.
- [ ] **Step 4: Run** the test (PASS) and the full fast gate build (`powershell -File tools/gate/verify.ps1 -Tier fast`); the Linux gate step proves `graphics` still builds headless.
- [ ] **Step 5: Commit** `refactor(graphics): portable HLSL material generator, compilers name their platform`.

### Task 2: Shared `material_graph` library without ImGui

**Files:**
- Move: `src/mmo_edit/editors/material_editor/{material_graph,material_node,material_function,material_function_manager}.{h,cpp}` and `node_editor/{node_registry,node_type_info,link_query_result}.*` → `src/shared/material_graph/`
- Create: `src/shared/material_graph/CMakeLists.txt` (`add_lib(material_graph)`, links `graphics assets binary_io base log`), `src/shared/material_graph/node_layout_store.h`
- Create: `src/mmo_edit/editors/material_editor/imgui_node_layout_store.{h,cpp}`
- Modify: `src/shared/CMakeLists.txt`, `src/mmo_edit/CMakeLists.txt`, editor includes, `src/metal_material_baker/CMakeLists.txt` (drop the ImGui sources, link `material_graph`)
- Test: `src/tests/material_graph_tests/{CMakeLists.txt,material_graph_tests.cpp}`, one line in `src/tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `class INodeLayoutStore { virtual bool GetNodeBounds(uint32 nodeId, float& x, float& y, float& w, float& h) const = 0; virtual void SetNodeBounds(uint32 nodeId, float x, float y, float w, float h) = 0; };`
  - `MaterialGraph::SetLayoutStore(INodeLayoutStore* store)`. `GraphNode::Serialize`/`Deserialize` use it in place of `ed::` calls. Without a store they write zeros and skip restoring, exactly as the "no editor context" branches do today.
  - Node colours become `constexpr uint32 PackNodeColor(float r, float g, float b, float a)` with ImGui's `IM_COL32` byte order (R in the low byte). Values stay bit-identical to today's `ImColor(...)` results.
  - `ImGuiNodeLayoutStore` (editor) wraps the current `ed::GetNodePosition` / `EditorState` code moved out of `material_node.cpp:342-430`.

- [ ] **Step 1: Write the failing test** `TEST_CASE("graph round-trips and compiles without an editor", "[material_graph]")`: build a graph in code (a `ConstFloatNode` linked into the root `MaterialNode`'s base colour), `Serialize`, `Deserialize` into a fresh graph with no layout store, `Compile` with `MaterialCompilerHlsl`, `REQUIRE(compiler.GetPixelShaderCode(PixelShaderType::Forward).find("BaseColor") != npos)` (use the identifier the generator emits for base colour). Second case: `PackNodeColor(0.57f, 0.88f, 0.29f, 0.25f)` equals the literal that `ImU32(ImColor(0.57f, 0.88f, 0.29f, 0.25f))` produces. Compute it once with ImGui before the move and paste it as a hex constant into the test, since the test target does not link ImGui.
- [ ] **Step 2: Run** `bin/Debug/material_graph_tests` — FAIL (target missing).
- [ ] **Step 3: Implement** the move, the layout store and the colour constants; the editor installs an `ImGuiNodeLayoutStore` on each graph it opens.
- [ ] **Step 4: Run** the tests (PASS); open and save a material in `mmo_edit` and check node positions survive a reload; fast gate.
- [ ] **Step 5: Commit** `refactor(material_graph): material graph as a shared library without ImGui`.

### Task 3: Shader build info in `.hmat` (format v0.9)

**Files:**
- Create: `src/shared/graphics/shader_build_info.{h,cpp}`
- Modify: `src/shared/graphics/material.{h,cpp}`, `src/shared/scene_graph/material_serializer.{h,cpp}`
- Test: `src/tests/scene_graph_tests/material_build_info_tests.cpp`

**Interfaces:**
- Produces:
  - `struct FunctionHash { String path; SHA1Hash hash; };`
  - `struct ShaderBuildInfo { SHA1Hash graphHash; uint32 generatorVersion; uint32 backendVersion; std::vector<FunctionHash> functions; };` with `functions` kept sorted by path.
  - `namespace material_compiler_version { constexpr uint32 Generator = 1; uint32 GetBackendVersion(std::string_view platform); }`, which returns `1` for `D3D11` and `METAL`, `0` otherwise.
  - `Material::SetShaderBuildInfo(std::string_view platform, ShaderBuildInfo)`, `const ShaderBuildInfo* Material::GetShaderBuildInfo(std::string_view platform) const`, `Material::SetGraphHash(const SHA1Hash&)`, `std::optional<SHA1Hash> Material::GetGraphHash() const`.
  - Chunk `'SBLD'` (`MakeChunkMagic('DLBS')`, matching the file's reversed-literal convention), written from `material_version::Version_0_9 = 0x0900`. Payload: `uint8 hasGraphHash`, `[20] graphHash`, `uint8 platformCount`, then per platform (sorted by id): `id (uint8 length + bytes)`, `graphHash[20]`, `generatorVersion u32`, `backendVersion u32`, `u16 functionCount`, then per function `path (u16 length + bytes)` and `hash[20]`.
  - The serializer still writes v0.7/v0.8 when there is no build info and no graph hash, so files without them stay byte-identical.

- [ ] **Step 1: Write the failing tests** `"build info round-trips per platform"` (two platforms, functions out of order → read back sorted, equal values), `"legacy material has no build info"` (export without build info → bytes equal to a v0.8 export, `GetShaderBuildInfo("D3D11") == nullptr`).
- [ ] **Step 2: Run** `bin/Debug/scene_graph_tests "[material]"` — FAIL.
- [ ] **Step 3: Implement** the struct, the chunk reader (registered for v0.9+) and the writer.
- [ ] **Step 4: Run** — PASS; fast gate.
- [ ] **Step 5: Commit** `feat(materials): record per-platform shader build info in .hmat (v0.9)`.

### Task 4: Graph content hash and function hashes

**Files:**
- Create: `src/shared/material_graph/material_graph_hash.{h,cpp}`
- Modify: `src/shared/material_graph/material_function_manager.cpp` (read `'FHSH'`), the material-function save path in `src/mmo_edit/editors/material_editor/` (write `'FHSH'`; find it via the `INPS` chunk writer), `src/shared/material_graph/material_node.{h,cpp}` (layout-free serialization flag)
- Test: `src/tests/material_graph_tests/material_graph_hash_tests.cpp`

**Interfaces:**
- Consumes: `FunctionHash`, `SHA1Hash`.
- Produces:
  - `SHA1Hash ComputeGraphHash(const MaterialGraph& graph)`: SHA1 over the graph serialized with layout omitted (positions and sizes not written) and node and pin ids in their stored order.
  - `bool CollectFunctionHashes(const MaterialGraph& graph, std::vector<FunctionHash>& out)`: walks `MaterialFunctionNode`s recursively through nested function graphs, reads each function's stored `'FHSH'` hash (computed with `ComputeGraphHash` on the function's graph when its file is saved), outputs the list sorted by path, and returns false (with `WLOG`) when a function file is missing or has no hash.
  - `bool IsShaderBuildCurrent(const ShaderBuildInfo& info, std::string_view platform, const SHA1Hash& graphHash, const std::vector<FunctionHash>& currentFunctions)`: true iff hash, `Generator`, backend version and function list all match.

- [ ] **Step 1: Write the failing tests:**
  - `"moving nodes keeps the graph hash"`: the hash is equal before and after changing layout through a test `INodeLayoutStore`.
  - `"changing a constant changes the graph hash"`.
  - `"function edit makes build info stale"`: `IsShaderBuildCurrent` is true with the recorded functions and false after one function hash changes.
  - `"generator version bump makes build info stale"`.
  - `"hash with missing function"`: `CollectFunctionHashes` returns false.
- [ ] **Step 2: Run** `bin/Debug/material_graph_tests "[hash]"` — FAIL.
- [ ] **Step 3: Implement** the three functions and the `'FHSH'` chunk (`[20]` SHA1) in `.hmf` save and load.
- [ ] **Step 4: Run** — PASS; fast gate.
- [ ] **Step 5: Commit** `feat(material_graph): layout-free graph hash and material function hashes`.

### Task 5: Compile one platform with build info

**Files:**
- Create: `src/shared/material_graph/material_shader_build.{h,cpp}`
- Test: `src/tests/material_graph_tests/material_shader_build_tests.cpp`

**Interfaces:**
- Consumes: Tasks 1, 3, 4.
- Produces:
  - `struct MaterialShaderBuildResult { bool cookable; std::vector<String> failedVariants; };`
  - `MaterialShaderBuildResult BuildMaterialShaders(Material& material, const MaterialGraph& graph, ShaderCompiler& compiler)`:
    - Generates and compiles every variant into a scratch material.
    - On success, replaces only `compiler.GetPlatform()`'s shaders in `material` with the fresh set (empty slots for failed variants) and sets that platform's `ShaderBuildInfo` and the material's graph hash.
    - `cookable == false` (material untouched) when the graph has no root or a function is missing.
    - Texture-slot mismatch between scratch and stored material, the check `metal_material_baker` does today, also makes it uncookable.
  - `bool ReadMaterialGraphChunk(std::span<const uint8> hmatBytes, std::vector<uint8>& outGraphChunk)`: finds the `GRPH` chunk without a full material load.
  - `bool LoadMaterialGraph(std::span<const uint8> graphChunk, MaterialGraph& outGraph)`.

- [ ] **Step 1: Write the failing tests:**
  - `"build replaces only the compiler's platform"`: a material holding `D3D11` shaders, built with a fake `"METAL"` compiler, keeps its `D3D11` bytes and gets `METAL` shaders plus build info.
  - `"failed variant stays empty"`.
  - `"graph without root is uncookable"`.
  - `"legacy material is stale"`: no build info means `IsShaderBuildCurrent` is false.
- [ ] **Step 2: Run** — FAIL.
- [ ] **Step 3: Implement**, moving the logic out of `metal_material_baker/main.cpp:200-260`.
- [ ] **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** `feat(material_graph): build one platform's shaders with build info`.

### Task 6: Editor uses build info on save and load

**Files:**
- Modify: `src/mmo_edit/editors/material_editor/material_editor_instance.cpp:720-790` (compile/save), `material_editor.cpp:170-200` (rebuild-all job) and `:465-490` (new material), material-instance load path in the editor
- Test: `src/tests/material_graph_tests/material_shader_build_tests.cpp` (determinism case)

**Interfaces:**
- Consumes: `BuildMaterialShaders`, `IsShaderBuildCurrent`, `GraphicsDevice::Get().CreateShaderCompiler()` (current platform).
- Produces: none new.
- Behaviour:
  - **Save** builds the current platform through `BuildMaterialShaders`, so other platforms' shaders are kept and build info is written.
  - **Opening a material** whose current-platform build is stale rebuilds it and marks the editor tab dirty. The file is written on the next save.

- [ ] **Step 1: Write the failing test** `"rebuilding an unchanged material is byte-identical"`: export, build again with the same fake compiler, export again, compare the bytes.
- [ ] **Step 2: Run** — FAIL or PASS; if PASS, the determinism is already there, so keep the test as a guard.
- [ ] **Step 3: Implement** the three editor call sites with `BuildMaterialShaders`; add the open-time staleness check.
- [ ] **Step 4: Verify in the editor:**
  - Save `Models/Boar/*.hmat` (any material) and reload it: it renders, and the file has `SBLD` (check with a hex view or a scene_graph test).
  - Move one node and save: the graph hash is unchanged.
  - Fast gate.
- [ ] **Step 5: Commit** `feat(editor): materials save current-platform shaders with build info`.

### Task 7: Development-client compile, shader cache and release abort

**Files:**
- Create: `src/shared/material_graph/material_shader_cache.{h,cpp}`, `src/mmo_client/material_shader_resolver.{h,cpp}`
- Modify: `cmake/mmo_options.cmake`, `src/shared/scene_graph/material_manager.cpp:60-80`, `src/shared/graphics/material.{h,cpp}` (missing-variant handler), `src/mmo_client/CMakeLists.txt` (link `material_graph`), `src/mmo_client/client.cpp` (install handler), `src/mmo_client/startup_error.cpp` (reuse dialog)
- Test: `src/tests/material_graph_tests/material_shader_cache_tests.cpp`, `src/tests/graphics_tests/material_missing_variant_tests.cpp`

**Interfaces:**
- Produces:
  - CMake `option(MMO_MATERIAL_RUNTIME_COMPILE "Clients compile missing or stale material shaders at load time (development builds)." ON)`, which defines `MMO_MATERIAL_RUNTIME_COMPILE`.
  - `class MaterialShaderCache { explicit MaterialShaderCache(std::filesystem::path root); bool TryLoad(const String& materialPath, std::string_view platform, const ShaderBuildInfo& expected, Material& into); void Store(const String& materialPath, std::string_view platform, const Material& from); };`
    - The file name is the hex SHA1 of `materialPath + '\n' + platform`, which keeps folders apart.
    - The file holds the `ShaderBuildInfo` followed by the platform's shaders in the `.hmat` platform-group layout.
    - A hit requires that `IsShaderBuildCurrent` holds against `expected`.
  - `using MissingShaderHandler = std::function<void(const String& materialName, std::string_view variant)>; static void Material::SetMissingShaderHandler(MissingShaderHandler);`
  - `MaterialManager::SetPostLoadHook(std::function<void(Material&, std::span<const uint8> fileBytes)>)`. The client resolver uses it. Without a hook, behaviour is as today.
- Behaviour:
  - **With `MMO_MATERIAL_RUNTIME_COMPILE`:** the resolver checks the current platform's build info against the graph in the file bytes. If it is stale, it tries the cache, then `BuildMaterialShaders`, then `Store`. A failure to write the cache is a `WLOG`.
  - **Without the option:** no resolver is installed and a `MissingShaderHandler` is. It shows the startup error dialog with `"Material <name> has no <variant> shader for this platform."` and exits with code 1.

- [ ] **Step 1: Audit** every `GetVertexShader(` / `GetPixelShader(` call site in `src/shared/deferred_shading`, `src/shared/scene_graph` and `src/shared/terrain`. List the sites that treat null as "variant not used, fall back" (these never trigger the handler) and the sites where null means "nothing drawn" (these call the handler). Write the list into the commit message.
- [ ] **Step 2: Write the failing tests:**
  - `"cache hit requires current build info"`: store, then load with matching info → true; load with changed graph hash → false.
  - `"cache key distinguishes folders"`: `A/M.hmat` and `B/M.hmat` get different files.
  - `"missing variant calls the handler once with name and variant"`: a material with an empty `UI` vertex slot fires the handler when the "nothing drawn" path requests it.
  - `"legacy material without build info still renders without runtime compile"`: its D3D11 bytes are used and the handler is not called.
- [ ] **Step 3: Run** — FAIL.
- [ ] **Step 4: Implement** the option, cache, resolver, hook and handler. On macOS the resolver compiles with the Task 8 Metal library compiler and falls back to a source blob when `xcrun` is unavailable.
- [ ] **Step 5: Run** the tests (PASS).
- [ ] **Step 6: Verify in the client:**
  - Debug client: start it after bumping `material_compiler_version::Generator` locally, confirm `Cache/Shaders/` fills and the world renders, then revert the bump.
  - Release build with `-DMMO_MATERIAL_RUNTIME_COMPILE=OFF`: run against repo data and confirm it still renders, since every material has D3D11 shaders.
  - Fast gate.
- [ ] **Step 7: Commit** `feat(client): runtime material shader compile with local cache; release abort on missing variant`.

### Task 8: Metal libraries

**Files:**
- Modify: `src/shared/msl_cross/metal_shader_blob.h`, `src/shared/graphics_metal/graphics_device_metal.mm:763-810`
- Create: `src/shared/msl_cross/shader_compiler_metal.{h,cpp}` (the class now in `src/metal_material_baker/main.cpp:50-110`, including `AddSelectOverloads`)
- Test: `src/tests/graphics_tests/metal_shader_blob_tests.cpp` (header-only, so it also runs on Windows/Linux)

**Interfaces:**
- Produces:
  - `metal_shader_blob::LibraryMagic = {'M','L','B','1'}`, with the same 24-byte header; `sourceSize` holds the library byte count.
  - `std::vector<uint8> PackLibrary(Stage, std::span<const uint8> metallib, uint32 tgX = 1, uint32 tgY = 1, uint32 tgZ = 1)`.
  - `enum class Payload { Source, Library }` and `bool Unpack(const void*, size_t, Header&, Payload&, std::span<const uint8>&)`. The old `Unpack` overload stays for source.
  - `class ShaderCompilerMetal : public ShaderCompiler { enum class Output { Source, Library }; explicit ShaderCompilerMetal(Output); }`. With `Library` it writes the MSL to a temp dir and runs `xcrun -sdk macosx metal -std=macos-metal2.3 -mmacosx-version-min=11.0 -c` and then `xcrun -sdk macosx metallib`. A non-zero exit fails the variant with the tool's stderr as `errorMessage`.
  - The device loads library blobs with `newLibraryWithData:` and looks up `main0` as today. Its function cache keys on payload bytes.

- [ ] **Step 1: Write the failing tests** `"library blob round-trips"` (pack bytes → unpack → `Payload::Library`, same bytes, stage kept) and `"source blob still unpacks as source"`.
- [ ] **Step 2: Run** — FAIL.
- [ ] **Step 3: Implement**; the baker uses `ShaderCompilerMetal(Output::Source)` until Task 12 retires it.
- [ ] **Step 4: Verify on the Mac:**
  - Bake one material with `Output::Library`, run the client and confirm it renders.
  - `MTL_DEBUG_LAYER=1` shows no new errors.
  - Fast gate on Windows (blob tests).
- [ ] **Step 5: Commit** `feat(metal): precompiled Metal library shader blobs`.

### Task 9: `content_cook` library and `mmo_cook` shader steps

**Files:**
- Create: `src/shared/content_cook/{CMakeLists.txt,cook_report.h,cook_report.cpp,shader_cook.h,shader_cook.cpp,material_assembly.h,material_assembly.cpp,overlay.h,overlay.cpp}`
- Create: `src/mmo_cook/{CMakeLists.txt,main.cpp}`; add to `src/CMakeLists.txt` under `MMO_BUILD_TOOLS`, and also build it when `MMO_BUILD_CLIENT` is on so the client CI jobs get it
- Test: `src/tests/content_cook_tests/{CMakeLists.txt,content_cook_tests.cpp}`

**Interfaces:**
- Consumes: `ReadMaterialGraphChunk`, `LoadMaterialGraph`, `BuildMaterialShaders`, `ShaderCompiler`.
- Produces:
  - `struct CookWarning { String asset; String message; }; class CookReport { void Warn(String asset, String message); const std::vector<CookWarning>& GetWarnings() const; void WriteGithubAnnotations(std::ostream&) const; /* "::warning title=cook::<asset>: <message>" */ };`
  - `bool CookShaders(const std::filesystem::path& dataDir, ShaderCompiler& compiler, const std::filesystem::path& outDir, CookReport& report)`:
    - For every `.hmat` under `dataDir`, sorted, writes `outDir/<relative path>.<platform>.shp` (build info followed by that platform's shaders in the platform-group layout).
    - Warns for uncookable materials and failed variants.
    - Returns false only on I/O errors.
  - `bool AssembleMaterials(const std::filesystem::path& dataDir, const std::vector<std::filesystem::path>& shaderPackDirs, const std::filesystem::path& overlayDir, CookReport& report)`:
    - Writes every material that has at least one pack to `overlayDir/<relative path>`.
    - Each written material gets the pack's platform shaders and build info.
    - Platforms without a pack keep the repository's shaders.
    - The `GRPH` chunk is appended unchanged, since Part 2 strips it.
  - `CookShaders` also writes `outDir/warnings.json`: a sorted array of `{"asset","message"}` objects.
  - `bool WriteOverlayManifest(const std::filesystem::path& overlayDir, const std::vector<String>& deletePaths, const std::vector<CookWarning>& warnings)` writes `overlay.json` as `{"schema":1,"delete":[...],"warnings":[{"asset":...,"message":...}]}`.
    - `delete` is sorted and empty in Part 1.
    - `warnings` merges the packs' `warnings.json` with assembly's own warnings, sorted.
  - `mmo_cook` subcommands:
    - `mmo_cook shaders --data <dir> --platform D3D11|METAL --out <dir> [--github]`. `D3D11` is Windows only, `METAL` is macOS only and uses `Output::Library`.
    - `mmo_cook assemble --data <dir> --packs <dir>... --out <dir> [--github]`.
    - `mmo_cook all --data <dir> --out <dir>` runs the current platform's shaders and assembly, producing a full cooked copy (spec 5.9).
    - Exit code 0 even with warnings, 1 on errors.

- [ ] **Step 1: Write the failing tests** with a fake `"TEST"` compiler over a temp data dir holding two fixture materials. Generate them in the test with `MaterialSerializer` plus a `GRPH` chunk from a code-built graph.
  - `"cook writes one pack per material and platform"`.
  - `"cook reports uncookable material"`: a material without `GRPH` gives a warning and no pack.
  - `"cook reports missing function"`.
  - `"assembly merges packs and keeps other platforms"`.
  - `"assembly is deterministic"`: run it twice and compare the bytes.
  - `"same file name in two folders cooks separately"`.
- [ ] **Step 2: Run** `bin/Debug/content_cook_tests` — FAIL.
- [ ] **Step 3: Implement** the library and `mmo_cook`.
- [ ] **Step 4: Verify:**
  - Run `bin/Release/mmo_cook all --data data/client --out %TEMP%/cooked` on Windows; the warnings list the two graph-less materials.
  - Start the client with its data directory pointed at the cooked copy (use the client's existing data-path setting; if none exists, add a `-data <dir>` launch argument here) and confirm the world renders.
  - Fast gate: the Linux step builds `content_cook` and the tests headless.
- [ ] **Step 5: Commit** `feat(cook): content_cook library and mmo_cook shader cooking`.

### Task 10: Deployer applies a cook overlay

**Files:**
- Modify: `deploy/deployer/mmo_deployer/stage.py`, `deploy/deployer/tests/fakes.py`, `deploy/deployer/tests/test_stage.py`, `docs/deployment.md`

**Interfaces:**
- Produces:
  - `OVERLAY_ASSET = "client-overlay.zip"`.
  - When `release.json` lists it, `Stager` downloads it with `_download_verified` and applies it after `_checkout_data`:
    - Extracts all files except `overlay.json` over `self.data_checkout`.
    - Deletes each `overlay.json["delete"]` path. Every path must resolve inside `data_checkout`, otherwise a `StageError`.
    - Raises a `StageError` if `schema` is not `1`.
    - Returns the manifest's `warnings`. `_stage` adds them to the returned manifest as `cook_warnings`, and the staging notification reports their count plus the first five `asset: message` lines (spec 5.6).
  - Releases without the asset stage as today.

- [ ] **Step 1: Write the failing tests:**
  - `test_overlay_replaces_and_deletes_files`: the data repo has `Models/a.hmat` and `Worlds/x.wobj`, the overlay replaces `a.hmat` and deletes `x.wobj`.
  - `test_overlay_path_outside_checkout_is_refused` (`"../../etc/passwd"` in delete).
  - `test_overlay_checksum_mismatch`.
  - `test_release_without_overlay_stages`.
  - `test_cook_warnings_reach_the_staged_manifest`.
- [ ] **Step 2: Run** `cd deploy/deployer && py -3 -m unittest tests.test_stage` — FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** the whole deployer suite (`py -3 -m unittest discover -s tests -t .`) — PASS.
- [ ] **Step 5: Commit** `feat(deployer): apply the nightly's cook overlay to the data checkout`.

### Task 11: Nightly cooks shaders and publishes the overlay

**Files:**
- Modify: `.github/workflows/nightly-release.yml`, `docs/deployment.md`

**Interfaces:**
- Consumes: `mmo_cook` (Task 9), overlay apply (Task 10).
- Produces:
  - Artifacts `shaders-d3d11` and `shaders-metal` (pack dirs) and `client-overlay`.
  - Release asset `client-overlay.zip` listed in `release.json`.

- [ ] **Step 1: Material files for the client jobs.** In `client` and `client-macos`, after the code submodules:
  - `git submodule update --init --depth 1 --filter=blob:none --no-checkout data/client`
  - `git -C data/client sparse-checkout set --no-cone '*.hmat' '*.hmf'`
  - `git -C data/client checkout`
- [ ] **Step 2: Cook in the client jobs.** Build `mmo_cook` next to the client (`-t mmo_client ... mmo_cook`), then run:
  - Windows: `bin/Release/mmo_cook shaders --data data/client --platform D3D11 --out shaders-d3d11 --github`
  - macOS: `bin/mmo_cook shaders --data data/client --platform METAL --out shaders-metal --github`

  Upload each output directory as an artifact.
- [ ] **Step 3: New job `cook`** (`ubuntu-24.04`, `needs: [check, client, client-macos]`):
  - Check out the release commit with the same sparse data checkout.
  - Install `gcc-13 g++-13 ninja-build libmysqlclient-dev uuid-dev`, configure as the gate does, and build `-t mmo_cook`.
  - Download both pack artifacts and run `mmo_cook assemble --data data/client --packs shaders-d3d11 shaders-metal --out overlay --github`.
  - Zip `overlay/` contents to `client-overlay.zip` and upload it.
- [ ] **Step 4: Publish.** Add `cook` to `needs`, download `client-overlay`, add `--asset client-overlay.zip=dist/client-overlay.zip`, and attach the file to the release.
- [ ] **Step 5: Verify:**
  - Parse the YAML with `python3 -c "import yaml; yaml.safe_load(open('.github/workflows/nightly-release.yml'))"` in WSL.
  - With the user's go-ahead to push, run the workflow with `workflow_dispatch` on the branch. If pushing is not approved, the first nightly after merging is the check; say so in the ship summary.
  - Confirm the run summary lists the cook warnings and the release carries `client-overlay.zip`.
- [ ] **Step 6: Commit** `ci(nightly): cook D3D11 and Metal material shaders into a client overlay`.

### Task 12: Turn on release behaviour and retire the baker

**Files:**
- Modify: `.github/workflows/nightly-release.yml` (both client configure steps add `-DMMO_MATERIAL_RUNTIME_COMPILE=OFF`), `CLAUDE.md` (new section "Material Compiler Version", next to "Network Protocol Changes"), `docs/macos-client.md` (Shaders section: precompiled libraries, cook, cache, no baker)
- Delete: `src/metal_material_baker/`, its `add_subdirectory` in `src/CMakeLists.txt`

**Interfaces:** none new.

- [ ] **Step 1: Add the CLAUDE.md rule.** Any change to generated material shader code requires bumping `material_compiler_version::Generator` (or that platform's backend version in `GetBackendVersion`). Without the bump, stale shaders are not detected in editors and development clients.
- [ ] **Step 2: Update the workflow and docs; delete the baker.**
- [ ] **Step 3: Verify:**
  - A Release client built locally with `-DMMO_MATERIAL_RUNTIME_COMPILE=OFF` against a `mmo_cook all` copy renders.
  - The same client against a copy where one material's D3D11 `Default` vertex slot was emptied by a test script shows the error naming that material and exits 1.
  - Fast gate.
- [ ] **Step 4: Commit** `feat(release): nightly clients rely on cooked shaders; retire metal_material_baker`.
