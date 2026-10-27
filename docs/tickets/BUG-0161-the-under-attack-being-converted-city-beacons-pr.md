---
id: BUG-0161
title: the "under attack" / "being converted" city beacons probably start with an empty box instead of the swords or cross symbol
status: open
kind: bug
stage: Civ
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#140
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #140
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/140 (full text, discussion).

In Civilization, a city of a player that is attacked or being converted gets a blinking beacon in the overlay: "⚔ Bob's city is under attack" or "✝ Bob's city is being converted". The overlay font is Arial (D3DX font from the game's d3dx9_27.dll). Arial does not contain U+2694 (crossed swords) or U+271D (Latin cross); both are outside the WGL4 set Arial covers. D3DX draws glyphs from the one font it was created with, so the symbol most likely appears as a missing-glyph box. The other overlay symbols (►, ◄, ▲, ▼, ■, □, ○, •) are in Arial.

### Where
- Beacon text: [civ.cpp#L618-L620](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L618-L620)
- Overlay font: [d3d9hook.cpp#L50-L63](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/d3d9hook.cpp#L50-L63)
- Drawn by `draw_marks`: [coop.cpp#L1235-L1268](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1235-L1268)

### How to reproduce
1. Civilization stage with two players. An NPC (or `coop.civ` debug command) attacks the guest's city.
2. Look at the beacon over the city, or take `sk shot`. The first character is a box.

### Expected
A symbol that the overlay font can draw, or no symbol.

## Root cause
**What happens**

The beacon reads "[box] Bob's city is under attack" on players' screens (Windows with the native d3dx9_27, and Wine's builtin d3dx9 with Liberation Sans as Arial). The meaning survives, but the symbol was the point of the prefix.

### Confidence
PLAUSIBLE, low severity. The two code points are not in Arial (WGL4). A second reviewer verified that, and narrowed the issue to these two glyphs. Not verified on a screenshot. Whether D3DX falls back to another font for CJK player names is also unverified and not part of this issue.

## Fix
Not fixed yet. Suggested in the review:

Use glyphs Arial has, for example "► " for attack and "+ " (or "† ", U+2020, in WGL4) for conversion. Alternatively, create the overlay font with a face that has them (Segoe UI Symbol) when it exists.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

Unit-style check in tools (Python, fontTools): every non-ASCII code point used in overlay strings in mods/coop/*.cpp is present in the Arial cmap (Liberation Sans in the lab). Ship it as a lint in `make test`. FAILS on main (U+2694, U+271D missing), PASSES with the fix.

### Related
None of the existing issues.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #140.
- 2026-10-27: ticket opened from the issue.
