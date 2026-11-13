# Flipparina

A Flipper Zero app that plays the ocarina in *Zelda: Ocarina of Time* and *Majora's Mask*. It either acts as a wired USB pad and presses the buttons for you, or plays the notes out loud for the Switch 2 version's microphone.

## Build

```sh
python -m ufbt          # build
python -m ufbt launch   # build, install and run (exit the app first)
```

## Use

1. Open **Apps → USB → Flipparina** and pick an **Output** (see below).
2. For the pad outputs, plug the Flipper into the Switch.
3. Take out the ocarina in game, pick a song and press OK.

While the app is running, the Flipper's USB acts as a gamepad, so qFlipper can't connect.

### Output modes

Choose with the **Output** item on the main menu (OK or Left/Right switches it). The choice is remembered.

| Output | For | How it plays | Notes |
|---|---|---|---|
| **N64 (NSO)** | OoT / MM in the Nintendo Switch Online N64 app | USB pad: A and C-buttons (right stick) | R/Z (±1 semitone) and full stick bends (±2) cover every note from B3 to F5 |
| **Switch 2** | Ocarina of Time on Switch 2 | USB pad: L, R, A, Y, X | Left stick bends (full tilt ±2, half tilt ±1) cover C4 to E5 |
| **Speaker (mic)** | Ocarina of Time on Switch 2 | Flipper speaker, picked up by the console's microphone | No cable needed. Untested |

### Menu

- **Ocarina of Time songs** / **Majora's Mask songs**: the built-in songs.
- **Play MIDI file**: plays the melody of a `.mid` file from `SD:/apps_data/flipparina/`. Notes the current output can't play are skipped. `tools/ocarina_fit.py` moves a melody into the ocarina's range.
- **Output**: see above.
- **Ocarina on** (N64): which C button the ocarina is assigned to, so the Flipper can take it out. Asked the first time it's needed.
- **Take control** (Switch 2): makes the Flipper the active controller. Runs by itself on the first play after connecting; use it again after using the Joy-Cons.

### Player controls

| Button | Action |
|---|---|
| OK | Play / stop |
| Left / Right | Tempo − / + |
| Up / Down | Transpose |
| Hold Up / Down | Change MIDI channel |
| Back | Back to the menu |

## Settings

Button mapping, timing, stick bend and speaker pitch/volume are in `config.h`.

## Credits

The built-in songs use the games' own note data (`sOcarinaSongNotes`) from the [zeldaret/oot](https://github.com/zeldaret/oot) and [zeldaret/mm](https://github.com/zeldaret/mm) decompilation projects.

Not affiliated with or endorsed by Nintendo. *The Legend of Zelda* is a trademark of Nintendo.
