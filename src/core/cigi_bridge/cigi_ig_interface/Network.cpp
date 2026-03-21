#include "Network.h"
#include <cstring>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define CLOSE_SOCKET(fd)     closesocket(fd)
#  define SET_NONBLOCKING(fd)  do { u_long mode = 1; ioctlsocket((fd), FIONBIO, &mode); } while(0)
static inline void inet_aton_compat(const char * ip, struct in_addr * addr) {
    InetPtonA(AF_INET, ip, addr);
}
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  define CLOSE_SOCKET(fd)     close(fd)
#  define SET_NONBLOCKING(fd)  fcntl((fd), F_SETFL, fcntl((fd), F_GETFL, 0) | O_NONBLOCK)
static inline void inet_aton_compat(const char * ip, struct in_addr * addr) {
    inet_aton(ip, addr);
}
#endif

using namespace Network_NS;

Network::~Network()
{
    if (recv_fd_ >= 0) { CLOSE_SOCKET(recv_fd_); recv_fd_ = -1; }
    if (send_fd_ >= 0) { CLOSE_SOCKET(send_fd_); send_fd_ = -1; }
}

bool Network::openSocket(const InAddress_sptr & igAddr, const OutAddress_sptr & hostAddr)
{
    recv_fd_ = static_cast<int>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (recv_fd_ < 0) return false;

    struct sockaddr_in bind_addr {};
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port        = htons(static_cast<uint16_t>(igAddr->port));
    if (bind(recv_fd_, reinterpret_cast<struct sockaddr *>(&bind_addr), sizeof(bind_addr)) < 0) {
        CLOSE_SOCKET(recv_fd_); recv_fd_ = -1; return false;
    }
    SET_NONBLOCKING(recv_fd_);

    send_fd_ = static_cast<int>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (send_fd_ < 0) return false;

    memset(&send_addr_, 0, sizeof(send_addr_));
    send_addr_.sin_family = AF_INET;
    send_addr_.sin_port   = htons(static_cast<uint16_t>(hostAddr->port));
    inet_aton_compat(hostAddr->ip.c_str(), &send_addr_.sin_addr);
    return true;
}

bool Network::openSocket(const OutAddress_sptr & hostAddr)
{
    send_fd_ = static_cast<int>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (send_fd_ < 0) return false;

    memset(&send_addr_, 0, sizeof(send_addr_));
    send_addr_.sin_family = AF_INET;
    send_addr_.sin_port   = htons(static_cast<uint16_t>(hostAddr->port));
    inet_aton_compat(hostAddr->ip.c_str(), &send_addr_.sin_addr);
    return true;
}

int Network::recv(unsigned char * buf, int size)
{
    if (recv_fd_ < 0) return -1;
    struct sockaddr_in from {};
    socklen_t fromlen = sizeof(from);
    return static_cast<int>(
        recvfrom(recv_fd_, buf, static_cast<size_t>(size), 0,
                 reinterpret_cast<struct sockaddr *>(&from), &fromlen));
}

bool Network::send(const unsigned char * buf, int size)
{
    if (send_fd_ < 0) return false;
#ifdef _WIN32
    int sent = sendto(send_fd_, reinterpret_cast<const char *>(buf), size, 0,
                      reinterpret_cast<struct sockaddr *>(&send_addr_), sizeof(send_addr_));
    return sent == size;
#else
    ssize_t sent = sendto(send_fd_, buf, static_cast<size_t>(size), 0,
                          reinterpret_cast<struct sockaddr *>(&send_addr_), sizeof(send_addr_));
    return sent == static_cast<ssize_t>(size);
#endif
}
