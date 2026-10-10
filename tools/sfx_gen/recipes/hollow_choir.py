# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Prompt table and layer mixes for the Hollow Choir boss spells (Brother Oswin, Sister Mereth,
Cantor Veyr; see ``docs/hollow_choir_bosses.md``, "Audio-visual design").

Generated through the ElevenLabs MCP connector (``eleven_text_to_sound_v2``); this file is the
checked-in record of what was asked for and how each final sound was assembled.

**Layered, not single-prompt** (see ``human.py``): the model renders one dominant sound per
prompt, however many elements the prompt lists. Every signature sound is therefore built from
separately generated layers -- each prompt names one element and excludes the others -- mixed
by ``tools/sfx_gen/layer.py`` according to ``MIXES``. Ambient beds and simple casts are single
prompts, listed in ``SINGLES``.

Sonic identity per boss, so the room can be read with eyes closed:

* **Oswin** -- physical and warm: censer chain, stone, bone dust, candle fire, a small bell.
* **Mereth** -- spectral and female: wordless wails, ghostly whooshes, hush and muffling.
* **Veyr** -- tonal and wrong: discordant bells, clashing choir clusters, sonic pressure.

Choir and wail layers are always wordless: a sung word reads as dialogue.

Loops (``loop=True``) are generated with the model's loop option and fetched with
``fetch.py --loop`` so the seam is crossfaded rather than trimmed.

Raw takes are named ``<SoundKey>_take<n>.wav``; render a mix with::

    py tools/sfx_gen/layer.py out.wav --recipe hollow_choir --mix Oswin_GraveStrikeImpact \
        --layer-dir generated/hollow_choir/sfx/layers
"""

from dataclasses import dataclass
from typing import Optional

PROMPT_INFLUENCE = 0.75

# eleven_text_to_sound_v2 silently fails anything longer than this; see warrior.py.
MAX_PROMPT_CHARS = 450

OUTPUT_DIR = "data/client/Sound/Spells/HollowChoir"


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


_CRYPT = "Dark undead abbey crypt, eerie sacred fantasy game boss ability sound."

# Layers are generated at -3 dBFS each; mixes are normalised again afterwards.
SOUNDS = {
    # --- Brother Oswin ----------------------------------------------------------------------
    "Oswin_Windup_Chain": SoundSpec(
        "A heavy iron incense censer on a long rattling chain swung in a wide circle overhead: "
        "rhythmic whooshing swings speeding up, chain links jangling and creaking, building "
        "tension. Only the swinging censer and chain: no voice, no impact, no music. " + _CRYPT,
        duration=2.0, target_dbfs=-3.0),
    "Oswin_Windup_Groan": SoundSpec(
        "A deep guttural undead monk groan rising in pitch and intensity, a rasping dead "
        "throat straining as it winds up a heavy blow, wordless, no words. Only the groan: no "
        "chain, no impact, no music. " + _CRYPT,
        duration=2.0, target_dbfs=-3.0),
    "Oswin_Impact_Slam": SoundSpec(
        "A massive crushing slam of a heavy iron censer into a stone crypt floor: stone "
        "cracking and splitting, old bones shattering and crunching, a hard punchy transient. "
        "Only the slam: no voice, no music. " + _CRYPT,
        duration=1.5, target_dbfs=-3.0),
    "Oswin_Impact_Boom": SoundSpec(
        "A deep cinematic sub-bass boom with a heavy low thud and a short rumbling decay, felt "
        "more than heard. Only the low boom: no voice, no metal, no music. Fantasy game boss "
        "impact layer.",
        duration=1.5, target_dbfs=-3.0),
    "Oswin_Impact_Dust": SoundSpec(
        "Dust, grit and small stone debris raining down after a collapse in a crypt: sifting "
        "sand, pebbles skittering and bouncing on stone, bone fragments settling, a long soft "
        "fading tail. Only the debris: no impact, no voice, no music. " + _CRYPT,
        duration=2.5, target_dbfs=-3.0),
    "Oswin_Vigil_Lids": SoundSpec(
        "Heavy stone coffin lids slowly grinding and scraping open in a crypt, deep gritty "
        "stone-on-stone friction, several sarcophagi creaking open one after another, low "
        "rumbles. Only the stone: no voice, no music. " + _CRYPT,
        duration=2.5, target_dbfs=-3.0),
    "Oswin_Vigil_Chant": SoundSpec(
        "A low droning wordless chant of dead monks, deep male voices humming one dark sustained "
        "note in unison, no words, hollow and cavernous, swelling in then fading. Only the "
        "chant: no instruments, no stone, no music bed. " + _CRYPT,
        duration=2.5, target_dbfs=-3.0),
    "Oswin_Candle_Ignite": SoundSpec(
        "A sudden flame ignition: a soft deep whoomp as a ring of candles flares alight at "
        "once, a quick airy fire whoosh and a brief crackle. Only the fire: no bell, no voice, "
        "no music. " + _CRYPT,
        duration=1.5, target_dbfs=-3.0),
    "Oswin_Candle_Bell": SoundSpec(
        "A single small clear handbell ting, a bright high pure chime struck once with a "
        "clean ringing decay, like a chapel warning bell. Only the bell: no fire, no voice, no "
        "music. Fantasy game alert sound.",
        duration=2.0, target_dbfs=-3.0),
    "Oswin_Flare_Roar": SoundSpec(
        "A violent burst of fire erupting upward from the floor: a powerful roaring flame "
        "whoosh, a pillar of fire blasting up with a deep body. Only the fire burst: no bell, "
        "no voice, no music. " + _CRYPT,
        duration=1.8, target_dbfs=-3.0),
    "Oswin_Flare_Crackle": SoundSpec(
        "Crackling, popping and hissing fire embers and melting candle wax dying down after a "
        "flare, sputtering flames fading into a soft long tail. Only the crackle: no whoosh, "
        "no voice, no music. " + _CRYPT,
        duration=2.5, target_dbfs=-3.0),

    # --- Sister Mereth ----------------------------------------------------------------------
    # The first wail prompt ("starts soft and slowly builds") came back as four takes that peak
    # early and decay; naming the loudest point ("loudest at the very end") fixed it.
    "Mereth_Lament_Swell": SoundSpec(
        "A wordless ghostly female wail, no words, swelling up from a faint breathy moan: a "
        "mournful banshee keening that grows steadily louder, higher and more anguished for "
        "three seconds, loudest at the very end, with a spectral echoing reverb. Only the wail: "
        "no instruments, no music. " + _CRYPT,
        duration=3.5, target_dbfs=-4.0, prompt_influence=0.8),
    "Mereth_Release_Whoosh": SoundSpec(
        "A spectral shockwave bursting outward: a powerful cold ghostly whoosh, a rushing "
        "wave of air with a hollow sub punch at the start, sweeping past and away. Only the "
        "whoosh: no voice, no music. " + _CRYPT,
        duration=1.8, target_dbfs=-3.0),
    "Mereth_Release_Echo": SoundSpec(
        "A fading ghostly female cry echoing away down a long stone crypt corridor, wordless, "
        "no words, a brief high spectral voice smeared into long reverberant echoes that "
        "decay to nothing. Only the echo: no whoosh, no music. " + _CRYPT,
        duration=2.5, target_dbfs=-3.0),
    "Mereth_Voices_Choir": SoundSpec(
        "An eerie wordless ghostly choir of female voices, no words, swelling up from silence "
        "in a dissonant cluster, airy breathy and haunted, cresting then fading with a "
        "spectral reverb. Only the choir: no instruments, no music bed. " + _CRYPT,
        duration=2.5, target_dbfs=-4.0),
    "Mereth_Silent_Thump": SoundSpec(
        "A single muffled deep sub-bass thump, as if heard underwater or through thick stone, "
        "with a soft pressure drop and a brief dull low tail. Only the thump: no voice, no "
        "music. Fantasy game spell layer.",
        duration=1.5, target_dbfs=-3.0),
    "Mereth_Silent_Whisper": SoundSpec(
        "A soft breathy ghostly hush, a single long whispered shhh drifting past close to "
        "the ear, airy and spectral, no words. Only the whisper: no thump, no music. " + _CRYPT,
        duration=1.8, target_dbfs=-3.0),
    "Mereth_Silent_Drone": SoundSpec(
        "A low hollow muffled drone, like pressure in the ears inside a sealed tomb: a dark "
        "steady sub hum with a faint airy hiss, smothered and suffocating, even level. Seamless "
        "loop with no attack and no ending. No voice, no music. " + _CRYPT,
        duration=4.0, target_dbfs=-10.0, loop=True),

    # --- Cantor Veyr ------------------------------------------------------------------------
    "Veyr_Mark_Bells": SoundSpec(
        "A cluster of discordant ringing bells and tuning forks out of tune with each other, "
        "beating and clashing, rising steadily in pitch over two seconds into a shrill warning. "
        "Only the bells: no voice, no impact, no music. " + _CRYPT,
        duration=2.5, target_dbfs=-3.0),
    "Veyr_Mark_Riser": SoundSpec(
        "A tense tonal riser, a dissonant metallic whine sweeping upward in pitch over two "
        "seconds, building pressure and unease, ending abruptly. Only the riser: no voice, no "
        "bells, no music. Fantasy game warning sound.",
        duration=2.5, target_dbfs=-3.0),
    "Veyr_Wave_Blast": SoundSpec(
        "A sudden blast of a discordant wordless choir, no words, many voices shouting one "
        "clashing chord at once, harsh and overwhelming, then cut off into a short hollow "
        "reverb. Only the choir blast: no music bed. " + _CRYPT,
        duration=1.8, target_dbfs=-3.0),
    "Veyr_Wave_Boom": SoundSpec(
        "A sonic boom shockwave: a sharp concussive air crack and a deep sub-bass blast wave "
        "rushing outward with a rumbling decay. Only the boom: no voice, no music. Fantasy "
        "game boss ability layer.",
        duration=1.8, target_dbfs=-3.0),
    "Veyr_Dirge_Drone": SoundSpec(
        "A dark wordless male choir drone, no words, deep bass voices holding a slow "
        "dissonant dirge chord, hollow and funereal in a stone crypt, steady and even. "
        "Seamless loop with no attack and no ending. No instruments, no music bed. " + _CRYPT,
        duration=5.0, target_dbfs=-9.0, loop=True),
    # Same trap as the wail: "a choir crescendo ... ending on a powerful peak" came back as four
    # blasts that decay from the first beat.
    "Veyr_Rises_Swell": SoundSpec(
        "A dark wordless male choir crescendo, no words, swelling up from near silence: the "
        "voices start barely audible and grow steadily louder and higher for two seconds, "
        "loudest at the very end, then stop. Only the choir: no instruments, no music bed. "
        + _CRYPT,
        duration=2.5, target_dbfs=-3.0, prompt_influence=0.8),
    "Veyr_Rises_Riser": SoundSpec(
        "A rising dark magical whoosh, a reversed swelling surge of spectral energy building "
        "over two seconds into a sharp burst at the end. Only the riser: no voice, no choir, "
        "no music. " + _CRYPT,
        duration=2.5, target_dbfs=-3.0),
    "Veyr_Resonance_Hum": SoundSpec(
        "A short soft harmonic hum and glassy chime, a gentle resonant tone blooming and "
        "fading in about one second, slightly eerie. Only the hum: no voice, no impact, no "
        "music. Fantasy game buff sound.",
        duration=1.5, target_dbfs=-3.0),
}

# Finals that are a single generated take, postprocessed: final name -> (layer, take, dBFS).
SINGLES = {
    "Mereth_LamentCast": ("Mereth_Lament_Swell", 3, -4.0),
    "Mereth_VoicesCast": ("Mereth_Voices_Choir", 1, -4.0),
    "Mereth_SilentLoop": ("Mereth_Silent_Drone", 2, -10.0),
    "Veyr_DirgeLoop": ("Veyr_Dirge_Drone", 2, -9.0),
    "Veyr_ResonanceHum": ("Veyr_Resonance_Hum", 3, -8.0),
}

# Finals assembled from the layers above.
MIXES = {
    "Oswin_GraveStrikeWindup": {
        "target_dbfs": -4.0,
        "layers": [
            MixLayer("Oswin_Windup_Chain", take=1, offset=0.0, gain_db=0.0, length=1.5),
            MixLayer("Oswin_Windup_Groan", take=3, offset=0.15, gain_db=-3.0, length=1.4),
        ],
    },
    "Oswin_GraveStrikeImpact": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("Oswin_Impact_Slam", take=1, offset=0.0, gain_db=0.0),
            MixLayer("Oswin_Impact_Boom", take=1, offset=0.0, gain_db=-3.0),
            MixLayer("Oswin_Impact_Dust", take=3, offset=0.12, gain_db=-8.0),
        ],
    },
    "Oswin_LastVigilCast": {
        "target_dbfs": -4.0,
        "layers": [
            MixLayer("Oswin_Vigil_Lids", take=3, offset=0.0, gain_db=-2.0, length=2.1),
            MixLayer("Oswin_Vigil_Chant", take=2, offset=0.1, gain_db=-1.0, length=2.1),
        ],
    },
    "Oswin_CandleMark": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("Oswin_Candle_Ignite", take=2, offset=0.0, gain_db=-4.0),
            MixLayer("Oswin_Candle_Bell", take=1, offset=0.05, gain_db=0.0),
        ],
    },
    "Oswin_CandleFlare": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("Oswin_Flare_Roar", take=3, offset=0.0, gain_db=0.0),
            MixLayer("Oswin_Flare_Crackle", take=1, offset=0.25, gain_db=-6.0),
        ],
    },
    "Mereth_LamentRelease": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("Mereth_Release_Whoosh", take=2, offset=0.0, gain_db=0.0),
            MixLayer("Mereth_Release_Echo", take=3, offset=0.08, gain_db=-5.0),
        ],
    },
    "Mereth_SilentMark": {
        "target_dbfs": -4.0,
        "layers": [
            MixLayer("Mereth_Silent_Thump", take=1, offset=0.0, gain_db=0.0),
            MixLayer("Mereth_Silent_Whisper", take=2, offset=0.1, gain_db=-4.0),
        ],
    },
    "Veyr_DissonanceMark": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("Veyr_Mark_Bells", take=1, offset=0.0, gain_db=0.0, length=2.2),
            MixLayer("Veyr_Mark_Riser", take=2, offset=0.0, gain_db=-6.0, length=2.2),
        ],
    },
    "Veyr_DissonanceWave": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("Veyr_Wave_Boom", take=1, offset=0.0, gain_db=-2.0),
            MixLayer("Veyr_Wave_Blast", take=1, offset=0.02, gain_db=0.0),
        ],
    },
    # The swell prompt still peaks around 40% of the clip, so the crescendo is built by the mix:
    # the riser carries the build and the choir lands on its burst.
    "Veyr_ChoirRisesCast": {
        "target_dbfs": -4.0,
        "layers": [
            MixLayer("Veyr_Rises_Riser", take=3, offset=0.0, gain_db=-3.0),
            MixLayer("Veyr_Rises_Swell", take=2, offset=0.55, gain_db=0.0),
        ],
    },
}

# Order of the finals in the audition reel (boss by boss, as the doc's spell table).
FINAL_ORDER = [
    "Oswin_GraveStrikeWindup", "Oswin_GraveStrikeImpact", "Oswin_LastVigilCast",
    "Oswin_CandleMark", "Oswin_CandleFlare",
    "Mereth_LamentCast", "Mereth_LamentRelease", "Mereth_VoicesCast", "Mereth_SilentMark",
    "Mereth_SilentLoop",
    "Veyr_DissonanceMark", "Veyr_DissonanceWave", "Veyr_DirgeLoop", "Veyr_ChoirRisesCast",
    "Veyr_ResonanceHum",
]
