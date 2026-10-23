# 5. Reverse engineering

## 5.1 Target and version

`SporebinEP1/SporeApp.exe` (GOG, 2016): MSVC 2008, x86, no RTTI (2 type descriptors in the whole
exe), `DYNAMIC_BASE`, has `.reloc`. Imports only `SteamApps`, `SteamAPI_Init`,
`SteamAPI_Shutdown` from `steam_api.dll`. Size 24 888 320 bytes - does **not** match any
build known to Spore ModAPI (Disk: 24 904 192 / 24 909 584; March2017: 24 885 248 /
24 895 536 / 25 066 744).

## 5.2 Method

1. **Porting ModAPI addresses (March2017 → GOG 2016)** - `tools/addrport/port_modapi.py`.
   A "does this address look like a function start" check showed that code in the two builds
   matches in chunks, with piecewise-constant offsets (0, ±0x10…0x90). The script finds function
   starts in our exe (`call rel32` targets + code pointers in `.rdata/.data`), then a Viterbi pass
   aligns the sorted list of ModAPI functions against the candidate offsets.
   Result: 1973 symbols (1744 high, 196 medium, 33 low) → `data/symbols/gog2016_922af7c7.sym`.
2. **Verification with REA** - Ghidra takes longer than the REA limit (330 s) to analyze the full exe, so
   `tools/addrport/pe_window.py` makes a "windowed" copy of the exe: same headers, imports,
   `.rdata/.data`, but `.text` is cut down to the needed window (e.g. 0xC00000–0xC30000). Addresses
   match the real ones, and analysis takes seconds. Verified addresses → `overrides_922af7c7.sym`.
2b. **A private full analysis** (2026-10-23): `analyzeHeadless <dir> spore -import SporeApp.exe
   -postScript Q.java q.txt out.txt` analyses the whole exe in ~15 min into a project of one's own
   (not the shared REA session, which other agents use); later queries with `-process SporeApp.exe
   -noanalysis -readOnly` take ~7 s (decompile, callers, xrefs, byte scans). Calls Ghidra does not
   see (functions only reached by `call rel32` from code it did not disassemble, BT node tables in
   .data) are found by scanning the file for the 32-bit value or for E8/E9 rel32 targets.
   A ready project on this machine: `out/ghidra-full/` (not in git, 350 MB); `out/ghidra-full/q.sh
   <queries> <output>` runs one batch (query lines: dec/asm/xref/callers/scan, see its scripts/Q.java).
3. **Runtime verification** via devkit: `mem.read`, `call`, the exception logger (`exc`),
   the watchdog thread (`hangdump`).

## 5.3 Confirmed functions

| Symbol | Address | How confirmed |
|---|---|---|
| `cCreatureAnimal::Create` | 0x00C09B00 | REA asm: cdecl `(const Vector3* pos, cSpeciesProfile*, int age, cHerd*, bool isAvatar, bool)`; age → +0xB34, herd → +0x1674, calls `SetAvatar` when isAvatar |
| `cCreatureAnimal::Update` | 0x00C0ADF0 | REA: virtual 0x60; if `!(+0xB58 & 0x200)` → `NPCTickAI`, otherwise `AvatarTickAI` |
| `cCreatureAnimal::NPCTickAI` | 0x00C09530 | REA callers: the only call is from Update |
| `cCreatureAnimal::GetHerd` | 0x00C046B0 | runtime exception: returns mHerd, callers don't check for null |
| `cGameNounManager::Get` | 0x00B3D3D0 | REA (call from Create) + runtime |
| `cGameNounManager::CreateInstance` | 0x00B20B80 | REA: Create calls it with noun 0x18EB45E |
| `cGameNounManager::DestroyInstance` | 0x00B224F0 | REA: error branch of Create |
| `cGameNounManager::SetAvatar` | 0x00B1FC70 | REA |
| `cCreatureAnimal::TakeDamage` | 0x00C075A0 | runtime: `cCombatant` vtable[6] of cCreatureAnimal (`this` = creature+0x5A8), `ret 0x14`: `(float dmg, u32 attackerPolitical, int type, const Vector3*, cCombatant* attacker)` |
| `cGameNounManager::CreateHerd` | 0x00B238B0 | REA: null only if species == null; `herdSize=0` → empty herd; herd fields: position +0x34, species +0xA4 |
| `Editors::cSpeciesManager::Get/GetSpeciesProfile` | 0x00401090 / 0x004DF580 | runtime: avatar species key → its profile |
| `cCreatureBase::PlayAnimation(To)` | 0x00C12430 / 0x00C14AE0 | REA + `ret 0xC` / `ret 0x10` |
| `ArgScript::PrintF` | 0x00841050 | REA: vsnprintf into a 0x400 buffer + write to stream |
| `App::cAppSystem::PreInit/Init`, `cSporeApp::Init/Shutdown` | 0x7E8BD0 / 0x7E67E0 / 0xF47E90 / 0xF475A0 | runtime: hooks fire in the correct order |
| `App::ICheatManager::Get`, `IGameModeManager::Get` | 0x67DDE0 / 0x67DCD0 | runtime (cheats, mode ids) |

## 5.4 Structures (offsets from `cCreatureAnimal*`)

| Field | Offset | Note |
|---|---|---|
| cSpatialObject / cLocomotiveObject | +0xC0 | vtable: GetPosition 0x2C, SetPosition 0x38, SetOrientation 0x3C, Teleport 0x44, SetDesiredSpeed 0xC4, MoveTo 0xE0, StopMovement 0xEC |
| mPosition / mOrientation | +0xC4 / +0xD0 | the planet is a sphere of radius ~505 |
| mVelocity | +0x288 | |
| cCombatant | +0x5A8 | HP +0x5E0, MaxHP +0x5D4 (MaxHP reads as 1.0 - to be clarified) |
| mpSpeciesProfile / mSpeciesKey | +0xB20 / +0xB28 | |
| flags | +0xB58 | bit 0x200 = avatar |
| mHerd | +0x1674 | **required**, see 5.5 |
| `cGameNounManager` | | mpAvatar +0x54, mpAvatarHerd +0x58, mPosseMembers +0x5C |

Mode IDs: Creature 0x1654C01, Editor 0xDBDBA1, main menu (GalaxyGameEntry) 0x2CCD1D2.
Cheat console: `ICheatManager` vtable AddCheat 0x18, RemoveCheat 0x1C, ProcessLine 0x20, GetArgScript 0x38.
`ArgScript::Line`: `vector<char*> mArguments` +0x14, mFirstArgIndex +0x3C, mArgumentCount +0x40.
`ArgScript::ICommand` (MSVC vtable): ParseLine, GetDescription, SetData, AddRef, Release, ~dtor;
fields: FormatParser* +4, refcount +8, data +0xC.

## 5.5 Problems found and solutions

* **A herd is required.** Code at 0x00D41A20 iterates over a creature's neighbors and calls
  `GetHerd()` on each without a null check → for a creature without a herd every update crashes with an AV
  at address 0x15C (Wine swallows the exception, the frame is aborted, the game "freezes").
  Solution: puppets are created in the avatar's herd (`mpAvatarHerd`) - which semantically is the posse anyway.
* **MinHook under Wine.** When installing a hook, MinHook suspends all threads of the process;
  a suspended thread may hold ntdll's virtual memory lock, and the next
  `VirtualProtect` hangs forever (reproduced when launching a second copy of the game).
  Solution: thread freezing in MinHook is disabled (`MH_SetFreezeThreads`), hooks are installed from
  the main thread.
* **Sockets in a second Wine process.** `select()`/blocking TCP operations could
  hang for tens of seconds in a second copy of the game under the same wineserver. The console was rewritten
  to use non-blocking sockets with polling.
* **Pausing in the background.** Spore stops rendering without focus; fullscreen mode under Wine
  minimizes. Solution: SporeKit's "background mode" (swallows WM_ACTIVATE*/KILLFOCUS,
  hooks GetForegroundWindow/GetActiveWindow/GetFocus) and a windowed test profile.

## 5.6 How NPC sync works (M2)

* **Creature registry** - a hook on `cCreatureAnimal::Update`: every creature updated during a frame
  is added to a list; the hook also acts as a filter (on the guest, its own NPCs are not updated and are removed).
* The host assigns NPCs a `net_id` and sends `NPC_SPAWN` (species key, "family" flag), `NPC_STATE` at 10 Hz,
  `NPC_DESPAWN`; the guest creates a proxy via `cCreatureAnimal::Create` in a herd of its species
  (`CreateHerd`, size 0) or in the avatar's herd for "family", without AI (hook on `NPCTickAI`).
* Damage - a hook on `TakeDamage`: a guest's hit on a proxy → `DAMAGE_REQ` → the host applies it to the real
  NPC with the guest's puppet as the attacker; an NPC's hit on a puppet on the host → `DAMAGE` → the guest
  applies it to its avatar. No friendly fire.
* **A dead proxy flies off the planet.** The guest plays a proxy's death locally (`TakeDamage`
  1e6 when the host's state says dead). The body then moves away from the planet centre at
  ~2.5 km/s (measured in the tribe stage, 2026-10-23: the same with damage = health + 1 and with
  the blow coming from above, so it is no knockback; the locomotive velocity +0x288 reads 0).
  Driven like a living proxy it was teleported back every frame and stayed ~80 m in the sky.
  `npc.cpp guest_tick` now pins a killed proxy to the host's corpse (velocity 0 + teleport every
  frame); coop_tribe.py checks the hunted animal's body (old code: 77.7 m off). Why the body
  flies (a ragdoll / death physics without the AI that a puppet lacks?) is open.

* **Time of day** - `cTimeOfDay::SetTimeOfDay` computes a quaternion and calls `SetOrientation`
  on the sky object (`0x01021110()`, then `0x01021630(obj)`); the host sends this object's orientation.
* **DNA** - global `float` 0x0169F378; `AddEvolutionPoints(float)` (0xD2E8A0, cdecl) also
  updates statistics and sends event 0x45AB96E for the UI; the guest catches up to the host's total
  by calling the original `Add` with the difference.

## 5.7 Open questions

* Auto-loading the received world: `cGamePersistenceManager::LoadGame` (0xB271A0, vtable[4],
  the first parameter field is the save name as a `string16`) is **not** called by the menu; the menu's load path
  goes through code near the string `%lslastSave.pld` (0xF94A26), which Ghidra didn't recognize
  as a function.

* Camera/view matrices for `world_to_screen` (nicknames above creatures) - `cGameModeManager::GetViewer`.
* Puppet recoloring (a different color/size to distinguish it from kin).
* Ability animations (attack, social) for M2: `cCreatureBase::PlayAnimation`, abilities via
  `GetAbility(index)` (vtable 0xB4).
* Full exe analysis in REA (requires a `REA_GHIDRA_STARTUP_TIMEOUT_MS` timeout of ≥ 2 h and
  reconnecting the MCP server).
