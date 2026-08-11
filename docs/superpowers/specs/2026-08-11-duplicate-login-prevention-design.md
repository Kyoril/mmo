# Duplicate Login Prevention — Design

**Date:** 2026-08-11
**Status:** Approved, implementing

## Problem

An account can currently hold several simultaneous sessions. Nothing at any tier notices
that the same account is already online: the login server hands out a fresh session key on
every successful `LogonProof`, and realms never learn that an older session has been
superseded. Two clients can therefore play the same account at once, and a stolen password
is invisible to the rightful owner.

## Policy

**Last login wins.** The newest successful authentication displaces every older session of
that account, everywhere. The displaced client is dropped to the login screen with an
explicit message telling the player their account was signed in elsewhere and that they
should change their password if it was not them.

The kick fires at **login-server `LogonProof`**, the moment credentials are verified and
the account's session key has been rotated — not at realm entry. Consequence, accepted
deliberately: correctly entering your password at the login screen kills your other
session even if you never pick a realm.

## Architecture

The login server is the sole decision point; every other tier reacts to a push.

```
new client --LogonProof(ok)--> LOGIN SERVER
                                  |
                    +-------------+--------------------+
                    |                                  |
      PostKick(LoggedInElsewhere)          KickAccount{accountId, reason}
      on other login-tier sessions          to every authenticated realm
                    |                                  |
             AccountKicked{reason}                     v
             then close                          REALM SERVER
                                        KickPlayerByAccountId(accountId, reason)
                                                       |
                                          KickReason{reason} to client,
                                          then existing Player::Destroy()
                                                       |
                                          World::Leave(guid, Disconnect)
                                          (world tier unchanged)
```

### Why a push, not a session registry

A persisted active-session table (the ROSE model) survives a login-server restart, but it
does not by itself remove an in-world player — a push is still needed to do that. It also
puts session validation on the database path, which is one connection per tier behind one
worker thread. The push model reuses machinery that already exists and is already proven
by the account-ban path.

### Local duplicate guard on the realm

Independently of the broadcast, a realm kicks any *other* local session holding the same
account id when a client session authenticates. This costs a few lines and closes two
holes the broadcast cannot:

- a realm that was disconnected from the login server when the broadcast went out,
- the race where the new client reaches char select before the broadcast lands.

## Protocol changes

A single reason enum lives in `auth_protocol.h`, alongside `auth::world_left_reason`,
which is already the precedent for a cross-tier enum shared by more than one protocol
library. No CMake change is needed: `game_protocol.h` only documents the reason values, and
every consumer that actually reads them (realm server, client) already links both protocol
libraries.

```cpp
namespace mmo::auth::session_kick_reason
{
    enum Type
    {
        /// The account was signed in from somewhere else and this session was displaced.
        LoggedInElsewhere = 0,
        /// The account was banned or suspended while this session was live.
        AccountBanned     = 1,
    };
}
```

Only reasons that are actually sent are defined. Adding a further reason (GM kick, server
shutdown) is a one-line change plus a localized string.

| Direction | Packet | Change | Payload |
|---|---|---|---|
| login → realm | `login_realm_packet::KickAccount` (0x03) | renamed from `AccountBanned`, gains a reason byte | `uint64 accountId, uint8 reason` |
| login → client | `login_client_packet::AccountKicked` (0x06) | new, appended | `uint8 reason` |
| realm → client | `realm_client_packet::KickReason` | new, appended to the end of the enum | `uint8 reason` |

Both reason packets are sent immediately before the socket is torn down, which only works
if `close()` defers the actual shutdown while a send is in flight. `Connection` — the
login↔client and login↔realm link — already did, via `m_isClosedOnSend`.
`EncryptedConnection`, the realm↔client link, did **not**: it closed the socket at once,
discarding whatever had not yet reached the kernel. On an idle socket the write completes
inline and the packet survives, which is what makes the bug easy to miss; on a congested
one the reason is lost, and a congested client is disproportionately the player this
feature exists to inform. `EncryptedConnection::close()` and `Sent()` were given the same
deferral, so both classes now honour the contract their shared interface implies.

`auth::ProtocolVersion` goes 3 → 4 and `game::ProtocolVersion` 6 → 7. A version bump only
helps if the receiver checks it: `Player::HandleLogonChallenge` already rejected mismatched
clients, but `Realm::HandleLogonChallenge` read the realm's version into a member and never
looked at it — so the login↔realm link, the one whose payload actually changed, had no
protection at all. That check was added, and a mismatched realm is now refused with
`FailVersionUpdate` / `FailVersionInvalid` instead of authenticating and then misreading
`KickAccount`. `game::ProtocolVersion` remains written by three senders and validated by
none; that is pre-existing and left alone.

## Login-tier behaviour

In `Player::HandleLogonProof`, inside the success branch of the `PlayerLogin` database
callback — the point at which the session key has been rotated and the new session is
definitively the winner:

1. `PlayerManager::KickOtherSessionsForAccount(accountId, exceptSelf)` — walks the session
   list, skips the caller, calls `PostKick(LoggedInElsewhere)` on each match. `PostKick`
   rather than `Kick` because the login tier runs two io threads and a cross-session kick
   must execute on the target's own strand.
2. `RealmManager::NotifyAccountKicked(accountId, LoggedInElsewhere)` — generalised from
   `NotifyAccountBanned`, posting `KickAccount` to every authenticated realm.

The existing REST ban handler switches to the same call with reason `AccountBanned`, so a
banned player now sees "this account has been closed" instead of a silent drop.

## Realm-tier behaviour

`LoginConnector::OnKickAccount` reads the account id and reason and calls
`PlayerManager::KickPlayerByAccountId(accountId, reason)`. `Player::Kick(reason)` sends
`KickReason` and then runs the existing `Destroy()`, which already declines pending group
invites, notifies guild and friends, leaves chat channels, and calls
`World::Leave(guid, Disconnect)`.

**The world tier is unchanged.** A displaced player is saved and removed on exactly the
path a dropped connection already takes.

## Client behaviour

`RealmConnector` stores the reason from `KickReason` and exposes it; the existing
`Disconnected` signal stays the single transition trigger, so the ordering between the
reason packet and the socket close cannot produce a lost or duplicated state change. Both
`LoginState::OnRealmDisconnected` and `WorldState::OnRealmDisconnected` pass the stored
reason into their Lua events, defaulting to today's generic message when there is none.

The client's `LoginConnector` handles `AccountKicked` the same way it handles an auth
failure, showing the message on the login screen.

Displaced sessions are covered in all three places the player can be: in-world, at char
select, and parked on the realm list at the login tier.

### Strings

`KICK_REASON_STRING` in `GlueDialog.lua` maps the reason code to a localization key,
mirroring the existing `AUTH_ERROR_STRING` / `REALM_AUTH_ERROR_STRING` tables. New keys are
added to all four locales (`enUS`, `deDE`, `frFR`, `ruRU`); non-English locales get the
English text as a placeholder, per the project's localization rule.

`KICK_REASON_LOGGED_IN_ELSEWHERE`:

> You have been disconnected because your account was logged in from another location. If
> this was not you, someone else may have access to your account — please change your
> password immediately.

## Testing

- **Unit tests** — `login_server_tests` covers `KickOtherSessionsForAccount` (displaces
  every other session of the account, never the caller, never another account, no-op when
  the account holds a single session), `KickPlayerByAccountId` taking every session rather
  than the first, that unauthenticated sessions are never displaced, and that two kicks
  landing on one session tear it down once.

  Selecting sessions by account keys off `IsAuthenticated()` and `GetAccountId()`, neither
  of which is otherwise reachable without driving a full SRP exchange against a database,
  so `Player` gained a `SetAuthenticatedForTest` seam. Without it these tests could only
  assert that nothing happens, never that the right thing does — which is exactly what had
  quietly happened to the pre-existing `ConcurrentKickIsSafe` stress test, whose
  unauthenticated players matched no lookup and provoked no kicks at all. That test now
  authenticates its sessions and does what it claims.
- **E2E** — the harness gains a scenario primitive that opens a second login + realm
  session on the same account from inside a running scenario. The new scenario asserts the
  first session is dropped and that it reports reason `LoggedInElsewhere`. This is the only
  test that proves the cross-tier path, and the primitive is reusable for future
  multi-session tests.

## Known limitations

- A realm that is disconnected from the login server when the broadcast is sent does not
  hear it. Its sessions survive until that realm authenticates the same account again, at
  which point the local duplicate guard removes them. The displaced session cannot enter a
  new realm in the meantime, because its database session key has already been rotated.
- Session state is not persisted, so a login-server restart forgets who is online. This is
  acceptable: realms re-authenticate through the login server, and the stale session key
  makes an orphaned session unable to enter anywhere.
- Two logins for the same account landing on the login server's two io threads inside the
  same scan window can each displace the other, leaving the account with no live session
  until the user retries. Very unlikely, self-healing, and the safe direction to fail in —
  noted rather than guarded against.
