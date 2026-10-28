// Coop session: host (server for up to 3 guests) or client. Game-agnostic.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "connection.h"
#include "protocol.h"
#include "transport.h"

namespace net {

enum class Role { None, Host, Client };

struct Peer {
    uint8_t id = 0xFF; // player id (host side: guest id; client side: 0 = host)
    std::string name;
    bool accepted = false;
    bool bye = false; // said goodbye: dropped in update()
    Connection conn;
};

class Session {
public:
    struct Callbacks {
        std::function<void(const std::string&)> log;
        std::function<void()> connected;                       // client: welcome received
        std::function<void(const std::string&)> disconnected;  // client: lost / rejected / host left
        std::function<void(const PlayerInfo&)> player_join;
        std::function<void(uint8_t id, uint8_t reason)> player_leave;
        std::function<void(const PlayerState&)> player_state;
        std::function<void(uint8_t id, const std::string& text)> chat;
        std::function<void(uint8_t type, uint8_t from, Reader& payload)> custom; // other msgs
    } cb;

    uint32_t build = 0;      // exe build id; mismatch = reject
    // BUG-0141: names are unique in a session (the host appends " 2", " 3" to a taken name; the
    // guest takes its name from the roster). Space tables keyed by name mixed up two players with
    // the same (default) name. false = the old behaviour (tests: coop.corefix unique_names 0)
    bool unique_names = true;
    uint32_t world = 0;      // host world id (save hash), sent in WELCOME
    uint32_t timeout_ms = 30000; // the game can block for a long time while saving/loading
    std::string lobby_info;  // host: shown to players looking for games on the network

    bool host(uint16_t port, const std::string& name, std::string& err);
    bool join(const std::string& addr, const std::string& name, std::string& err);
    void leave(const char* why = "left");
    void update(); // call every frame: receive, timeouts, send

    void send_state(PlayerState st);              // own state
    void send_chat(const std::string& text);
    void broadcast_reliable(uint8_t type, const Writer& w, int except_id = -1);
    bool send_reliable_to(uint8_t id, uint8_t type, const Writer& w); // host: guest id, client: 0
    // player event (reliable): client -> host, host relays to the other guests
    void send_event(uint8_t type, const Writer& w);
    size_t pending_to(uint8_t id) const;
    bool send_unreliable_to(uint8_t id, uint8_t type, const Writer& w);

    Role role() const { return role_; }
    uint8_t my_id() const { return my_id_; }
    bool connected() const { return role_ == Role::Host || (role_ == Role::Client && welcomed_); }
    std::string name_of(uint8_t id) const;
    std::vector<PlayerInfo> players() const; // including self
    std::string status() const;
    uint16_t port() const { return tr_->local_port(); }
    // round trip time to a player in ms (host: any guest, guest: the host), -1 if unknown
    int rtt_of(uint8_t id) const;
    std::string host_address() const; // guest: "ip:port" of the host
    size_t peer_count() const { return peers_.size(); } // host: accepted and half-open peers

    // test seam (tests/fuzz_decoders.cpp): another network and clock; nullptr = UDP and now_ms()
    void set_transport(Transport* t) { tr_ = t ? t : &udp_; }
    uint32_t (*clock)() = nullptr;

private:
    void handle_packet(Peer& p, const uint8_t* data, size_t n, uint32_t now);
    void handle_msg(Peer& p, uint8_t type, Reader& r);
    void flush(uint32_t now);
    Peer* find_peer(const Endpoint& e);
    void drop_peer(size_t idx, uint8_t reason);
    uint8_t alloc_id() const;
    bool retired(uint32_t conn) const;

    Role role_ = Role::None;
    uint32_t now() const { return clock ? clock() : now_ms(); }
    UdpTransport udp_;
    Transport* tr_ = &udp_;
    std::string my_name_;
    uint8_t my_id_ = 0;
    bool welcomed_ = false;
    uint32_t join_started_ = 0;
    std::vector<std::unique_ptr<Peer>> peers_;
    std::vector<PlayerInfo> roster_; // client: known players (incl. host)
    std::vector<uint32_t> retired_;  // host: conn ids of dropped peers (their late packets are ignored)
    // BUG-0029: a leave() asked for while a Connection is parsing a packet (REJECT, BYE, a callback)
    // waits until on_packet has returned: leave() frees the peers and with them that Connection
    bool delivering_ = false;
    bool leave_pending_ = false;
    std::string pending_why_;  // leave() reason
    std::string pending_disc_; // cb.disconnected text after the deferred leave ("" = none)
};

} // namespace net
