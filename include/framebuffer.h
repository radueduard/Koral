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
    class Image;
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
     * auto gbuffer = kor::Framebuffer::Builder{}
     *     .addColorAttachment("albedo", albedoImage, glm::vec4{0, 0, 0, 1})
     *     .addColorAttachment("normal", normalImage)
     *     .setDepthAttachment(depthImage, 1.f)
     *     .build();
     *
     * auto albedo = gbuffer->image("albedo");   // read it back, sample it, export it
     * @endcode
     *
     * Attachments may be given as Images — a view covering the top mip level is made and owned by
     * the image, which is what an attachment needs — or as ImageViews, when you want to render into
     * one layer or one level of something larger. Either way one image can be a target in one pass
     * and a texture in the next.
     *
     * Naming an attachment is optional and costs nothing, but it is what lets the targets be
     * reached afterwards by what they are rather than by the order they happened to be added in.
     * The order still decides the shader's output locations: the first colour attachment added is
     * location 0.
     *
     * Every attachment must agree on extent and sample count. For MSAA, give each attachment a
     * resolve view: the multisampled image is rendered into and collapsed onto the resolve target
     * as the pass ends, which is cheaper than resolving it afterwards.
     */
    class KORAL_API Framebuffer {
    public:
        /** @brief Makes this framebuffer the current render target. Called by the backend; a scene uses BeginRendering. */
        virtual void Bind() const = 0;

        /** @brief Releases it as the current render target. */
        virtual void Unbind() const = 0;

        /** @brief Whether this is the window's default framebuffer, the one presented at the end of the frame. */
        [[nodiscard]] bool IsDefault() const { return _isDefault; }

        /**
         * @brief One target of a framebuffer: what is rendered into, what it resolves onto, and
         *        what it is called.
         *
         * Held by tracked reference rather than by raw reference, which is what lets a framebuffer
         * outlive nothing and lets its targets be handed back out — reaching the default
         * framebuffer's swap-chain image is exactly that. @see Framebuffer::image
         */
        struct KORAL_API Attachment
        {
            ResourceRef<const ImageView> view;      ///< What is rendered into.
            ResourceRef<const ImageView> resolve;   ///< Where its samples land, for an MSAA target; empty otherwise.
            std::string name;                       ///< What it is called, if it was named.

            /** @brief Whether this attachment answers to @p wanted. */
            [[nodiscard]] bool namedBy(const std::string_view wanted) const {
                return !name.empty() && name == wanted;
            }
        };

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
            std::vector<Attachment> colorAttachments {};
            std::optional<Attachment> depthAttachment = std::nullopt;
            std::optional<Attachment> stencilAttachment = std::nullopt;
            ClearValues clearValues;
            std::optional<SampleCount> sampleCount = std::nullopt;
            std::optional<glm::uvec2> extent = std::nullopt;
            ResolveMode resolveMode = ResolveMode::eNone;

            /**
             * @name Colour targets
             *
             * The order of these calls is the order of the shader's output locations: the first
             * added is location 0.
             *
             * Give an Image and the view is made for you — the top mip level, every array layer,
             * which is what an attachment must be — and owned by the image, so the same image
             * rendered into by two framebuffers shares one. Give an ImageView instead to render
             * into one level or one layer of something larger.
             *
             * A name is optional and is what lets the target be found again by what it is rather
             * than by the order it was added in. @see Framebuffer::image
             *
             * @param clearColor What it is filled with when the pass clears on entry.
             * @param resolveView For a multisampled target, where its samples are collapsed as the
             *        pass ends.
             */
            ///@{
            Builder& addColorAttachment(ResourceRef<const ImageView> imageView, ClearColor clearColor = glm::vec4{ 0.f, 0.f, 0.f, 1.f });
            Builder& addColorAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, ClearColor clearColor = glm::vec4{ 0.f, 0.f, 0.f, 1.f });
            Builder& addColorAttachment(ResourceRef<const Image> image, ClearColor clearColor = glm::vec4{ 0.f, 0.f, 0.f, 1.f });

            Builder& addColorAttachment(std::string_view name, ResourceRef<const ImageView> imageView, ClearColor clearColor = glm::vec4{ 0.f, 0.f, 0.f, 1.f });
            Builder& addColorAttachment(std::string_view name, ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, ClearColor clearColor = glm::vec4{ 0.f, 0.f, 0.f, 1.f });
            Builder& addColorAttachment(std::string_view name, ResourceRef<const Image> image, ClearColor clearColor = glm::vec4{ 0.f, 0.f, 0.f, 1.f });
            ///@}

            /**
             * @name Depth and stencil targets
             *
             * The same in every respect as the colour targets above, including taking an Image
             * directly. A depth target's image needs a depth format and
             * Image::Usage::eDepthStencilAttachment; a stencil target's needs a stencil-carrying
             * format. The combined form sets one view as both, for a format such as
             * Image::Format::eD32_SFLOAT_S8_UINT that carries the two together.
             *
             * @param depth What the depth part is cleared to; 1.0 is the far plane.
             * @param stencil What the stencil part is cleared to.
             */
            ///@{
            Builder& setDepthAttachment(ResourceRef<const ImageView> imageView, float depth = 1.f);
            Builder& setDepthAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, float depth = 1.f);
            Builder& setDepthAttachment(ResourceRef<const Image> image, float depth = 1.f);
            Builder& setDepthAttachment(std::string_view name, ResourceRef<const ImageView> imageView, float depth = 1.f);
            Builder& setDepthAttachment(std::string_view name, ResourceRef<const Image> image, float depth = 1.f);

            Builder& setStencilAttachment(ResourceRef<const ImageView> imageView, glm::i32 stencil = 0);
            Builder& setStencilAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, glm::i32 stencil = 0);
            Builder& setStencilAttachment(ResourceRef<const Image> image, glm::i32 stencil = 0);
            Builder& setStencilAttachment(std::string_view name, ResourceRef<const ImageView> imageView, glm::i32 stencil = 0);
            Builder& setStencilAttachment(std::string_view name, ResourceRef<const Image> image, glm::i32 stencil = 0);

            Builder& setDepthStencilAttachment(ResourceRef<const ImageView> imageView, float depth = 1.f, glm::i32 stencil = 0);
            Builder& setDepthStencilAttachment(ResourceRef<const ImageView> imageView, ResourceRef<const ImageView> resolveView, float depth = 1.f, glm::i32 stencil = 0);
            Builder& setDepthStencilAttachment(ResourceRef<const Image> image, float depth = 1.f, glm::i32 stencil = 0);
            Builder& setDepthStencilAttachment(std::string_view name, ResourceRef<const Image> image, float depth = 1.f, glm::i32 stencil = 0);
            ///@}

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

        private:
            /**
             * @brief Takes the extent and sample count from the first attachment that can give them.
             *
             * Every attachment has to agree on both, so the first one to arrive settles them and the
             * rest are checked against that. An unusable attachment answers for neither and is left
             * to poison the build through adopt(), rather than being dereferenced here.
             */
            void adoptGeometry(const ResourceRef<const ImageView>& imageView);
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
        [[nodiscard]] const std::vector<Attachment>& getColorAttachments() const;

        /** @brief The view rendered into at colour attachment @p index, or an empty ref if there is none. */
        [[nodiscard]] ResourceRef<const ImageView> getColorAttachment(glm::u32 index) const;

        /** @brief Whether it has a depth target, and so whether depth testing is possible in the pass. */
        [[nodiscard]] bool hasDepthAttachment() const;

        /** @brief The depth target, or an empty ref if there is none. */
        [[nodiscard]] ResourceRef<const ImageView> getDepthAttachment() const;

        /** @brief Whether it has a stencil target. */
        [[nodiscard]] bool hasStencilAttachment() const;

        /** @brief The stencil target, or an empty ref if there is none. */
        [[nodiscard]] ResourceRef<const ImageView> getStencilAttachment() const;

        /** @brief Whether it has a depth target. Kept for callers written against the combined depth-stencil naming. */
        [[nodiscard]] bool hasDepthStencilAttachment() const { return hasDepthAttachment(); }

        /** @brief The depth target. Kept for callers written against the combined depth-stencil naming. */
        [[nodiscard]] ResourceRef<const ImageView> getDepthStencilAttachment() const { return getDepthAttachment(); }

        /** @brief Whether its attachments resolve onto single-sampled views as the pass ends. */
        [[nodiscard]] bool hasResolveAttachments() const;

        /** @brief The resolve target for colour attachment @p index, or an empty ref if it has none. */
        [[nodiscard]] ResourceRef<const ImageView> getResolveAttachment(glm::u32 index) const;

        /**
         * @name Reaching the targets by name
         *
         * What a framebuffer renders into is usually wanted afterwards — sampled by the next pass,
         * read back, exported, shown in a viewport — and asking for it by what it is beats
         * remembering which index it was added at.
         *
         * @code
         * auto normals = gbuffer->image("normal");
         * commandBuffer.CopyImageToBuffer(normals, readback);
         * @endcode
         *
         * The default framebuffer names its own targets `"color"` and `"depth"` (with `"stencil"`
         * aliasing the depth target when the format carries both), so the swap-chain image a frame
         * is presented from is reachable the same way as any other:
         *
         * @code
         * auto screen = kor::Context::DefaultFramebuffer()->image("color");
         * @endcode
         *
         * An unknown name gives an empty ref rather than throwing — the caller usually wants to
         * carry on without that target rather than stop.
         */
        ///@{
        /** @brief The view of the attachment called @p name, or an empty ref if there is none. */
        [[nodiscard]] ResourceRef<const ImageView> attachment(std::string_view name) const;

        /** @brief The image behind the attachment called @p name, or an empty ref if there is none. */
        [[nodiscard]] ResourceRef<const Image> image(std::string_view name) const;

        /** @brief The image behind colour attachment @p index, or an empty ref if there is none. */
        [[nodiscard]] ResourceRef<const Image> colorImage(glm::u32 index = 0) const;

        /** @brief The image behind the depth target, or an empty ref if there is none. */
        [[nodiscard]] ResourceRef<const Image> depthImage() const;

        /** @brief Every name its attachments answer to, in order, for a diagnostic. */
        [[nodiscard]] std::vector<std::string> attachmentNames() const;
        ///@}

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
        mutable std::vector<Attachment> _colorAttachments {};
        mutable std::optional<Attachment> _depthAttachment = std::nullopt;
        mutable std::optional<Attachment> _stencilAttachment = std::nullopt;
        SampleCount _sampleCount = SampleCount::e1;
        ClearValues _clearValues {};
        ResolveMode _resolveMode = ResolveMode::eNone;
    };
}
