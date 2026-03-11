//
// Created by radue on 2/22/2026.
//

#pragma once
#include <GL/glew.h>

#include "shader.h"
#include "core/graphicsPipeline.h"

namespace gfx::ogl
{
    inline GLenum toGLChannelType(const ChannelType channelType) {
        switch (channelType) {
        case ChannelType::eFloat: return GL_FLOAT;
        case ChannelType::eInt: return GL_INT;
        case ChannelType::eUInt: return GL_UNSIGNED_INT;
        case ChannelType::eShort: return GL_SHORT;
        case ChannelType::eUShort: return GL_UNSIGNED_SHORT;
        case ChannelType::eByte: return GL_BYTE;
        case ChannelType::eUByte: return GL_UNSIGNED_BYTE;
        case ChannelType::eDouble: return GL_DOUBLE;
        default: throw std::runtime_error("Unknown channel type!");
        }
    }

    class GraphicsPipeline final : public gfx::GraphicsPipeline
    {
    public:
        explicit GraphicsPipeline(const Builder& createInfo);
        ~GraphicsPipeline() override;

        void Bind(const gfx::CommandBuffer& commandBuffer) const override;
        void Unbind() const override;

        GLenum getMode() const;

        const std::map<std::pair<glm::u32, glm::u32>, glm::u32>& getSetAndBindingToBindingPointMap() const {
            return _setAndBindingToBindingPoint;
        }

    private:
        mutable bool _bound = false;
        GLuint _id = 0;

        std::map<std::pair<glm::u32, glm::u32>, glm::u32> _setAndBindingToBindingPoint;
    };
}

