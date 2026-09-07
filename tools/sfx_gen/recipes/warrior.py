# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Prompt table for the warrior ability sound effects.

Generation runs through the ElevenLabs MCP connector (``eleven_text_to_sound_v2``), which a
script cannot invoke -- this file is the checked-in record of *what* was asked for, so the
set can be regenerated consistently. The prompts deliberately avoid music and reverb: these
play in a 3D world that supplies its own space, and a baked-in tail smears the transient.

Shouts are voiceless on purpose. A baked-in human voice clashes across race and gender
combinations; per-race vocalisations belong in the existing voice-line system instead.

``target_dbfs`` sets the mix balance between abilities. Impacts sit hotter than the
sustained cooldown effects so a hit reads as a hit.
"""

from dataclasses import dataclass


@dataclass(frozen=True)
class SoundSpec:
    prompt: str
    duration: float
    target_dbfs: float


_DRY = ("Dry close-mic recording, no reverb tail, no music, no voice. "
        "Single isolated video game combat ability sound.")

SOUNDS = {
    "Strike01": SoundSpec(
        f"A one-handed sword blade landing hard on chain mail and flesh. Sharp metallic "
        f"edge impact with a wet body thud underneath, very fast decay. {_DRY}",
        1.0, -3.0),
    "Strike02": SoundSpec(
        f"A one-handed sword blade striking armour at a glancing angle. Bright scraping "
        f"metal ring with a shorter body thud, very fast decay. {_DRY}",
        1.0, -3.0),
    "Strike03": SoundSpec(
        f"A heavy one-handed sword chop into a padded armoured torso. Duller, deeper impact "
        f"with less metallic ring, very fast decay. {_DRY}",
        1.0, -3.0),
    "Rend": SoundSpec(
        f"A blade tearing open flesh and leather in one long ripping pull. Wet fibrous tear "
        f"with a sharp cutting onset. {_DRY}",
        1.2, -3.5),
    "Execute": SoundSpec(
        f"A massive committed finishing blow with a heavy sword. Deep bone-crushing impact, "
        f"heavy low-end weight, brief metallic ring, decisive. {_DRY}",
        1.4, -2.0),
    "Skullbash": SoundSpec(
        f"A blunt helmeted headbutt cracking into a skull. Short dense bony crack with a "
        f"low thud, no ring, extremely fast decay. {_DRY}",
        0.8, -3.0),
    "ShieldSlam": SoundSpec(
        f"Heavy steel shield bashed into an enemy. Dry percussive metal-on-metal slam with "
        f"a deep low thud underneath, short bright metallic ring, fast decay. {_DRY}",
        1.2, -2.5),
    "Cleave": SoundSpec(
        f"A wide two-handed sword sweep carving through several bodies in one arc. Broad "
        f"whoosh building into layered wet impacts. {_DRY}",
        1.4, -3.0),
    "Charge": SoundSpec(
        f"An armoured warrior sprinting and colliding shoulder-first into an enemy. Rapid "
        f"heavy footfalls, clanking plate, ending in a heavy body collision. {_DRY}",
        2.0, -3.0),
    "Shockwave": SoundSpec(
        f"A warrior slamming the ground with tremendous force. Deep earth impact followed "
        f"by a low rolling rumble spreading outward, dirt and debris. {_DRY}",
        2.0, -2.5),
    "Battlecry": SoundSpec(
        f"A rallying war horn swell rising over a low martial drum hit. Heroic, brassy, "
        f"brief and uplifting. No singing, no words, no voice. {_DRY}",
        1.6, -4.0),
    "Bloodrush": SoundSpec(
        f"A surge of adrenaline: a low rising whoosh with a heavy heartbeat pulse and a "
        f"blood-rush swell. Visceral and internal. No voice. {_DRY}",
        1.6, -4.5),
    "Provoke": SoundSpec(
        f"A sharp aggressive pressure burst directed at an enemy. Tight low-frequency thump "
        f"with a hostile metallic snap. Short and confrontational. No voice. {_DRY}",
        1.0, -4.0),
    "DemoralizingShout": SoundSpec(
        f"A dark oppressive pressure wave rolling outward. Low menacing sub-bass swell with "
        f"a dissonant metallic shimmer, sinister and heavy. No voice. {_DRY}",
        1.8, -4.0),
    "LastStand": SoundSpec(
        f"A warrior bracing behind steel: heavy armour plates locking into place, a solid "
        f"grounded metallic brace, resolute and defensive. No voice. {_DRY}",
        1.6, -4.0),
}
