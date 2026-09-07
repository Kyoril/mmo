# bot_core

The headless bot client: everything needed to authenticate at the login server, pick a realm,
enter the world and act as a real network peer, with no renderer and no asset pipeline. It speaks
the same wire protocol as `mmo_client` — SRP6, the packet cipher, character enumeration and
creation, and the world session — so a bot is indistinguishable from a player on the wire for
every subsystem it implements.

Two hosts build on it:

- `e2e_client` — one session driven by an imperative Lua scenario. The regression suite.
- `bot_swarm` — many autonomous sessions in one process, driven by `bot_ai`.

## Ownership rules

A `BotSession` owns its connectors and its object model, and nothing else. In particular it does
**not** own the io service or the navigation service, because the swarm shares both:

- **One `asio::io_service` per process.** The host polls it once per frame for every session.
  `BotSession::Pump` (poll + update + sleep) exists for the single-session case only; a host
  driving N sessions must poll once and call `BotSession::Update` on each, or it pays N sleeps
  per frame.
- **One `BotNavService` per process, and this is mandatory rather than an optimisation.** Its
  constructor calls `AssetRegistry::Initialize` and its destructor `AssetRegistry::Destroy`, and
  `AssetRegistry` is a class of statics — a second service would initialize the registry twice
  and the first destruction would pull it out from under everyone still using it. The constructor
  asserts on this. Sharing it also means the proto project and the navigation meshes are loaded
  once, which is the difference between a swarm that fits in memory and one that does not.

## Threading

**Everything here is single-threaded by design.** `BotNavService::m_loadedMaps` is mutated inside
`FindPath` and the Detour query behind it is not thread-safe, so a thread pool would need one
navigation service per thread — which the `AssetRegistry` singleton forbids, and which would
duplicate every loaded navigation mesh per thread.

Scale by running more processes, not more threads. The servers this talks to are themselves
single-threaded (realm and world both), so the bot process will not be the bottleneck.
