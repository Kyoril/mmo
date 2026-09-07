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
