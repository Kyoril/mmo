# Plaza Character Select — Design

Date: 2026-10-21
Status: draft for review

## Intent

Players keep telling the user the game "looks like WoW", and the character select screen is
the first thing that confirms it: WoW's own font (Friz Quadrata), WoW gold text, warm
parchment panels, a right-hand character list, and a single model over a static placeholder
backdrop. The user wants the game to read as *their* game. Its strengths are its music, a calm
and relaxed feel, lots of small details, and a sandbox/social direction.

The new screen is inspired by ROSE Online: after choosing a realm, the camera glides from
outside the gates of **Haven** through the gate and settles on the **town square**, where the
player's characters stand around as if they had met up there. The characters are in a place
in the world rather than in a menu.

Success means a returning player arrives in Haven every time they play, and nothing on the
screen reminds anyone of WoW.

## Goals

- Character select is a real 3D scene: a simplified Haven gate, street and plaza.
- A camera flight from outside the gate to the plaza plays once the realm is chosen and the
  character list has arrived.
- Up to **5 characters** stand on the plaza at once.
- A roster ("All characters (N)") lists every character and lets the player **reorder** them
  by drag and drop. The top 5 in that order are the plaza characters. The order persists per
  account on the server.
- Picking a character who is not among the 5 makes them **walk into the plaza and take the
  front spot** for this visit, without changing the saved order.
- A restyle of all glue screens away from WoW: no Friz Quadrata, no WoW gold, no parchment
  panels.

## Non-goals

- Character creation's layout and scene. It stays its own screen with its current layout and
  backdrop for now; it only picks up the new glue styling. (User decision.)
- Login and realm selection screens keep their layout and flow. They change only in styling
  and in that they sit over the gate scene instead of a static image.
- No hard character limit is introduced by this project. There is currently no per-realm
  character cap in the code, and this design does not add one.
- Loading the real Haven map. The scene is a cut-down glue-screen scene.

## Anti-goals (would make this a failure even if it works)

- It still looks or feels like WoW: gold-on-parchment, Friz Quadrata, list-on-the-right as the
  primary interaction.
- The flight makes players wait: the flight must never be held up by loading, and must be
  skippable.
- Logging out to character select becomes slow or repetitive (the flight on every return).
- It breaks the calm feel: busy UI, loud transitions, flashy effects.

## Decided by the user

- Setting: Haven, simplified so it loads quickly, rather than the full town.
- When the flight starts: after a realm is chosen **and** the character list has been
  received.
- Login and realm selection already show the Haven gate scene. Their static background images
  go away.
- "Change realm" flies back out through the gate; choosing a realm flies back in.
- 5 characters on the plaza. Additional characters are reachable through a UI roster, and the
  player can sort the roster to decide which 5 are shown by default.
- A character from outside the 5 walks in and takes the front spot.
- The primary button reads **"Enter World"** (existing `ENTER_WORLD` key).
- Character creation stays separate for now.
- The new font and colours apply to all glue screens, not just character select.

## Design

### Flow

1. **Login and realm selection are shown in front of Haven's gates (user decision).** From the
   moment the login screen appears, the 3D scene is already rendering, with the camera outside
   the gate. The login and realm UI sit over it.
   - The static background images of these screens (`BackgroundPH.htex` and similar) are no
     longer drawn.
   - The camera idles gently at the gate: slow drift, ambience, music.
   - Their layout and flow are otherwise unchanged apart from the glue restyle.
2. The gate view loads first, at client start. The street and plaza continue loading in the
   background while the player logs in and picks a realm.
3. Character list arrives → the camera flies from the gate view through the gate to the plaza
   (~4–6 s, eased). Plaza characters are already in place when the camera arrives. Any click
   or key skips straight to the end of the flight.
4. **"Change realm" flies back out (user decision):** from the plaza, the camera flies back out
   through the gate to the gate view, and the realm list appears there. Choosing a realm
   (again) flies back in once its character list arrives. This flight is also skippable.
5. Logging out fully, or a disconnect, returns to the login screen at the gate view.
   - *My call:* there is no flight for these; it is a cut or fade to the gate view.
6. On the plaza: the selected character stands at the front spot (in front of the fountain or
   the plaza's focal point). The other plaza characters idle at their spots.
7. Enter World → camera pushes in on the selected character, fade, then the normal loading
   screen and `WorldState`.
8. Logging out from the world back to character select lands **directly on the plaza**, with
   no flight. The flight is for arriving from the realm screen.

### The plaza

- Five character spots around the square, plus the front spot used by the selected character.
  *My call:* the selected plaza character moves to (or is framed at) the front spot; exactly
  how is the builder's call.
- Each plaza character idles naturally with a pose or emote animation (sit on a bench, lean,
  stand, cross arms…), taken from the existing emote/pose catalog. Variety beats realism; the
  selected character plays a calm standing idle.
- Switching selection eases the camera to frame the newly selected character, who turns
  slightly towards the camera (a small greeting emote is welcome, but keep it subtle).
- The selected character can be rotated by dragging (yaw), which the model frame supports
  today but nothing calls.
- **Visitors** (characters outside the 5): selecting one in the roster makes them walk in from
  a street entrance and take the front spot. Switching back to a plaza character makes the
  visitor walk back out. *My call:* the walk-out.
- Fewer than 5 characters: empty spots show a faint "New character" marker that opens
  character creation. With 5 or more, "Create character" is a normal button instead.
- Deleting (after the existing confirmation dialog): the character walks out of the plaza
  instead of vanishing. *My call.*
- Each plaza character has a soft nameplate: name, and "Level N Class" underneath. The
  selected character also shows the zone they are in (the zone ID is already in the character
  list).

### The scene

- A dedicated, simplified version of Haven: the gate (`Bridge_01_HavenGate` or equivalent),
  the street from the gate to the plaza, the plaza itself and the facades that frame it.
  Anything the camera never sees is left out. Full Haven is ~22 child WMOs and ~860 mesh
  references, too much for a glue screen.
- Lighting and sky use the existing shared `SkyComponent` / `EnvironmentController`, with an
  environment profile chosen for the scene.
- **Time of day:** *my call:* follow the player's local wall-clock time of day, because the
  client has no server game time before entering the world (game time is sent by the world
  server on enter). A fixed, hand-picked "golden hour" is the fallback if local time looks bad
  at some hours.
- Town ambience and music play. *My call:* reuse Haven/login music and ambience entries that
  already exist; no new audio is part of this project.

### UI

- **Minimal chrome over the scene:**
  - "Enter World" bottom centre.
  - Realms (existing `CHANGE_REALM`) and Quit bottom left.
  - "All characters (N)" bottom right, plus Create/Delete for the selected character
    somewhere unobtrusive.
- **Roster:** a slim panel that opens from the "All characters" button. It shows every
  character (name, level, class) with the top 5 marked as "on the plaza" and a divider before
  the rest. Drag to reorder. Clicking a row selects that character.
- **Restyle (all glue screens: user decision):** the new font and colours apply to every glue
  screen. That covers login, realm list, character creation's panels and buttons, and the shared
  dialogs and templates in `GlueTemplates.xml`. Character creation's layout and backdrop stay
  as they are; only its styling changes. In-game UI is not affected.
  - Replace Friz Quadrata and WoW gold with a calm look: soft ivory text, stone/sage/dusk
    tones, translucent dark backgrounds, rounded or very light borders.
  - The font must be openly licensed, must cover Latin and Cyrillic (ruRU is a shipped
    locale), and must not be Friz Quadrata. The candidate fonts in the repo
    (`arbutusslab-regular`, `MORPHEUS`, `SKURRI`) may not fit; adding a new font is fine.
  - The user picks the final font and colours from mockups during the build.
- All strings localized in all four locales (enUS, deDE, frFR, ruRU). This includes the
  currently hardcoded "N Characters" text.

### Character order (server)

- The order is stored per account, per realm, in the realm database.
- *My call:* a sort-index column on `characters`, added by an additive migration and mirrored
  in the `*_db_full.sql` history.
- The character list is sent in that order (the query has no `ORDER BY` today).
- A new client→realm request saves a new order. It is validated server-side: only the
  account's own characters, each exactly once.
- This is a wire change, so `game::ProtocolVersion` is bumped, the fingerprint updated and the
  bump recorded in `docs/protocol_versions.md`.
- New characters go to the end of the order.

### Settled during review (my calls)

- **Initial selection:** the plaza opens on the last-played character, remembered client-side
  per realm (a cvar holding the character GUID). If there is none, or it no longer exists, it
  opens on order #1. If the last-played character is a visitor (outside the top 5), they are
  already standing at the front spot when the plaza appears. Nothing new goes on the wire for
  this.
- **Roster changes are live:** dragging someone into or out of the top 5 makes them walk in or
  out right away. That needs simple choreography: walks are staggered and one character at a
  time is fine. The order is saved once, when the roster panel closes or on Enter World, not
  per drop. This avoids per-drag packets to the single-threaded realm.
- **Gate view not ready at startup:** the login UI is shown over a dark, calm fill and the
  scene fades in once the gate view is ready. No static image fallback is kept. The gate view
  is small, so this should take a moment at most.
- **Plaza not ready when the list arrives:** the flight is skipped. The screen fades from the
  gate view straight onto the plaza once the plaza is ready. Loading the plaza from client
  start should make this rare.
- **Scene lifetime:** the glue scene (gate + street + plaza) is owned by the login state and
  released when entering the world. Logging out rebuilds it behind a short fade. The
  simplified scene is small enough for that, and it avoids keeping a second scene resident
  during play.
- **Dead characters** appear normally on the plaza, with no ghost look. The zone line in the
  nameplate can say they are dead if the existing UI already did.

## Constraints and known facts

- **`LoginState` has no 3D scene or renderer.** Character select today is a `ModelFrame` with
  a private forward-only scene over a static image. The deferred renderer, sky, environment
  and WMO streaming are wired up only inside `WorldState`, although the underlying classes
  (`OctreeScene`, `DeferredRenderer`, `SkyComponent`, `EnvironmentController`,
  `WorldModelInstance`) are shared. The new scene needs its own glue in the login state.
- **Character visuals are built for one entity only** (`CharSelect` + `ModelFrame`
  "Preview"). Building five avatars needs that logic (base mesh, customization applier,
  `ApplyItemDisplay` for equipment) factored so it can build any number of entities. The same
  logic is duplicated in `UnitModelFrame`, `GameUnitC` and `CharCreateInfo`.
- **Emotes/poses:** the catalog is in `emotes.data`, and `AnimationController` takes a raw
  entity. It is believed, but not verified, to work without a server unit.
- **WMO loading** in the world is stepwise (`WorldModelPreparation`, ~100 ms merge for the
  town shell). The simplified scene must be ready before the flight ends, ideally before it
  starts.
- **Client threading rules** (`docs/threading.md`) apply to any background loading.
- Realm server is single-threaded io; DB access via `DatabasePool` keyed by account.
- Migrations must be additive.
- No exceptions; project code style per CLAUDE.md.

## Left to the builder

- How the simplified Haven scene is authored: a new small world/map, a single WMO assembled
  from Haven parts, or a hand-placed scene file. The camera path, spot positions, poses and
  the front spot are authored data, not hardcoded, if practical.
- How the scene is rendered in the login state: a new frame renderer or extending
  `ModelFrame`/`ModelRenderer`, as long as it uses the deferred path for proper lighting.
- Exact flight duration, easing, camera framing per spot, and the push-in on Enter World.
- Whether to keep `CharSelect.xml`'s structure or replace it.
- Whether the existing `idle_pose` column per character is worth sending to pick each
  character's plaza pose (it is not in the character list today).

## Out of scope / later

- Moving character creation into the plaza (for example, a new character stepping out of a
  doorway).
- Showing friends or guild members on the plaza.
- Seasonal or event variants of the plaza.
