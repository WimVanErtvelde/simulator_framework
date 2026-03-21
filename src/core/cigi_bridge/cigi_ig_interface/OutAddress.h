#pragma once
#include <memory>
#include <string>

namespace Network_NS
{
    struct OutAddress {
        std::string ip;
        int         port;
        bool        Valid;
        OutAddress(std::string ip_, int p)
            : ip(std::move(ip_)), port(p), Valid(!ip.empty() && p > 0 && p <= 65535) {}
    };
    using OutAddress_sptr = std::shared_ptr<OutAddress>;

    inline OutAddress_sptr make_out_address(const std::string & ip, int port)
    {
        return std::make_shared<OutAddress>(ip, port);
    }
}
