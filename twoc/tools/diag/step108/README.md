# Step 108's tests (notes 18.111): the audio DLL, the static runtime

Copy these into a scratch copy of `../sweep` (the harness: `exe` folders beside it, `root/game_files` with a Crashdat
junction, `runs/`), then run from there. Python: the repo's `.venv` (numpy, scipy).

| File | What it does |
|---|---|
| `aud108.sh TAG EXEDIR title\|demo\|hub\|level [ENV=VAL...]` | One scenario with the APU's own output (`RECOMP_APU_OUT_WAV` -> `runs/TAG.apu.wav`) and the loopback (`loopcap` -> `runs/TAG.loop.wav`): the slow title (Cortex's lines) 75 s, the attract demo (Arctic Antics) 110 s, a new game's hub 45 s, a level (`LV`, `RP`) 45 s. |
| `chain_b.sh EXEDIR PFX`, `chain_f.sh` | Every scenario twice at Game (60) and Match display; the feature checks before (`B`) and after (`N`): HRTF off, 5.1 forced, music 0, effects 0, Droid Void with the drone mute on and off. |
| `audcmp.py A.wav B.wav` | How alike two recordings are: B aligned to A, then each 0.5 s window with sound matched within +-0.25 s (or anywhere in +-12 s): the correlation's distribution and how many windows are **sample-identical**. Two runs of one exe set the floor (the game's timing moves sounds against the APU's frames), not 1.0. |
| `apusum.py LOG...` | One line a run from the `[APU-OUT]` lines: underruns, the frame rate's range, late wakeups, the longest gap, the longest swap gap. |
| `featsum.py WAV... [--from --to]` | Each channel's RMS, side against mid (centred = low), the share of samples with L == R. |
| `dlltest.py EXEDIR [V2DLL]` | The exe and `xemu_apu.dll` in a folder with spaces and "&": the real DLL, none, good ones planted in the current folder and on PATH (must be ignored), a text file, Windows' `version.dll` renamed, an interface-version-2 build; reads and clicks each message box, prints the exit code and the `[APU-DLL]` line. |
| `crashtest.py [ADDR]` | Makes the running game crash on purpose (a thread started at an unmapped address) to see the `[CRASH]` block. |
