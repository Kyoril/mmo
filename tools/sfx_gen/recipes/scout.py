# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Prompt table and layer mixes for the scout ability sounds (``docs/scout_spell_visuals.md``).

Generated through the ElevenLabs MCP connector (``eleven_text_to_sound_v2``); this file is the
checked-in record of what was asked for and how each final sound was assembled.

**Layered, not single-prompt** (see ``human.py``): the model renders one dominant sound per
prompt, however many elements the prompt lists. Every final is built from separately
generated layers -- each prompt names one element and excludes the others -- mixed by
``tools/sfx_gen/layer.py`` according to ``MIXES``. Layers are shared between finals where the
element is the same (one blade whoosh, one flesh slice), so the techniques sound like one
fighter, and two takes of the same layer give Twin Slash its two blades.

Sonic identity: **quick, close and sharp.** Thin fast blade swishes, wet cuts, a bright steel
ring for precision, cloth and smoke for evasion; tonal elements stay small (a reticle sting,
a crystalline focus tone, a stun twinkle). Voiceless throughout -- a grunt would clash across
races and genders. The scout attacks every second, so the strikes are short and sit below the
big cooldowns in level.

Raw takes are named ``<SoundKey>_take<n>.wav``; render every final with::

    py tools/sfx_gen/recipes/scout.py --render generated/scout_visuals/sfx/layers
"""

import argparse
import os
import sys
from dataclasses import dataclass
from typing import Optional

PROMPT_INFLUENCE = 0.75

# eleven_text_to_sound_v2 silently fails anything longer than this; see warrior.py.
MAX_PROMPT_CHARS = 450

OUTPUT_DIR = "data/client/Sound/Spells/Scout"


@dataclass(frozen=True)
class SoundSpec:
    prompt: str
    duration: float
    target_dbfs: float
    loop: bool = False
    prompt_influence: float = PROMPT_INFLUENCE


@dataclass(frozen=True)
class MixLayer:
    """One layer of a mix: which generated layer (and take) goes where, how loud."""

    sound: str            # key into SOUNDS
    take: int             # the chosen generation (1-4) of that layer
    offset: float = 0.0   # seconds
    gain_db: float = 0.0
    length: Optional[float] = None


_GAME = "Fantasy game rogue ability sound."

# Layers are generated at -3 dBFS each; mixes are normalised again afterwards.
SOUNDS = {
    # --- Blades -------------------------------------------------------------------------------
    "Blade_Whoosh_Fast": SoundSpec(
        "A very fast thin dagger swish slicing through the air: a sharp high whistling swoosh "
        "that passes close by in a fraction of a second, crisp and airy. Only the swish: no "
        "impact, no voice, no music. " + _GAME,
        duration=0.8, target_dbfs=-3.0),
    "Blade_Whoosh_Heavy": SoundSpec(
        "A powerful wide sword swing whooshing through the air: a full deep swoosh with a "
        "rising edge, a committed heavy blow. Only the swing: no impact, no voice, no music. "
        + _GAME,
        duration=1.0, target_dbfs=-3.0),
    "Slice_Flesh": SoundSpec(
        "A sharp blade slicing cleanly through flesh and leather armor: a quick wet cutting "
        "slash with a crisp tearing edge, punchy and close. Only the cut: no whoosh, no voice, "
        "no music. " + _GAME,
        duration=0.8, target_dbfs=-3.0),
    "Stab_Thrust": SoundSpec(
        "A quick dagger stab punching into a body: a short tight thrusting thunk with a small "
        "wet squelch, very fast attack. Only the stab: no whoosh, no metal ring, no voice, no "
        "music. " + _GAME,
        duration=0.6, target_dbfs=-3.0),
    "Steel_Ring": SoundSpec(
        "A thin steel dagger blade ringing out: a bright clean metallic shing like a blade "
        "drawn fast across a whetstone edge, with a shimmering high ring-out tail. Only the "
        "metal ring: no impact, no voice, no music. " + _GAME,
        duration=1.4, target_dbfs=-3.0),
    "Blood_Spatter": SoundSpec(
        "A short wet blood spatter: liquid droplets splashing and dripping onto leather and "
        "stone, a quick gory splash with a brief trickle. Only the splatter: no blade, no "
        "voice, no music. " + _GAME,
        duration=0.8, target_dbfs=-3.0),
    "Crimson_Sting": SoundSpec(
        "A dark sharp magical sting: a quick reversed hiss sweeping into a tight crimson "
        "energy crack, like a curse biting into a wound, short and menacing. Only the magical "
        "sting: no blade, no voice, no music. " + _GAME,
        duration=1.0, target_dbfs=-3.0),
    "Impact_Thump": SoundSpec(
        "A short heavy body blow: a deep punchy low thump with a tight sub-bass kick, felt "
        "more than heard, quick decay. Only the thump: no blade, no metal, no voice, no music. "
        + _GAME,
        duration=0.8, target_dbfs=-3.0),

    # --- Knockout ----------------------------------------------------------------------------
    "Pommel_Bonk": SoundSpec(
        "A dagger pommel cracking hard against a skull and helmet: a hollow solid knock, a "
        "blunt clonk with a short woody resonance. Only the knock: no blade, no chimes, no "
        "voice, no music. " + _GAME,
        duration=0.8, target_dbfs=-3.0),
    "Stun_Chime": SoundSpec(
        "A dizzy stunned sound: small bright tinkling bells and twinkling chimes circling "
        "round and round, wobbling in pitch, light and playful, fading out. Only the chimes: "
        "no impact, no voice, no music. " + _GAME,
        duration=2.0, target_dbfs=-3.0),

    # --- Throwing Knife ----------------------------------------------------------------------
    "Knife_Spin_Throw": SoundSpec(
        "A throwing knife hurled hard and spinning end over end: a quick whirring "
        "whip-whip-whip whoosh of a small blade rotating fast through the air, passing away. "
        "Only the spinning knife: no impact, no voice, no music. " + _GAME,
        duration=1.0, target_dbfs=-3.0),
    "Knife_Thunk": SoundSpec(
        "A thrown knife sinking deep into a target: a sharp solid thunk with a tiny metallic "
        "vibrating twang as the blade quivers. Only the hit: no whoosh, no voice, no music. "
        + _GAME,
        duration=0.9, target_dbfs=-3.0),

    # --- Mark Weakness -----------------------------------------------------------------------
    "Mark_Tone": SoundSpec(
        "A tense predatory targeting sting: a dark low plucked string note bending down, with "
        "a crisp mechanical click like a crossbow being cocked and a faint menacing shimmer. "
        "Only the sting: no drums, no voice, no music bed. " + _GAME,
        duration=1.6, target_dbfs=-3.0),
    "Heartbeat": SoundSpec(
        "Two heavy deep heartbeats, close and muffled, the thump-thump of a racing pulse, "
        "with a soft low resonance. Only the heartbeat: no music, no voice, no other sounds. "
        + _GAME,
        duration=1.2, target_dbfs=-3.0),

    # --- Evasion -----------------------------------------------------------------------------
    "Shadow_Whoosh": SoundSpec(
        "A fast evasive side step: a sharp swirl of a heavy cloak whipping through the air and "
        "a dark airy shadow swoosh that vanishes. Only the cloak and shadow whoosh: no "
        "footsteps, no voice, no music. " + _GAME,
        duration=1.0, target_dbfs=-3.0),
    "Footstep_Scuff": SoundSpec(
        "A quick nimble push-off: leather boots scuffing and skidding on dirt and gravel, one "
        "fast scrape and a light hop. Only the footwork: no whoosh, no voice, no music. "
        + _GAME,
        duration=0.8, target_dbfs=-3.0),
    "Smoke_Pop": SoundSpec(
        "A smoke bomb bursting at someone's feet: a sharp muffled pop followed by a thick "
        "rushing hiss of smoke billowing out and slowly settling. Only the pop and smoke: no "
        "shimmer, no voice, no music. " + _GAME,
        duration=2.0, target_dbfs=-3.0),
    "Glass_Shimmer": SoundSpec(
        "A glassy mirror illusion shimmer: bright crystalline glints, tiny glass chimes and a "
        "sparkling magical sweep that flickers left and right and fades away. Only the "
        "shimmer: no impact, no voice, no music. " + _GAME,
        duration=1.8, target_dbfs=-3.0),

    # --- Sprint / Fever Pitch / Steady Hand -------------------------------------------------
    "Wind_Rush": SoundSpec(
        "A burst of speed: a rushing gust of wind accelerating past the ears, a rising airy "
        "whoosh that swells quickly then streams away. Only the wind: no footsteps, no voice, "
        "no music. " + _GAME,
        duration=1.6, target_dbfs=-3.0),
    "Energy_Surge": SoundSpec(
        "An adrenaline energy surge: a rising crackling electric buzz that climbs quickly in "
        "pitch with sparkling static and a bright energetic lift at the top. Only the energy: "
        "no heartbeat, no voice, no music. " + _GAME,
        duration=1.6, target_dbfs=-3.0),
    "Focus_Tone": SoundSpec(
        "A calm moment of perfect focus: a pure crystalline tone like a rubbed glass rim, "
        "steady and clear, with a soft airy inhale and a gentle shimmering tail. Only the tone: "
        "no metal, no voice, no music. " + _GAME,
        duration=2.0, target_dbfs=-3.0),
}

# Finals assembled from the layers above. Strike timing: the slice lands ~0.12-0.15 s into
# the whoosh, which is roughly where the 1H attack clips connect. Takes were preselected on
# measurements (peak position, attack, spectral centroid, low-band share): Smoke_Pop 1/2 and
# Slice_Flesh 3 peak past half their length and were rejected; Mark_Tone 3 is the dark low
# pluck the prompt asked for (85 % of its energy under 200 Hz), 1 is the bright cocking click.
MIXES = {
    "WoundingStrike": {
        "target_dbfs": -4.0,
        "layers": [
            MixLayer("Blade_Whoosh_Fast", take=1, offset=0.0, gain_db=-2.0),
            MixLayer("Slice_Flesh", take=1, offset=0.12, gain_db=0.0),
            MixLayer("Blood_Spatter", take=2, offset=0.16, gain_db=-7.0),
            MixLayer("Crimson_Sting", take=2, offset=0.0, gain_db=-9.0),
        ],
    },
    "RuthlessStrike": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("Blade_Whoosh_Heavy", take=2, offset=0.0, gain_db=-2.0),
            MixLayer("Slice_Flesh", take=2, offset=0.15, gain_db=0.0),
            MixLayer("Impact_Thump", take=1, offset=0.15, gain_db=-4.0),
            MixLayer("Steel_Ring", take=1, offset=0.15, gain_db=-7.0, length=0.7),
            MixLayer("Blood_Spatter", take=3, offset=0.19, gain_db=-9.0),
        ],
    },
    "QuickCut01": {
        "target_dbfs": -5.0,
        "layers": [
            MixLayer("Blade_Whoosh_Fast", take=2, offset=0.0, gain_db=-3.0),
            MixLayer("Stab_Thrust", take=1, offset=0.08, gain_db=0.0),
            MixLayer("Steel_Ring", take=2, offset=0.08, gain_db=-10.0, length=0.5),
        ],
    },
    "QuickCut02": {
        "target_dbfs": -5.0,
        "layers": [
            MixLayer("Blade_Whoosh_Fast", take=3, offset=0.0, gain_db=-3.0),
            MixLayer("Stab_Thrust", take=2, offset=0.08, gain_db=0.0),
            MixLayer("Steel_Ring", take=1, offset=0.08, gain_db=-10.0, length=0.5),
        ],
    },
    "TwinSlash": {
        "target_dbfs": -3.5,
        "layers": [
            MixLayer("Blade_Whoosh_Fast", take=1, offset=0.0, gain_db=-3.0),
            MixLayer("Slice_Flesh", take=1, offset=0.10, gain_db=-1.0),
            MixLayer("Blade_Whoosh_Fast", take=3, offset=0.14, gain_db=-3.0),
            MixLayer("Slice_Flesh", take=2, offset=0.24, gain_db=-1.0),
            MixLayer("Steel_Ring", take=1, offset=0.24, gain_db=-8.0, length=0.7),
        ],
    },
    "Knockout": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("Blade_Whoosh_Heavy", take=1, offset=0.0, gain_db=-5.0),
            MixLayer("Pommel_Bonk", take=3, offset=0.10, gain_db=0.0),
            MixLayer("Impact_Thump", take=2, offset=0.10, gain_db=-3.0),
            MixLayer("Stun_Chime", take=3, offset=0.18, gain_db=-6.0),
        ],
    },
    "ThrowingKnifeThrow": {
        "target_dbfs": -4.0,
        "layers": [
            MixLayer("Knife_Spin_Throw", take=2, offset=0.0, gain_db=0.0),
            MixLayer("Steel_Ring", take=2, offset=0.0, gain_db=-11.0, length=0.4),
        ],
    },
    "ThrowingKnifeImpact": {
        "target_dbfs": -3.5,
        "layers": [
            MixLayer("Knife_Thunk", take=2, offset=0.0, gain_db=0.0),
            MixLayer("Blood_Spatter", take=2, offset=0.03, gain_db=-9.0),
        ],
    },
    "MarkWeakness": {
        "target_dbfs": -4.0,
        "layers": [
            MixLayer("Mark_Tone", take=3, offset=0.0, gain_db=0.0),
            MixLayer("Mark_Tone", take=1, offset=0.0, gain_db=-5.0),
            MixLayer("Heartbeat", take=1, offset=0.08, gain_db=-6.0),
        ],
    },
    "EvasiveStep": {
        "target_dbfs": -4.0,
        "layers": [
            MixLayer("Shadow_Whoosh", take=3, offset=0.0, gain_db=0.0),
            MixLayer("Shadow_Whoosh", take=1, offset=0.04, gain_db=-5.0),
            MixLayer("Footstep_Scuff", take=1, offset=0.0, gain_db=-4.0),
        ],
    },
    "SmokeAndMirrors": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("Smoke_Pop", take=3, offset=0.0, gain_db=0.0),
            MixLayer("Shadow_Whoosh", take=2, offset=0.05, gain_db=-6.0),
            MixLayer("Glass_Shimmer", take=3, offset=0.12, gain_db=-6.0),
        ],
    },
    "Sprint": {
        "target_dbfs": -4.0,
        "layers": [
            MixLayer("Wind_Rush", take=3, offset=0.0, gain_db=0.0),
            MixLayer("Footstep_Scuff", take=2, offset=0.0, gain_db=-3.0),
        ],
    },
    "FeverPitch": {
        "target_dbfs": -3.5,
        "layers": [
            MixLayer("Heartbeat", take=1, offset=0.0, gain_db=-1.0),
            MixLayer("Energy_Surge", take=1, offset=0.0, gain_db=-3.0),
        ],
    },
    "SteadyHand": {
        "target_dbfs": -4.0,
        "layers": [
            MixLayer("Steel_Ring", take=3, offset=0.0, gain_db=0.0),
            MixLayer("Focus_Tone", take=1, offset=0.05, gain_db=-5.0),
        ],
    },
}

FINAL_ORDER = list(MIXES)


def render(layer_dir: str, out_dir: str = OUTPUT_DIR, only=None) -> None:
    """Render every final (or ``only`` those named) from the raw layer takes."""
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    import layer  # noqa: E402  (tools/sfx_gen/layer.py)

    os.makedirs(out_dir, exist_ok=True)
    for name, mix in MIXES.items():
        if only and name not in only:
            continue
        out = os.path.join(out_dir, name + ".wav")
        info = layer.render(layer.recipe_layers(mix, layer_dir), out, mix["target_dbfs"])
        print(f"{out}: {info}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Render the scout ability sounds.")
    parser.add_argument("--render", metavar="LAYER_DIR", required=True)
    parser.add_argument("--out", default=OUTPUT_DIR)
    parser.add_argument("only", nargs="*")
    args = parser.parse_args()
    render(args.render, args.out, set(args.only))
