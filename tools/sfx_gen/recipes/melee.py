# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Prompt table for the melee auto attack sound effects.

Generation runs through the ElevenLabs MCP connector (``eleven_text_to_sound_v2``), which a
script cannot invoke -- this file is the checked-in record of *what* was asked for, so the
set can be regenerated consistently.

**Layered prompts, not foley.** Same lesson as :mod:`warrior`: naming the layers explicitly
(transient, body, sub-bass, tail) is what separates a produced game sound from a thin single
clank. Never reintroduce "dry", "isolated" or "no reverb" -- they flatten the result. Prompts
are hard-capped at 450 characters and fail *silently* above it; ``tools/tests`` guards both.

**Where this set deliberately differs from the warrior abilities.** Those prompts all carry an
arcane/energy layer, because an ability should read as an ability. Auto attacks are the
opposite: they are the mundane baseline heard hundreds of times per fight, so they stay
purely physical -- steel, flesh, bone, leather, wood. An arcane shimmer on every white swing
would both fatigue the ear and blur the line between a special and a normal hit.

**Restraint on the tail.** Ability sounds get a designed tail; auto attacks get a short one.
At a 1.5-2.6s swing timer, long tails from two duelling units overlap into mush. Durations
here are shorter than the warrior set for that reason, not by oversight.

Slots ending in a number are shuffle-bag variations of one ``SoundEntry``: the entry lists
them as separate files, and playback picks each once in random order before repeating (see
``SoundEntryPlayer``). That is why the high-frequency sounds -- swings and flesh impacts --
get three variations each while one-off events like a block get two.

``target_dbfs`` sets the mix balance. Impacts sit hotter than swings so a landed hit reads
louder than the wind-up, and misses sit lowest of all.
"""

from dataclasses import dataclass

PROMPT_INFLUENCE = 0.75


@dataclass(frozen=True)
class SoundSpec:
    prompt: str
    duration: float
    target_dbfs: float


# Shared tail of every prompt. Keeps the set coherent and states the register once.
_STYLE = ("Physical melee combat sound for a fantasy MMO, richly layered and produced, "
          "punchy and satisfying, short tail. No magic, no music, no voice.")

SOUNDS = {
    # ---- Swings: played as the attack animation starts, before contact ----
    "SwingLight01": SoundSpec(
        f"A light one-handed sword swung fast through the air. Layers: a thin high-frequency "
        f"blade whistle, an airy whoosh body swelling then dropping, and a soft low wind "
        f"rush underneath. Quick and clean. {_STYLE}",
        0.9, -8.0),
    "SwingLight02": SoundSpec(
        f"A short blade slicing through air in a tight arc. Layers: a bright metallic air "
        f"whistle, a compact whoosh body, a faint low rush beneath. Slightly higher and "
        f"faster than a broadsword swing. {_STYLE}",
        0.9, -8.0),
    "SwingLight03": SoundSpec(
        f"A quick dagger-length blade cutting the air. Layers: a sharp thin whistle, a very "
        f"short airy whoosh, a light low rush. Crisp, minimal, fast. {_STYLE}",
        0.8, -8.0),

    "SwingHeavy01": SoundSpec(
        f"A heavy two-handed greatsword hauled through a wide arc. Layers: a deep roaring "
        f"air whoosh, a broad low wind body with real mass, a faint metallic edge whistle "
        f"riding on top. Slow, weighty, effortful. {_STYLE}",
        1.2, -7.0),
    "SwingHeavy02": SoundSpec(
        f"A great axe swung overhead through the air. Layers: a thick low whoosh with weight "
        f"behind it, a broad wind body, a dull metallic hum from the head. Heavy and "
        f"committed. {_STYLE}",
        1.2, -7.0),
    "SwingHeavy03": SoundSpec(
        f"A large two-handed weapon sweeping horizontally. Layers: a deep air roar, a wide "
        f"wind body, a subtle low metallic ring. Slower and lower than a one-handed "
        f"swing. {_STYLE}",
        1.2, -7.0),

    "SwingBlunt01": SoundSpec(
        f"A heavy iron mace swung through the air. Layers: a dull thick whoosh with no edge "
        f"whistle, a rounded low wind body, a faint leather-grip creak. Blunt and "
        f"weighty. {_STYLE}",
        1.0, -7.5),
    "SwingBlunt02": SoundSpec(
        f"A wooden quarterstaff whipped through the air. Layers: a hollow woody whoosh, a "
        f"mid-range air body, a light rushing tail. Lighter and woodier than an iron "
        f"mace. {_STYLE}",
        1.0, -7.5),
    "SwingBlunt03": SoundSpec(
        f"A blunt weapon swung in a short arc. Layers: a muffled thick whoosh, a rounded low "
        f"wind body, a faint grip creak. Dull, no metallic ring. {_STYLE}",
        1.0, -7.5),

    "SwingUnarmed01": SoundSpec(
        f"A bare fist thrown fast through the air. Layers: a short sharp cloth-and-air snap, "
        f"a compact whoosh body, a faint low rush. Tight and quick. {_STYLE}",
        0.7, -9.0),
    "SwingUnarmed02": SoundSpec(
        f"A punch cutting through air. Layers: a crisp fabric snap from a sleeve, a short "
        f"airy whoosh, a light low rush. Very short. {_STYLE}",
        0.7, -9.0),

    # ---- Impacts on flesh: the default row, by far the most-heard sound in the game ----
    "BladeFlesh01": SoundSpec(
        f"A sword blade cutting deep into an unarmoured body. Layers: a sharp wet slicing "
        f"transient, a meaty flesh body beneath it, a dull bone knock inside, and a short "
        f"sub-bass thump. Wet, weighty, brutal. {_STYLE}",
        1.0, -3.0),
    "BladeFlesh02": SoundSpec(
        f"A blade slashing across flesh. Layers: a wet cutting crack, a thick meat body, a "
        f"faint bone scrape through the middle, a low thump underneath. Slightly duller than "
        f"a clean deep cut. {_STYLE}",
        1.0, -3.0),
    "BladeFlesh03": SoundSpec(
        f"A sword chopping into a torso. Layers: a hard wet impact transient, a dense flesh "
        f"body, a sharp bone crack, and a sub-bass drop. Heavier and lower than a glancing "
        f"slash. {_STYLE}",
        1.0, -3.0),

    "AxeFlesh01": SoundSpec(
        f"An axe head burying itself in an unarmoured body. Layers: a heavy wet chopping "
        f"transient, a thick meat body, a loud splintering bone crack, and a deep sub-bass "
        f"thump. Brutal, cleaving, low. {_STYLE}",
        1.1, -3.0),
    "AxeFlesh02": SoundSpec(
        f"A large axe cleaving into flesh and bone. Layers: a blunt wet crunch, a dense body "
        f"impact, a splintering crack, a long sub-bass drop. Heavier than a sword "
        f"cut. {_STYLE}",
        1.1, -3.0),
    "AxeFlesh03": SoundSpec(
        f"An axe chopping into a body. Layers: a wet heavy transient, a meaty body, a dull "
        f"bone break, and a low thump. Slightly shorter than a full cleave. {_STYLE}",
        1.0, -3.0),

    "BluntFlesh01": SoundSpec(
        f"An iron mace slamming into an unarmoured body. Layers: a dull heavy thud "
        f"transient, a dense muffled flesh body, a deep bone crunch, and a strong sub-bass "
        f"drop. No metallic ring, all weight. {_STYLE}",
        1.1, -3.0),
    "BluntFlesh02": SoundSpec(
        f"A blunt weapon crushing into a torso. Layers: a muffled heavy impact, a thick body "
        f"thump, a bone crack inside, a long low sub-bass tail. Dull and "
        f"punishing. {_STYLE}",
        1.1, -3.0),
    "BluntFlesh03": SoundSpec(
        f"A heavy club striking a body. Layers: a dull thick thud, a compressed flesh body, "
        f"a faint bone knock, and a sub-bass thump. Shorter and drier than a full mace "
        f"crush. {_STYLE}",
        1.0, -3.0),

    "FistFlesh01": SoundSpec(
        f"A bare fist landing hard on a body. Layers: a tight slapping transient, a compact "
        f"muffled flesh body, a faint low thump underneath. Punchy and close, much lighter "
        f"than a weapon hit. {_STYLE}",
        0.8, -5.0),
    "FistFlesh02": SoundSpec(
        f"A punch connecting with a torso. Layers: a dull skin-on-body slap, a short "
        f"compressed body thud, a light sub-bass tap. Short and dry. {_STYLE}",
        0.8, -5.0),

    # ---- Impacts on metal: plate and mail armour ----
    "BladeMetal01": SoundSpec(
        f"A sword blade striking steel plate armour. Layers: a bright metallic clang "
        f"transient, a ringing steel body, a dull padded thud beneath the plate, and a short "
        f"sub-bass thump. Ringing metal tail. {_STYLE}",
        1.2, -3.5),
    "BladeMetal02": SoundSpec(
        f"A blade skidding off steel armour at an angle. Layers: a bright metallic scrape "
        f"and ring, a shorter steel body, a muffled thud underneath, a low thump. More "
        f"scrape than clang. {_STYLE}",
        1.2, -3.5),
    "BladeMetal03": SoundSpec(
        f"A sword hitting chainmail. Layers: a rattling jingle of steel rings, a duller "
        f"metallic body than plate, a padded thud beneath, and a low thump. Rattly, not "
        f"ringing. {_STYLE}",
        1.1, -3.5),

    "AxeMetal01": SoundSpec(
        f"An axe head crashing into steel plate armour. Layers: a heavy dull metallic clang, "
        f"a deep dented steel body, a padded thud beneath the plate, and a strong sub-bass "
        f"drop. Lower and heavier than a sword clang. {_STYLE}",
        1.2, -3.5),
    "AxeMetal02": SoundSpec(
        f"A heavy axe biting into armour. Layers: a blunt metallic crunch, a dented steel "
        f"body, a muffled impact beneath, a long low tail. Crushing rather than "
        f"ringing. {_STYLE}",
        1.2, -3.5),

    "BluntMetal01": SoundSpec(
        f"An iron mace crushing steel plate armour. Layers: a deep dull metallic bash, a "
        f"heavily dented steel body with little ring, a padded thud beneath, and a big "
        f"sub-bass drop. Crushing and low. {_STYLE}",
        1.2, -3.5),
    "BluntMetal02": SoundSpec(
        f"A blunt weapon slamming into armour. Layers: a muffled heavy metallic thud, a dead "
        f"dented steel body, a padded impact underneath, a long sub-bass tail. Almost no "
        f"ringing. {_STYLE}",
        1.2, -3.5),

    # ---- Special outcomes ----
    "Crit01": SoundSpec(
        f"An extra impact layer stacked on a devastating critical hit. Layers: a hard "
        f"cracking transient, a deep bone-breaking crunch, and a heavy sub-bass drop with a "
        f"short tail. Sits on top of a normal hit, so no metal and no air. {_STYLE}",
        1.0, -3.0),
    "Crit02": SoundSpec(
        f"A brutal reinforcing crunch for a critical strike. Layers: a sharp snapping crack, "
        f"a dense low body crunch, a strong sub-bass thump. Punctuation on top of an "
        f"existing hit. {_STYLE}",
        1.0, -3.0),

    "Miss01": SoundSpec(
        f"A weapon swing that hits nothing but air. Layers: a fast airy whoosh passing the "
        f"listener, a faint low wind rush, and a quick fading tail. Empty and unresolved, no "
        f"impact of any kind. {_STYLE}",
        0.8, -10.0),
    "Miss02": SoundSpec(
        f"A missed weapon swing cutting past a target. Layers: an airy whoosh sweeping by, a "
        f"soft low rush beneath, a short fade. No contact. {_STYLE}",
        0.8, -10.0),
    "Miss03": SoundSpec(
        f"A blade whiffing through empty air. Layers: a thin whistle, a light whoosh body, a "
        f"quick decay. Clearly a miss. {_STYLE}",
        0.8, -10.0),

    "Parry01": SoundSpec(
        f"Two steel blades colliding as one parries the other. Layers: a bright ringing "
        f"metal-on-metal clash, a sharp sliding scrape as the blades slide apart, a steel "
        f"ring tail. Clean, bright, no body impact. {_STYLE}",
        1.3, -3.5),
    "Parry02": SoundSpec(
        f"A sword deflecting an incoming strike. Layers: a hard metallic clang, a shorter "
        f"scraping slide, a ringing decay. Sharp and defensive. {_STYLE}",
        1.2, -3.5),

    "Block01": SoundSpec(
        f"A weapon blow stopped dead by a heavy wooden shield with an iron boss. Layers: a "
        f"solid woody thunk transient, a dull iron clang from the boss, a dense dead body "
        f"with almost no ring, and a low thump. Absorbed and final. {_STYLE}",
        1.1, -3.5),
    "Block02": SoundSpec(
        f"A strike caught flat on a shield. Layers: a heavy wooden impact, a muted metallic "
        f"rim clank, a dead thud body, a short low tail. Blunt and stopped. {_STYLE}",
        1.1, -3.5),
    # ---- Impacts on wood: training dummies, practice posts, shield rims -------------------
    # The training dummy is the first thing most players ever hit, so a wet flesh impact on a
    # straw-and-timber mannequin is the single most noticeable wrong note in the set.
    "BladeWood01": SoundSpec(
        f"A sword blade biting into a wooden training dummy. Layers: a sharp splintering wood "
        f"crack, a hollow timber body knock beneath it, a dry straw rustle, and a short low "
        f"thump. Dry and woody, nothing wet. {_STYLE}",
        1.0, -3.5),
    "BladeWood02": SoundSpec(
        f"A blade chopping into timber. Layers: a crisp wood split transient, a hollow post "
        f"resonance, a faint straw shift, a low thud. Drier and higher-pitched than a cut into "
        f"flesh. {_STYLE}",
        1.0, -3.5),
    "AxeWood01": SoundSpec(
        f"An axe head sinking deep into a wooden post. Layers: a heavy splitting crack, a deep "
        f"hollow timber boom, splintering fibres tearing, and a strong low thump. Deep and "
        f"satisfying. {_STYLE}",
        1.1, -3.5),
    "AxeWood02": SoundSpec(
        f"A large axe cleaving into a practice dummy. Layers: a loud wood split, a hollow body "
        f"resonance, straw and splinters scattering, and a sub-bass thud. {_STYLE}",
        1.1, -3.5),
    "BluntWood01": SoundSpec(
        f"An iron mace slamming into a wooden training dummy. Layers: a dull heavy timber thud, "
        f"a hollow booming post resonance, a dry straw compression, and a deep sub-bass drop. "
        f"No splintering, all weight. {_STYLE}",
        1.1, -3.5),
    "BluntWood02": SoundSpec(
        f"A blunt weapon striking a wooden post. Layers: a muffled woody knock, a hollow "
        f"resonant body, a faint creak from the frame, and a low thump. {_STYLE}",
        1.0, -3.5),
    "FistWood01": SoundSpec(
        f"A bare fist striking a wooden training dummy. Layers: a dry knuckle knock on timber, "
        f"a short hollow post resonance, a faint straw rustle, and a light low tap. Small and "
        f"dry. {_STYLE}",
        0.8, -5.0),
}
