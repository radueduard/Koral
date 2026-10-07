//
// koral-net: how the library announces itself. Linking it is what loads it. Nothing of the network starts
// until it is used: the I/O thread starts with the first socket.
//

#include <cstdint>
#include <string_view>

#include <module.h>

#include <knet/knetApi.h>

namespace knet
{
    class NetModule final : public kor::Module
    {
    public:
        static constexpr std::string_view ModuleId      = knet::ModuleId;
        static constexpr std::uint32_t    ModuleVersion = knet::ModuleVersion;
    };
}

KORAL_DECLARE_MODULE(knet::NetModule)
