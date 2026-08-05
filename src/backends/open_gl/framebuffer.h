//
// Created by radue on 2/21/2026.
//

#pragma once
#include <vector>

#include <GL/glew.h>
#include <framebuffer.h>

namespace kor::ogl {
    class Framebuffer final : public kor::Framebuffer {
    public:
        Framebuffer();

        explicit Framebuffer(const kor::Framebuffer::Builder& createInfo);
        ~Framebuffer() override;

        GLuint operator*() const;
        void Bind() const override;
        void Unbind() const override;
        [[nodiscard]] bool hasDepthStencilAttachment() const;

    private:
        /// Point every GL attachment at its image's *current* texture, and report the result.
        void attachAll() const;

        /// Re-attach when an attachment's texture has been replaced underneath us.
        ///
        /// glNamedFramebufferTexture stores the texture *object*, not a reference to whatever the
        /// engine holds — so a resize, which recreates the texture (@see Image::doResize), leaves
        /// this framebuffer pointing at a deleted one and therefore incomplete. Image *views*
        /// forward to the live id and survive the swap; a framebuffer cannot, and has to be told.
        void Refresh() const;

        GLuint _id;

        // The *generation* of each attached image at the time it was attached — not its texture
        // id. A resize deletes the texture and immediately creates a new one, and the driver
        // hands back the very same name for it, so comparing ids sees nothing while GL's
        // attachment still refers to the deleted object. The generation is what actually moves.
        mutable std::vector<glm::u64> _attachedColor;
        mutable glm::u64 _attachedDepth = 0;
        mutable glm::u64 _attachedStencil = 0;
        mutable bool _attached = false;
    };
}

