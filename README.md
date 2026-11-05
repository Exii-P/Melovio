# Melovio

Melovio is a standalone desktop application for writing Japanese singing-voice synthesis projects: a piano-roll editor with a pronunciation (phoneme) editor, a voicebank manager, a voicebank workshop, and WAV export.

**Melovio Studio** is the application; **Wauvio-Vocal** is the synthesis backend it uses. They are separate things: the backend is vendored unmodified in `third_party/wauvio-cpp/` and is only touched through one bridge file (`src/core/engine_bridge.cpp`).

**There are no recordings in this project.** Wauvio-Vocal is a parametric (Klatt-style formant + LF glottal source) synthesiser. Every sound is computed at render time, so sample-based voicebanks are not supported. See [Known limitations](#known-limitations).

## Contents

- [What you get](#what-you-get)
- [Quick start](#quick-start)
- [Building](#building)
- [User guide](#user-guide)
- [Voicebanks](#voicebanks)
- [Project file format](#project-file-format-mvp)
- [Synthesis pipeline](#synthesis-pipeline)
- [Architecture](#architecture)
- [Using `melovio_core` from C++](#using-melovio_core-from-c)
- [Development](#development)
- [Features and status](#features-and-status)
- [Known limitations](#known-limitations)

## What you get

| Part | Binary / target | Notes |
|---|---|---|
| Core library | `melovio_core` | Projects, timeline, pronunciation, voicebanks, rendering, export, transport. No GUI, no audio device. |
| GUI | `melovio-studio` | Dear ImGui (docking) + SDL2 + OpenGL |
| CLI | `melovio-cli` | validate / render / package / analyze without a GUI |
| Tests | `melovio_tests` | doctest suite |

Bundled voicebanks: **Yune**, **Koharu**, **Reiha**, **Itsuki**, **Kyo**.

## Quick start

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build            # or ./build/melovio_tests
./build/melovio-studio examples/sakura.mvp
./build/melovio-cli render examples/sakura.mvp sakura.wav
```

## Building

### Requirements
- A C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+), CMake 3.20+, and Git (CMake fetches dependencies). Python is not needed.
- Network access at configure time for FetchContent (pinned versions). Dependencies are not vendored in the tree except Wauvio-CPP.
- GUI on Linux: X11 or Wayland development libraries are only needed if SDL2 is built from source (otherwise install `libsdl2-dev`). OpenGL needs no dev package: ImGui's backend loads GL at run time.
- A Japanese-capable font for lyrics (Noto Sans CJK, IPA, Takao, Yu Gothic, Meiryo and Hiragino are searched). Set `MELOVIO_FONT=/path/to/font.ttf` to force one. Without one the UI still works but kana render as boxes.

### Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Options:

- `-DMELOVIO_BUILD_GUI=OFF` – core + CLI + tests only, no SDL/ImGui
- `-DMELOVIO_BUILD_CLI=OFF`
- `-DMELOVIO_BUILD_TESTS=OFF`
- `-DMELOVIO_WARNINGS_AS_ERRORS=ON`
- `-DMELOVIO_WAUVIO_DIR=<path>` – use another Wauvio-CPP checkout (the code must stay unmodified)

The build copies `voicebanks/` next to the binaries (`build/voicebanks`).

### Test

```sh
ctest --test-dir build --output-on-failure   # or ./build/melovio_tests
```

Tests need no display and no audio hardware.

### Install

```sh
cmake --install build --prefix /usr/local
```

Installs `bin/melovio-studio`, `bin/melovio-cli`, and the bundled voicebanks to `share/melovio/voicebanks`.

### Where Melovio keeps things

| What | Linux | Windows | macOS |
|---|---|---|---|
| Imported voicebanks | `~/.local/share/melovio/voicebanks` | `%APPDATA%/melovio/voicebanks` | `~/Library/Application Support/melovio/voicebanks` |
| Settings, layout, user dictionary | `~/.config/melovio/` | `%APPDATA%/melovio/` | `~/Library/Application Support/melovio/` |
| Autosave (recovery) | `<config>/autosave/` | | |

Extra voicebank search folders: set `MELOVIO_VOICEBANKS` (path list) or the `voicebank_dirs` setting.

### Running headless (CI / servers)

`melovio-cli` works without a display. `melovio-studio` needs a window system; under Xvfb it runs with Mesa software rendering:

```sh
xvfb-run -a ./build/melovio-studio --frames 30 --screenshot out.bmp
```

Without an audio device the editor still opens and export works; playback reports "No audio device".

### Troubleshooting
- *"wauvio_vocal_v2.hpp not found"* – `third_party/wauvio-cpp` is missing or `MELOVIO_WAUVIO_DIR` is wrong.
- *Kana show as boxes* – install Noto Sans CJK or set `MELOVIO_FONT`.
- *Blank/black window* – the GL driver is too old (OpenGL 3.0 or GLES 3.0 is required).
- *Platforms* – developed and tested on Linux only. Windows and macOS builds are expected to work (no platform-specific code beyond paths and SDL) but have **not** been built.

## User guide

### Window layout
Menu bar and **Transport** on top; **Tracks** on the left; **Piano Roll** in the centre with **Parameter Lanes** and **Voicebanks** below; **Phonemes & Lyrics** and **Note Properties** on the right. All panels dock, tab and float; the layout is remembered (View > Reset layout, then restart).

### Projects
File > New / Open / Open recent / Save (Ctrl+S) / Save as. *Project settings* edits title, composer, lyricist, comment, tempo changes, time-signature changes and the autosave interval.

Unsaved changes prompt before New/Open/Quit. Autosave writes to a recovery folder every N seconds (default 60) and never overwrites your file; after a crash you are offered *Recover*.

If a project needs a voicebank that is not installed you get a dialog with **Locate folder...**; the located folder is stored as a *relative* hint.

### Piano roll
- **Select tool (V)**: click a note, drag to move (time and pitch), drag its left/right edge to resize, drag empty space for a rubber band, Ctrl/Shift to add to the selection.
- **Draw tool (D)**: click to create a note with the grid length and default lyric, drag to set its length.
- Double-click a note to type its lyric; separate words with spaces to spread syllables over following notes.
- Right-click for the context menu (cut/copy/paste/delete/transpose/quantize...). Ctrl+C/X/V copy/cut/paste at the cursor, Del deletes, Q quantizes to the grid, arrows nudge (Up/Down transpose, Shift = octave).
- **Grid**: 1/4, 1/8, 1/16, 1/32 and triplets. **Snap** toggles snapping (hold Alt to bypass).
- Zoom with the H/V sliders, Ctrl+wheel (time) or Ctrl+Shift+wheel (pitch); wheel scrolls pitch, Shift+wheel scrolls time, middle-drag pans. **Follow** keeps the playhead in view.
- Click the ruler to set the cursor/playhead; drag the thin strip above the ruler to set the **loop** region (Loop checkbox or L).
- Notes are monophonic per track: overlapping notes are trimmed. Notes the engine cannot sing (red) or outside the voice's range (amber/red) are flagged.
- Undo/redo: Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z). Continuous drags and slider moves collapse into one step.

### Playback
Space = play/pause, Stop returns to the cursor, `|<` rewinds, *Preview sel.* (Enter) plays the selected notes. Synthesis happens on a background thread, so the UI never blocks; the transport bar shows a cancellable render progress bar. After edits, the next Play re-renders (unchanged phrases come from a cache).

### Lyrics and pronunciation
The **Phonemes & Lyrics** panel shows, for the selected note, the syllables and phonemes the engine will sing, with block widths proportional to each syllable's share of the note.

- Kana and romaji are accepted. Kanji needs a reading (kana, or the user dictionary `<config>/user_dictionary.tsv`, one `surface<TAB>reading` per line) and is flagged otherwise.
- は/へ/を as particles: use the suggested particle reading.
- **Manual pronunciation**: type phoneme notation (`{k a}`, `{sh i N}`; `N` moraic nasal, `Q` small tsu, `-` long vowel) and *Apply*. Invalid text is rejected with a reason. *Clear* returns to the lyric.
- **Syllable timing**: length and consonant sliders per syllable (stored as an explicit pronunciation override).
- **Lyric entry** tab: assign space-separated words to consecutive notes. **All notes** tab lists every note's phonemes and status.

### Parameter lanes
Pick a parameter (only ones the engine supports) and click to add points, drag to move, right-click to delete, Shift-drag to snap to the default value. Lanes are additive offsets on top of note/track values.

Per-track values are in **Tracks > Track style overrides**; per-note values are in **Note Properties** (check a box to override, uncheck to inherit).

The available parameters are those the engine really has:

- **Lanes**: pitch deviation, volume, brightness, breathiness, tension, formant shift, mouth opening, growl, tremor, fry, air, register.
- **Note/track values**: softness, power, vibrato depth/rate/delay, portamento, scoop, consonant strength, vowel clarity, legato.

There is deliberately no "dynamics" or "gender" lane, and no "tension" lane beyond the above.

### Tracks
Add/remove, name, mute/solo, gain, pan, voicebank, persona, "natural" amount (+ take) and auto-expression. Each track is one monophonic voice; use several tracks for harmony.

### Export
File > Export audio (Ctrl+E): scope (whole project / region / single track), sample rate 22.05–96 kHz, 16/24-bit PCM or 32-bit float, mono/stereo, optional normalize, dither and tail. There is a progress bar and Cancel. An existing file asks before being replaced, and a cancelled export leaves nothing behind.

### Shortcuts
Ctrl+N/O/S, Ctrl+Shift+S, Ctrl+E, Ctrl+Z/Y, Ctrl+C/X/V/A, Space, Del, Home, V/D tools, Q quantize, L loop, Enter preview selection.

## Voicebanks

Melovio voicebanks describe a *voice model*, not recordings: formants, glottal-source settings, pitch range and character defaults.

### Voicebanks panel
Browse installed voicebanks (bundled/user, versions), see manifest details, range, validation messages and compatibility with the current track's note range. Actions: *Use for current track*, *Preview* (sings the voicebank's preview lyric), *Import package/folder*, *Export .mvb*, *Remove* (user voicebanks only, with confirmation), *Rescan*.

### Package format
A voicebank is a **directory** or a **`.mvb`** file (a zip of that directory, with `voicebank.json` at the root). Installed copies live in `<user dir>/<id>-<version>/`; several versions of one id can coexist.

```
yune/
  voicebank.json     manifest
  voice.voice        Wauvio-Vocal v2 voice profile (text; strict parser, errors carry line numbers)
```

#### voicebank.json (format_version 1)

| Field | Required | Meaning |
|---|---|---|
| `format` = `"melovio-voicebank"`, `format_version` = `1` | yes | |
| `id` | yes | `[a-z0-9_-]+`, referenced by projects |
| `version` | yes | `MAJOR.MINOR.PATCH` |
| `name`, `native_name`, `author`, `license`, `description`, `tagline`, `homepage`, `tags[]`, `languages[]` | name, author, license: warned if missing | `languages` must include `ja` to be usable for Japanese |
| `character.notes` | no | free text |
| `synthesis.engine` = `wauvio-vocal`, `engine_version` = `v2`, `kind` = `parametric-formant`, `profile` | yes | `parametric-formant` is the only `kind` the engine can render. `sample-based` is **rejected** (`vb.kind`) |
| `phonetics.mappings` | no | `{ "lyric": "{phoneme notation}" }`, applied before synthesis; validated with the engine parser |
| `phonetics.recordings[]` | no | `{phoneme, file}`; the current engine cannot play them, so referenced files are checked and a warning says they are unused (`vb.recordings_unused`) |
| `phonetics.preview_lyric` | no | used by the Preview button |
| `compatibility.min_melovio` | no | refused (`vb.too_new`) if newer than the running app |
| `completeness` | recommended | honest statement of what the package contains; shown to users |

### Validation
`melovio-cli validate-voicebank <dir> [--deep]`

- **Errors** make a voicebank unusable: bad id/version, wrong engine/kind, missing or invalid profile (engine parse error with line), invalid mapping, too-new `min_melovio`, language without `ja`.
- **Warnings**: missing author/license, unused recordings, not deep-checked.
- `--deep` (and the workshop's *Run completeness check*) synthesises about 131 kana units plus vowels at the range limits through the real engine and verifies the output is finite, audible and not clipping.

### Packaging and install
```sh
melovio-cli package <dir> out.mvb
melovio-cli install <dir|.mvb>
```

The GUI imports from the Voicebanks panel. Extraction rejects absolute paths, `..` and oversized archives, stages into a temporary folder and moves into place. Installing an existing id+version needs *Replace*. Bundled voicebanks are read-only.

### How projects find voicebanks
By id (+ exact version if present, otherwise the highest usable one) in the library; otherwise the project's relative `path_hint`; otherwise Missing. The state is always shown, never hidden, and missing voicebanks are never silently replaced.

### Making a voicebank (workshop)
1. Open **View > Voicebank workshop**, pick a voicebank to start from, and give a new `id` and name. Melovio copies it to `<user dir>/workshop/<id>/` (installed and bundled voicebanks are never touched).
2. **Metadata** tab: name, author, license, description, preview lyric, phonetic mappings (e.g. map the written lyric `は` to `{w a}` for a particular voice) and the *completeness statement* (keep it truthful).
3. **Voice model** tab: comfortable and hard pitch ranges (should nest), natural amount, lyric-parsing defaults, per-vowel F1–F3/B1–B3/gain (with an F1–F2 vowel chart), and the engine's model/tune/style values. Saving rewrites `voice.voice` from your values while preserving personas and anything not shown.
4. **Test & validate**: *Save*, then *Render test phrase* (waveform) or *Play*; run the **completeness check** and fix failing units.
5. **Install into library** (replaces the same id+version), or export a `.mvb` from the Voicebanks panel to share.

By hand: copy a bundled voicebank directory, edit `voicebank.json` and `voice.voice`, then run `melovio-cli validate-voicebank <dir> --deep` and `melovio-cli package <dir> out.mvb`. Voice parameters are documented by the engine (`third_party/wauvio-cpp`); Melovio does not reinterpret them.

The workshop cannot record, cut, label or edit audio samples (there are none), create sample-based voicebanks, or train models.

## Project file format (`.mvp`)

UTF-8 JSON with `format` = `"melovio-project"` and `format_version` = `1`. Newer major versions are refused with a message; unknown fields are ignored on load. Writes are atomic (temp file + rename).

```jsonc
{
  "format": "melovio-project", "format_version": 1, "app_version": "0.1.0",
  "meta": { "title": "", "composer": "", "lyricist": "", "comment": "", "created": "ISO-8601 UTC", "modified": "ISO-8601 UTC" },
  "settings": { "ppq": 480, "sample_rate": 44100 },
  "tempo_map": [ { "tick": 0, "bpm": 72.0 } ],                       // first entry at tick 0
  "time_signatures": [ { "bar": 0, "numerator": 4, "denominator": 4 } ],  // bar is 0-based
  "loop": { "enabled": false, "start": 0, "end": 0 },                 // ticks
  "voicebanks": [ { "id": "yune", "version": "1.0.0", "name": "Yune", "path_hint": "../voices/yune" } ],  // path_hint optional, RELATIVE
  "next_note_id": 16,
  "tracks": [ {
    "id": "trk_...", "name": "Lead", "voicebank": "yune", "persona": "", "persona_amount": 1.0,
    "style": { "brightness": 0.2 },                                    // track-wide overrides
    "natural": 0.0, "natural_take": 1, "auto_expression": false,
    "gain_db": 0.0, "pan": 0.0, "mute": false, "solo": false,
    "lanes": [ { "param": "pitch", "points": [ { "tick": 0, "value": 0.0 } ] } ],
    "notes": [ { "id": 1, "tick": 0, "length": 460, "pitch": 69, "lyric": "さ", "phonemes": "", "velocity": 0.75, "offset_ms": 0.0, "params": {} } ]
  } ]
}
```

- **Resources**: the only external references are voicebank path hints, stored relative to the project file. Absolute paths found in a file are dropped with a warning (`project.absolute_path_ignored`) and are never written. Missing voicebanks are detected on open and shown in a dialog with *Locate*.
- **Validation**: strict; errors name the offending field (e.g. `tracks[0].notes[3].pitch`). Some out-of-range values (velocity, pan, ...) are clamped silently; unknown parameters/lanes are dropped with a warning.
- **Autosave** files wrap the same JSON in `{ "original_path", "modified", "project" }` and live outside the project directory.
- `phonemes`, when non-empty, replaces the lyric as the sung pronunciation (Wauvio phoneme notation, e.g. `{k a}`).

## Synthesis pipeline

1. **Resolve voicebanks.** Each track's voicebank id/version is mapped to an installed, usable package (exact version, other version, project path hint, or Missing/Unusable).
2. **Phrase splitting.** A track is cut into phrases wherever the gap between notes is ≥ 1.0 s. Phrases are independent, so they render in parallel and are cached.
3. **Pronunciation.** For each note: the manual override, else the voicebank's `phonetic_mappings` entry for the lyric, else the lyric itself. The text is analysed with the engine's own lyric parser (kana, romaji, `{phoneme notation}`). Kanji is an *error* unless the user dictionary supplies a reading; it is never guessed. Unrepresentable kana are flagged (see [Known limitations](#known-limitations)).
4. **Plan.** Ticks are converted to seconds through the tempo map; per-note `offset_ms` is applied; durations are clipped so the voice stays monophonic; per-note and per-track style overrides are clamped to the allowed ranges; automation lanes are sampled into second-based curves (only non-zero lanes). A 0.15 s lead-in lets consonants start before the vowel onset.
5. **Cache lookup.** The phrase cache (256 MB default) is keyed on everything that affects audio plus the `.voice` file contents. A hit reuses the samples.
6. **Engine render.** A Wauvio `Singer` is built (voicebank profile → persona → track style → notes → lanes; `natural`/`auto_expression` if enabled) and renders mono floats at the target sample rate. Output is deterministic for identical input.
7. **Mix.** Phrases are placed on the timeline, mute/solo applied, then track gain and equal-power balance pan, summed to stereo.
8. **Output.** *Play* builds an immutable buffer that the audio callback streams. *Export* optionally normalizes, writes 16/24-bit PCM with TPDF dither or 32-bit float, mono sum or stereo, to `<file>.part` and renames it (a cancelled or failed export leaves no file). Clipping is reported.

Progress is reported per phrase; cancelling aborts between phrases.

## Architecture

```
            +-----------------------------+      +------------------+
            | melovio-studio (src/app)    |      | melovio-cli      |
            | ImGui panels, App state     |      | src/cli          |
            +--------------+--------------+      +---------+--------+
                           |                               |
            +--------------v--------------+                |
            | melovio_audio_sdl           |                |
            | SDL2 IAudioOutput           |                |
            +--------------+--------------+                |
                           |                               |
            +--------------v-------------------------------v--------+
            | melovio_core (src/core)                                |
            | util  timeline  params  project  project_io  history   |
            | pronunciation  voicebank  voicebank_library  config    |
            | synth_plan  renderer  wav_writer  export  transport    |
            +--------------+-----------------------------------------+
                           | (only engine_bridge.cpp, pronunciation.cpp, vb_validate.cpp)
            +--------------v--------------+
            | Wauvio-Vocal v2 (header-only, third_party/wauvio-cpp, UNMODIFIED)
            +-----------------------------+
```

### Principles
- **Core has no GUI and no device.** Everything testable is in `melovio_core`; audio goes through `IAudioOutput` (SDL in the app, `ManualAudioOutput` in tests).
- **The engine is behind a narrow bridge.** The project model never contains engine types. `pronunciation.cpp` and `vb_validate.cpp` also include Wauvio because the pronunciation editor must show exactly what the engine's own parser will sing, and deep validation must call the real engine.
- **Nothing is faked.** Nothing is pre-rendered and nothing pretends to be a recording. Features the engine lacks (sample voicebanks, polyphony per voice, parameters it has no channel for) are absent or rejected with a message.
- **Relative paths only** in projects. Machine-specific paths live in the settings file, never in a project or voicebank.

### Threads

| Thread | Does | Must not |
|---|---|---|
| UI | ImGui, editing, project mutation | wait for synthesis |
| Player worker | `Renderer::render_mix` for play/preview (parallel phrase threads inside) | touch UI state (works on a *copy* of the project) |
| Audio callback | `Transport::fill`: reads an immutable `MixBuffer` through atomics | allocate, lock, call the engine, log |
| Export worker | `export_wav` on a copy of the project, with progress + `CancelToken` | |
| Workshop jobs | test render, deep validation via `std::async` | |

The `MixBuffer` swap is lock-free. Position, loop and state are atomics. Playback is a read of pre-rendered audio; editing during playback marks the player dirty and the next Play re-renders (cached phrases are reused).

### Data flow
`Project` (ticks, lyrics, params) → `preflight` / `build_phrase_plan` (seconds, engine lyrics, style, lanes) → `render_plan` (mono floats per phrase, cached) → mix (mute/solo, gain, pan) → `MixBuffer` (play) or WAV encoder (export).

### Undo/redo
History stores whole-project snapshots with an optional coalesce key (continuous drags and slider edits merge into one step). Simple and robust; memory grows with project size × history depth (capped).

### Autosave and recovery
Autosave writes `<config>/autosave/<key>.autosave.json` (never the project file). It is discarded on a clean save/exit. At start-up leftovers are offered for recovery; corrupt autosaves are ignored; an autosave older than the saved project file is deleted instead of offered.

### Dependencies

| Dependency | Version | Why |
|---|---|---|
| Wauvio-CPP | vendored snapshot | The synthesis backend. Header-only. |
| nlohmann/json | 3.11.3 | Project, voicebank and settings JSON with precise error messages; header-only |
| miniz | 3.0.2 | `.mvb` voicebank packages (zip) with zip-slip checks; single small C library |
| Dear ImGui (docking) | 1.91.5 | Immediate-mode UI suited to a custom piano roll; docking gives a pro layout |
| SDL2 | 2.30.9 (system or built from source) | Window, GL context, input and audio device on all desktop platforms |
| doctest | 2.4.11 | Tests only; header-only |

No native file-dialog library is used: a built-in ImGui file dialog keeps dependencies down and looks the same on all platforms (but is less polished than a native one).

## Using `melovio_core` from C++

Link `melovio_core` (public include dir `src/`, headers under `core/`). Everything is in `namespace melovio`; fallible functions return `Status` or `Result<T>` (`.ok()`, `.error`) and never throw across the API.

```cpp
#include "core/project_io.hpp"
#include "core/voicebank_library.hpp"
#include "core/renderer.hpp"
#include "core/export.hpp"

using namespace melovio;
VoicebankLibrary lib; setup_default_library(lib, exe_dir);          // bundled + user + env dirs
auto pr = load_project("song.mvp"); if (!pr) { /* pr.error */ }
auto res = lib.resolve(*pr, "."); auto banks = snapshot_for_project(res);
Renderer r(banks);                                                   // optional PhraseCache / PronunciationEngine
ExportSettings s; s.sample_rate = 48000; s.bit_depth = 24; s.channels = 2;
auto out = export_wav(*pr, r, s, "song.wav", [](double f, const std::string&) {}, nullptr);
```

| Header | Main types / functions |
|---|---|
| `util.hpp` | `Status`, `Result<T>`, `Diagnostic{severity, code, message}`, atomic write, `compare_versions` |
| `timeline.hpp` | `TempoMap` (tick↔seconds, bar/beat, signature changes), `GridDivision`, `snap_to_grid` |
| `params.hpp` | `ParamDef` registry of the supported parameters (`param_registry()`, `find_param`) |
| `project.hpp` | `Project`, `Track`, `Note`, `Lane`, `VoicebankRef`, `validate_project` |
| `project_io.hpp` | `save_project`, `load_project`, `project_to_json/from_json`, `AutosaveStore` |
| `history.hpp` | `History::record(project, label, coalesce_key)`, `undo`, `redo` |
| `pronunciation.hpp` | `PronunciationEngine::analyze/analyze_phonemes/analyze_note`, `UserDictionary`, `IReadingProvider`, `validate_phoneme_text` |
| `voicebank.hpp` | `VoicebankManifest`, `load_voicebank`, `validate_voicebank`, `check_completeness` |
| `voicebank_library.hpp` | `VoicebankLibrary` (refresh/find/install/remove/export_package/resolve/locate), `unzip_package`, `zip_directory` |
| `synth_plan.hpp` | `split_phrases`, `build_phrase_plan`, `preflight` |
| `renderer.hpp` | `Renderer::render_track/render_mix`, `PhraseCache`, `CancelToken`, `StereoAudio` |
| `export.hpp`, `wav_writer.hpp` | `export_wav`, `ExportSettings`, WAV encode/decode (16/24 PCM, 32 float) |
| `transport.hpp`, `audio_output.hpp` | `Transport` (RT-safe `fill`), `Player` (async render + playback), `IAudioOutput`, `ManualAudioOutput` |
| `config.hpp` | `AppConfig`, `load_config_or_default`, `save_config` |
| `engine_bridge.hpp` | `ProfileParams`, `read_profile/write_profile`, `render_plan`, `diagnose_plan`, `engine_info` |

**Threading:** `Renderer` and `export_wav` are re-entrant and take project copies/const refs; `Transport::fill` is the only real-time entry point; `Player` owns its worker thread.

The API is not yet frozen (0.x): expect changes before 1.0.

## Development

### Layout
```
src/core/   melovio_core (engine-free except engine_bridge.cpp, pronunciation.cpp, vb_validate.cpp)
src/audio/  SDL2 IAudioOutput
src/app/    Melovio Studio (ImGui panels, one file each)
src/cli/    melovio-cli
tests/      doctest; tests/gui/ holds the SDL audio device test
voicebanks/ bundled voicebank packages
third_party/wauvio-cpp/  untouched engine snapshot
```

### CLI
`melovio-cli` provides: voicebanks, validate, install, package, validate-voicebank, render, analyze, demo.

### GUI automation (smoke testing)
`melovio-studio` has flags for checking the UI without a human, e.g. under Xvfb:

```
--frames N  --screenshot out.bmp  --size WxH  --focus "Window title"  --select-note N  --select-all  --show-workshop  --show-log
--script file     one command per line: click X Y | dblclick X Y | rclick X Y | drag X1 Y1 X2 Y2 | move X Y | key [ctrl+|shift+]NAME | text STR | wait N | shot FILE.bmp
```

Automated runs neither read nor write the user's layout file. `MELOVIO_DEBUG_INPUT=1` prints the mouse state per frame. This is a development aid, not a replacement for unit tests.

## Features and status

Status of version 0.1.0.

### Implemented
- **Projects**: new, open, save, save as, recent files, unsaved-change prompts, metadata, relative resource paths, missing-voicebank detection + locate, autosave to a separate folder, crash recovery, atomic writes, strict loading with field-named errors.
- **Timeline**: tempo changes, time-signature changes, bars/beats, triplet grids, quantize/snap.
- **Piano roll**: create, delete, move, resize (both edges), multi-select/rubber band, copy/cut/paste, quantize, snap, transposition, nudge, playhead, loop region, zoom/scroll, undo/redo.
- **Pronunciation**: lyric input (kana/romaji/phoneme notation), phoneme + syllable visualization, manual override with validation, per-syllable timing, particle helper, bulk lyric assignment, user reading dictionary, unrepresentable-kana flags.
- **Parameter lanes**: the 12 expression channels the engine really has; add/move/delete points.
- **Voicebanks**: browse, import (dir/.mvb), export .mvb, remove, inspect, validate, versions side by side, compatibility with project range, locate missing, preview.
- **Voicebank workshop**: copy-to-edit, metadata, voice-model editing via the engine's profile format, vowel chart, test render with waveform, deep completeness check.
- **Voices**: Yune, Koharu, Reiha and Itsuki as parametric profiles generated from the engine.
- **Synthesis**: phrase-parallel real-engine rendering, cache, cancel, progress, deterministic output, mute/solo/gain/pan mix.
- **Playback**: non-blocking render, real-time-safe transport, loop, seek, preview.
- **Export**: WAV 16/24/32f, 22.05–96 kHz, mono/stereo, project/region/track, normalize, dither, progress, cancel, no partial files, clipping report.
- **UI**: docking dark theme, menus, shortcuts, Japanese font discovery, toasts, log/diagnostics.

### Not implemented
- Sample-based voicebanks and anything needing recordings (engine limitation; rejected, not faked).
- Polyphony within one track (the voice is monophonic; use several tracks).
- MIDI / MusicXML / UST / VSQ import, MIDI export, stems export, formats other than WAV.
- Velocity/dynamics lane in the piano roll (velocity is edited in Note Properties); no tempo track editor in the roll (tempo changes live in Project settings; the BPM box edits the tempo at the playhead).
- Native file dialogs, native menus, drag-and-drop of files.
- Plugin (VST/CLAP) or DAW integration.
- Languages other than Japanese.
- Kanji-to-reading conversion (needs a dictionary; Melovio will not guess).
- Command-based undo (whole-project snapshots are used instead).
- Windows/macOS builds (not built or tested).
- Editing audio while it plays: edits apply at the next Play.

### Known bugs / rough edges
- Voicebank *Preview* and workshop *Play* give the player a voicebank set that includes the previewed voicebank; the next Play/preview restores the project's own set.
- "Reset layout" needs an application restart.
- The built-in file dialog is basic (no bookmarks, thumbnails or type-ahead).
- Audio was only tested through SDL's dummy driver; latency and underrun behaviour on real hardware is untested.
- The GUI has not been run on a physical display or a HiDPI screen; HiDPI font scaling follows the SDL drawable size and is untested.
- Kana text depends on a system CJK font; without one kana render as boxes (a warning is shown in the transport bar).

## Known limitations

### Synthesis
- **Parametric voices only.** Wauvio-Vocal generates sound from formant/glottal models; the voices sound synthetic, not like recorded singers. This is inherent to the backend.
- **Unrepresentable kana.** The engine's phoneme notation cannot express てぃ とぅ でぃ どぅ しぇ じぇ ちぇ くぁ as written. Melovio flags them (`pronunciation.no_notation`) instead of rendering a silent or wrong sound; write an explicit phoneme override or use a nearby kana.
- **Monophonic voice per track.**
- **Kanji** needs a reading (kana or user dictionary).
- **Range.** Notes outside a voice's hard range are warned about and may sound wrong; comfortable-range violations are warned too.
- Very long text on one short note is flagged (too many morae) rather than compressed silently.

### Application
- Every Play after an edit re-renders. Unchanged phrases come from the cache, so this is usually quick; a change that affects every phrase, such as a tempo change, re-renders everything.
- Undo uses whole-project snapshots.
- Only verified on Linux.
- No audio recording, audio import, time-stretching or effects (reverb/EQ): the output is the dry synthesised voice.

### Data safety
Melovio never overwrites a project except through Save, never writes machine-specific paths into projects or voicebanks, writes files atomically, and refuses to replace an existing exported WAV or imported voicebank version without confirmation.