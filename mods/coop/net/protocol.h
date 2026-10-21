// SporeCoop wire protocol: message ids and payload encoders/decoders (portable).
// See docs/04-netcode.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "buffer.h"

namespace net {

constexpr uint32_t kMagic = 0x4F435053; // 'SPCO'
constexpr uint16_t kProtoVersion = 1;
constexpr uint16_t kDefaultPort = 47810;
constexpr int kMaxPlayers = 4; // host + 3 guests (Spore pack size)
constexpr size_t kMaxPacket = 1200;

enum MsgType : uint8_t {
    MSG_HELLO = 1,
    MSG_WELCOME = 2,
    MSG_REJECT = 3,
    MSG_PLAYER_JOIN = 4,
    MSG_PLAYER_LEAVE = 5,
    MSG_PLAYER_STATE = 6,
    MSG_CHAT = 7,
    MSG_PING = 8,
    MSG_PONG = 9,
    MSG_BYE = 10,
    MSG_ABILITY = 11,  // M2
    MSG_DAMAGE = 12,   // M2
    MSG_NPC_STATE = 13, // M2
    MSG_WORLD_OFFER = 14, // host -> guest: a save folder follows
    MSG_WORLD_FILE = 15,  // file header
    MSG_WORLD_CHUNK = 16, // file data
    MSG_WORLD_DONE = 17,  // end of transfer
    MSG_ANIM = 18,        // a player's creature played an animation (attack, social, ...)
};

struct AnimEvent {
    uint8_t id = 0;
    uint32_t anim = 0;
    int32_t block = -1;
    uint8_t flags = 0; // 1 = disable blend-in, 2 = was PlayAnimationTo (had a target)
    void write(Writer& w) const { w.u8(id); w.u32(anim); w.u32((uint32_t)block); w.u8(flags); }
    bool read(Reader& r) { id = r.u8(); anim = r.u32(); block = (int32_t)r.u32(); flags = r.u8(); return r.ok(); }
};

enum RejectReason : uint8_t {
    REJ_VERSION = 1,
    REJ_FULL = 2,
    REJ_BUILD = 3,
};

enum PlayerFlags : uint8_t {
    PF_IN_WORLD = 1,  // player has an avatar in the creature stage
    PF_MOVING = 2,
    PF_RUNNING = 4,
    PF_JUMPING = 8,
    PF_DEAD = 16,
    PF_SWIMMING = 32,
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
};
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

struct Hello {
    uint16_t proto = kProtoVersion;
    uint32_t build = 0; // first 4 bytes of exe sha256
    std::string name;
    void write(Writer& w) const { w.u16(proto); w.u32(build); w.str8(name); }
    bool read(Reader& r) { proto = r.u16(); build = r.u32(); name = r.str8(); return r.ok(); }
};

struct PlayerInfo {
    uint8_t id = 0;
    std::string name;
};

struct Welcome {
    uint8_t your_id = 0;
    uint32_t world = 0;
    std::vector<PlayerInfo> players;
    void write(Writer& w) const
    {
        w.u8(your_id);
        w.u32(world);
        w.u8((uint8_t)players.size());
        for (auto& p : players) { w.u8(p.id); w.str8(p.name); }
    }
    bool read(Reader& r)
    {
        your_id = r.u8();
        world = r.u32();
        uint8_t n = r.u8();
        players.clear();
        for (int i = 0; i < n && r.ok(); i++) {
            PlayerInfo p;
            p.id = r.u8();
            p.name = r.str8();
            players.push_back(p);
        }
        return r.ok();
    }
};

struct Reject {
    uint8_t reason = 0;
    std::string text;
    void write(Writer& w) const { w.u8(reason); w.str8(text); }
    bool read(Reader& r) { reason = r.u8(); text = r.str8(); return r.ok(); }
};

struct PlayerJoin {
    PlayerInfo p;
    void write(Writer& w) const { w.u8(p.id); w.str8(p.name); }
    bool read(Reader& r) { p.id = r.u8(); p.name = r.str8(); return r.ok(); }
};

struct PlayerLeave {
    uint8_t id = 0, reason = 0;
    void write(Writer& w) const { w.u8(id); w.u8(reason); }
    bool read(Reader& r) { id = r.u8(); reason = r.u8(); return r.ok(); }
};

struct PlayerState {
    uint8_t id = 0;
    uint32_t time_ms = 0; // sender clock
    Vec3 pos;
    Quat rot;
    Vec3 vel;
    uint8_t flags = 0;
    uint8_t anim = 0;
    float health = 0, max_health = 0;
    uint32_t species = 0; // instance id of species key (appearance hint)

    void write(Writer& w) const
    {
        w.u8(id); w.u32(time_ms);
        w.f32(pos.x); w.f32(pos.y); w.f32(pos.z);
        w.f32(rot.x); w.f32(rot.y); w.f32(rot.z); w.f32(rot.w);
        w.f32(vel.x); w.f32(vel.y); w.f32(vel.z);
        w.u8(flags); w.u8(anim);
        w.f32(health); w.f32(max_health);
        w.u32(species);
    }
    bool read(Reader& r)
    {
        id = r.u8(); time_ms = r.u32();
        pos.x = r.f32(); pos.y = r.f32(); pos.z = r.f32();
        rot.x = r.f32(); rot.y = r.f32(); rot.z = r.f32(); rot.w = r.f32();
        vel.x = r.f32(); vel.y = r.f32(); vel.z = r.f32();
        flags = r.u8(); anim = r.u8();
        health = r.f32(); max_health = r.f32();
        species = r.u32();
        return r.ok();
    }
};

struct Chat {
    uint8_t id = 0;
    std::string text;
    void write(Writer& w) const { w.u8(id); w.str16(text); }
    bool read(Reader& r) { id = r.u8(); text = r.str16(); return r.ok(); }
};

struct WorldOffer {
    uint32_t world_id = 0; // crc32 of all files
    uint32_t total_size = 0;
    uint16_t file_count = 0;
    std::string planet; // display name
    void write(Writer& w) const { w.u32(world_id); w.u32(total_size); w.u16(file_count); w.str8(planet); }
    bool read(Reader& r) { world_id = r.u32(); total_size = r.u32(); file_count = r.u16(); planet = r.str8(); return r.ok(); }
};

struct WorldFile {
    uint16_t index = 0;
    uint32_t size = 0;
    std::string path; // relative, '/' separated
    void write(Writer& w) const { w.u16(index); w.u32(size); w.str8(path); }
    bool read(Reader& r) { index = r.u16(); size = r.u32(); path = r.str8(); return r.ok(); }
};

struct WorldChunk {
    uint16_t index = 0;
    uint32_t offset = 0;
    std::vector<uint8_t> data;
    void write(Writer& w) const { w.u16(index); w.u32(offset); w.u16((uint16_t)data.size()); w.raw(data.data(), data.size()); }
    bool read(Reader& r)
    {
        index = r.u16(); offset = r.u32();
        uint16_t n = r.u16();
        if (!r.ok() || r.left() < n) return false;
        data.assign(r.cur(), r.cur() + n);
        r.skip(n);
        return r.ok();
    }
};

uint32_t crc32_update(uint32_t crc, const uint8_t* p, size_t n);

} // namespace net
