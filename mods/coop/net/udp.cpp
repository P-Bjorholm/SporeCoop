#include "udp.h"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSESOCK ::close
#endif

namespace net {

bool net_init()
{
#ifdef _WIN32
    static bool done = false;
    if (!done) {
        WSADATA wd;
        if (WSAStartup(MAKEWORD(2, 2), &wd) != 0)
            return false;
        done = true;
    }
#endif
    return true;
}

uint32_t now_ms()
{
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string Endpoint::str() const
{
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&ip);
    char s[32];
    snprintf(s, sizeof s, "%u.%u.%u.%u:%u", b[0], b[1], b[2], b[3], port);
    return s;
}

bool Endpoint::parse(const std::string& s, uint16_t default_port, Endpoint& out)
{
    std::string host = s;
    uint16_t port = default_port;
    size_t c = s.rfind(':');
    if (c != std::string::npos) {
        host = s.substr(0, c);
        port = (uint16_t)atoi(s.c_str() + c + 1);
    }
    net_init();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res)
        return false;
    out.ip = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr.s_addr;
    out.port = port;
    freeaddrinfo(res);
    return true;
}

UdpSocket::~UdpSocket() { close(); }

bool UdpSocket::open(uint16_t port, std::string* err)
{
    close();
    net_init();
    intptr_t s = (intptr_t)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        if (err) *err = "socket() failed";
        return false;
    }
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind((decltype(socket(0, 0, 0)))s, (sockaddr*)&a, sizeof a) != 0) {
        if (err) *err = "bind() failed on port " + std::to_string(port);
        CLOSESOCK((decltype(socket(0, 0, 0)))s);
        return false;
    }
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket((SOCKET)s, FIONBIO, &nb);
    // ignore ICMP port unreachable resets (WSAECONNRESET on recvfrom)
    BOOL bNewBehavior = FALSE;
    DWORD dwBytesReturned = 0;
    WSAIoctl((SOCKET)s, _WSAIOW(IOC_VENDOR, 12), &bNewBehavior, sizeof bNewBehavior, NULL, 0,
             &dwBytesReturned, NULL, NULL);
#else
    fcntl((int)s, F_SETFL, fcntl((int)s, F_GETFL) | O_NONBLOCK);
#endif
    // big buffers: a world transfer keeps hundreds of datagrams in flight
    int buf = 4 << 20;
    setsockopt((decltype(socket(0, 0, 0)))s, SOL_SOCKET, SO_RCVBUF, (const char*)&buf, sizeof buf);
    setsockopt((decltype(socket(0, 0, 0)))s, SOL_SOCKET, SO_SNDBUF, (const char*)&buf, sizeof buf);
    fd_ = s;
    return true;
}

void UdpSocket::close()
{
    if (fd_ != -1) {
        CLOSESOCK((decltype(socket(0, 0, 0)))fd_);
        fd_ = -1;
    }
}

bool UdpSocket::send_to(const Endpoint& to, const void* data, size_t n)
{
    if (fd_ == -1)
        return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(to.port);
    a.sin_addr.s_addr = to.ip;
    return sendto((decltype(socket(0, 0, 0)))fd_, (const char*)data, (int)n, 0, (sockaddr*)&a, sizeof a) == (int)n;
}

int UdpSocket::recv_from(Endpoint& from, void* buf, size_t cap)
{
    if (fd_ == -1)
        return -1;
    sockaddr_in a{};
    socklen_t al = sizeof a;
    int r = (int)recvfrom((decltype(socket(0, 0, 0)))fd_, (char*)buf, (int)cap, 0, (sockaddr*)&a, &al);
    if (r < 0) {
#ifdef _WIN32
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK || e == WSAECONNRESET)
            return 0;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
#endif
        return -1;
    }
    from.ip = a.sin_addr.s_addr;
    from.port = ntohs(a.sin_port);
    return r;
}

bool UdpSocket::enable_broadcast()
{
    if (fd_ == -1)
        return false;
    int on = 1;
    return setsockopt((decltype(socket(0, 0, 0)))fd_, SOL_SOCKET, SO_BROADCAST, (const char*)&on, sizeof on) == 0;
}

uint16_t UdpSocket::local_port() const
{
    if (fd_ == -1)
        return 0;
    sockaddr_in a{};
    socklen_t al = sizeof a;
    getsockname((decltype(socket(0, 0, 0)))fd_, (sockaddr*)&a, &al);
    return ntohs(a.sin_port);
}

} // namespace net
