# Channel Strips Automation Lookup

This file defines the standard mixer macros. The channel strip runs on every track and provides the core mixing tools (Gain, EQ, Dynamics). These operate independently of the sound design parameters on the instrument itself.

## Supported Plugins
- Console 1
- American Class A
- British Class A
- Summit Audio Grand Channel
- Weiss DS1-MK3

## Mixer / Channel Strip Macros -> Plugin Parameters

### Gain Staging
* **`input_gain`**: Maps to the Channel Input Gain (useful to drive the strip harder).
* **`output_gain`**: Maps to the Channel Output Volume (fader level).

### Filters (Pre-EQ)
* **`low_cut`**: Maps to High Pass Filter Frequency (cleans up mud).
* **`high_cut`**: Maps to Low Pass Filter Frequency (tames harshness).

### Equalizer (Tone Shaping)
* **`eq_low_gain`**: Maps to Low Band Gain.
* **`eq_low_freq`**: Maps to Low Band target frequency.
* **`eq_low_shape`**: Maps to Low Band shape (Shelf vs. Bell).
* **`eq_mid_gain`**: Maps to Mid Band Gain.
* **`eq_mid_freq`**: Maps to Mid Band target frequency.
* **`eq_mid_shape`**: Maps to Mid Band shape or Q (width of the bell).
* **`eq_high_gain`**: Maps to High Band Gain.
* **`eq_high_freq`**: Maps to High Band target frequency.
* **`eq_high_shape`**: Maps to High Band shape (Shelf vs. Bell).

### Dynamics / Compression
* **`transient_shape`**: Maps to the Channel Strip's Attack / Punch control (positive = more click, negative = softened).
* **`transient_sustain`**: Maps to the Channel Strip's Sustain control (positive = ringing, negative = tight/gated).
* **`comp_thresh`**: Maps to Compressor Threshold.
* **`comp_ratio`**: Maps to Compressor Ratio.
* **`comp_mix`**: Maps to parallel compression Mix amount.
* **`sidechain`**: Maps to the External Sidechain Source track ID (e.g., `sidechain: 1` to pump to the kick).

### Stereo Image & Saturation
* **`width`**: Maps to Stereo Width or Pan.
* **`drive`**: Maps to Saturation Amount / Drive level.
* **`character`**: Maps to Saturation Tone (changes harmonic profile).
