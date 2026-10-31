# macOS Client (Metal)

The client builds and runs natively on macOS (Apple Silicon tested) with a Metal render backend.
Audio uses the null audio system (FMOD is Windows-only in this tree).

## Prerequisites

```bash
brew install cmake glslang spirv-cross openssl@3 mysql-client protobuf
```

Xcode command line tools (Metal compiler, AppKit SDK) are required.

## Build

```bash
cmake -S . -B build_mac -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMMO_BUILD_CLIENT=ON \
	-DMMO_BUILD_TESTS=OFF -DMMO_BUILD_BOTS=OFF -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3
cmake --build build_mac -t mmo_client metal_material_baker -j8
```

The app bundle lands in `bin/mmo_client.app`. Inside a bundle the client uses the directory
containing the `.app` as its working directory, so the data goes next to it:

```bash
ln -sfn ../data/client bin/Data
open bin/mmo_client.app
```

## Shaders

Metal cannot run the D3D11 bytecode the engine ships, so all shaders are cross compiled from
HLSL: HLSL → SPIR-V (glslang) → Metal Shading Language (SPIRV-Cross), implemented in
`src/shared/msl_cross`. The result is a small blob (`metal_shader_blob.h`): a header with the
stage and compute threadgroup size, followed by MSL source that the device compiles at load time.

- **Built-in shaders** (deferred passes, compute passes, fixed vertex format shaders) are
  converted at build time by the `hlsl2msl` tool (`mmo_add_metal_shaders` in
  `cmake/mmo_macros.cmake`). The generated headers use the same symbol names as fxc's output,
  so each shader seam only has an `#elif defined(__APPLE__)` include branch.
- **Materials** (`.hmat`) store compiled shaders per platform. `metal_material_baker` recompiles
  every material graph with the HLSL generator, cross compiles it, and stores the result as the
  `METAL` platform. D3D11 bytecode and the graph chunk are written back byte for byte.

```bash
./bin/metal_material_baker data/client            # bake materials that lack Metal shaders
./bin/metal_material_baker data/client --force    # rebake everything
./bin/metal_material_baker data/client --check    # report materials without Metal shaders
```

Materials saved by the Windows editor keep their existing (possibly stale) Metal shaders, so
rebake with `--force` after material changes. Materials without a graph (`Models/Mine/Mine.hmat`,
`Textures/Desert/Rocks/Rock.hmat`) cannot be baked and render nothing on macOS. The UI variant of
some 3D materials fails to compile because the generator references the world normal there; those
variants are never used for 3D materials.

### Resource binding conventions

HLSL registers keep their numbers where Metal has a matching table (`bN` → `buffer(N)`,
`tN` → `texture(N)`, `sN` → `sampler(N)`). Structured buffers move to `buffer(14 + N)`, RW
textures to `texture(16 + N)`, vertex streams to `buffer(30 - stream)`. Values passed between
the vertex and pixel stage get locations derived from their semantic, so separately compiled
stages always match. Vertex inputs are named after their semantic (`TEXCOORD8`) and matched
against vertex declarations by name when a pipeline is created.

## Backend design (`src/shared/graphics_metal`)

The engine drives its graphics device like a D3D11 immediate context. The Metal device records
that state and builds Metal objects lazily:

- A render command encoder opens on the first draw after the render targets changed; clears turn
  into load actions of the next pass on those targets.
- Pipeline states (shaders + vertex layout + attachment formats + blend mode) and depth-stencil
  states are created on demand and cached. Pipeline reflection is used to bind placeholders where
  the engine leaves a slot empty, which D3D11 tolerates but Metal does not.
- Constant buffers are CPU memory copied into the command stream per draw (`setBytes`, or a
  per-frame transient buffer above 4 KB), so every draw sees the contents it was recorded with.
- Discarding maps of vertex/index/structured buffers rename the buffer instead of overwriting
  data the GPU may still read.
- Textures are uploaded into private storage through a separate upload command buffer that is
  committed before the frame.
- The window renders at one pixel per point (the window server scales on Retina screens).

Debugging aids: `MMO_METAL_CAPTURE_DIR=<dir>` writes every `MMO_METAL_CAPTURE_INTERVAL`th
presented frame (default 300) as BMP; `MMO_METAL_DEBUG_LAYOUTS=1` logs the vertex attribute
mapping of each new pipeline; `MTL_DEBUG_LAYER=1` enables Apple's API validation.

## Platform layer (`src/mmo_client/macos`)

- `main_osx.mm` sets up `NSApplication` without a nib and runs the common main function; the game
  loop polls Cocoa events every frame (`event_loop_osx.mm`) inside a per-frame autorelease pool.
- Key events are translated to Windows virtual key codes, which the game's key handling uses.
  Command is reported as Control (so Cmd+C/V work in text fields), Cmd+Q quits, and
  Control-click acts as a right click.
- Mouse capture (camera turning) freezes the cursor and reports a virtual cursor position fed
  from raw mouse deltas.

## Playing against a local server stack

The servers build on macOS too. `tools/macos/local_stack.sh` starts login, realm and world servers
with a private throwaway MySQL on port 3307 (no system MySQL needed; requires `brew install mysql`):

```bash
cmake --build build_mac -t login_server realm_server world_server e2e_client -j8
tools/macos/local_stack.sh up
bin/mmo_client.app/Contents/MacOS/mmo_client -exec tools/macos/local_login.cfg
tools/macos/local_stack.sh down
```

It creates the GM account `E2EGM` / `E2EGMPASS`. Create a character in the client, or headless with
`e2e_client` (configure with `-DMMO_BUILD_BOTS=ON`; see `e2e/README.md`).

## Known gaps

- No audio (FMOD).
- Occlusion queries and GPU timers are not implemented (the terrain falls back to no occlusion
  culling).
- No Retina-resolution rendering.
