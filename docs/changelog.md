## 1.2.1

- N64 (NSO) and Switch 2 each keep their own harmony delay, since NSO lags far more. N64 starts at 380 ms; Switch 2 keeps the delay set in 1.2.0.

## 1.2.0

- Harmony: with a pad output, the Flipper's speaker plays a second MIDI channel along with the game's ocarina. Pick it in the player's Harmony row.
- **Settings > Harmony delay** lines the harmony up with the game.
- `tools/make_test_midi.py` also writes `harmony_sync.mid` for tuning the delay.
- Back from a MIDI file returns to the file browser, at that file, instead of the main menu.
- The MIDI player shows melody, harmony, key and tempo as rows: Up/Down picks one, Left/Right changes it. No more held buttons; Up/Down no longer switches files.
- A MIDI file with exactly two channels starts with the first as the melody and the second as the harmony.
- The player's Key row shows how many notes fall out of range, and a Delay row appears while a harmony is on.
- Settings and the player show values with arrows on the selected row; long lists have a scrollbar.
- OK says why when a song can't play ("No notes in range", "USB busy") instead of doing nothing.
- `.mid` files show the app's icon in the file browser.
- Changing the melody channel keeps a key set by hand; the header says "No USB" instead of "--".
- The app closes when asked remotely (`loader close`, used by `ufbt launch`), even mid-song, except while the file browser or a dialog is open.

## 1.1.0

- Switch 2 output bends semitones with D-pad up/down instead of a half stick tilt, reaching B3 to F5 instead of C4 to E5.
- MIDI files with fewer than 16 notes no longer fail with "Out of memory".
- MIDI modulation (CC1) plays as vibrato, by tilting the stick sideways.
- `tools/make_test_midi.py` also writes `chromatic_vibrato.mid`; `tools/add_vibrato.py` adds vibrato to a MIDI file's long notes.

## 1.0.0

- First release.
- All official ocarina songs from Ocarina of Time and Majora's Mask, with the games' own timing.
- MIDI file player with automatic key, tempo, channel and file switching.
- Outputs: NSO N64 pad, Switch 2 pad, and the Flipper speaker for the Switch 2 microphone.
- Take control and "Is the ocarina out?" helpers for playing in game.
