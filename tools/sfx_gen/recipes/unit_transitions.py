# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Prompt table and layer mixes for unit spawn and despawn effects (ModelDataEntry spawn_effect /
despawn_effect): the demon summons (Gloomlings, Brood Horror) materializing out of shadow and
burning away into it again.

Generated through the ElevenLabs MCP connector (``eleven_text_to_sound_v2``). Layered, not
single-prompt: one prompt renders one dominant sound however many layers it names (see
human.py), so every element is its own generation and ``tools/sfx_gen/layer.py`` mixes them.

Dissolve (despawn, 1.6 s visual): the body burns away along glowing violet edges. Embers
crackling + a dark descending whoosh + scattering motes.
Resolve (spawn, 1.2 s visual): the same dissolve backwards. A rising reversed swell that
gathers shadow, a crackle of coalescing energy, and a soft low thump when the body is whole.
"""

from dataclasses import dataclass
from typing import Optional

PROMPT_INFLUENCE = 0.75

# eleven_text_to_sound_v2 silently fails anything longer than this; see warrior.py.
MAX_PROMPT_CHARS = 450


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


# Layers are generated at -3 dBFS each; the mix is normalised again afterwards.
SOUNDS = {
    "DemonDissolve_Embers": SoundSpec(
        "Crackling sizzling embers: a body of dark magic burning away into ash, fine fizzing "
        "crackles and hissing sparks that thin out and die over about one and a half seconds. "
        "Only the crackle and sizzle: no whoosh, no voice, no music, no impact. Fantasy game "
        "sound.",
        duration=2.0, target_dbfs=-3.0),
    "DemonDissolve_Whoosh": SoundSpec(
        "A dark airy whoosh of shadow energy dispersing: a soft breathy descending swish that "
        "sinks in pitch and fades into silence, with a faint ghostly low hum. Only the whoosh: "
        "no crackle, no voice, no music, no impact. Fantasy game spell sound.",
        duration=2.0, target_dbfs=-3.0),
    "DemonDissolve_Motes": SoundSpec(
        "Tiny glittering magical motes scattering and fading: soft dark crystalline tinkles "
        "and a faint shimmering sparkle drifting away, quiet and airy. Only the sparkle: no "
        "whoosh, no crackle, no voice, no music. Fantasy game magic sound.",
        duration=2.0, target_dbfs=-3.0),
    "DemonResolve_Swell": SoundSpec(
        "A reversed dark magical swell: shadow energy being sucked inward and gathering, a "
        "breathy rising whoosh that grows louder and climbs in pitch, then cuts off abruptly "
        "at its peak. Only the rising swell: no impact, no voice, no music. Fantasy game spell "
        "sound.",
        duration=1.5, target_dbfs=-3.0),
    "DemonResolve_Crackle": SoundSpec(
        "Violet magic energy coalescing: quick electric sizzling crackles and fizzing sparks "
        "drawing together and intensifying, then snapping shut. Only the crackle: no whoosh, "
        "no voice, no music, no impact. Fantasy game magic sound.",
        duration=1.5, target_dbfs=-3.0),
    "DemonResolve_Thump": SoundSpec(
        "A soft deep magical thump of a creature materializing: one muffled low boom with a "
        "short dark sub-bass bloom and a brief airy tail. Only the thump: no crackle, no "
        "whoosh, no voice, no music. Fantasy game summon sound.",
        duration=1.2, target_dbfs=-3.0),
}

# Final sounds assembled from the layers above (mix "A" of each audition pair).
# Dissolve: the ember crackle leads, the dark whoosh sinks under it, motes scatter late.
# Resolve: the swell peaks about when the 1.2 s visual completes; the thump lands on it.
MIXES = {
    "DemonDissolve": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("DemonDissolve_Embers", take=4, offset=0.0, gain_db=-2.0),
            MixLayer("DemonDissolve_Whoosh", take=3, offset=0.0, gain_db=-4.0),
            MixLayer("DemonDissolve_Motes", take=2, offset=0.35, gain_db=-9.0),
        ],
    },
    "DemonResolve": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("DemonResolve_Swell", take=4, offset=0.0, gain_db=-2.0),
            MixLayer("DemonResolve_Crackle", take=1, offset=0.2, gain_db=-8.0, length=1.3),
            MixLayer("DemonResolve_Thump", take=1, offset=1.05, gain_db=-3.0),
        ],
    },
}
