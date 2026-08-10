# Testing the Servers

What each layer of the server test suite actually covers, and — just as importantly — what it
does not. Written down because several defects fixed on `feature/server-network-robustness`
were invisible to every layer that existed at the time.

## The layers

| Layer | Command | Covers |
|---|---|---|
| Unit | `bin/Debug/unit_tests.exe` | Protocol framing, socket-level `Connection` / `EncryptedConnection` behaviour, the acceptor, the shutdown-signal helper |
| Unit (per tier) | `bin/Debug/login_server_tests.exe`, `game_server_unit_tests.exe`, `realm_server_tests.exe` | Session lifecycle, HTTP handlers, groups, friends, MOTD, game-server logic |
| E2E | `powershell -File tools/e2e/e2e_run.ps1` | Real login → realm → world stack driven by a scripted client |
| Shutdown | `python tools/shutdown_check.py` | Each tier exits cleanly on a stop signal |
| Gate | `powershell -File tools/gate/verify.ps1` | Build + all unit suites + E2E |

The gate does **not** run the shutdown check — it needs `e2e/runtime/config`, which only
exists once the E2E stack has been brought up. Run it by hand after touching anything that
owns an asio timer, acceptor, or work guard.

## Socket-level tests

`src/unit_tests/test_network_connection.cpp` and `test_game_connection.cpp` drive a real
`io_service` over loopback rather than mocking asio. That is deliberate: buffer growth, close
ordering and strand dispatch are all properties of *how the connection drives asio*, and a mock
would only assert that the code calls what the mock expects.

Each file opens with a test that asserts positive delivery. Without one, a later
"nothing was received" assertion cannot be told apart from a broken fixture.

## Shutdown testing

`tools/shutdown_check.py` launches each server in its own process group and sends
`CTRL_BREAK_EVENT`, then requires: exit within the timeout, exit code 0, a `stopped cleanly`
line in the log, and an attached client observing a closed connection.

It exists as a separate tool because the E2E harness *cannot* test this. `Start-E2eServer`
launches servers hidden with redirected stdio, leaving them with no window to close and no
shared console to signal — the harness can only force-kill, which proves nothing about
graceful shutdown.

**The failure mode it catches:** a shutdown handler that fires, closes everything it knows
about, and still never exits, because something else is holding outstanding io work. Three
such holders were found this way, none of which any other test could see:

- `web::WebService`'s acceptor holds a permanently pending `async_accept`.
- `TimerQueue` keeps its asio timer armed. Note `Countdown::Cancel()` does **not** help — it
  only invalidates its own callback and leaves the timer running.
- `WorldInstanceManager`'s 30ms world tick re-arms itself forever.

If you add anything that owns a timer, an acceptor, or a work guard, it needs a `Stop()` and a
call from the relevant shutdown handler. Run this tool to confirm.

On Linux the equivalent is `SIGTERM` (what `docker stop` sends); the tool is Windows-only
because it drives console control events.

## Race testing

**MSVC has no thread sanitizer.** There is no way to prove the absence of a data race on this
toolchain. What is available:

- Stress tests tagged `[.stress]`, which Catch2 skips by default. Run with
  `bin-asan/Debug/login_server_tests.exe "[.stress]"` (or `bin/Debug/...` for an uninstrumented
  run).
- AddressSanitizer, via `-DMMO_ENABLE_ASAN=ON` in a separate build directory. ASan does not
  detect races, but it does detect what these races *manifest* as — use-after-free and heap
  corruption.

Running the sanitized suite:

```bash
cmake -S . -B build-asan -DMMO_BUILD_CLIENT=OFF -DMMO_WITH_DEV_COMMANDS=ON -DMMO_ENABLE_ASAN=ON
```

```bash
cmake --build build-asan --config Debug -t login_server_tests realm_server_tests game_server_unit_tests -- /m:4
```

Two traps:

- **The ASan runtime must be on `PATH`**, or every test exits with `0xC0000135`
  (`STATUS_DLL_NOT_FOUND`) and *no* diagnostic explaining why. Add
  `%VCToolsInstallDir%\bin\Hostx64\x64` (the directory holding
  `clang_rt.asan_dynamic-x86_64.dll`) before running. Usefully, this doubles as proof the
  binary really is instrumented: if it runs without that DLL, ASan is not linked in and a
  "clean" result means nothing.
- **The sanitized tree writes to `bin-asan/` and `lib-asan/`**, not `bin/` and `lib/`. Output
  goes into the source tree rather than the build tree, so without that split an ASan build
  overwrites the ordinary binaries *and static libraries* — and the next normal build then
  fails with `LNK2038: mismatch detected for 'annotate_string'`, which reads like a code error
  rather than the build collision it is. If you ever see that, delete `lib/<config>` and
  `bin/<config>` and rebuild.

Do not build `unit_tests` with `MMO_BUILD_CLIENT=OFF`: it links `graphics_d3d11`
unconditionally on Windows and will fail to link.

Treat the combination as evidence, not proof. The strongest tool remains reading the diff and
asking, for every teardown path: **what operations are still in flight when this runs, and what
do their completion handlers touch?** That question is what caught a null dereference in
`EncryptedConnection` teardown that a green unit suite and 14/14 E2E scenarios both missed.

## Threading contract per tier

- **Login server** — two io threads (`maxNetworkThreads = 1` plus the main thread). Anything
  reaching across sessions must hop onto the target's strand via `AbstractConnection::Post`.
  Session lookups return `shared_ptr`, never raw pointers: the manager's mutex protects the
  list, not the lifetime of what comes out of it.
- **Realm and world servers** — single-threaded io, and they depend on it. Cross-session access
  goes through raw `Player*` taken from the managers in roughly thirty places. Raising either
  tier's thread count requires doing the login server's `shared_ptr` + `Post` work there first.
- **Database** — one connection per tier behind one worker thread. Call sites rely on the
  implicit FIFO ordering that gives (for example `SetCharacterActionButtons` followed by
  `GetActionButtons` on class change). A connection pool must preserve per-entity ordering.

## Verifying a fix actually fixes something

For any bug fix, confirm the test fails *without* the fix before trusting it. Revert the
production change, run the test, see it fail, restore. Several tests here were written after
their fix and would have passed vacuously otherwise.
