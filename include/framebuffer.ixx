//
// Created by radue on 2/21/2026.
//

module;

#include <glm/glm.hpp>
#include "api.h"

export module gfx:framebuffer;
import :types;

import std;
import resource;

namespace gfx
{
    class ImageView;

    export using ClearColor = std::variant<
        float,
        glm::vec2,
        glm::vec3,
        glm::vec4,
        glm::i32,
        glm::ivec2,
        glm::ivec3,
        glm::ivec4,
        glm::u32,
        glm::uvec2,
        glm::uvec3,
        glm::uvec4
    >;

    export class GFX_API Framebuffer {
    public:
        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;
        [[nodiscard]] bool IsDefault() const { return _isDefault; }

        struct GFX_API ClearValues
        {
            std::vector<ClearColor> clearColor { glm::vec4{ 0.f, 0.f, 0.f, 1.f } };
            float clearDepth = 1.f;
            glm::i32 clearStencil = 0;
        };

        struct GFX_API Builder {
            std::vector<ResourceRef<const ImageView>> colorAttachments {};
            std::optional<std::vector<ResourceRef<const ImageView>>> colorResolveAttachments = std::nullopt;
            std::optional<ResourceRef<const ImageView>> depthAttachment = std::nullopt;
            std::optional<ResourceRef<const ImageView>> depthResolveAttachment = std::nullopt;
            std::optional<ResourceRef<const ImageView>> stencilAttachment = std::nullopt;
            std::optional<ResourceRef<const ImageView>> stencilResolveAttachment = std::nullopt;
            ClearValues clearValues;
            std::optional<SampleCount> sampleCount = std::nullopt;
            std::optional<glm::uvec2> extent = std::nullopt;
            ResolveMode resolveMode = ResolveMode::eNone;

            Builder& addColorAttachment(ResourceRef<const ImageView> imageView, ClearColor clearColor = glm::vec4{ 0.f, 0.f, 0.f, 1.f });
            Builder& addColorAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, ClearColor clearColor = glm::vec4{ 0.f, 0.f, 0.f, 1.f });

            Builder& setDepthAttachment(ResourceRef<const ImageView> imageView, float depth = 1.f);
            Builder& setDepthAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, float depth = 1.f);
            Builder& setStencilAttachment(ResourceRef<const ImageView> imageView, glm::i32 stencil = 0);
            Builder& setStencilAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, glm::i32 stencil = 0);

            Builder& setDepthStencilAttachment(ResourceRef<const ImageView> imageView, float depth = 1.f, glm::i32 stencil = 0);
            Builder& setDepthStencilAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, float depth = 1.f, glm::i32 stencil = 0);

            Builder& setResolveMode(ResolveMode mode);

            [[nodiscard]] Resource<Framebuffer> build() const;
        };

        virtual ~Framebuffer() = default;
        static Resource<Framebuffer> CreateDefault();

        [[nodiscard]] glm::u32 getColorAttachmentCount() const;
        [[nodiscard]] SampleCount getSampleCount() const;
        [[nodiscard]] const glm::uvec2& getExtent() const;

        [[nodiscard]] ResourceRef<const ImageView> getColorAttachment(glm::u32 index) const;
        [[nodiscard]] const std::vector<ResourceRef<const ImageView>>& getColorAttachments() const;

        [[nodiscard]] bool hasDepthAttachment() const;
        [[nodiscard]] ResourceRef<const ImageView> getDepthAttachment() const;
        [[nodiscard]] bool hasStencilAttachment() const;
        [[nodiscard]] ResourceRef<const ImageView> getStencilAttachment() const;

        [[nodiscard]] bool hasResolveAttachments() const;
        [[nodiscard]] ResourceRef<const ImageView> getResolveAttachment(glm::u32 index) const;
        [[nodiscard]] std::vector<ResourceRef<const ImageView>>& getColorResolveAttachments() const;
        [[nodiscard]] ResourceRef<const ImageView> getDepthResolveAttachment() const;
        [[nodiscard]] ResourceRef<const ImageView> getStencilResolveAttachment() const;

        [[nodiscard]] const ClearColor& getClearColor(glm::u32 index) const;
        [[nodiscard]] float getClearDepth() const;
        [[nodiscard]] glm::i32 getClearStencil() const;

        [[nodiscard]] ResolveMode getResolveMode() const;

        virtual void Resize(const glm::uvec2& newExtent) const {};

    protected:
        bool _isDefault = false;
        Framebuffer() = default;
        explicit Framebuffer(const Builder& createInfo);

        mutable glm::uvec2 _extent = { 0, 0 };
        mutable std::vector<ResourceRef<const ImageView>> _colorAttachments {};
        mutable std::optional<std::vector<ResourceRef<const ImageView>>> _colorResolveAttachments = std::nullopt;
        mutable std::optional<ResourceRef<const ImageView>> _depthAttachment = std::nullopt;
        mutable std::optional<ResourceRef<const ImageView>> _depthResolveAttachment = std::nullopt;
        mutable std::optional<ResourceRef<const ImageView>> _stencilAttachment = std::nullopt;
        mutable std::optional<ResourceRef<const ImageView>> _stencilResolveAttachment = std::nullopt;
        SampleCount _sampleCount = SampleCount::e1;
        ClearValues _clearValues {};
        ResolveMode _resolveMode = ResolveMode::eNone;
    };
}

