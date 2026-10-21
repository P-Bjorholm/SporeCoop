// Thin, offset-based access to Spore creature-stage objects (see docs/05-re-notes.md).
#pragma once

#include <cstdint>

#include "net/protocol.h"
#include "sporekit.h"

namespace game {

// cCreatureAnimal layout (GOG 2016 build, matches ModAPI March2017 headers)
constexpr uintptr_t kSpatialOff = 0xC0;     // cLocomotiveObject/cSpatialObject subobject
constexpr uintptr_t kPosOff = 0xC0 + 0x04;  // cSpatialObject::mPosition
constexpr uintptr_t kRotOff = 0xC0 + 0x10;  // cSpatialObject::mOrientation
constexpr uintptr_t kVelOff = 0xC0 + 0x1C8; // cLocomotiveObject::mVelocity
constexpr uintptr_t kCombatOff = 0x5A8;     // cCombatant subobject
constexpr uintptr_t kMaxHpOff = 0x5A8 + 0x2C;
constexpr uintptr_t kHpOff = 0x5A8 + 0x38;
constexpr uintptr_t kSpeciesOff = 0xB20;    // cSpeciesProfile*
constexpr uintptr_t kSpeciesKeyOff = 0xB28; // ResourceKey
constexpr uintptr_t kFlagsOff = 0xB58;      // bit 0x200 = avatar
constexpr uintptr_t kDeadOff = 0xB5E;       // bool mbDead
constexpr uintptr_t kNounAvatarOff = 0x54;  // cGameNounManager::mpAvatar
constexpr uintptr_t kNounAvatarHerdOff = 0x58; // cGameNounManager::mpAvatarHerd
constexpr uintptr_t kHerdOff = 0x1674;     // cCreatureAnimal::mHerd

bool init(const SkHost* host, SkMod* mod); // resolve symbols; false = coop disabled
bool ready();                              // symbols ok
uint32_t mode_id();
bool in_creature_stage();
void* avatar();
net::Vec3 position(void* c);
net::Quat orientation(void* c);
net::Vec3 velocity(void* c);
float health(void* c);
float max_health(void* c);
bool is_dead(void* c);
uint32_t species_instance(void* c);
void* species_profile(void* c);

// Every creature must belong to a herd: game code calls GetHerd() on neighbours
// without a null check (0xD41A20 -> 0xC046B0, see docs/05-re-notes.md).
void* avatar_herd();
void* spawn_creature(const net::Vec3& pos, void* species, void* herd);
void destroy_creature(void* c);
void teleport(void* c, const net::Vec3& pos, const net::Quat& rot);
void set_orientation(void* c, const net::Quat& rot);
void move_to(void* c, const net::Vec3& dst, float stop_dist);
void stop(void* c);
void set_speed(void* c, float speed);
float scale(void* c);
float bounding_radius(void* c);
void set_scale(void* c, float s);

// Animation events: called for every PlayAnimation/PlayAnimationTo on any creature.
using AnimListener = void (*)(void* creature, uint32_t anim_id, int block, bool disable_blend, bool to_target);
void set_anim_listener(AnimListener fn);
void play_animation(void* c, uint32_t anim_id, int block, bool disable_blend);

// AI suppression for puppets: NPCTickAI is skipped for registered creatures
void puppet_register(void* c);
void puppet_unregister(void* c);
bool is_puppet(void* c);

} // namespace game
