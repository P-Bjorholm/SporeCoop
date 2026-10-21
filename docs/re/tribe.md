# Tribe stage — RE notes (GOG 2016 build, sha256 922af7c7…)

Static analysis only (llvm-objdump + local headless Ghidra on windowed PE copies).
All addresses are VAs in this build. "ret N" = callee pops N bytes (thiscall/stdcall).
Confidence: **V** = verified by disassembly in this build (signature + ret counted),
**P** = partly verified (address/role sure, some argument semantics inferred),
**U** = unverified / inferred, check at runtime.

ModAPI (March2017) names are used where they match. Several ported symbols in
`gog2016_922af7c7.sym` are wrong for this area — see "Symbol fixes" at the end.

---------------------------------------------------------------------------------------------
## 1. Player tribe, members, citizen fields

### Getting the tribe
| What | Address | Signature | Conf |
|---|---|---|---|
| `cGameNounManager::Get` | 0xB3D3D0 | cdecl `()` | V (known) |
| `cGameNounManager::GetPlayerTribe` | **0xBFC710** | thiscall `(nm)` → `cTribe*`; body is `mov eax,[ecx+0x70]; ret` | V |

`nm+0x70` = mpPlayerTribe (already used in shared.cpp). The tribe input code calls
`Get()` + `0xBFC710` everywhere.

### cTribe (noun 0x18C6D19, TYPE 0x4F396A66, main vtable **0x1474910**)
Layout matches ModAPI `cTribe.h` (checked by the getters):

| Off | Field | Evidence |
|---|---|---|
| +0x120 | cSpatialObject (pos +0x124) | known |
| +0x20C | cGameBundleContainer (food) | known |
| +0x334/+0x338/+0x33C | mbRoboTribe / mRoboPopulationCount / mbVisualized | SpawnMember/AddMember |
| **+0x340** | `vector<cCreatureCitizen*> mTribeMembers` (begin,+4 end) | vt 0x90 = 0xC8E990 `lea eax,[ecx+0x340]` |
| **+0x354** | `vector<cSpatialObject*> mSelectableMembers` — holds **citizen+0xC0** (spatial sub-object), adults only | vt 0x94 = 0xC8E710; AddMember pushes `citizen+0xC0` when `GetAge()==1` |
| +0x368 | `cTribeHut* mpHut` | vt 0xAC/0xB0 = 0xC8F850 |
| +0x36C | `vector<cTribeTool*> mTools` | vt 0xBC/0xC0 = 0xC8E880 (both return +0x36C here) |
| +0x380 | social tools vector (types 4,5,6 are also pushed here) | CreateTool |
| +0x550 | mTribeArchetype | 0xC8EC60 |
| +0x19D8 | mPurchasedTools bitmask | 0xC8ED60 set / 0xC8ED80 test |

cTribe vtable (0x1474910), verified slots:

| Slot | Addr | Meaning / signature | Conf |
|---|---|---|---|
| 0x8C | 0xC9A280 | SpawnTribe(...) (creates hut, food mat, initial members) | P |
| 0x90 | 0xC8E990 | `vector<cCreatureCitizen*>* GetTribeMembers()` | V |
| 0x94 | 0xC8E710 | `vector<cSpatialObject*>* GetSelectableMembers()` | V |
| 0x98 | 0xC983B0 | `cCreatureCitizen* SpawnMember(int age)` ret 4 | V |
| 0x9C | 0xC96060 | `void RemoveMember(cCreatureCitizen* c /*null=auto pick*/, bool destroy)` ret 8 | V |
| 0xA0 | 0xC8E800 | `RemoveMember(c)` → vt0x9C(c, true) ret 4 | V |
| 0xA4 | 0xC8E850 | GetTotalFood | V (known) |
| 0xAC | 0xC8F850 | `cTribeHut* GetHut()` | V |
| 0xB4 | 0xC96160 | `cTribeTool* CreateTool(int toolType)` ret 4 | V |
| 0xB8 | 0xC97010 | RemoveTool(cTribeTool*) | P (ModAPI, address matches) |
| 0xBC | 0xC8E880 | `vector<cTribeTool*>* GetTools()` | V |

Iterating members (C++ for the mod):
```cpp
void* tribe = thiscall<void*>(0xBFC710, cGameNounManager_Get());
void** b = *(void***)((char*)tribe + 0x340), **e = *(void***)((char*)tribe + 0x344);
for (void** it = b; it < e; ++it) { void* citizen = *it; ... }
```

### cCreatureCitizen (noun 0x18EB4B7, TYPE 0x4F176642, size 0x1038)
Main vtable **0x146B818** (ctor 0xC26F40 region writes sub-vtables:
+0x04 0x146B804, +0x34 0x146B7C0, +0x58 0x146B758 (behavior list),
**+0xC0 0x146B658 (cSpatialObject/locomotive)**, +0x194 0x146B64C, +0x590 0x146B630,
**+0x5A8 0x146B5C8 (cCombatant)**, +0x670 0x146B5B0).

Fields (verified in code unless noted):

| Off | Field | Evidence |
|---|---|---|
| +0x21 | destroyed flag (cGameData) | 0xB182E0 |
| +0x24 | mID (cGameData) | U (ModAPI) |
| +0x30 | political ID (cGameData) | U (ModAPI); Init sets it via vt0x48(tribe->vt0x4C()) |
| +0xC0 | cSpatialObject: GetPosition vt0x2C, SetPosition 0x38, SetOrientation 0x3C (same as animal, see 05-re-notes) | Init 0xC27310 uses 0x38/0x3C |
| +0xC0+0x6C = **+0x12C** | byte **IsSelected** | spatial vt0x50 = 0xAD26A0 `mov al,[ecx+0x6C]` |
| +0x5A8 | cCombatant; HP +0x5E0, MaxHP +0x5D4 | ctor vtable write; offsets as cCreatureAnimal |
| +0xB28 | species key | DoAction neighbour 0xC27810 |
| +0xB34 | age (0 = baby, 1 = adult) | 0xC0B880 GetAge, 0xC0B890 IsAdult |
| **+0xB4C** | AI/behavior-tree data (action queue list at AI+0x60C) | DoAction, Update |
| +0xB5E | byte flag (skip locomotion/anim when set) | Update |
| +0xFC0..+0x1002 | ModAPI fields (FC0=3, FF4 selection group, FF8, FFC handheld item, 1000, 1002) | reset in vt0xCC 0xC243D0 |
| +0xFF4 | selection group (Ctrl+N) — set 0xC228F0, get 0xC22900 | V |
| +0xFFC | mCurrentHandheldItem | V (writes) |
| +0x1010 | mpOwnerCity (null in tribe stage → AI runs) | V |
| **+0x1014** | **mpOwnerTribe** | V (DoAction, Init) |
| +0x102C | mSpecializedTool (tool type held, e.g. 9 = heal); getter **0xC22EE0** | V |

Current action ("what is the member doing"):
* `0xBCA410` thiscall(`ai` = citizen[+0xB4C]) → `ActionRec*` or null (first node of list at ai+0x60C, +8). **V**
* ActionRec: `+0x00 int actionId` (CitizenAction), `+0x28 cGameData* target` (intrusive),
  `+0x2C Vector3 targetPos` (copied from target), `+0x38 bool hasTarget`,
  `+0x39 App::Property param` (type word at +0x4B; 0x31 = Vector3 used by WalkTo). **V** (DoAction 0xC276F0, 0xBCB0C0, 0xCD0400)

Useful citizen helpers: `0xC233F0` IsChieftain (thiscall, P), `0xC0C4A0` cdecl
`cCreatureCitizen* CastToCitizen(cGameData*)` (V), `0xC22DD0` = vt0xF0 `PlayVoice(int code)`
(0 eat, 3 tool, 7/8/10 battle, 0x29A illegal, 0x29B select, 0x29C arrived; V).

---------------------------------------------------------------------------------------------
## 2. How the player issues orders (cTribeInputStrategy)

Getting the strategy: `TribeUI = *(void**)0x169C2A8` (getter **0xCD41E0**, cdecl, V);
`strategy = TribeUI->vt[0x70]()` (used by 0xB1B2F8 and ChooseInputActionID; P — class of
0x169C2A8 not identified, probably the tribe game-mode UI object).

Message handler: **0xCD1D10** = `cTribeInputStrategy::HandleMessage(uint32 msg, IMessage*)`
(IUnmanagedMessageListener sub-object at strategy+0x3C; listener vtable 0x1477CA0). Relevant
messages: `0x506D38C9` → DoActionGeneric(hovered); `0xFC6F3B` → 0xCD0790 click (walks via
0xCD0400); `0xE39174`/`0xE615BF` → selection clicks; `0xFC6D2C` → deselect; `0x2C0EDC1` →
`tribe->SpawnMember(1)`, `0x2C0EDC2` → `RemoveMember(0,1)` (debug/cheat-like commands).

### Call graph of every player order (all V)
```
right-click / action msg 0x506D38C9
  DoActionGeneric 0xCD0BD0  thiscall(strategy, cGameData* hovered) ret 4
    id = ChooseInputActionID 0xCCF0E0  cdecl(cGameData* obj, const Vector3* pos|0) -> TribeInputAction
    switch(id):
      1      -> WalkSelectedTo 0xCD0400(&groundPos)
      0x11..0x1A -> DoToolActionForAllSelected 0xCD0960(tool)
      0x28 (heal) -> builds vector of selected healers, calls DoActionForObjects 0xCCEAE0(vec,5,target,0)
      others -> DoActionForAllSelected 0xCD08F0(citizenAction, target, 0)
click msg 0xFC6F3B -> 0xCD0790 -> WalkSelectedTo 0xCD0400(pos)   (also called from UI 0xE0BBBD)

DoToolActionForAllSelected 0xCD0960  thiscall(strategy, cTribeTool*) ret 4, returns bool
   damaged tool -> DoActionForAllSelected(24 Repair); type 1..9 -> (3 GrabTool); type 10 firepit -> (12 Party)
DoActionForAllSelected 0xCD08F0  thiscall(strategy, int action, cGameData* target, App::Property* p) ret 0xC
   sel = GetTerrainCursor()->vt[0x5C](&fixed_vector)       // selected spatial objects
   0xCCFE20 thiscall(strategy, vec*, action, target, p) ret 0x10   // + feedback sound per action
     DoActionForObjects 0xCCEAE0 (ecx unused) (vector<cSpatialObject*>* vec, int action, cGameData* target, App::Property* p) ret 0x10
        for each o: c = o->vt[0xB8](0x4F176642) /*Cast to citizen*/; c->DoAction(action, target, p)
        returns 1 if vec empty else 0
WalkSelectedTo 0xCD0400 (ecx unused) (const Vector3* pos) ret 4
   rejects water/unreachable (cPlanetModel 0xB830A0 / 0xB88670 region check -> error sound 0xB062B167)
   for each selected citizen: if current rec is WalkTo with Vector3 param within 1.0 of pos: skip, else
        rec = c->DoAction(4, nullptr, nullptr); App::Property::SetValueVector3 0x6A3570 (rec+0x39, pos)
   then c->vt[0xF0](4)  // voice
```

### cCreatureCitizen::DoAction — **0xC276F0** (V; sym "medium" is correct)
`ActionRec* __thiscall DoAction(cCreatureCitizen* this, int action, cGameData* target, App::Property* param)` ret 0xC.
Body: `cur = 0xBCA410(ai)`; `rec = 0xBCB0C0(ai, action, target, 1 /*replace queue*/)` (thiscall ret 0xC);
copies `param` into rec+0x39 (0x542B80), copies target's position to rec+0x2C, then re-plans.
**No null check on `this`.** Also called by AI code (ac8abb…, ce1639…, d1efc5…), so hooking it
alone cannot tell player orders from AI.

### Action enums (from DoActionGeneric switch; TribeInputAction → CitizenAction)
| InputAction (ChooseInputActionID) | → CitizenAction / call |
|---|---|
| 1 | walk to ground (0xCD0400) |
| 7 | invalid (error sound) |
| 9 | 24 Repair |
| 0xA | 7 Attack (citizen) |
| 0xB | 10 Hunt (animal) |
| 0xC | 15 RaidHuts (hut/tool of other tribe) |
| 0xE, 0x10 | 9 GiftFood (with food-cost check) |
| 0x11–0x1A | tool click → DoToolActionForAllSelected (3 GrabTool / 24 Repair / 12 Party) |
| 0x1B | 14 Bundle |
| 0x1C, 0x1D | 27 GatherMeat |
| 0x1E–0x20 | 0 Eat |
| 0x21 | 8 RaidFood |
| 0x22 | 1 Fish (also sets fishing hotspot) |
| 0x23 | 13 CollectEgg |
| 0x24, 0x25 | 2 Gather |
| 0x27 | 26 Recruit (social) |
| 0x28 | 5 Heal (only members holding heal tool, 0xC22EE0()==9) |
| 0x29 | 20 FeedWild or 25 Tame |
CitizenAction 4 = WalkTo (param = Vector3 property), 6 Sleep, 28 ReturnToTribe (ModAPI enum).

---------------------------------------------------------------------------------------------
## 3. Executing a command on a given member (host)

* Action on object: `DoAction(c, action, target, nullptr)` — **0xC276F0**, thiscall, 3 stack args.
  Equivalent to what the UI does (0xCCEAE0 just loops this). Alternatively build a vector
  `{c+0xC0}` and call 0xCCEAE0(vec, action, target, 0) (ecx can be anything).
* Walk: `rec = DoAction(c, 4, nullptr, nullptr); SetValueVector3(rec+0x39, &pos)`
  (`App::Property::SetValueVector3` 0x6A3570 thiscall(prop, const Vector3*) — sym high).
  Optional voice: `c->vt[0xF0](4)`. (Simpler alternative, U: pass an App::Property holding the
  Vector3 as DoAction's 4th arg — DoAction copies it to rec+0x39.)
* Grab tool: `DoAction(c, 3, tool, nullptr)`; party: `(12, firepit)`; repair `(24, tool|hut)`.
* WalkSelectedTo's reachability check (0xB830A0 on cPlanetModel 0xB3D420) is skipped this way —
  either run it on the guest before sending (call original logic) or accept.

---------------------------------------------------------------------------------------------
## 4. Per-frame update / AI (to suppress on mirrors)

`cCreatureCitizen::Update` = vt 0x60 = **0xC249F0**, thiscall `(this, int dtMs)` ret 4 (V; sym medium is right).
Order inside:
1. return if `!byte[+0x135]` or destroyed;
2. `this->vt[0x70](dtSec)`;
3. **AI**: if `ai=[+0xB4C]`, `*ai` and `city([+0x1010])==0`: build flags, then
   `BehaviorTreeTick` **0xD995C0** — cdecl, 9 dwords (`add esp,0x24`):
   `(float dt, double time, void* btRoot=*ai, cCreatureBase* self, void* ai+0x1D0, int ai[+0x5E8], uint flags, int x)`;
4. if `!byte[+0xB5E]`: `vt[0x64]()`, `vt[0x68](dt)` — locomotion/animation update (keep these on mirrors);
5. misc (vt 0x58 when swimming, hit-sphere at +0x1030, `0xC214F0`).

0xD995C0 is shared: called from cCreatureAnimal::NPCTickAI (0xC09AE9), AvatarTickAI (0xC0409F),
citizen Update (0xC24BAB) and 0xD5842C/0xD6073A/0xD60821/0xD60B40 (other creature kinds).
**Suppress mirrors' AI by hooking 0xD995C0 and skipping when `self` (4th arg) is a mirror.**
(Hooking 0xC249F0 and not calling the original would also kill locomotion/animation.)

Positioning a member directly: spatial at `c+0xC0`: SetPosition vt 0x38 `(const Vector3*)`,
SetOrientation vt 0x3C `(const Quaternion*)`, GetPosition vt 0x2C — same as cCreatureAnimal
(docs/05-re-notes.md 5.4; MoveTo 0xE0 / StopMovement 0xEC on the locomotive object for smooth
driving, as npc.cpp does). Animations: `cCreatureBase::PlayAnimation` 0xC12430 (ret 0xC).
Pending actions are in the AI queue; without BT ticks a queued DoAction is not executed (U).

---------------------------------------------------------------------------------------------
## 5. Creating / removing members, buying tools

| Function | Address | Signature | Conf |
|---|---|---|---|
| cTribe::SpawnMember (vt 0x98) | 0xC983B0 | thiscall(tribe, int age) ret 4 → citizen or **0 if population cap** (`0xC8EC60` max pop) | V |
| cTribe::CreateCitizen | 0xC8E720 | thiscall(tribe, int age) ret 4: `CreateInstance(0x18EB4B7)`; `c->vt[0xE8](city=0, tribe, age, &zeroVec)`; `c[+0x5A8]->vt[0x2C](1)`; `cGameBehaviorManager::Get()(0xB3D330)->vt[0x38](c+0x58)`; age 0 → placed at tribe position | V |
| cTribe::AddMember | 0xC95EF0 | thiscall(tribe, citizen, bool) ret 8; pushes into +0x340, adults also into +0x354; names it | V |
| cCreatureCitizen::Init (vt 0xE8) | 0xC27310 | thiscall(c, cCity*, cTribe*, int age, const Vector3* pos /*zero vec 0x1696354 = auto*/) ret 0x10; sets +0x1010/+0x1014/+0xB34, political id, position | V |
| cTribe::RemoveMember (vt 0x9C) | 0xC96060 | thiscall(tribe, citizen, bool destroy) ret 8: deselects, removes from +0x340/+0x354, behavior-manager remove, `DestroyInstance` when destroy | V |
| "baby born" | 0xCD7490 | thiscall(TribeUI, Vector3 pos **by value**) ret 0xC: `playerTribe->SpawnMember(0)`, set pos/orient, anim 0x640358F, "vcode_baby_born_cry"; callers 0xCD7B11 (in 0xCD78E0), 0xCD8EBE (in 0xCD81E0) | P |
| cTribe::CreateTool (vt 0xB4) | 0xC96160 | thiscall(tribe, int toolType) ret 4: `CreateInstance(0x18C8F0C)`, political id, `SetToolType` (tool vt 0x60), model from `GetTribeToolData` 0xC9D040, push to +0x36C (and +0x380 for 4/5/6). Does **not** position/pay. | V |
| player tool placement start | 0xD130D0 (code at 0xD13C05) | palette item → `GetToolByType`, `tribe->CreateTool(type)`, `tool->vt[0x64](tribe)` (SetTribe), `0xC9D2C0(tool)`, starts drag via 0xD09FA0 | P |
| placement finish | 0xD10F90 | finalizes placed structure; for a cTribeTool sets purchased bit (0xC8ED60), fx; food charge path not located | P/U |
| GetToolByType | 0xC8F860 | thiscall(tribe, int type) ret 4 | V (sym high) |

Member purchase: the hatching path is 0xCD7490 (SpawnMember(0) → baby, becomes adult later,
age +0xB34 0→1). Where food is deducted for "buy baby" was not found (U). For the mod, buying
is best executed on the host by the original UI path, or by `SpawnMember(0)` + food via
`UpdateFoodVisuals`/bundles (shared.cpp).

---------------------------------------------------------------------------------------------
## 6. Selection

* Selection lives on the spatial objects: byte `spatial+0x6C` (`citizen+0x12C`).
  IsSelected = spatial vt 0x50 (0xAD26A0). **SetSelected = spatial vt 0x54; for citizens
  0xC227E0**: thiscall(`spatial = citizen+0xC0`, bool sel) ret 4; plays the "select" voice
  (vt0xF0(0x29B)) on a new selection (V).
* `cGameTerrainCursor::GetTerrainCursor` **0xB30E80** (cdecl; sym "medium" — V), cursor class
  vtable 0x1460A98. Candidate list pointer at **cursor+0x164** (`vector<cSpatialObject*>*`, set by
  vt 0x58 = 0xB2FC40 `SetObjects`). vt 0x5C = **0xB310C0 `GetSelectedObjects(vector* out)`**
  (AddRefs via spatial vt 0xBC), vt 0x64 = 0xB2FFA0 deselect all, vt 0x68 count candidates,
  vt 0x74 = 0xB300C0 `UpdateSelectedObjectCount` (+0x15C), vt 0x7C = 0xB31140 box select,
  vt 0x80 = 0xB308F0 apply set, vt 0x9C = 0xB30F90 / vt 0xA0 = 0xB314C0 click select.
  **Every selection change calls spatial vt 0x54 on objects of the candidate list** (V).
* The tribe candidate list is presumably `&playerTribe->mSelectableMembers` (+0x354) — **U**
  (check at runtime: `*(void**)(cursor+0x164) == tribe+0x354`).
* Restricting selection: hook 0xC227E0 and drop `sel=true` for citizens not owned by the local
  player (`citizen = this - 0xC0`). Covers click, box and group select.

---------------------------------------------------------------------------------------------
## 7. Other (NPC) tribes

* Same classes: NPC tribes are `cTribe` with `mpTribePlanner` (+0x290, AI) and members are
  `cCreatureCitizen` with `+0x1014` = their tribe. Spawn: `Simulator::SpawnNpcTribe` 0xC932E0
  (sym high; it calls `GetToolByType(10)`/`CreateTool(10)` at 0xC9353D). NPC tribe controller:
  `cNpcTribeController::Get` 0xCC8C40 (sym high).
* Planner AI behaviours are functions referenced from .data (e.g. 0xCE36D0, 0xCE38F0,
  0xCE3B00 — "fldz; ret" score functions, use CreateTool/SpawnMember) (P).
* Enumerate all tribes: `cGameNounManager::GetData` **0xB21260**, thiscall(nm, create, clear,
  add, filter, uint32 nounID) ret 0x14 (V by ret count; semantics = ModAPI template), with
  nounID 0x18C6D19 and a filter `obj->vt[0x20]() == nounID`. Or walk all citizens with noun
  0x18EB4B7 and group by `+0x1014`.

---------------------------------------------------------------------------------------------
## Implementation recipe (co-op "Chiefs")

**Ownership.** Host keeps `owner[netId]` for every player-tribe member; netIds assigned by the
host (pointer→id map like npc.cpp). Initial split: alternate by index in `tribe+0x340`.
New members (SpawnMember / AddMember 0xC95EF0 hook on host) go to whoever bought them (host:
default host).

**Guest mirrors.** Both players load the same tribe save, so the guest already has a local
player tribe. Recommended: reuse its members as mirrors (keeps UI, food mat, selection working):
1. On sync start the host sends the roster `{netId, age, specializedTool, pos, owner}`; guest maps
   by index, then reconciles counts with `SpawnMember(age)` / `RemoveMember(c, true)` on its
   local tribe (SpawnMember returns 0 at the population cap → use `CreateCitizen`+`AddMember`).
2. Hook **0xD995C0** (cdecl, 9 dwords): if `self` is a guest mirror → return without ticking.
   (Locomotion/animation in Update still run.)
3. Drive mirrors from host snapshots (10 Hz): position/orientation via spatial vt 0x38/0x3C or
   MoveTo (0xE0) like `drive::` in npc.cpp; HP `+0x5E0`; handheld item `+0xFFC`, tool `+0x102C`;
   current action id from `0xBCA410(c[+0xB4C])->actionId` to pick animations (U: needs a way
   to show tool models; not researched).
4. Guest local NPC tribes / animals: host-owned, mirrored by existing npc.cpp style (out of scope here).

**Order capture (guest).** Hook two functions; when on guest, send and do not call original:
* **0xCCEAE0** `DoActionForObjects(vec*, action, target, param)` (hook as thiscall, ecx ignored,
  ret 0x10): read `vec` (cSpatialObject* = citizen+0xC0), map to netIds, send
  `CmdAction{u16 ids[], u8 action, target}`; return 0 so the UI plays its confirmation sound.
* **0xCD0400** `WalkSelectedTo(const Vector3* pos)` (thiscall, ecx ignored, ret 4): collect
  selection via `GetTerrainCursor()->vt[0x5C](&vec)` (or remember last selection), send
  `CmdWalk{ids[], Vector3 pos}`.
  (Optionally call the original's reachability check first; skipping it is harmless.)
* Target encoding: `{u32 nounId (vt 0x20), Vector3 pos, u16 netId if it is a mirrored creature/
  citizen}`. Host resolves: netId → object, else nearest object with that noun within ~3 m
  (plants/huts/tools/fish spots exist on both sides from the same save).

**Order execution (host).** On `CmdAction`: for each id owned by the sender: `DoAction(c, action,
target, nullptr)` (0xC276F0). On `CmdWalk`: `rec = DoAction(c, 4, 0, 0); SetValueVector3(rec+0x39,
&pos)` (0x6A3570). Validate ownership and `c[+0x1014] == playerTribe`, alive (`!c[+0x21]`).

**Selection restriction (both sides).** Hook **0xC227E0** citizen SetSelected; if `sel` and
`owner[c] != me` → call original with `false` (or return). Host also uses it so the host cannot
command guest members.

**Purchases.** Food is already shared (shared.cpp). Tool/member purchases by a guest:
first version — forward `CmdBuyTool{type, pos}` → host `tool = tribe->vt[0xB4](type)`,
`tool->vt[0x64](tribe)`, set position via tool spatial (+0x34) vt 0x38, charge food; the
placement finalizer (0xD10F90: footprint, purchased bit 0xC8ED60) is skipped → **U, test**.
Guest gets the tool through a tool snapshot/rebuild. Alternatively keep building host-only.

---------------------------------------------------------------------------------------------
## Symbol fixes (proposed lines for overrides_922af7c7.sym — not applied)
```
Simulator::cGameNounManager::GetPlayerTribe=0x00BFC710 ; verified asm: mov eax,[ecx+0x70]; ret
Simulator::cTribeInputStrategy::DoActionGeneric=0x00CD0BD0 ; verified: thiscall(hovered) ret 4, switch on ChooseInputActionID (sym 0xCD0400 was wrong)
Simulator::cTribeInputStrategy::WalkSelectedTo=0x00CD0400 ; verified: (const Vector3*) ret 4, ecx unused, DoAction(4)+SetValueVector3(rec+0x39)
Simulator::cTribeInputStrategy::DoActionForObjects=0x00CCEAE0 ; verified: (vector<cSpatialObject*>*, action, target, prop) ret 0x10, ecx unused
Simulator::cTribeInputStrategy::DoActionForAllSelected=0x00CD08F0 ; verified ret 0xC
Simulator::cTribeInputStrategy::DoToolActionForAllSelected=0x00CD0960 ; verified ret 4
Simulator::cTribeInputStrategy::ChooseInputActionID=0x00CCF0E0 ; verified cdecl (obj, pos)
Simulator::cTribeInputStrategy::HandleMessage=0x00CD1D10 ; verified (listener at +0x3C)
Simulator::cCreatureCitizen::DoAction=0x00C276F0 ; verified thiscall(action,target,prop) ret 0xC, returns ActionRec*
Simulator::cCreatureCitizen::Update=0x00C249F0 ; verified vt 0x60, thiscall(int dtMs) ret 4
Simulator::cCreatureCitizen::Init=0x00C27310 ; verified vt 0xE8 (city,tribe,age,pos*) ret 0x10
Simulator::cCreatureCitizen::SetSelected=0x00C227E0 ; verified spatial vt 0x54, this=citizen+0xC0
Simulator::cCreatureCitizen::GetSpecializedTool=0x00C22EE0 ; verified [+0x102C]
Simulator::BehaviorTreeTick=0x00D995C0 ; verified cdecl 9 dwords, arg4 = creature
Simulator::cTribe::CreateCitizen=0x00C8E720 ; verified (age) ret 4
Simulator::cTribe::AddMember=0x00C95EF0 ; verified (citizen,bool) ret 8
Simulator::cTribe::RemoveMember=0x00C96060 ; verified vt 0x9C (citizen,bool destroy) ret 8
Simulator::cGameTerrainCursor::GetTerrainCursor=0x00B30E80 ; verified (creates noun 0x18C40BC)
Simulator::cGameTerrainCursor::GetSelectedObjects=0x00B310C0 ; verified vt 0x5C
Simulator::cTribeUI::Get=0x00CD41E0 ; verified returns [0x169C2A8], class name unverified
```

## Open questions
* Which object holds the tribe selection candidate list (cursor+0x164) — runtime check.
* Food cost / validation path for buying a tool or a baby (UI side) not found.
* How the held-tool model / gather/eat animations are shown — needed for good-looking mirrors.
* Whether mID (+0x24) is identical between host and guest after loading the same save.
