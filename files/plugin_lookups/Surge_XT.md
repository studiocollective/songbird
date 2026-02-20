# Surge XT Automation Lookup

This file maps the standardized "Songbird Macros" to the conceptual parameters found in Surge XT. Use these macros when generating `.bird` files.

## Supported Plugins
- Surge XT

## Standard Macros -> Plugin Parameters

### Tone / Timbre
* **`brightness`**: 
  * Surge XT: Global Filter Cutoff.
  * Surge XT: Modulator Output Level.
* **`resonance`**: 
  * Surge XT: Global Filter Resonance.
* **`drive`**: 
  * Surge XT: Global Distortion / Saturation amount.
  * Surge XT: Oscillator Drive.

### Dynamics / Envelopes
* **`attack`**: Maps to the Global Amp Envelope Attack Time.
* **`decay`**: Maps to the Global Amp Envelope Decay Time.
* **`sustain`**: Maps to the Global Amp Envelope Sustain Level.
* **`release`**: Maps to the Global Amp Envelope Release Time.

### Filter Envelopes
* **`env_depth`**: Maps to the Global Filter Envelope Amount.
* **`filter_attack`**: Maps to the Global Filter Envelope Attack Time.
* **`filter_decay`**: Maps to the Global Filter Envelope Decay Time.

### Modulation / LFOs
* **`mod_rate`**: Maps to the Global LFO Rate.
* **`mod_depth`**: Maps to the Global LFO Depth.
* **`vibrato`**: Maps to LFO to Pitch amount.

### Voice Management
* **`unison_voices`**: Maps to unison voice count.
* **`detune`**: Maps to unison detune amount.
* **`width`**: Maps to the stereo width parameter.
