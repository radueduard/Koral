//
// Created by radue on 3/6/2026.
//

#pragma once
#include <stdexcept>
#include <glm/fwd.hpp>

namespace gfx
{
    enum class ChannelType : glm::u32
    {
        eFloat = 0,
        eInt = 1,
        eUInt = 2,
        eShort = 3,
        eUShort = 4,
        eByte = 5,
        eUByte = 6,
        eDouble = 7
    };

    inline glm::u32 sizeofChannelType(const ChannelType channelType) {
        switch (channelType) {
        case ChannelType::eFloat: return sizeof(float);
        case ChannelType::eInt: return sizeof(int);
        case ChannelType::eUInt: return sizeof(unsigned int);
        case ChannelType::eShort: return sizeof(short);
        case ChannelType::eUShort: return sizeof(unsigned short);
        case ChannelType::eByte: return sizeof(char);
        case ChannelType::eUByte: return sizeof(unsigned char);
        case ChannelType::eDouble: return sizeof(double);
        default: throw std::runtime_error("Unknown channel type!");
        }
    }
}
