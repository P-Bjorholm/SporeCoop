# Cell stage RE notes (GOG 2016 build, sha256 922af7c7…)

Static analysis only (objdump + Ghidra on a windowed copy 0xE40000–0xE90000). All addresses are VAs of
this build. "✔" = verified in this build's disassembly (convention checked at call sites / `ret`),
"~" = behaviour inferred from decompilation, "?" = unverified guess.

Globals: `G = *(uint8_t**)0x016B4BE4` (cCellGame, ✔). Serializable data `S = *(uint8_t**)(G+0x5190)`
(same object as `*(void**)0x016B4DC0` = cCellSerializableData::_ptr, ✔ — ModAPI m17 0x16B3DE0 + 0xFE0).
cCellUI `*(void**)0x016B4BEC`, cCellGFX `*(void**)0x016B4BE8` (already in overrides).

## 0. Object pool (cObjectPool, used for cells G+0x1C and the event pool G+0x54) ✔

Layout: `+0 data, +4 freeHead, +8 generation, +0xC liveCount, +0x10 highWater (slots ever used),
+0x14 objSize (0x398 for cells), +0x18 capacity`. Index = `slot | gen<<16`; a live object's first
dword == its index; free stores the old free-head slot in the first dword.

| addr | what | conv |
|---|---|---|
| 0xB72220 | `Allocate()` → index, memsets object, writes index to +0 | thiscall(pool) |
| 0xB72290 | `GetIfNotDeleted(idx)` → ptr/0 | thiscall, `ret 4` |
| 0xB722D0 | `Get(idx)` (no check) | thiscall, `ret 4` |
| 0xB72320 | `Free(idx)` (raw — does NOT clean GFX/query, never call for cells) | thiscall, `ret 4` |
| 0xB722F0 / 0xB721E0 | iterator next (alive test: `(first & 0xFFFF) == slot`) | thiscall |

## 1. Enumerate and classify cells

Enumeration (✔, same test as the game's iterator 0xB721E0):
```
pool = G+0x1C; for slot in [0, *(u32*)(pool+0x10)): c = data + slot*0x398;
  alive = (*(u32*)c & 0xFFFF) == slot; idx = *(u32*)c
```
Radius query: `int __cdecl FindCellsInRadius(query, const Vec3* pos, float r, u32* outIdx, int max, void* 0)`
0xE87210 ✔ (used so by 0xE71F00).

Per-cell fields (cCellObjectData, 0x398) confirmed in this build:

| off | field | evidence |
|---|---|---|
| +0x000 | own pool index | pool |
| +0x004 | u8 mIsIdle (steering off) | 0xE6EEC0 |
| +0x008 | Vec3 mTargetPosition | 0xE6EEC0, 0xE6F5B0 |
| +0x014 | Quat mTargetOrientation | CreateCellObject |
| +0x048 | Transform: u16 flags (\|4 = offset dirty, \|2 = rot dirty), u16 change counter +0x4A, offset +0x4C, scale +0x58, rot 3x3 +0x5C | 0xE7C080, 0xE5A450, 0xE74A20 |
| +0x080 | float elevation (CreateCellObject arg3) | 0xE74A20 |
| +0x084 | Vec3 previous position | 0xE7C080 |
| +0x090 | Vec3 velocity (integrated into pos each frame) | 0xE7C080 |
| +0x0A0/+0x0A4 | opacity / target opacity (opacity 0 only hides) | 0xE7BFB0 |
| +0x0AC/+0x0B4 | size smoothing rate / mTargetSize; **size→0 auto-destroys the cell** | 0xE7BFB0 |
| +0x0FC | ResourceKey mModelKey {instance, type, group}; instance≠0 ⇒ creature model | 0xE74A20 (GetModelKey) |
| +0x108 | cCellDataReference* mCellResource (its +0 = .cell instanceID) | many |
| +0x110 | u8 "player-owned": kills by this attacker count as player kills | 0xE7A7C0, 0xE7A8AF |
| +0x111 | u8 mIsInvulnerable (debug key toggles it) | 0xE7A7F5, 0xE8173B |
| +0x112 | u8 dead/consumed (set by kill event 6 → 0xE59170; ShouldNotAttack/IsDamageable test it) | ✔ |
| +0x113 | u8 dying (set by KillCell/PlayerDeath, blocks second kill) | 0xE7A4A0 |
| +0x16C | u8 **AI lock**: when ≠0 the AI dispatcher skips all behaviour (see §2) | 0xE7A218 |
| +0x178..+0x189 | u8 animation/interaction locks (set together with +0x16C by death/mating/cinematics) | 0xE59190… |
| +0x18C | int animation/state id (7 = dying, 10 = dead-anim) | 0xE59190, 0xE7A209 |
| +0x1B0 | current animation | PlayAnimation callers |
| +0x1C4 | Vec3 acceleration (accumulated by steering, cleared each frame) | 0xE6EEC0, 0xE7C8C0 |
| +0x220/+0x21C | flee target idx / flee time | 0xE71F00 |
| +0x22C | chase target idx | ModAPI, used by modes.cpp |
| +0x244 | int hp 0..6 | ✔ |
| +0x248 | GFX object index (0 = not created yet; created lazily when visible) | 0xE78C00 |
| +0x354 | u8 keep-alive: not destroyed when outside the active tiles | 0xE7C080, 0xE771D0 |
| +0x358 | int mScale level (-1 = player/puppet; never culled) | 0xE5F8D0 |
| +0x35C | query: `G+0x40FC` = foreground (gameplay), `G+0x4100` = background decoration | 0xE74A20, 0xE7C8C0 |
| +0x360/+0x364 | query handle / collision-pair handle (G+0x4104) | 0xE780A0 |

Resource data (`cCellCellResource`, ModAPI layout matches): get it with
`void* __cdecl GetData(ref, void** holder)` = **0xE823A0** (✔; 0xE4CC30 is a jmp thunk to it). It
writes `*holder = ref` and increments `ref+0xC`; release with `--*(int*)(ref+0xC)` (game uses 0xE82130,
thiscall on &holder). NOTE: symbol `Simulator::Cell::GetData=0xE4CC00` in gog2016 sym is wrong — that
is a stdcall string→hash helper.
Used fields: `+0xB0 flags, +0xB4 cellType, +0xB8 unlockType, +0x2FC friendGroup, +0x300/+0x301
wontAttackPlayer(/WhenSmall), +0x30C eat.foodValue, +0x310 eat.hpValue`. AI block by difficulty
`d = *(int*)(S+0x7C)`: d==0 → `res+0x248` (easy) if its type ≠ -1, d==2 → `res+0x194` (hard) if ≠ -1,
else `res+0xE0`; `ai.type` is its first int (0 = none/player, 0x1001.. behaviours).

Classification recipe:
- skip if `*(void**)(c+0x35C) == *(void**)(G+0x4100)` (background layer);
- avatar: `idx == *(u32*)(G+0x411C)`;
- cellType (`res+0xB4`): 2 = "solid" (creatures and meat; only type 2 is damageable, 0xE52950),
  3 = plant food (counts as plant food in eating), 1 rock, 4 air/bubble, 5 poison, 6 unlockable part
  (`res+0xB8` = part id, auto-removed once unlocked), 7 nanite;
- NPC creature: cellType 2 and `ai.type != 0` (and normally modelKey.instance ≠ 0);
- meat chunk / food: no AI (`ai.type == 0`), cellType 2 or 3.

Network identity of a species: `instanceID = *(u32*)*(void**)(c+0x108)` (the .cell file) **plus**
`modelKey` (+0xFC, 3×u32 — RandomCreature cells pick a random model per spawn) plus level `+0x358`.
Host pool index (`*(u32*)c`) is a stable net id for the cell's lifetime.

## 2. Per-frame update and how to make a cell passive

Frame: `cCellModeStrategy::Update` 0xE80980 → `CellFrame(dt…)` 0xE806B0 (✔): MovePlayerToMousePosition
0xE5B7B0 → `SimUpdate` 0xE7F3A0 → (if `G+0x51D8`) RebuildLevel 0xE7FD00 → GFX 0xE73F60 → UI 0xE600B0.
SimUpdate 0xE7F3A0 (~): … `PopulateTiles` 0xE7D370(level, G+0x4128) → events 0xE7E7A0 →
**`UpdateCells` 0xE7C8C0** → 0xE5AF50 → 0xE7AD70 → `Collisions` 0xE7F260 → …

UpdateCells 0xE7C8C0 per foreground cell: GFX/visibility 0xE78C00(cell,res) → **AI 0xE7A190** →
(if still alive) anim 0xE6D340, soft-body 0xE5A450, size/opacity 0xE7BFB0 (destroys when size→0),
physics 0xE7C080 (vel += accel·dt, pos += vel·dt, re-insert into query, destroy if outside active
tiles and `+0x354==0`), clear accel.

**AI dispatcher 0xE7A190** ✔ custom convention: `ECX = cell, EAX = resource data, [esp+4] = float dt`,
plain `ret` (caller `add esp,4`). Body: run if `ai.type != 0 || cell is avatar`; timers + AoE
0xE76A80; if `+0x112` → state 10; **if `byte[cell+0x16C] != 0` → return** (no steering, no chase/flee,
no behaviour); avatar → 0xE6F5B0 (click-target + steering); else switch ai.type → behaviour
(0x1001 0xE71300, 0x1003 0xE70650, 0x1004 0xE704B0, 0x1005 0xE702D0, 0x1006 0xE7A0A0, 0x1007 0xE6F990,
0x1008 0xE78FC0, 0x1009 0xE707D0, 0x100B 0xE6FBB0, 0x100C 0xE6FD70, 0x100D 0xE6FCE0, 0x100E 0xE7A0D0).
Steering used by behaviours and the avatar: `SwimTowardsTarget` 0xE6EEC0 (`ESI = cell`, stack:
`float dt, float maxSpeed, float accel(1.0), int, int 0`, caller pops 0x14) — reads +4/+8.

Passive mirror (two options):
- **A (no code patch, ✔ by static reading):** `byte[c+0x16C] = 1`. Only reader in cell code is
  0xE7A218; no code that clears it was found (all writers set 1). Still runs 0xE76A80 (timers and
  electric/poison AoE 0xE71980 → 0xE71520), anim, physics, collisions.
- **B (strict):** hook 0xE7A190 with a naked stub (`push eax; push ecx; call is_mirror; …; pop eax`
  — EAX must be preserved) and `ret` for mirrors.
Collisions (0xE7F260 → 0xE7F140 → interaction 0xE7E7F0) still run for passive cells: a mirror can
still bite/zap the local avatar on contact and the local avatar can eat/bite it — handled in §3/§5.

Moving a mirror: set velocity `+0x90` (dead reckoning happens in 0xE7C080), add a correction
`(target-pos)*k` to it, snap `+0x4C` (and `+0x84`) when far, then `*(u16*)(c+0x48) |= 4;
++*(u16*)(c+0x4A)`. Rotation: write 3×3 at +0x5C, `|= 2`, counter++ (as 0xE5A450 does). Keep
`+0xB4`/`+0x58` = host size, `+0xA4` opacity, `+0x354 = 1` (we own the lifetime).

## 3. Damage, eating, death

| addr | signature (all cdecl unless noted) | notes |
|---|---|---|
| 0xE57360 | `int GetScaleDifferenceWithPlayer(cell*)` 0..4 | ✔ |
| 0xE57480 | `bool ShouldNotAttack(cell* a, cell* b)` | ✔ same .cell ref, +0x112, wontAttackPlayer, friendGroup |
| 0xE589A0 | `int GetDamageAmount(cell* attacker, cell* victim)` | ✔ depends only on scale diff/avatar/difficulty; NPC↔NPC table 5×5 |
| 0xE52950 | `bool IsDamageable()` **ESI = cell** | ✔ ai.type≠0 or avatar, hp≠0, +0x112≠1, cellType==2 |
| **0xE7A7C0** | `bool DamageCell(cell* victim, cell* attacker, const float* dir, int dmgType, int, float dt)` | ✔ checks IsDamageable, ShouldNotAttack, +0x17C, +0x111; `hp -= GetDamageAmount(attacker,victim)` (clamped ≥0); health ring 0xE62350 if avatar involved; AI reactions 0xE57930/0xE57710; hp==0 → KillCell; else hurt anim (types 1/3/5 → anim 0xE/0xF/0x11, else 0x10) |
| 0xE7A770 | `void KillIfDead(u32 victimIdx, u32 killerIdx, ref* killerRes, bool byPlayer, int dmgType, float)` | ✔ |
| **0xE7A4A0** | `bool KillCell(cell*, u32 killerIdx, ref* killerRes, bool byPlayer, int dmgType, float dt)` | ✔ 6 args. NPC: +0x113=1, death effect via InstanceEffectOnCell, alert same species 0xE71F00, if byPlayer telemetry + `S+0x6C` killCount++, loot (0xE52A30/0xE771D0) if level within ±1 of player, anims 10/7, event 6 (→ +0x112), locks 0xE59190. Avatar → PlayerDeath (unless `G+0x5169` or state `G+0x5158`∈{3,5}) |
| 0xE72060 | `bool PlayerDeath(cell*, u32 killerIdx, ref*, float)` | ✔ +0x113, UI `cCellUI+0x936=1`, telemetry 0xE5C900, anims 10/7, event 0x1D in pool G+0x54 (index stored at `G+0x5194`), locks +0x16C/+0x178/+0x17C, state 7 |
| 0xE7AEE0 | `void BreakOrSwallow(cell* victim, cell* attacker, float)` | ✔ effect "cell_death_break"; avatar victim: `S+0x70` deathCount++; event 5 → 0xE7ADD0 (loot; avatar → 0xE72200 respawn request; NPC → DestroyCell(idx,1,…)) |
| 0xE7B0A0 | `bool Bite(u32 attackerIdx, float* dir, ?, float)` | ~ DamageCell type 4, or BreakOrSwallow when victim is ≥3 levels smaller and would die |
| 0xE7E7F0 | collision interaction executor (switch on kind) | ~ |
| 0xE62350 | `void ShowHealthRollover(cell*, int oldHp)` | ✔ needs GFX (+0x248≠0) |
| 0xE66850 | `IVisualEffect* InstanceEffectOnCell(cell*, u32 effectId)` | ✔ ids: `0x932E50(name, 0x811C9DC5, 1)` (FNV) e.g. "cell_death_oneshot(_small/_large)" |

Eating (events posted on mouth contact, processed by 0xE7E130):
- event 0x12 → `EatFood(eaterIdx, foodIdx, foodRef, float)` 0xE7DF40 ✔: OnEat, then the food shrinks one
  level or is destroyed (`DestroyCell(food,1,0,0)`).
- events 0x13/0x14 → 0xE7DD90 / 0xE7DBD0: eat a chunk of a creature: OnEat + damage + KillIfDead(type 6).
- `OnEat` 0xE7D760: **ESI = eater, EDI = foodType, stack (ref* food, float)** — avatar → AvatarEat,
  NPC → growth 0xE79320.
- `AvatarEat` 0xE7D070 ✔: **EAX = ref* food**, stack `(int foodType, float)`: heals
  `hp += eat.hpValue` (×2 on easy; +2 if no resource), clamp 6, rollover; points
  `AddFoodPoints(eat.foodValue or 10, isPlant)`; isPlant = cellType==3 (or foodType 3 when flags&1);
  mission/mate-button bookkeeping (`S+0x69`, `S+0x80/0x84`).

Health: max 6. **There is no passive regeneration** — the only writers that raise +0x244 are
AvatarEat, CreateCellObject (=6) and the cheat `RefillAvatarHealth` 0xE5C580 (void, sets avatar hp 6).

Avatar death outcome (✔ flow): both PlayerDeath and 0xE72200 end in `RequestRespawn` 0xE50930
(`G+0x51D8 = 1`, `G+0x51D9 = flag`, UI reset) → next CellFrame calls **RebuildLevel 0xE7FD00**
`(foodProg, plantProg, overPlant, overAnimal, byte G+0x410C, G+0x4110, 0)`: destroys every cell,
clears the tile list, recreates the world and the avatar (0xE78B80/0xE78B20) at the **same food
progression** (no DNA lost), then starts the intro/hatch sequence 0xE6ECB0 / 0xE6EB60 (? egg).
In co-op this wipes all mirrors/puppets on that machine.

## 4. Progress (DNA/food bar)

`S` (cCellSerializableData, ModAPI layout ✔ for these): `+0x1C foodProgression 0..1000`,
`+0x20 plant food`, `+0x24 over-plant`, `+0x28 over-animal` (points above the 1000 cap),
`+0x34 unlockedParts[13]`, `+0x68/+0x69` cinematic/mate-button flags, `+0x6C killCount`,
`+0x70 deathCount`, `+0x74 gameTime ms`, `+0x7C difficulty (0 easy,1 normal,2 hard)`.

**`void __cdecl AddFoodPoints(int amount, bool isPlant)` 0xE7CE10** ✔ (debug keys call it with ±50 at
0xE81762/0xE8178D; negative works). Clamps to 1000 (0x14846AC), updates plant/over counters, and on
change runs level-up/down (zoom 0xE72B40 / 0xE57190, repopulate 0xE7CD10) and the 1000 end trigger
0xE6E8C0. Level table: entries of 0x1C bytes at 0x14846B0 `{playerLevel, foodThreshold, …, float size}`;
thresholds every 50 points (0,50,100,…).

## 5. Removing / creating cells

- **`void __cdecl DestroyCell(u32 idx, bool spawnLoot, float, int)` 0xE780A0** ✔ — the game's own
  removal: clears avatar index if it was the avatar, removes from query (0xE86930) and collision pairs,
  frees GFX (0xE66020), detaches links (+0x370), optional loot/remains (spawnLoot=1 → 0xE67890 + 0xE771D0),
  then pool Free. Use `DestroyCell(idx, 0, 0.f, 0)` for mirrors. Never on the avatar.
  (Setting hp=0 does NOT remove a cell; `mTargetSize(+0xB4)=0` makes it shrink and auto-destroy.)
- **CreateCellObject 0xE74A20** ✔ cdecl, returns index:
  `(query*, const Vec3* pos, float elevation, ref* res, int level, float sizeFactor(→+0xB4),
  float cellSize (0 = auto from level/res), bool applySize, const Quat* orient (null = random),
  int collisionGroup (-1 = auto: own index, -5 for poison/nanite))`. Natural spawns pass
  `(…, 1.0, 0.0, 1, &identity|null, -1)`. Sets hp 6, GFX is created later by 0xE78C00.
- Resource ref by .cell instance id: **`ref* __cdecl cCellDataReference_::Create(u32 instanceId,
  CellSerializer* ser)` 0xE82420** ✔ (dedups: returns the existing ref). Cell serializer =
  **0x016AE490** ✔ (from the static ref table 0x15A8584; others: globals 0x016B1B3C, world 0x016AE3B0,
  loot table 0x016B2000). Model: force `modelKey` via the existing GetModelKeyForCellResource hook
  (0xE65650) while calling CreateCellObject.
- Population: `PopulateTiles` 0xE7D370 → `PopulateTile` 0xE7C6C0(tile*, …) → **`PopulateMarker`
  0xE7C500** ✔ cdecl `(float prob, int layer, int level, query*, bbox*, markerRef, int)` (recursive).
  Skipping it when `query == *(void**)(G+0x40FC)` suppresses all gameplay-layer spawns (creatures,
  food, parts) while keeping the background. Other spawn sources: loot on death/break (0xE771D0 via
  DestroyCell(…,1)/KillCell), NPC growth/spawnOutput (behaviours — off for passive cells).
- Active area: tiles stream around the **local** player's view (`G+0x4128`, bbox 0x16B4C68..7C);
  cells outside it are destroyed unless `+0x354` (0xE7C080) or level −1. ⇒ on the host, NPCs near a
  guest far from the host do not exist.

## 6. Avatar move / respawn, regen

- Teleport (✔ fields): write `+0x4C` pos (z = 0 on the gameplay layer), `+0x08` target = pos,
  `+0x84` = pos, `+0x90` vel = 0, `+0x04` idle = 1, `flags(+0x48) |= 4; ++counter(+0x4A)`; query
  position is refreshed by 0xE7C080 the same frame. Camera follows the avatar (?) — check for a jump.
- Respawn-next-to-partner: avoid the vanilla path (RebuildLevel wipes the world):
  hook **KillCell 0xE7A4A0** and **BreakOrSwallow 0xE7AEE0**; when the victim is the avatar and the
  partner is alive → do not call the original; set hp 6, clear `+0x112/+0x113`, teleport next to the
  partner, `+0x111 = 1` for ~3 s, optional effect. Safety net: hook PlayerDeath 0xE72060 and
  0xE72200/RequestRespawn 0xE50930 (if reached, also clear `+0x16C,+0x178..+0x189`, `+0x18C = 0`,
  `cCellUI+0x936 = 0`, `G+0x5194 = 0`).
- Regen near partner: own timer (no vanilla regen exists): if `dist(avatar, partner) < R` and hp < 6
  every T s: `ShowHealthRollover(avatar, hp)`; `hp++`.

## 7. Implementation recipe (host-authoritative tide pool)

Rule: each machine is authoritative for its own avatar; the host for NPCs, food and progress.

Host
1. Each tick enumerate foreground cells (§1) near any player; send per cell: net id (pool index),
   instanceID, modelKey, level, size(+0x58)/targetSize(+0xB4), pos, vel, rot, hp, flags
   (dying +0x113). Creatures at 10–15 Hz, food/parts as spawn/despawn + low-rate position.
2. Guest puppets (created as now): keep `+0x111 = 1` (NPC bites on them are not applied — the guest
   applies them locally), set `+0x110 = 1` so damage/kills they cause count as player kills.
3. On `Damage{netId, amount, type}` from a guest: victim = pool[netId]; set a "forced damage" global,
   call DamageCell(victim, guestPuppet, &dir, type, 0, dt) with a GetDamageAmount hook returning the
   forced amount (or replicate: `hp -= amount`, clamp, KillCell when 0). Damage from several players
   sums up naturally.
4. On `Consumed{netId}`: run EatFood-like logic for food (shrink a level or `DestroyCell(idx,0,0,0)`).
5. AddFoodPoints hook: every local or forwarded gain is applied with the original, then broadcast the
   authoritative `S+0x1C/+0x20` values.

Guest
1. Hook PopulateMarker 0xE7C500 → skip gameplay layer (no local NPCs/food).
2. Mirrors: `ref = Create(instanceID, 0x16AE490)`; force modelKey; `idx = CreateCellObject(G+0x40FC,
   &pos, 0, ref, level, 1.f, 0.f, 1, &q, -1)`; then `+0x16C = 1` (or hook 0xE7A190), `+0x354 = 1`,
   copy size/hp. Drive by velocity + correction (§2). Remove with `DestroyCell(idx,0,0,0)`
   (play "cell_death_oneshot" via InstanceEffectOnCell first when the host reports a kill).
3. Hook DamageCell 0xE7A7C0: if victim is a mirror → send `Damage{netId,
   GetDamageAmount(attacker, victim), type}` and return 0 (no local hp change/kill/loot); if victim is
   the local avatar → let it run (mirrors can bite the guest on contact).
4. Mirror eaten locally (EatFood 0xE7DF40 / 0xE7DBD0 / 0xE7DD90 with a mirror as food): let the local
   heal/points happen, send `Consumed{netId}`; detect it by hooking DestroyCell 0xE780A0 for mirrors
   not removed by the sync code (and BreakOrSwallow for swallowed mirrors).
5. AddFoodPoints hook: forward `{amount, isPlant}` to the host; on host total T vs local L call the
   original `AddFoodPoints(T-L, isPlant)` under a re-entrancy guard (keeps level-ups consistent).

Both: co-op respawn and regen near partner as in §6.

## Open questions
- Who clears `+0x16C`/`+0x178…` locks (none found statically) — confirm at runtime that a live NPC
  with `+0x16C=1` stays passive and nothing resets it.
- `DamageCell` arg3 (float*, negated "dir") and arg5 meaning; damage type numbers beyond 3 (periodic
  poison/electric), 4 (bite), 5 (0xE7B280), 6 (eat chunk).
- Effects of `+0x110` beyond kill credit; meaning of `G+0x5169`, state `G+0x5158`.
- Whether RandomCreature models must be preloaded on the guest (cCellGFX::PreloadCreature 0xE64990)
  before CreateCellObject with a forced model key.
- Camera behaviour after an avatar teleport; whether 0xE6ECB0/0xE6EB60 is the egg/hatch intro.
- Existing `cell_despawn` in mods/coop/modes.cpp sets hp=0 and opacity 0: that only hides the puppet
  (cells are removed by DestroyCell or size→0), so puppets may leak — use DestroyCell 0xE780A0.
