# Plugins (`app/plugins/`)

Plugin window management, swapping, and macro parameter mapping.

## Files

| File | Purpose |
|------|---------|
| `PluginManager.cpp` | Opening/closing plugin editor windows, changing plugins on tracks. |
| `MacroMapper.cpp/.h` | Maps high-level macro knobs to specific plugin parameters (e.g., Console 1 strip). |
| `MacroMapper/` | Per-plugin macro mapping profiles (JSON definitions). |
