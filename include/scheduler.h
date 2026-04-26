//
// Created by eduard on 11.03.2026.
//

#pragma once
#include <glm/fwd.hpp>
#include <vector>
#include <functional>
#include <memory>
#include <unordered_set>

#include "commandBuffer.h"

namespace gfx
{
    enum class MSAA;

    class Frame
    {
    public:
        explicit Frame(glm::u32 imageIndex);
        virtual ~Frame() = default;

        Frame(const Frame&) = delete;
        Frame& operator=(const Frame&) = delete;

		[[nodiscard]] glm::u32 getImageIndex() const { return _imageIndex; }
		[[nodiscard]] gfx::CommandBuffer& getCommandBuffer() const { return *_commandBuffer; }
    protected:
        glm::u32 _imageIndex;
		std::unique_ptr<gfx::CommandBuffer> _commandBuffer;
    };

    class Scheduler
    {
    public:
        struct Builder {
            glm::u32 minImageCount = 2;
            glm::u32 imageCount = 2;

            Builder& setMinImageCount(const glm::u32 minImageCount) { this->minImageCount = minImageCount; return *this; }
            Builder& setImageCount(const glm::u32 imageCount) { this->imageCount = imageCount; return *this; }
            [[nodiscard]] Scheduler* build() const;
        };

        virtual ~Scheduler() = default;
    	virtual void Initialize() = 0;

        [[nodiscard]] glm::u32 getImageCount() const { return _imageCount; }
    	[[nodiscard]] virtual glm::u32 getCurrentImageIndex() const { return _currentFrame; }
        [[nodiscard]] const gfx::Frame &getCurrentFrame() const { return *_frames.at(_currentFrame); }
        [[nodiscard]] const gfx::Frame &getNextFrame() const { return *_frames.at((_currentFrame + 1) % _imageCount); }

    	void advanceFrame() const { _currentFrame = (_currentFrame + 1) % _imageCount; }

        [[nodiscard]] std::vector<std::reference_wrapper<Frame>> getFrames() const
        {
    		std::vector<std::reference_wrapper<Frame>> frames;
			for (const auto& frame : _frames) {
				frames.emplace_back(*frame);
			}
			return frames;
        }

        virtual void Draw(const std::function<void(gfx::CommandBuffer&)>& renderFunc) const { _started = true; }

    	[[nodiscard]] bool hasStarted() const { return _started; }
    	virtual void WaitIdle() const = 0;

    	std::unordered_set<glm::u32> ImageIndicesExcept(const glm::u32 index) const
		{
			std::unordered_set<glm::u32> indices;
			for (glm::u32 i = 0; i < _imageCount; ++i) {
				if (i != index) {
					indices.insert(i);
				}
			}
			return indices;
		}

    protected:
    	virtual void createFrames() = 0;
        explicit Scheduler(const Builder& createInfo);
    	mutable bool _started = false;

        glm::u32 _minImageCount;
        glm::u32 _imageCount;
	    mutable glm::u32 _currentFrame = 0;
    	std::vector<std::unique_ptr<Frame>> _frames;
    };
}

