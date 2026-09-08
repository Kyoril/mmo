# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
The checked-in line table for human character and NPC voice lines.

Every clip is one short bark. Paths are **locale relative** -- the same string that goes
into a SoundEntry's ``files`` list -- because the client mounts the active locale root and
resolves ``Voice/...`` against it (never ``Locales/Locale_enUS/Voice/...``).

Casting notes:

* ``brian`` / ``laura`` were already used for the human male / female lines that shipped
  before this table existed; new lines for those banks must keep the same voice or the
  shuffle bag will swap timbre mid-session.
* ``clarissa`` and ``elira`` are deliberately *different* voices. Both NPCs own a display
  model of their own (model_data 14 and 16), so they can carry an individual voice without
  bleeding into the generic human-female crowd.

Generation is manual (the ElevenLabs MCP connector, ``eleven_multilingual_v2``, one take
per line); ``tools/sfx_gen/fetch.py`` downloads and postprocesses each take to the game's
44.1 kHz mono 16-bit WAV contract at :data:`TARGET_DBFS`.
"""

# Peak level every generated line is normalised to. The pre-existing female lines sit
# around -3.5 dBFS and the older male lines around -8.5; -4.0 matches the louder cluster,
# which is the one a shuffle bag should be judged against -- a quiet line next to a loud
# one reads as a bug, not as variation.
TARGET_DBFS = -4.0

VOICES = {
    "brian": "nPczCjzI2devNBz1zQrb",       # premade "Brian - Deep, Resonant and Comforting"
    "laura": "FGY2WhTYpPnrIDTdsKH5",       # premade "Laura - Enthusiast, Quirky Attitude"
    "clarissa": "Xb7hH8MSUJpSbSDYk0k2",    # premade "Alice - Clear, Engaging Educator"
    "elira": "ZEt85AU1ui8Rr8FxNslW",       # "Alice - Young British Woman"
}

MODEL_ID = "eleven_multilingual_v2"

PLAYER_MALE = "Voice/Player/Human/Male/"
PLAYER_FEMALE = "Voice/Player/Human/Female/"
NPC = "Voice/Npc/"

# (locale relative path, voice key, spoken line)
CLIPS = [
    # ---------------------------------------------------------------- player, male
    # Topping up banks that already exist but only had one or two takes.
    (PLAYER_MALE + "HumanMale_OutOfMana_03.wav", "brian", "I have no mana left."),
    (PLAYER_MALE + "HumanMale_OutOfRage_03.wav", "brian", "I need more rage!"),
    (PLAYER_MALE + "HumanMale_OutOfEnergy_03.wav", "brian", "I'm spent."),
    (PLAYER_MALE + "HumanMale_Cooldown_02.wav", "brian", "Not yet!"),
    (PLAYER_MALE + "HumanMale_Cooldown_03.wav", "brian", "Give it a moment."),
    (PLAYER_MALE + "HumanMale_OutOfRange_03.wav", "brian", "I need to get closer."),
    (PLAYER_MALE + "HumanMale_CantAttack_02.wav", "brian", "I won't fight that."),
    (PLAYER_MALE + "HumanMale_CantAttack_03.wav", "brian", "That is no enemy of mine."),
    # New error banks.
    (PLAYER_MALE + "HumanMale_TargetDead_01.wav", "brian", "It's already dead."),
    (PLAYER_MALE + "HumanMale_TargetDead_02.wav", "brian", "That one is finished."),
    (PLAYER_MALE + "HumanMale_TargetDead_03.wav", "brian", "Dead already."),
    (PLAYER_MALE + "HumanMale_WrongFacing_01.wav", "brian", "I have to face it!"),
    (PLAYER_MALE + "HumanMale_WrongFacing_02.wav", "brian", "It's behind me!"),
    (PLAYER_MALE + "HumanMale_WrongFacing_03.wav", "brian", "I can't see it!"),
    (PLAYER_MALE + "HumanMale_BadTarget_01.wav", "brian", "Not on that."),
    (PLAYER_MALE + "HumanMale_BadTarget_02.wav", "brian", "Wrong target."),
    (PLAYER_MALE + "HumanMale_BadTarget_03.wav", "brian", "That won't work here."),
    (PLAYER_MALE + "HumanMale_CasterDead_01.wav", "brian", "Not while I'm dead."),
    (PLAYER_MALE + "HumanMale_CasterDead_02.wav", "brian", "I can do nothing... like this."),

    # -------------------------------------------------------------- player, female
    # The female bank shipped with one file per error and no rage or energy lines at all.
    (PLAYER_FEMALE + "HumanFemale_OutOfMana_02.wav", "laura", "I have no mana left."),
    (PLAYER_FEMALE + "HumanFemale_OutOfMana_03.wav", "laura", "Not enough mana!"),
    (PLAYER_FEMALE + "HumanFemale_OutOfRage_01.wav", "laura", "Not enough rage!"),
    (PLAYER_FEMALE + "HumanFemale_OutOfRage_02.wav", "laura", "I need more rage!"),
    (PLAYER_FEMALE + "HumanFemale_OutOfRage_03.wav", "laura", "My rage is spent."),
    (PLAYER_FEMALE + "HumanFemale_OutOfEnergy_01.wav", "laura", "Not enough energy!"),
    (PLAYER_FEMALE + "HumanFemale_OutOfEnergy_02.wav", "laura", "I need a moment to recover."),
    (PLAYER_FEMALE + "HumanFemale_OutOfEnergy_03.wav", "laura", "I'm spent."),
    (PLAYER_FEMALE + "HumanFemale_Cooldown_02.wav", "laura", "Not yet!"),
    (PLAYER_FEMALE + "HumanFemale_Cooldown_03.wav", "laura", "Give it a moment."),
    (PLAYER_FEMALE + "HumanFemale_OutOfRange_02.wav", "laura", "I need to get closer."),
    (PLAYER_FEMALE + "HumanFemale_OutOfRange_03.wav", "laura", "That's too far away!"),
    (PLAYER_FEMALE + "HumanFemale_CantAttack_01.wav", "laura", "I can't attack that."),
    (PLAYER_FEMALE + "HumanFemale_CantAttack_02.wav", "laura", "I won't fight that."),
    (PLAYER_FEMALE + "HumanFemale_CantAttack_03.wav", "laura", "That is no enemy of mine."),
    (PLAYER_FEMALE + "HumanFemale_TargetDead_01.wav", "laura", "It's already dead."),
    (PLAYER_FEMALE + "HumanFemale_TargetDead_02.wav", "laura", "That one is finished."),
    (PLAYER_FEMALE + "HumanFemale_TargetDead_03.wav", "laura", "Dead already."),
    (PLAYER_FEMALE + "HumanFemale_WrongFacing_01.wav", "laura", "I have to face it!"),
    (PLAYER_FEMALE + "HumanFemale_WrongFacing_02.wav", "laura", "It's behind me!"),
    (PLAYER_FEMALE + "HumanFemale_WrongFacing_03.wav", "laura", "I can't see it!"),
    (PLAYER_FEMALE + "HumanFemale_BadTarget_01.wav", "laura", "Not on that."),
    (PLAYER_FEMALE + "HumanFemale_BadTarget_02.wav", "laura", "Wrong target."),
    (PLAYER_FEMALE + "HumanFemale_BadTarget_03.wav", "laura", "That won't work here."),
    (PLAYER_FEMALE + "HumanFemale_CasterDead_01.wav", "laura", "Not while I'm dead."),
    (PLAYER_FEMALE + "HumanFemale_CasterDead_02.wav", "laura", "I can do nothing... like this."),

    # ------------------------------------------------- npc, generic human male bank
    # Hello and annoyed already shipped; only the goodbye slot was empty.
    (NPC + "HumanMale01/HumanMale_Npc_Goodbye_01.wav", "brian", "Safe travels."),
    (NPC + "HumanMale01/HumanMale_Npc_Goodbye_02.wav", "brian", "Farewell, friend."),
    (NPC + "HumanMale01/HumanMale_Npc_Goodbye_03.wav", "brian", "Mind how you go."),
    (NPC + "HumanMale01/HumanMale_Npc_Goodbye_04.wav", "brian", "Watch yourself out there."),

    # ----------------------------------------------- npc, generic human female bank
    (NPC + "HumanFemale01/HumanFemale_Npc_Hello_01.wav", "laura", "Well met."),
    (NPC + "HumanFemale01/HumanFemale_Npc_Hello_02.wav", "laura", "Good day to you."),
    (NPC + "HumanFemale01/HumanFemale_Npc_Hello_03.wav", "laura", "Yes? What is it?"),
    (NPC + "HumanFemale01/HumanFemale_Npc_Hello_04.wav", "laura", "Hello there."),
    (NPC + "HumanFemale01/HumanFemale_Npc_Annoyed_01.wav", "laura", "Do you need something, or not?"),
    (NPC + "HumanFemale01/HumanFemale_Npc_Annoyed_02.wav", "laura", "I am busy, you know."),
    (NPC + "HumanFemale01/HumanFemale_Npc_Annoyed_03.wav", "laura", "That is quite enough."),
    (NPC + "HumanFemale01/HumanFemale_Npc_Annoyed_04.wav", "laura", "Move along, please."),
    (NPC + "HumanFemale01/HumanFemale_Npc_Goodbye_01.wav", "laura", "Safe travels."),
    (NPC + "HumanFemale01/HumanFemale_Npc_Goodbye_02.wav", "laura", "Take care of yourself."),
    (NPC + "HumanFemale01/HumanFemale_Npc_Goodbye_03.wav", "laura", "Farewell."),
    (NPC + "HumanFemale01/HumanFemale_Npc_Goodbye_04.wav", "laura", "Mind the road."),

    # ------------------------------------------- npc, Clarissa Whiteshield (cleric)
    (NPC + "ClarissaWhiteshield/Clarissa_Npc_Hello_01.wav", "clarissa", "Light guide you."),
    (NPC + "ClarissaWhiteshield/Clarissa_Npc_Hello_02.wav", "clarissa", "You seek the Light's teachings?"),
    (NPC + "ClarissaWhiteshield/Clarissa_Npc_Hello_03.wav", "clarissa", "Peace be with you."),
    (NPC + "ClarissaWhiteshield/Clarissa_Npc_Hello_04.wav", "clarissa", "Welcome, faithful."),
    (NPC + "ClarissaWhiteshield/Clarissa_Npc_Annoyed_01.wav", "clarissa", "Patience is also a virtue, you know."),
    (NPC + "ClarissaWhiteshield/Clarissa_Npc_Annoyed_02.wav", "clarissa", "The Light rewards the patient. Try it."),
    (NPC + "ClarissaWhiteshield/Clarissa_Npc_Annoyed_03.wav", "clarissa", "Enough. I have prayers to keep."),
    (NPC + "ClarissaWhiteshield/Clarissa_Npc_Goodbye_01.wav", "clarissa", "Go with the Light."),
    (NPC + "ClarissaWhiteshield/Clarissa_Npc_Goodbye_02.wav", "clarissa", "May the Light keep you."),
    (NPC + "ClarissaWhiteshield/Clarissa_Npc_Goodbye_03.wav", "clarissa", "Walk in the Light."),

    # ------------------------------------------------- npc, Elira Hawke (scout)
    (NPC + "EliraHawke/Elira_Npc_Hello_01.wav", "elira", "Keep your eyes open out there."),
    (NPC + "EliraHawke/Elira_Npc_Hello_02.wav", "elira", "You move quietly. Good."),
    (NPC + "EliraHawke/Elira_Npc_Hello_03.wav", "elira", "Something to report?"),
    (NPC + "EliraHawke/Elira_Npc_Hello_04.wav", "elira", "Ah. You made it."),
    (NPC + "EliraHawke/Elira_Npc_Annoyed_01.wav", "elira", "I'm working. Go and be useful."),
    (NPC + "EliraHawke/Elira_Npc_Annoyed_02.wav", "elira", "You'll give away our position at this rate."),
    (NPC + "EliraHawke/Elira_Npc_Annoyed_03.wav", "elira", "Enough chatter."),
    (NPC + "EliraHawke/Elira_Npc_Goodbye_01.wav", "elira", "Watch the treeline."),
    (NPC + "EliraHawke/Elira_Npc_Goodbye_02.wav", "elira", "Keep to the shadows."),
    (NPC + "EliraHawke/Elira_Npc_Goodbye_03.wav", "elira", "Good hunting."),
]
