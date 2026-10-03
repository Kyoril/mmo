# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Prompt table and layer mix for the human racial sound effects.

Generated through the ElevenLabs MCP connector (``eleven_text_to_sound_v2``); this file is
the checked-in record of what was asked for and how the result was assembled.

**Layered, not single-prompt.** The first Call of the Watch attempt listed every layer in
one prompt ("Layers: a horn blast, a steel ring, a drum hit, a sub swell, a tail"). All four
takes came back as the same single dull drum hit -- no horn at all -- and were rejected as
too primitive. The model renders one dominant sound per prompt, however many layers the
prompt names. So each element is generated on its own (one prompt = one sound, with the
other elements explicitly excluded) and the layers are combined by ``tools/sfx_gen/layer.py``
with explicit timing and balance (``MIXES``). That gives the sound a recognisable signature:
the horn says "the Watch", the steel and drum give it weight, the shimmer says "ability".

Call of the Watch: martial and grounded, no voice (a baked-in shout would clash with every
race and gender), no choir (that is the cleric's register).
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
    "CallOfTheWatch_Horn": SoundSpec(
        "A solo medieval brass war horn sounding a short heroic rallying call: two bold rising "
        "notes, the second held and bright, played in a stone castle courtyard with a natural "
        "hall reverb tail. Only the horn: no drums, no percussion, no voice, no music bed. "
        "Fantasy game ability sound.",
        duration=3.0, target_dbfs=-3.0, prompt_influence=0.8),
    "CallOfTheWatch_Steel": SoundSpec(
        "A sword blade struck hard against a steel shield: a bright sharp metallic clang "
        "followed by a long shimmering resonant ring-out that slowly fades. Only the metal: "
        "no drums, no voice, no music. Fantasy game sound.",
        duration=2.0, target_dbfs=-3.0, prompt_influence=0.8),
    "CallOfTheWatch_Drum": SoundSpec(
        "A single heavy war drum hit with a deep sub-bass boom and a short rumbling decay. "
        "Only the drum: no horn, no metal, no voice, no music. Cinematic fantasy game impact.",
        duration=1.5, target_dbfs=-3.0),
    "CallOfTheWatch_Shimmer": SoundSpec(
        "A rising golden magical shimmer: sparkling bright chimes and an airy upward whoosh of "
        "glittering energy that swells and blooms, then fades softly. No voice, no choir, no "
        "drums, no music. Fantasy game spell buff sound.",
        duration=2.5, target_dbfs=-3.0),
}

# Final sounds assembled from the layers above (mix "B" of the audition: weight first,
# the horn entering a beat later over the steel ring).
MIXES = {
    "CallOfTheWatch": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("CallOfTheWatch_Drum", take=2, offset=0.0, gain_db=-3.0),
            MixLayer("CallOfTheWatch_Steel", take=2, offset=0.0, gain_db=-2.5, length=1.4),
            MixLayer("CallOfTheWatch_Horn", take=4, offset=0.12, gain_db=-2.0),
            MixLayer("CallOfTheWatch_Shimmer", take=2, offset=0.05, gain_db=-8.0),
        ],
    },
}
