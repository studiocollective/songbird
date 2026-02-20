# Drums and Bass Automation Lookup

This file maps the standardized "Songbird Macros" to the conceptual parameters found in the default Drum and Bass plugins. Use these macros when generating `.bird` files.

## Supported Plugins
- SubLabXL (808s and Sub Bass)
- Monoment Bass (Modern Bass Synth)
- Heartbeat (Analog Drum Machine)
- Kick 3 (Kick Drum Synthesizer)

## Standard Macros -> Plugin Parameters

### Tone / Timbre
* **`brightness`**: 
  * SubLabXL / Monoment: Synth Filter Cutoff.
  * Heartbeat / Kick 3: High-frequency click/transient level or EQ High Shelf.
* **`drive`**: 
  * SubLabXL: Distortion / Saturation Amount.
  * Monoment: Dirt / Drive macro.
  * Heartbeat: Global Saturation / Master Drive.
* **`sub_level`**: (Specific to these plugins) Level of the fundamental sine/sub oscillator.

### Dynamics / Envelopes
* **`attack`**: Maps to the Amp/Transient attack time. Useful for softening a kick drum or making an 808 pluckier.
* **`decay`**: Maps to the Amp Decay time. Controls the length/tail of the drum hit or 808.
* **`pitch_decay`**: (Crucial for drums) Maps to the Pitch Envelope Decay time. Controls the "zap" or "punch" of a kick drum.
* **`pitch_depth`**: Maps to the Pitch Envelope Amount.

### Width
* **`width`**: 
  * SubLabXL: Stereo width of the synth/sample layer (bypasses the mono sub).
  * Monoment: Stereo spread macro.
