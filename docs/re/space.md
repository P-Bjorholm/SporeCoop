# Space stage RE: squadron co-op (GOG 2016 build, sha256 922af7c7…)

Static analysis only (llvm-objdump of `SporebinEP1/SporeApp.exe`, image base 0x400000, no runtime
checks). All conventions are MSVC x86: `thiscall` = `this` in ECX, callee pops `ret N` bytes.
Confidence: **V** = verified in this build's disassembly (prologue/ret/field use seen),
**P** = ported from ModAPI and consistent with code but not traced end to end,
**U** = unverified/hypothesis. Space code at 0x0102xxxx–0x0107xxxx has the *same* addresses as
ModAPI March2017 (port delta 0), 0x00Bxxxxx–0x00Cxxxxx is shifted by −0x10…−0x30.

Note: `overrides_922af7c7.sym` names `0x01020F30` "TimeOfDay_Unavailable" and `0x01021110`
"TimeOfDay_GetObject"; they are really `Simulator::GetCurrentContext()` (`mov eax,[0x16DEA6C];
mov eax,[eax+0x10]`) and `Simulator::GetActivePlanet()` (ModAPI names, same addresses).

## 0. Object layout recap (`cGameDataUFO`, size 0x818)

Vtables written by the ctor 0xC3C9B0 and the dtor 0xC3BAE0 (V):

| subobject | offset | vtable |
|---|---|---|
| cGameData (main) | +0x000 | 0x0146C7E0 (Cast +0x0C = 0xC36FC0, GetNounID +0x20 = 0xC37160 → 0x18EBADC, GetCastID +0x38 → 0xB033B403, SetPoliticalID +0x48 = 0x804880, GetPoliticalID +0x4C = 0x5AADB0 `mov eax,[ecx+0x30]`) |
| DefaultRefCounted | +0x004 | 0x0146C7CC |
| cLocomotiveObject | +0x034 | 0x0146C6D0 (SetPosition +0x38 = 0xC37B70, SetOrientation +0x3C = 0xC88850, Teleport +0x44 = 0xC888C0, GetModelKey +0x98) |
| ? (+0x108) | +0x108 | 0x0146C6C8 |
| cCombatant | +0x508 | 0x0146C660 (GetPoliticalID +0x10 = 0xC69020, GetWeapon +0x14 = 0xC37870 → [ufo+0x790], TakeDamage +0x18 = 0xC3CEA0, GetMaxHitPoints +0x58) |
| cBehaviorList | +0x5D0 | 0x0146C640 |
| IUnmanagedMessageListener | +0x5E8 | 0x0146C638 (HandleMessage = 0xC3B6A0, ret 8) |

Fields (V unless noted): cGameData `mbIsDestroyed` +0x21 (IsDestroyed 0xB182E0), `mID` +0x24
(= `++[0x167CE08]` in `cGameNounManager::CreateInstance` 0xB20B80 - per-process counter, unique
while the session lives), `mPoliticalID` +0x30; loco pos +0x38, rot +0x44, vel +0x1FC,
movement controller `loco+0x4CC` = ufo+0x500; combatant max HP +0x534, HP +0x540,
`mbInvincible` +0x588, `mpTarget` +0x550 (P); `mUFOType` +0x714 (0 = player, see UfoType below),
`mNextPosition` +0x718, `mNextVelocity` +0x724, `mNextOrientation` +0x730, `mOffsetFromPosition`
+0x740, `mbAtDestination` +0x74C, `mDestination` +0x750, `mZoomAltitude` +0x768 (planet: radius
of the flight sphere), `mDesiredModelKey` +0x780 (ResourceKey, printed by the debug dump 0xC3E150),
NPC weapons +0x790 (`mpNPCWeapon`), +0x794 ground, +0x798 near-air, +0x79C medium-air, +0x7A0
far-air, +0x7A4 abduct, current target refs +0x7A8 / +0x7AC (getters 0xC38C40 / 0xC38CC0),
`mEnergy` +0x7F4, `mMaxEnergy` +0x7F8.

UfoType (ModAPI SimulatorEnums): 0 Player, 1 UberTurret, 2 Raider, 3 ?, 4 Bomber, 5 Abductor,
6 Defender, 7 ?, 8 AmbushPirate, 9/10 ?, 11 SecurityDrone.

## 1. Enumerating UFOs in the current scene

| what | address | signature | conf |
|---|---|---|---|
| `GetData<cGameDataUFO>` helper | **0x00AD48B0** | `eastl::vector<cGameDataUFO*>* __thiscall(cGameNounManager*)`: pushes the 5 args below, calls GetData, returns `container+4` (the vector `{begin,end,cap}`), plain `ret` | V |
| `cGameNounManager::GetData` | 0x00B21260 | `tGameDataVectorT* __thiscall(mgr, create, clear, add, filter, nounID)`, `ret 0x14`; rebuilds the cached vector from the noun list (+0x78) only when the container's dirty byte is set | V |
| callbacks for UFOs | create 0xCD7E80, clear 0xD3D3D0, add 0xAD47B0 (casts to 0xB033B403, AddRef, push_back), filter 0xB1E410 (`obj->GetNounID()==noun`), noun 0x18EBADC | | V |
| `cGameNounManager::Get` | 0x00B3D3D0 | cdecl () | V (existing) |
| `cSimulatorSpaceGame::Get` | 0x01002A70 | cdecl (); `mNPC_UFOs` vector at +0x78 (offset from the attribute table 0x139D420) | P |
| `cStarManager::Get` / `GetEmpire(pol)` | 0x00B3D370 / 0x00BA94A0 | `cEmpire* __thiscall(starMgr, uint32 politicalID)` (used exactly so by SpawnUFO) | V |

Evidence: ~40 call sites use the exact sequence
`push 0x18EBADC; push 0xB1E410; push 0xAD47B0; push 0xD3D3D0; push 0xCD7E80; mov ecx,nounMgr;
call 0xB21260` and iterate `[ret+4]..[ret+8]` (e.g. 0x1000C60 kills all type-3 UFOs of the
player empire: `cmp [esi+0x714],3`, `GetPoliticalID`, then `TakeDamage`).

The list contains **every** live UFO noun: the player UFO (type 0, also
`cSimulatorPlayerUFO::Get()+0x40`; the singleton pointer is `[0x016DC9F4]`), our puppets, NPC
UFOs on the planet or in the system. Skip `mbIsDestroyed` (+0x21) and HP ≤ 0.

Per-UFO data to replicate:

| field | read |
|---|---|
| net identity | host keeps `map<cGameDataUFO*, net_id>`; `mID` +0x24 is a convenient stable key on the host only (counter differs per process) |
| owner | `mPoliticalID` +0x30 (or vtable +0x4C). Empire = `cStarManager::GetEmpire(pol)`; player = `GetPlayerEmpireID()` 0x1020F40 |
| type | `mUFOType` +0x714 |
| model | `mDesiredModelKey` +0x780 (12-byte ResourceKey) - pass to SpawnUFO on the mirror |
| pose | loco +0x38 / +0x44 / +0x1FC, plus `mDestination` +0x750, `mZoomAltitude` +0x768 |
| HP | +0x540 / +0x534; invincible +0x588 |
| place | same as players: `SpacePlayerData` context (planet id / star id) |

Recipe (host → guests, 10 Hz like NPC_STATE in the creature stage):
1. Each frame (or 10 Hz) in space: `vec = thiscall(0xAD48B0, cGameNounManager::Get())`; for each
   `u` with `u->mUFOType != 0` and not one of our puppets: assign/lookup net id, send
   `{net_id, type, pol, modelKey, pos, rot, vel, hp, maxhp, place}`; UFOs missing from the list
   → `UFO_DESPAWN`.
2. Guest: on first sight `SpawnUFO(type, &pol, &modelKey)` (0x102ACC0, cdecl, `ret`; model ptr may
   be null → default model; on a planet it calls `0xC3AB80` to put the UFO on the flight sphere),
   then drive it like a puppet (§4). Despawn with `cGameNounManager::DestroyInstance` 0xB224F0.
3. Guest must not have its own NPC UFOs: hook `SpawnUFO` 0x102ACC0 and `CreateUFO` 0x102AC60
   (cdecl `(UfoType, cEmpire*)`, calls `Initialize`) and return null for non-mod calls while a
   co-op guest is in space (keep a thread-local "mod is spawning" flag), and destroy any
   non-mirror UFO with type ≠ 0 found in the GetData list (callers of SpawnUFO such as raid
   code must tolerate null - **U**, check each caller before enabling; alternative is
   spawn-then-destroy next frame). SpawnUFO callers (functions): 0xC5D2C0, 0xD4C150, 0xFF8980,
   0xFF96B0, 0xFFA630, 0x1025200, 0x102B500, 0x102B790; CreateUFO callers at 0xFDAEDA,
   0x1008B3A, 0x1008FF2, 0x100923F, 0x10095C4, 0x100B004, 0x102B204, 0x10387F8, 0x1038B01.

## 2. Player weapons → damage

Every space weapon ends in the target combatant's virtual `TakeDamage` (combatant vtable +0x18).
For UFOs that is **0x00C3CEA0** - the single hook point for both directions.

### 2.1 `cGameDataUFO::TakeDamage` 0x00C3CEA0 (V)
`int __thiscall(cCombatant* this /*ufo+0x508*/, float dmg, uint32 attackerPol, int type,
const Vector3* dir, cCombatant* attacker)`, `ret 0x14`. **`dir` is a unit knock-back direction
(explosion → target), not a world position** (stored at combatant+0x74). Order:
1. `type==10` (heal) → base directly.
2. Return if invincible (combatant+0x80 = ufo+0x588, via 0xBFC5A0) or `0x1041D20()` (global flag
   0x23BA1FC, probably god-mode cheat - U); if dead (combatant+0x34 = ufo+0x53C == 2); if
   `ufo+0x76C==3 && type!=5`.
3. Player only (type 0): return if on a planet and `cSimulatorPlayerUFO::Get()+0x24 > 0`
   (invulnerability lock counter, incremented by 0xFFBF50).
4. Destroyed attacker → null. Attacker UFO of type 3 (posse/ally) → `attackerPol = player
   empire`. `attackerPol == own political id` → return (no friendly fire; turrets too).
5. `AddHostileUnit(attacker)` (vt+0x1C = 0xC37AE0, sets ufo+0x710, null-safe).
6. NPC-vs-NPC multiplier (target type ∉ {0,3} and attacker is a turret or a UFO of type ∉ {0,3}):
   `dmg *= 0x102A5F0(0x15B8510)` (tuning prop 0x679F157, default 1.0).
7. Knock-back on planets (not type 8): ufo+0x60C / +0x618, clears `mbAtDestination`.
8. Base **`cCombatant::TakeDamage` 0x00BFCEF0** (same args, `ret 0x14`): sets mpLastAttacker
   (+0x28), space damage-boost tool multiplier, stores mLastAttacker (+0x3C) and dir (+0x74);
   type 10 heals (clamped to vt+0x58), type 11 no-op, else `HP -= dmg`; if HP < 1.53e-5:
   HP = 0, state +0x34 = 2, posts **kMsgCombatantKilled 0x1622184** (combatant, attacker) via
   MessageManager 0x67DC80 vt+0x14, then `0xF249B0(gameData, type)`.
9. Target is the player: interrupts the active tool (+0x13C) and sets ufo+0x76C = 1;
   auto-retaliation through 0x102AF90 / 0xFFD5B0.
10. HP ≤ 0: player → death (§3); NPC → loot 0x102BA40, relationship hit 0x591F833 (both pols
    ≠ −1), event log, explosion effect, then **DestroyUFO 0x0102C350** (cdecl `(ufo, bool)`:
    removes from lists, stops its tool, `DestroyInstance`). Message 0x2A7C313D if the player
    was the killer.

### 2.2 Fire path (V unless noted)
* `cToolManager` player fire entry 0x01050BC0 (`thiscall(toolMgr, tool, ?, bool)`, `ret 0xC`,
  arg 2 U) → **FireTool 0x01050080**: `thiscall(cToolManager*, cSpaceToolData* tool,
  cSpatialObject* owner, const Vector3* aim, int targetDamagePointId, int arg5, Object* target,
  bool showErrors)`, `ret 0x1C`. Writes owner → tool+0x114, damage point → +0x11C, target →
  +0x118, calls strategy `WhileAiming` (vt+0x24) then `WhileFiring` (vt+0x34). Costs/UI only
  when owner is the player UFO (+0x34). Autofire variant 0x010504B0 (`ret 0x10`).
* `cDefaultProjectileWeapon` vtable 0x149C128: **WhileFiring 0x01056F10** (`thiscall(strategy,
  tool, const Vector3* aim, int)`, `ret 0xC`) creates noun 0x24270C5 (cDefaultToolProjectile) and
  calls `LaunchDefaultToolProjectile` 0x01054390 (cdecl `(tool, proj, &ownerPos, aim)`), which
  configures (vt+0x54 = 0xCB4EB0) min = `(int)(tool+0x1B4 * tool+0x144)`, max =
  `(int)(tool+0x1B8 * tool+0x144)`, radius tool+0x1AC → proj+0x52C/0x530/0x534/0x538.
* Projectile Update 0xCC2920 (cProjectile sub-object at +0x504), phase byte +0x544:
  phase 0 `0xCBB970`: on collision sets strike type/target (+0x5D0/+0x5D4) and calls strategy
  **OnHit** (vt+0x30; `cDefaultProjectileWeapon::OnHit` 0x0105B310 → `cToolStrategy::OnHit`
  0x010593B0, `ret 0x10`, only special tools/terraform/relationship effects, posts
  kMsgToolOnHit 0x56690BB - **no combatant damage here**); phase 1 **0x00CBBEF0** (`thiscall(proj)`,
  `ret`) = explosion: radius grows, damage tick every 250 ms, spatial query, each combatant hit
  once (+0x570); same political id skipped unless `kFlagDamagesFriendlies` (tool+0x174 bit 9);
  call `target->TakeDamage(min + rand(max-min), attackerPol, proj+0x538, &dir, attacker)` with
  attacker = owner (tool+0x114) cast to cCombatant, attackerPol = attacker vt+0x10 (or −1).
* Beams: AoE helper 0xCBA690 (usercall eax = centre) → 0xCB5B20 (usercall esi = target) →
  TakeDamage. Artillery-like 0xCB9940 calls TakeDamage with attacker = 0.
* Symbol fix: ported `cSpaceToolData::GetStrategy = 0x1050030` is wrong (it returns
  `strategy->ProcessCost(mUseCost)`); real GetStrategy = **0x0104F940** (`thiscall(tool)`,
  reads tool+0x178, default 0xB0DA49D9).

## 3. Enemy damage to the player, NPC fire, death/respawn

* NPC fire: **`cCombatSimulator::Update` 0x00AE7330** (vtable 0x145C838) iterates all
  cCombatants (GetData, cast 0x13F94D4); weapon = vt+0x14 (UFO: ufo+0x790), target =
  combatant+0x44 (pending +0x48); if `IsInWeaponRange` (vt+0x40) → FireTool 0x1050080
  `(weapon, self vt+8, &targetPos, aimIdx, delta, target vt+8, 1)` (autofire → 0x10504B0), then
  `target->AddHostileUnit(self)`. Hits reach the player through the same projectile →
  0xCBBEF0 → 0xC3CEA0 path with mUFOType 0.
* Player death (inside 0xC3CEA0, HP ≤ 0, type 0; P): `cSimulatorSpaceGame::Get()->0x1005020(10)`,
  sound, **0xC3C660(ufo)** (`thiscall`, `ret`): cinematic `SPG_PlayerDestroyed_PrePosse_Galaxy /
  _PrePosse_NotGalaxy / _PostPosse`, handle → ufo+0x7F0, hides ship (+0x77C=0, +0x77D=1),
  deactivates tool, event log 0x949ACE8, `(*0x16DD778)->0x100E580(attacker)`.
* Respawn (✔ runtime): UFO message handler 0xC3B6A0 (msg 0x44F1189 = a cinematic ended, or its own
  retry 0x67B2A95) with ufo+0x7F0 set → **0xC38420** (`thiscall(ufo)`, `ret`): travel to nearest owned
  star, clear +0x7F0, state +0x53C = 0, HP = max, energy = max (+0x7F4 = +0x7F8), message 0x6174B67.
  The death cinematic ends only when the player presses OK on its caption (§7.2.1).
* Sync state: `ufo+0x53C == 2` dead, `ufo+0x7F0 != 0` death explosion effect / caption running.

### Recipe for §2/§3 (hook 0xC3CEA0, thiscall 5 args, `ret 0x14`; flag `g_netApply` for replays)
Guest:
1. `this-0x508` is a mirror: if the shot is ours (`attacker == localUFO+0x508`, or
   `attackerPol == GetPlayerEmpireID()` and `type != 10`) send `HIT{npc_id, dmg, type, dir}`;
   in every case return without calling the original (host is authoritative).
2. `this` is the local UFO and the attacker is a mirror / NPC empire and `!g_netApply` → drop.
3. On `DAMAGE{dmg, type, dir, npc_id}`: `g_netApply=1; orig(localUFO+0x508, dmg, npcPol, type,
   &dir, mirror ? mirror+0x508 : nullptr); g_netApply=0` - local death/respawn then run natively.
4. On `NPC_DEATH`: clear mirror+0x588 and call orig with `(mirror+0x508, 1e6, 0xFFFFFFFF, 0,
   &dir, nullptr)` (explosion + 0x102C350; pol −1 skips relationship/event log) or call
   `0x102C350(mirror, 0)` directly.
5. Mirrors never fire: FireTool hook (§4).
Host:
1. `this-0x508` is a guest puppet (mod Defender UFO): send `DAMAGE` to that guest, then call the
   original (puppet stays invincible via +0x588; NPC aggression/AddHostileUnit keep working).
2. On `HIT` from a guest: if `npc+0x53C != 2`: `g_netApply=1; orig(npc+0x508, dmg,
   GetPlayerEmpireID(), type, &dir, puppet+0x508)`. With the puppet as attacker the NPC
   retaliates against the puppet, but the NPC-vs-NPC multiplier applies (default 1.0);
   `nullptr` avoids it but loses retaliation.
3. NPC deaths: kMsgCombatantKilled 0x1622184 or `+0x53C == 2` after the call → `NPC_DEATH`.
Open (U): whether host NPC AI targets Defender puppets at all (runtime check); meaning of
`0x1041D20`; arg 2 of 0x1050BC0.

## 4. NPC UFO per-frame update and direct pose control

Movement is done by a per-UFO *locomotion controller* object stored at `loco+0x4CC`
(= ufo+0x500), set by `cLocomotiveObject::SetController` **0x00C422F0** (`thiscall(loco, ctrl)`,
`ret 4`, AddRef new / Release old). Two UFO controllers exist:

| controller | vtable | ctor | Update (vtbl +0x0C) | installed by |
|---|---|---|---|---|
| default UFO controller | 0x0149CB20 | 0x105D1B0 | **0x01062CF0** `thiscall(ctrl, ctx*)`, `ret 4` | `cGameDataUFO` ctor **0x00C3C9B0** (every UFO) and Read 0xC37BD0 |
| "follow next pose" controller | 0x0149CB10 | 0x105D150 | 0x0105EBA0 `thiscall(ctrl, ctx*)`, `ret 4`: steers straight to mNextPosition/mNextOrientation | 0xC373B0 `thiscall(ufo)` (callers 0x1001807, 0x102C16B, 0x102C2D9, 0xDBEFE2) |

`ctx`: +0x00 Vector3 output velocity, +0x10 float dt (s; ×1000 → ms), +0x14 `cLocomotiveObject*`
(UFO = ptr − 0x34). 0x1062CF0 calls **0x1060E00** (`thiscall(ctrl, ctx, int dtMs)`, `ret 8`) when
`GetCurrentContext()==0` (planet) and **0x105F2E0** (same signature, `ret 8`) otherwise
(solar system), then integrates toward `mNextPosition + mOffsetFromPosition`.
0x1060E00 returns immediately if `byte [ufo+0x691] != 0` (flag set/cleared by AI behaviours,
meaning unknown - **U**, do not rely on it).

Decision AI ("attack", "patrol", "flee"…) is data-driven utility AI: a registry of 16-byte
entries `{hash, fnA, fnB, fnC}` at 0x0159E640… (e.g. hash 0x5872576 → 0xDC32C0/0xDC0AF0/0xDBFFD0;
functions get `cGameDataUFO*` and return float scores or fill a goal). They drive the UFO only
through `cGameDataUFO` commands: **MoveTo(dir)** 0x00C3BFB0 (`thiscall(ufo, const Vector3*)`,
`ret 4`: `mDestination = normalize(v)*mZoomAltitude`, `mbAtDestination = 0`, loco+0xDC request),
**SetZoomAltitude(f)** 0x00C3DBE0 (`ret 4`, then MoveTo(mDestination)), **MoveToPosition(pos)**
0x00C3DC20 (`ret 4`; planet → MoveTo + altitude = |pos|, system → 0xC3C120) and weapon use
(see §2). The dispatcher that iterates this registry was not found (**U**).

Direct pose control (V):
* `cGameDataUFO::SnapToDestination()` **0x00C37FA0** (`thiscall(ufo)`, plain `ret`):
  `mNextVelocity = 0; mNextPosition = mDestination; mZoomAltitude = |mNextPosition|;
  loco->SetPosition(mNextPosition + mOffsetFromPosition); loco->SetOrientation(mNextOrientation);
  mbAtDestination = 1`. So: write `mDestination` (+0x750) and `mNextOrientation` (+0x730), call
  0xC37FA0 - the controller then has nothing to do.
* Or the current puppet path: loco vtable SetPosition +0x38 (0xC37B70, also moves the abduct
  beam) / SetOrientation +0x3C / Teleport +0x44, and pin +0x718/+0x724/+0x730/+0x750/+0x74C.

Recipe for mirrors (guest):
1. Spawn with SpawnUFO (§1). Mark the pointer in a `mirrors` set.
2. Movement: hook **0x01062CF0** (controller Update; it also runs for the player UFO and for
   puppets, so filter strictly on the mirror set). If `ctx->loco − 0x34` is a mirror:
   write the received pose (`mDestination`, `mNextOrientation`, `mNextVelocity`) and call
   0xC37FA0, zero `ctx+0..8`, return without calling the original. Otherwise call original.
   (Cheaper alternative without a hook: keep doing what `space_drive` does for puppets.)
3. Firing / damage: mirrors must not shoot - hook FireTool **0x01050080** and return `al=0`
   when `owner == mirror+0x34` (NPC fire comes from `cCombatSimulator::Update` 0xAE7330, §3).
   Drop all local damage to mirrors in the TakeDamage hook (§2); real HP comes from the host
   and is written to +0x540.
4. Host side: nothing is suppressed; guest hits arrive as damage requests (§2).

## 5. Colonies

### 5.1 Placing a colony (V)
1. **`cPlaceColonyToolStrategy::OnHit` 0x0105B930** (`thiscall(strategy, tool, Vector3* pos,
   hitType, int)`, `ret 0x10`): calls `cDefaultProjectileWeapon::OnHit` 0x105B310, clears ground
   objects `0xFDC330(pos, radius)` (cdecl), then queues a delayed drop on a sequencer singleton
   (`*0x167B5EC`, lazy getter 0xB268A0 cdecl): `seq->0xAE4920(cb = 0x1058C60,
   GetPlayerEmpireID(), &pos, 0, 0xFD701F6F, 0, quat* or null, 0)` (`thiscall`, `ret 0x20`;
   null quat → `cPlanetModel::GetOrientation(pos)` 0xB7F320; node+0x04 pos, node+0x20 quat).
2. **Colony callback 0x01058C60** (cdecl `(node*)`): `activeStar->SetOwner(playerEmpire->mPoliticalID)`
   (`cStar` 0xC8B740, `ret 4` → starRec SetTechLevel(5) 0xBB9C10 + SetEmpireID 0xA16980 (+0x54));
   active planet's cGameData (planet+0xD4) vt+0x48 SetPoliticalID; then
   `cSimulatorSpaceGame::Get()+0x54` (mpHighLODPlanetSim) → **CreateColony**; city+0x2E8 =
   node+0x38 (U meaning); UI/badges.
3. **`cSimPlanetHighLOD::CreateColony` 0x00FF8FB0** (`thiscall(sim, Vector3* pos, Quaternion* rot)`
   → `cCity*`, `ret 8`, not in ModAPI): city via 0xBD9EA0(pos, 0); player cCivilization (0xB25F20,
   or new noun 0x18C816A); setup 0xBE87B0; city SetPoliticalID; star citizen species 0xBB9CD0
   (+0x74); `cPlanet::SetTechLevel(5)` 0xC70F60 → cdecl 0xC70850(record, level) (+0x194); finds
   the `cCivData` with `+0x14 == playerPol` → new cCityData (0xC4, ctor 0xFF2260) +
   `cCivData::AddCityData` 0xFF2B20, else new cCivData (0x50, ctor 0xFF3210) +
   `cPlanet::AddCivData` 0xC71230; `playerEmpire->AddPlanet(record)` 0xC34FB0; messages 0x4446533
   (first city) and 0x43F2583 (colony placed); 0xC70120(planet). Uses `GetActivePlanet()`
   internally → only for the planet the player is on.

### 5.2 Ownership / spice storage
There is no owner field in `cPlanetRecord`. Ownership = record tech level **+0x194 == 5**
(empire colony) + `mCivData` vector **+0x15C** of `cCivData` (`mPoliticalID` +0x14, `mCities`
+0x3C); star record `mEmpireID` **+0x54**, flags +0x5C (0x10 owned), tech level; `cPlanet`
cGameData base at +0xD4 (political id at planet+0x104), record ptr +0x13C; `cEmpire::mStars`
+0x88; player only: `SpacePlayerData` +0x20 `mPlayerColonies` (vector<cPlanet*>).

| function | address | conv | notes |
|---|---|---|---|
| `cEmpire::AddPlanet(cPlanetRecord*)` | 0xC34FB0 | thiscall `ret 4` | player → `SpacePlayerData::AddColony` 0x1022040 (thiscall on `*0x16DEA6C`, `ret 4`, via RecordToPlanet 0xBB5B20); then AddStarOwnership; ally → msg 0x43F2585 |
| `cEmpire::AddStarOwnership(cStarRecord*)` | 0xC342E0 | thiscall `ret 4` | mStars +0x88, mFlags +0x50 \|= 0x10, species key; each tech-5 planet → 0xC73920; msg 0x55BD8F7 |
| ConvertPlanetOwner(rec, pol) | 0xC73920 | cdecl | star SetEmpireID, rewrites every cCivData pol (0x97D0D0), recolour, live civ nouns if active |
| SetTechLevel(rec, level) | 0xC70850 | cdecl | +0x194 (getter 0xB8DB90), then star tech 0xC8B760(starRec) |
| FillPlanetDataForTechLevel(rec, level) | **0xB97040** | cdecl | (ported 0xB970A0 is wrong) level 5 → 0xB96E40(rec): cCivData with pol = star mEmpireID + a cCityData; then `rec->0xB8DA00(1)` (flag 0x40 in +0x2C) |
| cStarRecord SetEmpireID / SetTechLevel / SetFlag(mask,bool) | 0xA16980 / 0xBB9C10 / 0xBB9C40 | thiscall | +0x54 `ret 4`; tech level; +0x5C `ret 8` |
| `cPlanet::GetEmpire` | 0xC71FB0 | thiscall | empire only if record tech == 5; pol from planet cGameData |
| `cEmpire::IsAllyOf(pol)` | 0xC31D30 | thiscall `ret 4` | mAllies +0x70 vs ally +0x84 |

Spice: per city `cCityData` +0x1C `mSpiceProduction` (stored spice; Get 0xFD9420, Add (≥0)
0xFF0200, Set 0xFF0220, thiscall); `CalculateSpiceProduction(rec, remove)` 0xC70740 (cdecl) sums
cities and removes; spice type = record +0x198 `mSpiceGen` (getter 0xB8DBB0, setter 0xB8DBC0,
`cSpaceTrading::AssignPlanetSpice` 0x103CA50 `ret 8`).

Record-level colony sequence used by the game (0xFE9AF3–0xFE9B43 inside 0xFE9410, purpose U):
`empire->AddStarOwnership(star); star->SetEmpireID(id); star->SetTechLevel(5);
star->SetFlag(0x100,1); SetTechLevel(rec,5); FillPlanetDataForTechLevel(rec,5); rec->0xB8DA00(1)`.

### 5.3 Recipe: shared colonies
* Capture (any player): hook `CreateColony` 0xFF8FB0 (or callback 0x1058C60), send
  `{PlanetID (record+0x184), pos, quat}`; re-entrancy flag so replays are not re-sent.
* Apply, receiver on the same planet: `0xB268A0()->0xAE4920(0x1058C60, GetPlayerEmpireID(),
  &pos, 0, 0xFD701F6F, 0, &quat, 0)` - drop animation + full original path. Direct alternative:
  `cStar 0xC8B740(activeStar, pol)` then `GetPlanetHighLOD()->CreateColony(&pos, &quat)`.
* Apply, receiver elsewhere: `rec = cStarManager::GetPlanetRecord(planetID)` (0xBA6EF0), run the
  record-level sequence above, then `playerEmpire->AddPlanet(rec)` 0xC34FB0. Layout is then
  procedurally generated, not the host's. U: regeneration on later visit; idempotence of
  AddPlanet (it skips already-owned stars).
* Shared spice/money: one empire → treat host as authority for `cEmpire::mEmpireMoney` (+0xD0)
  and per-city `mSpiceProduction`, push periodic snapshots per PlanetID.

## 6. Raids

**ModAPI ids are swapped for this build**: `cRaidEvent` Cast compares **0x3960C0A**
(0xC5B340), GetNounID returns **0x3960C0E**. Plunder event: Cast 0x397BFF2, noun 0x397BFF3.

`cRaidEvent` (V): main vtable 0x1470350 (+4: 0x1470340, +0x34: 0x1470330), ctor 0xC5BEB0
(fields 0x1F0–0x238 match ModAPI), Cast 0xC5B340; vt +0x78 Initialize 0xC5B7B0, **+0x7C Update
0xC5E3E0** (`thiscall(raid, int dtMs)` → new state, `ret 4`), +0x80 Load 0xC5B430, +0x88
OnMissionAccept 0xC5C5E0, +0x94 OnMissionCompleted 0xC5BB70, +0x98 OnMissionFailed 0xC5BC60.
cMission fields confirmed: mMissionID +0x78, mElapsedTimeMS +0x7C, mState +0x84, owner empire
id/ptr +0x8C/+0x90, target empire id +0x94, source planet +0x9C, prop list +0x180,
mpTargetPlanet +0x1E8 (getter 0x970A90); base Update 0xC479F0.
`cMission::SetState(int)` 0xC47E00 (`ret 4`, writes +0x84, posts message); wrappers Accept(2)
0xC486F0, Complete(5) 0xC48720, Fail(6) 0xC48730, Abort(7) 0xC48710, Reject(8) 0xC48700.

Start: `cSimulatorUniverse` (singleton `*0x16DD778`, ctor 0x1015390) has an event-routine table
(0x14961A8; raid descriptor at +0x50). **Raid start 0x010125D0** (`ret 8`, `(cPlanetRecord*
colony, vector<cMission*>* out)`): `cMissionManager::Get()` 0xFEB880 →
**`CreateMission` 0xFEC420** (`thiscall(mgr, missionID, cPlanetRecord* src, cEmpire* owner,
cMission* parent)`, `ret 0x10`; class from prop 0x874B25B6, 0xA4B0A2DF → raid factory 0xFEBF70)
`(0xA4B0A2DF, colonyRec, empireOwningStar, 0)` → `SetAttackerEmpire(0)` 0xC5B520 (+0x230; 0
keeps −1 = pirates? U) → `SetTargetPlanet(rec)` 0xC472C0 → `SetNumBombers(rand)` 0xC5B540
(+0x1F8) → Accept. Variants: 0x100E620 (`ret 0xC`, `(0, rec, numBombers)`, used by the `raid`
cheat parser 0x1025200: `raid -start N | -stop | -plunder | -repair`: handy for tests);
plunder starter 0x10143E0 (class 0xC4956AA7, factory 0xFEBF90).

Update 0xC5E3E0 ("player present" = target planet is active and context 0):
* while `mWaitingForRaid` (+0x215): when arrival time passed or player present, ship list via
  0xC5C480; if present → **spawn 0xC5D2C0** (cdecl, 9 dwords: `(raid, &bgList +0x238,
  attackerPol, &pendingUFOKey +0x200, Vector3 spawnLoc +0x224 by value, bool summoned +0x234,
  bool leaveOnArrival +0x21C)`; per 0x20-byte entry (UFO type at +4) `SpawnUFO(type, &pol,
  &key)` + `ufo->SetOwnerMission(raid)` 0xC37810 (`ret 4`, ufo+0x800; getter 0xC37850)), clear +0x215.
* "still active" 0xC5DB90 (cdecl `(raid, &list)`): present → any UFO with +0x800 == raid, not
  dying (0xC373F0)/destroyed, HP +0x540 above threshold; else bgList non-empty.
* active: colony still tech 5, not owned by attacker (0xC8BAA0), owned by +0x94 → before timeout
  (prop 0x2B9E524) off-screen sim **0xC5C9E0** (cdecl 10 args `(list, planet, attackerID,
  &numBombers, &numFighters, propList, &dmgRem +0x20C, &dmgRemUFO +0x210, &destroy +0x214, dt)`)
  → return 3; else `0x102C0D0(planet)` → 6 (Failed).
* no ships left → 5 (Completed); event log 0x25594EA8.
* Completed/Failed handlers both call `0x102C0D0(planet)` (cdecl: raid UFOs leave) and
  `ApplyRelationship` 0xD06280.

### Recipe: shared raid defence
* Guests: no own universe events - hook 0x10125D0, 0x10143E0, 0x100E620 → return 0
  (alternative: the cSimulatorUniverse update, slot U: vtable 0x1496280 [4] = 0x1014CD0?).
* Host start: hook CreateMission (or end of 0x10125D0); if `Cast(0x3960C0A)` succeeds send
  `{missionID, PlanetID, attacker +0x230, bombers +0x1F8, fighters +0x1FC}`.
* Raid ships: UFOs with `+0x800 == raid` (or hook 0xC5D2C0) are ordinary hostile NPC UFOs →
  mirrored by §1/§4, guest hits go to the host by §2.
* Host end: hook Update 0xC5E3E0 post-call; return 5/6 → send result (+ city destruction /
  capture; `cSimPlanetHighLOD::DestroyCity` 0xFFA7B0 is ported, not checked).
* Optional guest UI: create a mirror raid with the 0x10125D0 steps, hook 0xC5D2C0 and 0xC5C9E0 to
  no-op, make Update return 3 until the host result, then 5/6 so the native handlers run.
* U: whether the manager applies Update's return through SetState; meaning of node+0x38.

## Open questions (all items)
* Dispatcher of the UFO utility-AI registry (0x159E640) - not needed if mirrors are pinned and
  FireTool is blocked.
* Runtime checks needed: SpawnUFO/CreateUFO null-tolerance of callers on guests; host NPC AI
  targeting Defender puppets; `+0x691` semantics; record-level colony replay on later visits.

## Co-op features (rescue, jump, raids, missions, terraform, cargo)

Static analysis only (llvm-objdump + Ghidra headless decompile, no runtime). Marks: ✔ verified in
this build's code (prologue / `ret N` / call sites / field use), ~ likely (behaviour inferred from
code, names are ours), ? unknown. [ModAPI] = name/meaning recalled from ModAPI, addresses differ.

### 7.1 Globals and getters used below (✔)

| what | address | notes |
|---|---|---|
| `SpacePlayerData*` | `[0x16DEA6C]` | +0x04 active `cPlanet*`, +0x08 active `cStar*`, +0x0C ?, +0x10 context (0 planet, 1 system, 2 galaxy, −1 transition), +0x18 player pol, +0x1C cached player `cEmpire*`, +0x20 colonies |
| GetCurrentContext | 0x01020F30 | cdecl () → int |
| GetActiveStar / GetActiveStarRecord | 0x010210E0 / 0x010210F0 | cdecl; record = `star+0x48` (null-safe) |
| GetActivePlanet / GetActivePlanetRecord | 0x01021110 / 0x01021150 | cdecl; record = `planet+0x13C` |
| GetPlayerEmpire | 0x010211B0 | cdecl → `cEmpire*` (re-cached from pol) |
| GetPlayerHomePlanetRecord | 0x01021220 | cdecl; `cEmpire::RequireHomePlanet` 0xC31870 or active planet |
| SetCurrentContext(int) | 0x01021850 | cdecl; only writer of +0x10 (called by the transitions below) |
| SetActiveStarAndPlanet(cStarRecord*, cPlanetRecord*) | 0x010218A0 | cdecl; RecordToStar 0xBB38C0 → +0x08, RecordToPlanet 0xBB5B20 → +0x04 (planet null if not in star). Low-level setter, no loading |
| `cAppModeSpace*` (space game mode, vtable 0x1494AF8, name u"cAppModeSpace") | `[0x16DA918]`, getter 0x00FD9C50 | set by ctor 0xFDC9F0; `+0x15C` = `cSimulatorSpaceGame*` |
| `cSimulatorSpaceGame*` | `[0x16DD0DC]`, getter 0x01002A70 | +0x40 = **`cPlayerInventory*`** (generic getter 0xA1AC50 = `mov eax,[ecx+0x40]`), +0x78 NPC UFO vector |
| `cSimulatorPlayerUFO*` | `[0x16DC9F4]`, getter 0x00FFBD00 | +0x24 planet-invulnerability counter (inc 0xFFBF50 / dec 0xFFBF60 / >0 test 0xFFBF40), +0x40 player `cGameDataUFO*` |
| `cStarManager::GetStarRecord(StarID)` | 0x00BA6EB0 | thiscall(starMgr 0xB3D370(), id) `ret 4`; 0 → `[+0x14C]`, −1 → null, else table `+0xC8`[(id>>12)*5][id&0xFFF] |
| `cStarManager::GetPlanetRecord(PlanetID)` | 0x00BA6EF0 | thiscall `ret 4`; star = id&0xFFFFFF, planet index = id>>24 |
| star id / planet id | starRecord+0x70 / planetRecord+0x184 | as used by `modes.cpp` (`space_place`) |
| `cEmpire::GetStars` / `GetHomeStarRecord` | 0x00C30A10 / 0x00C30DA0 | thiscall; `lea eax,[ecx+0x88]` (vector<cStarRecord*>) / home star id +0xB0 → GetStarRecord |

### 7.2 Player ship destruction → "disabled 20 s" (rescue)

Death path (✔, all inside `cGameDataUFO::TakeDamage` 0xC3CEA0, `ebp` = combatant = ufo+0x508):
1. base `cCombatant::TakeDamage` 0xBFCEF0: `HP(+0x540) -= dmg`; if `HP < 1.5259e-5` (const
   0x146A3B8): HP = 0, state +0x53C = 2, message 0x1622184. Damage boost multiplier only for the
   *attacker* side (player damage-boost tool 0x168E044), so a hit on the player loses exactly `dmg`.
   Early returns: `ufo+0x588` invincible, state already 2, planet + `cSimulatorPlayerUFO+0x24 > 0`.
2. back in 0xC3CEA0 at 0xC3D545: `if (HP <= 0 && mUFOType == 0)` → `cSimulatorSpaceGame->0x1005020(10)`,
   sound 0xD456D958, unless adventure/scenario checks (0x1015C90 → 0xA97F80 / 0xFF02D0) call
   **`cGameDataUFO::PlayerDestroyed` 0x00C3C660** (thiscall(ufo) plain `ret`: cinematic
   `SPG_PlayerDestroyed_*`, handle → +0x7F0, hides ship +0x77C=0/+0x77D=1), then event log
   0x949ACE8 and `cSimulatorUniverse(*0x16DD778)->0x100E580(attacker)`.
   Second caller of 0xC3C660: 0x1016948 (inside 0x10167F0, when the player UFO has HP ≤ 0 after an
   adventure/sequence ends) ~.
3. Cinematic end (✔ runtime: the player's OK on the death caption, §7.2.1) → UFO message handler
   0xC3B6A0 (msg 0x44F1189 / 0x67B2A95, `ufo+0x7F0` set) → **Respawn 0x00C38420** (thiscall(ufo), `ret`): picks the owned star (cEmpire::GetStars) nearest to
   the active star (fallback home star), travels there with the §7.3 sequence (+ landing 0xFE0E00),
   message 0x6174B67, clears +0x7F0, `state = 0`, **`cCombatant::SetHealth` 0x00F92480**
   (`thiscall(combatant, float)`, `ret 4`, writes combatant+0x38 = ufo+0x540) with
   GetMaxHitPoints (combatant vt+0x58, ST0), energy +0x7F4 = max +0x7F8.

Healing (✔):
* **`cCombatant::AddHealth(float)` 0x00BFD2C0** (thiscall(combatant), `ret 4`): if amount > 0:
  HP += amount clamped to max, **state +0x34 (ufo+0x53C) := 0** (posts 0x1622184 if it changed).
  Not blocked by the invincible flag → the right call to revive a disabled ship.
* `TakeDamage(..., type 10)` heals too but is blocked by invincible +0x588 (checked in the base).
* Repair tools: `cRepairToolStrategy::OnHit` 0x0105B140 (thiscall(strategy, tool, pos, hitType, int),
  `ret 0x10`) heals the **owner** (tool+0x114) by `maxHP * tool->0xCE6AB0() * [0x13EB950]` through
  AddHealth; `cEnergyRepairToolStrategy::OnHit` 0x0105B0B0 refills energy via 0xC38B30;
  **`cRepairAOEToolStrategy::WhileFiring` 0x01057FF0** heals the owner and every UFO in range that
  belongs to the player empire or an ally (0xD01FE0) with `combatant->TakeDamage(amount, playerPol, 10,
  &zero 0x16E2AF4, nullptr)` (vt+0x18) + effect 0xC38B00(2,1). So a partner's Repair-AoE already
  "hits" our puppet on the host with type 10 → a natural rescue trigger.

Recommended hook (extend the existing TakeDamage hook in `squadron.cpp`):
* `this == localUFO+0x508`, `type != 10`, partner in the same system, not disabled:
  if `HP - dmg < 1.5259e-5` → call the original with `dmg = HP - 1` (or skip it) so HP stays ≥ 1;
  set disabled (20 s timer), `ufo+0x588 = 1` (no further damage), block FireTool (owner = local)
  and the controller Update 0x01062CF0 for the local UFO (zero `ctx+0..8`, return) so it drifts dead.
* Rescue: on partner Repair-AoE (puppet hit with type 10 on the host → message to the owner) or
  proximity: `ufo+0x588 = 0; AddHealth(ufo+0x508, max*0.5)`.
* Timeout: `ufo+0x588 = 0`, `SetHealth(ufo+0x508, 0)`, `ufo+0x53C = 2`, then call
  `0xC3C660(ufo)` → native cinematic and respawn at the nearest colony (also call
  `cSimulatorSpaceGame->0x1005020(10)` / event log only if you want the full native side effects ~).
* Alternative single hook: `0xC3C660` itself (skip while rescue is possible). Simpler, but by then
  state = 2, the death sound/event log already ran ~.

### 7.2.1 Death and respawn captions (✔ runtime, lane 4, 2026-10-22; test `coop_space_death.py`)

Found while fixing "after a lone death the respawn did not come within 60 s" (branch
fix/space-lone-death). Not a hook problem: the debug hit (attacker null) and the rescue timeout
(`end_down(false)` → PlayerDestroyed) behave exactly like a native death.
* `PlayerDestroyed` 0xC3C660 starts the cinematic `SPG_PlayerDestroyed_PrePosse_NotGalaxy` (in a
  system; `_Galaxy` in the galaxy view, `_PostPosse` with a posse) through
  `cCinematicManager::PlayCinematic` 0xAE0880 (manager `[0x167FB30]`, getter 0xB3D5A0), after
  `0xAE0900(mgr, 0x14A4C5F5, &params, 0)`. ufo+0x7F0 is the handle of the explosion effect
  (`0x67DD90()` vt+0x2C, effect 0x4D42643), not the cinematic itself.
* The cinematic shows the caption "Your <ship> has taken so much damage that it has been destroyed.
  Thanks to advances in cloning technology you have not died. Instead, you return to your
  homeworld, a new spaceship at the ready." with a green check button and **waits for the player**:
  nothing ends it on its own (60 s+ in tests). The check button or Enter posts 0x44EE075 to the
  cinematic manager → EndCinematic 0xADFF20 → message 0x44F1189 (data+8 = the ended cinematic id)
  → 0xC3B6A0 → Respawn. The old test that never pressed OK waited forever (test artefact); a player
  presses OK. Tests press it with `key enter` (works in background mode).
* 0xC3B6A0 re-posts 0x67B2A95 to itself instead of respawning while `cSimulatorSpaceGame+0x50`
  has any bit set (0xB18D50). Posting 0x67B2A95 (MessagePost, IMessageManager vt 0x18, 4 args) is
  the native way to ask for the respawn again.
* Respawn lands on the colony planet (0xFE0E00) and at once plays the respawn caption
  **CVG2SPG_short** ("We've built you a new ship, try not to break it this time!", from 0xFF5F66;
  `CVG2SPG` in the other branch). Its text appears after ~15-30 s and waits for OK too. Cinematic
  manager +0x2C = 2 while any cinematic plays, 0 otherwise.
* **The game fault behind a ship stuck dead:** the end of CVG2SPG(_short) is also handled by
  `cSimulatorSpaceGame` 0x100B730 (at 0x100C513): it sets player UFO +0x6B4/+0x6B8 = 1.0 and calls
  `[cSimulatorSpaceGame+0x54]->0xFF59D0` (`+0x1E4` → 0x7CFCC0). +0x54 is set on a planet and null in
  a system (~, runtime reads). If the ship leaves the planet while the respawn caption still plays,
  the end of that caption faults there (AV at 0x7CFCC0, data 0x1E4, stack holds 0x100C57F and
  044F1189), the dispatch of 0x44F1189 stops, and a ship that died in the meantime never gets its end
  message: dead (state 2, +0x7F0 set), no cinematic running, stuck forever. A death started while
  another cinematic plays does not replace the running id (+0x148), so the OK on the death caption
  ends the respawn caption. The mod's jump (`travel_to`) did exactly that: it only checked +0x7F0.
* Fix in `spacecoop.cpp`: no jump while `cCinematicManager+0x2C != 0`, and a watchdog: own ship in
  state 2 with +0x7F0 set but no cinematic running for 5 s → MessagePost 0x67B2A95 → native
  Respawn (`respawns_forced` in `coop.status`; the old path is reproduced by
  `coop.space jumpto <star> force`).
* **The fault itself, fixed natively (✔ runtime, lane 7, 2026-10-23, branch fix/space-death-fault):**
  0x7CFCC0 is a generic `thiscall(holder)` "clear a set" (`mov ecx,[ecx]; jmp 0x7CFC10`, the impl
  has vectors at +4/+8, +0x1C, +0x34, +0x4C, +0x64). The caller is
  `cSimPlanetHighLOD_EndRespawnCaption` **0xFF59D0** (`add ecx,0x1E4; jmp 0x7CFCC0`), the only two
  callers of which are 0x100C57A (end of CVG2SPG_short) and 0x100C5E7 (end of CVG2SPG), both with
  `ecx = [cSimulatorSpaceGame+0x54]` (mpHighLODPlanetSim) and no check. The set at sim+0x1E4 is filled
  by 0xFF5D90 (only caller 0xFDBBF8, after the respawn landing): it adds 0x9C377C26 there (0xFF5EE2)
  and plays the caption. Off the planet the sim (and the set) is gone: nothing to clear.
  `gamefixes.cpp` hooks 0xFF59D0 and skips it when `this` is null ("respawn caption without a planet
  N" in `coop.status`). The handler then returns normally, the dispatch of 0x44F1189 goes on, and the
  UFO handler 0xC3B6A0 respawns the dead ship natively 2 s after the OK (the watchdog does not fire:
  `respawns_forced=0`). Crash stack of the old fault: `0100C57F 007E30B6 00C60660 00922514 0088564C
  ...` (message dispatch). Before the fix the forced-jump step of `coop_space_death.py` faulted in 4
  of 4 runs (it cleared the fault and passed); now the test fails on any fault of either game.
* Calling Respawn 0xC38420 directly on a stuck ship also works (console `call`), but the message
  keeps the game's own "busy" check.
* Not tested: a lethal hit by a real NPC while alone (the debug hit has no attacker; the attacker
  only reaches the event log and 0x100E580).

### 7.3 Travel / "jump to partner"

| function | address | signature | notes |
|---|---|---|---|
| **`SpaceTeleportTo(cStarRecord*)`** | 0x010228C0 | cdecl, `ret` | ✔ the `space -flyto …` debug cheat target. **No-op unless context == 2 (galaxy)**. Refunds the travel cost (`cEmpire::SetMoney(GetMoney() + cost)`), then `cSimulatorPlayerUFO::TravelToStar` and `SnapToDestination` 0xC37FA0 → instant arrival in galaxy view |
| `cSimulatorPlayerUFO::TravelToStar(cStarRecord*)` | 0x01002040 | thiscall(playerUFOsingleton), ✔ | normal galaxy flight: cost = 0xFFC390(curRec, dstRec) (thiscall on player-UFO singleton, `ret 8`), "not enough money" msg 0x3E7E341, `AddMoney(-cost)`, sets active star via 0x10218A0(dst, 0), star+0x44 = 1 (visited) |
| **`cAppModeSpace::TravelToStarSystem(cStarRecord*, bool keepPosse)`** | 0x00FDD380 | thiscall(`[0x16DA918]`), `ret 8` ✔ | **system → other system** jump: `simSpaceGame->0x1009310(0, keepPosse, 0)` (destroy NPC UFOs, keep posse if true), `SetActiveStarAndPlanet(star, 0)`, tool reset 0x1048E10, picks a planet (0xFDD1F0: owned/colonised first) and makes it active, places the UFO next to it (0xC3C2D0 + 0xC38320). Callers: respawn (keepPosse 0), Return-home tool 0x1051D98 (1), wormhole 0xFDD4E0 (1) |
| `cAppModeSpace::EnterSolarSystem()` | 0x00FDD590 | thiscall, `ret` ✔ | galaxy → system of the active star (loads planets 0xBB5CE0, context 1) |
| `cAppModeSpace::LeavePlanet(bool)` | 0x00FDE3D0 | thiscall, `ret 4` ✔ | planet → system (sets context 1 via `simSpaceGame->0x10038F0`); respawn and Return-home pass 0 |
| respawn landing | 0x00FE0E00 | thiscall, `ret` ~ | only caller is respawn: lands on the active planet |
| cSimulatorSpaceGame context switches | 0x01003330 → 2, 0x01003530 → 0, 0x010038F0 → 1 | thiscall ✔ | low level, called by the mode functions above |
| `cSimulatorSpaceGame::DestroyNPCUFOs(keepAmbush, keepPosse, bool)` | 0x01009310 | thiscall `ret 0xC` ✔ | iterates +0x78, DestroyUFO 0x102C350 |

Native sequence (copied from Respawn 0xC38420, ✔), to put the player into star `dst`:
```
mode = *(void**)0x16DA918;
switch (GetCurrentContext()) {
case 0: thiscall(0xB7D650, *(void**)0x167FADC, GetActivePlanet());  // planet-model cleanup, ret 4
        thiscall(0xFDE3D0, mode, 0);                                 // leave planet -> system
        break;
case 2: thiscall(0xFDD590, mode);                                    // galaxy -> current system
        break;
}                                                                    // case 1: nothing
thiscall(0xFDD380, mode, dst, /*keepPosse*/ 1);                      // jump to the other system
```
Galaxy-only alternative: `SpaceTeleportTo(dst)` (free, instant), then optionally
`EnterSolarSystem()`. Partner's star: send `starRecord+0x70` (StarID), resolve with
`cStarManager::GetStarRecord` 0xBA6EB0 (same galaxy layout → same ids). Run it outside Present
(SK_CMD_OUTSIDE_FRAME). ? whether FDD380 is safe while a cinematic/comm screen is open - guard on
`ufo+0x7F0 == 0` and context ≠ −1.

### 7.4 Raids and plunder (builds on §6)

* All missions/events are nouns 0x2AA5ADA. Enumerate (✔): `vec = thiscall(0xB21260, nounMgr,
  0xCD7E80, 0xD3D3D0, 0xFECEA0, 0xB1E430, 0x2AA5ADA)`, iterate `[ret+4]..[ret+8]`.
* Mission ids (+0x78) handled by cRaidEvent::Update 0xC5E3E0: 0xA4B0A2DF (pirate raid),
  0xC4956AA7 (raid plunder), 0xD646C543, 0x77184BAE (? other raid-type events).
* **Raid plunder** = separate class `cRaidPlunderEvent` (✔): noun 0x397BFF3, Cast 0x397BFF2
  (0xC5B0C0, chains to cMission Cast, *not* to cRaidEvent), vtable 0x1470168: Update **0xC5DC60**,
  Accept 0xC5B260, Completed 0xC5B2E0, Failed 0xC5B300. Starter 0x010143E0 (`ret 8`,
  `(cPlanetRecord* colony, vector<cMission*>* out)`, event-table routine, no direct callers):
  CreateMission(0xC4956AA7, colony, owner of star, 0), Cast, fills +0x200/+0x204, **event log
  0x31DF9AC0 (table 0x131A9F54) = "Raid Plundering occurred"** via ShowEvent 0xDD8640,
  SetTargetPlanet 0xC472C0, Accept. Cheat variant 0x01012940 (`ret 8`, `raid -plunder`).
  Update: waits for the player (spawns ships via 0xC5D2C0 like the raid), while ships alive and
  before timeout (prop 0x2B9E524) steals spice from the target planet (0xC71150 with a negative
  amount ~) → 3; no ships left → event 0x25594EA8 + 5 (Completed = defended); timeout →
  0x102C0D0 (ships leave), event 0xB681FBA2, → 6 (Failed = plundered).
* State machine `cMissionManager::Update` 0x00FEF240 (thiscall, `ret 8`, ✔): per mission by
  `GetState` (vt+0x60 = `[+0x84]`): 2 → OnAccept (vt+0x88), SetState(3); **3 → `r = Update(dt)`
  (vt+0x7C); if r ≠ 3 → SetState(r)**; 5 → OnCompleted (vt+0x94) → SetState(9 if +0x17C else 1);
  6 → OnFailed (vt+0x98) → 1; 7 → Abort (vt+0x90) → 1; 8 → Reject (vt+0x8C) → 1; 1 → removed (0xFEDB30).
* Detect a raid on planet P (✔): mission with Cast(0x3960C0A) (raid) or Cast(0x397BFF2) (plunder),
  `[+0x84] == 3`, target `cPlanet*` `[+0x1E8]` (getter 0x970A90; record = `planet+0x13C`,
  PlanetID = `record+0x184`). Generic: `cMissionManager::ThereIsEventInPlanet` 0x00FEE020
  (thiscall(mgr 0xFEB880()), `cPlanetRecord*`) `ret 4` → any active mission with flag `+0x130 & 1`
  (event) whose vt+0xF4 (0xC45EC0: `target->+0x13C == rec`) matches.
* Start/end hooks: start = `cMissionManager::CreateMission` 0xFEC420 post-call (`ret 0x10`) with id
  0xA4B0A2DF / 0xC4956AA7; end = **`cMission::SetState` 0xC47E00** (thiscall(int) `ret 4`) with 5
  (defended) / 6 (lost/plundered) on a raid/plunder mission; or the class handlers 0xC5BB70 /
  0xC5BC60 (raid) and 0xC5B2E0 / 0xC5B300 (plunder).
* Co-op: the guest at the raided colony sees host-mirrored raid UFOs (they are normal NPC UFOs with
  `ufo+0x800 == mission`); a guest's colony raid can't be mirrored from the host's save (different
  colonies) - to let the partner help, the owner of the raided planet stays authoritative and the
  helper "jumps" there (§7.3); helper hits on raid UFOs go through the §2 host path only if the owner
  is the host ?.

### 7.5 Missions

cMission fields (✔ unless noted): `mMissionID` +0x78 (template prop instance), elapsed ms +0x7C,
state +0x84, giver empire id +0x8C / cached `cEmpire*` +0x90 (`GetOwnerEmpire` 0xC45320), target
empire id +0x94, source planet +0x9C, **reward money (float) +0xA0**, reward tool ResourceKey +0xA4,
extra reward tool keys vector<ResourceKey> +0x11C..+0x120, flags +0x130 (bit0 = event ~),
+0x17C next step (non-zero → state 9), props +0x180, `mpTargetPlanet` +0x1E8.
`IsActive` 0xC44DC0 (state == 3). States: 0 ?, 1 finished/removed, 2 accepted, 3 active, 4 ?,
5 completed, 6 failed, 7 aborted, 8 rejected, 9 completed-with-follow-up ~.
Wrappers (thiscall, `ret`): Accept 0xC486F0 (2), Reject 0xC48700 (8), Abort 0xC48710 (7),
**Complete 0xC48720 (5)**, **Fail 0xC48730 (6)**. Base vtable `cMission` 0x146DCC8 (Update +0x7C
0xC479F0, OnCompleted +0x94 **0xC46CC0**, OnFailed +0x98 0xC46B30).
Base OnCompleted pays: reward tools via `cPlayerInventory::AddItemByKey` 0xFF4490 (§7.7),
`cEmpire::AddMoney((int)+0xA0)` on the player empire, relationship +/− with the giver (0xD06280,
pol 0x526E519), stats/sounds.
Player's missions: enumerate mission nouns (§7.4), `state == 3 && !(flags & 1)`; ? which one the UI
tracks as "current".
Feasibility of "a mission taken by one counts for both": missions exist only in the taker's save
(own galaxy save per player), so the partner cannot complete *the same object*. Feasible variant:
hook `SetState` 0xC47E00 (value 5/6) or OnCompleted, send `{missionID, money +0xA0, tool keys,
giver pol}` and pay the partner with AddMoney / AddItemByKey (relationship only if political ids
match across saves - ? same galaxy seed should give the same ids). Objective sharing (e.g. partner
kills/scans counting) would need per-mission-class hooks - not recommended.

### 7.6 Terraforming

Tool effect = `cToolStrategy::OnHit` 0x010593B0 (✔ `ret 0x10`, 4 stack args
`(cSpaceToolData* tool, const Vector3* pos, int hitType, int)`, ecx unused). Per hitType 0..3 it reads
three float deltas from the tool (hitType 1: +0x26C/+0x270/+0x274, 2: +0x278.., 0: +0x284.., 3: +0x290..;
getters 0x104C050..0x104C100), then only in planet context posts kMsgToolOnHit 0x56690BB and, with
`tm = cTerraformingManager::Get()` 0xB3D500 (`[0x167FB10]`) and `p = GetActivePlanet()`:

| function | address | effect |
|---|---|---|
| `tm->AddAtmosphereDelta(cPlanet*, float)` ~name | **0x00BBC480** thiscall `ret 8` ✔ | `p+0x154` (terrain, null if not loaded) `+0x1FD4 += d`; the terrain update 0xC7FC40 integrates it into **record+0xB0** over time |
| `tm->AddWaterDelta(cPlanet*, float)` ~name | **0x00BBC4B0** thiscall `ret 8` ✔ | only if p is active and context 0: `cPlanet::SetScoreB8` 0xC71F20(Get 0xC71EB0 + d) → record+0xB8 clamped to the range from 0xFC20A0(B0,B4); `tm+0x64 = 0` |
| `tm->AddTemperatureDelta(cPlanet*, float)` ~name | **0x00BBC500** thiscall `ret 8` ✔ | terrain `+0x1FD8 += d` → record+0xB4 |
| `CalculateTScore(float b0, float b4)` | 0x00FC2040 cdecl → int 0..3 ✔ | distance from the centre; so +0xB0/+0xB4 are the two T-score axes (atmosphere/temperature [ModAPI]) |
| `tm->GetTScore(cPlanetRecord* or null=active)` | 0x00BBC7B0 thiscall `ret 4` ✔ | |

Recommended: hook the three appliers (one place for all terraform tools). When the local player
calls them on the active planet, send `{PlanetID, which, d}`; a partner on the same planet replays
with the same function (its own copy of the planet) → both planets move at the sum of both
players' rates. Optional bonus multiplier `d *= 1.5` while a partner is on the planet. Partner
elsewhere: apply to `GetPlanetRecord(id)+0xB0/+0xB4` directly (not loaded → no terrain object).

### 7.7 Inventory: tools, charges, spice cargo, money

`inv = *(cPlayerInventory**)(*(uint8_t**)0x16DD0DC + 0x40)` ✔. `cPlayerInventory` vtable 0x14958A0
(ctor 0xFF5040) derives from `cSpaceInventory` (vtable 0x14740B8, ctor 0xC87CA0): items
`vector<cSpaceInventoryItem*>` +0x34..+0x3C, max stack +0x48 (−1 = unlimited, setter vt+0x58).

| vt | address (player / base) | signature | conf |
|---|---|---|---|
| +0x5C | 0xC873C0 | `int GetItemCount()` | ✔ |
| +0x60 | 0xC873D0 | `cSpaceInventoryItem* GetItem(int index)` `ret 4` | ✔ |
| +0x64 | 0xC873F0 | `int FindItem(int itemType, const ResourceKey* key)` `ret 8`, −1 if none | ✔ |
| +0x68 | 0xC87170 | `GetItemByKey(key)` = type 2 (tool) lookup `ret 4` | ✔ |
| +0x70 / +0x74 | 0xC874A0 / 0xC871A0 | `bool HasItem(type, key)` / `HasTool(key)` | ✔ |
| +0x78 | 0xFF3FD0 / 0xC87F00 | `bool RemoveItem(cSpaceInventoryItem*)` `ret 4` | ✔ |
| +0x7C | 0xFF40B0 / 0xC87F70 | `bool TakeItem(int index, cSpaceInventoryItemPtr* out, int count)` `ret 0xC`: count −1 or ≥ stack → removes the whole item into *out; less → splits (`item+0x10 -= count`); tools with ammo move charges (ConsumeAmmo/AddAmmo) | ✔ (split branch ~) |
| +0x80 | 0xFF4780 / 0xC881E0 | **`bool AddItem(cSpaceInventoryItem* item, cSpaceInventoryItem* cargoSlot /*null = find free*/, bool apply)`** `ret 0xC`: same key+type stacks (`count += item.count`, capped by +0x48), cargo types 0/1/4/5/6 need a free cargo slot (type 3) else returns 0; tools: ammo merge | ✔ |
| +0x88 / +0x8C / +0x90 / +0x94 | 0xC875A0 / 0xC87550 / 0xC875D0 / 0xFF39E0 | cargo slots: count / free count / `GetFreeSlot(out*)` / `AddCargoSlots(n)` | ✔ |

`cSpaceInventoryItem` (size 0x7C, ctor 0xC877E0): +0x0C tool sub-object (vt+0x0C Cast(0x707459F0) →
`cSpaceToolData*`), **+0x10 count**, **+0x14 item type** (2 tool, 3 empty cargo slot, 4 trading
object / spice; 0/1/5/6 other cargo - plant/animal/rare ~), +0x18 price, +0x1C non-stackable,
key via vt+0x18 (returns `ResourceKey*`), vt+0x14 SetKey.

Tools (✔): `cPlayerInventory::GetTool(const ResourceKey*)` **0xFF3BF0** (thiscall `ret 4` →
`cSpaceToolData*` or null); `cSpaceToolData` charges **+0x7C** current (getter 0xA02C10), +0x1A0
max, +0x19C use cost, uses-ammo flag `+0x174 bit 5` (0x104CBD0); **AddAmmo(int)** 0x0104CBE0
(`ret 4`, clamps to max), **ConsumeAmmo(int)** 0x0104CC10 (`ret 4`, −1 = use cost, no clamp).
New tool: **`cPlayerInventory::AddItemByKey(const ResourceKey*)` 0xFF4490** (`ret 4`, loads via
`cToolManager::LoadTool` 0x104E350, AddItem(item,0,1), event log 0x15E1A624) - used by mission rewards.

Spice (✔ from `space -addCargo` and the colony-collect code 0x103E6F0): spice is a trading object
(type 4) keyed by the planet's spice key `record+0x198` (getter 0xB8DBB0). Add N:
```
cSpaceInventoryItem* it = nullptr;
thiscall(0x103A490, cSpaceTrading::Get() /*0xB3D4A0*/, &it, &spiceKey, N, 0.0f /*price*/); // CreateTradingObject, ret 0x10
vcall(inv, 0x80/4, it, nullptr, true);   // stacks onto existing spice of that key, else needs a free slot
it->Release();                            // vt+0x08
```
Remove N: `i = vcall(inv, 0x64/4, 4, &spiceKey); vcall(inv, 0x7C/4, i, &out, N); out->Release()`.
Read amount: `GetItem(i)->+0x10`.

Money (✔): `cEmpire` +0xD0 (int, 0..99 999 999): GetMoney 0xC30DF0, **AddMoney(int)** 0xC31B40
(`ret 4`, clamps), **SetMoney(int)** 0xC31BA0 (`ret 4`). Player empire = GetPlayerEmpire 0x10211B0.
Sending money/spice/tools between players = subtract locally with the functions above, send, add
on the receiver; all safe in any context (no planet needed).

### 7.8 Open (?)
* Exact meaning of OnHit hitType 0..3 and of record+0xB8 (water [ModAPI] ?).
* Whether `TravelToStarSystem` is safe during comm screens / cinematics; FE0E00 details.
* Mission state 0/4 meaning; how the UI picks the tracked mission.
* Political ids identical across two saves of the same galaxy (needed for shared relationships).

## 8. Loading a save while a stage runs (leaving space)

Found while fixing "loading a Cell save from the Space stage crashes" (branch fix/stage-switch-load,
2026-10-22; test `tools/tests/coop_stage_switch.py`).

### 8.1 The crash (✔ runtime + decompile)
* `cGamePersistenceManager::LoadGameByName` 0xB27FF0 → `SimulatorModeMgr_SetMode` 0xB1E320
  (`thiscall([0x167FACC], modeId)`, +0x20 current mode, +0x28 "switch pending") with kLoadGameMode
  0x1654C08 → the space app mode reacts in `cAppModeSpace_OnModeChange` 0xFDCED0
  (`thiscall(mode, oldMode, newMode) ret 8`; only does work when oldMode is Space 0x1654C05):
  * new = LoadGame → `cAppModeSpace_LeaveContext` 0xFDA9E0 then `cAppModeSpace_Teardown` 0xFDC140;
  * new = galaxy menu 0x2CCD1D2 → `cAppModeSpace_ExitToMenu` 0xFDC970 (LeaveContext, 0xB3D420,
    0xB8C1D0, Teardown), then the mode reset 0xB3D300(0xEBB801);
  * new = editor 0xDBDBA1 / 0x1654C06 / adventure 0x1654C10: own branches.
* LeaveContext switches on `GetCurrentContext` (SpacePlayerData+0x10): 0 planet → 0x100A800,
  1 system → 0x1009310(0,0,1) + `Space_LeaveSolarSystem` 0x1003860(`GetActiveStar()` 0x10210E0),
  2 galaxy → 0x1009310 + 0x10034C0, −1 → nothing; then `SetCurrentContext(-1)` + 0x10218A0(0,0).
  `Space_LeaveSolarSystem` (`thiscall(?, cStar*) ret 4`) calls `cStar::GetSolarSystem` 0xC8B8F0 on
  the star without a null check.
* On the way to kLoadGameMode SpacePlayerData+0x04 (planet) and +0x08 (star) are already 0 while
  +0x10 is still 1 → `GetSolarSystem(null)` AV at 0xC8B8F3 (data 0x40), then
  `cStar::IsStarOrBinaryStar` 0xC8B980 faults every frame (data 0x48) and the game hangs in
  LoadGame. The game's own `ReloadCurrentGame` 0xB27120 from Space crashes the same way (✔ runtime,
  no mod code involved). `ReadStarsFromSaveDB` while in space does **not** clear the active star
  (✔ runtime, negative result). Who clears +0x04/+0x08 before the handler: not found (?).
* Stack signature: `GetSolarSystem+0x3` ← 0x10038A6 ← 0xFDAA2F ← 0xFDD0CF, with 01654C08 01654C05
  (new, old mode) on the stack.
* It does not depend on the stage of the save: Space → Space crashes the same way (nightly chaos
  run, lane 1: a guest in Space left, joined again and loaded the re-sent world in game; reproduced
  on lane 5 with the pre-fix build by `coop_stage_switch.py --steps space,rejoin`). Both runs were in
  a solar system (context 1). Host `coop.load` space → space in warm resets did not crash in the lab;
  why is open (?) - perhaps another context (planet/galaxy) at that moment; `ReloadCurrentGame` in a
  system crashed (✔).

### 8.2 The native way out: quit message 0x4715068 (✔)
* `Simulator_HandleMessage` 0xB63610 (`thiscall(this, msgId, data) ret 8`), message 0x4715068:
  if a mode switch is pending ([0x167FACC]+0x28) it re-posts itself; otherwise, in Space on a planet
  (context 0) it first leaves the planet (`LeavePlanet` 0xFDE3D0), posts 0x689C9B9, calls
  `IStateManager::Get()`→vt 0x40 and sets +0x28/+0x29. Result: the game **saves** (a `GameN.old`
  backup appears) and goes to the galaxy menu (GalaxyGameEntry) in ~1 s from any stage. Posted with
  `MessagePost` (IMessageManager vt 0x18, 4 arguments) like `kMsgSaveGame`.
* From the menu, `world::load_now` (Close/SetLocation/Open of DB 0x4729A47, `ReadStarsFromSaveDB`,
  `LoadGameByName`) works for every stage. SporeCoop loads every save requested while a stage runs
  this way (`world.cpp load_slot`): host `coop.load`, guest received world (also Space → Space).
  A host reload of the same stage sends no world (coop.cpp `on_mode_change`: not a new stage); the
  guest keeps its own copy until it rejoins. Timing: menu after
  ~1 s, 3 s settle, host in the target stage ~5–6 s after the request, guest together ~16–18 s.
* Other in-game loads seen to go wrong (all avoided by the menu route): a guest in Cell loading a
  world in game crashed in `modes::cell_fill` (cell pool freed under the cached local cell, coop.dll);
  a Space save loaded in game from Cell kept `CurrentGameName` = "Karke.spo", so the next save wrote
  `Karke.spo` into the space slot and the guest got the wrong planet; tribe → civ in game worked.
* A save of ours made while the game switches modes does not load at all: the host's share save
  written while its editor opened (end of an evolution vote) kept every game that loaded it in
  LoadGame 0x1654C08 forever, frames running, no fault (docs/re/creature.md 9). `save_game` now
  waits while an editor opens or is open, a load runs or [0x167FACC]+0x28 (switch pending) is set,
  and outside a stage. Two saves at once break the slot too: our share save and the quit message's
  own save (a guest joined as the host left) gave a slot whose load faulted at 0xBA7F87 (data
  0x1188, from 0xB27DA6) and stayed in LoadGame; the quit message now waits for our save (a hook on
  0xB63610 posts it again, docs/re/creature.md 9).
* Loading 3 s after the menu came up from on_frame (inside Present): the guest twice got a
  first-chance AV in `cTerrainStateMgr_GetTemperatureScore` 0xFB89D0 (ecx 0; caller 0xF9E5EC
  `[esi+0xC]->vt 0x10()` returns a null terrain state, a planet render), once at the very load call
  with 1 s settle, once 1 s into the load; the game survived it. Not seen in 14 switches since the
  load runs outside the frame (`run_outside_frame`). Why: 8.3.

### 8.3 Planet texture jobs and loads from inside Present (✔ runtime + disassembly, 2026-10-23)

Found on branch fix/menu-load-crash (test `tools/tests/coop_rejoin_loop.py`).

* A terrain sphere (`Terrain::cTerrainSphere`, the planet of a stage and every planet the galaxy menu
  shows) draws its textures in **render jobs**, one cube face per frame: the job object
  `cTerrainSphereTextureJob` (vtable 0x14911FC, size 0x28, kept at sphere+0x8A0; +0xC the sphere,
  AddRef'd; +0x10 pass 0..3; +0x14 face 0..5; +0x20 a render helper, null = nothing to draw; +0x24
  set up) is queued by `QueueTextureJob` 0xFA0520 (`thiscall(sphere, face, pass) ret 8`) through the
  render manager ([0x15FE8B8], vtable 0x140DCB0) `AddRenderJob` vt 0x78 = 0x75F6E0 (list +0x330 under
  the lock +0x28, returns an id from the counter 0x1630C5C, kept at sphere+0x8A4). After the draw its
  completion calls `OnTextureJobDone` 0xFA8800, which resets the job (0xF99F90) and queues the next
  face / pass: 6 faces x 3 passes, then pass 3, about 19 frames per planet.
* `DrawLayer` 0xF9E490 (job vt slot 3, `thiscall(job, flags, layerIndex, viewers, engine) ret 0x10`)
  sets sphere+0x8A4 = -1 and reads the sphere without checks: the terrain state sphere+0x20C (sphere
  vt 0x10 = 0xFA0D20) and the face render targets sphere+0x148/+0x160 [face]. The sphere's dispose
  0xFA4880 (vtable 0x14916D8 slot +0x110) releases the job (+0x8A0 = 0), calls `RemoveRenderJob`
  vt 0x7C = 0x75F9A0 (by id, no lock) when +0x8A4 != -1, then frees the targets and the state.
* The render manager's frame 0x7604F0 first **takes** the queued jobs into a local list (0x76062D),
  then calls Present of the previous frame (0x760727 -> 0x11F8150: swap chain vt 0xC / device vt 0x44),
  then draws the taken jobs (0x7607A4 -> 0x75E490 -> DrawLayer) and the layers, and puts undrawn
  jobs back. Present is where SporeKit's frame event runs (coop `on_frame`). Anything that disposes a
  terrain sphere from there (a load: `LoadGameByName` leaves the galaxy menu and disposes its planets)
  cannot remove a job that the frame already took; the job is drawn right after Present with the
  state and targets gone: AV at 0xFB89D0 (temperature, pass 0..2), 0xF9E660 / 0xF9E67E / 0xF9E6FE (the
  face targets), all on the main thread, call chain 0x75E547 <- 0x7607A9. Seen in co-op as "a planet
  render faults while a received world loads" (guests with 3-4 players, 2026-10-22/23).
* Repro (old build): the guest leaves its stage for the galaxy menu (`coop.menu`) and loads a save
  with `coop.load` (a main-thread command: runs inside Present) 0-1.5 s after the menu came up, in a
  freshly started game: 2 of 10 fresh games faulted (0xF9E67E, 0xF9E6FE), 4 of 17 first trips to
  the menu in all; later trips in the same game hardly ever fault (1 hard crash without a logged
  fault in about 150 later trips, cause not known). The first trip after the join textures the
  menu's planets, later ones seem to reuse them (~). With the guard of `gamefixes.cpp` and the old load path, 5 of 12
  fresh games had a skipped draw (`job .. sphere .. state 00000000 targets 00000000/00000000 sphere
  job 00000000 id -1 tid <main>`: the sphere was disposed, the job had already been taken).
* What SporeCoop does: every load goes through `world.cpp` `via_menu_tick`: from the galaxy menu
  only after it has been up `kMenuSettle` (3 s) and always outside the frame (`run_outside_frame`:
  a posted window message, pumped outside the render manager's frame). Before, a world that arrived
  while the guest was in the menu (join from the menu, a rejoin during a load) and the host's
  `coop.load` (a main-thread command) loaded at once from inside Present. As a safety net
  `gamefixes.cpp` puts a guard into the job's vtable slot 3 that skips the draw while the state or a
  target is null (`coop.status`: "planet texture job guard on, skipped N"); the guard replaced the
  first fix (only the temperature call at 0xF9E5EC redirected), after which the same draw faulted at
  0xF9E660.
* Leaving a stage: the stage mode begins at once when a load starts (LoadGame 0x1654C08 lasts about
  0.1 s), its loading screen and setup run in the stage mode. The tribe game's state machine
  0xCDE7D0 (state at +0x22C, previous mode +0x230, timer +0x238) runs its setup 0xCD78E0 one step
  after the load (state 3 -> 4); the setup points a camera-like object of the simulator
  ([0x167FAAC], getter 0xB3D350, vtable 0x14672A8, made by 0xBC3830; null in the galaxy menu) at the
  tribe through 0xB12CE0 (`thiscall(obj, Vector3*, ?, bool) ret 0xC`, reads obj+0x2B4). A guest got
  an AV there with the object null (0xB12D32, data 0x2B4; stack: Vector3 on the stack, ebx = the
  tribe+0x120, saved esi 0: the caller is 0xCD7D0D) during a 4-player churn. Not reproduced on 2
  players (? which order of events); likely a quit or load while the stage was still setting up.
  `via_menu_tick` now leaves a stage only when the co-op adapter has had something to control for
  3 s (`kStageSettle`; a stage without anything to control is left after 20 s).
* Also found with the same test: two world transfers to the same player (a rejoin while the first
  one was still being sent) sent their chunks in turn; the guest wrote the old chunks into the new
  world and dropped it ("world transfer failed: checksum mismatch", 3 of 60 rounds of
  `--reshare 0:2.5`). A newer transfer now replaces the running one (`share_now`).
* Seen as a desync: the same fault hit guests that join **from the galaxy menu** (an empty profile: the world
  arrives in `on_frame`, and `load_slot` called `load_now` right there, inside Present): 3 of 3 cold
  4-player sessions (2026-10-23, `coop_npc_multi.py`) had one lane-B guest with a first-chance AV in
  the terrain render pass of the menu's planet view 3 ms after `LoadGameByName`: 0xF9E67E or
  0xF9E6FE (`mov reg,[ecx]` with ecx = `terrainSphere+0x160+i*4`, a null RTT; same function as the
  0xF9E5EC call). The game survived, but that guest's planet came up **without its height map**: its
  ground lay ~6 m under the host's (its own avatar stood 6 m low), NPC proxies placed at the host's
  heights fell through to it (radial error only) and its screen kept showing the menu picture.
  Since `load_slot` sends this case through `run_outside_frame` too: 0 faults in 7 sessions.
  Check for a guest with a broken planet: compare `|pos|` of its own avatar with the host's copy
  (`coop.sync`), or `exc`.

## 9. Navigation, tools and what the game frees (✔ runtime, play_space.py, lane 7, 2026-10-23)

Found while writing the game bot `tools/tests/play_space.py` (players fly with real input).

### 9.1 Where things are on the screen
* **Planets of the current system**: `cStarManager` (`cStarManager::Get` 0xB3D370) **+0x50..+0x54** is
  the vector of the loaded `cPlanet*` (the list `RecordToPlanet` 0xBB5B20 searches, comparing
  `planet+0x13C` with the record). `cPlanet` **+0x04** = position (system coordinates; planets orbit,
  so it changes), +0x10 orientation, +0x13C record (+0x184 PlanetID, +0x194 tech level, 5 = colony),
  +0xD4 the cGameData base (vtable 0x1473038, GetNounID 0xC70100 returns **0x3275728**; the planets are
  not in the GetData noun list). In a system the list held 6 planets, one of them of another star
  (`P01B16013`).
* **Stars in the galaxy view**: `cStarManager` **+0xC8** = vector of 20-byte sectors whose first dword
  is an array of `cStarRecord*` (`GetStarRecord` 0xBA6EB0: StarID = sector << 12 | index).
  `cStarRecord` **+0x3C** position (galaxy coordinates, the player UFO's position in the galaxy view
  is in the same space; neighbouring stars are ~1 unit apart), +0x54 empire id (the player's political id for own stars), +0x70 StarID.
* `coop.targets all` prints them (`obj P/C<PlanetID>` in a system, `obj S/H<StarID>` in the galaxy
  view) and `far player <id> x y` for a partner off the screen (its projected point).

### 9.2 Input (1440x1080 test window)
* Planet view: W/A/S/D (or the arrows) fly the ship relative to the screen (W = up); a left click on
  the ground with no tool selected flies there; a click on the minimap flies to that place (a city on
  the minimap: the ship goes there).
* Solar system view: the keys barely move the ship; a **click on a planet** flies there and makes it
  the active planet (`coop.space star` prints `active <PlanetID>`; a moon next to a planet often
  takes the click). Zooming in (wheel) at the ship lands on the active planet (a short cinematic);
  zooming out of a planet goes back to the system. Clicks on empty space do nothing.
* Galaxy view: zooming out of a system (wheel, or the zoom buttons at the bottom left, "Zoom Out"
  at (18, H-60)) only works with the **interstellar drive**: without it the camera stops at the
  edge of the system. The space fixture's ship does not have it yet (a new player gets it in the
  first missions: fly to Dunoselka, scan the wreck).
* Galaxy view: a click on a star in the drive's range (the circle) flies there at once (no money
  spent in this save); zooming in enters its system. Neighbouring stars are ~1 galaxy unit apart.
* Tool palettes (bottom right, anchored to the right edge): category tabs at x = W-426 main, W-387
  combat, W-347 gear (scan, abduct), W-306 colony, then three terraforming tabs and cargo; y = H-51 while the palette is
  closed, H-151 while open (a click on a closed tab opens it). Keys 1..9 select the slot of the open
  palette. Esc deselects the tool; a second Esc opens the pause menu ("Resume Game").
* Clicking an own city (planet view) opens the city planner (buildings for money, done at the
  bottom right check button).
* Captions that wait for OK (6.9 in docs/06-testing.md): also the tutorial captions ("Something is
  showing up on the ship's radar...") and badges ("title of Commander"); Enter confirms them.

### 9.3 Tools and cheats
* Tool keys in the inventory are `{FNV-1 hash of the tool file name, 0, 0}`: scan 0x08EF6A62, abduct
  0xF58C1654, interplanetarydrive 0x4133FE05, **interstellardrive 0x2569FAE8** (not in the fixture).
  `cPlayerInventory::AddItemByKey` with such a key adds the tool (`coop.space tool add <name>`); the
  interstellar drive at once unlocks the galaxy view (a badge cinematic and a tutorial caption
  follow).
* The release build registers only some cheats: `moreMoney` (+1 000 000) works, the developer
  cheats `space`, `raid`, `tool` (with their help texts in the exe) do not exist (ProcessLine false).

### 9.4 The game frees NPC UFOs, puppets included
* Leaving a planet (zoom out to the system) frees the UFOs of the planet, also the mod's Defender
  puppets of the partners there and the guest's mirrors; it happened ~10 times in 8 minutes of play.
  Any later use of the pointer is a use after free (the host crashed in `modes::space_drive`, vtable
  overwritten). Only a pointer still in the UFO noun list (`GetAllUFOs` 0xAD48B0) may be touched
  (`game::ufo_listed`); a listed pointer can be a new UFO at a reused address (check the type).
* The partners' ships are Defender UFOs (type 6): their own AI fires at every hostile ship in range
  (`cCombatSimulator::Update` -> FireTool with the puppet as owner, ~10 calls per second with
  pirates around). Pirates near a guest's puppet died on the host while the guest never fired; the
  mod drops FireTool for puppets (`puppet_shots_blocked` in `coop.fleet list`). (~) The player's own
  ship may also shoot back at an attacker on its own (TakeDamage step 9, auto-retaliation).
* Pirates spawned with `SpawnUFO`: type 2 (raider) attack the player only around its colonies; on
  other planets they fly off and vanish after ~30 s without a shot. Type 8 (ambush pirate) attack
  the player anywhere (✔ runtime, `coop.fleet spawn <type>`).
* Right after landing the player's ship takes no damage for a few seconds (the planet
  invulnerability counter `cSimulatorPlayerUFO` +0x24, §7.2) ~.

### 9.5 Rescue in real fights (✔, `coop_space_rescue.py`)
* NPC hits on a guest arrive as MSG_UFO_DAMAGE (the host's TakeDamage on the guest's puppet). The
  guest applied them with the original TakeDamage, past the rescue filter: a guest next to its
  partner was never disabled by pirates, it died. Now the hook applies them (`g_applying`: only the
  own-ship filter, then the game).
* The filter let a ship with HP <= 1 die at the next hit (small hits can leave 0.5 HP); now any
  lethal hit next to a partner disables the ship (60 s cooldown after a rescue as before).

## 10. Co-op contracts (✔ runtime, lane 3, GOG 2016, 2026-10-23; `space_contracts.cpp`)

### 10.1 Star records: name, type, planets
* `cStarRecord` **+0x60 `mName`** (eastl::string16: +0x60 begin, +0x64 end, UTF-16) ✔ runtime: the
  fixture's home star 0x460035 reads "Fibucius", its neighbours "Argis-2", "Gerbus", "Aspytory" (the
  names the galaxy view shows). +0x48 `StarType` (ModAPI: 1 galactic core, 2 black hole, 3
  proto-planetary disk, 4.. real stars), +0xAC planet count (byte), +0x84 `vector<cPlanetRecord*>`
  (ModAPI layout, not needed so far). Ids from the network: `spacecoop::star_record` (the table
  lookup; `GetStarRecord` faults on an id that is not a star).
* `cEmpire` **+0x88** = `vector<cStarRecord*>` of the empire's stars: `GetStars` is `lea eax,[ecx+0x88];
  ret` at 0x00C30A10 (GOG 2016) and 0x00C30A30 (GOG 2024) ✔ objdump of both exes. The mod reads the
  field directly (no symbol).
* Neighbour stars: the sector table of §9.1 (`cStarManager` +0xC8), sorted by the distance of +0x3C.
  Neighbours were 0.3 to 1 galaxy units away; `TravelToStarSystem` (§7.3, `coop.space jumpto`)
  reaches any of them without the interstellar drive and without money.

### 10.2 Spawning an armada
* `SpawnUFO(8 /*AmbushPirate*/, &pol /*0xFFFFFFFF: no empire*/, nullptr)` ✔ objdump + runtime: with a
  null model key and a type != 0 SpawnUFO uses the default key {0xCB89C3D6, 0x476A98C7, [0x15B8504]}
  (0x102AD33..0x102AD56); the ships look like ordinary pirates. Teleport (loco vt +0x44) puts them
  around the host's ship (60 m ring, 15 m above), in a system and on a planet.
* Type 8 has 100 HP in this save (`coop.fleet list`); the armada sets max HP and HP to 150 right
  after the spawn, and the guests' mirrors (squadron.cpp) show the host's HP.
* Waves: crew + wave number ships (two players: 3, then 4), 8 s between the waves. A wave comes only
  while the host and another member of the crew are in the same place (`modes` place: the system or
  one planet) of the contract's star for 3 s, because the host's NPC ships are mirrored only to
  guests in the host's place.
* Tracking: the host keeps the pointers of the wave and checks them every 0.25 s against
  `GetAllUFOs` (§9.4): listed with state != 2, HP > 0 and type 8 = alive; otherwise (also: gone from
  the list while the host is still in the same place) = destroyed. A place change of the host drops
  the pointers (the game frees NPC UFOs on every context switch) and the rest of the wave comes back
  when the crew is together there again.

### 10.3 Rewards
* Money: `cEmpire::AddMoney` on the host only (shared budget, shared.cpp gives the total to every
  guest within a second ✔ runtime: 100 000 -> 115 000 -> 140 000 on both games); in rivals mode
  every member of the crew adds it locally.
* Tools: `cPlayerInventory::AddItemByKey` with {FNV-1 of the file name, type, group of a tool in the
  inventory} (§9.3), on every member of the crew; `HasTool` (vt +0x74) after it is true ✔ runtime.
  File names in `spacetools~` (0x30608F0B), hashes checked against the index of the game's packages
  (Spore_Game / EP1 .package): laserlevel1..3 0x6DB01899.., pulselevel1..3 0x7B76E38F / 0x7B76E38C /
  0x7B76E38D, protonmissilelevel1..3 0xFC220278 / 0xFC22027B / 0xFC22027A, antimattermissile
  0xF06A8152, shield 0x4542380A, cloak 0x44808DE7, atmospheregenerator 0x931426A0, gravitationwave
  0xA43B6F38, monolith 0x14A7D231, allylaser1..3 0x47E65D39.., allymissile1..3 0x0068DF06 /
  0x0068DF05 / 0x0068DF04 (378 resources in the group; the other names are not known).
