# Effects Automation Lookup

This file maps the standardized "Songbird Macros" to the conceptual parameters found in the default Effects plugins. 
Use these macros when generating `.bird` files, targeting the insert FX or send/return routing channels.

## Supported Plugins
- ValhallaRoom
- Tube Delay
- Widener
- Dist TUBE-CULTURE

## Effects Macros -> Plugin Parameters

### Spaces / Reverb (ValhallaRoom)
* **`space`**: Maps to Reverb Mix (0.0 to 1.0).
* **`decay`**: Maps to Decay Time.
* **`predelay`**: Maps to Pre-delay Time.
* **`size`**: Maps to Room Size / Space.
* **`high_cut`**: Maps to High Cut Frequency (rolls off high frequencies in the tail).

### Echos / Delays (Tube Delay)
* **`echo`**: Maps to Delay Mix (0.0 to 1.0).
* **`feedback`**: Maps to Feedback Amount (how many repeats).
* **`delay_time`**: Maps to Delay Time (can be milliseconds or beat-synced fractions).
* **`drive`**: Maps to Tube Drive (saturation applied to the echoes).

### Stereo Imaging (Widener)
* **`width`**: Maps to Stereo Width (0% to 200%). Positive widens, negative narrows toward mono.

### Distortion / Saturation (Dist TUBE-CULTURE)
* **`drive`**: Maps to Drive / Distortion amount.
* **`character`**: Maps to Bias/Function switch (determines the type of harmonic distortion).
* **`mix`**: Maps to parallel Mix amount (0.0 to 1.0) so the transient isn't destroyed.
