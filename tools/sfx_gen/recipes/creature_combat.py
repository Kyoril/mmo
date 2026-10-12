# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
The checked-in table for creature and NPC combat audio: what a boar, a rat, a wolf and a
human NPC sound like when they aggro, attack, get hit and die.

These feed ``ModelDataEntry.combat_sounds`` (attack / hit voice, plus the aggro and death
slots), not the ability or melee impact tables. Generation runs through the ElevenLabs MCP
connector, which a script cannot invoke -- this file records *what* was asked for so the set
can be regenerated consistently; :mod:`wire_creature_combat` turns the picked takes into
SoundEntries and model data.

**Animals are one creature per prompt, not a layered construction.** The ability and melee
tables name their layers because a single-prompt "impact" collapses into thin foley. A
vocalisation is the opposite case: it IS one source, and naming extra layers just invites the
model to add a drum or a whoosh under a squeal. So these prompts describe one animal and the
shape of its call, and explicitly exclude music, speech and other sounds.

**Each event gets a different job.**

* *Aggro* is the "you pulled something" cue. It has to be the loudest, most distinct sound
  the creature makes, because it is heard at the edge of the fight and has to cut through.
* *Attack* fires on a fraction of swings; short and percussive so it rides on the swing.
* *Pain* fires on a fraction of the player's hits; short and sharp so it reads as feedback
  for *your* hit landing.
* *Death* is the reward. It is allowed the longest tail of the set.

**Human NPCs do not speak.** The same reasoning as :mod:`combat_voice`: effort and pain
lines repeat dozens of times per fight, so they stay non-verbal and phonetically spelled.
Aggro is a surprised exclamation ("Ey!") and never a sentence: the first pass had "You'll
regret that!" and the user rejected it -- a sentence is dialogue, and every bandit, guard and
cultist sharing models 3 and 4 would say the same one.

**Casting.** Male NPC = Brian, already the voice of every model-4 townsman's gossip lines, so
the man you talked to in town sounds like himself when he fights. Female NPC = a new voice,
deliberately *not* Laura: Laura is the female player character, and a female player fighting
a female bandit must not hear her own grunts coming back at her.
"""

from dataclasses import dataclass

MAX_PROMPT_CHARS = 450
PROMPT_INFLUENCE = 0.75

# Matches combat_voice.TARGET_DBFS for the human banks: the NPC barks shuffle alongside the
# player's own barks in the same fight, and a level mismatch reads as a bug.
VOICE_DBFS = -4.0

EVENTS = ("Aggro", "Attack", "Pain", "Death")


@dataclass(frozen=True)
class CreatureSound:
    prompt: str
    duration: float
    target_dbfs: float


# Shared tail of every creature prompt: one animal, nothing else in the clip.
_ONLY = ("Fantasy MMO creature vocalisation, close and present, punchy and clear. "
         "Only the animal itself: no music, no human voice, no footsteps, no ambience.")

# creature -> event -> spec. Aggro sits hottest so a pull cuts through a fight already in
# progress; attack and pain are the frequent ones and sit a little lower.
CREATURES = {
    # Second pass. The first boar set read as a cartoon creature (rejected by the user), so
    # these describe a real hog's voice -- a farm pig recorded up close -- and the specific
    # sound it makes, rather than an "angry wild boar" the model has to invent.
    "Boar": {
        "Aggro": CreatureSound(
            f"A real wild hog alarmed: a loud explosive huff through the snout, then a few "
            f"deep rapid aggressive pig grunts. Natural animal recording of a large pig up "
            f"close, gritty and throaty. {_ONLY}",
            1.4, -3.0),
        "Attack": CreatureSound(
            f"A real large pig ramming forward: one short deep forceful grunt through the "
            f"snout, throaty and heavy. Natural animal recording up close. {_ONLY}",
            0.6, -5.0),
        "Pain": CreatureSound(
            f"A real pig startled by a hit: one short sharp high squeal that cuts off "
            f"quickly. Natural farm animal recording up close. {_ONLY}",
            0.6, -4.0),
        "Death": CreatureSound(
            f"A real large pig collapsing: a strained squeal that drops into low "
            f"faltering grunts and a final heavy exhale through the snout. Natural animal "
            f"recording up close. {_ONLY}",
            1.8, -3.0),
    },
    "Rat": {
        "Aggro": CreatureSound(
            f"A large vicious sewer rat alerted to an intruder: a sharp hostile hiss "
            f"followed by an angry chittering screech. {_ONLY}",
            1.3, -3.0),
        "Attack": CreatureSound(
            f"A large rat biting: a quick snarling squeak with a snap of small teeth. {_ONLY}",
            0.6, -5.0),
        "Pain": CreatureSound(
            f"A large rat hurt: one short high-pitched pained squeak. {_ONLY}",
            0.6, -4.0),
        "Death": CreatureSound(
            f"A large rat dying: a shrill squeal that weakens into a faint, fading dying "
            f"squeak. {_ONLY}",
            1.5, -3.0),
    },
    "Wolf": {
        "Aggro": CreatureSound(
            f"A hostile wolf spotting prey: a deep menacing snarl rising into a sharp "
            f"aggressive bark-growl. {_ONLY}",
            1.6, -3.0),
        "Attack": CreatureSound(
            f"A wolf lunging to bite: a short vicious snarl with snapping jaws. {_ONLY}",
            0.8, -5.0),
        "Pain": CreatureSound(
            f"A wolf hit and hurt: one short sharp yelp. {_ONLY}",
            0.6, -4.0),
        "Death": CreatureSound(
            f"A wolf dying: a pained yelp that trails into a weak whimpering whine and a "
            f"final breath. {_ONLY}",
            2.2, -3.0),
    },
}

# Skeletons and undead: monsters, not people, so they are generated as sound effects like the
# animals rather than voiced -- a voice actor asked for a zombie groan gives you an actor
# doing a zombie impression. "Wordless" is in every prompt for the same reason.
_UNDEAD = ("Dark fantasy MMO undead creature sound, close and present, punchy and clear. "
           "Only the creature itself: no music, no words, no footsteps, no ambience.")

CREATURES.update({
    "Skeleton": {
        "Aggro": CreatureSound(
            f"An animated skeleton warrior rising to fight: a sharp dry clatter of rattling "
            f"bones and a hollow, breathy ghostly hiss from an empty ribcage. {_UNDEAD}",
            1.4, -3.0),
        "Attack": CreatureSound(
            f"An animated skeleton swinging a weapon: a quick rattle of bones with a short "
            f"hollow raspy exhale. {_UNDEAD}",
            0.6, -5.0),
        "Pain": CreatureSound(
            f"An animated skeleton struck hard: a sharp crack of bone and a brief rattle of "
            f"loose bones. {_UNDEAD}",
            0.6, -4.0),
        "Death": CreatureSound(
            f"An animated skeleton collapsing: bones clattering apart and tumbling onto "
            f"stone, then a fading hollow ghostly sigh. {_UNDEAD}",
            2.0, -3.0),
    },
    "UndeadMale": {
        "Aggro": CreatureSound(
            f"A rotting undead man noticing the living: a deep guttural raspy zombie snarl "
            f"rising into a hungry growl. Inhuman and wordless. {_UNDEAD}",
            1.4, -3.0),
        "Attack": CreatureSound(
            f"A rotting undead man lunging to strike: one short guttural raspy grunt. "
            f"Inhuman and wordless. {_UNDEAD}",
            0.6, -5.0),
        "Pain": CreatureSound(
            f"A rotting undead man struck: one short choked raspy groan. Inhuman and "
            f"wordless. {_UNDEAD}",
            0.6, -4.0),
        "Death": CreatureSound(
            f"A rotting undead man finally destroyed: a long rattling death groan that fades "
            f"into a last dry wheeze. Inhuman and wordless. {_UNDEAD}",
            2.0, -3.0),
    },
    "UndeadFemale": {
        "Aggro": CreatureSound(
            f"A rotting undead woman noticing the living: a hoarse rasping hiss rising into a "
            f"shrill, ghastly shriek. Inhuman and wordless. {_UNDEAD}",
            1.4, -3.0),
        "Attack": CreatureSound(
            f"A rotting undead woman lunging to strike: one short hoarse raspy snarl. "
            f"Inhuman and wordless. {_UNDEAD}",
            0.6, -5.0),
        "Pain": CreatureSound(
            f"A rotting undead woman struck: one short strangled raspy wail. Inhuman and "
            f"wordless. {_UNDEAD}",
            0.6, -4.0),
        "Death": CreatureSound(
            f"A rotting undead woman finally destroyed: a long hoarse dying wail that fades "
            f"into a last dry rattle of breath. Inhuman and wordless. {_UNDEAD}",
            2.0, -3.0),
    },
})

TAKES_PER_SOUND = 4

# ---------------------------------------------------------------------------------- humans

VOICES = {
    # voice ids come from creative_list_voices, never from memory
    "brian": "nPczCjzI2devNBz1zQrb",   # premade "Brian - Deep, Resonant and Comforting"
    "sarah": "GuflK5NRKwVLKwEeBYTy",   # "Sarah - Warm and Grounded"
}

# Expressive lines (aggro, death) use eleven_v3 for its delivery tags; the grunts use the
# same model as the player barks in combat_voice so the two banks sit together.
MODEL_GRUNT = "eleven_multilingual_v2"
MODEL_EXPRESSIVE = "eleven_v3"

MALE = "Voice/Npc/Human/Male/Combat/"
FEMALE = "Voice/Npc/Human/Female/Combat/"

# (locale relative path, voice key, model, spoken line)
NPC_CLIPS = [
    # ------------------------------------------------------------------- male: aggro
    (MALE + "NpcHumanMale_Aggro_01.wav", "brian", MODEL_EXPRESSIVE, "[angry] [shouting] Ey!"),
    (MALE + "NpcHumanMale_Aggro_02.wav", "brian", MODEL_EXPRESSIVE, "[startled] Hm?!"),
    (MALE + "NpcHumanMale_Aggro_03.wav", "brian", MODEL_EXPRESSIVE, "[angry] Hah!"),
    # A second take of the "Ey!" line; the user kept two of its four takes.
    (MALE + "NpcHumanMale_Aggro_04.wav", "brian", MODEL_EXPRESSIVE, "[angry] [shouting] Ey!"),
    # ------------------------------------------------------------------ male: effort
    (MALE + "NpcHumanMale_Effort_01.wav", "brian", MODEL_GRUNT, "Hah!"),
    (MALE + "NpcHumanMale_Effort_02.wav", "brian", MODEL_GRUNT, "Hrrgh!"),
    (MALE + "NpcHumanMale_Effort_03.wav", "brian", MODEL_GRUNT, "Hyah!"),
    # -------------------------------------------------------------------- male: pain
    (MALE + "NpcHumanMale_Pain_01.wav", "brian", MODEL_GRUNT, "Ugh!"),
    (MALE + "NpcHumanMale_Pain_02.wav", "brian", MODEL_GRUNT, "Agh!"),
    (MALE + "NpcHumanMale_Pain_03.wav", "brian", MODEL_GRUNT, "Hnngh!"),
    # ------------------------------------------------------------------- male: death
    (MALE + "NpcHumanMale_Death_01.wav", "brian", MODEL_EXPRESSIVE, "[pained] Aaargh... [dying gasp]"),
    # Death 02/03 were recast after the user heard them: "no..." and a soft "Uhhh..." sounded
    # horribly wrong. Dying cries stay wordless. Male 03 keeps only the first half of its take
    # (cut at 50%, 150 ms fade): the rattle's second half dragged on.
    (MALE + "NpcHumanMale_Death_02.wav", "brian", MODEL_EXPRESSIVE, "[pained] Urrgh... [dying gasp]"),
    (MALE + "NpcHumanMale_Death_03.wav", "brian", MODEL_EXPRESSIVE, "[choking] Hhkk... ahh... [dying gasp]"),
    # ----------------------------------------------------------------- female: aggro
    (FEMALE + "NpcHumanFemale_Aggro_01.wav", "sarah", MODEL_EXPRESSIVE, "[angry] [shouting] Ey!"),
    (FEMALE + "NpcHumanFemale_Aggro_02.wav", "sarah", MODEL_EXPRESSIVE, "[startled] Hm?!"),
    (FEMALE + "NpcHumanFemale_Aggro_03.wav", "sarah", MODEL_EXPRESSIVE, "[angry] Hah!"),
    # A second take of the "Ey!" line; the user kept two of its four takes.
    (FEMALE + "NpcHumanFemale_Aggro_04.wav", "sarah", MODEL_EXPRESSIVE, "[angry] [shouting] Ey!"),
    # ---------------------------------------------------------------- female: effort
    (FEMALE + "NpcHumanFemale_Effort_01.wav", "sarah", MODEL_GRUNT, "Hah!"),
    (FEMALE + "NpcHumanFemale_Effort_02.wav", "sarah", MODEL_GRUNT, "Hnngh!"),
    (FEMALE + "NpcHumanFemale_Effort_03.wav", "sarah", MODEL_GRUNT, "Hyah!"),
    # ------------------------------------------------------------------ female: pain
    (FEMALE + "NpcHumanFemale_Pain_01.wav", "sarah", MODEL_GRUNT, "Ugh!"),
    (FEMALE + "NpcHumanFemale_Pain_02.wav", "sarah", MODEL_GRUNT, "Aah!"),
    (FEMALE + "NpcHumanFemale_Pain_03.wav", "sarah", MODEL_GRUNT, "Hnk!"),
    # ----------------------------------------------------------------- female: death
    (FEMALE + "NpcHumanFemale_Death_01.wav", "sarah", MODEL_EXPRESSIVE, "[pained] Aaah... [dying gasp]"),
    (FEMALE + "NpcHumanFemale_Death_02.wav", "sarah", MODEL_EXPRESSIVE, "[pained] Aaah... [dying gasp]"),
    (FEMALE + "NpcHumanFemale_Death_03.wav", "sarah", MODEL_EXPRESSIVE, "[choking] Hhkk... ahh... [dying gasp]"),
]

# Takes generated per line; the best one is kept.
TAKES_PER_LINE = 2


def creature_path(creature: str, event: str, index: int) -> str:
    """Asset path of one shuffle-bag variation, relative to data/client."""
    return f"Sound/Creature/{creature}/{creature}_{event}_{index:02d}.wav"
