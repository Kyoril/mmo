# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Sound recipes for the level-up and quest-accepted / quest-completed UI stingers and the Bind
spell sound (2026-10-11).

* Level up, quest accepted and quest completed are single takes of the ElevenLabs *music*
  model, see ``MUSIC`` below for the prompts and the history of what did not work.
* Bind is a layered spell sound: one prompt renders one dominant sound, so each element
  (``SOUNDS``) is generated on its own with the others excluded and ``layer.py`` assembles
  them (``MIXES``; layer files are named ``<SoundKey>_take<n>.wav``):

      py tools/sfx_gen/layer.py out.wav --recipe ui_fanfares --mix Bind --layer-dir <dir>
"""

from recipes.human import SoundSpec, MixLayer  # noqa: F401  (shared record types)

SOUNDS = {
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

# Level up and quest accepted / completed are NOT layered mixes. After five rejected layered attempts (war
# drums + trumpet blare "annoying"; soft thump/bells "far too soft", then "too simple" and "too
# playful"; separately generated horns/strings/timpani "random notes, no common key"; a
# generated drum rhythm that models cannot keep; a melody resampled from single notes "silly and
# cheap") the winner was the ElevenLabs MUSIC model (eleven_music_v2_5, instrumental, 4 s,
# ~106 credits/take), which plays a coherent orchestra. 4 takes per prompt, user picked by ear.
# fetch.py -3 dBFS, then Sound/Interface/{LevelUp,QuestAccepted,QuestCompleted}.wav (volume 1.0/0.8/0.8, 2D).
MUSIC = {
    # prompt B, take 4 chosen ("Level_B_4": short, timpani hit up front; the level-up VISUAL
    # already supplies the impact, so the sound stays compact). 5 s request, ~133 credits/take.
    "LevelUp": (
        "Majestic cinematic fantasy achievement stinger: a deep timpani hit, then a radiant "
        "ascending horn and trumpet melody with soaring strings and wordless choir, resolving "
        "to a glorious sustained major chord. Epic, uplifting, polished, classic RPG level up."),
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
