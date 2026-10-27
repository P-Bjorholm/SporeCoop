---
id: BUG-0129
title: chat in notifications is cut short for non-Latin text, styled wrongly, and cannot be read in full anywhere
status: open
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#108
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #108
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/108 (full text, discussion).

Several small problems together mean a partner's chat message often cannot be read:

1. The notification panel wraps and cuts text by **bytes**, not characters (46 bytes per line, 2 lines). A Cyrillic message (2 bytes per letter) gets about half the room of an English one. A CJK message (3 bytes per character) is cut inside a UTF-8 sequence, and the last character shows as U+FFFD.
2. The panel style is guessed from English words in the text:
   - Chat arrives as `"<Bob> text"`, which has no `": "`, so chat is **never** styled as chat. It gets the info style and 6 s instead of 8 s.
   - A chat that contains words like "lost", "failed", "joined" or "left" gets the danger, join or leave style (danger stays 10 s).
   - System lines that contain `": "` get the chat style.
   - Russian notices (Russian UI) never match any keyword.
3. The co-op window shows only the **last 4** of the 50 kept chat lines, with no scrolling. Join/leave/stage notices go into the same list, so a message is pushed out within seconds. docs/08-ui.md says "last 5 lines" and "The full history stays in the F9 window chat", which is not true.

### Where
- Wrapping by bytes, cut at byte 46 / 43: [notify.cpp#L59-L77](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/notify.cpp#L59-L77)
- Style from keywords: [notify.cpp#L35-L52](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/notify.cpp#L35-L52)
- Chat text without ": ": [coop.cpp#L1725-L1728](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1725-L1728), into [feed, coop.cpp#L118-L125](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L118-L125)
- 4 lines of 50, no scroll: [coop_ui.cpp#L624-L627](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L624-L627), [coop.cpp#L137-L142](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L137-L142)
- Docs: [docs/08-ui.md#L22](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/08-ui.md#L22), [docs/08-ui.md#L102](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/08-ui.md#L102)

### How to reproduce
- Host and guest are connected. The guest sends a Russian chat line of about 60 letters (or a Chinese line of 30 characters).
- On the host the panel shows only the first part, and the CJK case ends in a replacement character. `coop.notify` (notify::status) prints the text. The panel uses the info style, not chat.
- Then let 4 notices arrive (a third player joins and leaves, a stage change): the message is no longer in the F9 window.

### Expected
- Wrap by characters (code points, ideally by measured width), and never cut inside a UTF-8 sequence.
- The kind of a notice is given by the code that creates it, not guessed from words in player text.
- The chat history can be scrolled in the F9 window (or at least the documented 5 lines, with chat kept apart from system notices).

## Root cause
**What happens**

`lines_of` was run on the host with the exact code from notify.cpp:

| Message | Characters | Shown | Note |
|---|---|---|---|
| `<Ivan>` plus a 52-letter Russian sentence | 58 | 46 (line 1 only 20) | rest replaced by "..." |
| Russian space notice "Offer sent: <name>: 5 <spice> x $1200" | 60 | 50 | |
| `<Li>` plus 30 Chinese characters | 34 | 36 incl. "..." | line 2 is invalid UTF-8, ends in U+FFFD |
| English, 78 characters | 78 | 77 | fine |

The same message in the co-op window is one of 4 lines and is gone after 4 more events. So a non-Latin message longer than about 23 characters cannot be read in full anywhere in the game.

### Confidence
CONFIRMED, low severity: a host-side harness ran on the real `lines_of`, the rest was confirmed by reading. A second reviewer verified it.

## Fix
Not fixed yet. Suggested in the review:

- `lines_of`: count and cut on code points (`(c & 0xC0) != 0x80`), as `text_width` in coop_ui.cpp already does.
- `notify::show(text, kind)`, with the kind passed by `feed` callers. Chat passes `chat`.
- Keep chat lines in their own list and add a page or scroll to the lobby chat. Fix docs/08-ui.md.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

Host-side unit test in tests/ for `lines_of`. Move it to a header or make it testable, then check:
- (a) every output line is valid UTF-8;
- (b) a 58-letter Cyrillic message is shown with at least 80 % of its characters;
- (c) `kind_of` / the new kind for a chat feed line is `chat`.

FAILS on main 563f72d (invalid UTF-8, 46 of 58), PASSES with the fix.

### Related
None of the existing issues.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #108.
- 2026-10-27: ticket opened from the issue.
