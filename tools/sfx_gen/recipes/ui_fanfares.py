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
}

# Quest accepted / completed are NOT layered mixes. After five rejected layered attempts (war
# drums + trumpet blare "annoying"; soft thump/bells "far too soft", then "too simple" and "too
# playful"; separately generated horns/strings/timpani "random notes, no common key"; a
# generated drum rhythm that models cannot keep; a melody resampled from single notes "silly and
# cheap") the winner was the ElevenLabs MUSIC model (eleven_music_v2_5, instrumental, 4 s,
# ~106 credits/take), which plays a coherent orchestra. 4 takes per prompt, user picked by ear.
# fetch.py -3 dBFS, then Sound/Interface/Quest{Accepted,Completed}.wav (volume 0.8, 2D).
MUSIC = {
    # take 1 chosen ("Accept_1")
    "QuestAccepted": (
        "Short epic orchestral fantasy stinger in the style of Warcraft: marching snare and "
        "war drums playing a rhythmic call to arms, a noble French horn fanfare answering, "
        "ending on a strong resolved chord. Heroic and cinematic but restrained, mid tempo, "
        "medieval adventure."),
    # prompt B, take 4 chosen ("Done_4")
    "QuestCompleted": (
        "Orchestral fantasy victory stinger: a gentle ascending strings and brass melody with "
        "soft snare, timpani and a satisfying cadence ending on a sustained major chord. "
        "Noble, uplifting and stylish, like a classic fantasy RPG quest reward, not bombastic."),
}
