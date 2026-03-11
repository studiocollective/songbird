# Bird Notation

Bird is a musical notation designed for composition of modern electronic music. Think of it as Markdown for music.

## File Structure

A `.bird` file has three sections, in order:

1. **Global channel definitions** — declare channels with their instruments, effects, and channel strips
2. **Arrangement** — define the section order and bar counts
3. **Section definitions** — write the musical content for each section

```bird
# 1. Global channel definitions
ch 1 kick
  plugin kick
  strip console1

ch 2 bass
  plugin mini
  fx delay

ch 3 lead
  plugin jup8

# 2. Arrangement
arr
  intro 4
  verse 8
  chorus 8

# 3. Section definitions
sec intro
  ch 1 kick
    p x _ x _ x _ x _
    v 100 - 80 - 90 - 85 -
    n C1

sec verse
  ch 2 bass
    p q q q q
    v 90 70 80 60
    n C2 - Eb2 G2
```

---

## Tokens Reference

### Structure

| Token | Purpose | Syntax | Example |
|-------|---------|--------|---------|
| `ch` | Define a channel | `ch <number> <name>` | `ch 1 kick` |
| `plugin` | Assign instrument plugin | `plugin <keyword>` | `plugin mini` |
| `fx` | Assign insert effect | `fx <keyword>` | `fx delay` |
| `strip` | Assign channel strip | `strip <keyword>` | `strip console1` |
| `type` | Set track type | `type midi` or `type audio` | `type audio` |
| `arr` | Start arrangement block | `arr` then indented entries | see below |
| `sec` | Start section definition | `sec <name>` | `sec verse` |
| `b` | Set global bar count | `b <bars>` | `b 4` |
| `key` | Set key signature | `key <root> [mode]` | `key Eb min` |
| `cont` | Continue pattern phase across section boundary | `cont` | `cont` |

### Arrangement

The `arr` block lists sections with their bar lengths, indented:

```bird
arr
  intro 4
  verse 8
  chorus 8
  verse 8
  chorus 8
  outro 4
```

### Key Signature

Sets the musical key for the project. Supports all 12 roots with sharps/flats, major and minor:

```bird
key C            # C major
key Eb min       # Eb minor
key F# minor     # F# minor
key Bb           # Bb major
```

---

## Pattern (`p`)

Defines the rhythmic grid. Each token is one note slot. Positive = note-on, `_` = rest.

### Duration Tokens

| Token | Duration | Ticks |
|-------|----------|-------|
| `ww` | Double whole note | 768 |
| `w` | Whole note (1 bar in 4/4) | 384 |
| `qq` | Half note | 192 |
| `q` | Quarter note (1 beat) | 96 |
| `xx` | Eighth note | 48 |
| `x` | Sixteenth note | 24 |

**Dotted** (1.5× duration) — append `d`:

| Token | Duration | Ticks |
|-------|----------|-------|
| `wd` | Dotted whole | 576 |
| `qd` | Dotted quarter | 144 |
| `xxd` | Dotted eighth | 72 |
| `xd` | Dotted sixteenth | 36 |

**Triplet** (⅔× duration) — append `t`:

| Token | Duration | Ticks |
|-------|----------|-------|
| `wt` | Whole triplet | 256 |
| `qt` | Quarter triplet | 64 |
| `xxt` | Eighth triplet | 32 |
| `xt` | Sixteenth triplet | 16 |

### Rests

- `_` — rest of the same length as the previous duration
- `_w`, `_q`, `_x`, `_xx` etc. — explicit rest durations (same naming as above, prefixed with `_`)

### Timing Rule

All `p` lines in a section must sum to the same total bar length. A 4/4 bar = 384 ticks = 4 quarters = 8 eighths = 16 sixteenths. Mismatched lengths cause desync.

### Examples

```bird
p q q q q          # 4 quarter notes = 1 bar
p x x x x x x x x x x x x x x x x   # 16 sixteenths = 1 bar
p qq _ qq _        # half note, rest, half note, rest = 1 bar
p q _ q q          # quarter, rest, quarter, quarter
p qd xx q q        # dotted quarter + eighth + two quarters = 1 bar
```

---

## Velocity (`v`)

Sets the MIDI velocity (0–127) for each note slot. Cycles through values when the pattern repeats.

| Syntax | Meaning | Example |
|--------|---------|---------|
| `80` | Absolute velocity | `v 80` |
| `-` | Repeat previous velocity | `v 80 - - -` |
| `+N` / `-N` | Relative offset from previous | `v 80 +10 -20` |

Values are clamped to 0–127. The velocity list cycles independently of the pattern length.

```bird
v 100 60 80 40      # cycling 4 velocities
v 90 -               # alternating: 90, 90, 90, ...
v 80 +10 +10 -30    # 80, 90, 100, 70, 80, 90, ...
```

---

## Notes (`n`)

Specifies pitches for each pattern slot. Supports multiple formats that can be mixed.

### Formats

| Format | Example | Description |
|--------|---------|-------------|
| MIDI number | `n 60` | Middle C |
| Note name | `n C4` | Middle C (with octave) |
| Chord name | `n @Cm7` | C minor 7th chord (all notes play simultaneously) |
| Repeat | `n 60 -` | Repeat previous pitch |
| Relative offset | `n 60 +7 +5` | Add/subtract semitones from previous |
| Simultaneous notes | `n 60 64 67` | All on one line = chord (C major triad) |

### Note Names

Standard note names with optional accidentals and octave:

```
C4  D4  E4  F4  G4  A4  B4     # naturals
C#4 D#4 F#4 G#4 A#4            # sharps
Db4 Eb4 Gb4 Ab4 Bb4            # flats
```

Octave 4 starts at MIDI 60 (Middle C). Omitting octave defaults to 4.

### Chord Names

Prefix with `@`. Optionally prefix octave before root letter.

| Chord | Syntax | Notes |
|-------|--------|-------|
| Major | `@C`, `@Cmaj`, `@CM` | C E G |
| Minor | `@Cm`, `@Cmin`, `@C-` | C Eb G |
| Dominant 7th | `@C7`, `@Cdom7` | C E G Bb |
| Major 7th | `@Cmaj7`, `@CM7` | C E G B |
| Minor 7th | `@Cm7`, `@Cmin7`, `@C-7` | C Eb G Bb |
| Diminished | `@Cdim`, `@Co` | C Eb Gb |
| Diminished 7th | `@Cdim7`, `@Co7` | C Eb Gb A |
| Half-diminished | `@Cm7b5`, `@Cø` | C Eb Gb Bb |
| Augmented | `@Caug`, `@C+` | C E G# |
| Augmented 7th | `@Caug7`, `@C+7` | C E G# Bb |
| Suspended 2nd | `@Csus2` | C D G |
| Suspended 4th | `@Csus4`, `@Csus` | C F G |
| Dominant 9th | `@C9` | C E G Bb D |
| Minor 9th | `@Cm9`, `@Cmin9` | C Eb G Bb D |
| Major 9th | `@Cmaj9`, `@CM9` | C E G B D |
| Power chord | `@C5`, `@Cpower` | C G |

**Octave prefix**: `@3Cm7` = C minor 7th starting at octave 3.

### Simultaneous Notes (Chords)

Multiple plain notes on one `n` line play simultaneously as a chord:

```bird
n 41 48 53 56       # MIDI numbers = chord
n E2 C3 F3 Ab3      # note names = chord
```

### Sequential Notes

When using `-`, `+N`, or `@chords`, each token is a separate step that cycles with the pattern:

```bird
n C4 - Eb4 G4       # step 1: C4, step 2: C4, step 3: Eb4, step 4: G4
n 60 +7 +5 -12      # step 1: 60, step 2: 67, step 3: 72, step 4: 60
n @Cm7 @Fm7 @G7     # step 1: Cm7 chord, step 2: Fm7, step 3: G7
```

### Layering

Multiple velocity + note groups can share one pattern (multi-layer voicings):

```bird
p w _ w _
  v 70 90 100
    n @Cm7 @Fm7 @G7
  v 60
    n +7 +7 +8        # second layer at different velocity
  v 30
    n +12              # third layer (higher octave, soft)
```

---

## Swing (`sw`)

Adds groove by shifting every other note later. Applied per channel per section.

```bird
sw <percent> [~<humanize>]
```

| Value | Feel |
|-------|------|
| `50` | Perfectly straight (default) |
| `55` | Light swing |
| `60` | Medium swing |
| `67` | Triplet swing (classic MPC feel) |
| `75` | Heavy swing |

The optional `~N` adds random ±N ticks of jitter to **all** notes for extra humanization.

```bird
sw 60           # medium swing
sw 67 ~5        # triplet swing + ±5 tick humanize
sw 50 ~3        # straight timing + slight random jitter
```

---

## Per-Note Timing (`t`)

Fine-grained timing offsets per pattern slot. Values are in ticks (1 tick ≈ 1/96 of a beat). Cycles like velocity.

```bird
t <offset> <offset> ...
```

- Positive = late (behind the beat / laid back)
- Negative = early (ahead of the beat / pushing)
- `0` = on grid

```bird
p x  x  x  x
v 90 60 80 50
n C4 Eb4 G4 Bb4
t 0  +5 -3  0       # C4 on grid, Eb4 late, G4 early, Bb4 on grid
```

One tick = 1/24 of a sixteenth note = 1/96 of a quarter note.

---

## Automation

### Step-Based Automation

Any unrecognized keyword inside a channel block is treated as a **step automation macro**. Values cycle with the pattern.

```bird
brightness 80 40 60 90     # brightness cycles: 80, 40, 60, 90
cutoff 100 50 75 25        # cutoff cycles similarly
```

### Continuous Automation (Ramp)

Use `ramp` for time-based sweeps across a section:

```bird
brightness ramp 0.0 1.0     # sweep brightness from 0% to 100% over 1 bar
drive ramp 0.5 0.8          # subtle drive increase
```

---

## Plugin Keywords

### Instruments

| Keyword | Plugin | Type |
|---------|--------|------|
| `synths` | Pigments | General synth |
| `mini` | Mini V3 | Analog mono synth |
| `cs80` | CS-80 V4 | Poly synth |
| `prophet` | Prophet-5 V | Poly synth |
| `jup8` | Jup-8 V4 | Poly synth |
| `dx7` | DX7 V | FM synth |
| `buchla` | Buchla Easel V | West coast |
| `surge` | Surge XT | Open source synth |
| `kick` | Kick 3 | Kick drum synth |
| `drums` | Heartbeat | Drum machine |
| `bass` | Mini V3 | Bass (uses Mini V) |
| `sublab` | SubLabXL | Sub bass |
| `monoment` | Monoment Bass | Bass synth |

### Effects

| Keyword | Plugin |
|---------|--------|
| `delay` | Tube Delay |
| `valhalla` | ValhallaRoom |
| `widener` | Softube Widener |
| `soothe` | soothe2 |
| `tube` | Dist TUBE-CULTURE |

### Channel Strips

| Keyword | Plugin |
|---------|--------|
| `console1` | Console 1 |

---

## Automation Macro Names

Semantic macro names are mapped to real plugin parameter names automatically. Use these in step or ramp automation.

### Synth Macros

| Macro | Controls |
|-------|----------|
| `brightness` / `cutoff` | Filter cutoff frequency |
| `resonance` | Filter resonance |
| `attack` | Amp/filter attack time |
| `decay` | Amp/filter decay time |
| `release` | Amp/filter release time |

### Drum/Bass Macros

| Macro | Controls |
|-------|----------|
| `pitch` | Oscillator/sound pitch |
| `decay` | Sound decay time |
| `volume` | Output level |
| `drive` | Distortion/harmonics amount |

### Effect Macros

| Macro | Controls |
|-------|----------|
| `mix` | Wet/dry mix |
| `decay` | Reverb decay / delay feedback |
| `width` | Stereo width |

### Channel Strip Macros (Console 1)

| Macro | Controls |
|-------|----------|
| `input_gain` | Input gain |
| `comp_thresh` | Compressor threshold |
| `comp_ratio` | Compressor ratio |
| `eq_mid_gain` | EQ mid band gain |
| `low_cut` | High-pass filter |

---

## `cont` — Pattern Continuity

Use `cont` in a section to continue a channel's pattern phase from the previous section (rather than restarting from beat 1):

```bird
sec verse
  ch 1 arp
    cont
    p x x x x x x x x
    v 80 60 70 50
    n C4 E4 G4 C5
```

Without `cont`, each section resets the pattern/note/velocity counters to 0. With `cont`, the counters pick up where the previous section left off, creating seamless transitions.

---

## Track Types

| Type | Description |
|------|-------------|
| `midi` | MIDI track (default) — plays notes through instrument plugin |
| `audio` | Audio track — plays audio samples or Lyria-generated audio |

```bird
ch 4 pad
  type audio
```

---

## Complete Example

```bird
key Eb min
b 4

ch 1 kick
  plugin kick
  strip console1

ch 2 hihat
  plugin drums

ch 3 bass
  plugin mini
  fx delay

ch 4 chords
  plugin jup8
  strip console1

arr
  intro 4
  verse 8
  chorus 8

sec intro
  ch 1 kick
    p q _ q _
    v 100 - 80 -
    n C1

  ch 2 hihat
    sw 60
    p x x x x x x x x x x x x x x x x
    v 60 40 80 40 60 40 80 40 60 40 80 40 60 40 80 40
    n F#2

sec verse
  ch 1 kick
    p q _ q q
    v 110 - 90 100
    n C1

  ch 2 hihat
    sw 60 ~3
    p x x x x x x x x x x x x x x x x
    v 70 40 90 40 70 40 90 50 70 40 90 40 70 40 90 50
    n F#2
    t 0 +3 -2 0 +1 +4 -1 0 0 +2 -3 0 +1 +3 -2 0

  ch 3 bass
    p xx xx _ xx xx xx _ xx
    v 100 80 - 90 85 100 - 70
    n Eb2 - - Gb2 Ab2 Bb2 - Eb2
    brightness ramp 0.3 0.8

  ch 4 chords
    p w _ w _
    v 70
    n @Ebm7 - @Abm7 @Bb7

sec chorus
  ch 1 kick
    p q q q q
    v 120
    n C1

  ch 3 bass
    cont
    p xx xx xx xx xx xx _ xx
    v 110 90 100 80 110 90 - 70
    n Eb2 Gb2 Ab2 Bb2 Eb3 Bb2 - Gb2

  ch 4 chords
    p qq qq qq qq
    v 85 75 90 80
    n @Ebm7 @Abm7 @Bb7 @Gbmaj7
```

---

## Tick Reference

All timing in Bird is based on a 384-tick bar (96 ticks per beat in 4/4):

| Unit | Ticks |
|------|-------|
| Whole note (bar) | 384 |
| Half note | 192 |
| Quarter note (beat) | 96 |
| Eighth note | 48 |
| Sixteenth note | 24 |
| One `t` tick | 1 |

The `sw` and `t` timing features operate in this tick space.
