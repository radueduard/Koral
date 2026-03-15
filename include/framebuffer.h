//
// Created by radue on 2/21/2026.
//

#pragma once
#include <memory>
#include <optional>
#include <vector>
#include <glm/glm.hpp>

#include "api.h"

namespace gfx
{
    class ImageView;

    class GFX_API Framebuffer {
    public:
        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;
        [[nodiscard]] bool IsDefault() const { return _isDefault; }

        struct GFX_API ClearValues
        {
            std::vector<glm::vec4> clearColor {};
            float clearDepth = 1.f;
            glm::i32 clearStencil = 0;
        };

        struct GFX_API Builder {
            std::vector<std::reference_wrapper<const ImageView>> colorAttachments {};
            std::optional<std::reference_wrapper<const ImageView>> depthStencilAttachment = std::nullopt;
            ClearValues clearValues;

            Builder& addColorAttachment(const ImageView& imageView, glm::vec4 clearColor);
            Builder& setDepthStencilAttachment(const ImageView& imageView, float depth, glm::i32 stencil);
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

        [[nodiscard]] virtual const ClearValues& getClearValues() const { return _clearValues; }

    protected:
        bool _isDefault = false;
        Framebuffer() = default;
        explicit Framebuffer(const Builder& createInfo);

        glm::uvec2 _extent = { 0, 0 };
        std::vector<std::reference_wrapper<const ImageView>> _colorAttachments;
        std::optional<std::reference_wrapper<const ImageView>> _depthStencilAttachment;
        ClearValues _clearValues;
    };
}

