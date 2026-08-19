//
// Created by radue on 2/21/2026.
//

#pragma once
#include <memory>
#include <optional>
#include <vector>
#include <variant>
#include <glm/glm.hpp>

#include "api.h"
#include <source_location>

#include "builder.h"
#include "resource.h"
#include "structs.h"
#include "error.h"

namespace kor
{
    class ImageView;

    /**
     * @brief The set of attachments a render pass draws into.
     *
     * A framebuffer names the colour targets, the depth and stencil target, and the values each is
     * cleared to *by default*. Pass one to CommandBuffer::BeginRendering to render into it; pass
     * nothing to render into the window's default framebuffer. A pass that wants to clear this
     * framebuffer to something else says so in its own @ref RenderInfo, which overrides these
     * without touching the framebuffer everything else shares.
     *
     * @code
     * kor::Framebuffer::Builder builder;
     * auto gbuffer = builder
     *     .addColorAttachment(albedoView, glm::vec4{0, 0, 0, 1})
     *     .addColorAttachment(normalView)
     *     .setDepthAttachment(depthView, 1.f)
     *     .build();
     * @endcode
     *
     * Attachments are given as ImageViews, so one image can be a target in one pass and a texture
     * in the next. Every attachment must agree on extent and sample count. For MSAA, give each
     * attachment a resolve view: the multisampled image is rendered into and collapsed onto the
     * resolve target as the pass ends, which is cheaper than resolving it afterwards.
     */
    class KORAL_API Framebuffer {
    public:
        /** @brief Makes this framebuffer the current render target. Called by the backend; a scene uses BeginRendering. */
        virtual void Bind() const = 0;

        /** @brief Releases it as the current render target. */
        virtual void Unbind() const = 0;

        /** @brief Whether this is the window's default framebuffer, the one presented at the end of the frame. */
        [[nodiscard]] bool IsDefault() const { return _isDefault; }

        /** @brief What each attachment is filled with when a pass opens with LoadOperation::eClear. */
        struct KORAL_API ClearValues
        {
            /// One entry per colour attachment, appended by addColorAttachment and indexed the same way.
            std::vector<ClearColor> clearColor {};
            float clearDepth = 1.f;     ///< Depth clear value. 1.0 is the far plane under the usual depth range.
            glm::i32 clearStencil = 0;  ///< Stencil clear value.
        };

        /**
         * @brief Collects the attachments a framebuffer is made of.
         *
         * Order matters for colour attachments: the first added is what a fragment shader writes to
         * location 0.
         */
        struct KORAL_API Builder : ::Builder {
            std::vector<std::reference_wrapper<const ImageView>> colorAttachments {};
            std::optional<std::vector<std::reference_wrapper<const ImageView>>> colorResolveAttachments = std::nullopt;
            std::optional<std::reference_wrapper<const ImageView>> depthAttachment = std::nullopt;
            std::optional<std::reference_wrapper<const ImageView>> depthResolveAttachment = std::nullopt;
            std::optional<std::reference_wrapper<const ImageView>> stencilAttachment = std::nullopt;
            std::optional<std::reference_wrapper<const ImageView>> stencilResolveAttachment = std::nullopt;
            ClearValues clearValues;
            std::optional<SampleCount> sampleCount = std::nullopt;
            std::optional<glm::uvec2> extent = std::nullopt;
            ResolveMode resolveMode = ResolveMode::eNone;

            /**
             * @brief Appends a colour target.
             * @param imageView What to render into. Its image needs Image::Usage::eColorAttachment.
             * @param clearColor What it is filled with when the pass clears on entry.
             * @return The builder, for chaining.
             *
             * The order of these calls is the order of the shader's output locations.
             */
            Builder& addColorAttachment(ResourceRef<const ImageView> imageView, ClearColor clearColor = glm::vec4{ 0.f, 0.f, 0.f, 1.f });

            /**
             * @brief Appends a multisampled colour target and the single-sampled view it resolves onto.
             * @param imageView The multisampled target that is rendered into.
             * @param resolveView Where its samples are collapsed as the pass ends.
             * @param clearColor What the target is filled with when the pass clears on entry.
             */
            Builder& addColorAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, ClearColor clearColor = glm::vec4{ 0.f, 0.f, 0.f, 1.f });

            /**
             * @brief Sets the depth target.
             * @param imageView What to render depth into. Its image needs a depth format and
             *        Image::Usage::eDepthStencilAttachment.
             * @param depth What it is cleared to; 1.0 is the far plane.
             */
            Builder& setDepthAttachment(ResourceRef<const ImageView> imageView, float depth = 1.f);

            /** @brief Sets a multisampled depth target and the view its samples resolve onto. */
            Builder& setDepthAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, float depth = 1.f);

            /**
             * @brief Sets the stencil target.
             * @param imageView What to render stencil into. Its image needs a stencil-carrying format.
             * @param stencil What it is cleared to.
             */
            Builder& setStencilAttachment(ResourceRef<const ImageView> imageView, glm::i32 stencil = 0);

            /** @brief Sets a multisampled stencil target and the view its samples resolve onto. */
            Builder& setStencilAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, glm::i32 stencil = 0);

            /**
             * @brief Sets one view as both the depth and the stencil target.
             * @param imageView A view of a combined depth-stencil format, such as
             *        Image::Format::eD32_SFLOAT_S8_UINT.
             * @param depth What the depth part is cleared to.
             * @param stencil What the stencil part is cleared to.
             */
            Builder& setDepthStencilAttachment(ResourceRef<const ImageView> imageView, float depth = 1.f, glm::i32 stencil = 0);

            /** @brief Sets a multisampled combined depth-stencil target and the view it resolves onto. */
            Builder& setDepthStencilAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, float depth = 1.f, glm::i32 stencil = 0);

            /** @brief Sets how samples are combined when resolving — averaged, or one sample taken. @see ResolveMode */
            Builder& setResolveMode(ResolveMode mode);

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<Framebuffer>> create() const;

            /**
             * @brief Creates the framebuffer.
             * @return It as a Resource; poisoned rather than thrown if the attachments disagree on
             *         extent or sample count.
             */
            [[nodiscard]] kor::Resource<Framebuffer> build(std::source_location where = std::source_location::current()) const;
        };

        virtual ~Framebuffer() = default;

        /** @brief Creates the window's default framebuffer over the swap chain. Called by the window; use Context::DefaultFramebuffer() to reach it. */
        static Resource<Framebuffer> CreateDefault();

        /** @brief How many colour targets it has. */
        [[nodiscard]] glm::u32 getColorAttachmentCount() const;

        /** @brief Its samples per pixel. A pipeline rendering into it must declare the same. */
        [[nodiscard]] SampleCount getSampleCount() const;

        /** @brief Its size in pixels, which every attachment shares. */
        [[nodiscard]] const glm::uvec2& getExtent() const { return _extent; }

        /** @brief The colour targets, in the order a fragment shader's output locations address them. */
        [[nodiscard]] const std::vector<std::reference_wrapper<const ImageView>>& getColorAttachments() const;

        /** @brief Whether it has a depth target, and so whether depth testing is possible in the pass. */
        [[nodiscard]] bool hasDepthAttachment() const;

        /** @brief The depth target. Only call it when hasDepthAttachment() is true. */
        [[nodiscard]] const ImageView& getDepthAttachment() const;

        /** @brief Whether it has a stencil target. */
        [[nodiscard]] bool hasStencilAttachment() const;

        /** @brief The stencil target. Only call it when hasStencilAttachment() is true. */
        [[nodiscard]] const ImageView& getStencilAttachment() const;

        /** @brief Whether it has a depth target. Kept for callers written against the combined depth-stencil naming. */
        [[nodiscard]] bool hasDepthStencilAttachment() const { return hasDepthAttachment(); }

        /** @brief The depth target. Kept for callers written against the combined depth-stencil naming. */
        [[nodiscard]] const ImageView& getDepthStencilAttachment() const { return getDepthAttachment(); }

        /** @brief Whether its attachments resolve onto single-sampled views as the pass ends. */
        [[nodiscard]] bool hasResolveAttachments() const;

        /** @brief The resolve target for colour attachment @p index. */
        [[nodiscard]] const ImageView& getResolveAttachment(glm::u32 index) const;

        /** @brief What colour attachment @p index is cleared to. */
        [[nodiscard]] const ClearColor& getClearColor(glm::u32 index) const;

        /** @brief What the depth target is cleared to. */
        [[nodiscard]] float getClearDepth() const;

        /** @brief What the stencil target is cleared to. */
        [[nodiscard]] glm::i32 getClearStencil() const;

        /** @brief How samples are combined when resolving. */
        [[nodiscard]] ResolveMode getResolveMode() const;

        /** @brief Every clear value at once. */
        [[nodiscard]] virtual const ClearValues& getClearValues() const { return _clearValues; }

        /**
         * @brief Resizes the framebuffer and every image it renders into.
         * @param newExtent The new size in pixels.
         *
         * Resizes the attachments themselves — colour, depth, stencil and their resolve targets — so
         * one call is the whole of following a new size. An image is *replaced* rather than resized,
         * since GPU image storage is immutable, and its views notice and rebuild; anything else
         * holding one, a descriptor set above all, has to be rebuilt by whoever built it.
         *
         * Its contents do not survive: a resized attachment starts undefined and is expected to be
         * rendered into before it is read, which is what the frame that follows does anyway.
         *
         * Done for you on the default framebuffer when the window resizes — there the swap chain owns
         * the images and this only re-points at them. A framebuffer of your own is yours to resize,
         * from Scene::OnResize, or from Update when it follows something else's size (a viewport's).
         * Do it between frames, which is where both of those are, and not from inside Render.
         */
        virtual void Resize(const glm::uvec2& newExtent) const;

    protected:
        bool _isDefault = false;
        Framebuffer() = default;
        explicit Framebuffer(const Builder& createInfo);

        mutable glm::uvec2 _extent = { 0, 0 };
        mutable std::vector<std::reference_wrapper<const ImageView>> _colorAttachments {};
        mutable std::optional<std::vector<std::reference_wrapper<const ImageView>>> _colorResolveAttachments = std::nullopt;
        mutable std::optional<std::reference_wrapper<const ImageView>> _depthAttachment = std::nullopt;
        mutable std::optional<std::reference_wrapper<const ImageView>> _depthResolveAttachment = std::nullopt;
        mutable std::optional<std::reference_wrapper<const ImageView>> _stencilAttachment = std::nullopt;
        mutable std::optional<std::reference_wrapper<const ImageView>> _stencilResolveAttachment = std::nullopt;
        SampleCount _sampleCount = SampleCount::e1;
        ClearValues _clearValues {};
        ResolveMode _resolveMode = ResolveMode::eNone;
    };
}
