<h1 align="center"><img src="docs/logo.svg" alt="Flipparina" width="520"></h1>

A Flipper Zero app that plays the ocarina in *Zelda: Ocarina of Time* and *Majora's Mask*. It either acts as a wired USB pad and presses the buttons for you, or plays the notes out loud for the Switch 2 version's microphone.

## Install

1. Download the `.fap` for your firmware from the [latest release](https://github.com/elpendor/flipparina/releases/latest): `flipparina-official-release.fap` for stock firmware, `flipparina-momentum-release.fap` for Momentum.
2. Copy it to the Flipper's SD card as `apps/USB/flipparina.fap`, for example with qFlipper's file manager or the Flipper mobile app.
3. Open **Apps → USB → Flipparina**.

If the Flipper shows an "API mismatch" error, the file doesn't match your firmware version: update the firmware or build the app yourself (below).

## Build

```sh
python -m ufbt          # build
python -m ufbt launch   # build, install and run (exit the app first)
```

To release, run the **Build** workflow on GitHub (Actions → Build → Run workflow) and pick **patch**, **minor** or **major**. It tags the next version and attaches the `.fap` files.

## Use

1. Open **Apps → USB → Flipparina** and pick an **Output** in **Settings** (see below).
2. For the pad outputs, plug the Flipper into the Switch.
3. Pick a song and press OK. The Flipper asks whether the ocarina is out and takes it out for you if not.

With a pad output, the Flipper's USB acts as a gamepad while the app runs, so qFlipper can't connect.

### Output modes

Choose in **Settings > Output**. The choice is remembered.

| Output | For | How it plays | Notes |
|---|---|---|---|
| **N64 (NSO)** | OoT / MM in the Nintendo Switch Online N64 app | USB pad: A and C-buttons (right stick) | R/Z (±1 semitone) and full stick bends (±2) cover every note from B3 to F5 |
| **Switch 2** | Ocarina of Time on Switch 2 | USB pad: L, R, A, Y, X | Left stick bends (full tilt ±2, half tilt ±1) cover C4 to E5 |
| **Speaker (mic)** | Ocarina of Time on Switch 2 | Flipper speaker, picked up by the console's microphone | No cable needed. Untested |

### Menu

- **Ocarina of Time songs** / **Majora's Mask songs**: the built-in songs.
- **MIDI files**: plays the melody of a `.mid` file from `SD:/apps_data/flipparina/`.
- **Take control** (pad outputs): makes the Flipper the active controller. Use it after using the Joy-Cons. On Switch 2 it also runs by itself on the first play after connecting.
- **Settings**:
  - **Output**: see above.
  - **Ocarina on** (N64): which C button the ocarina is assigned to, so the Flipper can take it out. Asked the first time it's needed.
  - **Out of range**: notes outside the ocarina's range are left out (**Skip**, the default) or play an octave up or down (**Octave**).

With several instruments in a MIDI file, hold Left/Right to pick the melody's channel.

### Player controls

| Button | Action |
|---|---|
| OK | Play / stop |
| Up / Down | Previous / next song (or MIDI file in the same folder) |
| Left / Right | Tempo − / + |
| Hold Up / Down | Key − / + (MIDI files) |
| Hold Left / Right | MIDI channel (MIDI files) |
| Back | Back to the menu |

## Advanced

Button mapping, timing, stick bend and speaker pitch/volume are in `config.h`.

## Credits

The built-in songs use the games' own note data (`sOcarinaSongNotes`) from the [zeldaret/oot](https://github.com/zeldaret/oot) and [zeldaret/mm](https://github.com/zeldaret/mm) decompilation projects.

Not affiliated with or endorsed by Nintendo. *The Legend of Zelda* is a trademark of Nintendo.
