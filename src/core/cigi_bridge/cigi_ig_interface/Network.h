#pragma once
// Cross-platform UDP socket abstraction matching the Network_NS interface
// expected by CIGI_IG_Interface.  Compiles on both Linux and Windows (mingw-w64).
#include "InAddress.h"
#include "OutAddress.h"

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <netinet/in.h>
#endif

namespace Network_NS
{
    class Network {
        int recv_fd_ = -1;
        int send_fd_ = -1;
        struct sockaddr_in send_addr_ {};
    public:
        ~Network();
        // Two-socket mode: bind recv on igAddr->port, send to hostAddr ip:port
        bool openSocket(const InAddress_sptr & igAddr, const OutAddress_sptr & hostAddr);
        // Single send-only socket (used when m_send == false in CIGI_IG_Interface)
        bool openSocket(const OutAddress_sptr & hostAddr);
        // Non-blocking recv; returns bytes received, 0 = would block, <0 = error
        int  recv(unsigned char * buf, int size);
        bool send(const unsigned char * buf, int size);
    };
}
