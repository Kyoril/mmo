# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Prompt table for the cleric spell sound effects.

Generated through the ElevenLabs MCP connector (``eleven_text_to_sound_v2``), which a script
cannot invoke -- this file is the checked-in record of *what* was asked for, so the set can be
regenerated consistently. See ``warrior.py`` for why every prompt names its layers and asks
for a tail: "dry, isolated, no reverb" prompting produces thin foley, not ability audio.

Art direction is radiant stylized holy magic in the register of a AAA fantasy MMO: bright
bell and chime transients, choir-like *pads* (never words -- a baked-in voice would clash
with every race and gender, and would read as dialogue), warm shimmering energy, a soft
sub-bass for weight. Damage (Smite, Holy Fire) is brighter and harder-edged; healing is
softer, slower and warmer; protection leans metallic and resonant.

Three sounds are **loops** (``loop=True``): the cast-bar channels. They are generated with
the model's seamless-loop mode and postprocessed with ``postprocess.process_loop``, which
neither trims nor fades -- trimming a loop cuts its seam and a fade puts a hole in it once
per cycle. Loops sit quieter than one-shots so a long cast does not mask combat.

``duration`` is generous for one-shots (``trim_silence`` removes what the model does not use)
and exact for loops (the loop length is the cycle length; ~3-4 s avoids an audible repeat).
"""

from dataclasses import dataclass

PROMPT_INFLUENCE = 0.75

# eleven_text_to_sound_v2 silently fails anything longer than this; see warrior.py.
MAX_PROMPT_CHARS = 450


@dataclass(frozen=True)
class SoundSpec:
    prompt: str
    duration: float
    target_dbfs: float
    loop: bool = False


_STYLE = ("Epic AAA fantasy MMO holy spell sound effect, richly layered and produced, "
          "radiant and magical, with a designed tail.")
_LOOP_STYLE = ("Seamless sustained loop for a AAA fantasy MMO spell channel, even and "
               "steady with no start or end.")

SOUNDS = {
    # --- cast-bar loops -------------------------------------------------------------------
    "HolyCastLoop": SoundSpec(
        f"A priest channelling gentle holy light in their hands. Layers: a warm airy choir-like "
        f"pad with no words, soft shimmering crystalline sparkles, a slow breathing glow swell, "
        f"and a quiet warm low hum underneath. Calm, reverent, sustained. {_LOOP_STYLE}",
        4.0, -9.0, loop=True),
    "HolyFireCastLoop": SoundSpec(
        f"A priest conjuring sacred fire between their hands. Layers: a soft roaring holy "
        f"flame crackle, bright shimmering embers, a radiant tonal drone with no words, and a "
        f"low warm rumble underneath. Intense but controlled, sustained. {_LOOP_STYLE}",
        4.0, -9.0, loop=True),
    "ResurrectionChannel": SoundSpec(
        f"A long solemn ritual calling a fallen soul back to life. Layers: a slow wordless "
        f"angelic choir pad, gentle tolling distant bells, soft rising shimmering light, and a "
        f"deep sacred organ-like drone underneath. Reverent and awe-filled, sustained. "
        f"{_LOOP_STYLE}",
        4.0, -9.0, loop=True),

    # --- releases and impacts ------------------------------------------------------------
    "HolyRelease": SoundSpec(
        f"A burst of holy light released from a priest's hand. Layers: a bright crystalline "
        f"bell strike, a soft airy whoosh outward, a radiant shimmering sparkle bloom, and a "
        f"gentle sub thump. Short and clean, with a sparkling tail. {_STYLE}",
        1.5, -5.0),
    "HealingLight": SoundSpec(
        f"Warm healing light washing over an ally. Layers: a soft glassy chime onset, a warm "
        f"wordless choir swell blooming upward, gentle rising harp-like glissando sparkles, "
        f"and a soft warm sub swell. Soothing and uplifting, with a long glowing shimmer "
        f"tail. {_STYLE}",
        2.4, -4.0),
    "Smite": SoundSpec(
        f"A bolt of divine judgement striking an enemy from above. Layers: a sharp bright "
        f"electric crack of holy light, a ringing cathedral bell hit, a radiant energy blast "
        f"with crackling sparks, and a heavy deep sub-bass impact. Righteous and punchy, with a "
        f"ringing tail. {_STYLE}",
        2.0, -2.5),
    "HolyFire": SoundSpec(
        f"Holy flames erupting around an enemy. Layers: a bright igniting whoomp of sacred "
        f"fire, a roaring rising flame burst, a radiant shimmering bell-like tone inside the "
        f"fire, crackling embers, and a deep sub-bass boom. Fierce and holy, with a crackling "
        f"burning tail. {_STYLE}",
        2.4, -3.0),
    "HolyFireTick": SoundSpec(
        f"A short flare of holy fire burning an enemy. Layers: a quick soft fire flare puff, a "
        f"faint bright shimmer, crackling embers, and a light low thump. Small and brief, "
        f"with a short sizzling tail. {_STYLE}",
        1.0, -7.0),
    "DivineVitality": SoundSpec(
        f"A priest blessing an ally with divine vitality and strength. Layers: a warm golden "
        f"bell chord, a rising wordless choir swell, bright ascending shimmering sparkles, a "
        f"strong heartbeat-like warm pulse, and a soft sub swell. Empowering and uplifting, "
        f"with a glowing tail. {_STYLE}",
        2.6, -4.0),
    "RenewingLight": SoundSpec(
        f"Gentle renewing light settling on an ally like soft rain. Layers: delicate falling "
        f"crystalline chimes, a soft airy breath of light, a warm gentle wordless pad, and a "
        f"quiet warm low bloom. Peaceful and restorative, with a long soft shimmer tail. "
        f"{_STYLE}",
        2.2, -5.0),
    "RenewingLightTick": SoundSpec(
        f"A tiny pulse of healing light. Layers: a single soft glassy chime, a faint rising "
        f"sparkle, and a whisper of warm air. Very small and gentle, with a short shimmer "
        f"tail. {_STYLE}",
        1.0, -9.0),
    "HealingAura": SoundSpec(
        f"A priest consecrating the ground with a healing aura. Layers: a deep warm gong-like "
        f"bell swell, a wide wordless choir bloom spreading outward, gentle shimmering "
        f"sparkles circling, and a soft warm sub swell. Sacred and expansive, with a long "
        f"radiant tail. {_STYLE}",
        3.0, -4.0),
    "ProtectiveAura": SoundSpec(
        f"A priest raising a protective holy ward around their allies. Layers: a resonant "
        f"metallic shield-like shimmer ringing up, a deep solid bell tone, a crystalline "
        f"barrier forming with glassy harmonics, and a firm grounded sub swell. Steadfast and "
        f"safe, with a ringing tail. {_STYLE}",
        3.0, -4.0),
    "Faithward": SoundSpec(
        f"A shimmering holy barrier flaring around an ally. Layers: a quick bright glassy "
        f"shield shimmer, a resonant metallic ring, swirling sparkles orbiting, and a soft low "
        f"thump. Protective and reassuring, with a short ringing tail. {_STYLE}",
        1.6, -5.0),
    "Resurrection": SoundSpec(
        f"A fallen hero brought back to life by divine light. Layers: a huge radiant "
        f"cathedral bell strike, a soaring wordless angelic choir swell, a bright cascade of "
        f"rising shimmering light, a warm heartbeat returning, and a deep sub-bass bloom. "
        f"Awe-inspiring and triumphant, with a long glowing tail. {_STYLE}",
        3.5, -3.0),
}
