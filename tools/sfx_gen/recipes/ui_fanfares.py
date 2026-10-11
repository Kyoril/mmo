# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Prompt table and layer mixes for the level-up, quest-accepted and quest-completed UI
fanfares and the Bind spell sound (2026-10-11).

Same lesson as ``human.py``: one prompt renders one dominant sound, so every element is
generated on its own with the other elements explicitly excluded, and ``layer.py`` assembles
them (``MIXES``). Layer files are named ``<SoundKey>_take<n>.wav``.

    py tools/sfx_gen/layer.py out.wav --recipe ui_fanfares --mix LevelUp --layer-dir <dir>

Register: level up is the big one (rare, epic: brass + choir + sub impact + sparkle); quest
accepted is drums + a short brass call ("the adventure begins"); quest completed is a short
bright victory flourish (heard constantly, so brief and light). Bind is a magic spell, not a
fanfare: swirl + bell + warm pad, no brass.
"""

from recipes.human import SoundSpec, MixLayer  # noqa: F401  (shared record types)

SOUNDS = {
    "LevelUp_Brass": SoundSpec(
        "An epic heroic brass fanfare: a full trumpet and French horn section playing a short "
        "triumphant rising phrase of four bold notes ending on a long held bright major chord, "
        "in a grand hall with natural reverb. Only brass: no drums, no choir, no chimes, no "
        "voice. Fantasy RPG level up sound.", duration=3.5, target_dbfs=-3.0, prompt_influence=0.8),
    "LevelUp_Impact": SoundSpec(
        "A massive cinematic impact: a deep sub-bass boom with a heavy low drum hit and a long "
        "rumbling decay, like a trailer hit. Only the impact: no brass, no choir, no melody, "
        "no voice, no music. Epic fantasy game power-up hit.",
        duration=2.5, target_dbfs=-3.0, prompt_influence=0.8),
    "LevelUp_Choir": SoundSpec(
        "A heavenly choir singing a sustained bright major chord \"aaah\" that swells up "
        "powerfully and then fades with cathedral reverb, wordless and glorious. Only the "
        "choir: no brass, no drums, no chimes, no music bed. Fantasy game divine ascension "
        "sound.", duration=3.5, target_dbfs=-3.0, prompt_influence=0.8),
    "LevelUp_Chimes": SoundSpec(
        "A rising golden glissando of sparkling magical chimes and harp, bright glittering "
        "bells climbing upward then a shimmering twinkling tail that fades softly. Only chimes "
        "and harp: no brass, no choir, no drums, no voice. Fantasy game magical reward sound.",
        duration=3.0, target_dbfs=-3.0, prompt_influence=0.8),
    "Bind_Whoosh": SoundSpec(
        "A mystical swirling whoosh of magical energy spiraling inward and condensing, rising "
        "in pitch with airy whispering shimmer, ending in a soft glowing pulse. Only the "
        "energy swirl: no bell, no drums, no voice, no music. Fantasy game spell sound.",
        duration=2.5, target_dbfs=-3.0, prompt_influence=0.8),
    "Bind_Bell": SoundSpec(
        "A single pure resonant crystal bell strike with a warm glassy tone and a long singing "
        "ring-out that slowly fades, like a soul being anchored in place. Only the bell: no "
        "whoosh, no drums, no voice, no music. Fantasy game magic confirmation sound.",
        duration=3.0, target_dbfs=-3.0, prompt_influence=0.8),
    "Bind_Hum": SoundSpec(
        "A warm low magical drone that swells slowly with a soft ethereal pad, glowing and "
        "comforting, building then dissolving into a gentle fade. Only the drone pad: no bell, "
        "no whoosh, no drums, no voice. Fantasy game spell sound.",
        duration=3.0, target_dbfs=-3.0, prompt_influence=0.8),
    # v2 (2026-10-11): the first quest set (war drums + trumpet fanfare) was rejected as too
    # annoying next to the WoW reference sounds (iQuestActivate/iQuestComplete): those are
    # quiet, sparse, soft-attack swells with long tails, not dense front-loaded fanfares.
    "QA_Thump": SoundSpec(
        "A single soft low frame drum thump with a warm round tone and a short gentle decay, "
        "muffled mallet, restrained and calm. Only the drum: no brass, no melody, no voice, no "
        "music, no metal. Subtle fantasy RPG interface sound.",
        duration=1.5, target_dbfs=-3.0, prompt_influence=0.8),
    "QA_Swell": SoundSpec(
        "A single mellow distant French horn note that swells in gently with a soft attack, "
        "rises a little in pitch and fades away softly with light reverb, warm and noble. Only "
        "the horn: no drums, no trumpet fanfare, no voice, no music bed. Subtle fantasy RPG "
        "quest sound.", duration=2.5, target_dbfs=-3.0, prompt_influence=0.8),
    "QC_Bells": SoundSpec(
        "Three soft gentle ascending bell notes, like a music box or celesta played quietly, "
        "warm and mellow, each note ringing softly into a long gentle fade. Only the bells: no "
        "brass, no drums, no voice, no harsh attack. Subtle fantasy RPG quest reward sound.",
        duration=3.0, target_dbfs=-3.0, prompt_influence=0.8),
    "QC_Pad": SoundSpec(
        "A warm soft orchestral string and choir pad that swells slowly and gently into a "
        "hopeful major chord and then fades out softly with a long smooth tail, peaceful and "
        "rewarding. Only the soft pad: no brass, no drums, no bells, no words. Subtle fantasy "
        "RPG quest complete sound.", duration=4.0, target_dbfs=-3.0, prompt_influence=0.8),
}

MIXES = {
    # Weight first, brass over it, choir blooming under the held chord, sparkle on top.
    "LevelUp": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("LevelUp_Impact", take=4, offset=0.0, gain_db=-3.0, length=2.2),
            MixLayer("LevelUp_Brass", take=1, offset=0.0, gain_db=-1.0),
            MixLayer("LevelUp_Choir", take=1, offset=0.35, gain_db=-5.0),
            MixLayer("LevelUp_Chimes", take=2, offset=0.6, gain_db=-9.0),
        ],
    },
    # Swirl condenses, bell lands as it resolves, warm pad carries the tail.
    "Bind": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("Bind_Whoosh", take=2, offset=0.0, gain_db=-4.0),
            MixLayer("Bind_Hum", take=2, offset=0.0, gain_db=-6.0),
            MixLayer("Bind_Bell", take=1, offset=0.55, gain_db=-1.5),
        ],
    },
    # Soft thump, then a mellow horn swelling in over it. Quiet by design (volume 0.6).
    "QuestAccepted": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("QA_Thump", take=4, offset=0.0, gain_db=-2.0),
            MixLayer("QA_Swell", take=4, offset=0.3, gain_db=-4.0),
        ],
    },
    # A slow warm pad swell with soft bells blooming on top and a long tail.
    "QuestCompleted": {
        "target_dbfs": -3.0,
        "layers": [
            MixLayer("QC_Pad", take=2, offset=0.0, gain_db=-3.0),
            MixLayer("QC_Bells", take=3, offset=0.8, gain_db=-5.0),
        ],
    },
}
