//
// Created by radue on 2/28/2026.
//

#pragma once
#include "../../../include/framebuffer.h"

namespace kor { class Window; }

namespace kor::vk
{
    class SwapChain;

    class Framebuffer final : public kor::Framebuffer {
    public:
        void Bind() const override;
        void Unbind() const override;

        /** @brief @p window's default framebuffer: its swap chain's image, and its depth target. */
        explicit Framebuffer(const kor::Window& window);
        explicit Framebuffer(const Framebuffer::Builder& builder);

        ~Framebuffer() override;
        void DoResize(const glm::uvec2& newExtent) override;

    private:
        void attachSwapChain();
        const SwapChain* _swapChain = nullptr;   // a default framebuffer's; outlived by it
    };
}
