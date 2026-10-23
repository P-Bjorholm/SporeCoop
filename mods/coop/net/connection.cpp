#include "connection.h"

#include <algorithm>
#include <cstring>

#include "protocol.h"

namespace net {

// header: magic u32, proto u16, seq u16, ack u16, ackbits 2 x u64 (128 packets), count u8
static constexpr size_t kHeader = 4 + 2 + 2 + 2 + 16 + 1;

bool Connection::looks_valid(const uint8_t* data, size_t n)
{
    if (n < kHeader)
        return false;
    Reader r(data, n);
    return r.u32() == kMagic && r.u16() == kProtoVersion;
}

void Connection::send_reliable(uint8_t type, const Writer& w)
{
    OutMsg m;
    m.type = type;
    m.rel_id = next_rel_id_++;
    m.payload = w.data;
    rel_out_.push_back(std::move(m));
}

void Connection::send_unreliable(uint8_t type, const Writer& w)
{
    unrel_out_.emplace_back(type, w.data);
}

bool Connection::build_packet(uint32_t now, std::vector<uint8_t>& out)
{
    uint32_t rto = (uint32_t)std::max(100.f, rtt_ms * 1.5f + 20.f);
    bool any_rel_due = false;
    for (auto& m : rel_out_)
        if (m.sends == 0 || now - m.last_sent >= rto) {
            any_rel_due = true;
            break;
        }
    uint32_t since = now - last_send_ms_;
    bool keepalive = since >= 250 || (ack_pending_ && since >= 30);
    if (unrel_out_.empty() && !any_rel_due && !keepalive)
        return false;

    Writer w;
    w.u32(kMagic);
    w.u16(kProtoVersion);
    uint16_t seq = local_seq_++;
    w.u16(seq);
    w.u16(remote_seq_);
    AckBits bits = have_remote_ ? recv_bits_ : AckBits{};
    w.raw(&bits.lo, 8);
    w.raw(&bits.hi, 8);
    size_t count_pos = w.size();
    w.u8(0);
    uint8_t count = 0;
    SentPkt sp;
    sp.seq = seq;
    sp.time = now;

    // unreliable first (latest state matters most); what does not fit goes into the next packet of
    // this flush (snapshots come in several chunks: dropping the rest froze those objects)
    size_t used = 0;
    for (auto& [type, payload] : unrel_out_) {
        if (count == 255)
            break;
        if (w.size() + 4 + payload.size() > kMaxPacket) {
            if (count == 0 && used == 0)
                used++; // can never fit any packet: drop it
            break;
        }
        w.u8(type);
        w.u8(0);
        w.u16((uint16_t)payload.size());
        w.raw(payload.data(), payload.size());
        count++;
        used++;
    }
    unrel_out_.erase(unrel_out_.begin(), unrel_out_.begin() + used);
    for (auto& m : rel_out_) {
        if (!(m.sends == 0 || now - m.last_sent >= rto))
            continue;
        if (w.size() + 6 + m.payload.size() > kMaxPacket || count == 255)
            break;
        w.u8(m.type);
        w.u8(1);
        w.u16((uint16_t)m.payload.size());
        w.u16(m.rel_id);
        w.raw(m.payload.data(), m.payload.size());
        if (m.sends > 0)
            stats.resends++;
        m.sends++;
        m.last_sent = now;
        sp.rel_ids.push_back(m.rel_id);
        count++;
    }
    w.data[count_pos] = count;
    sent_.push_back(std::move(sp));
    while (sent_.size() > 1024)
        sent_.pop_front();
    out = std::move(w.data);
    last_send_ms_ = now;
    ack_pending_ = false;
    stats.packets_sent++;
    return true;
}

void Connection::ack_packet(SentPkt& p, uint32_t now)
{
    if (p.acked)
        return;
    p.acked = true;
    stats.acked++;
    float sample = (float)(now - p.time);
    rtt_ms = rtt_ms * 0.9f + sample * 0.1f;
    for (uint16_t id : p.rel_ids) {
        auto it = std::find_if(rel_out_.begin(), rel_out_.end(),
                               [id](const OutMsg& m) { return m.rel_id == id; });
        if (it != rel_out_.end())
            rel_out_.erase(it);
    }
}

bool Connection::on_packet(const uint8_t* data, size_t n, uint32_t now, const Deliver& deliver)
{
    if (!looks_valid(data, n))
        return false;
    Reader r(data, n);
    r.u32();
    r.u16();
    uint16_t seq = r.u16();
    uint16_t ack = r.u16();
    AckBits ackbits;
    if (r.left() >= 16) {
        memcpy(&ackbits.lo, r.cur(), 8);
        memcpy(&ackbits.hi, r.cur() + 8, 8);
    }
    r.skip(16);
    uint8_t count = r.u8();
    if (!r.ok())
        return false;

    stats.packets_recv++;
    last_recv_ms = now;
    ack_pending_ = true;

    // receive window
    if (!have_remote_) {
        have_remote_ = true;
        remote_seq_ = seq;
        recv_bits_.clear();
    } else if (seq_greater(seq, remote_seq_)) {
        uint16_t shift = (uint16_t)(seq - remote_seq_);
        recv_bits_.shift(shift);
        if (shift <= kAckWindow)
            recv_bits_.set(shift - 1u); // the previous latest
        remote_seq_ = seq;
    } else {
        uint16_t diff = (uint16_t)(remote_seq_ - seq);
        if (diff >= 1 && diff <= kAckWindow)
            recv_bits_.set(diff - 1u);
    }

    // acks for our packets
    for (auto& p : sent_) {
        if (p.acked)
            continue;
        if (p.seq == ack) {
            ack_packet(p, now);
            continue;
        }
        uint16_t diff = (uint16_t)(ack - p.seq);
        if (diff >= 1 && diff <= kAckWindow && ackbits.test(diff - 1u))
            ack_packet(p, now);
    }

    for (int i = 0; i < count; i++) {
        uint8_t type = r.u8();
        uint8_t flags = r.u8();
        uint16_t len = r.u16();
        uint16_t rel_id = 0;
        if (flags & 1)
            rel_id = r.u16();
        if (!r.ok() || r.left() < len)
            return false;
        const uint8_t* p = r.cur();
        r.skip(len);
        if (!(flags & 1)) {
            Reader pr(p, len);
            deliver(type, pr);
            continue;
        }
        if (rel_id == next_rel_expected_) {
            Reader pr(p, len);
            deliver(type, pr);
            next_rel_expected_++;
            for (auto it = rel_in_.find(next_rel_expected_); it != rel_in_.end();
                 it = rel_in_.find(next_rel_expected_)) {
                Reader br(it->second.second.data(), it->second.second.size());
                deliver(it->second.first, br);
                rel_in_.erase(it);
                next_rel_expected_++;
            }
        } else if (seq_greater(rel_id, next_rel_expected_)) {
            if (!rel_in_.count(rel_id) && rel_in_.size() < 1024)
                rel_in_[rel_id] = {type, std::vector<uint8_t>(p, p + len)};
        }
        // else: duplicate of an already delivered message
    }
    return true;
}

} // namespace net
