# Songbird-Chirp: Agent Reference

## What This Project Is

Songbird is a **C++ music sequencer and composition platform** that generates MIDI output to control hardware synthesizers and DAWs (primarily Ableton Live). It has a custom musical notation called **Bird** (`.bird` files) — think "Markdown for music" — designed for fast keyboard-driven electronic music composition.

The platform runs in two modes:
- **macOS (local)**: Compiles as a native binary, creates virtual MIDI ports via **RtMIDI** that appear in DAWs like Ableton
- **Arduino (FeatherS2)**: Compiles as an `.ino` sketch, sends MIDI over hardware serial to connected synths

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│  Entry Points                                           │
│  songbird.cpp (macOS)  |  songbird-chirp.ino (Arduino)  │
└──────────────────┬──────────────────────────────────────┘
                   │
        ┌──────────▼──────────┐
        │  Program (selector) │  ← Picks which Composer to run
        └──────────┬──────────┘
                   │
        ┌──────────▼──────────┐
        │     Composer        │  ← Orchestrates sequencers + scale
        │  (Bird / Electronica│
        │   / Loop / File)    │
        └──────────┬──────────┘
                   │
   ┌───────────────▼───────────────┐
   │   Sequencers (per channel)    │
   │  Bass, Melody, Groove,        │
   │  ChordSequencer, Harmony,     │
   │  FileSequencer                │
   └───────────────┬───────────────┘
                   │
   ┌───────────────▼───────────────┐
   │   Clock (singleton)           │
   │   Transport → pulse/tick      │
   │   Internal or external sync   │
   └───────────────┬───────────────┘
                   │
   ┌───────────────▼───────────────┐
   │   MIDI I/O                    │
   │   macOS: RtMIDI virtual ports │
   │   Arduino: HardwareSerial     │
   └───────────────────────────────┘
```

## Directory Structure

```
songbird-chirp/
├── songbird.cpp              # macOS entry point
├── songbird-chirp.ino        # Arduino entry point
├── tests.cpp                 # Unit tests (macOS only)
├── files/                    # .bird composition files + .mid files
│   ├── live.bird             # Live performance composition
│   ├── full.bird             # Full arrangement example
│   └── grooves.bird          # Groove/rhythm patterns
├── libraries/
│   ├── composition/          # Composers that build sequences
│   │   ├── composer.h/cpp    # Base Composer class (holds Scale + Sequencers)
│   │   ├── composers/        # Concrete implementations:
│   │   │   ├── bird_composer  # Parses .bird files (macOS only, hot-reloads)
│   │   │   ├── electronica_composer  # Hard-coded electronica loops
│   │   │   ├── loop_composer  # Loop-based composition
│   │   │   ├── basic_composer # Simple patterns
│   │   │   └── file_composer  # MIDI file playback
│   │   ├── arrangement/      # Song sections: intro, verse, chorus, bridge, outro
│   │   ├── styles/           # Musical style presets
│   │   └── themes/           # Thematic presets
│   ├── sequencing/           # Core timing and note generation
│   │   ├── clock.h/cpp       # Singleton Clock + Transport (BPM, pulse, tick)
│   │   ├── sequencer.h/cpp   # Base Sequencer: generates Note sequences
│   │   ├── note.h/cpp        # Note struct (on/off, pitch, velocity, timing)
│   │   ├── sequencers/       # Specialized sequencers:
│   │   │   ├── bass           # Bass line patterns
│   │   │   ├── melody         # Melodic sequences
│   │   │   ├── groove         # Rhythmic/drum patterns
│   │   │   ├── chord_sequencer # Chord voicings
│   │   │   ├── harmony        # Harmony layers
│   │   │   └── file_sequencer  # MIDI file playback
│   │   └── utils/            # Swing, patterns, arpeggiator, modulator, time constants
│   ├── interface/            # I/O layer
│   │   ├── midi_io.h/cpp     # MIDI send/receive (RtMIDI on macOS, Arduino MIDI lib)
│   │   ├── program.h/cpp     # Program selector (maps index → Composer)
│   │   ├── display.h/cpp     # OLED display driver (Arduino)
│   │   ├── console.h         # Cross-platform logging
│   │   ├── buttons.h/cpp     # Button input handling (Arduino)
│   │   ├── midifile/         # MIDI file parser library
│   │   └── rtmidi/           # RtMIDI library (macOS virtual MIDI ports)
│   ├── theory/               # Music theory primitives
│   │   ├── scale.h/cpp       # Scale (root, octave, mode, voicing)
│   │   ├── chord.h/cpp       # Chord construction + inversions
│   │   ├── progression.h/cpp # Chord progressions from scale degrees
│   │   └── circle_of_fifths, repetition, rythym, song, tempo (stubs)
│   ├── voices/               # Instrument/voice definitions
│   │   ├── instrument.h/cpp  # Base Instrument (MIDI channel, note on/off)
│   │   ├── synths/           # Synth presets: Matriarch, Sub37, Mother32,
│   │   │                     #   JU-06A, Nord Wave, Subharmonicon + patch system
│   │   ├── drum_machines/    # Drum machine definitions
│   │   ├── samplers/         # Sampler definitions
│   │   └── midi/             # Generic MIDI voice definitions
│   └── effects/              # MIDI effects processing
│       ├── midi_effects.h/cpp  # Singleton MIDIEffects (routes note on/off through effects)
│       ├── effect.h/cpp      # Base Effect class
│       └── effects/          # Concrete effect implementations
└── utils/                    # Build scripts & tools
    ├── local.sh              # macOS build script (wraps clang++)
    ├── flash.sh              # Arduino compile + flash
    ├── files.sh              # Flash data files to FeatherS2 FAT partition
    ├── setup.sh              # Install Arduino CLI + dependencies
    └── partition/            # ESP32 partition table configs
```

## Bird Notation (`.bird` files)

Bird is the custom notation for composing. Key syntax:

| Token | Meaning | Example |
|-------|---------|---------|
| `b`   | Bars (loop length) | `b 1` |
| `ch`  | MIDI channel + name | `ch 3 drums` |
| `p`   | Pattern (`x`=16th, `q`=quarter, `w`=whole, `_`=rest) | `p xx _ x` |
| `sw`  | Swing (`<`=drag, `>`=rush, `-`=straight, `~`=humanize) | `sw < 20 x ~` |
| `m`   | Modulation (sin, tri, %) | `m sin 50` |
| `v`   | Velocity | `v 60 80 100` |
| `n`   | Notes (MIDI #, `-`=repeat, `+N`/`-N`=offset from root) | `n 60 +3 +5` |
| `cc`  | MIDI CC | `cc 74 50` |
| `sec` | Section name | `sec verse` |
| `arr` | Arrangement | `arr → verse 8, chorus 8` |

The `BirdComposer` (macOS only) watches `.bird` files for changes and **hot-reloads** — edits to `.bird` files are reflected in real-time MIDI output without restarting.

## How to Build & Run

### macOS (local, for DAW integration)
```bash
# Build
clang++ -std=c++11 -stdlib=libc++ -D__MACOSX_CORE__ -g \
  songbird.cpp libraries/**/*.cpp libraries/**/**/*.cpp \
  -framework CoreMIDI -framework CoreAudio -framework CoreFoundation \
  -o songbird

# Run (creates virtual MIDI port "Songbird" visible in Ableton)
./songbird
```
Or use VSCode shortcuts: `Cmd+R` to build+run, `Cmd+B` to debug.

### Arduino (FeatherS2 hardware)
```bash
# Setup
./utils/setup.sh

# Flash firmware
# VSCode: Shift+Cmd+R

# Flash .bird files to FAT partition
# Put board in bootloader mode first
# VSCode: Shift+Cmd+Alt+F
```

## Key Design Patterns

- **Conditional compilation**: `#ifdef ARDUINO` / `#else` throughout — same codebase runs on both platforms
- **Singletons**: `Clock::getInstance()` and `MIDIEffects::getInstance()` are global singletons accessed via static pointers `midiclock` and `midieffects`
- **Program selector**: `Program` class maps an integer index to a `Composer` subclass:
  - `0` = basic, `1` = MIDI file, `2` = dance loop, `3` = electronica, `4` = effects, `5` = live (bird)
- **Transport model**: Clock pulses (24 PPQ MIDI standard) → Transport ticks registered Sequencers → Sequencers emit MIDI notes
- **Hot reload**: `BirdComposer` runs a file-watching thread that re-reads `.bird` files when modified, allowing live-coding workflow

## Important Notes for Agents

1. **The macOS build is the primary development target** — it's where `.bird` files are composed and MIDI goes to Ableton via virtual ports
2. **No `send_midi_cc` implementation on macOS** — the function body is empty in the `#else` block of `midi_io.cpp`
3. **The `intialize_midi` function has a typo** — it's missing an 'i' ("intialize" not "initialize") — maintain the typo for consistency
4. **All synth presets** (Matriarch, Sub37, Mother32, JU-06A, Nord Wave, Subharmonicon) map to specific hardware synths the user owns
5. **`.bird` files in `files/`** are the user's actual compositions — treat them carefully
6. **Arrangement/sections system** (`arr`, `sec` in bird notation) is partially implemented — the infrastructure exists in `libraries/composition/arrangement/` but may not be fully wired up
7. **The test file** (`tests.cpp`) is compiled into the macOS binary via `#include` in `songbird.cpp` — it's not a separate test binary
