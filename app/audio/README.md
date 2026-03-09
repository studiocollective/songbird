# Audio Processing (`app/audio/`)

Real-time audio analysis, recording, and metering.

## Files

| File | Purpose |
|------|---------|
| `PlaybackInfo.cpp/.h` | 30Hz timer for transport position, FFT spectrum, stereo width, phase correlation. |
| `AudioRecorder.cpp/.h` | Real-time audio recording to Tracktion clips. |
| `AudioQuantizer.cpp/.h` | Audio clip quantization to beat grid. |
| `LevelMeterBridge.cpp/.h` | Per-track peak/RMS level metering pushed to WebView. |
