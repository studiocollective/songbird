# Bird File Loader (`app/loader/`)

Parsing and loading `.bird` music notation files into the Tracktion Engine.

## Files

| File | Purpose |
|------|---------|
| `BirdLoader.cpp/.h` | Core parser: `.bird` text → `BirdParseResult` → `populateEdit()` → `te::Edit`. |
| `Entitlements.plist` | macOS app sandbox entitlements. |
