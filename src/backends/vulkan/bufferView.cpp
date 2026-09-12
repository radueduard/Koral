//
// Created by radue on 12.09.2026.
//

#include "bufferView.h"

#include "device.h"
#include "vk_enum_conversions.h"
#include "vulkanContext.h"

#include <context.h>
#include <scheduler.h>

namespace kor::vk
{
    BufferView::BufferView(const Builder& builder) : kor::BufferView(builder)
    {
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*_buffer);

        for (const auto& buffer : vkBuffer._buffers) {
            const auto viewInfo = ::vk::BufferViewCreateInfo()
                .setBuffer(buffer)
                .setFormat(getVkFormat(_format))
                .setOffset(static_cast<::vk::DeviceSize>(_offset))
                .setRange(static_cast<::vk::DeviceSize>(_range));
            _bufferViews.emplace_back(vk::Context::Device()->createBufferView(viewInfo));
        }
    }

    BufferView::~BufferView()
    {
        for (const auto& bufferView : _bufferViews) {
            vk::Context::Device()->destroyBufferView(bufferView);
        }
    }

    ::vk::BufferView BufferView::operator*() const
    {
        const auto currentFrame = _isPerFrame ? kor::Context::Scheduler().currentImageIndex() : 0;
        return _bufferViews[currentFrame];
    }

    ::vk::BufferView BufferView::operator[](const size_t i) const
    {
        if (!_isPerFrame) {
            return _bufferViews[0];
        }
        if (i >= _bufferViews.size()) {
            throw std::out_of_range("BufferView index out of range!");
        }
        return _bufferViews[i];
    }
}
