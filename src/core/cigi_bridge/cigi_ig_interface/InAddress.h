#pragma once
#include <memory>

namespace Network_NS
{
    struct InAddress {
        int  port;
        bool Valid;
        explicit InAddress(int p) : port(p), Valid(p > 0 && p <= 65535) {}
    };
    using InAddress_sptr = std::shared_ptr<InAddress>;

    inline InAddress_sptr make_in_address(int port)
    {
        return std::make_shared<InAddress>(port);
    }
}
