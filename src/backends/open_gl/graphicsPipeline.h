//
// Created by radue on 2/22/2026.
//

#pragma once
#include <GL/glew.h>
#include <graphicsPipeline.h>

#include "shader.h"


namespace kor::ogl
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

    class GraphicsPipeline final : public kor::GraphicsPipeline
    {
    public:
        explicit GraphicsPipeline(const Builder& createInfo);
        ~GraphicsPipeline() override;

        void Bind(const kor::CommandBuffer& commandBuffer) const override;
        void Unbind() const override;

        GLenum getMode() const;

        const std::map<std::pair<glm::u32, glm::u32>, glm::u32>& getSetAndBindingToBindingPointMap() const {
            return _setAndBindingToBindingPoint;
        }

        // Push-constant UBO backing (see CommandBuffer::PushConstants). Zero-sized when
        // the pipeline declares no push constants.
        [[nodiscard]] bool hasPushConstants() const { return _pushConstantSize > 0; }
        [[nodiscard]] GLuint getPushConstantUBO() const { return _pushConstantUBO; }
        [[nodiscard]] glm::u32 getPushConstantBindingPoint() const { return _pushConstantBindingPoint; }
        [[nodiscard]] glm::u32 getPushConstantSize() const { return _pushConstantSize; }

        [[nodiscard]] GLuint getProgram() const { return _id; }
        [[nodiscard]] const std::vector<BindlessSamplerArray>& getBindlessArrays() const { return _bindlessArrays; }

    protected:
        void Setup() override;
        void Teardown() override;

    private:
        mutable bool _bound = false;
        GLuint _id = 0;

        std::map<std::pair<glm::u32, glm::u32>, glm::u32> _setAndBindingToBindingPoint;

        GLuint _pushConstantUBO = 0;
        glm::u32 _pushConstantBindingPoint = 0;
        glm::u32 _pushConstantSize = 0;

        // GL_ARB_bindless_texture material arrays declared by this pipeline's shaders,
        // with uniform locations resolved after link. See DescriptorSet::bind.
        std::vector<BindlessSamplerArray> _bindlessArrays;
    };
}

