# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Prompt table for the warrior ability sound effects.

Generation runs through the ElevenLabs MCP connector (``eleven_text_to_sound_v2``), which a
script cannot invoke -- this file is the checked-in record of *what* was asked for, so the
set can be regenerated consistently.

**Why these prompts look the way they do (learned the hard way, 2026-09-07).** The first
pass ended every prompt with "dry close-mic recording, no reverb tail, no music, no voice,
single isolated video game combat ability sound". That is a recipe for realistic *foley* --
one object hitting another -- and the whole set came back as thin single metallic clanks.
Game ability audio is not foley; it is a layered construction. Every prompt here names its
layers explicitly, in order, and asks for a tail. Do not reintroduce "dry", "isolated" or
"no reverb": they flatten the result.

Art direction is stylized high fantasy -- physical weapon impacts fused with arcane energy,
in the register of a AAA fantasy MMO. Shouts stay voiceless: a baked-in human voice clashes
across race and gender combinations, and per-race vocalisations belong in the existing
voice-line system instead.

``duration`` is generous on purpose. The model needs room to build a tail, and
``postprocess.trim_silence`` removes whatever it does not use, so over-asking costs nothing
but a little generation time.

``target_dbfs`` sets the mix balance between abilities. Impacts sit hotter than the
sustained cooldown effects so a hit reads as a hit.

``PROMPT_INFLUENCE`` is 0.75, up from the first pass's 0.6, so the model follows the layer
list instead of drifting toward a generic impact.
"""

from dataclasses import dataclass

PROMPT_INFLUENCE = 0.75


@dataclass(frozen=True)
class SoundSpec:
    prompt: str
    duration: float
    target_dbfs: float


_STYLE = ("Epic AAA fantasy MMO combat ability sound effect, richly layered and produced, "
          "powerful and satisfying, with weight and a designed tail.")

SOUNDS = {
    "Strike01": SoundSpec(
        f"A warrior's enchanted sword striking an armoured enemy. Layers: a sharp metallic "
        f"blade crack, a wet armoured body impact beneath it, a bright arcane energy snap "
        f"through the middle, and a deep sub-bass thump underneath. Short shimmering "
        f"magical tail. {_STYLE}",
        1.5, -3.0),
    "Strike02": SoundSpec(
        f"A warrior's enchanted sword raking across armour at a glancing angle. Layers: a "
        f"bright metallic scrape and ring, a shorter body impact, a crackling arcane spark "
        f"wash, and a sub-bass thump. Ringing magical tail. {_STYLE}",
        1.5, -3.0),
    "Strike03": SoundSpec(
        f"A heavy enchanted sword chop into an armoured torso. Layers: a dull dense impact, "
        f"a low armour crunch, a muted arcane energy pulse, and a heavy sub-bass drop. "
        f"Darker and weightier than a normal hit, with a short low tail. {_STYLE}",
        1.5, -3.0),
    "Rend": SoundSpec(
        f"A cursed blade tearing a deep bleeding wound. Layers: a wet fibrous flesh tear, a "
        f"sharp cutting metallic onset, a dark magical shimmer lingering over the wound, and "
        f"a low ominous sub swell. Sinister decaying tail. {_STYLE}",
        1.8, -3.5),
    "Execute": SoundSpec(
        f"A devastating finishing blow from a greatsword. Layers: a massive bone-crushing "
        f"impact, a heavy metallic ring, a surge of released arcane power, a huge sub-bass "
        f"drop, and scattering debris. Triumphant and final, with a long powerful tail. "
        f"{_STYLE}",
        2.2, -2.0),
    "Skullbash": SoundSpec(
        f"A brutal stunning helmet blow to the head. Layers: a dense bony crack, a metallic "
        f"helm clang, a disorienting ringing tone that swims and detunes, and a tight "
        f"sub-bass thump. Concussive, with a dizzying ring-out tail. {_STYLE}",
        1.3, -3.0),
    "ShieldSlam": SoundSpec(
        f"A massive steel shield slammed into an enemy with magical force. Layers: a "
        f"percussive metal-on-metal slam, a deep low thud, a burst of concussive force like "
        f"compressed air released, and a bright metallic ring. Punchy, with a short resonant "
        f"tail. {_STYLE}",
        1.8, -2.5),
    "Cleave": SoundSpec(
        f"A wide sweeping greatsword arc carving through several enemies at once. Layers: a "
        f"broad rising whoosh, an arcane energy blade wave crackling along the arc, three "
        f"layered wet impacts in quick succession, and a rolling sub-bass. Sweeping, with a "
        f"trailing energy tail. {_STYLE}",
        2.0, -3.0),
    "Charge": SoundSpec(
        f"An armoured warrior explosively charging across the battlefield and colliding with "
        f"an enemy. Layers: a burst of accelerating force, rapid heavy plated footfalls, a "
        f"rushing wind whoosh, then a huge armoured body collision with a deep sub-bass "
        f"impact. Momentum building to a slam. {_STYLE}",
        2.5, -3.0),
    "Shockwave": SoundSpec(
        f"A warrior slamming the ground and releasing a shockwave through the earth. Layers: "
        f"a colossal ground impact, cracking stone and earth, a low rolling rumble expanding "
        f"outward, a resonant magical boom, and showering dirt and debris. Enormous, with a "
        f"long decaying rumble tail. {_STYLE}",
        3.0, -2.5),
    "Battlecry": SoundSpec(
        f"A heroic rallying battle cry surging with power. Layers: a rising war horn swell, "
        f"a deep martial drum hit, a bright uplifting magical shimmer blooming outward, and "
        f"a warm sub-bass swell. Inspiring and triumphant, with a glowing tail. No singing, "
        f"no words, no human voice. {_STYLE}",
        2.5, -4.0),
    "Bloodrush": SoundSpec(
        f"A warrior's body flooding with berserk rage. Layers: a low rising whoosh, a heavy "
        f"pounding heartbeat, a visceral surging blood swell, and a dark red-hot energy "
        f"crackle igniting through it. Building and internal, with a throbbing tail. No "
        f"human voice. {_STYLE}",
        2.2, -4.5),
    "Provoke": SoundSpec(
        f"A warrior hurling a magical taunt that seizes an enemy's attention. Layers: a "
        f"tight aggressive low-frequency thump, a hostile metallic snap, a dark energy lash "
        f"striking outward, and a sharp sub-bass punch. Confrontational and immediate, with "
        f"a short menacing tail. No human voice. {_STYLE}",
        1.5, -4.0),
    "DemoralizingShout": SoundSpec(
        f"A wave of oppressive dread rolling outward from a warrior. Layers: a low menacing "
        f"sub-bass swell, a dark dissonant metallic shimmer, a draining descending tone, and "
        f"a shadowy pressure wave expanding outward. Sinister and heavy, with a long "
        f"unsettling tail. No human voice. {_STYLE}",
        2.8, -4.0),
    "LastStand": SoundSpec(
        f"A warrior bracing behind an enchanted shield as protective magic ignites. Layers: "
        f"heavy armour plates slamming together, a deep grounded metallic brace, a radiant "
        f"barrier igniting with a warm resonant hum, and a low reassuring sub swell. "
        f"Resolute and defensive, with a sustained glowing tail. No human voice. {_STYLE}",
        2.4, -4.0),
}

# eleven_text_to_sound_v2 rejects anything longer than this. Found the hard way: the first
# LastStand prompt was 465 characters and all four of its generations failed while the other
# fourteen sounds succeeded. tools/tests/test_sfx_recipes.py guards it.
MAX_PROMPT_CHARS = 450
