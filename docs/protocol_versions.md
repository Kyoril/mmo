# Protocol versions

Two constants describe what a binary can say on the wire:

| Constant | Defined in | Current |
|---|---|---|
| `mmo::auth::ProtocolVersion` | [auth_protocol.h](../src/shared/auth_protocol/auth_protocol.h) | 5 |
| `mmo::game::ProtocolVersion` | [game_protocol.h](../src/shared/game_protocol/game_protocol.h) | 7 |

When they disagree between two peers, the handshake is refused. When they *agree* but the
formats do not, nothing is refused — the peers authenticate and then misread each other,
which surfaces later as a malformed packet, a wrong field, or a crash with no obvious
connection to the change that caused it. That failure mode is the whole reason the bump
matters, and the reason it is now enforced rather than remembered.

## When to bump

Bump when a peer built against the previous revision would misread anything:

- an opcode added, removed, renumbered, or **reordered** (several enums use implicit
  values, so inserting in the middle silently renumbers everything after it)
- a packet payload changed — a field added, widened, narrowed, reordered, or removed
- packet framing or the game cipher changed
- the meaning of an existing field changed, even with the layout untouched

Do **not** bump for comments, formatting, or server-internal changes that never reach a
socket.

After bumping, re-record the fingerprint:

```bash
python tools/protocol_version_check.py --update
```

## What enforces it

| Layer | Catches | Where |
|---|---|---|
| `tools/protocol_version_check.py` | opcode/enum changes, framing, cipher — **hard failure** | first step of `tools/gate/verify.ps1`, and CI on push to `develop` |
| `tools/gate/serialization_warning.py` | `io::read`/`io::write` edits on a branch with no version bump — **advisory** | the review step of `/gate` |
| Handshake checks | an actual mismatched peer, at runtime | login, realm and world servers (see below) |

The first tool fingerprints the files that define the protocol, so it cannot see a payload
change made inside a handler — that is what the advisory exists for, and why the constants
still carry a note telling you to think.

## Where a mismatch is caught at runtime

| Link | Checks | Enforced in |
|---|---|---|
| client → login | auth + game | [player.cpp](../src/login_server/player.cpp) `HandleLogonChallenge` |
| realm → login | auth + game | [realm.cpp](../src/login_server/realm.cpp) `HandleLogonChallenge` |
| world → realm | auth + game | [world.cpp](../src/realm_server/world.cpp) `OnLogonChallenge` |
| client → realm | *nothing* | — |

The client never presents a protocol version to the realm; that link carries no version
field at all. It does not need one: a client cannot reach a realm without a session the
login server issued, and the login server has already checked both of its versions by then.
The realm's own game protocol version is checked when it registers with the login server, so
both ends of the client↔realm link have been verified by a third party before they ever
speak.

Rejections are reported as `FailVersionUpdate` when the peer is older and
`FailVersionInvalid` when it is newer.

## Version history

`auth::ProtocolVersion` covers all three links that use the auth protocol — client↔login,
login↔realm and realm↔world — so a change confined to one of them still bumps the version
for the others. That is coarse on purpose: one constant that is always right beats three
that are individually forgettable.

### auth

| Version | Date | Change |
|---|---|---|
| 5 | 2026-08-11 | The world→realm handshake carries both protocol versions, and the realm validates them (`1c87e67d`'s link was the last one negotiating nothing) |
| 4 | 2026-08-11 | Duplicate login prevention: `AccountKicked` and `KickAccount` opcodes, `session_kick_reason` on the wire (`1c87e67d`) |
| 3 | 2026-06-16 | Account and realm feature management: `AccountFeatures` opcode, feature list on `ClientAuthSessionResponse` (`cb0c54d0`) |
| 2 | 2025-03-14 | Linux clock fixes and overflow handling (`c885abc7`) |
| 1 | — | Initial |

### game

| Version | Date | Change |
|---|---|---|
| 7 | 2026-08-11 | Duplicate login prevention (`1c87e67d`) |
| 6 | 2026-07-13 | Emote support (`e2b85c29`) |
| 5 | 2026-05-28 | Disabled races and classes (`b797e170`) |
| 4 | 2026-01-19 | Client-side spell cooldowns (`b2cbe1c5`) |
| 3 | 2025-11-04 | Item function cleanup, gold overflow check (`6a1f4f7e`) |
| 2 | 2025-03-14 | Linux clock fixes and overflow handling (`c885abc7`) |
| 1 | — | Initial |

Entries before version 5 (auth) and 7 (game) were reconstructed from `git log -S` on the
constant, so they name the commit that introduced each value rather than a hand-written
account of what changed. Add a row here as part of any future bump.
