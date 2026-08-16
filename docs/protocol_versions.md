# Protocol versions

Two constants describe what a binary can say on the wire:

| Constant | Defined in | Current |
|---|---|---|
| `mmo::auth::ProtocolVersion` | [auth_protocol.h](../src/shared/auth_protocol/auth_protocol.h) | 5 |
| `mmo::game::ProtocolVersion` | [game_protocol.h](../src/shared/game_protocol/game_protocol.h) | 8 |

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
| `tools/protocol_version_check.py` | opcode/enum changes, **field indices**, framing, cipher — **hard failure** | first step of `tools/gate/verify.ps1`, and CI on push to `develop` |
| `tools/tests/test_protocol_version_check.py` | the checker itself silently breaking | second step of the gate |
| `tools/gate/serialization_warning.py` | `io::read`/`io::write` edits on a branch with no version bump — **advisory** | the review step of `/gate` |
| Handshake checks | an actual mismatched peer, at runtime | login, realm and world servers (see below) |

### What the fingerprint does and does not cover

It covers files that define a wire structure **together with its serializer**, or that define
the indices and framing those structures travel in. For the game protocol that deliberately
includes the field layer — `object_type_id.h` (`object_fields`), `field_map.h`,
`movement_info.h` — plus the standalone payload structures `character_view.{h,cpp}`,
`mail.h` and `game.h`. The field layer matters most: `FieldMap` indices go out in every
`UpdateObject` and `object_fields::UnitFields` is numbered implicitly, so adding a unit stat
in the middle renumbers every field after it. That is ordinary gameplay work that looks
nothing like a protocol edit, and it is the likeliest wire break in this codebase.

What it does **not** cover is a payload assembled inline in a packet handler — an
`io::write<uint32>` added next to the others in a `Player::` or `World::` method, of which
there are hundreds across the three tiers. Fingerprinting that would mean fingerprinting most
of the server code, and the resulting false-positive rate would make the check worthless. That
gap is the advisory's job, and it is why the constants still carry a note telling you to
think. If you add a new *structure* with its own serializer, add its file to `PROTOCOLS`.

If you change the tracked file set or the normalization, bump `FINGERPRINT_FORMAT` in the
checker and re-record with `--update --migrate`. Old fingerprints then compare as
"incomparable" rather than as a wire change, which avoids a spurious version bump — the
migration prints which protocols it re-recorded without one, so the two cannot be confused.

## Where a mismatch is caught at runtime

| Link | Checks | Enforced in |
|---|---|---|
| client → login | auth + game | [player.cpp](../src/login_server/player.cpp) `HandleLogonChallenge` |
| realm → login | auth + game | [realm.cpp](../src/login_server/realm.cpp) `HandleLogonChallenge` |
| world → realm | auth + game | [world.cpp](../src/realm_server/world.cpp) `OnLogonChallenge` |
| client → realm | *nothing* | — |

The client never presents a *protocol* version to the realm. Its `AuthSession` packet does
carry the client build number ([player.cpp](../src/realm_server/player.cpp) `OnAuthSession`),
but that check only warns in debug and is compiled out entirely in release, so it enforces
nothing. The link does not need its own version: a client cannot reach a realm without a
session the login server issued, and the login server has already checked both of the
client's versions by then. The realm's own game protocol version is checked when it registers
with the login server, so both ends have been verified by a third party before they ever
speak.

Rejections are reported as `FailVersionUpdate` when the peer is older and
`FailVersionInvalid` when it is newer.

### Deployment consequence

Because the login server validates `game::ProtocolVersion` — for clients and for realms —
**bumping either constant means rebuilding and redeploying all three tiers together.** A
login server left on an older build will reject every realm and every client. There is no
staged-upgrade path, which suits `compose.yml` deploying the three as a unit. `game` moves
more often than `auth`, so this is the constant that will usually force the redeploy.

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
| 11 | 2026-08-14 | GM damage cheat opcode: walks a boss across its health-gated phase thresholds without depending on the test character's damage output |
| 10 | 2026-08-14 | Widen the set-instance-variable cheat's value to int64, matching `WorldInstance`'s storage |
| 9 | 2026-08-14 | GM set-instance-variable cheat opcode for the E2E harness |
| 8 | 2026-08-12 | GM godmode cheat opcode for the E2E harness |
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
