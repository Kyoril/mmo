# Warrior Spell Visuals Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the warrior ability visuals — ten layered particle effects authored through the real pipeline, fifteen generated sound files routed through the `SoundEntry` catalog, and kit animations played at native rate.

**Architecture:** Four mostly-independent workstreams. A new `tools/sfx_gen/` Python package makes generated audio usable (trim / fade / normalise / convert). A one-field proto addition (`SpellKit.sound_ids`) lets kits reference catalog entries instead of raw file paths, so `SoundEntryPlayer`'s existing shuffle-bag and pitch-variance machinery applies. Particle effects are rebuilt as checked-in recipes under `tools/particle_gen/recipes/`. Animation fixes are pure data — removing the guessed `duration_ms` values.

**Tech Stack:** Python 3.12 (numpy, Pillow, protobuf), ffmpeg 7.1, C++17, protobuf 2 syntax, Catch2, CMake.

## Global Constraints

- **Copyright header** on every new source file: `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` (C++) or `# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` (Python).
- **C++ style:** Allman braces, tabs, `m_camelCase` members, `PascalCase` methods, `#pragma once`, Doxygen on public members. No exceptions (`SIMPLE_NO_EXCEPTIONS`).
- **Never use `Particles/Additive.hmat`.** It is typed `Unlit`, so the engine renders it with `BlendMode::Opaque`. Only `Particles/Particle_{Beam,Glow,Ring,Star}.hmi` are safe.
- **The engine has no additive blending and no bloom.** Keep volumetric layers at peak alpha 0.2–0.4; density carries brightness.
- **One-shot effects must have `loop=False` on every emitter**, or `ParticleSystem::IsFinished` never returns true and the effect leaks a scene node.
- **Every proto data change is a dual write** — `data/editor/data/<name>.data` *and* `data/client/ClientDB/<name>.data`.
- **`SpellKit` field numbers must stay identical** between `src/shared/proto_data/spell_visualizations.proto` and `src/shared/client_data/spell_visualizations.proto`.
- **Do not push to origin.** Work stays on `feature/warrior-spell-visuals`.
- Sound entry ids **21–33** are free (19 entries exist, max id 20).

---

### Task 1: `postprocess.py` — make generated audio usable

Raw text-to-sound output is unusable as-is: a verification take came back at −24 dBFS peak with 1.06 s of trailing near-silence on a 1.2 s clip. This module is the deterministic fix and is the only part of the audio pipeline that can be unit tested.

**Files:**
- Create: `tools/sfx_gen/postprocess.py`
- Create: `tools/sfx_gen/__init__.py` (empty)
- Test: `tools/tests/test_sfx_postprocess.py`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `trim_silence(samples: np.ndarray, sample_rate: int, threshold_db: float = -45.0, pre_roll_ms: float = 10.0) -> np.ndarray`
  - `apply_fades(samples: np.ndarray, sample_rate: int, fade_in_ms: float = 2.0, fade_out_ms: float = 15.0) -> np.ndarray`
  - `normalize_peak(samples: np.ndarray, target_dbfs: float = -3.0) -> np.ndarray`
  - `process(samples: np.ndarray, sample_rate: int, target_dbfs: float = -3.0) -> np.ndarray`
  - `decode_to_mono(path: str, sample_rate: int = 44100) -> np.ndarray`
  - `write_wav(path: str, samples: np.ndarray, sample_rate: int = 44100) -> None`
  - `RATE = 44100`

- [ ] **Step 1: Write the failing tests**

Create `tools/tests/test_sfx_postprocess.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Tests for tools/sfx_gen/postprocess.py."""

import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sfx_gen"))

import postprocess


class TrimSilenceTests(unittest.TestCase):
    def test_removes_leading_and_trailing_silence(self):
        rate = 1000
        body = np.ones(200, dtype=np.float32) * 0.5
        clip = np.concatenate([np.zeros(500, dtype=np.float32), body,
                               np.zeros(800, dtype=np.float32)])

        out = postprocess.trim_silence(clip, rate, pre_roll_ms=0.0)

        self.assertEqual(len(out), 200)

    def test_keeps_pre_roll_before_the_transient(self):
        rate = 1000
        clip = np.concatenate([np.zeros(500, dtype=np.float32),
                               np.ones(100, dtype=np.float32) * 0.5])

        # 10 ms of pre-roll at 1000 Hz is 10 samples.
        out = postprocess.trim_silence(clip, rate, pre_roll_ms=10.0)

        self.assertEqual(len(out), 110)

    def test_all_silence_returns_empty(self):
        out = postprocess.trim_silence(np.zeros(1000, dtype=np.float32), 1000)
        self.assertEqual(len(out), 0)


class NormalizeTests(unittest.TestCase):
    def test_quiet_clip_is_raised_to_target(self):
        clip = np.ones(100, dtype=np.float32) * 0.01

        out = postprocess.normalize_peak(clip, target_dbfs=-3.0)

        expected = 10.0 ** (-3.0 / 20.0)
        self.assertAlmostEqual(float(np.max(np.abs(out))), expected, places=5)

    def test_loud_clip_is_lowered_to_target(self):
        clip = np.ones(100, dtype=np.float32) * 0.99

        out = postprocess.normalize_peak(clip, target_dbfs=-6.0)

        expected = 10.0 ** (-6.0 / 20.0)
        self.assertAlmostEqual(float(np.max(np.abs(out))), expected, places=5)

    def test_silence_is_left_alone(self):
        out = postprocess.normalize_peak(np.zeros(50, dtype=np.float32))
        self.assertEqual(float(np.max(np.abs(out))), 0.0)


class FadeTests(unittest.TestCase):
    def test_fades_start_and_end_at_zero(self):
        rate = 1000
        clip = np.ones(1000, dtype=np.float32)

        out = postprocess.apply_fades(clip, rate, fade_in_ms=2.0, fade_out_ms=15.0)

        self.assertAlmostEqual(float(out[0]), 0.0, places=6)
        self.assertAlmostEqual(float(out[-1]), 0.0, places=6)
        # The middle is untouched.
        self.assertAlmostEqual(float(out[500]), 1.0, places=6)


class ProcessTests(unittest.TestCase):
    def test_quiet_clip_with_long_tail_becomes_short_and_loud(self):
        rate = 44100
        body = (np.random.default_rng(7).normal(0, 1, 4410) * 0.02).astype(np.float32)
        clip = np.concatenate([np.zeros(2205, dtype=np.float32), body,
                               np.zeros(44100, dtype=np.float32)])

        out = postprocess.process(clip, rate, target_dbfs=-3.0)

        # The one-second dead tail is gone.
        self.assertLess(len(out), 5000)
        # And it is no longer inaudible.
        self.assertGreater(float(np.max(np.abs(out))), 0.6)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python -m unittest tools.tests.test_sfx_postprocess -v`

Expected: FAIL — `ModuleNotFoundError: No module named 'postprocess'`

- [ ] **Step 3: Write the implementation**

Create `tools/sfx_gen/__init__.py` as an empty file, then `tools/sfx_gen/postprocess.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Turn raw generated sound-effect output into game-ready WAV files.

Text-to-sound models return a fixed-length clip padded with near-silence and mixed well
below full scale -- a verification take came back at -24 dBFS peak with 1.06 s of dead tail
on a 1.2 s clip. Dropping that straight into the game gives inaudible effects that also
waste memory. Every generated file goes through :func:`process` first.

The output contract matches what the rest of data/client/Sound uses: 44.1 kHz, mono,
16-bit PCM.
"""

from __future__ import annotations

import shutil
import subprocess
import wave

import numpy as np

RATE = 44100


def _db_to_linear(db: float) -> float:
    return float(10.0 ** (db / 20.0))


def trim_silence(samples: np.ndarray, sample_rate: int, threshold_db: float = -45.0,
                 pre_roll_ms: float = 10.0) -> np.ndarray:
    """Drop leading and trailing near-silence.

    A small amount of pre-roll is kept ahead of the first loud sample so the attack
    transient -- the part that makes an impact read as an impact -- is never clipped.
    Returns an empty array when the whole clip is below the threshold.
    """
    if len(samples) == 0:
        return samples

    threshold = _db_to_linear(threshold_db)
    loud = np.flatnonzero(np.abs(samples) > threshold)
    if len(loud) == 0:
        return samples[:0]

    pre_roll = int(sample_rate * pre_roll_ms / 1000.0)
    start = max(0, int(loud[0]) - pre_roll)
    end = int(loud[-1]) + 1
    return samples[start:end]


def apply_fades(samples: np.ndarray, sample_rate: int, fade_in_ms: float = 2.0,
                fade_out_ms: float = 15.0) -> np.ndarray:
    """Ramp the first and last few milliseconds so trimming cannot introduce a click."""
    if len(samples) == 0:
        return samples

    out = samples.astype(np.float32, copy=True)

    fade_in = min(int(sample_rate * fade_in_ms / 1000.0), len(out))
    if fade_in > 1:
        out[:fade_in] *= np.linspace(0.0, 1.0, fade_in, dtype=np.float32)

    fade_out = min(int(sample_rate * fade_out_ms / 1000.0), len(out))
    if fade_out > 1:
        out[-fade_out:] *= np.linspace(1.0, 0.0, fade_out, dtype=np.float32)

    return out


def normalize_peak(samples: np.ndarray, target_dbfs: float = -3.0) -> np.ndarray:
    """Scale so the loudest sample sits at ``target_dbfs``. Silence is left alone."""
    if len(samples) == 0:
        return samples

    peak = float(np.max(np.abs(samples)))
    if peak <= 0.0:
        return samples.astype(np.float32, copy=True)

    return (samples * (_db_to_linear(target_dbfs) / peak)).astype(np.float32)


def process(samples: np.ndarray, sample_rate: int, target_dbfs: float = -3.0) -> np.ndarray:
    """Trim, fade and normalise in the order that keeps the transient intact.

    Normalising last matters: trimming removes the quiet padding that would otherwise drag
    the average down, so the level is set from the real body of the sound.
    """
    trimmed = trim_silence(samples, sample_rate)
    faded = apply_fades(trimmed, sample_rate)
    return normalize_peak(faded, target_dbfs)


def _ffmpeg() -> str:
    exe = shutil.which("ffmpeg")
    if not exe:
        raise RuntimeError("ffmpeg is not on PATH; it is required to decode generated audio")
    return exe


def decode_to_mono(path: str, sample_rate: int = RATE) -> np.ndarray:
    """Decode any ffmpeg-readable file to a mono float32 array at ``sample_rate``."""
    result = subprocess.run(
        [_ffmpeg(), "-v", "error", "-i", str(path), "-f", "f32le",
         "-ac", "1", "-ar", str(sample_rate), "-"],
        capture_output=True, check=True)
    return np.frombuffer(result.stdout, dtype="<f4").copy()


def write_wav(path: str, samples: np.ndarray, sample_rate: int = RATE) -> None:
    """Write a mono 16-bit PCM WAV."""
    clipped = np.clip(samples, -1.0, 1.0)
    with wave.open(str(path), "wb") as handle:
        handle.setparams((1, 2, sample_rate, 0, "NONE", "not compressed"))
        handle.writeframes((clipped * 32767.0).astype("<i2").tobytes())
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python -m unittest tools.tests.test_sfx_postprocess -v`

Expected: PASS, 9 tests.

- [ ] **Step 5: Confirm the gate will pick the tests up**

Run: `python -m unittest discover -s tools/tests -p "test_*.py"`

Expected: OK, with the new tests included in the count.

- [ ] **Step 6: Commit**

```bash
git add tools/sfx_gen/__init__.py tools/sfx_gen/postprocess.py tools/tests/test_sfx_postprocess.py
git commit -m "feat(sfx): add the generated-audio postprocessing pipeline"
```

---

### Task 2: The warrior prompt table and fetch script

**Files:**
- Create: `tools/sfx_gen/recipes/warrior.py`
- Create: `tools/sfx_gen/fetch.py`
- Create: `tools/sfx_gen/README.md`

**Interfaces:**
- Consumes: `postprocess.process`, `postprocess.decode_to_mono`, `postprocess.write_wav` (Task 1).
- Produces:
  - `warrior.SOUNDS: dict[str, SoundSpec]` where `SoundSpec` is a `dataclass(prompt: str, duration: float, target_dbfs: float)`
  - `fetch.fetch_and_process(url: str, out_path: str, target_dbfs: float) -> dict` returning `{"peak": float, "duration": float}`
  - `fetch.build_audition(wav_paths: list[str], out_path: str, gap_seconds: float = 0.5) -> None`

- [ ] **Step 1: Write the prompt table**

Create `tools/sfx_gen/recipes/warrior.py`:

```python
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
```

- [ ] **Step 2: Write the fetch script**

Create `tools/sfx_gen/fetch.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Download generated audio and run it through the postprocessor.

Usage::

    python tools/sfx_gen/fetch.py <out.wav> <target_dbfs> <signed-url>
    python tools/sfx_gen/fetch.py --audition <out.wav> <wav> [<wav> ...]

The signed URLs come from the MCP connector's run status; they expire, so fetch promptly.
"""

from __future__ import annotations

import os
import sys
import urllib.request

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import postprocess


def fetch_and_process(url: str, out_path: str, target_dbfs: float) -> dict:
    """Download one generated clip, postprocess it, and write it as a game-ready WAV."""
    tmp_path = out_path + ".download"
    with urllib.request.urlopen(url) as response:
        with open(tmp_path, "wb") as handle:
            handle.write(response.read())

    try:
        raw = postprocess.decode_to_mono(tmp_path)
        processed = postprocess.process(raw, postprocess.RATE, target_dbfs=target_dbfs)
        os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
        postprocess.write_wav(out_path, processed)
    finally:
        if os.path.exists(tmp_path):
            os.remove(tmp_path)

    return {
        "peak": float(np.max(np.abs(processed))) if len(processed) else 0.0,
        "duration": len(processed) / float(postprocess.RATE),
    }


def build_audition(wav_paths, out_path: str, gap_seconds: float = 0.5) -> None:
    """Concatenate finished WAVs with gaps, so the whole set can be judged in one listen."""
    gap = np.zeros(int(postprocess.RATE * gap_seconds), dtype=np.float32)
    clips = []
    for path in wav_paths:
        clips.append(postprocess.decode_to_mono(path))
        clips.append(gap)
    postprocess.write_wav(out_path, np.concatenate(clips) if clips else np.zeros(0, np.float32))


if __name__ == "__main__":
    if sys.argv[1] == "--audition":
        build_audition(sys.argv[3:], sys.argv[2])
        print("wrote %s" % sys.argv[2])
    else:
        stats = fetch_and_process(sys.argv[3], sys.argv[1], float(sys.argv[2]))
        print("wrote %s  peak=%.3f  duration=%.2fs"
              % (sys.argv[1], stats["peak"], stats["duration"]))
```

- [ ] **Step 3: Write the README**

Create `tools/sfx_gen/README.md`:

```markdown
# sfx_gen

Pipeline for generated sound effects in `data/client/Sound/`.

| File | Purpose |
|---|---|
| `postprocess.py` | trim silence, fade, peak-normalise, convert to 44.1 kHz mono 16-bit WAV |
| `fetch.py` | signed URL -> download -> postprocess; also builds audition reels |
| `recipes/warrior.py` | the checked-in prompt table for the warrior ability set |

Requires `numpy` and `ffmpeg` on PATH.

## Why postprocessing is mandatory

Text-to-sound models return a fixed-length clip padded with near-silence and mixed well
below full scale. A verification take came back at **-24 dBFS peak with 1.06 s of dead tail
on a 1.2 s clip**. Dropped straight into the game that is inaudible, and it wastes memory
on silence. Nothing goes into `data/client` without `process()`.

## The loop

1. Generate with the ElevenLabs MCP connector (`eleven_text_to_sound_v2`), 4 variations per
   sound, using the prompt and duration from the recipe table.
2. `python tools/sfx_gen/fetch.py <out.wav> <target_dbfs> "<signed-url>"` for each take.
3. Pick the best take per sound.
4. `python tools/sfx_gen/fetch.py --audition audition.wav <wav> ...` and have a human listen
   to the whole set before anything is committed.

Signed URLs expire (2 hours), so fetch promptly.

## Prompting notes

Ask for dry close-mic recordings with no reverb and no music: the game supplies its own 3D
space, and a baked-in tail smears the transient that makes an impact read as an impact.
State the decay explicitly -- "very fast decay" is the single most useful phrase for
combat impacts.
```

- [ ] **Step 4: Verify the scripts import cleanly**

Run: `python -c "import sys; sys.path.insert(0,'tools/sfx_gen'); import fetch; from recipes import warrior; print(len(warrior.SOUNDS), 'sounds')"`

Expected: `15 sounds`

- [ ] **Step 5: Commit**

```bash
git add tools/sfx_gen/fetch.py tools/sfx_gen/README.md tools/sfx_gen/recipes/warrior.py
git commit -m "feat(sfx): add the warrior prompt table and fetch tooling"
```

---

### Task 3: Generate, postprocess and audition the sound set

This task involves MCP tool calls that a script cannot make, plus a human listening gate. It produces no code.

**Files:**
- Create: `data/client/Sound/Spells/Warrior/{Strike01,Strike02,Strike03,Rend,Execute,Skullbash,ShieldSlam,Cleave,Charge,Shockwave,Battlecry,Bloodrush,Provoke,DemoralizingShout,LastStand}.wav` (15 files)
- Create: `generated/warrior_visuals/audition.wav` (not committed)

**Interfaces:**
- Consumes: `warrior.SOUNDS` (Task 2), `fetch.fetch_and_process`, `fetch.build_audition`.
- Produces: the 15 WAV files referenced by Task 7's sound entries.

- [ ] **Step 1: Generate 4 variations of each sound**

Create one `sfx` node per sound on a single flow with `model_id: eleven_text_to_sound_v2`, using the `prompt` and `duration` from `warrior.SOUNDS` and `prompt_influence: 0.6`. Run each with `generations_count: 4`.

Batch the runs — `creative_run_flow_nodes` accepts up to 20 node ids.

- [ ] **Step 2: Download and postprocess every take**

For each returned media URL:

```bash
python tools/sfx_gen/fetch.py "generated/warrior_visuals/takes/<name>_<n>.wav" <target_dbfs> "<signed-url>"
```

Expected per file: `wrote ...  peak=0.708  duration=...` — peak must be ≈0.708 for `-3.0` dBFS, ≈0.501 for `-6.0`. A peak far from target means the clip was silent and the take should be discarded.

- [ ] **Step 3: Select the best take per sound and place it**

Copy the chosen take to `data/client/Sound/Spells/Warrior/<name>.wav`.

Selection criteria, in order: correct character for the ability; a clean fast transient; duration in the expected band (impacts under 1.0 s after trimming, cooldowns under 2.0 s).

- [ ] **Step 4: Build the audition reel and verify the set is consistent**

```bash
python tools/sfx_gen/fetch.py --audition generated/warrior_visuals/audition.wav data/client/Sound/Spells/Warrior/Strike01.wav data/client/Sound/Spells/Warrior/Strike02.wav data/client/Sound/Spells/Warrior/Strike03.wav data/client/Sound/Spells/Warrior/Rend.wav data/client/Sound/Spells/Warrior/Execute.wav data/client/Sound/Spells/Warrior/Skullbash.wav data/client/Sound/Spells/Warrior/ShieldSlam.wav data/client/Sound/Spells/Warrior/Cleave.wav data/client/Sound/Spells/Warrior/Charge.wav data/client/Sound/Spells/Warrior/Shockwave.wav data/client/Sound/Spells/Warrior/Battlecry.wav data/client/Sound/Spells/Warrior/Bloodrush.wav data/client/Sound/Spells/Warrior/Provoke.wav data/client/Sound/Spells/Warrior/DemoralizingShout.wav data/client/Sound/Spells/Warrior/LastStand.wav
```

Then verify every file meets the output contract:

```bash
python -c "
import wave,glob,os
for f in sorted(glob.glob('data/client/Sound/Spells/Warrior/*.wav')):
    w=wave.open(f); p=w.getparams(); w.close()
    print(os.path.basename(f), p.nchannels, p.framerate, p.sampwidth*8, round(p.nframes/p.framerate,2))
"
```

Expected: every row reads `1 44100 16` and a duration under 2.1 s.

- [ ] **Step 5: Human audition gate**

Send `generated/warrior_visuals/audition.wav` to the user and **wait for approval** before committing. Do not proceed on your own judgement — generated audio quality varies per take and this is the only place a human ear enters the loop.

- [ ] **Step 6: Commit (in the data/client submodule)**

```bash
cd data/client
git add Sound/Spells/Warrior
git commit -m "feat(audio): regenerate the warrior ability sound set"
cd ../..
```

---

### Task 4: Add `SpellKit.sound_ids` to both protos

**Files:**
- Modify: `src/shared/proto_data/spell_visualizations.proto` (inside `message SpellKit`, after field 12)
- Modify: `src/shared/client_data/spell_visualizations.proto` (inside `message SpellKit`, after field 12)

**Interfaces:**
- Consumes: nothing.
- Produces: `SpellKit.sound_ids` — `repeated uint32`, field number **13** in both files. Read by Task 5 as `kit.sound_ids_size()` / `kit.sound_ids(i)`.

- [ ] **Step 1: Confirm field 13 is free in both files**

Run: `grep -n "= 1[0-9];" src/shared/proto_data/spell_visualizations.proto src/shared/client_data/spell_visualizations.proto`

Expected: the highest `SpellKit` field shown is `delay_ms = 12`.

- [ ] **Step 2: Add the field to both files**

In **both** `src/shared/proto_data/spell_visualizations.proto` and `src/shared/client_data/spell_visualizations.proto`, insert immediately before the closing brace of `message SpellKit`:

```proto

    // Sound catalog entries (sounds.data) to play for this kit. Preferred over the legacy
    // `sounds` file-path list: entries carry category, 3D distances, volume and pitch
    // variance, and multi-file entries are picked shuffle-bag style.
    // A kit sets `sounds` or `sound_ids`, never both -- when this is non-empty the legacy
    // list on the same kit is ignored.
    repeated uint32 sound_ids = 13;
```

Match the surrounding indentation: the proto_data file uses 4 spaces, the client_data file uses tabs.

- [ ] **Step 3: Verify the two schemas still agree**

Run:

```bash
python -c "
import re
def fields(p):
    t=open(p).read()
    b=re.search(r'message SpellKit\s*\{(.*?)\n\}', t, re.S).group(1)
    return sorted((int(n), f) for f,n in re.findall(r'\b(\w+)\s*=\s*(\d+)', b))
a=fields('src/shared/proto_data/spell_visualizations.proto')
b=fields('src/shared/client_data/spell_visualizations.proto')
print('proto_data :', a)
print('client_data:', b)
assert a==b, 'SpellKit field numbers diverged between the two schemas'
print('OK - schemas agree')
"
```

Expected: `OK - schemas agree`, with `(13, 'sound_ids')` present in both lists.

- [ ] **Step 4: Confirm no protocol version bump is needed**

Run: `python tools/protocol_version_check.py`

Expected: exit 0. Spell visualization *contents* never cross the wire — only visualization ids do, via `PlaySpellVisual` — so this schema change is ClientDB-only.

- [ ] **Step 5: Commit**

```bash
git add src/shared/proto_data/spell_visualizations.proto src/shared/client_data/spell_visualizations.proto
git commit -m "feat(spells): let visualization kits reference sound catalog entries"
```

---

### Task 5: Route kit sounds through `SoundEntryPlayer`

`SoundEntryPlayer::PlayEntry(id, position)` already implements shuffle-bag file selection, pitch randomisation from `pitch_min`/`pitch_max`, per-entry volume, 3D min/max distance and category routing. This task is wiring only.

**Files:**
- Modify: `src/shared/game_client/spell_visualization_service.h` (line 80 declaration, line 197 member area)
- Modify: `src/shared/game_client/spell_visualization_service.cpp` (line 29 `Initialize`, and the sound block at lines 262–326)
- Modify: `src/mmo_client/game_states/world_state.cpp:333`

**Interfaces:**
- Consumes: `SpellKit.sound_ids` (Task 4); `SoundEntryPlayer::PlayEntry(uint32, const Vector3&)`.
- Produces: `SpellVisualizationService::Initialize(const proto_client::Project&, IAudio*, SoundEntryPlayer*)` — the third parameter is new and callers must pass it.

- [ ] **Step 1: Update the header**

In `src/shared/game_client/spell_visualization_service.h`, add a forward declaration next to the other client-class forward declarations near the top of `namespace mmo`:

```cpp
	class SoundEntryPlayer;
```

Replace the `Initialize` declaration at line 80:

```cpp
        /// \brief Initializes the visualization service with a project reference, audio player
        ///        and sound entry player.
        /// \param soundEntryPlayer Resolves kit sound_ids against the sounds.data catalog. May
        ///        be nullptr, in which case kits using sound_ids stay silent.
        void Initialize(const proto_client::Project& project, IAudio* audioPlayer,
                        SoundEntryPlayer* soundEntryPlayer);
```

And add a member beside `m_audioPlayer` at line 197:

```cpp
        SoundEntryPlayer* m_soundEntryPlayer { nullptr }; // not owned
```

- [ ] **Step 2: Update `Initialize` and add the include**

In `src/shared/game_client/spell_visualization_service.cpp`, add to the include block:

```cpp
#include "game_client/sound_entry_player.h"
```

Replace lines 29–33:

```cpp
    void SpellVisualizationService::Initialize(const proto_client::Project& project,
                                               IAudio* audioPlayer,
                                               SoundEntryPlayer* soundEntryPlayer)
    {
        m_project = &project;
        m_audioPlayer = audioPlayer;
        m_soundEntryPlayer = soundEntryPlayer;
    }
```

- [ ] **Step 3: Route the catalog path ahead of the legacy path**

In `ApplyVisualization`, replace the condition that opens the sound block (currently `if (m_audioPlayer && kit.sounds_size() > 0)`) with a catalog branch placed **before** it. The legacy block stays exactly as it is, guarded so it only runs when no `sound_ids` are set:

```cpp
        // Catalog-backed sounds. Preferred over the legacy file-path list: SoundEntryPlayer
        // applies the entry's category, 3D distances, volume, pitch variance and shuffle-bag
        // file selection. A kit sets one or the other, never both.
        if (m_soundEntryPlayer && kit.sound_ids_size() > 0)
        {
            const Vector3 actorPosition = actor.GetPosition();
            const bool isLooped = kit.has_loop() && kit.loop();

            for (const uint32 soundId : kit.sound_ids())
            {
                if (isLooped)
                {
                    const uint64 guid = actor.GetGuid();

                    // Only one looped sound per actor at a time, matching the legacy path.
                    FadeOutLoopedSoundForActor(guid);

                    const ChannelIndex channel = m_soundEntryPlayer->PlayEntry(soundId, actorPosition);
                    if (channel == InvalidChannel)
                    {
                        continue;
                    }

                    // Start silent so Update can fade the loop in.
                    if (IChannelInstance* channelInstance = m_audioPlayer->GetChannelInstance(channel))
                    {
                        channelInstance->SetVolume(0.0f);
                    }

                    LoopedSoundHandle loopHandle;
                    loopHandle.audioHandle = channel;
                    loopHandle.spellId = vis.id();
                    loopHandle.event = Event::Casting;
                    loopHandle.currentVolume = 0.0f;
                    loopHandle.targetVolume = m_soundEntryPlayer->GetEntryVolume(soundId);
                    loopHandle.fadeSpeed = 3.0f;
                    m_loopedSounds[guid] = loopHandle;
                }
                else
                {
                    // PlayEntry already applied the entry's volume and pitch; leave the
                    // channel alone so the authored transient survives.
                    m_soundEntryPlayer->PlayEntry(soundId, actorPosition);
                }
            }
        }
        else if (m_audioPlayer && kit.sounds_size() > 0)
        {
            // ... existing legacy file-path block, unchanged ...
        }
```

Leave the body of the legacy block byte-for-byte as it was; only its `if` becomes an `else if`.

- [ ] **Step 4: Pass the sound entry player at the call site**

In `src/mmo_client/game_states/world_state.cpp` line 333, change:

```cpp
		SpellVisualizationService::Get().Initialize(m_project, &m_audio);
```

to:

```cpp
		SpellVisualizationService::Get().Initialize(m_project, &m_audio, &m_soundEntryPlayer);
```

`m_soundEntryPlayer` is already a member (declared at `world_state.h:678`).

- [ ] **Step 5: Check for any other callers**

Run: `grep -rn "SpellVisualizationService::Get().Initialize\|\.Initialize(m_project" --include=*.cpp src/`

Expected: exactly one call site, the one just edited. If the editor also initialises the service, update it the same way, passing `nullptr` if no `SoundEntryPlayer` exists there.

- [ ] **Step 6: Build the client**

Run: `cmake --build build --config Debug -t mmo_client`

Expected: builds clean. A `no matching function for call to Initialize` error means a call site was missed in Step 5.

- [ ] **Step 7: Commit**

```bash
git add src/shared/game_client/spell_visualization_service.h src/shared/game_client/spell_visualization_service.cpp src/mmo_client/game_states/world_state.cpp
git commit -m "feat(client): play visualization kit sounds through the sound catalog"
```

---

### Task 6: Editor support for kit sound ids

**Files:**
- Modify: `src/mmo_edit/editor_windows/spell_visualization_editor_window.cpp` (the kit editing section, near the existing `sounds` list and the animation-name tooltip at line 789)

**Interfaces:**
- Consumes: `SpellKit.sound_ids` (Task 4).
- Produces: no code interface; enables authoring kits without hand-editing binary data.

- [ ] **Step 1: Read the existing kit sound editing UI**

Run: `sed -n '844,884p' src/mmo_edit/editor_windows/spell_visualization_editor_window.cpp`

The existing `Sounds` tree node is the pattern to mirror: a `TreeNode`, an add button, a loop with `ImGui::PushID(i)`, and a deferred-removal vector applied in reverse so erasing does not invalidate the loop index.

- [ ] **Step 2: Add a sound-entry list editor after the `Sounds` tree node**

Insert immediately after the closing brace of the `if (ImGui::TreeNode("Sounds"))` block (after its `ImGui::TreePop();` and closing `}`, around line 884), before the `// --- Particles ---` comment.

`m_project.sounds` is a `TemplateManager<Sounds, SoundEntry>` (see `src/shared/proto_data/project.h:198`), so `getById` resolves the name for display — a wrong id is then visible while authoring rather than silently silent in game.

```cpp
			// --- Sound entries ---
			ImGui::Spacing();
			if (ImGui::TreeNode("Sound Entries"))
			{
				if (ImGui::Button("Add Sound Entry"))
				{
					kit.add_sound_ids(0);
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("?##soundIds"))
				{
					ImGui::SetTooltip("Catalog entries from sounds.data. Preferred over raw file\n"
						"paths: entries carry category, 3D distances, volume and pitch\n"
						"variance, and multi-file entries shuffle between takes.\n"
						"A kit uses these or the file paths above, never both.");
				}

				std::vector<int> soundIdsToRemove;

				for (int i = 0; i < kit.sound_ids_size(); ++i)
				{
					ImGui::PushID(i);

					int soundId = static_cast<int>(kit.sound_ids(i));
					char soundIdLabel[32];
					snprintf(soundIdLabel, sizeof(soundIdLabel), "Entry %d", i);

					if (ImGui::InputInt(soundIdLabel, &soundId))
					{
						kit.set_sound_ids(i, static_cast<uint32>(std::max(0, soundId)));
					}

					ImGui::SameLine();
					const auto* soundEntry = m_project.sounds.getById(kit.sound_ids(i));
					ImGui::TextUnformatted(soundEntry ? soundEntry->name().c_str() : "<unknown id>");

					ImGui::SameLine();
					if (ImGui::SmallButton("Remove"))
					{
						soundIdsToRemove.push_back(i);
					}

					ImGui::PopID();
				}

				for (auto it = soundIdsToRemove.rbegin(); it != soundIdsToRemove.rend(); ++it)
				{
					kit.mutable_sound_ids()->erase(kit.mutable_sound_ids()->begin() + *it);
				}

				ImGui::TreePop();
			}
```

- [ ] **Step 3: Build the editor**

Run: `cmake --build build --config Debug -t mmo_edit`

Expected: builds clean.

- [ ] **Step 4: Commit**

```bash
git add src/mmo_edit/editor_windows/spell_visualization_editor_window.cpp
git commit -m "feat(editor): author kit sound entries in the visualization editor"
```

---

### Task 7: Author the warrior sound catalog entries

**Files:**
- Create: `tools/warrior_visuals/author_sounds.py`
- Modify: `data/editor/data/sounds.data`
- Modify: `data/client/ClientDB/sounds.data`

**Interfaces:**
- Consumes: the 15 WAV files from Task 3.
- Produces: sound entry ids **21–33**, referenced by Task 11's kits.

The `Footstep - *` entries (ids 15–19) already use exactly this shape — 3D, six files, pitch 0.95–1.05 — so follow them.

- [ ] **Step 1: Write the authoring script**

Create `tools/warrior_visuals/author_sounds.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the warrior ability sound catalog entries (ids 21-33) into sounds.data.

Writes both the editor dataset and the client ClientDB copy. Idempotent: re-running
replaces the same ids rather than appending duplicates.

    python tools/warrior_visuals/author_sounds.py            # validate only
    python tools/warrior_visuals/author_sounds.py --apply    # write both datasets
"""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

from google.protobuf import descriptor_pb2, descriptor_pool, message_factory

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
from proto_runtime import find_protoc  # noqa: E402

SOUND_DIR = "Sound/Spells/Warrior/"

# id, name, files, pitch_min, pitch_max, volume
# Strike fires on every warrior swing and on eight creature types, so it carries three
# files and the widest pitch range -- it is the one sound that must never fatigue.
ENTRIES = [
    (21, "Warrior - Strike", ["Strike01.wav", "Strike02.wav", "Strike03.wav"], 0.92, 1.08, 0.85),
    (22, "Warrior - Rend", ["Rend.wav"], 0.96, 1.04, 0.90),
    (23, "Warrior - Execute", ["Execute.wav"], 0.97, 1.03, 1.00),
    (24, "Warrior - Skullbash", ["Skullbash.wav"], 0.95, 1.05, 0.90),
    (25, "Warrior - Shield Slam", ["ShieldSlam.wav"], 0.96, 1.04, 0.95),
    (26, "Warrior - Cleave", ["Cleave.wav"], 0.96, 1.04, 0.90),
    (27, "Warrior - Charge", ["Charge.wav"], 0.98, 1.02, 0.90),
    (28, "Warrior - Shockwave", ["Shockwave.wav"], 0.98, 1.02, 1.00),
    (29, "Warrior - Battlecry", ["Battlecry.wav"], 0.98, 1.02, 0.85),
    (30, "Warrior - Bloodrush", ["Bloodrush.wav"], 0.98, 1.02, 0.80),
    (31, "Warrior - Provoke", ["Provoke.wav"], 0.97, 1.03, 0.85),
    (32, "Warrior - Demoralizing Shout", ["DemoralizingShout.wav"], 0.98, 1.02, 0.85),
    (33, "Warrior - Last Stand", ["LastStand.wav"], 0.98, 1.02, 0.85),
]


def load_type(schema_dir, proto_name, message_name):
    with tempfile.TemporaryDirectory(prefix="warrior_sounds_") as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(find_protoc(ROOT)), f"-I{schema_dir}",
                        f"--descriptor_set_out={desc}", "--include_imports", proto_name],
                       cwd=schema_dir, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())
    pool = descriptor_pool.DescriptorPool()
    for f in file_set.file:
        pool.Add(f)
    return message_factory.GetMessageClass(pool.FindMessageTypeByName(message_name))


def populate(entry, spec):
    entry_id, name, files, pitch_min, pitch_max, volume = spec
    entry.Clear()
    entry.id = entry_id
    entry.name = name
    for f in files:
        entry.files.append(SOUND_DIR + f)
    entry.category = 0          # SOUND_EFFECTS
    entry.is_3d = True
    entry.looped = False
    entry.stream = False
    entry.volume = volume
    entry.pitch_min = pitch_min
    entry.pitch_max = pitch_max
    # Matches the distances the visualization service previously hardcoded at its
    # PlaySound3D call site, so audible range does not change.
    entry.min_distance = 5.0
    entry.max_distance = 30.0


def upsert(dataset):
    by_id = {e.id: e for e in dataset.entry}
    for spec in ENTRIES:
        target = by_id.get(spec[0])
        if target is None:
            target = dataset.entry.add()
        populate(target, spec)
    assert len({e.id for e in dataset.entry}) == len(dataset.entry), "duplicate sound ids"
    assert dataset.IsInitialized()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    for spec in ENTRIES:
        for f in spec[2]:
            path = ROOT / "data/client" / SOUND_DIR / f
            assert path.is_file(), f"missing sound file: {path}"

    targets = [
        (ROOT / "data/editor/data/sounds.data",
         load_type(ROOT / "src/shared/proto_data", "sounds.proto", "mmo.proto.Sounds")),
        (ROOT / "data/client/ClientDB/sounds.data",
         load_type(ROOT / "src/shared/client_data", "sounds.proto", "mmo.proto_client.Sounds")),
    ]

    for path, message_type in targets:
        dataset = message_type.FromString(path.read_bytes())
        upsert(dataset)
        if args.apply:
            path.write_bytes(dataset.SerializeToString())

    action = "wrote" if args.apply else "validated"
    print(f"{action} {len(ENTRIES)} warrior sound entries (ids 21-33) in both datasets")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Validate without writing**

Run: `python tools/warrior_visuals/author_sounds.py`

Expected: `validated 13 warrior sound entries (ids 21-33) in both datasets`. A `missing sound file` assertion means Task 3 is incomplete.

- [ ] **Step 3: Apply**

Run: `python tools/warrior_visuals/author_sounds.py --apply`

Expected: `wrote 13 warrior sound entries (ids 21-33) in both datasets`

- [ ] **Step 4: Verify both datasets agree**

```bash
python -c "
import importlib.util, sys
spec = importlib.util.spec_from_file_location('author_sounds', 'tools/warrior_visuals/author_sounds.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
for label, path, schema, name in [
        ('editor', m.ROOT/'data/editor/data/sounds.data', m.ROOT/'src/shared/proto_data', 'mmo.proto.Sounds'),
        ('client', m.ROOT/'data/client/ClientDB/sounds.data', m.ROOT/'src/shared/client_data', 'mmo.proto_client.Sounds')]:
    dataset = m.load_type(schema, 'sounds.proto', name).FromString(path.read_bytes())
    rows = [e for e in dataset.entry if 21 <= e.id <= 33]
    print(label, len(rows), 'entries, files:', sum(len(e.files) for e in rows))
"
```

Expected: both lines read `13 entries, files: 15`.

Importing the module rather than re-deriving the loader keeps this check honest — it reads the same schema the authoring script wrote through.

- [ ] **Step 5: Commit**

```bash
git add tools/warrior_visuals/author_sounds.py
git commit -m "feat(audio): author the warrior sound catalog entries"
cd data/editor && git add data/sounds.data && git commit -m "Add warrior ability sound entries" && cd ../..
cd data/client && git add ClientDB/sounds.data && git commit -m "Add warrior ability sound entries" && cd ../..
```

---

### Task 8: Shared particle building blocks

Ten effects share a palette and three recurring shapes. Factoring them out keeps each effect recipe short enough to read whole, and makes a palette change one edit rather than ten.

**Files:**
- Create: `tools/particle_gen/recipes/warrior_common.py`

**Interfaces:**
- Consumes: `hpar.Emitter`, `hpar.Burst`, `hpar.ParticleSystem`, `hpar.float_curve`, `hpar.color_curve`, `hpar.save`.
- Produces:
  - Palette tuples: `STEEL`, `SPARK`, `BLOOD`, `BLOOD_DARK`, `DUST`, `DUST_DARK`, `RAGE`, `RAGE_DEEP`, `DREAD`, `GUARD`
  - Material constants: `BEAM`, `GLOW`, `RING`, `STAR`
  - `rgba(rgb, a) -> tuple`
  - `spark_burst(name, count, speed, colour, size=0.10, lifetime=0.45, gravity=-9.0, drag=1.6, spread=1.0) -> Emitter`
  - `ground_ring(name, start_size, end_size, colour, alpha=0.35, lifetime=0.5, delay=0.0) -> Emitter`
  - `dust_cloud(name, count, spread, colour, alpha=0.30, size=0.5, lifetime=0.8, rise=0.4) -> Emitter`
  - `soft_flash(name, size, colour, alpha=0.5, lifetime=0.12) -> Emitter`
  - `write(system, filename) -> None` writing to `data/client/Particles/Warrior/<filename>`

- [ ] **Step 1: Write the module**

Create `tools/particle_gen/recipes/warrior_common.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Shared palette and emitter building blocks for the warrior ability effects.

Art direction is grounded physicality -- dust, grit, sparks, blood and steel -- with a
restrained warm accent reserved for the rage and shout cooldowns so the big buttons read as
empowered at a glance.

Two engine constraints shape every number in here:

* There is **no additive blending and no bloom**. Overlapping particles do not accumulate
  toward white, so volumetric layers stay at peak alpha 0.2-0.4 and density carries the
  brightness. A stack of high-alpha particles composites to a solid wall, not to a glow.
* ``Particles/Additive.hmat`` is typed ``Unlit``, which makes the engine render it with
  opaque blending -- hard occluding rectangles. Only the ``.hmi`` instances below are safe.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import hpar
from hpar import Burst, Emitter, ParticleSystem, color_curve, float_curve

# Translucent, depth-write off, soft DXT5 sprites.
BEAM = "Particles/Particle_Beam.hmi"     # streak, for RENDER_STRETCHED
GLOW = "Particles/Particle_Glow.hmi"     # soft radial blob
RING = "Particles/Particle_Ring.hmi"     # hollow ring, for ground shockwaves
STAR = "Particles/Particle_Star.hmi"     # four-point star

# Palette. Impacts are steel and spark; wounds are blood; movement is dust; the cooldowns
# carry the warm accent, and the debuff shouts deliberately sit cold and desaturated
# against them.
STEEL = (0.92, 0.94, 1.00)
SPARK = (1.00, 0.78, 0.38)
BLOOD = (0.55, 0.05, 0.03)
BLOOD_DARK = (0.28, 0.02, 0.02)
DUST = (0.52, 0.45, 0.34)
DUST_DARK = (0.34, 0.29, 0.22)
RAGE = (1.00, 0.42, 0.18)
RAGE_DEEP = (0.72, 0.12, 0.06)
DREAD = (0.30, 0.26, 0.32)
GUARD = (0.62, 0.72, 0.88)

OUT_DIR = os.path.join("data", "client", "Particles", "Warrior")


def rgba(rgb, a):
    return (rgb[0], rgb[1], rgb[2], a)


def spark_burst(name, count, speed, colour, size=0.10, lifetime=0.45,
                gravity=-9.0, drag=1.6, spread=1.0):
    """A single burst of stretched sparks thrown radially and killed by gravity and drag.

    ``spread`` biases the velocity box: 1.0 is omnidirectional, smaller values tighten the
    cone so the spray reads as directional.
    """
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_WORLD,     # sparks stay where they were thrown
        loop=False, duration=0.10,
        spawn_rate=0.0, max_particles=count + 8,
        bursts=[Burst(0.0, count)],
        shape=hpar.SHAPE_SPHERE, shape_extents=(0.14, 0.0, 0.0),
        min_lifetime=lifetime * 0.55, max_lifetime=lifetime,
        min_velocity=(-spread, 0.1, -spread), max_velocity=(spread, spread * 1.6, spread),
        min_start_speed=speed * 0.5, max_start_speed=speed,
        min_start_size=size * 0.6, max_start_size=size,
        gravity=(0.0, gravity, 0.0), drag=drag,
        render_mode=hpar.RENDER_STRETCHED, length_scale=5.0,
        material_name=BEAM,
        size_over_life=float_curve((0.0, 1.0), (1.0, 0.2)),
        color_over_lifetime=color_curve(
            (0.00, rgba(colour, 1.0)),
            (0.45, rgba(colour, 0.85)),
            (1.00, rgba(colour, 0.0))),
    )


def ground_ring(name, start_size, end_size, colour, alpha=0.35, lifetime=0.5, delay=0.0):
    """A flat expanding ring on the ground. This is what anchors an effect to the world.

    Lifted a hair off the floor so it does not z-fight the terrain, and given a random start
    rotation so repeated pulses do not visibly stamp the same image.
    """
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_LOCAL,
        loop=False, duration=0.10, start_delay=delay,
        spawn_rate=0.0, max_particles=4,
        bursts=[Burst(0.0, 1)],
        shape=hpar.SHAPE_POINT,
        min_lifetime=lifetime * 0.85, max_lifetime=lifetime,
        min_velocity=(0.0, 0.05, 0.0), max_velocity=(0.0, 0.10, 0.0),
        min_start_size=start_size, max_start_size=start_size,
        min_start_rotation=0.0, max_start_rotation=3.14,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_HORIZONTAL,
        material_name=RING,
        size_over_life=float_curve((0.0, 1.0), (1.0, end_size / max(start_size, 1e-6))),
        color_over_lifetime=color_curve(
            (0.00, rgba(colour, 0.0)),
            (0.15, rgba(colour, alpha)),
            (1.00, rgba(colour, 0.0))),
    )


def dust_cloud(name, count, spread, colour, alpha=0.30, size=0.5, lifetime=0.8, rise=0.4):
    """Slow, soft, low-alpha billboards -- the body of any dust or pressure effect.

    Alpha stays low by design: with no additive blending, stacking opaque-ish dust reads as
    a solid wall instead of a cloud.
    """
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_WORLD,
        loop=False, duration=0.18,
        spawn_rate=0.0, max_particles=count + 8,
        bursts=[Burst(0.0, count)],
        shape=hpar.SHAPE_SPHERE, shape_extents=(spread * 0.35, 0.0, 0.0),
        min_lifetime=lifetime * 0.6, max_lifetime=lifetime,
        min_velocity=(-spread, 0.0, -spread), max_velocity=(spread, rise, spread),
        min_start_size=size * 0.6, max_start_size=size,
        min_start_rotation=0.0, max_start_rotation=3.14,
        min_angular_velocity=-0.8, max_angular_velocity=0.8,
        gravity=(0.0, -0.4, 0.0), drag=1.2,
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.5), (0.4, 1.0), (1.0, 1.6)),
        color_over_lifetime=color_curve(
            (0.00, rgba(colour, 0.0)),
            (0.20, rgba(colour, alpha)),
            (1.00, rgba(colour, 0.0))),
    )


def soft_flash(name, size, colour, alpha=0.5, lifetime=0.12):
    """One brief soft blob at the origin. Gives an impact an onset instead of a fade-in."""
    return Emitter(
        name=name,
        simulation_space=hpar.SIM_WORLD,
        loop=False, duration=0.05,
        spawn_rate=0.0, max_particles=2,
        bursts=[Burst(0.0, 1)],
        shape=hpar.SHAPE_POINT,
        min_lifetime=lifetime, max_lifetime=lifetime,
        min_velocity=(0.0, 0.0, 0.0), max_velocity=(0.0, 0.0, 0.0),
        min_start_size=size, max_start_size=size,
        gravity=(0.0, 0.0, 0.0),
        render_mode=hpar.RENDER_BILLBOARD,
        material_name=GLOW,
        size_over_life=float_curve((0.0, 0.6), (0.3, 1.0), (1.0, 1.3)),
        color_over_lifetime=color_curve(
            (0.00, rgba(colour, alpha)),
            (1.00, rgba(colour, 0.0))),
    )


def write(system, filename):
    path = os.path.join(OUT_DIR, filename)
    os.makedirs(OUT_DIR, exist_ok=True)
    hpar.save(system, path)
    print("wrote %s (%d emitters, %d bytes)"
          % (path, len(system.emitters), os.path.getsize(path)))
```

- [ ] **Step 2: Verify the module imports and builds a valid emitter**

```bash
python -c "
import sys; sys.path.insert(0,'tools/particle_gen/recipes')
import warrior_common as w
s = w.ParticleSystem(emitters=[w.spark_burst('t', 12, 5.0, w.SPARK)])
w.write(s, '_scratch.hpar')
"
python tools/particle_gen/inspect_hpar.py data/client/Particles/Warrior/_scratch.hpar --check --one-shot
rm data/client/Particles/Warrior/_scratch.hpar
```

Expected: the inspect run reports **0 problems**.

- [ ] **Step 3: Commit**

```bash
git add tools/particle_gen/recipes/warrior_common.py
git commit -m "feat(particles): shared building blocks for the warrior effects"
```

---

### Task 9: Impact effects — SteelImpact, HeavyImpact, BloodImpact

**Files:**
- Create: `tools/particle_gen/recipes/warrior_impacts.py`
- Modify: `data/client/Particles/Warrior/{SteelImpact,HeavyImpact,BloodImpact}.hpar` (overwrite)

**Interfaces:**
- Consumes: `warrior_common` (Task 8).
- Produces: three `.hpar` files referenced by Task 11's kits.

`SteelImpact` is the most-fired effect in the game — every warrior swing plus eight creature types cast spell 8 `Strike`. It is deliberately the cheapest of the three.

- [ ] **Step 1: Write the recipe**

Create `tools/particle_gen/recipes/warrior_impacts.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Weapon impact effects. Run from the repo root::

    python tools/particle_gen/recipes/warrior_impacts.py

These spawn on the *target*, attached to a bone, at the moment the server reports a hit.
Scale reference: a player capsule is ~1.8 units tall, so a 0.10 particle is a fist-sized
spark and a 0.8 ring covers a torso.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import warrior_common as wc
from warrior_common import ParticleSystem


def steel_impact():
    """Sword on mail. The cheapest effect in the set by design: it fires on every warrior
    swing and on eight creature types, so two emitters and ~20 particles is the budget."""
    return ParticleSystem(emitters=[
        wc.spark_burst("Steel Sparks", count=16, speed=6.0, colour=wc.SPARK,
                       size=0.09, lifetime=0.34, gravity=-9.0, drag=1.8, spread=1.1),
        wc.soft_flash("Steel Flash", size=0.45, colour=wc.STEEL, alpha=0.45, lifetime=0.10),
    ])


def heavy_impact():
    """Execute and Shield Slam. Same language as SteelImpact, but with real weight behind
    it: a wider spark fan, a dust puff kicked loose, and a ground ring to sell the force."""
    return ParticleSystem(emitters=[
        wc.spark_burst("Heavy Sparks", count=30, speed=8.5, colour=wc.SPARK,
                       size=0.13, lifetime=0.48, gravity=-10.0, drag=1.4, spread=1.5),
        wc.soft_flash("Heavy Flash", size=0.75, colour=wc.STEEL, alpha=0.55, lifetime=0.14),
        wc.dust_cloud("Heavy Dust", count=14, spread=1.1, colour=wc.DUST,
                      alpha=0.26, size=0.55, lifetime=0.70, rise=0.5),
        wc.ground_ring("Heavy Ring", start_size=0.5, end_size=2.0, colour=wc.DUST,
                       alpha=0.30, lifetime=0.45),
    ])


def blood_impact():
    """Rend and Crippling Strike. Droplets arc down hard under gravity with very little
    drag, so it reads as spray rather than as smoke -- the previous version used a smoke
    material and looked like a dark cloud."""
    return ParticleSystem(emitters=[
        wc.spark_burst("Blood Spray", count=22, speed=4.5, colour=wc.BLOOD,
                       size=0.11, lifetime=0.55, gravity=-14.0, drag=0.4, spread=0.9),
        wc.spark_burst("Heavy Droplets", count=7, speed=2.6, colour=wc.BLOOD_DARK,
                       size=0.17, lifetime=0.70, gravity=-16.0, drag=0.2, spread=0.5),
        wc.dust_cloud("Blood Mist", count=9, spread=0.5, colour=wc.BLOOD_DARK,
                      alpha=0.22, size=0.30, lifetime=0.45, rise=0.2),
    ])


if __name__ == "__main__":
    wc.write(steel_impact(), "SteelImpact.hpar")
    wc.write(heavy_impact(), "HeavyImpact.hpar")
    wc.write(blood_impact(), "BloodImpact.hpar")
```

- [ ] **Step 2: Build the effects**

Run: `python tools/particle_gen/recipes/warrior_impacts.py`

Expected: three `wrote data/client/Particles/Warrior/....hpar` lines.

- [ ] **Step 3: Validate**

```bash
for f in SteelImpact HeavyImpact BloodImpact; do
  echo "--- $f"
  python tools/particle_gen/inspect_hpar.py "data/client/Particles/Warrior/$f.hpar" --check --one-shot
done
```

Expected: **0 problems** for each. Any `Additive.hmat`/`Unlit` or depth-write message means a material constant was wrong.

- [ ] **Step 4: Render and review the contact sheets**

```bash
for f in SteelImpact HeavyImpact BloodImpact; do
  python tools/particle_gen/preview.py "data/client/Particles/Warrior/$f.hpar" --figure --out "generated/warrior_visuals/preview_$f.png"
done
```

Then **Read each PNG**. This step is not optional — particle parameters are unintuitive and every shipped effect in this repo needed 2–4 rounds of looking. Judge against the checklist in `.claude/skills/particle-author/references/pitfalls.md`:

- scale versus the 1.8-unit silhouette (an impact should be torso-sized, not character-sized)
- does it read as the intended shape
- does the base stay anchored
- does it fade out rather than cut
- does it end at all

Retune the recipe and repeat until each passes.

- [ ] **Step 5: Commit**

```bash
git add tools/particle_gen/recipes/warrior_impacts.py
git commit -m "feat(particles): rebuild the warrior impact effects"
cd data/client && git add Particles/Warrior && git commit -m "Rebuild warrior impact particle effects" && cd ../..
```

---

### Task 10: Motion and cooldown effects — the remaining seven

**Files:**
- Create: `tools/particle_gen/recipes/warrior_abilities.py`
- Modify: `data/client/Particles/Warrior/{CleaveBurst,ChargeDust,ShockwaveDust,RallyBurst,RageBurst,DreadBurst,GuardBurst}.hpar` (overwrite)

**Interfaces:**
- Consumes: `warrior_common` (Task 8).
- Produces: seven `.hpar` files referenced by Task 11's kits.

- [ ] **Step 1: Write the recipe**

Create `tools/particle_gen/recipes/warrior_abilities.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Caster-side warrior ability effects. Run from the repo root::

    python tools/particle_gen/recipes/warrior_abilities.py

These spawn on the caster. The three cooldowns that grant the warrior something -- Battlecry,
Bloodrush, Last Stand -- carry the warm accent; the two debuff shouts sit cold and
desaturated so the two groups never read as the same effect.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import warrior_common as wc
from warrior_common import ParticleSystem


def cleave_burst():
    """A wide arc carved to the character's right. The velocity box is deliberately
    asymmetric -- that is what turns an omnidirectional burst into a directional sweep."""
    sweep = wc.spark_burst("Cleave Arc", count=26, speed=7.0, colour=wc.STEEL,
                           size=0.12, lifetime=0.40, gravity=-4.0, drag=2.0, spread=1.0)
    sweep.min_velocity = (0.2, -0.2, -1.6)
    sweep.max_velocity = (2.6, 0.6, 1.6)
    sweep.shape_extents = (0.30, 0.0, 0.0)
    return ParticleSystem(emitters=[
        sweep,
        wc.dust_cloud("Cleave Wake", count=10, spread=1.0, colour=wc.DUST,
                      alpha=0.20, size=0.45, lifetime=0.45, rise=0.3),
    ])


def charge_dust():
    """Kicked-up ground dust behind a sprinting armoured body. Staggered rings stamp the
    path; the dust drifts backward, opposite the direction of travel."""
    wake = wc.dust_cloud("Charge Wake", count=20, spread=0.9, colour=wc.DUST,
                         alpha=0.28, size=0.60, lifetime=0.85, rise=0.6)
    wake.min_velocity = (-0.8, 0.1, -2.4)
    wake.max_velocity = (0.8, 0.9, -0.4)
    return ParticleSystem(emitters=[
        wake,
        wc.ground_ring("Charge Ring A", start_size=0.6, end_size=1.8, colour=wc.DUST,
                       alpha=0.26, lifetime=0.50),
        wc.ground_ring("Charge Ring B", start_size=0.6, end_size=1.8, colour=wc.DUST,
                       alpha=0.22, lifetime=0.50, delay=0.22),
    ])


def shockwave_dust():
    """The signature effect. A ground ring racing outward is what makes this read as a
    shockwave; the previous version had no ring at all and looked like a dark dust cloud."""
    outward = wc.dust_cloud("Shock Wall", count=34, spread=2.6, colour=wc.DUST,
                            alpha=0.26, size=0.85, lifetime=0.85, rise=0.5)
    outward.shape_extents = (0.5, 0.0, 0.0)
    return ParticleSystem(emitters=[
        wc.ground_ring("Shock Ring", start_size=0.8, end_size=7.0, colour=wc.DUST,
                       alpha=0.42, lifetime=0.70),
        wc.ground_ring("Shock Ring Inner", start_size=0.5, end_size=3.4, colour=wc.DUST_DARK,
                       alpha=0.30, lifetime=0.55),
        outward,
        wc.spark_burst("Shock Grit", count=22, speed=5.0, colour=wc.DUST_DARK,
                       size=0.10, lifetime=0.70, gravity=-11.0, drag=1.0, spread=1.8),
    ])


def rally_burst():
    """Battlecry. Warm accent: an upward flare with a ground pulse and motes that outlive
    it, so the effect trails off instead of cutting."""
    flare = wc.spark_burst("Rally Flare", count=26, speed=4.0, colour=wc.RAGE,
                           size=0.14, lifetime=0.70, gravity=1.2, drag=1.0, spread=0.7)
    flare.min_velocity = (-0.6, 1.6, -0.6)
    flare.max_velocity = (0.6, 4.2, 0.6)
    motes = wc.dust_cloud("Rally Motes", count=16, spread=0.7, colour=wc.RAGE,
                          alpha=0.34, size=0.22, lifetime=1.10, rise=1.4)
    motes.material_name = wc.STAR
    motes.gravity = (0.0, 0.5, 0.0)
    return ParticleSystem(emitters=[
        flare,
        wc.ground_ring("Rally Ring", start_size=0.7, end_size=2.6, colour=wc.RAGE,
                       alpha=0.34, lifetime=0.55),
        motes,
    ])


def rage_burst():
    """Bloodrush. Warm accent held tight to the body -- this is an internal surge, not an
    outward blast, so nothing travels far."""
    embers = wc.spark_burst("Rage Embers", count=20, speed=2.6, colour=wc.RAGE_DEEP,
                            size=0.11, lifetime=0.80, gravity=0.9, drag=1.4, spread=0.5)
    embers.min_velocity = (-0.4, 0.8, -0.4)
    embers.max_velocity = (0.4, 2.6, 0.4)
    return ParticleSystem(emitters=[
        wc.dust_cloud("Rage Aura", count=18, spread=0.55, colour=wc.RAGE_DEEP,
                      alpha=0.30, size=0.50, lifetime=0.75, rise=1.0),
        embers,
        wc.ground_ring("Rage Ring", start_size=0.5, end_size=1.6, colour=wc.RAGE_DEEP,
                       alpha=0.28, lifetime=0.50),
    ])


def dread_burst():
    """Provoke and Demoralizing Shout. Cold, desaturated and low to the ground -- the
    deliberate opposite of the warm rally pair, so a debuff never reads as a buff."""
    pressure = wc.dust_cloud("Dread Pressure", count=26, spread=2.0, colour=wc.DREAD,
                             alpha=0.30, size=0.75, lifetime=0.80, rise=0.25)
    pressure.shape_extents = (0.4, 0.0, 0.0)
    return ParticleSystem(emitters=[
        wc.ground_ring("Dread Ring", start_size=0.7, end_size=4.4, colour=wc.DREAD,
                       alpha=0.34, lifetime=0.65),
        pressure,
        wc.spark_burst("Dread Grit", count=14, speed=3.4, colour=wc.DREAD,
                       size=0.09, lifetime=0.60, gravity=-8.0, drag=1.6, spread=1.4),
    ])


def guard_burst():
    """Last Stand. A steel-blue shell that expands and settles -- defensive, not explosive,
    so the particles slow down rather than fly apart."""
    shell = wc.spark_burst("Guard Shell", count=22, speed=3.0, colour=wc.GUARD,
                           size=0.13, lifetime=0.70, gravity=0.4, drag=3.0, spread=0.9)
    shell.min_velocity = (-0.9, 0.4, -0.9)
    shell.max_velocity = (0.9, 2.4, 0.9)
    return ParticleSystem(emitters=[
        shell,
        wc.ground_ring("Guard Ring", start_size=0.9, end_size=2.2, colour=wc.GUARD,
                       alpha=0.36, lifetime=0.60),
        wc.dust_cloud("Guard Haze", count=14, spread=0.7, colour=wc.GUARD,
                      alpha=0.24, size=0.55, lifetime=0.80, rise=0.5),
    ])


if __name__ == "__main__":
    wc.write(cleave_burst(), "CleaveBurst.hpar")
    wc.write(charge_dust(), "ChargeDust.hpar")
    wc.write(shockwave_dust(), "ShockwaveDust.hpar")
    wc.write(rally_burst(), "RallyBurst.hpar")
    wc.write(rage_burst(), "RageBurst.hpar")
    wc.write(dread_burst(), "DreadBurst.hpar")
    wc.write(guard_burst(), "GuardBurst.hpar")
```

- [ ] **Step 2: Build the effects**

Run: `python tools/particle_gen/recipes/warrior_abilities.py`

Expected: seven `wrote ...` lines.

- [ ] **Step 3: Validate all ten warrior effects together**

```bash
fail=0
for f in data/client/Particles/Warrior/*.hpar; do
  echo "--- $(basename $f)"
  python tools/particle_gen/inspect_hpar.py "$f" --check --one-shot || fail=1
done
echo "exit=$fail"
```

Expected: **0 problems** on all ten and `exit=0`. This is the check the original pass never ran.

- [ ] **Step 4: Render and review the contact sheets**

```bash
for f in CleaveBurst ChargeDust ShockwaveDust RallyBurst RageBurst DreadBurst GuardBurst; do
  python tools/particle_gen/preview.py "data/client/Particles/Warrior/$f.hpar" --figure --out "generated/warrior_visuals/preview_$f.png"
done
```

**Read each PNG** and judge against the pitfalls checklist. Pay particular attention to:

- `ShockwaveDust` — the ring must be clearly visible expanding along the ground; this is the effect the user specifically reported as a dark dust cloud.
- `CleaveBurst` — the sweep must be visibly directional, not a symmetric ball.
- `RallyBurst` vs `DreadBurst` — the two must be distinguishable at a glance.

Use `--camera top` on `ShockwaveDust` to confirm the ring reads as a ring:

```bash
python tools/particle_gen/preview.py data/client/Particles/Warrior/ShockwaveDust.hpar --camera top --out generated/warrior_visuals/preview_ShockwaveDust_top.png
```

Retune and repeat until each passes.

- [ ] **Step 5: Commit**

```bash
git add tools/particle_gen/recipes/warrior_abilities.py
git commit -m "feat(particles): rebuild the warrior ability effects"
cd data/client && git add Particles/Warrior && git commit -m "Rebuild warrior ability particle effects" && cd ../..
```

---

### Task 11: Rewrite the visualization data

**Files:**
- Create: `tools/warrior_visuals/author_visuals.py` (replaces the existing file wholesale)
- Delete: `tools/warrior_visuals/build_assets.py`
- Create: `tools/tests/test_warrior_visual_data.py`
- Modify: `data/editor/data/spell_visualizations.data`
- Modify: `data/client/ClientDB/spell_visualizations.data`

**Interfaces:**
- Consumes: sound ids 21–33 (Task 7), the ten `.hpar` files (Tasks 9–10), `SpellKit.sound_ids` (Task 4).
- Produces: visualization entries 26–39 with corrected animations, catalog sound ids and re-timed delays.

**Animation decisions.** `duration_ms` is removed from every kit so clips play at native rate — `ApplyAnimationToActor` only rescales when the field is present. Caster-side `delay_ms` values are derived from measured clip lengths (`CastRelease` 0.63 s, `Attack_1H_01/02` 0.97 s, `UnarmedAttack01` 0.70 s). `IMPACT`-scoped kits are keyed off the server hit packet rather than the caster animation, so their delay is 0.

**Two mappings need a human eye** and are called out at the end of the task: `HumanMale` has no shield-bash and no ground-slam clip, so Shield Slam keeps `UnarmedAttack01` (an off-hand forward thrust) and Shockwave moves from `UnarmedAttack01` to `CastRelease` (a downward outward gesture reads better for a ground slam than a jab).

- [ ] **Step 1: Write the authoring script**

Delete the old asset builder and replace the authoring script:

```bash
git rm tools/warrior_visuals/build_assets.py
rm -rf tools/warrior_visuals/__pycache__
```

Create `tools/warrior_visuals/author_visuals.py` (overwriting the existing file):

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Author the warrior spell visualization kits (entries 26-39).

Writes both the editor dataset and the client ClientDB copy. Only visualization entries and
each spell's visualization_id are touched; all other spell data is asserted unchanged.

    python tools/warrior_visuals/author_visuals.py            # validate only
    python tools/warrior_visuals/author_visuals.py --apply    # write both datasets

Design notes that are easy to undo by accident:

* No kit sets ``duration_ms``. ApplyAnimationToActor turns it into
  ``playRate = clipLength / duration``, so a guessed value time-warps the clip -- the
  previous pass ran every warrior animation 1.4x to 1.8x too fast.
* Kits use ``sound_ids`` (catalog entries) and leave ``sounds`` empty. Setting both would
  double-trigger.
* Caster-side delays come from measured clip lengths: CastRelease 0.63 s, Attack_1H_01/02
  0.97 s, UnarmedAttack01 0.70 s.
"""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

from google.protobuf import descriptor_pb2, descriptor_pool, json_format, message_factory

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
from spell_catalog_lib import load_catalogs  # noqa: E402
from proto_runtime import find_protoc  # noqa: E402

OUT = ROOT / "generated/warrior_visuals"
PARTICLE_DIR = "Particles/Warrior/"

# Event ids: 3 = CAST_SUCCEEDED, 4 = IMPACT, 5 = AURA_APPLIED.
CAST, IMPACT, AURA = "3", "4", "5"


def anim(name):
    """A caster kit that plays a clip at its native rate."""
    return {"scope": "CASTER", "loop": False, "animation_name": name}


def fx(particle, sound=None, scope="CASTER", bone=None, delay=0):
    kit = {"scope": scope, "loop": False, "particles": [PARTICLE_DIR + particle + ".hpar"]}
    if sound:
        kit["sound_ids"] = [sound]
    if bone:
        kit["attach_bone"] = bone
    if delay:
        kit["delay_ms"] = delay
    return kit


def definitions():
    """(visualization name suffix, spell ids, kits_by_event)."""
    return [
        ("Strike", [8, 71], {
            IMPACT: {"kits": [fx("SteelImpact", 21, "TARGET", "spine_03")]}}),
        ("Battlecry", [9], {
            CAST: {"kits": [anim("CastRelease"), fx("RallyBurst", 29, delay=150)]},
            AURA: {"kits": [fx("RallyBurst", scope="TARGET")]}}),
        ("Rend", [19], {
            CAST: {"kits": [anim("Attack_1H_01")]},
            IMPACT: {"kits": [fx("BloodImpact", 22, "TARGET", "spine_03")]}}),
        ("Charge", [48], {
            CAST: {"kits": [fx("ChargeDust", 27)]}}),
        ("Execute", [50], {
            CAST: {"kits": [anim("Attack_1H_02")]},
            IMPACT: {"kits": [fx("HeavyImpact", 23, "TARGET", "spine_03")]}}),
        ("Bloodrush", [62], {
            CAST: {"kits": [fx("RageBurst", 30)]}}),
        ("Crippling Strike", [69], {
            CAST: {"kits": [anim("Attack_1H_01")]},
            IMPACT: {"kits": [fx("BloodImpact", 22, "TARGET", "foot_l")]}}),
        ("Skullbash", [70], {
            CAST: {"kits": [anim("UnarmedAttack01")]},
            IMPACT: {"kits": [fx("SteelImpact", 24, "TARGET", "head")]}}),
        # No ground-slam clip exists on the Human rig; CastRelease's downward outward
        # gesture reads closer to a ground slam than UnarmedAttack01's jab. Its release
        # beat lands ~40% into the 0.63 s clip, hence the 260 ms delay.
        ("Shockwave", [122], {
            CAST: {"kits": [anim("CastRelease"), fx("ShockwaveDust", 28, delay=260)]}}),
        # No shield-bash clip either; UnarmedAttack01 is an off-hand forward thrust and is
        # the closest available.
        ("Shield Slam", [142], {
            CAST: {"kits": [anim("UnarmedAttack01")]},
            IMPACT: {"kits": [fx("HeavyImpact", 25, "TARGET", "spine_03")]}}),
        ("Last Stand", [205], {
            CAST: {"kits": [fx("GuardBurst", 33)]}}),
        # Attack_1H_02 is 0.97 s and makes contact about a third of the way in.
        ("Cleave", [209], {
            CAST: {"kits": [anim("Attack_1H_02"),
                            fx("CleaveBurst", 26, bone="hand_r", delay=300)]},
            IMPACT: {"kits": [fx("SteelImpact", scope="TARGET", bone="spine_03")]}}),
        ("Provoke", [216], {
            CAST: {"kits": [anim("CastRelease"), fx("DreadBurst", 31, delay=150)]},
            IMPACT: {"kits": [fx("DreadBurst", scope="TARGET")]}}),
        ("Demoralizing Shout", [217], {
            CAST: {"kits": [anim("CastRelease"), fx("DreadBurst", 32, delay=150)]},
            AURA: {"kits": [fx("DreadBurst", scope="TARGET")]}}),
    ]


def load_type(schema_dir, protos, message_name):
    with tempfile.TemporaryDirectory(prefix="warrior_vis_") as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(find_protoc(ROOT)), f"-I{schema_dir}",
                        f"--descriptor_set_out={desc}", "--include_imports", *protos],
                       cwd=schema_dir, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())
    pool = descriptor_pool.DescriptorPool()
    for f in file_set.file:
        pool.Add(f)
    return message_factory.GetMessageClass(pool.FindMessageTypeByName(message_name))


def validate(dataset, sound_ids):
    """Everything that must hold before either dataset is written."""
    for vis in dataset.entry:
        if not vis.name.startswith("Warrior - "):
            continue
        for event, kit_list in vis.kits_by_event.items():
            for kit in kit_list.kits:
                assert not kit.loop, f"{vis.name}: one-shot kits must not loop"
                assert not kit.HasField("duration_ms"), (
                    f"{vis.name}: duration_ms time-warps the clip; leave it unset")
                assert not (kit.sounds and kit.sound_ids), (
                    f"{vis.name}: a kit sets sounds or sound_ids, never both")
                assert not kit.sounds, f"{vis.name}: warrior kits use sound_ids"
                for sound_id in kit.sound_ids:
                    assert sound_id in sound_ids, f"{vis.name}: unknown sound id {sound_id}"
                for particle in kit.particles:
                    path = ROOT / "data/client" / particle
                    assert path.is_file(), f"{vis.name}: missing particle {particle}"
                if event == 3:
                    assert kit.scope == 0, "CastSucceeded has no target list"
    ids = [v.id for v in dataset.entry]
    assert len(ids) == len(set(ids)), "duplicate visualization ids"
    assert dataset.IsInitialized()


def upsert(dataset, drafts):
    by_name = {v.name: v for v in dataset.entry}
    for draft in drafts:
        target = by_name.get(draft["name"])
        if target is None:
            target = dataset.entry.add()
        else:
            assert target.id == draft["id"], (
                f"{draft['name']} already exists with id {target.id}, expected {draft['id']}")
            target.Clear()
        json_format.ParseDict(draft, target)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)

    catalogs = load_catalogs(str(ROOT))
    spells, visuals = catalogs["spells"], catalogs["spell_visualizations"]
    by_id = {s.id: s for s in spells.entry}
    existing = {v.name: v.id for v in visuals.entry}
    next_id = max(v.id for v in visuals.entry) + 1

    sounds_type = load_type(ROOT / "src/shared/proto_data", ["sounds.proto"], "mmo.proto.Sounds")
    sound_ids = {e.id for e in
                 sounds_type.FromString((ROOT / "data/editor/data/sounds.data").read_bytes()).entry}

    drafts, mapping = [], {}
    for suffix, spell_ids, events in definitions():
        name = "Warrior - " + suffix
        vis_id = existing.get(name)
        if vis_id is None:
            vis_id = next_id
            next_id += 1
        drafts.append({"id": vis_id, "name": name, "kits_by_event": events})
        for spell_id in spell_ids:
            assert by_id[spell_id].name == suffix or suffix == "Strike", (
                spell_id, by_id[spell_id].name, suffix)
            mapping[spell_id] = vis_id

    editor_visuals = type(visuals)()
    editor_visuals.CopyFrom(visuals)
    upsert(editor_visuals, drafts)
    validate(editor_visuals, sound_ids)

    (OUT / "visualizations.json").write_text(json.dumps(drafts, indent=2) + "\n")

    if not args.apply:
        print(f"validated {len(drafts)} visualizations across {len(mapping)} spells")
        return

    client_visuals_type = load_type(
        ROOT / "src/shared/client_data",
        ["spells.proto", "spell_visualizations.proto"],
        "mmo.proto_client.SpellVisualizations")

    paths = [ROOT / "data/editor/data/spell_visualizations.data",
             ROOT / "data/client/ClientDB/spell_visualizations.data"]
    backup = OUT / ("backup_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    backup.mkdir()
    for index, path in enumerate(paths):
        shutil.copy2(path, backup / f"{index}_{path.name}")

    paths[0].write_bytes(editor_visuals.SerializeToString())

    client_visuals = client_visuals_type.FromString(paths[1].read_bytes())
    upsert(client_visuals, drafts)
    validate(client_visuals, sound_ids)
    paths[1].write_bytes(client_visuals.SerializeToString())

    print(f"wrote {len(drafts)} visualizations to both datasets. Backup: {backup}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Validate without writing**

Run: `python tools/warrior_visuals/author_visuals.py`

Expected: `validated 14 visualizations across 15 spells`. An `unknown sound id` failure means Task 7 was not applied; a `missing particle` failure means Tasks 9–10 are incomplete.

- [ ] **Step 3: Apply**

Run: `python tools/warrior_visuals/author_visuals.py --apply`

Expected: `wrote 14 visualizations to both datasets. Backup: ...`

- [ ] **Step 4: Write the regression test**

Create `tools/tests/test_warrior_visual_data.py`. The gate runs `python -m unittest discover -s tools/tests`, so this guards the invariants on every run:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Guards the warrior visualization data invariants.

These are the mistakes the first warrior pass shipped: particle files that did not exist,
kits that time-warped their animation via duration_ms, and effects pointing at materials
the engine renders opaque. Skips rather than fails when protoc or the data submodules are
unavailable, so a partial checkout does not break the gate.
"""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def _load():
    from google.protobuf import descriptor_pb2, descriptor_pool, message_factory
    sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
    from proto_runtime import find_protoc

    schema = ROOT / "src/shared/proto_data"
    with tempfile.TemporaryDirectory() as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(find_protoc(ROOT)), f"-I{schema}",
                        f"--descriptor_set_out={desc}", "--include_imports",
                        "spell_visualizations.proto", "sounds.proto"],
                       cwd=schema, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())

    pool = descriptor_pool.DescriptorPool()
    for f in file_set.file:
        pool.Add(f)

    visuals_type = message_factory.GetMessageClass(
        pool.FindMessageTypeByName("mmo.proto.SpellVisualizations"))
    sounds_type = message_factory.GetMessageClass(
        pool.FindMessageTypeByName("mmo.proto.Sounds"))

    visuals = visuals_type.FromString(
        (ROOT / "data/editor/data/spell_visualizations.data").read_bytes())
    sounds = sounds_type.FromString(
        (ROOT / "data/editor/data/sounds.data").read_bytes())
    return visuals, sounds


class WarriorVisualDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (ROOT / "data/editor/data/spell_visualizations.data").is_file():
            raise unittest.SkipTest("data/editor submodule is not checked out")
        try:
            cls.visuals, cls.sounds = _load()
        except Exception as exc:  # protoc missing, protobuf missing
            raise unittest.SkipTest(f"cannot load proto data: {exc}")

        cls.kits = [(v.name, kit)
                    for v in cls.visuals.entry if v.name.startswith("Warrior - ")
                    for kl in v.kits_by_event.values() for kit in kl.kits]

    def test_warrior_visualizations_are_present(self):
        names = {v.name for v in self.visuals.entry if v.name.startswith("Warrior - ")}
        self.assertEqual(len(names), 14, f"expected 14 warrior visualizations, got {names}")

    def test_no_kit_time_warps_its_animation(self):
        # duration_ms becomes playRate = clipLength / duration. Guessed values ran the
        # original warrior animations 1.4x to 1.8x too fast.
        for name, kit in self.kits:
            self.assertFalse(kit.HasField("duration_ms"),
                             f"{name}: duration_ms time-warps the clip")

    def test_kits_use_the_sound_catalog_not_raw_paths(self):
        for name, kit in self.kits:
            self.assertFalse(list(kit.sounds),
                             f"{name}: warrior kits reference sound_ids, not file paths")

    def test_no_kit_sets_both_sound_fields(self):
        for name, kit in self.kits:
            self.assertFalse(kit.sounds and kit.sound_ids,
                             f"{name}: setting both fields double-triggers audio")

    def test_every_referenced_sound_id_exists(self):
        known = {e.id for e in self.sounds.entry}
        for name, kit in self.kits:
            for sound_id in kit.sound_ids:
                self.assertIn(sound_id, known, f"{name}: unknown sound id {sound_id}")

    def test_every_referenced_sound_file_exists(self):
        by_id = {e.id: e for e in self.sounds.entry}
        for name, kit in self.kits:
            for sound_id in kit.sound_ids:
                for path in by_id[sound_id].files:
                    self.assertTrue((ROOT / "data/client" / path).is_file(),
                                    f"{name}: missing sound file {path}")

    def test_every_referenced_particle_exists(self):
        for name, kit in self.kits:
            for particle in kit.particles:
                self.assertTrue((ROOT / "data/client" / particle).is_file(),
                                f"{name}: missing particle {particle}")

    def test_no_kit_loops(self):
        for name, kit in self.kits:
            self.assertFalse(kit.loop, f"{name}: a looping kit never self-terminates")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 5: Run the test**

Run: `python -m unittest tools.tests.test_warrior_visual_data -v`

Expected: 8 tests pass (or the whole class skips if protoc is unavailable).

- [ ] **Step 6: Confirm every warrior effect now validates**

```bash
python -c "
import sys; sys.path.insert(0,'.agents/skills/mmo-spell-designer/scripts')
from spell_catalog_lib import load_catalogs
c=load_catalogs('.')
EV={3:'CAST_SUCCEEDED',4:'IMPACT',5:'AURA_APPLIED'}
for v in sorted(c['spell_visualizations'].entry, key=lambda x:x.id):
    if not v.name.startswith('Warrior - '): continue
    print('###', v.id, v.name)
    for ev,kl in sorted(v.kits_by_event.items()):
        for k in kl.kits:
            bits=[]
            if k.animation_name: bits.append('anim='+k.animation_name)
            if k.particles: bits.append('par='+','.join(p.split('/')[-1] for p in k.particles))
            if k.sound_ids: bits.append('snd='+','.join(str(s) for s in k.sound_ids))
            if k.attach_bone: bits.append('bone='+k.attach_bone)
            if k.delay_ms: bits.append('delay=%d'%k.delay_ms)
            print('   %-15s %s' % (EV.get(ev,ev), ' '.join(bits)))
"
```

Expected: 14 entries, no `duration_ms` anywhere, every sound shown as a numeric id.

- [ ] **Step 7: Flag the two animation mappings for review**

Report to the user, in the task summary, that `HumanMale` has no shield-bash and no ground-slam clip, so Shield Slam keeps `UnarmedAttack01` and Shockwave moved to `CastRelease`. These are best-effort within the 21 available clips and want an in-game look.

- [ ] **Step 8: Commit**

```bash
git add tools/warrior_visuals/author_visuals.py tools/tests/test_warrior_visual_data.py
git rm --cached tools/warrior_visuals/build_assets.py 2>/dev/null || true
git commit -m "feat(spells): rewrite the warrior visualization kits"
cd data/editor && git add data/spell_visualizations.data && git commit -m "Rewrite warrior spell visualization kits" && cd ../..
cd data/client && git add ClientDB/spell_visualizations.data && git commit -m "Rewrite warrior spell visualization kits" && cd ../..
```

---

### Task 12: Retire the old placeholder sounds and run the gate

**Files:**
- Delete: `data/client/Sound/Spells/Warrior/Strike.wav` — and only whatever else Step 1 actually reports
- Modify: `data/client` and `data/editor` submodule pointers in the superproject

**Critical:** Task 3 wrote `Rend.wav`, `Execute.wav`, `Skullbash.wav`, `ShieldSlam.wav`, `Cleave.wav`, `Charge.wav`, `Shockwave.wav`, `Battlecry.wav`, `Bloodrush.wav`, `Provoke.wav`, `DemoralizingShout.wav` and `LastStand.wav` **in place**, replacing the placeholder content under the same filenames. Those files are live and referenced. Deleting them would destroy the freshly generated audio. The only name genuinely orphaned by the rebuild is `Strike.wav`, superseded by the three-file `Strike01/02/03` shuffle bag. Derive the list from Step 1's output; never from the pre-rebuild file listing.

**Interfaces:**
- Consumes: everything above.
- Produces: a green gate report.

`AbilityStrike03.wav` and `AbilityStrike04.wav` stay — the user asked for them to be left alone.

- [ ] **Step 1: Find genuinely unreferenced sound files**

```bash
python -c "
import sys; sys.path.insert(0,'.agents/skills/mmo-spell-designer/scripts')
sys.path.insert(0,'tools/warrior_visuals')
from spell_catalog_lib import load_catalogs
import subprocess, tempfile
from pathlib import Path
from google.protobuf import descriptor_pb2, descriptor_pool, message_factory
from proto_runtime import find_protoc
ROOT=Path('.').resolve(); schema=ROOT/'src/shared/proto_data'
with tempfile.TemporaryDirectory() as t:
    d=Path(t)/'d.pb'
    subprocess.run([str(find_protoc(ROOT)),f'-I{schema}',f'--descriptor_set_out={d}','--include_imports','sounds.proto'],cwd=schema,check=True)
    fs=descriptor_pb2.FileDescriptorSet.FromString(d.read_bytes())
p=descriptor_pool.DescriptorPool()
for f in fs.file: p.Add(f)
S=message_factory.GetMessageClass(p.FindMessageTypeByName('mmo.proto.Sounds'))
snd=S.FromString((ROOT/'data/editor/data/sounds.data').read_bytes())
referenced={f.split('/')[-1] for e in snd.entry for f in e.files}
for v in load_catalogs('.')['spell_visualizations'].entry:
    for kl in v.kits_by_event.values():
        for k in kl.kits:
            for s in k.sounds: referenced.add(s.split('/')[-1])
on_disk={p.name for p in (ROOT/'data/client/Sound/Spells/Warrior').glob('*.wav')}
print('UNREFERENCED:', sorted(on_disk - referenced))
"
```

Expected output lists the old placeholder files plus `AbilityStrike03.wav` and `AbilityStrike04.wav`.

- [ ] **Step 2: Delete only the superseded placeholders**

Step 1 should report exactly three unreferenced names: `Strike.wav`, `AbilityStrike03.wav`, `AbilityStrike04.wav`. The two `AbilityStrike*` files stay — the user asked for them to be left alone. So the removal is one file:

```bash
git -C data/client rm Sound/Spells/Warrior/Strike.wav
```

If Step 1 reports any name **not** in that expected set of three, stop and investigate rather than deleting it: an unexpected entry means a kit lost its sound reference somewhere in Tasks 7 or 11, and the fix is to restore the reference, not to delete the file.

- [ ] **Step 2a: Confirm the generated audio survived**

```bash
python -c "
import wave, glob, os
files = sorted(glob.glob('data/client/Sound/Spells/Warrior/*.wav'))
for f in files:
    w = wave.open(f); p = w.getparams(); w.close()
    print(os.path.basename(f), p.nchannels, p.framerate, p.sampwidth*8, round(p.nframes/p.framerate,2))
print(len(files), 'files')
"
```

Expected: 17 files — the 15 generated ones (all `1 44100 16`) plus the two untouched `AbilityStrike*` files at `2 48000 16`. If a generated file is missing, it was deleted in error; recover it with `git -C data/client checkout HEAD -- <path>`.

- [ ] **Step 3: Verify nothing references a missing file**

Run: `python -m unittest tools.tests.test_warrior_visual_data -v`

Expected: 8 tests pass, including `test_every_referenced_sound_file_exists`.

- [ ] **Step 4: Commit the submodules and advance the pointers**

```bash
cd data/client && git commit -m "Retire the superseded warrior placeholder sounds" && cd ../..
git add data/client data/editor
git commit -m "chore: advance the data submodule pointers"
```

- [ ] **Step 5: Full verification sweep**

```bash
python tools/protocol_version_check.py
python -m unittest discover -s tools/tests -p "test_*.py"
for f in data/client/Particles/Warrior/*.hpar; do python tools/particle_gen/inspect_hpar.py "$f" --check --one-shot || echo "FAILED $f"; done
```

Expected: protocol check exits 0; all tool tests pass; every effect reports 0 problems.

- [ ] **Step 6: Run the gate**

Run the `/gate` skill, which executes `tools/gate/verify.ps1` (Debug build + unit tests + E2E) followed by a code review of the branch diff.

Expected: green report at `tools/gate/last_report.json`.

- [ ] **Step 7: Report the in-game verification the plan cannot cover**

Tell the user explicitly what still needs a human look, and do not claim it verified:

- the two animation mappings from Task 11 (Shield Slam, Shockwave)
- whether the caster-side `delay_ms` values land on the intended beat
- whether the sound mix balance holds in real combat, especially `Strike` at high frequency

---

## Out of Scope

Recorded so it is not silently absorbed into this branch:

- **`Particles/Additive.hmat` is still broken** for `ResetTalents.hpar` and `FrostImpact.hpar`, and `Sparkles.hpar` still has a reversed colour curve. Same class of bug, separate pass.
- **Per-race shout vocalisations** through the existing voice-line system.
- **New animation clips** for a real shield bash and ground slam.
- **The Orc and Undead Human rigs** (8 and 1 clips respectively) would need a C++ animation fallback chain before those races could be enabled. Dead code today, since both are `disabled=True`.
