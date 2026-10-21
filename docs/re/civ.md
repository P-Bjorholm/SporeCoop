# Civilization stage — RE notes (GOG 2016 build, sha256 922af7c7…)

Static analysis only (llvm-objdump on the full exe + local headless Ghidra on a windowed PE copy
0xBD0000–0xD20000). All addresses are VAs in this build. "ret N" = callee pops N bytes.
Confidence: **V** = verified by disassembly in this build (role + args + ret counted),
**P** = partly verified (address/role sure, some argument semantics inferred),
**U** = unverified / inferred from ModAPI headers or a single use, check at runtime.

Game-mode ids: kGameCiv = 0x1654C04, kGameSpace = 0x1654C05 (`0xB5B8D0()` returns the
current mode id; civ code branches on it everywhere).

---------------------------------------------------------------------------------------------
## 1. Civilizations, cities, city fields

### Getting the objects (all thiscall on `nm = cGameNounManager::Get()` 0xB3D3D0)
| What | Address | Signature | Conf |
|---|---|---|---|
| GetPlayerCivilization | 0xB25F20 | `(nm)` → `cCivilization*` | V (known) |
| **GetAllCivilizations** | 0xB25C10 | `(nm)` → `eastl::vector<cCivilization*>*` (begin,+4 end) | V |
| **GetCivilizationByPoliticalID** | 0xB25EB0 | `(nm, uint32 polID)` → civ or null, ret 4 | V |
| **GetAllCities** | 0xACE1C0 | `(nm)` → `vector<cCity*>*` (every city on the planet, all civs) | V |
| **GetAllVehicles** | 0xAE7300 | `(nm)` → `vector<cVehicle*>*` | V |
| first player city ("home") | 0xB25BA0 | `(nm)` → first city with `city+0x120 vt0x58 IsPlayerOwned()` | V |
| GetData (generic) | 0xB21260 | `(nm, create, clear, add, filter, nounID)`; the 3 getters above are wrappers that return `GetData(...)+4` | V |

Noun ids: civ 0x18C816A, city 0x18C43E8, vehicle 0x18C6DE8 (all wrappers push these).

### cCivilization (TYPE 0x901F1362) — matches ModAPI `cCivilization.h`
| Off | Field | Evidence | Conf |
|---|---|---|---|
| +0x30 | mPoliticalID (cGameData); vt 0x4C GetPoliticalID | used everywhere | V |
| +0x6C | cCultureSet (design keys) — `0xBF9890` = `lea ecx,[ecx+0x6C]; jmp 0xBF9820` | V |
| +0x89 | mIsPlayerOwned | GetMoney/SpendMoney | V |
| +0x98 | mWealth | GetMoney | V |
| +0x9C | `vector<cCity*> mCities` | AddCity 0xBF4490 | V |
| +0xB0 | `vector<cVehicle*> mVehicles` (max 36 total, see 0xBF2220) | AddVehicle 0xBF5750 | V |
| +0xD4 | `vector<cSpatialObject*> mSelectableObjectVector` (vehicle+0x34 pushed) | AddVehicle | V |
| +0x4AC/+0x4B0 | domestic/expansion budget (NPC civs only, added to money) | GetMoney | V |
| +0x4C0/+0x4C4/+0x4C8 | military/religious/economic capture counters | ChangeOwner | V |

Civ functions (thiscall on civ):
| Function | Address | Signature | Conf |
|---|---|---|---|
| GetMoney | 0xBEF800 | `()` → float in ST0 (player civ: just mWealth) | V |
| **SpendMoney** | 0xBEF840 | `(float amount)` ret 4; `mWealth -= amount` | V |
| AddCity | 0xBF4490 | `(cCity*)` ret 4 | V |
| RemoveCity | 0xBF4710 | `(cCity*, int)` ret 8 | P |
| AddVehicle | 0xBF5750 | `(cVehicle*)` ret 4: push to mVehicles, `SetPoliticalID(civ)`, push vehicle+0x34 to +0xD4 | V |
| **CreateVehicle** | 0xBF58D0 | `(int purpose, int locomotion, ResourceKey key /*12 B by value*/, const Vector3* pos, bool isSpace)` → `cVehicle*`, ret 0x1C. CreateInstance(0x18C6DE8) → AddVehicle → `cVehicle::Load` 0xCA67B0 `(loco, purpose, key)` → SetPosition(pos) | V |
| VehicleSlotsLeft | 0xBF2220 | `(int purpose)` ret 4 → int (0 if ≥36 vehicles) | V |
| **GetVehicleCost** | 0xBF2290 | `(int purpose, int locomotion)` ret 8 → float ST0 | V |
| GetDesignKey | 0xBF9890 | `(uint32 designId)` ret 4 → `ResourceKey*` (from culture set) | V |
| BuyCity | 0xBF0500 | `(cCity*, float price)` (pie command BuyCity) | P |
| AddMoney (refund) | 0xBEFEB0 | `(float)` (used by CitySellBuilding) | P |

`int VehicleDesignId(purpose, locomotion)` = **0xC9E850** cdecl → id for `GetDesignKey`
(space mode uses another table). Enums: purpose 0 military, 1 cultural(religious),
2 economic, 3 colony; locomotion 0 land, 1 water, 2 air.

### cCity (TYPE 0xEE9B2232, noun 0x18C43E8) — matches ModAPI `cCity.h`
| Off | Field | Evidence | Conf |
|---|---|---|---|
| +0x24 | mID (cGameData) | header only | U |
| +0x30 | mPoliticalID; set from civ in SetCivilization | 0xBE1280 | V |
| +0x120 | cSpatialObject; **+0x124 position**; vt 0x58 IsPlayerOwned | known, purchase UI | V |
| +0x29C | capture type of the last capture (0 mil, 1 rel, 2 econ) | Capture 0xBE9410 writes it | V |
| +0x320 | mpCityHall (`0xBD9C70` getter) | V |
| +0x324 / +0x328 | mpCityWalls / mpCityTerritory | dispose | P |
| +0x33C | mbIsPlayerCity (= new civ is player civ) | ChangeOwner | V |
| +0x340 | `vector<cBuilding*> mBuildings` (begin,+4 end) | AddBuilding / AI growth | V |
| +0x354 | `fixed_vector<cTurret*,32> mTurrets` (begin,+4 end) | dispose | P |
| +0x3EC | `cCommunityLayout mBuildingsLayout`; slot vector at **+0x43C** (layout+0x50), stride 0x18: `{+0 u8 flag, +4 occupant obj, +8 int, +0xC Vector3 pos}`; 14 building slots | `0xAF9F40` GetSlot(i), `0xAFADA0` RemoveObj, `0xFCC1D0` slot→occupant | V |
| +0x4B4 | mTurretsLayout | header | U |
| +0x518 | mbSmallCity | V |
| +0x540 | **mVehicleSpecialty** (= purpose of vehicles this city builds) | `0xBD8300` getter | V |
| +0x590 | **mpCivilization** (`0xBD9D20` getter) | SpawnVehicle | V |
| +0x664..0x678 | money/happy/unhappy/new buildings/final income | header (mNewBuildings +0x670 used by AI growth) | U |
| +0x748/+0x74C | mCapturePercent / mCaptureID (military) | 0xBE9AB0 | V |
| +0x750/+0x754 | mConvertPercent / mConvertID (religious) | 0xBEAB60 | V |
| +0x758/+0x75C | mBuyPercent / mBuyID | header | U |

City functions (thiscall on city):
| Function | Address | Signature | Conf |
|---|---|---|---|
| SetCivilization | 0xBE1280 | `(cCivilization*)` ret 4 (sets +0x590 and political id; low level, no bookkeeping) | V |
| **SpawnVehicle** | 0xBDDED0 | `(int purpose, int locomotion, ResourceKey key, bool isSpace)` → cVehicle*, ret 0x18. Spawn point = `0xBDCB60(city, Vector3* out, loco)`, then civ CreateVehicle, faces the walls. **No money** | V |
| AddBuilding | 0xBE2020 | `(cBuilding*, int noUpdate)` ret 8 | V |
| RemoveBuilding | 0xBE3480 | `(cBuilding*)` | P (ported) |
| CreateBuilding (auto slot) | 0xBE54E0 | `(uint32 noun, uint32 typeId)` ret 8 → cBuilding*; picks the best free slot (`0xBDB6C0`), used by AI growth | V |
| CreateBuildingByType | 0xBE74A0 | `(uint32 typeId)` ret 4 (type→noun: 0x1A55E4D→0x1A56ABA ent., 0xECADE42→0x18EA2CC factory, else house 0x18EB106) | V |
| SetBuildingModel | 0xBDB920 | `(cBuilding*, uint32 typeId)` ret 8 — sets model key from civ culture set | V |
| **SnapBuildingToSlot** | 0xBE3630 | `(cBuilding*, bool update)` ret 8 → bool; finds the free slot nearest to the building's current position (`0xAFACC0`), assigns it, sets slot position + orientation facing the city | V |
| ChangeOwner | 0xBE8A00 | see §6 | V |

### Network id for a city
`mID` is not proven stable between two processes. Cities never move, so use the position:
key = index in `GetAllCities()` sorted by (x,y,z) of `city+0x124`, or send the position and
resolve to the nearest city (`cPlanetModel::GetNearestCity` 0xB89580 is ported, unverified).
Buildings: (city key, slot index in +0x43C). Vehicles: host-assigned net id (like npc.cpp).

---------------------------------------------------------------------------------------------
## 2. Building placement

The player builds in the **city planner** = `Simulator/cCommunityEditor` (size 0x2FC, ctor
0xD0D200, main vtable 0x147A0BC, created by cCityInputStrategy when the city hall button
0x62D5618 in the RolloverCivCity window is clicked: input strategy +0x110 = city hall,
message 0xB2699148). Palette items are dragged onto the 14 plots.

* Commit of a placement: `cCommunityEditor` vt[1] **0xD130D0** `HandleMessage(msgId, msg)`
  (thiscall, ret 8). On drop it: reads the palette item's noun (property 0x456B66A),
  `CreateInstance(noun)`, `vt0x54 SetOwnerCity(city)`, sets model key from
  `civ->GetDesignKey(...)`, **`AddBuilding(city, b, 0)` at 0xD14197 (return address 0xD1419C)**,
  then cost `0xD0AC10(obj, refund=false, ...)` (cdecl, building → `vt0x5C GetCost()`,
  turret → 0xBD0240) is taken from the editor budget (`[editor+0x68] vt0x18(0, -cost)`). **P**
* There is no simple "place(noun, slot)" API; the host should rebuild the sequence:

```
// host: build `noun` in `city` at slot `k` (or at a guest-supplied position)
b   = CreateInstance(nm, noun)            // 0xB20B80
b   = b->vt0x0C Cast(0x0E9CB8BA)          // cBuilding
SetBuildingModel(city, b, typeId)         // 0xBDB920 (typeId: house 0xFF10521, factory 0xECADE42, ent 0x1A55E4D)
b->vt0x54(city)                           // SetOwnerCity (+0x1F0)
pos = *(Vector3*)(*(uint8**)(city+0x43C) + k*0x18 + 0xC)
(b+0x34)->vt0x38 SetPosition(&pos)
if (SnapBuildingToSlot(city, b, 1))       // 0xBE3630, picks nearest free slot = k
    AddBuilding(city, b, 0);              // 0xBE2020
else DestroyInstance(nm, b);              // 0xB224F0
civ->SpendMoney(b->vt0x5C GetCost())      // 0xBEF840 (GetCost returns float in ST0)
```
Building vtable (house 0x1468220, verified): 0x20 GetNounID, 0x54/0x58 SetOwnerCity, 0x5C GetCost,
0x84 GetOwnerCity (+0x1F0). Building nouns: house 0x18EB106, factory 0x18EA2CC,
entertainment 0x1A56ABA, city hall 0x18EA1EB, turret 0x436F342 (turrets use mTurretsLayout,
turret placement path not traced — **U**).

Sell/destroy: pie command CitySellBuilding (0x37EAF27) in 0xCF3340: refund
`cf7ba0()*b->GetCost()` via 0xBEFEB0, then `0xBE3510(city, b)` removes it. **P**

---------------------------------------------------------------------------------------------
## 3. Vehicle purchase

UI: `RolloverCivCity` window proc (vtable 0x1482190), **HandleUIMessage 0xE2ADE0**
`thiscall(this, IWindow* win, const Message* msg)` ret 8. `msg+8` = 0x287259F6 (button click),
`msg+0xC` = control id; `this+0xA8` = the cCity. **V**

| Button id | locomotion |
|---|---|
| 0x62D6902 | 0 land |
| 0x62D6908 | 1 water |
| 0x62D690F | 2 air |
| 0x62D5618 | open city planner (city hall) |

Purchase sequence (identical for the 3 buttons; host can call it for any city):
```
civ = GetPlayerCivilization(); if (!city->+0x120 vt0x58 IsPlayerOwned()) return;
purpose = *(int*)(city+0x540);                         // 0xBD8300
if (VehicleSlotsLeft(civ, purpose) <= 0) return;       // 0xBF2220
cost = GetVehicleCost(civ, purpose, loco);             // 0xBF2290 (float ST0)
if (GetMoney(civ) < cost) return;                      // 0xBEF800
key  = *GetDesignKey(civ, VehicleDesignId(purpose, loco));   // 0xBF9890(0xC9E850(p,l))
v    = SpawnVehicle(city, purpose, loco, key, mode == 0x1654C05);  // 0xBDDED0
if (v) PushEvent(v, 10);                               // 0xCA8260 thiscall(v, int) ret 4
SpendMoney(civ, cost);                                 // 0xBEF840
// cosmetic: 0xCF85F0 / 0xCF9010(v) on cCivModeStrategy (hint + "vehicle built" event)
```
Vehicles are stored in `civ+0xB0` (and their spatial in `civ+0xD4`). The pie/cheat command
ids 0x321..0x32F in 0xCF3340 also spawn vehicles (`purpose = id-0x321 / 5`, `loco = % 5`)
**without** charging money. Civ start (0xCF72A0) spawns the free first vehicle at
`0xB25BA0()` city and charges it.

### cVehicle (TYPE 0x137E8E0, noun 0x18C6DE8, size 0xD98, ctor 0xCA7A20)
Vtables: main 0x14756D8, +0x34 locomotive/spatial 0x1475730, +0x508 cCombatant 0x1475830,
+0x5D0 behavior agent 0x1475658, +0x638 0x1475638, +0x650 0x1475620.
| Off | Field | Conf |
|---|---|---|
| +0x30 | mPoliticalID (set by AddVehicle) | V |
| +0x34 | cLocomotiveObject/cSpatialObject; **+0x38 position, +0x44 orientation (quat)** (GetPosition = spatial+4, GetOrientation = spatial+0x10) | V |
| +0xA0 | selected flag (spatial+0x6C) | V |
| +0x508 | cCombatant; **+0x540 HP**, +0x534 base max HP (vt0x58 GetMaxHitPoints applies modifiers) | V |
| +0xAF0 | behavior tree data (AI) | V |
| +0xB1C / +0xB20 | mLocomotion / mPurpose | V |
| +0xB2C | event deque (PushEvent 0xCA8260) | V |
| +0xB5C | mpNearestCity | P |
| +0xB68 | `vector<tVehicleOrder>` mOrders, element 0x18 B: `{+0 target obj (ref-counted), +4 Vector3 pos, +0x10 int type, +0x14 issuer politicalID}` | V |
| +0xBC8 | order flag (1, or 2 when ≥10 units ordered) | V |
| +0xBCD | mbDead | V |
| +0xD24/+0xD28 | order stamp (`0xC9EFB0(v, x)`) | P |

Spatial vtable (+0x34): 0x2C GetPosition, 0x30 GetOrientation, 0x38 SetPosition (vehicle
override 0xC9FDE0), 0x3C SetOrientation, 0x44 Teleport, 0x50 IsSelected, 0x54 SetIsSelected
(0xC9F8D0), 0x58 IsPlayerOwned, 0xE0 MoveTo (0xCA3590), 0xEC StopMovement. Combatant vt[6]
TakeDamage = **0xCAB6D0** (this = vehicle+0x508, ret 0x14, same args as the creature one).
`cVehicle::Die` **0xCAA4E0** thiscall(v, bool effects, uint32 killerPolID) ret 8
(TakeDamage calls it with (1, attacker); silent removal path uses (0, -1)).

---------------------------------------------------------------------------------------------
## 4. Vehicle orders

Input: `Simulator/cCityInputStrategy` (size 0x148, ctor 0xCF4DF0, main vtable 0x147925C,
message-listener vtable 0x147924C → HandleMessage **0xCF5690**). Owned by
`cCivModeStrategy` (global ptr **0x169E2A8**, getter 0xCF7610) at +0xE8.

| Function | Address | Signature | Conf |
|---|---|---|---|
| **Right-click on object** | 0xCF3D40 | thiscall(input, cGameData* target, ?) ret 8; classifies target (creature 0xD0036E08, building ≠ city hall, turret 0x436F315, tribe hut 0xEE02C7, spice node cCommodityNode 0x403DF5F, vehicle) and orders all selected vehicles | V |
| **Move selected to ground** | 0xCF2670 | thiscall(input) — formation positions, `SetMoveOrder` per vehicle | P |
| **Pie/command handler** | 0xCF3340 | thiscall(input, uint32 cmdId) ret 4; target object at input+0x10C (city via vt0x84), spice node at +0x11C, turret at +0x118 | V |
| **Group order** | 0xCF3200 | thiscall(input, cGameData* target, Vector3 pos /*by value*/, int type) ret 0x14; uses the vehicles collected in input+0xE8 (begin,+4 end); type 6 → per-vehicle SetMoveOrder, else SetOrder; then `0xC9EFB0(v,0)` | V |
| **cVehicle::SetOrder** | **0xCAC180** | thiscall(v, cGameData* target, int type, int flag) ret 0xC: clears orders (0xCAAA80), pushes `{target,_,type,polID}`, +0xBC8=flag, 0xC9F940 | V |
| **cVehicle::SetMoveOrder** | **0xCAC260** | thiscall(v, Vector3 pos /*by value*/, int flag) ret 0x10: order type 6 | V |
| GetCurrentOrder | 0xCA7350 | thiscall(v) → tVehicleOrder* | V |
| CanReach/CanTarget city | 0xCA84C0 | thiscall(v, cCity*) → bool | P |
| Can reach position | 0xC9EAE0 | thiscall(v, const Vector3*) → bool | P |
| Can claim node | 0xC9FFC0 | thiscall(v, obj) → bool | P |

Order types (from the call sites): **1** attack target (unit/building/turret/creature, enemy
spice node), **2** military attack city (cmd VehicleAttack 0x37E95B9, military vehicles),
**3** religious attack city (cmd CityCultureAttack 0x37E95BA, purpose 1), **5** trade with city
(cmd CityTradeWithCity 0x37E95BD, economic), **6** move to position, **8** claim free spice
node / act on tribe hut, **9** create trade route (cmd 0x37E95BC), **11** bribe spice node
(cmd BribeNode 0x51B40A4). 0, 4, 7, 10 are used only by the civ AI (0xBF5DD0, 0xBF8830…) — **U**.
Command name table (id → name) is registered in 0xCF47B0 (e.g. 0x37E8514 CityBuyVehicle,
0x37E95BB BuyCity, 0x37EAF27 CitySellBuilding, 0x406D0D8 CityPosseSelect).

Programmatic order (host, for vehicle `v`):
`SetOrder(v, target, type, 1); 0xC9EFB0(v, 0)` or `SetMoveOrder(v, pos, 1)`.
Target must be the cGameData* of the object (city, building, vehicle, cCommodityNode…).

---------------------------------------------------------------------------------------------
## 5. Vehicle per-frame update (AI) and direct pose

Civ vehicles are ticked by a simulator strategy, **0xDCEDB0** (vtable 0x147CF44, slot 13
`Update(uint64 dtMs)`): for every vehicle of `GetAllVehicles()`:
`0xCAC890` (one-time init) → `0xC9F450(v, dt)` weapon timers → `0xCA9830` timers/events →
`0xC9E880` → **behavior tree tick `0xBC9140`** (only if `v+0x5D0 vt0x20()==0` and
`[v+0xAF0]` valid) → `0xC9F740` UI state → `0xCA3370` visual update. **V**

* **AI = behavior tree**: `0xBC9140` cdecl `(float dt, double time, treeDef, agent, …)`,
  9 dwords, **agent (the cVehicle*) at [esp+0x14] on entry** (ret addr at [esp]). Shared with
  creatures/scenario (callers 0xC38880, 0xC69810, 0xCE4A60, 0xD995C0, 0xF39DF0), so filter by
  agent. Hooking it and returning for mirror vehicles stops orders, movement goals and
  weapon use. **V** (layout), runtime test needed.
* Alternative: hook 0xDCEDB0 is too coarse (all vehicles).
* Pose: `(v+0x34)->vt0x44 Teleport(&pos, &quat)` or vt0x38/0x3C; call vt0xEC StopMovement
  first so the locomotion does not pull it back. Read pose from v+0x38 / v+0x44.
* Death on mirrors: `cVehicle::Die(v, 1, killerPol)`; damage: TakeDamage 0xCAB6D0.

---------------------------------------------------------------------------------------------
## 6. City capture / ownership change

| Function | Address | Signature | Conf |
|---|---|---|---|
| **Capture** | 0xBE9410 | thiscall(city, uint32 newPolID, int captureType, a3, a4, a5) ret 0x14; writes +0x29C, returns if already owned, then ChangeOwner (+cinematics) | P (first 2 args V) |
| **ChangeOwner** | 0xBE8A00 | thiscall(city, uint32 newPolID, bool, bool) ret 0xC: civ = GetCivilizationByPoliticalID; `RemoveCity(old)`, `AddCity(new)` (sets +0x590 + political id of city), capture counters, +0x33C mbIsPlayerCity, notifications | V |
| military capture check | 0xBE9AB0 | thiscall(city): if `+0x748 ≥ 1.0` and `+0x74C` ≠ owner → Capture(+0x74C, 0) | V |
| religious conversion check | 0xBEAB60 | `+0x750 ≥ 1.0` → Capture(+0x754, 1) | V |
| economic buy | 0xBE9980 | thiscall(city, uint32 buyerPolID, float price) ret 8 → Capture(…, 2) | V |
| other capture caller | 0xBE9DE0 | (called from 0xBEAA60, 0xBEACE0, 0xBEB130, 0xCFF000) | U |
| cheat path | 0xCFDEA0 | `ChangeOwner(city, playerCiv->GetPoliticalID(), 0, 0)` | V |

What changes: city+0x590 (civ), city+0x30 political id, civ mCities lists, +0x29C,
+0x33C, civ capture counters. The buildings keep their objects (owner city unchanged).

---------------------------------------------------------------------------------------------
## 7. Selection

* Terrain cursor = selection holder: `cGameTerrainCursor::GetTerrainCursor` **0xB30E80**
  (cdecl, global 0x167F784). Main vtable 0x1460A98: **vt0x5C GetSelected(vector<cSpatialObject*>*)**
  (0xB310C0: copies objects of `cursor+0x164` list whose `vt0x50 IsSelected()` is true),
  **vt0x60 GetSelectedOfType(vector*, typeId)**. `cursor+0x164` = pointer to the selectable
  object vector (in civ = civ+0xD4). **V**
* Selection state is the per-object flag set through spatial vt0x54. For vehicles:
  **`cVehicle::SetIsSelected` 0xC9F8D0** thiscall(v+0x34, bool) ret 4, flag at v+0xA0. **V**
* Selected → vehicle: `0xBD8570` cdecl(spatial) → cVehicle* (used on every selection entry).

---------------------------------------------------------------------------------------------
## 8. Implementation recipe (co-op "governors")

Host simulates everything; guests run mirrors. Mod-side tables (host-authoritative):
`city_owner[cityKey] = playerId` (capital = host), `vehicle_owner[netId] = playerId`.

**Hooks (both peers unless noted)**
1. `cVehicle::SetIsSelected` 0xC9F8D0: if `bool==true` and the vehicle is not owned by the local
   player → ignore (return). Keeps foreign vehicles out of every selection/order path.
2. RolloverCivCity `HandleUIMessage` 0xE2ADE0: on click 0x62D6902/08/0F
   * city not owned by the local player → consume (return 1) + hint "city of Alice";
   * guest → send `CIV_BUY{cityKey, loco}` and consume; host → let it run, then tag the new
     vehicle (hook SpawnVehicle with return address 0xE2B05B/0xE2B265/0xE2B3E7 (land/water/air) or diff
     `civ+0xB0`) with owner = local host player.
   Same check for button 0x62D5618 (city planner) on foreign cities.
3. City planner placement: hook `AddBuilding` 0xBE2020 with return address **0xD1419C**:
   guest → read `b->GetNounID()` and position (b+0x38), send `CIV_BUILD{cityKey, noun, pos}`
   (the local building is replaced by the host's result; or let it stay and reconcile by
   slot). Host executes the §2 sequence.
4. Orders, guest: hook `SetOrder` 0xCAC180 and `SetMoveOrder` 0xCAC260; when the return
   address is inside the input strategy (0xCF1000–0xCF7000) send
   `CIV_ORDER{netId, type, targetRef, pos, flag}` and skip the original. Host: owner check,
   then call the same function on the real vehicle (+ `0xC9EFB0(v,0)`).
   targetRef = {kind, key}: city / building(city,slot) / vehicle netId / spice node or other
   object by position (nearest object of that TYPE).
5. Mirrors (guest): create with `CreateVehicle(civ, purpose, loco, key, &pos, 0)` 0xBF58D0
   on the guest's player civ; hook `0xBC9140` and return when `[esp+0x14]` is a mirror;
   apply `CIV_VEH_STATE{netId, pos, quat, hp, flags}` at 10 Hz via StopMovement + Teleport
   and write HP at v+0x540; `CIV_VEH_DESPAWN` → `Die(v, 1, -1)`.
   Guest must not create vehicles of its own: block `SpawnVehicle` 0xBDDED0 on guests
   (catches the free start vehicle 0xCF72A0 and the cheat ids) unless the mod spawns a mirror.
6. Capture (guest): hook `Capture` 0xBE9410 and return unless applying a host message;
   host: hook it (post) and broadcast `CIV_CITY_OWNER{cityKey, newPolID, type}` → guest calls
   `ChangeOwner(city, pol, 0, 0)` 0xBE8A00. New owner player of a captured city = owner of
   the vehicle whose political attack captured it (host knows from `+0x74C` / the attacking
   vehicles near the city).
7. Money is already shared (civ+0x98); host-side purchases call SpendMoney themselves.

**Messages**: CIV_BUY, CIV_BUILD, CIV_ORDER, CIV_VEH_SPAWN{netId, owner, purpose, loco, key,
pos}, CIV_VEH_STATE, CIV_VEH_DESPAWN, CIV_CITY_OWNER, CIV_BUILDING_ADDED/REMOVED{cityKey,
slot, noun}, CIV_OWNERSHIP{city→player table}.

---------------------------------------------------------------------------------------------
## Open questions / unverified
* Whole-planet consistency on guests: NPC civs, their vehicles and city growth (AI growth
  0xBE6010 path via `CreateBuilding` 0xBE54E0) still run on guests; the host would have to
  mirror or suppress them too (civ AI entry points 0xBF5DD0, 0xBF6930, 0xBF8830, 0xBFB140).
* Exact meaning of SetOrder `flag` (1/2) and of order types 0/4/7/10.
* Planner commit: where the slot index lives at commit time (Ghidra shows a local slot index
  ≠ -1) and how the editor budget `[editor+0x68]` is settled with civ money.
* Turret placement path and mTurretsLayout slots.
* Whether cGameData mID (+0x24) is persisted in the save (would be a better network key).
* Capture() args 3–5, RemoveCity 2nd arg, BuyCity 0xBF0500 args.
* Civ vehicle-list removal on death (Die only marks dead/effects; list cleanup not traced).
