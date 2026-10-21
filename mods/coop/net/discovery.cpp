#include "discovery.h"

namespace net {

static uint32_t ip4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    uint32_t v;
    uint8_t* p = reinterpret_cast<uint8_t*>(&v);
    p[0] = a; p[1] = b; p[2] = c; p[3] = d;
    return v;
}

bool Discovery::start()
{
    if (active())
        return true;
    if (!sock_.open(0))
        return false;
    sock_.enable_broadcast();
    if (targets.empty())
        targets = {ip4(255, 255, 255, 255), ip4(127, 0, 0, 1)};
    last_query_ = 0;
    return true;
}

void Discovery::stop()
{
    sock_.close();
    games_.clear();
}

void Discovery::update(uint32_t now)
{
    if (!active())
        return;
    if (last_query_ == 0 || now - last_query_ >= interval_ms) {
        last_query_ = now ? now : 1;
        Writer w;
        w.u32(kDiscoverQuery);
        w.u16(kProtoVersion);
        for (uint32_t ip : targets)
            for (uint16_t i = 0; i < kDiscoverPorts; i++)
                sock_.send_to(Endpoint{ip, (uint16_t)(kDefaultPort + i)}, w.data.data(), w.size());
    }
    uint8_t buf[512];
    for (int i = 0; i < 64; i++) {
        Endpoint from;
        int n = sock_.recv_from(from, buf, sizeof buf);
        if (n <= 0)
            break;
        Reader r(buf, (size_t)n);
        DiscoverReply rep;
        if (!rep.read(r) || rep.proto != kProtoVersion)
            continue;
        Endpoint host{from.ip, rep.port};
        bool found = false;
        for (auto& g : games_) {
            // the same host can answer on loopback and on its LAN address: keep one
            uint32_t lo = ip4(127, 0, 0, 1);
            bool same = g.host == host || (g.info.port == rep.port && g.info.name == rep.name &&
                                           (g.host.ip == lo || host.ip == lo));
            if (same) {
                if (g.host.ip == ip4(127, 0, 0, 1))
                    g.host = host; // prefer an address friends can use too
                g.info = rep;
                g.seen_ms = now;
                found = true;
            }
        }
        if (!found)
            games_.push_back({host, rep, now});
    }
}

std::vector<Discovery::Game> Discovery::games(uint32_t now) const
{
    std::vector<Game> v;
    for (auto& g : games_)
        if (now - g.seen_ms < 5000)
            v.push_back(g);
    return v;
}

} // namespace net
