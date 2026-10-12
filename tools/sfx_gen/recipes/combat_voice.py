# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
The checked-in line table for combat barks: the attacker's effort grunt and the victim's
pain react.

Kept separate from :mod:`voice_lines` on purpose. That table is UI feedback -- "Not enough
mana", "I'm too far away" -- spoken 2D to the player who triggered it. These are 3D combat
vocalisations heard from any unit in earshot, they are driven by a different system
(ModelDataEntry.combat_sounds rather than the race voice sets), and they are tuned by play
chance rather than fired on every event. Two tables, two lifecycles.

**Casting is inherited, not chosen.** ``voice_lines`` established that the male PLAYER
character is Liam and the female is Laura, and that a bank mixing two voices swaps timbre
mid-shuffle. These barks belong to the same characters, so they use the same two voice ids.
Brian stays on the NPC townsfolk bank and is deliberately NOT used here: an NPC human is a
different person from the player character.

**Why mostly non-verbal.** An effort grunt fires on a fraction of swings and a pain react on
a fraction of hits, in a fight that lasts dozens of swings. Words wear out fast at that
repetition rate -- "Take that!" is charming twice and grating the tenth time -- so the banks
are dominated by breath and exertion, with at most one worded line each in the critical-hit
slots where the event is rare enough to carry it.

**Phonetic spelling is load-bearing.** The model is being asked for a vocalisation, not a
word, so the text is spelled to be *pronounced*: "Hyah", "Hrrgh", "Aaargh". Punctuation
matters -- the exclamation mark is what stops the model reading a grunt as a mumble.

Paths are locale relative, the same strings that go into a SoundEntry's ``files`` list,
because the client mounts the active locale root and resolves ``Voice/...`` against it.
"""

# Matches voice_lines.TARGET_DBFS: these barks shuffle alongside that bank in the same
# fights, and a quieter line next to a louder one reads as a bug rather than as variation.
TARGET_DBFS = -4.0

VOICES = {
    "liam": "TX3LPaxmHKxFdv7VOQHJ",        # male PLAYER character  (see voice_lines)
    "laura": "FGY2WhTYpPnrIDTdsKH5",       # female PLAYER character (see voice_lines)
}

MODEL_ID = "eleven_multilingual_v2"

# Lines whose text starts with an ``[audio tag]`` were generated with V3_MODEL_ID instead.
# multilingual_v2 reads consonant clusters like "Hnngh!", "Tsah!" or "Hnk!" as words, and
# the female bank shipped an "Mhhh", a flat "Zah" and a spoken "Hanka". v3 takes a
# description of the vocalisation in the tag plus a plain interjection, and stays in the
# same voice. Use it for any re-take of a non-verbal bark.
V3_MODEL_ID = "eleven_v3"

MALE = "Voice/Player/Human/Male/Combat/"
FEMALE = "Voice/Player/Human/Female/Combat/"

# (locale relative path, voice key, spoken line)
CLIPS = [
    # ------------------------------------------------------- male: effort on a swing
    (MALE + "HumanMale_Effort_01.wav", "liam", "Hyah!"),
    (MALE + "HumanMale_Effort_02.wav", "liam", "Hah!"),
    (MALE + "HumanMale_Effort_03.wav", "liam", "Hrrgh!"),
    (MALE + "HumanMale_Effort_04.wav", "liam", "Nngh!"),

    # ------------------------------------------------- male: effort on a critical hit
    (MALE + "HumanMale_EffortCrit_01.wav", "liam", "Rrraaagh!"),
    (MALE + "HumanMale_EffortCrit_02.wav", "liam", "Hraaah!"),
    (MALE + "HumanMale_EffortCrit_03.wav", "liam", "Take that!"),

    # ---------------------------------------------------------- male: pain when struck
    (MALE + "HumanMale_Pain_01.wav", "liam", "Ugh!"),
    (MALE + "HumanMale_Pain_02.wav", "liam", "Aagh!"),
    (MALE + "HumanMale_Pain_03.wav", "liam", "Hrk!"),
    (MALE + "HumanMale_Pain_04.wav", "liam", "Nngh!"),

    # ------------------------------------------------ male: pain from a critical hit
    (MALE + "HumanMale_PainCrit_01.wav", "liam", "Aaargh!"),
    (MALE + "HumanMale_PainCrit_02.wav", "liam", "Gaaah!"),
    (MALE + "HumanMale_PainCrit_03.wav", "liam", "Hnnngh!"),

    # ----------------------------------------------------- female: effort on a swing
    (FEMALE + "HumanFemale_Effort_01.wav", "laura", "Hyah!"),
    (FEMALE + "HumanFemale_Effort_02.wav", "laura", "Hah!"),
    (FEMALE + "HumanFemale_Effort_03.wav", "laura", "[exerting effort, sharp grunt while swinging a sword] Hup!"),
    (FEMALE + "HumanFemale_Effort_04.wav", "laura", "[fierce, strained effort, short battle grunt as she strikes hard] Hah-ya!"),

    # ----------------------------------------------- female: effort on a critical hit
    (FEMALE + "HumanFemale_EffortCrit_01.wav", "laura", "Yaaah!"),
    (FEMALE + "HumanFemale_EffortCrit_02.wav", "laura", "Hraaah!"),
    (FEMALE + "HumanFemale_EffortCrit_03.wav", "laura", "Take that!"),

    # -------------------------------------------------------- female: pain when struck
    (FEMALE + "HumanFemale_Pain_01.wav", "laura", "Ugh!"),
    (FEMALE + "HumanFemale_Pain_02.wav", "laura", "Aah!"),
    (FEMALE + "HumanFemale_Pain_03.wav", "laura", "[sharp cry of pain, struck by a blade] Agh!"),
    (FEMALE + "HumanFemale_Pain_04.wav", "laura", "[winded and hurt, knocked back by a heavy blow] Oof!"),

    # ---------------------------------------------- female: pain from a critical hit
    (FEMALE + "HumanFemale_PainCrit_01.wav", "laura", "Aaargh!"),
    (FEMALE + "HumanFemale_PainCrit_02.wav", "laura", "Gaah!"),
    (FEMALE + "HumanFemale_PainCrit_03.wav", "laura", "Hnnngh!"),
]
