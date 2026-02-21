
# Channel Strips

> **Parameter names verified** via `tools/scan_plugin_params.cpp`.
> Run: `./build/ScanPluginParams_artefacts/Debug/ScanPluginParams "/path/to/Plugin.vst3"`

## Macro Reference

| Macro | Description |
|---|---|
| `comp_thresh` | Compressor threshold (or compression amount) |
| `comp_ratio` | Compressor ratio |
| `comp_attack` | Compressor attack |
| `comp_release` | Compressor release |
| `sidechain_enable` | Enable external sidechain input |
| `input_gain` | Input/pre-amp gain |
| `output_gain` | Output level |
| `eq_low` / `eq_mid` / `eq_high` | EQ band gains |
| `sidechain` | External Sidechain Source track ID (e.g. `sidechain: 1` to pump to kick) |

---

## Console 1 — Softube (v2.5.83)

**Plugin name:** `Console 1`

| Macro | Param (exact) | Idx | Notes |
|---|---|---|---|
| `comp_on` | `Compressor` | 24 | 2 steps: 0=off, 1=on |
| `comp_thresh` | `Compression` | 29 | Continuous amount |
| `comp_ratio` | `Ratio` | 25 | |
| `comp_attack` | `Attack` | 27 | |
| `comp_release` | `Release` | 28 | |
| `sidechain_enable` | `External Sidechain` | 31 | 3 steps: 0=Int, 0.5=Ext1, 1.0=Ext2 |
| `sidechain_sub` | `Ext. Sidechain to Subsystem` | 119 | 2 steps |
| `drive` | `Drive` | 32 | |
| `character` | `Character` | 33 | |

---

## Weiss DS1-MK3 — Softube (v2.6.30)

**Plugin name:** `Weiss DS1-MK3`
Two compressor bands: Band 1 = de-esser, Band 2 = wideband.

| Macro | Param (exact) | Idx |
|---|---|---|
| `comp_thresh` | `Threshold 1` | 20 |
| `comp_ratio` | `Ratio 1` | 21 |
| `comp_attack` | `Attack 1` | 14 |
| `comp_release` | `Release Fast 1` | 16 |
| `comp_thresh2` | `Threshold 2` | 32 |
| `comp_ratio2` | `Ratio 2` | 33 |
| `freq` | `Center Frequency 1` | 13 |
| `sidechain` | `Sidechain` | 37 |
| `input_gain` | `Input Gain` | 0 |
| `output_gain` | `Output Gain` | 1 |

---

## Summit Audio Grand Channel

**Plugin name:** `Summit Audio Grand Channel`

| Macro | Param (exact) | Idx |
|---|---|---|
| `comp_thresh` | `Gain Reduction` | 24 |
| `comp_gain` | `Gain` | 25 |
| `comp_attack` | `Attack` | 26 |
| `comp_release` | `Release` | 27 |
| `sidechain` | `Sidechain` | 32 |
| `comp_bypass` | `Comp Bypass` | 22 |
| `saturation` | `Saturation` | 30 |
| `low_cut` | `Low Cut` | 18 |
| `output_gain` | `Output Volume` | 20 |

---

## Pre 1973 (Neve 1073-style)

**Plugin name:** `Pre 1973` — Large parameter list (~2700 params). Use `getPluginParams` to explore.

---

## UA Century Channel Strip

**Plugin name:** `UA Century Channel`

---

## UA Manley VoxBox

**Plugin name:** `UA Manley VoxBox`
