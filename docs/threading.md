# Client Threading Model

This document describes the concurrency model of the game client. Servers and tools are
single-threaded (plus their DB thread) and are unaffected by everything below — the
`TaskSystem` stays uninitialized there, which keeps all thread-affinity asserts inert.

## The model: parallel islands inside a single-threaded frame

The **main thread owns the frame**. At explicit points it fans CPU work out to the
`TaskSystem` worker pool (`src/shared/base/task_system.h`) and **joins before proceeding**
(fork-join). There are no free-running gameplay threads and no cross-frame task lifetimes,
with one exception: the terrain streaming worker (see below).

```
Main thread frame:
  OsInput → FlushBuffered logs → Idle (game logic  [ParallelFor islands])
          → Paint (culling + draw [ParallelFor islands]) → Present
Workers:   idle ──────┤ island ├────── idle ─────┤ island ├────── idle
```

Threads at runtime:

| Thread | Created by | Purpose |
|---|---|---|
| Main | OS | Everything not listed below: input, network poll, UI + Lua, gameplay, culling, all GPU work |
| `mmo_worker_N` | `TaskSystem::Initialize` (ClientApplication) | Fork-join islands: `ParallelFor` chunks, `Dispatch` jobs |
| Streaming worker | `WorldState` | `terrain::Page::PrepareParse` (disk read + parse) via the asio work-queue; results marshalled back through `m_dispatcher` |
| FMOD internal | FMOD | Audio mixing |

Worker count: `clamp(hardware_concurrency() - 2, 2, 8)` — one core is reserved for the
main thread and one for the streaming worker / FMOD / OS.

## Rules for worker code

Code running on a TaskSystem worker (or the streaming worker) must be self-contained CPU
work. It must **never** touch:

1. **`signal<>`** — connect, disconnect and emission are main-thread-only (unsynchronized
   intrusive list, non-atomic refcounts). Emission asserts thread affinity in Debug.
   To notify the main thread, post a completion (see "Completions" below) or collect
   events on the worker and emit after the join — the pattern the parallel clip advance
   uses (`AnimationState::SetNotifyDeferralEnabled` +
   `AnimationStateSet::FlushDeferredNotifies`, driven by `ObjectMgr::UpdateObjects`).
2. **`GraphicsDevice` / GPU resources** — the D3D11 immediate context is main-thread-only
   (`Render`, buffer `Map`/`Update` assert this). Workers stage CPU data; the main thread
   uploads after the join.
3. **Lua / FrameManager (UI)** — strictly main-thread.
4. **Resource managers** (TextureManager, MeshManager, MaterialManager, SkeletonMgr) —
   unsynchronized, main-thread-only.
5. **Logging is the exception**: `DLOG`/`WLOG`/`ELOG` are safe from any thread. Off-main
   entries are buffered (`Log::Emit`) and emitted by the event loop's per-frame
   `FlushBuffered()` — they appear at the next frame boundary.

`ASSERT_MAIN_THREAD()` (from `base/thread_checks.h`) enforces contracts 1 and 2 in Debug
builds. Sprinkle it into any new main-thread-only entry point you add.

## Reference counting

The default `ref_count` policy in `base/intrusive_ptr.h` is non-atomic. Any type whose
`intrusive_ptr`/`ref_counted` handles cross a thread boundary must use `ref_count_atomic`.
Do not blanket-convert main-thread-only types.

## Completions: getting results back to the main thread

Two established mechanisms, pick by shape:

- **Fork-join (same frame)**: `TaskSystem::ParallelFor` blocks until all chunks are done —
  results are simply written into pre-sized output slots (one per index, no locking needed).
  The calling thread participates, so this cannot deadlock even with busy workers.
- **Cross-frame (streaming-style)**: post the completion into an `asio::io_service`
  drained by the main thread (`WorldState::m_dispatcher`, `≤20/frame` in `OnIdle`). This is
  the pattern the terrain three-phase page split uses (`terrain/page.h`), and the shape any
  long-running background work should follow.

## Profiling

`PROFILE_SCOPE` is safe on any thread; each thread accumulates locally and the main
thread merges at `PROFILE_END_FRAME`. The perf overlay (`gxPerf` cvar) shows which thread
a metric ran on ("Multiple" when a scope ran on several, e.g. ParallelFor bodies).

## What we deliberately do NOT do

- **No D3D11 render thread / deferred contexts** — drivers serialize command-list
  submission anyway; the win is not worth double-buffering the scene graph.
- **No thread-safe `signal<>`** — locking would tax every UI event, and cross-thread
  emission is semantically wrong (slots assume main-thread state).
- **No general async asset loading** — streaming workers touch parse-phase data only.
- **No lock-free queues / work stealing** — jobs are chunky (whole emitters, entities,
  tiles); a mutex+condvar queue is invisible in profiles and debuggable.
