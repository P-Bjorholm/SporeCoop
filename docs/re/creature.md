# Creature stage (RE notes)

Marks: ✔ verified (code read or seen at runtime), ~ likely, ? open.
Structures of `cCreatureAnimal` and the NPC sync: [05-re-notes.md](../05-re-notes.md) 5.4-5.6.

## 1. Playing through input (test bots, 2026-10-23)

* ✔ A left click on a creature walks the avatar to it and selects it (its health bar and species card
  appear); a click on the ground walks there. A click on the partner's puppet does not move the host
  (the selection of puppets is guarded, `CreatureSelectionUI`).
* ✔ The ability bar (bottom centre) has two stance buttons above it: social (green) at
  `(W/2 - 30, H - 82)` and combat (red) at `(W/2 + 43, H - 82)` (1440x1080: 690,998 / 763,998).
  The keys 1..4 use the abilities of the current stance. The fixture's creatures have one ability
  per stance (combat: bite / strike, social: sing). The stance a save starts in is social.
* ✔ Combat: the selected target takes ~1 damage per key press at melee range (Kook: 6-7.5 hp, the
  host's creature 10 hp, the guest's Kirgle 16 hp); a Kook nest fights back as a group.
* ✔ Social: key 1 on a Kook (social stance, next to it) impresses it ("Impress 3 Kook to ally" card,
  1/3) and pays 10 DNA on the host.
* ✔ Kill DNA: Kook 7.0-8.8 DNA, paid inside `TakeDamage` (below).

## 2. Kill credit in `cCreatureAnimal::TakeDamage` (0xC075A0)

* ✔ The DNA of a kill is paid inside TakeDamage: `AddEvolutionPoints` is called at 0xC07BAC
  (return address 0xC07BB1, seen at runtime) with the value of `0xC04260(victim, 0)` (thiscall,
  returns the victim's DNA worth as a float). Right after it 0xC75820 (via 0xF67D40) gets the same
  value (~ the "+N DNA" popup / statistics).
* ✔ It is paid only when the credit flag (`[esp+0x80]`) is set at 0xC07A78: the attacker creature
  (`ebp`, the attacker combatant's owner when its noun is 0x18EB45E, cCreatureAnimal) answers true
  to its spatial vtable +0x58 (~ "is the player's avatar"), or its species' relation to the player
  (0xB1FCD0 / 0xBA40C0) is 6 (~ ally) and a meter at victim+0xA38 passes a threshold.
* ✔ Consequence for co-op: a guest's hit is applied on the host with the guest's puppet as the
  attacker, the puppet is not the avatar, so guest kills paid nothing (no DNA for anybody, no goal
  progress). Fixed in `npc.cpp`: a lethal guest hit is applied as the host avatar's
  (`guest_kills=` in `coop.npc` on the host); regression check in `coop_team.py`.

## 3. The push vector of `TakeDamage`

* ✔ The 4th argument (`const Vector3*`) is the hit's push (knockback impulse), not a position: a
  bite or strike of the player (type 2) passes (0,0,0) (runtime log of every real call,
  2026-10-23). Our synthetic hits passed the victim's position (|v| ~ 505 on Scalinus): a forwarded
  guest hit pushed the NPC on the host, a 1000-damage kill threw the corpse ~80 m up, and the guest's
  local death replay (1e6 damage) threw its proxy the same way; the proxy corpse then hung 15-80 m
  away from the host's corpse until it was removed (`teleport` / `set_position` do not hold a dead
  creature). `game::apply_damage` / `game::hit` pass (0,0,0) now; the death replay uses a just-lethal
  hit. Regression: the corpse check in `coop_team.py`.

## 4. The avatar-event handler and +0xE84

* ✔ `Simulator::CreatureStageEventHandler` 0xD47FC0: a vtable method (pointer at .rdata 0x147B680),
  `thiscall(self, u32 msgId, msg*) ret 8 -> bool`; it handles the messages 0x61B1320, 0x49B56D7,
  0x5371F11, 0x60B4123, 0xD335362C, 0xD3353635, 0xD335363A when `msg+0` is the avatar (checked with
  0xB1FCD0). On the 0x60B4123 / 0xD335362C path it walks creatures and, for an adult (`+0xB34 == 1`,
  0xC0B890) creature of another species with `+0xB54` set, reads `(+0xE84)->+0x32C` (0xC0C2C0 is
  the getter of +0xE84) without a null check: our puppets and NPC proxies have no +0xE84 object, the
  guest faulted at 0xD48634 (data 0x32C) while it fought proxies. Same field as the
  `CreatureSelectionUI` crash (coop.cpp guards both: during this handler the puppets and proxies
  get a blank object whose +0x32C is -1, "none"). +0xE84 is the creature's archetype tuning, set by
  `Create` from the herd's archetype (docs/re/tribe.md E: +0x388 = isEpic); our herds have none.
  ? the meaning of tuning +0x32C.
* ✔ 0xC0B890: `+0xB34 == 1` (age: 1 = adult ~). 0xC04260 `thiscall(victim, 0)` -> float: the DNA a
  kill of this creature is worth.

## 5. What a player meets that tests did not do (play_creature.py)

* ✔ The pause menu (Esc) is "Options": Resume Game, Spore Guide, Spy Glass, Ban, Settings, Save,
  Exit to Main Menu, Quit Spore. `coop.ui find "<caption>"` lists a window with its parents
  (rectangles relative to the parent, flag 0x1 = visible); `tools/tests/uifind.py` turns that into a
  screen point. "Exit to Main Menu" asks "Do you want to save your game?" (Don't Save / Cancel /
  Save), then "Are you sure? All progress will be lost!" (Don't Save again). The menu stays
  "visible" under the dialogs it opened: check for the dialog, not for the menu to go away.
* ✔ A guest that exits to the main menu stays in the session (galaxy menu, co-op lobby open in the
  Co-op item); since 2026-10-23 the host sends its world again after 5 s (back in ~14 s).
* ✔ The mating call is Backspace (blue bar, middle button "Mating Call [Backspace]"). With DNA 0
  nothing happens; with DNA the "Mate: Locate your Mate!" card appears and a thought bubble shows
  over the nest. Clicking the nearby family members did not start the editor within 30 s (not
  explored further; the evolution vote is tested through `coop.vote editor`).
* ✔ The ability bar keys work on an unfocused test window (background mode); WASD and the arrow
  keys move the avatar; a right-drag turns the camera.
* ✔ On the guest, the goal card of a species whose local creatures were removed (NPCs are the
  host's) says "The <species> species is extinct!" while the host's card says "Impress 3 <species>
  to ally" (docs/known-issues.md).

## 6. Herds of our own creatures fill themselves up

* ✔ `CreateHerd(pos, species, 0, 0, 0, 0)` (puppets of players with their own creature, NPC proxies
  on the guest) gives a herd with `mTargetHerdSize` (+0xF4) = 5 and `mbEnabled` (+0x218) = 1 a
  moment later (runtime, 2026-10-23; docs/re/tribe.md E has the herd fields). The game then spawns
  members up to the target: on the host 4 clones of the guest's Kirgle stood at the nest within
  seconds of the join, as real NPCs, mirrored to the guest. On the guest the proxies' herds did the
  same, the update filter removed them again (`local_removed` grew all session). `game::herd_quiet`
  sets the target to 0 and disables such herds (at creation and on every puppet update).
  Regression: `coop_team.py` ("no NPC clones of the guest's creature").

## 7. When the host's simulation stands still

* ✔ In the host's pause menu, in the death caption ("You were killed in battle! ..." with a green
  check that waits for OK, like the space captions in docs/06-testing.md 6.9) and in cinematics no
  creature is updated (`cCreatureAnimal::Update` is not called: the registry of the frame is
  empty). `npc.cpp` used to forget every NPC 2 s later and the guests lost them all until the host
  went on; a frame with no creature update now counts as a pause (the NPCs stay mirrored, standing
  still). Regression: `coop_exit_menu.py` (host paused 6 s: 21 -> 0 NPCs before, 21 -> 21 after).
* ✔ The death caption is `coop.ui find "You were killed"`; `key enter` presses its OK.
