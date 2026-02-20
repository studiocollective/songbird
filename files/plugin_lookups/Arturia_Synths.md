# Arturia Synths Automation Lookup

This file maps the standardized "Songbird Macros" to the conceptual parameters found in the Arturia V Collection synths. Use these macros when generating `.bird` files to ensure cross-compatibility and predictable sound design.

## Supported Synths
- Mini V3 (Moog Minimoog)
- CS-80 V4 (Yamaha CS-80)
- Jup-8 V4 (Roland Jupiter-8)
- Jun-6 V (Roland Juno-6/60)
- Prophet-5 V (Sequential Circuits Prophet-5)
- OB-Xa V (Oberheim OB-Xa)
- DX7 V (Yamaha DX7)
- Buchla Easel V
- Augmented Strings

## Standard Macros -> Synth Parameters

### Filter / Tone
* **`brightness`**: Maps to the primary Low Pass Filter Cutoff Frequency (e.g., VCF Cutoff). On FM synths (DX7), maps to Modulator Output Level / Operator Env Level.
* **`resonance`**: Maps to Filter Resonance / Q / Emphasis.

### Amp Envelope
* **`attack`**: Maps to VCA Envelope Attack Time.
* **`release`**: Maps to VCA Envelope Release Time.

### Filter Envelope
* **`env_depth`**: Maps to Filter Envelope Amount / Contour / Mod Depth.
* **`filter_attack`**: Maps to VCF Envelope Attack Time.
* **`filter_decay`**: Maps to VCF Envelope Decay Time.

### Modulation & Character
* **`drive`**: Maps to Oscillator Drive, Filter Overdrive, or global Saturation/Voice Dispersion.
* **`mod_rate`**: Maps to global LFO Rate.
* **`mod_depth`**: Maps to LFO to Pitch (Vibrato) or LFO to Filter (Wah).
* **`noise_level`**: Maps to White/Pink Noise oscillator volume.

### Synth-Specific Exceptions
* **DX7 V**: `brightness` increases FM index (brighter tone).
* **Augmented Strings**: `brightness` maps to the Morph macro (acoustic to synthetic).
* **CS-80 V4**: `drive` maps to the Sub Oscillator mix.
* **Jun-6 V**: `mod_depth` maps to the iconic Chorus mix amount.
