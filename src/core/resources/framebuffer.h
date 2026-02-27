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

        struct CreateInfo {
            std::vector<std::reference_wrapper<const ImageView>> colorAttachments;
            std::optional<std::reference_wrapper<const ImageView>> depthStencilAttachment;
            ClearValues clearValues;

            CreateInfo& addColorAttachment(const ImageView& imageView, glm::vec4 clearColor) {
                colorAttachments.emplace_back(imageView);
                clearValues.clearColor.emplace_back(clearColor);
                return *this;
            }

            CreateInfo& setDepthStencilAttachment(const ImageView& imageView, float depth, glm::i32 stencil) {
                depthStencilAttachment = imageView;
                clearValues.clearDepth = depth;
                clearValues.clearStencil = stencil;
                return *this;
            }
        };
        virtual ~Framebuffer() = default;

        static std::unique_ptr<Framebuffer> Create(const CreateInfo& createInfo);
        static std::unique_ptr<Framebuffer> CreateDefault();

        virtual bool hasDepthStencilAttachment() const;
        const std::vector<std::reference_wrapper<const ImageView>>& getColorAttachments() const;

        const ImageView& getDepthStencilAttachment() const;

        const glm::vec4& getClearColor(glm::u32 index) const;
        float getClearDepth() const;
        glm::i32 getClearStencil() const;

    protected:
        Framebuffer() = default;
        explicit Framebuffer(const CreateInfo& createInfo);

        std::vector<std::reference_wrapper<const ImageView>> colorAttachments;
        std::optional<std::reference_wrapper<const ImageView>> depthStencilAttachment;
        ClearValues clearValues;
    };
}

