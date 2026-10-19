# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Prompt table and layer mixes for the acolyte spell sounds (``docs/acolyte_spell_visuals.md``).

Generated through the ElevenLabs connector (``eleven_text_to_sound_v2``); this file is the
checked-in record of what was asked for and how each final sound was assembled.

**Layered, not single-prompt** (see ``human.py`` and ``scout.py``): the model renders one
dominant sound per prompt, so every one-shot final is built from separately generated layers
-- each prompt names one element and excludes the others -- mixed by ``tools/sfx_gen/layer.py``
according to ``MIXES``. Layers are shared between finals where the element is the same (one
void whoosh, one sub drop, one whisper bed), so the whole kit sounds like one caster.

Sonic identity: **whispering void, wet rot, cold dread.** Shadow is a low swirling hum and
breathy wordless whispers that snap into hollow cracks; decay is wet, bubbling and acidic;
dread is a reversed swell, a cursed bell and a distant wordless wail. Nothing bright or heroic
-- the acolyte is the one class whose magic should make the listener uneasy.

The two cast-bar channels are single generated loops (``SINGLES``), made with the model's
loop option and fetched with ``fetch.py --loop`` so the seam is crossfaded, not trimmed.

Raw takes are named ``<SoundKey>_take<n>.wav``; render every final with::

    py tools/sfx_gen/recipes/acolyte.py --render generated/acolyte_visuals/sfx/layers
"""

import argparse
import os
import sys
from dataclasses import dataclass
from typing import Optional

PROMPT_INFLUENCE = 0.75

# eleven_text_to_sound_v2 silently fails anything longer than this; see warrior.py.
MAX_PROMPT_CHARS = 450

OUTPUT_DIR = "data/client/Sound/Spells/Acolyte"


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


_GAME = "Dark fantasy game warlock spell sound."
_LOOP = "Seamless loop with no attack and no ending."

# Layers are generated at -3 dBFS each (loops quieter); mixes are normalised afterwards.
SOUNDS = {
    # --- Channels (looping, cast bar) ---------------------------------------------------------
    "Shadow_Channel_Loop": SoundSpec(
        "Dark void magic gathering in a hand: a low ominous swirling hum of shadow energy, a "
        "slow breathing pulse with soft rushing air and a faint dissonant shimmer underneath. "
        + _LOOP + " No voice, no music. " + _GAME,
        duration=4.0, target_dbfs=-9.0, loop=True),
    "Blight_Channel_Loop": SoundSpec(
        "Rotting decay magic festering in a hand: a sickly low bubbling and fizzing like acid "
        "eating through wet wood, with slow gurgles and a faint corrosive sizzle. "
        + _LOOP + " No voice, no music. " + _GAME,
        duration=4.0, target_dbfs=-9.0, loop=True),

    # --- Shadow ------------------------------------------------------------------------------
    "Void_Whoosh": SoundSpec(
        "A bolt of dark shadow magic hurled from the hand: a heavy dark whoosh that swells and "
        "rushes away, thick and smoky with a low hollow body. Only the whoosh: no impact, no "
        "voice, no music. " + _GAME,
        duration=1.2, target_dbfs=-3.0),
    "Shadow_Crack": SoundSpec(
        "A shadow bolt detonating on a body: a sharp dark energy crack with a hollow thump, "
        "like black glass bursting inside a muffled drum, very fast decay. Only the burst: no "
        "whoosh, no voice, no music. " + _GAME,
        duration=1.0, target_dbfs=-3.0),
    "Shadow_Sizzle": SoundSpec(
        "Dark magic dissipating after a hit: a crackling static sizzle of shadow energy that "
        "fizzles and hisses away into nothing, a little unstable. Only the sizzle: no impact, "
        "no voice, no music. " + _GAME,
        duration=1.4, target_dbfs=-3.0),
    "Sub_Drop": SoundSpec(
        "A deep cinematic sub-bass drop: a heavy low boom that falls in pitch and rumbles out, "
        "felt more than heard. Only the low boom: no hit, no whoosh, no voice, no music. "
        + _GAME,
        duration=1.6, target_dbfs=-3.0),
    "Whispers": SoundSpec(
        "Eerie breathy ghostly whispers swirling around the listener: many overlapping hushed "
        "wordless whispering voices, sibilant and unintelligible, rising and fading. Only the "
        "whispers: no words, no music, no impact. " + _GAME,
        duration=2.0, target_dbfs=-3.0),

    # --- Decay -------------------------------------------------------------------------------
    "Rot_Splash": SoundSpec(
        "A curse of rot splashing onto a body: a thick wet slimy squelch and a bubbling "
        "gurgling burst of decay, gross and organic, quick. Only the wet burst: no hiss, no "
        "voice, no music. " + _GAME,
        duration=1.0, target_dbfs=-3.0),
    "Acid_Hiss": SoundSpec(
        "Corrosive blight eating into flesh: a sharp acidic hiss and fizz like acid on hot "
        "metal, sputtering and steaming, then thinning out. Only the hiss: no splash, no voice, "
        "no music. " + _GAME,
        duration=1.4, target_dbfs=-3.0),

    # --- Dread -------------------------------------------------------------------------------
    "Reverse_Swell": SoundSpec(
        "A dark reversed swell: a reverse-cymbal style sucking rush of air and shadow that "
        "builds fast and stops abruptly, ominous and tense. Only the swell: no hit, no voice, "
        "no music. " + _GAME,
        duration=1.2, target_dbfs=-3.0),
    "Ghost_Wail": SoundSpec(
        "A distant haunting ghostly wail: an inhuman wordless spectral shriek that bends down "
        "in pitch and echoes away, chilling and hollow. Only the wail: no words, no whispers, "
        "no music. " + _GAME,
        duration=2.0, target_dbfs=-3.0),
    "Dread_Bell": SoundSpec(
        "A cursed funeral bell: one deep heavy toll of a cracked iron bell, dissonant and "
        "metallic, with a long dark ringing hum. Only the bell: no voice, no music bed. "
        + _GAME,
        duration=3.0, target_dbfs=-3.0),
    "Shadow_Wrap": SoundSpec(
        "Shadows wrapping around a body like a cloak: a deep soft velvety whoosh that circles "
        "and closes in, smoky and muffled, ending in a hush. Only the whoosh: no shimmer, no "
        "voice, no music. " + _GAME,
        duration=1.6, target_dbfs=-3.0),

    # --- Talents and procs -------------------------------------------------------------------
    "Void_Burst": SoundSpec(
        "A wave of void energy erupting outward from the ground: a hollow imploding suck "
        "followed by a big dark blooming whoosh that spreads out in a ring. Only the eruption: "
        "no bell, no voice, no music. " + _GAME,
        duration=1.8, target_dbfs=-3.0),
    "Dark_Chime": SoundSpec(
        "A dark crystalline omen: a low glassy chime and an eerie shimmering ring with a slow "
        "minor-key glimmer, cold and mysterious. Only the chime: no whoosh, no voice, no music. "
        + _GAME,
        duration=1.8, target_dbfs=-3.0),
    "Soul_Draw": SoundSpec(
        "A soul being drawn into a warlock: an airy ethereal rushing inward, a ghostly sweep "
        "that spirals closer and is swallowed with a soft low gulp. Only the inward rush: no "
        "words, no music. " + _GAME,
        duration=1.6, target_dbfs=-3.0),
}

# Loops are single generations; ``SINGLES`` maps the final file to (layer, take, target_dbfs).
SINGLES = {
    "ShadowChannelLoop": ("Shadow_Channel_Loop", 1, -9.0),
    "BlightChannelLoop": ("Blight_Channel_Loop", 1, -9.0),
}

# One-shot finals assembled from the layers above. Shadowbolt's impact is two mixes of
# different takes so the most frequent sound in the kit does not repeat exactly. Takes were
# preselected on measurements (peak position, length, spectral centroid, low-band share):
# Void_Burst 3 peaks at 99% of its length and was rejected; Shadow_Crack 2/4 and Dark_Chime 2
# came back half-length; Sub_Drop 3/4 hit at the onset (impacts), 1/2 swell to mid-clip;
# Dark_Chime 1 and 3 are the low glassy tones the prompt asked for (30-38% under 200 Hz).
MIXES = {
    "ShadowboltCast": {
        "target_dbfs": -4.0,
        "layers": [
            MixLayer("Void_Whoosh", take=1, offset=0.0, gain_db=0.0),
            MixLayer("Whispers", take=1, offset=0.0, gain_db=-9.0, length=0.9),
        ],
    },
    "ShadowboltImpact01": {
        "target_dbfs": -3.5,
        "layers": [
            MixLayer("Shadow_Crack", take=1, offset=0.0, gain_db=0.0),
            MixLayer("Sub_Drop", take=4, offset=0.0, gain_db=-6.0, length=1.0),
            MixLayer("Shadow_Sizzle", take=1, offset=0.06, gain_db=-7.0),
        ],
    },
    "ShadowboltImpact02": {
        "target_dbfs": -3.5,
        "layers": [
            MixLayer("Shadow_Crack", take=3, offset=0.0, gain_db=0.0),
            MixLayer("Sub_Drop", take=3, offset=0.0, gain_db=-6.0, length=1.0),
            MixLayer("Shadow_Sizzle", take=2, offset=0.06, gain_db=-7.0),
        ],
    },
    "CorruptingTouch": {
        "target_dbfs": -4.0,
        "layers": [
            MixLayer("Rot_Splash", take=1, offset=0.0, gain_db=0.0),
            MixLayer("Acid_Hiss", take=1, offset=0.05, gain_db=-4.0),
            MixLayer("Whispers", take=2, offset=0.0, gain_db=-10.0, length=1.2),
        ],
    },
    "Fear": {
        "target_dbfs": -3.5,
        "layers": [
            MixLayer("Ghost_Wail", take=1, offset=0.0, gain_db=0.0),
            MixLayer("Sub_Drop", take=3, offset=0.0, gain_db=-5.0),
            MixLayer("Whispers", take=3, offset=0.1, gain_db=-8.0),
        ],
    },
    # Played from the cast bar so the swell ends on the release (see the visualization kit).
    "FearCast": {
        "target_dbfs": -5.0,
        "layers": [
            MixLayer("Reverse_Swell", take=1, offset=0.0, gain_db=0.0),
            MixLayer("Void_Whoosh", take=4, offset=0.0, gain_db=-8.0, length=0.7),
        ],
    },
    "VeilOfShadows": {
        "target_dbfs": -4.0,
        "layers": [
            MixLayer("Shadow_Wrap", take=4, offset=0.0, gain_db=0.0),
            MixLayer("Whispers", take=1, offset=0.15, gain_db=-8.0),
            MixLayer("Dark_Chime", take=1, offset=0.35, gain_db=-11.0),
        ],
    },
    # Instant: the toll lands with the burst. A reversed swell in front of it would put the
    # hit 0.9 s after the visuals.
    "EmbraceOfDread": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("Dread_Bell", take=1, offset=0.0, gain_db=0.0),
            MixLayer("Sub_Drop", take=4, offset=0.0, gain_db=-3.0),
            MixLayer("Void_Whoosh", take=1, offset=0.0, gain_db=-7.0),
            MixLayer("Ghost_Wail", take=3, offset=0.12, gain_db=-9.0),
        ],
    },
    "UmbralBloom": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("Void_Burst", take=1, offset=0.0, gain_db=0.0),
            MixLayer("Sub_Drop", take=4, offset=0.05, gain_db=-4.0),
            MixLayer("Shadow_Sizzle", take=1, offset=0.25, gain_db=-8.0),
        ],
    },
    "GloamingTrance": {
        "target_dbfs": -5.0,
        "layers": [
            MixLayer("Dark_Chime", take=1, offset=0.0, gain_db=0.0),
            MixLayer("Whispers", take=2, offset=0.0, gain_db=-11.0, length=1.0),
        ],
    },
    "VoidResurgence": {
        "target_dbfs": -4.0,
        "layers": [
            MixLayer("Soul_Draw", take=1, offset=0.0, gain_db=0.0),
            MixLayer("Dark_Chime", take=3, offset=0.3, gain_db=-8.0),
        ],
    },
}

FINAL_ORDER = list(SINGLES) + list(MIXES)


def render(layer_dir: str, out_dir: str = OUTPUT_DIR, only=None) -> None:
    """Render every one-shot final (or ``only`` those named) from the raw layer takes. Loops
    in ``SINGLES`` are fetched directly with ``fetch.py --loop`` and are not rendered here."""
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
    parser = argparse.ArgumentParser(description="Render the acolyte spell sounds.")
    parser.add_argument("--render", metavar="LAYER_DIR", required=True)
    parser.add_argument("--out", default=OUTPUT_DIR)
    parser.add_argument("only", nargs="*")
    args = parser.parse_args()
    render(args.render, args.out, set(args.only))
