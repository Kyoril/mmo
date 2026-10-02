# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Prompt table for the mage spell sound effects.

Generated through the ElevenLabs MCP connector (``eleven_text_to_sound_v2``); this file is
the checked-in record of what was asked for. The prompting rules from ``warrior.py`` apply
unchanged: every prompt names its layers in order and asks for a designed tail, because a
"dry, isolated" prompt produces thin realistic foley instead of layered game audio
(``tools/tests/test_sfx_recipes.py`` enforces both, and the 450-character model limit).

Art direction is stylized high fantasy in the register of a AAA fantasy MMO, with one
sonic identity per school so a player can tell spells apart with their eyes closed:

* **Frost** -- crystalline: glassy chimes, cracking ice, cold airy wind, a bell-like tail.
* **Fire** -- roaring: a whoosh of ignition, crackling flame, a deep explosive body.
* **Arcane** -- tonal: shimmering resonant chimes, a choral-synth pad, sparkling glitter.

The three ``*Channel`` sounds play while a cast bar fills. They are generated with the
model's ``loop`` option and fetched with ``fetch.py --loop``, which keeps both ends of the
clip intact -- the normal trim and anti-click fades would break the seam and make the loop
audibly pulse. Every other sound is a one-shot.

Frostbolt and Fireball impacts are the most repeated sounds in the class, so each has two
takes shipped as one shuffle-bag catalog entry, like the warrior's Strike.

``duration`` is generous on purpose (``trim_silence`` removes what the model does not use);
``target_dbfs`` sets the mix: impacts hottest, buffs and channels quieter.
"""

from dataclasses import dataclass

PROMPT_INFLUENCE = 0.75


@dataclass(frozen=True)
class SoundSpec:
    prompt: str
    duration: float
    target_dbfs: float
    loop: bool = False


_STYLE = ("Epic AAA fantasy MMO spell sound effect, richly layered and produced, magical "
          "and satisfying, with a designed tail.")

_LOOP = ("Seamless steady loop for a spell being channelled, even level with no attack and "
         "no ending, AAA fantasy MMO magic.")

SOUNDS = {
    # --- Cast channels (looping) ---------------------------------------------------------
    "FrostChannel": SoundSpec(
        f"A mage gathering frost magic in an open hand. Layers: a soft swirling icy wind, "
        f"delicate glassy crystalline chimes forming, faint crackling frost, and a cold airy "
        f"shimmering pad underneath. {_LOOP}",
        4.0, -9.0, loop=True),
    "FireChannel": SoundSpec(
        f"A mage gathering fire magic in an open hand. Layers: a low steady roar of "
        f"contained flame, soft crackling embers, a warm breathing whoosh, and a deep warm "
        f"hum underneath. {_LOOP}",
        4.0, -9.0, loop=True),
    "ArcaneChannel": SoundSpec(
        f"A mage weaving arcane magic between their hands. Layers: a shimmering resonant "
        f"tonal hum, sparkling glittering chimes drifting, a soft ethereal choral synth pad, "
        f"and a gentle pulsing magical warble. {_LOOP}",
        4.0, -9.0, loop=True),

    # --- Frost ---------------------------------------------------------------------------
    "FrostboltCast": SoundSpec(
        f"A mage launching a bolt of ice. Layers: a sharp crystalline snap of release, a "
        f"fast cold whoosh flying away, glassy shimmering chimes, and a soft low thump. "
        f"Short icy trailing tail. {_STYLE}",
        1.5, -4.0),
    "FrostboltImpact01": SoundSpec(
        f"A bolt of magical ice shattering against an enemy. Layers: a hard crystalline "
        f"crack, ice shards shattering and scattering, a cold hissing burst of frost, and a "
        f"punchy low thump. Glassy tinkling tail. {_STYLE}",
        1.8, -3.0),
    "FrostboltImpact02": SoundSpec(
        f"A frost bolt exploding into an icy burst on impact. Layers: a dense ice impact "
        f"crunch, bright glassy fragments raining down, a freezing breath of cold wind, and "
        f"a deep sub thump. Crystalline ringing tail. {_STYLE}",
        1.8, -3.0),
    "IceLanceCast": SoundSpec(
        f"A mage hurling a razor sharp lance of ice. Layers: a quick crystalline whip crack, "
        f"a piercing high whistle cutting through the air, a glassy ring, and a tight low "
        f"punch. Fast and sharp, short icy tail. {_STYLE}",
        1.2, -4.0),
    "IceLanceImpact": SoundSpec(
        f"A lance of ice piercing and shattering against an enemy. Layers: a piercing stab, "
        f"a bright explosive glass shatter, tinkling ice fragments, and a sharp sub-bass "
        f"hit. Brittle crystalline tail. {_STYLE}",
        1.5, -3.0),
    "FrostNova": SoundSpec(
        f"A burst of frost magic exploding outward and freezing everything around the "
        f"caster. Layers: a deep concussive whump, a wave of ice rapidly crackling and "
        f"freezing across the ground, a cold rushing wind blast, and shimmering crystalline "
        f"bells. Long freezing tail. {_STYLE}",
        2.5, -2.5),
    "FrostArmor": SoundSpec(
        f"A mage encasing themselves in protective frost armor. Layers: a rising icy "
        f"shimmer, ice crystals forming and locking together with glassy clinks, a cold "
        f"airy swirl, and a soft resonant low hum. Calm protective tail. {_STYLE}",
        2.2, -5.0),
    "Chilled": SoundSpec(
        f"An attacker suddenly chilled and slowed by frost. Layers: a quick cold hiss, a "
        f"soft crackle of frost forming, a small glassy tinkle, and a dull low thud. Brief "
        f"and subtle, short frosty tail. {_STYLE}",
        1.0, -7.0),
    "Frostburn": SoundSpec(
        f"A creeping curse of frost seeping into an enemy. Layers: a cold hissing breath, "
        f"slow crackling frost spreading over skin, an eerie glassy shimmer, and a low "
        f"ominous cold swell. Lingering icy tail. {_STYLE}",
        1.8, -5.0),

    # --- Fire ----------------------------------------------------------------------------
    "FireballCast": SoundSpec(
        f"A mage hurling a blazing fireball. Layers: a fierce ignition burst, a roaring "
        f"whoosh of flame flying away, crackling embers, and a deep low thump. Short "
        f"burning trailing tail. {_STYLE}",
        1.5, -4.0),
    "FireballImpact01": SoundSpec(
        f"A fireball exploding against an enemy. Layers: a punchy fiery explosion, a "
        f"roaring burst of flame, crackling sizzling embers scattering, and a heavy "
        f"sub-bass boom. Smouldering crackling tail. {_STYLE}",
        2.0, -2.5),
    "FireballImpact02": SoundSpec(
        f"A blazing ball of fire bursting on impact. Layers: a dense concussive fire blast, "
        f"a whooshing flare of flame engulfing the target, popping crackling embers, and a "
        f"deep rumbling boom. Burning crackle tail. {_STYLE}",
        2.0, -2.5),
    "FireBlast": SoundSpec(
        f"A sudden eruption of fire bursting up from beneath an enemy. Layers: a sharp "
        f"ignition snap, a violent roaring column of flame erupting upward, crackling "
        f"exploding embers, and a huge sub-bass boom. Intense, with a smouldering tail. "
        f"{_STYLE}",
        2.0, -2.0),
    "FireBarrageShot": SoundSpec(
        f"A mage firing a quick fire bolt in a rapid barrage. Layers: a short punchy flame "
        f"burst, a fast fiery whoosh, a few crackling sparks, and a small low thump. Very "
        f"short and snappy with a brief burning tail. {_STYLE}",
        0.8, -6.0),
    "FireBarrageImpact": SoundSpec(
        f"A small fire bolt hitting an enemy as part of a rapid barrage. Layers: a compact "
        f"fiery pop, a short flare of flame, sizzling embers, and a tight low thud. Short "
        f"and punchy with a brief crackle tail. {_STYLE}",
        0.9, -5.0),

    # --- Arcane --------------------------------------------------------------------------
    "ArcaneIntellect": SoundSpec(
        f"A mage granting arcane brilliance to an ally. Layers: an uplifting rising "
        f"shimmering chime, a sparkling glittering cascade, a warm ethereal choral synth "
        f"swell, and a soft resonant low tone. Bright and wise, with a glowing tail. No "
        f"singing, no words. {_STYLE}",
        2.4, -5.0),
    "Sleep": SoundSpec(
        f"A mage casting a magical sleep on an enemy. Layers: a soft descending dreamy "
        f"chime, a hazy shimmering lullaby-like synth tone, a gentle airy whoosh settling "
        f"down, and a warm low hum fading out. Drowsy and hypnotic, with a long soft tail. "
        f"No singing, no words. {_STYLE}",
        2.4, -5.0),
    "ArcaneDisruption": SoundSpec(
        f"A burst of arcane magic interrupting and silencing an enemy spellcaster. Layers: "
        f"a sharp tonal arcane crack, a glitching warping shimmer that cuts off abruptly, a "
        f"reverse whoosh sucking energy away, and a tight sub-bass punch. Disruptive, with a "
        f"short ringing tail. {_STYLE}",
        1.6, -3.5),
    "ArcanePulse": SoundSpec(
        f"A pulse of unstable arcane energy exploding outward from a mage. Layers: a deep "
        f"resonant arcane boom, a wide shimmering wave of tonal energy expanding outward, "
        f"crackling sparkling arcane static, and a heavy sub-bass swell. Powerful, with a "
        f"long shimmering tail. {_STYLE}",
        2.5, -2.5),
    "ManastoneConjure": SoundSpec(
        f"A mage conjuring a glowing crystal of stored mana. Layers: a rising shimmering "
        f"arcane swirl, a resonant crystalline chime as the gem forms, a soft glassy clink, "
        f"and a warm humming tonal glow. Satisfying and magical, with a sustained glittering "
        f"tail. {_STYLE}",
        2.2, -4.5),
}

# eleven_text_to_sound_v2 rejects anything longer than this, silently (see warrior.py).
MAX_PROMPT_CHARS = 450
