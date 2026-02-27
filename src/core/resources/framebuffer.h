//
// Created by radue on 2/21/2026.
//

#pragma once
#include <optional>
#include <vector>

#include "imageView.h"

namespace gfx
{
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

        struct Builder {
            std::vector<std::reference_wrapper<const ImageView>> colorAttachments;
            std::optional<std::reference_wrapper<const ImageView>> depthStencilAttachment;
            ClearValues clearValues;

            Builder& addColorAttachment(const ImageView& imageView, glm::vec4 clearColor) {
                colorAttachments.emplace_back(imageView);
                clearValues.clearColor.emplace_back(clearColor);
                return *this;
            }

            Builder& setDepthStencilAttachment(const ImageView& imageView, float depth, glm::i32 stencil) {
                depthStencilAttachment = imageView;
                clearValues.clearDepth = depth;
                clearValues.clearStencil = stencil;
                return *this;
            }

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

    protected:
        Framebuffer() = default;
        explicit Framebuffer(const Builder& createInfo);

        std::vector<std::reference_wrapper<const ImageView>> colorAttachments;
        std::optional<std::reference_wrapper<const ImageView>> depthStencilAttachment;
        ClearValues clearValues;
    };
}

