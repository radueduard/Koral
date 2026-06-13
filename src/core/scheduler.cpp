//
// Created by eduard on 11.03.2026.
//

module gfx;
import :scheduler;
import :commandBuffer;

import :vk_scheduler;
import :ogl_scheduler;


namespace gfx
{
    struct FrameCommandBuffer {
        std::unique_ptr<gfx::CommandBuffer> cmd;
    };

    Frame::~Frame() = default;

    gfx::CommandBuffer& Frame::getCommandBuffer() const { return *_commandBuffer->cmd; }

    void Frame::setCommandBuffer(std::unique_ptr<gfx::CommandBuffer> cmd)
    {
        _commandBuffer = std::make_unique<FrameCommandBuffer>(FrameCommandBuffer{std::move(cmd)});
    }

    Frame::Frame(const glm::u32 imageIndex) : _imageIndex(imageIndex)
    {
        setCommandBuffer(CommandBuffer::Create(CommandBuffer::Usage::eGraphics));
    }

    Scheduler* Scheduler::Builder::build() const
    {
        switch (Context::GetWindow().getAPI()) {
        case API::eOpenGL:
            return new ogl::Scheduler(*this);
            case API::eVulkan:
            return new vk::Scheduler(*this);
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    Scheduler::Scheduler(const Builder& createInfo) :
        _minImageCount(createInfo.minImageCount),
        _imageCount(createInfo.imageCount) {}

}
