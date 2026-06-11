# Spell Effects Cookbook

Practical, step-by-step recipes for building common spell VFX with the particle system and the
**Particle System Editor** in `mmo_edit`. Each recipe lists the emitter modules to configure, the
material/texture/mesh you need, and how to wire the finished `.hpar` into a spell.

> **New to the editor?** Read [particle_system.md](particle_system.md) first — it explains Systems,
> Emitters, the module stack (Emitter / Emission / Spawn / Update / Render), simulation space, render
> modes, bursts and curves. Every recipe below maps directly onto those modules.

---

## How effects are assembled

A spell visual is one or more **`.hpar` particle systems** referenced from a `SpellKit`:

- A `.hpar` file is a **System** containing one or more **Emitters** (Niagara-style). A fireball, for
  example, is a single `.hpar` with four emitters: glowing core, sparks, smoke, heat haze.
- `SpellKit.particles` is a **list of `.hpar` paths**. The client (`spell_visualization_service`)
  spawns each, attaches it to the caster (optionally to `attach_bone`), and auto-destroys non-looping
  systems when they finish (`ParticleSystem::IsFinished()`).
- For **projectiles**, the trail/core/light are configured on the projectile visual
  (`projectile_manager` + `ribbon_trail` + point light); the **impact** is a separate one-shot `.hpar`.

### The fastest way to build any recipe

1. Asset Browser → right-click → create a new `.hpar` (or open an existing one).
2. In the editor's **Emitters** panel click **+ Add → From Template** and pick the closest starting
   point (Fire, Smoke, Spark Burst, Swirl Aura, Trail, Sword Slash, Blood Impact, Holy Column,
   Heal Sparkle).
3. Tune the module stack on the right, watching the live preview. Use **Move Emitter** in the preview
   toolbar to confirm **Local**-space effects follow a moving emitter.
4. **Save** (Ctrl+S), then reference the file from a `SpellKit`.

### Materials you will reference

| Material | Blend | Use for |
|---|---|---|
| `Particles/Additive.hmat` | additive | fire, sparks, holy, magic, energy — anything glowing |
| `Particles/Alpha.hmat` | alpha | smoke, blood, dust — anything that occludes |
| `Particles/SoftAdditive.hmat` *(author if missing)* | additive + soft depth fade | volumetric glows that shouldn't hard-clip on geometry |

A particle material is just a normal material set to **Translucent**, **Two-Sided**, **Depth Write
OFF**, **Depth Test ON**, with your particle texture plugged into emissive/base color and the texture's
alpha into opacity. Additive = the material's blend resolves to add; Alpha = standard alpha blend.
(See "Authoring particle materials" at the end.)

### Textures you will want

Most of these effects look dramatically better with the right texture. Common ones to create:

- **Soft radial dot** (white→transparent radial gradient) — universal glow/spark/sparkle.
- **Streak** (a horizontal soft line) — slashes and velocity-stretched sparks.
- **Smoke puff** (soft cloudy blob) — smoke/dust.
- **Flipbook sheets** (e.g. 4×4 or 8×8) — blood splatter, explosions, impact bursts. Set
  **Render → Sprite Sheet** Columns/Rows to match and **Sprite Animation → Animate Over Life**.

---

## 1. Sword slash / melee attack arc

Short, fast, bright streak that reads as a blade swipe. One-shot burst with **Stretched** rendering so
each particle elongates along its velocity.

- **Template:** `Sword Slash`
- **Material:** `Particles/Additive.hmat` (texture: *streak*)
- **Emitter:** Simulation Space = **World**, **Loop = off**, Duration ≈ `0.15`
- **Emission:** Spawn Rate = `0`, one **Burst** at `t=0`, count ≈ `18`
- **Spawn:** Shape = Point; Lifetime `0.18–0.28`; Min/Max Velocity pointing along the swing
  (e.g. `+X`), magnitude `6–10`
- **Render:** Mode = **Stretched**, Length Scale ≈ `8`
- **Update:** Size Over Life `1 → 0.3`; Color Over Life white → light-blue, alpha `1 → 0`

**Tips:** Attach to the weapon/hand bone via `SpellKit.attach_bone`. Orient the swing by rotating the
attach node, or set velocity in the bone's local axes. For a crescent arc, add a second emitter using a
narrow **Cone** shape. For a colored "enchanted" blade, tint Color Over Life.

---

## 2. Blood impact

A wet, gravity-driven splatter burst. Best with an **alpha flipbook** texture; falls back to plain
droplets without one.

- **Template:** `Blood Impact`
- **Material:** `Particles/Alpha.hmat` (texture: *blood flipbook*, e.g. 4×4)
- **Emitter:** Space = **World**, **Loop = off**, Duration ≈ `0.2`
- **Emission:** Spawn Rate = `0`, **Burst** `t=0` count ≈ `30`
- **Spawn:** Shape = small Sphere (radius `0.1`); Lifetime `0.4–0.9`; outward Start Speed `1–3`;
  upward-biased velocity
- **Update:** Gravity `(0,-9.81,0)`; Size Over Life `1 → 0.6`; Color dark red → near-black, alpha `1 → 0`
- **Render (flipbook):** Sprite Sheet Columns/Rows = `4`/`4`, Sprite Animation = **Animate Over Life**

**Tips:** Spawn at the hit location (the impact `.hpar`, not attached to a bone). Add a second short
**Stretched** emitter for fast blood streaks. A brief decal/ground splat can be a third emitter with
**Horizontal Billboard** render mode.

---

## 3. Holy effect for level-up

A rising column of golden light that bursts around the player. Use **Local** space so it stays centered
on the moving character, plus gentle swirl.

- **Template:** `Holy Column`
- **Material:** `Particles/Additive.hmat` (texture: *soft dot* or *streak*)
- **Emitter:** Space = **Local**, Loop on (or set Duration ≈ `2` and Loop off for a one-shot level-up)
- **Spawn:** Shape = Cone (narrow angle, base radius ≈ `0.6`) so particles rise in a pillar;
  Velocity up `3–5`; Lifetime `0.8–1.4`
- **Update:** Gravity `0`; **Orbital/Swirl ≈ 90°/s** for a gentle spiral; Size Over Life `0.5 → 1`
- **Render:** Mode = **Velocity Aligned** (vertical light streaks); Color Over Life
  transparent → warm gold → white, alpha curve `0 → 1 → 0`

**Tips:** Pair with a **point light** (yellow, short fade-in/out) for a real glow — add it on the spell
side. Add a ground **shockwave ring**: a second one-shot emitter, **Horizontal Billboard**, single burst,
Size Over Life `0 → 3`, alpha `1 → 0`. For "ascending sparkles", add a third emitter like recipe 4.

---

## 4. Healing spell impact

Soft green sparkles that rise and twinkle around the target.

- **Template:** `Heal Sparkle`
- **Material:** `Particles/Additive.hmat` (texture: *soft dot*; optional 4-frame twinkle flipbook)
- **Emitter:** Space = **Local** (follows the target); for a one-shot, Loop off + Duration ≈ `1.2`
- **Spawn:** Shape = Cone (wide-ish, base radius `0.5`); Velocity up `1.5–3`; Lifetime `0.9–1.6`;
  Rotation Rate randomized for shimmer
- **Update:** Gravity slightly up `(0,0.5,0)`; Size Over Life `0 → 1 → 0` (pop-in / fade-out via a
  3-key curve); Color green → pale-green, alpha `1 → 0`
- **Render:** Billboard (Face Camera). For twinkle, Sprite Sheet + **Animate Over Life**.

**Tips:** Attach to the target's chest/root bone. A faint **Swirl Aura** (recipe 5) underneath sells the
"restorative glow". Add a soft green point light with a ~0.3s fade.

---

## 5. Casting glow around the caster's hands

A magic aura that **orbits the hand** and changes color by school. This is the canonical use of
**Local** simulation space + **Orbital** swirl, attached to a hand bone — so it tracks the hand as the
cast animation moves.

- **Template:** `Swirl Aura`
- **Material:** `Particles/Additive.hmat` (texture: *soft dot*)
- **Emitter:** Space = **Local** (critical — particles follow the hand), Loop on
- **Spawn:** Shape = small Sphere (radius `0.3–0.4`); low upward velocity; Lifetime `0.8–1.4`
- **Update:** Gravity `0`; **Orbital/Swirl ≈ 200°/s**; **Radial Accel slightly negative** (pulls
  inward so it hugs the hand); optional small **Noise** (amplitude `0.5`, frequency `1`) for life;
  Size Over Life `0.2 → 1`
- **Render:** Billboard; Color Over Life by school — fire: orange/red; frost: cyan/white;
  shadow: violet/black; nature: green; holy: gold

**Wiring:** Set `SpellKit.attach_bone` to the hand bone (e.g. `hand_r`). Use the editor's **Move
Emitter** preview toggle to verify the aura trails the hand correctly. Make a copy per school and just
swap the Color Over Life curve.

---

## 6. Projectiles — fireball / frostbolt / shadowbolt

A projectile = a **moving core** + **ribbon trail** + **point light** + an **impact** `.hpar`. The core
and trail are configured on the projectile visual; the impact is a one-shot system spawned where it
lands.

**Core + sparks `.hpar` (attached to the projectile):**
- Emitter A — **core glow**: Point shape, high Spawn Rate, short Lifetime, Size Over Life `1 → 0.6`,
  **Local** space, Billboard, additive; color = school color.
- Emitter B — **trailing sparks**: `Trail` template (Spawn Rate ~80, zero velocity so they're left
  behind in **World** space as the projectile moves), Size Over Life `1 → 0`, **Stretched** optional.

**Ribbon trail + light:** configure on the projectile (see `projectile_manager` / the projectile
fields): ribbon material, initial/final width and color, segment lifetime; a point light with the
school color and a small range. These give the streak and the moving glow.

**Impact `.hpar` (one-shot, spawned at hit):**
- A **Spark Burst** (template) + a soft **flash** emitter (single burst, Size `0 → large`, alpha
  `1 → 0`) + optional **smoke** puff.

**Per school:**

| School | Core/Trail color | Impact | Extras |
|---|---|---|---|
| **Fireball** | yellow → orange → red, additive | spark burst + smoke (`Alpha`) + flash | orange light; optional `Fire` emitter on the core |
| **Frostbolt** | white → cyan, additive | shard sparks + frosty `IceSmoke.hmat` puff | cyan light; slow-falling shards (low gravity, drag) |
| **Shadowbolt** | violet → black; use additive for the glow and `Alpha` for dark wisps | dark burst + violet flash | purple light; add **Noise** to the trail wisps for an unstable look |

**Tips:** Keep impact systems **Loop = off** so they auto-clean. Reuse one trail `.hpar` across schools
and only change colors/material. Drag a `.hmat` from the Asset Browser onto the **Render → Material**
slot to swap looks fast.

---

## Authoring particle materials

If `Particles/SoftAdditive.hmat` (or a custom textured material) doesn't exist yet:

1. Open the **Material Editor**, create a new material.
2. Set **Type = Translucent**, **Two-Sided = on**, **Depth Write = off**, **Depth Test = on**.
3. Plug your particle **texture** into base/emissive color and the texture **alpha** into opacity.
4. For **additive**, make the blend add (bright-on-dark texture, alpha-weighted); for **alpha**, use a
   standard alpha blend.
5. *(Optional, soft particles)* fade opacity near scene depth to avoid hard intersections with geometry.
6. Save under `data/client/Particles/…` and reference the path in the emitter's **Render → Material**.

The particle vertex color (driven by **Color Over Life**) multiplies the texture, so a single white
texture can produce every color via the curve.

---

## Performance checklist

- Keep `Max Particles` tight; prefer **bursts** over high continuous spawn rates for impacts.
- **Additive** needs no back-to-front sort; **alpha** does (slightly more expensive at high counts).
- Short lifetimes = fewer live particles. One-shot systems free themselves when finished.
- **Stretched/Velocity-Aligned** quads are larger — watch overdraw on big sparks.
- Curl **Noise** and **Orbital** are cheap (CPU, no per-frame allocation) but adds up across thousands
  of particles — use where it reads, not everywhere.
