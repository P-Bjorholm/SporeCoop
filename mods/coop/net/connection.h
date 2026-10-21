// One logical connection over UDP: seq/ack/ackbits, reliable-ordered + unreliable messages.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <vector>

#include "buffer.h"
#include "udp.h"

namespace net {

inline bool seq_greater(uint16_t a, uint16_t b)
{
    return ((a > b) && (a - b <= 32768)) || ((a < b) && (b - a > 32768));
}

struct ConnStats {
    uint32_t packets_sent = 0, packets_recv = 0, resends = 0, acked = 0;
};

class Connection {
public:
    using Deliver = std::function<void(uint8_t type, Reader& payload)>;

    Endpoint remote;
    float rtt_ms = 100.f;
    uint32_t last_recv_ms = 0;
    uint32_t created_ms = 0;
    ConnStats stats;

    void send_reliable(uint8_t type, const Writer& w);
    void send_unreliable(uint8_t type, const Writer& w);
    size_t pending_reliable() const { return rel_out_.size(); }

    // Builds the next datagram if anything is due (messages, acks, keepalive).
    bool build_packet(uint32_t now, std::vector<uint8_t>& out);
    // Parses a datagram, updates acks, delivers messages in order. false = invalid packet.
    bool on_packet(const uint8_t* data, size_t n, uint32_t now, const Deliver& deliver);

    static bool looks_valid(const uint8_t* data, size_t n);

private:
    struct OutMsg {
        uint8_t type;
        uint16_t rel_id;
        std::vector<uint8_t> payload;
        uint32_t last_sent = 0;
        int sends = 0;
    };
    struct SentPkt {
        uint16_t seq;
        uint32_t time;
        std::vector<uint16_t> rel_ids;
        bool acked = false;
    };

    void ack_packet(SentPkt& p, uint32_t now);

    uint16_t local_seq_ = 0;
    uint16_t remote_seq_ = 0;
    uint32_t recv_bits_ = 0;
    bool have_remote_ = false;
    bool ack_pending_ = false;
    uint32_t last_send_ms_ = 0;

    std::deque<SentPkt> sent_;
    uint16_t next_rel_id_ = 0;
    std::deque<OutMsg> rel_out_;
    uint16_t next_rel_expected_ = 0;
    std::map<uint16_t, std::pair<uint8_t, std::vector<uint8_t>>> rel_in_;
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> unrel_out_;
};

} // namespace net
