# Custom Font System — Implementation Summary

**Project:** CrossPoint Reader (ESP32-C3 e-ink firmware)
**Date:** 2026-04-20
**Scope:** Replace hardcoded OpenDyslexic font with a user-uploadable custom font system

---

## Background & Motivation

The CrossPoint Reader previously shipped with four built-in font families baked into flash:
- Bookerly (regular, bold, italic, bold-italic × 4 sizes = 16 variants)
- NotoSans (same)
- OpenDyslexic (same)

OpenDyslexic was hardcoded and occupied ~180 KB of flash rodata. Users had no way to substitute their own font. The goal was to:

1. Remove OpenDyslexic (flash savings)
2. Build the infrastructure for arbitrary user-provided fonts
3. Provide a web UI to install/activate fonts from a browser
4. Extend the font conversion script to produce the binary format the firmware expects

---

## Thought Train & Design Decisions

### Constraint: ESP32-C3 memory is tight

The ESP32-C3 has ~400 KB SRAM, of which ~240 KB is typically free at runtime. A full custom font (all sizes, all styles) in uncompressed heap would not fit. This drove the core architectural decision:

> **Metrics in heap, bitmaps streamed from SPIFFS.**

Only the font metrics (glyph dimensions, kerning tables, ligature pairs) are loaded into heap — typically 10–30 KB per style. Glyph bitmaps stay compressed in SPIFFS and are decompressed on demand by the existing `FontDecompressor`, one group at a time into a static BSS buffer (`_hotGroupBuf[51200]`).

### Why SPIFFS and not SD card directly?

SD card files are on a FAT filesystem with a shared SPI bus. SPIFFS is an internal SPI flash partition (NOR flash) that is memory-mapped on ESP32 — reads are faster, more reliable, and don't compete with display SPI traffic. The tradeoff: SPIFFS is small (~3.375 MB partition), so only **one** active custom font slot is supported at a time.

### Atomic install: font.json written last

If power is lost mid-copy, a partial SPIFFS font slot must not be silently used. The solution: `font.json` is the last file written during SD → SPIFFS copy. At boot, the firmware checks for `font.json` first. If absent → no custom font, fall back to Bookerly. No rename API is needed; the absence of the JSON is the "incomplete" signal.

### SPIFFS ownership marker

`SPIFFS.begin(formatOnFail=true)` only formats on a mount failure, not if the partition contains valid files from foreign firmware (e.g. the OEM Xteink firmware). To prevent using stale foreign content, a marker file `/.crosspoint_spiffs` is written on first use. On every mount, its absence triggers a format + rewrite. This is implemented in `HalSpiffs`.

### IBitmapSource abstraction

Previously, `FontDecompressor` assumed bitmaps lived in a `const uint8_t*` in flash. To support SPIFFS-streamed bitmaps without changing the public API, a thin abstract interface was added:

```cpp
class IBitmapSource {
  virtual bool read(uint32_t offset, uint8_t* dst, uint32_t length) = 0;
};
```

Two implementations:
- `FlashBitmapSource` — wraps a `const uint8_t*`, zero-copy `memcpy`
- `SpiffsBitmapSource` — holds an open `fs::File` handle and a `bitmapOffset`; `read()` seeks and reads

`FontDecompressor::decompressGroup()` checks `fontData->bitmapSource`: if non-null, delegates to it; otherwise uses the old flash pointer path. Built-in fonts are unaffected — their `bitmapSource` is null.

### CRC32 font ID

The GfxRenderer needs a stable integer ID to identify each font family. Rather than require users to assign one, the system computes a CRC32 over all `.epdfont` file contents for the family and uses that as the ID. This is recomputed after every upload, so incrementally uploading Bold after Regular produces a correct final ID.

### Lazy style loading

Bold/italic styles are not loaded at boot — only Regular is required. `ensureStyle(style)` loads a style on first request. A heap guard of 80 KB is enforced before each `malloc`, so the system degrades gracefully rather than crashing if memory is low.

---

## What Was Built

### Phase 0 — FontDecompressor static buffers

**Before:** `_hotGroupBuf` and `_hotGlyphBuf` were `std::vector` members, heap-allocated per instance.

**After:** Replaced with static BSS arrays (`static uint8_t _hotGroupBuf[51200]`). This eliminated per-instance heap fragmentation and made the buffer lifetime predictable.

---

### Phase 1 — Core Infrastructure

#### 1.1 — Remove OpenDyslexic

- Deleted 16 header files (`lib/EpdFont/builtinFonts/opendyslexic_*.h`)
- Removed their `#include` from `all.h`
- Removed 20 lines of globals and 4 `renderer.insertFont()` calls from `main.cpp`
- Renamed `STR_OPEN_DYSLEXIC` → `STR_CUSTOM_FONT` in `I18nKeys.h` and all 23 translation YAML files
- Updated English value to `"Custom Font"`

**Flash savings:** ~180 KB rodata freed.

#### 1.2 — IBitmapSource + SpiffsBitmapSource

New file: `lib/EpdFont/IBitmapSource.h`

```
IBitmapSource (abstract)
├── FlashBitmapSource  — const uint8_t*, memcpy
└── SpiffsBitmapSource — fs::File + uint32_t bitmapOffset, seek+read
```

`EpdFontData` gained a new field: `IBitmapSource* bitmapSource = nullptr;`

#### 1.3 — FontDecompressor dispatch

`FontDecompressor::decompressGroup()` now checks `fontData->bitmapSource`. If set, it reads compressed group data into `_compressedStagingBuf` via the source interface. Built-in fonts continue to work unchanged via the null path.

#### 1.4 — HalSpiffs

New files: `lib/hal/HalSpiffs.h` / `HalSpiffs.cpp`

- `init(maxOpenFiles)` — mounts SPIFFS, detects foreign content via marker file, formats if needed
- `ready()` — simple flag
- `clearFontSlot()` — removes all files in `/font/` and `/font/font.json`

#### 1.5 — CrossPointSettings changes

`CrossPointSettings.h`:
- `enum FONT_FAMILY { BOOKERLY=0, NOTOSANS=1, CUSTOM=2 }`
- Added `char customFontName[64]`
- Added `int cachedCustomFontId`
- Added `uint8_t getReaderFontPt()` (maps fontSize enum → point size)

`CrossPointSettings.cpp`:
- `getReaderFontId()` CUSTOM case returns `cachedCustomFontId`
- `getReaderLineCompression()` OPENDYSLEXIC case replaced with CUSTOM

`SettingsList.h`:
- Added `customFontName` as a JSON-persisted string setting

#### 1.6 — CustomFontLoader

New files: `src/fonts/CustomFontLoader.h` / `CustomFontLoader.cpp`

**EpdFontHeader** (48 bytes, packed):
```
magic[4]  ver[1]  flags[1]  glyphCount[2]  groupCount[4]  intervalCount[4]
kernLeftCount[4]  kernRightCount[4]  kernMatrixSize[4]  ligatureCount[4]
advanceY[1]  reserved[1]  ascender[2]  descender[2]
kernLeftClassCount[2]  kernRightClassCount[2]  bitmapSize[4]  reserved[2]
= 48 bytes total
```

**flags bitmap:**
- `0x01` — 2-bit grayscale
- `0x02` — kerning present
- `0x04` — ligatures present

**Key methods:**
- `discoverFamilies()` — scans SD `/fonts/` for subdirs containing `font.json`
- `init(name, pt)` — resolves nearest available size, loads Regular style
- `ensureStyle(style)` — lazy-loads Bold/Italic/BoldItalic on demand
- `copyFamilyToSpiffs(name)` — atomic SD → SPIFFS copy (font.json last)
- `registerWithRenderer(renderer)` — calls `renderer.insertFont(fontId, family)`
- `release()` — frees all heap allocations

**Struct size corrections discovered during implementation:**

| Struct | Expected | Actual | Reason |
|--------|----------|--------|--------|
| `EpdGlyph` | 14 bytes | **16 bytes** | `uint32_t dataOffset` at offset 10 → padded to 12 for 4-byte alignment |
| `EpdFontGroup` | 18 bytes | **20 bytes** | `uint32_t firstGlyphIndex` after `uint16_t glyphCount` → 2 bytes padding |

These are not `__attribute__((packed))` structs. The Python binary writer had to be corrected to insert explicit zero-padding bytes to match the C compiler layout.

#### 1.7 — Boot wiring in main.cpp

```cpp
HalSpiffs::init(8);  // before settings load

// After setupDisplayAndFonts():
if (HalSpiffs::ready()
    && SETTINGS.fontFamily == CrossPointSettings::CUSTOM_FONT
    && SETTINGS.customFontName[0] != '\0') {
  const uint8_t pt = SETTINGS.getReaderFontPt();
  if (customFontLoader.init(SETTINGS.customFontName, pt)) {
    SETTINGS.cachedCustomFontId = customFontLoader.fontId();
    customFontLoader.registerWithRenderer(renderer);
  } else {
    // Graceful fallback: revert to Bookerly and save
    SETTINGS.fontFamily = CrossPointSettings::BOOKERLY;
    SETTINGS.saveToFile();
  }
}
```

---

### Phase 2 — Web UI Font Manager

#### New HTML page: FontManagerPage.html

Served at `/fonts`. Compressed to PROGMEM via the existing `build_html.py` pre-build script → `FontManagerPageHtml.generated.h`.

**UI layout:**

```
Font Manager
├── [Nav: Home | Files | Settings]
├── [Active banner — green if custom font active, grey if built-in]
├── Installed Fonts card
│   └── Per-family card:
│       ├── Name + [Active badge | Activate button] + [Delete button]
│       └── Style grid:
│             Size | R  | B  | I  | BI
│             ─────┼────┼────┼────┼────
│              12  | ✓  | ✓  | ✗  | ✗
│              14  | ✓  | ✓  | ✓  | ✓
│              16  | ✓  | ✗  | ✗  | ✗
│              18  | ✗  | ✗  | ✗  | ✗
├── Upload Font File card
│   ├── Font name input
│   ├── Size selector (12 / 14 / 16 / 18)
│   ├── Style selector (Regular / Bold / Italic / Bold Italic)
│   ├── .epdfont file picker
│   └── [Upload button] + status message
└── How to convert a TTF font (collapsible accordion)
    └── fontconvert.py command examples
```

**Dark mode:** Full support via CSS `prefers-color-scheme: dark`.

**JavaScript (vanilla, no framework):**
- `loadFonts()` → `GET /api/fonts` → renders family cards
- `doUpload()` → `POST /api/fonts/upload` (multipart form)
- `doActivate(name)` → `POST /api/fonts/activate?name=X` (triggers device restart)
- `doDelete(name)` → `DELETE /api/fonts?name=X`

#### New API routes in CrossPointWebServer

| Method | Route | Handler | Description |
|--------|-------|---------|-------------|
| GET | `/fonts` | `handleFontManagerPage()` | Serve compressed HTML |
| GET | `/api/fonts` | `handleGetFonts()` | JSON array of font families + style grids |
| POST | `/api/fonts/upload` | `handleFontUpload()` + `handleFontUploadPost()` | Upload one `.epdfont` variant |
| POST | `/api/fonts/activate?name=X` | `handleFontActivate()` | Copy SD→SPIFFS, save settings, restart |
| DELETE | `/api/fonts?name=X` | `handleFontDelete()` | Delete family from SD (refuses if active) |

**handleGetFonts()** scans `SD:/fonts/` for family directories, then checks which `<name>_<size>_<style>.epdfont` files exist per family, and returns:
```json
[
  {
    "name": "MyFont",
    "active": true,
    "styles": {
      "14": ["R", "B", "I", "BI"],
      "16": ["R"]
    }
  }
]
```

**handleFontUpload()** validates the EPDF magic bytes on the first upload chunk before writing to SD, rejecting non-font files early.

**handleFontUploadPost()** recomputes the CRC32 `fontId` over all `.epdfont` files in the family directory and rewrites `font.json`:
```json
{ "fontId": 2847361920, "sizes": [14, 16] }
```

**handleFontActivate()** calls `CustomFontLoader::copyFamilyToSpiffs()`, saves `SETTINGS.fontFamily = CUSTOM` and `SETTINGS.customFontName`, then calls `ESP.restart()`.

**handleFontDelete()** returns HTTP 409 Conflict if the requested family is the currently active custom font.

---

### Phase 3 — fontconvert.py --binary mode

#### New flag: `--binary`

Requires `--compress` and `--2bit`. Outputs a binary `.epdfont` file to `sys.stdout.buffer` instead of a C header.

**Usage:**
```bash
python fontconvert.py --binary --compress --2bit MyFont-Regular.ttf     14 > MyFont_14_R.epdfont
python fontconvert.py --binary --compress --2bit MyFont-Bold.ttf        14 > MyFont_14_B.epdfont
python fontconvert.py --binary --compress --2bit MyFont-Italic.ttf      14 > MyFont_14_I.epdfont
python fontconvert.py --binary --compress --2bit MyFont-BoldItalic.ttf  14 > MyFont_14_BI.epdfont
# Repeat for other sizes (12, 16, 18) as needed.
```

#### Binary file format

```
Offset   Size   Field
──────   ────   ─────
0        4      magic: "EPDF"
4        1      version: 1
5        1      flags (0x01=2bit, 0x02=kern, 0x04=lig)
6        2      glyphCount
8        4      groupCount
12       4      intervalCount
16       4      kernLeftCount
20       4      kernRightCount
24       4      kernMatrixSize
28       4      ligatureCount
32       1      advanceY
33       1      reserved
34       2      ascender  (signed)
36       2      descender (signed)
38       2      kernLeftClassCount
40       2      kernRightClassCount
42       4      bitmapSize
46       2      reserved
──────── ──     ──────── total: 48 bytes

Metrics segment (immediately after header):
  EpdGlyph[glyphCount]             — 16 bytes each
  EpdUnicodeInterval[intervalCount] — 12 bytes each
  EpdFontGroup[groupCount]         — 20 bytes each
  EpdKernClassEntry[kernLeftCount] — 3 bytes each (packed)
  EpdKernClassEntry[kernRightCount]— 3 bytes each (packed)
  int8_t[kernMatrixSize]           — 1 byte each
  EpdLigaturePair[ligatureCount]   — 8 bytes each

Bitmap segment (immediately after metrics):
  raw DEFLATE-compressed group data, concatenated
```

#### Group size enforcement

The firmware's `FontDecompressor` has two static buffers that constrain group sizes:
- `_hotGroupBuf[51200]` — uncompressed group data
- `_compressedStagingBuf[16384]` — compressed group data

If any group exceeds either limit, `split_and_compress()` recursively bisects it until both constraints are met. This happens transparently during conversion.

#### Struct packing bugs found and fixed

During implementation, the Python `struct.pack` format strings were initially wrong:

1. **EpdGlyph**: First written as `"<BBHhhHI"` (14 bytes). `uint32_t dataOffset` is at C struct offset 10, but `uint32_t` requires 4-byte alignment, so the compiler inserts 2 bytes of padding → actual offset 12. Fixed to `"<BBHhhHHI"` (16 bytes) with explicit zero-pad `H`.

2. **EpdFontGroup**: First written as `"<IIIHHI"` without accounting for the padding between `uint16_t glyphCount` and `uint32_t firstGlyphIndex`. Fixed to `"<IIIHHI"` = 20 bytes (the pad `H` was already in the final struct, correcting an earlier 18-byte version).

3. **Header format string**: Initially had 7 `I` fields (52 bytes). `advanceY` is `uint8_t` (`B`), not `uint32_t` (`I`). Fixed to `"<4sBBHIIIIIIBBhhHHIH"` = exactly 48 bytes, verified with `assert len(header) == 48`.

---

## Pros and Cons

### Pros

- **Zero flash cost for built-in fonts** — OpenDyslexic removed, ~180 KB freed in rodata.
- **No heap cost at boot** — custom font only loaded if `SETTINGS.fontFamily == CUSTOM`. Built-in fonts remain zero-heap (flash pointers).
- **Graceful fallback** — if SPIFFS copy is corrupt/missing, firmware falls back to Bookerly and saves the setting. No crash, no brick.
- **Atomic install** — `font.json` written last means a power-cut mid-copy leaves the system in a known-good state.
- **Incremental upload** — upload individual style/size variants one at a time via browser. Each upload recomputes the fontId.
- **Ownership detection** — prevents stale OEM firmware SPIFFS content from being silently used.
- **Heap-guarded loading** — 80 KB heap guard before each style malloc prevents OOM crash.
- **Binary format is self-describing** — header contains all counts, flags, and sizes. No external manifest needed to read the file.
- **Backward compatible** — `IBitmapSource` is opt-in. All existing built-in fonts have `bitmapSource = nullptr` and continue to work via the original flash-pointer path.

### Cons

- **One active font slot** — SPIFFS space (~3.375 MB) supports only one custom font family at a time. Switching fonts requires a new SD→SPIFFS copy and a device restart.
- **Device restarts on activate** — unavoidable: the font is loaded at boot in `main.cpp`. A hot-reload would require restructuring the renderer lifecycle.
- **No lazy style auto-hook** — `ensureStyle()` is available, but GfxRenderer does not call it automatically on first bold/italic request. Styles must be pre-loaded explicitly. This is a future enhancement.
- **Only 4 sizes supported** — 12, 14, 16, 18 pt. Hardcoded in both the web UI and `copyFamilyToSpiffs()`. Adding new sizes requires code changes.
- **fontconvert.py requires Python + freetype-py + fonttools** — not a one-click tool. Users must set up a Python environment. The web UI includes instructions.
- **No font preview** — the web UI shows which style/size variants are present but cannot render a preview on the embedded device. Preview would require a separate rendering pass and image encoding.
- **CRC32 fontId is not stable across re-uploads** — if the same font file is deleted and re-uploaded, the computed CRC32 will be identical only if file contents are identical. If any glyph data differs (e.g. different conversion options), the ID changes and cached state may be invalidated.

---

## Files Changed / Created

### Deleted
- `lib/EpdFont/builtinFonts/opendyslexic_*.h` (16 files)

### Modified
| File | Change |
|------|--------|
| `lib/EpdFont/builtinFonts/all.h` | Removed 16 OpenDyslexic includes |
| `lib/EpdFont/EpdFontData.h` | Added `IBitmapSource* bitmapSource` field |
| `lib/EpdFont/FontDecompressor.cpp` | Added IBitmapSource dispatch in `decompressGroup()` |
| `lib/EpdFont/scripts/fontconvert.py` | Added `--binary` mode, group-size enforcement, `split_and_compress()` |
| `lib/I18n/I18nKeys.h` | `STR_OPEN_DYSLEXIC` → `STR_CUSTOM_FONT` |
| `lib/I18n/translations/*.yaml` (23 files) | Key renamed, EN value updated |
| `src/main.cpp` | Removed OpenDyslexic globals, added HalSpiffs init + custom font boot sequence |
| `src/CrossPointSettings.h` | Added `CUSTOM` enum, `customFontName`, `cachedCustomFontId`, `getReaderFontPt()` |
| `src/CrossPointSettings.cpp` | CUSTOM case in `getReaderFontId()` and `getReaderLineCompression()` |
| `src/fontIds.h` | Removed 4 OPENDYSLEXIC_*_FONT_ID defines |
| `src/SettingsList.h` | `STR_CUSTOM_FONT` enum entry, `customFontName` string setting |
| `src/network/CrossPointWebServer.h` | Added 6 font handler method declarations |
| `src/network/CrossPointWebServer.cpp` | Added font route registrations + ~170 lines of handler implementations |

### Created
| File | Purpose |
|------|---------|
| `lib/EpdFont/IBitmapSource.h` | Abstract bitmap source interface + FlashBitmapSource + SpiffsBitmapSource |
| `lib/hal/HalSpiffs.h` | SPIFFS lifecycle management header |
| `lib/hal/HalSpiffs.cpp` | SPIFFS mount, ownership check, font slot clearing |
| `src/fonts/CustomFontLoader.h` | EpdFontHeader struct, StyleSlot, CustomFontLoader class |
| `src/fonts/CustomFontLoader.cpp` | Font discovery, loading, SPIFFS copy, renderer registration |
| `src/network/html/FontManagerPage.html` | Web UI for font management (auto-compressed at build time) |

---

## How to Use

### Install a custom font

1. Convert TTF files to `.epdfont` binary format:
   ```bash
   pip install freetype-py fonttools

   python lib/EpdFont/scripts/fontconvert.py --binary --compress --2bit \
       MyFont-Regular.ttf 14 > MyFont_14_R.epdfont
   # Repeat for B, I, BI styles and 12/16/18 sizes as needed.
   ```

2. Open `http://<device-ip>/fonts` in a browser.

3. Enter the font name (e.g. `MyFont`), select size and style, pick the `.epdfont` file, click **Upload**. Repeat for each variant.

4. Click **Activate** next to the font name. The device will copy the font to SPIFFS and restart (~5 seconds).

5. The reader will now use your custom font. Switch back to Bookerly/NotoSans via Settings → Font Family.

### Delete a custom font

Click **Delete** in the Font Manager. This removes the font from the SD card. If it is currently active, delete is blocked — switch font family first, then delete.

---

## Future Enhancements

- **Lazy style auto-hook in GfxRenderer** — call `ensureStyle()` automatically when a bold/italic variant is first requested, rather than requiring explicit pre-loading.
- **Multiple SPIFFS slots** — partition the SPIFFS space into two slots for A/B switching without needing a restart.
- **Font preview** — render a sample string to a PNG on device and serve it via the web UI.
- **More sizes** — make the supported size list dynamic (read from `font.json` rather than hardcoded).
- **Windows/macOS GUI wrapper** — drag-and-drop tool that calls `fontconvert.py` internally.
