# ChordGen (VST3)

Genre-aware chord, bass and melody generator for FL Studio (or any VST3 host).
It is the Piano Roll script as a plugin: same engine, with a built-in sound, a piano-roll preview and
drag-out MIDI.

## Get the plugin (no programming needed)

1. Create a free GitHub account and a new empty repository.
2. Upload everything in this folder to it (drag the files into the web page, keep the folder structure,
   including the hidden `.github` folder).
3. Open the repository's **Actions** tab. The "Build ChordGen" workflow starts by itself (a few minutes).
4. When it is green, open the run and download **ChordGen-VST3-Windows** under *Artifacts*.
5. Unzip it and copy the `ChordGen.vst3` folder to `C:\Program Files\Common Files\VST3`.
6. In FL Studio: *Options > Manage plugins > Find plugins*. ChordGen appears as an instrument.

If the build fails, open the failed step, copy the red error text and send it to me.

## Using it

- **Generate** makes a new pattern. Nothing repeats; every result is remembered
  (`%APPDATA%\ChordGen\history.txt`, shared by all instances). **Reset history** wipes it.
- **Play** auditions the loop with the built-in sound when the host is stopped. When the host plays,
  ChordGen follows the host transport.
- **Drag MIDI** onto an FL Studio channel, the Piano roll or the Playlist. The file has Chords, Bass and
  Melody on separate tracks/channels.
- To play the notes through another instrument instead: switch **Sound** off and route ChordGen's MIDI
  output to it with FL's plugin-wrapper MIDI output port.
- The pattern and all settings are saved with the FL project.

## Layout

- `Source/Engine.*`  the generator (plain C++17, unit-tested)
- `Source/Plugin*.*`  JUCE plugin, editor and built-in synth
- `Tests/engine_test.cpp`  engine tests

JUCE is used under its AGPLv3 license, which is fine for personal use. If you distribute the plugin you
must also share this source code, or buy a JUCE license.
