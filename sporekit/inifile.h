// Settings files (mods\*.ini) through the Windows profile API, in Unicode (BUG-0142).
//
// The profile API reads and writes a file without a byte order mark in the system ANSI codepage:
// a name outside it ("Вася" on a Western Windows or Wine) was saved as "????", and a file edited
// by hand in UTF-8 was read garbled ("JÃ¶rg"). A file that holds anything but ASCII, or gets a
// value that is not ASCII, is rewritten once as UTF-16LE with a BOM (from UTF-8 when its bytes are
// valid UTF-8, else from the ANSI codepage); the profile API then keeps it in Unicode. ASCII-only
// files stay as they are (tools edit them with sed). Standalone: tests/win_tests.cpp tests it.
#pragma once
#include <string>

namespace skini {

extern bool g_unicode_fix; // false: the old behaviour (kit.fix iniutf 0, win_tests --old)

// rewrites `file` as UTF-16LE with a BOM when it is not already; `force`: also when it is ASCII
// only (or missing: then an empty Unicode file is made). true when the file is Unicode afterwards.
bool make_unicode(const std::wstring& file, bool force);

std::string read_str(const std::wstring& file, const char* section, const char* key, const char* def);
int read_int(const std::wstring& file, const char* section, const char* key, int def);
bool write_str(const std::wstring& file, const char* section, const char* key, const char* value);

// the text of an ini file as UTF-8 (a UTF-16 file converted; others as they are): report archives
std::string to_utf8_text(const std::string& bytes);

} // namespace skini
