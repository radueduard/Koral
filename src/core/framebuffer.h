//
// Created by radue on 2/21/2026.
//

#pragma once
#include <optional>
#include <vector>

#include "imageView.h"

namespace gfx
{
    class FramebufferImage;

    class Framebuffer {
    public:
        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;

        struct ClearValues
        {
            std::vector<glm::vec4> clearColor {};
            float clearDepth = 1.f;
            glm::i32 clearStencil = 0;
        };

        struct PerFrameAttachmentInfo
        {
            std::vector<std::reference_wrapper<const ImageView>> colorAttachments;
            std::optional<std::reference_wrapper<const ImageView>> depthStencilAttachment;
        };

        struct Builder {
            std::vector<PerFrameAttachmentInfo> attachments;
            ClearValues clearValues;

            Builder();
            Builder& addColorAttachment(const FramebufferImage& framebufferImage, glm::vec4 clearColor);
            Builder& setDepthStencilAttachment(const FramebufferImage& framebufferImage, float depth, glm::i32 stencil);
            [[nodiscard]] std::unique_ptr<Framebuffer> build() const;
        };

        virtual ~Framebuffer() = default;
        static std::unique_ptr<Framebuffer> CreateDefault();

        [[nodiscard]] virtual bool hasDepthStencilAttachment() const;
        [[nodiscard]] const std::vector<std::reference_wrapper<const ImageView>>& getColorAttachments() const;

        [[nodiscard]] const ImageView& getDepthStencilAttachment() const;

        [[nodiscard]] const glm::vec4& getClearColor(glm::u32 index) const;
        [[nodiscard]] float getClearDepth() const;
        [[nodiscard]] glm::i32 getClearStencil() const;

        [[nodiscard]] const glm::uvec2& getExtent() const { return _extent; }
        [[nodiscard]] glm::u32 getFrameCount() const { return _frameCount; }

        [[nodiscard]] virtual const ClearValues& getClearValues() const { return clearValues; }

    protected:
        bool isDefault = false;
        Framebuffer() = default;
        explicit Framebuffer(const Builder& createInfo);

        glm::uvec2 _extent = { 0, 0 };
        glm::u32 _frameCount = 0;
        std::vector<PerFrameAttachmentInfo> attachments;
        ClearValues clearValues;
    };
}

