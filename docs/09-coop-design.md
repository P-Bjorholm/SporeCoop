# 9. Concept: playing together, not side by side

Before this version every player played in their own copy of the host's world, and only resources
and visibility were shared. The new goal: **one world, one team**. All players are in the same
world and affect it, each has their own role and their own property, and the best results come
only from working together.

## 9.1 Three rules for all phases

1. **One world, simulated by the host.** Everything that moves and lives (creatures, tribe members, vehicles,
   ships) is simulated only by the host's game. Instead of their own copies, guests have
   "mirrors" of the host's objects (like the NPCs in the Creature phase today). So the worlds never diverge.
2. **Everything has an owner.** An avatar, a tribe member, a city, a vehicle, a ship belongs to
   a specific player. A guest sends the owner's commands to the host, the host executes them, and everyone sees
   the result. You cannot command someone else's property, but you can see it (owner's label and color).
3. **Together is better.** Every phase has mechanics that only work when players are
   close to each other or act at the same time: assistance, revival, joint attacks, shared progress.

## 9.2 Phases

### Cell — "Symbiosis"
* One tide pool: NPC cells, food and predators are shared (simulated by the host, mirrored on guests).
* **Bond.** If players' cells are close (≤ 6 m), both regenerate health faster.
* **Knockout and revive.** A lethal hit (if a partner is in the same pool) does not kill but
  stuns the cell for 10 s: it freezes and is invulnerable. A partner who swims right next to it
  (≈ 3 m) and stays there for 1.5 s revives it with half health. If they fail, the
  cell respawns next to the partner but **small** (half size); it
  grows back by eating (50 food points — full size) or at the next level.
  Progress (evolution bar, parts) is not lost. Alone in the pool — normal Spore death.
* **Hunting together.** Damage to a large cell adds up, food is shared by everyone.
* A shared evolution bar (like DNA).

### Creature — "Pack"
* Already there: one planet, shared NPCs, fights via the host, shared DNA.
* **Revive.** If a player dies and a partner is nearby, the partner can pick them up
  (walk up and hold a social action / key), otherwise they respawn at the nest.
* **Joint hunting.** If two players hit the same target, each one's damage is +25 %.
* **Joint socializing.** Any player's social actions advance friendship with the same
  nest (counted by the host) — with several players, befriending is easier.
* **Epics** are stronger but give more DNA: they are nearly impossible to beat alone.

### Tribe — "Chiefs"
* One tribe, shared food and huts. **Tribe members are split between players**: the host has their own,
  each guest has their own (equal shares by default; new members go to whoever "bought" them).
* Everyone selects and leads **their own** members: hunting, gathering, attacking, music.
  Guest commands go to the host, and members move identically for everyone.
* Roles emerge naturally: one hunts and feeds, another fights, a third makes friends.
* Buildings and new members are bought from shared food; whoever bought it owns it.

### Civilization — "Governors"
* One civilization, a shared treasury. **Cities are split between players**: the capital goes to the host,
  the rest are handed out in the lobby, and a captured or bought city goes to the player whose
  vehicles took it.
* In **their own** city a player builds buildings and buys vehicles. In someone else's they cannot
  (hint: "Alice's city").
* **Vehicles** belong to the buyer, and only the buyer drives them. Joint attacks: a city
  is captured faster if vehicles of several players storm it.
* Diplomacy (alliances, trade, religion) is the host's; guests make suggestions via ping and chat.

### Space — "Squadron"
* One empire, a shared budget, shared colonies. Everyone has their own ship.
* **Wing.** Ships in the same system: enemies and pirates are shared (simulated by the host),
  guests' shots hit the same targets.
* **Colonies.** Anyone can found a colony; it is shared. Defending colonies from raids
  is easier with two: one fights off pirates, the other repairs/delivers.
* Freedom: you can scatter across the galaxy, but missions and wars pay off better together.

### Adventures
* Everyone shares one adventure (guests auto-launch the host's adventure), with a party of captains.

## 9.3 Phase transitions — "Vote"
* When the phase's shared progress is complete, **everyone** gets a window "Move to Tribe?
  Ready: Alice ✔, Bob …". The transition starts when everyone is ready or after 60 s.
* The host goes through the editor and the cutscene, then the world is sent to guests automatically.
* A guest cannot advance alone: their transition button leads to the vote.
* An editor within a phase (for example, the creature editor after the mating call) does not kick guests out.

## 9.4 What already works (verified by automated tests on two game instances)

| Phase | Done | Module | Test |
|---|---|---|---|
| All | Evolution vote: the host's request to enter an editor (new phase or evolution within a phase) waits until everyone presses "I'm ready", 60 s, or "Start now"; once the host returns from the editor or enters the new phase, the world is immediately sent to everyone ("evolving together"); a guest's editor session is not interrupted | `coop.cpp` (`h_Submit`, `on_mode_change`) | `coop_vote.py` |
| Cell | One pool: the host's NPCs and food appear on the guest as passive mirrors, the guest has none of its own; guest bites are applied by the host; bites by the host's NPCs on the guest's cell are sent to the guest; shared food counter; "Symbiosis" — a 10 s knockout (visible to all: translucent cell, "KO n", revive bar, off-screen arrow to the partner, game effects), revive by a partner right next to you, otherwise respawn next to them as a small cell (grows by eating); healing near a partner; the partner's cell is exactly in place and faces the same way (pose is set right after the simulation step) | `cellsync.cpp` | `coop_cell.py` |
| Creature | Joint hunting +25 %, "The pack protects" (40 % health instead of death, once a minute), regeneration near a partner; NPCs, fights, DNA shared (existing) | `teamplay.cpp`, `npc.cpp` | `coop_team.py`, `coop_two_instances.py` |
| Tribe | All members of all tribes are host mirrors; the player tribe's members are split between players; you can select and command only your own; guest orders (move, attack, hunt, gather…) are executed by the host; animals around the tribe are shared; owner labels | `tribe.cpp` | `coop_tribe.py` |
| Civilization | All vehicles are host mirrors; cities are split (the capital is shared while there are fewer cities than players); a vehicle belongs to its buyer; guest purchases, orders and buildings go through the host; host buildings and captures are replayed on guests; owner labels | `civ.cpp` | `coop_civ.py` |
| Space | In the host's system/on the host's planet the NPC ships are the host's, the guest has mirrors (the guest's own NPCs there are removed); guest shots are applied by the host; NPC hits on the guest's ship are sent to the guest; mirrors do not fire on their own | `squadron.cpp` | `coop_space.py` |

Not done yet: shared colonies in space (analysis in docs/re/space.md §5), NPC civilizations' city growth
on the guest still runs locally (the host's buildings do arrive, though), auto-launching the adventure on guests.

## 9.4.1 Your own creations

Each player has their own creature, cell and ship (`mods/coop/creations.cpp`,
analysis — [docs/re/creations.md](re/creations.md)):
* your creation is taken from the player's "My Creations" (`EditorSaves.package` in their profile): for a guest
  the newest one by default, for the host — the species of their world, the ship — their own empire's ship. In the F9 window
  the line "My creature: Kirgle < >" switches it; the choice is stored in the profile
  (`Spore/SporeCoop/choices.ini`);
* the creation's records (`bem` + metadata) go to the host, which distributes them to everyone, including new players. On
  every machine the creation gets the same new ID (player + content hash: creation IDs
  in different profiles collide), is written to a small package and attached to the
  game's resources on the fly (`DatabasePackedFile` + `RegisterDatabase`). The host's creations are also registered under their
  original ID above the guest's "My Creations" — that is how the guest gets the host world's species;
* a player's copy is built from their creation; your own avatar gets your own creature
  (`SetSpecies`: health and abilities from its parts) — 3 s after the world loads; your own
  cell — the player's model + a level rebuild (the cell hatches again next to the partner);
* evolution vote: the host and guests open the editor for **their own** creature; a host world
  that arrives while the guest is in the editor is loaded after the guest leaves the editor; after the editor
  the edited creature becomes the player's own and is sent to the others.

Not ready: on a guest, the editor's "done" button for a creature from "My Creations" does not leave
the editor (DNA is not the cause, cancel works) — under investigation; the player's ship is visible to others
but not tested separately; tribe and civilization members use the host world's species.

## 9.5 Implementation order
1. Transition vote, editor fix, owner colors/labels. (all phases)
2. Creature: revive, joint hunting, shared socializing.
3. Tribe: mirroring host members on guests, owners, guest commands via the host.
4. Civilization: mirroring vehicles, city and vehicle owners, buildings via the host.
5. Cell: mirroring NPC cells, bond, rescue.
6. Space: shared enemies in the system, guest shots via the host, shared colonies.

Each step is verified by an automated test on two game instances. Status is in the README.
