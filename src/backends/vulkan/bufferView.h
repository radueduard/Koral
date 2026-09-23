//
// Created by radue on 12.09.2026.
//

#pragma once
#include "buffer.h"
#include <bufferView.h>

namespace kor::vk
{
    class DescriptorSet;

    class BufferView final : public kor::BufferView {
        friend class kor::vk::DescriptorSet;
    public:
        explicit BufferView(const Builder& builder);
        ~BufferView() override;

        BufferView(const BufferView&) = delete;
        BufferView& operator=(const BufferView&) = delete;

        ::vk::BufferView operator*() const;
        ::vk::BufferView operator[](size_t i) const;

    private:
        // One VkBufferView per copy of the buffer: a view names a single VkBuffer, and a per-frame
        // buffer is several. @see kor::vk::Buffer::_buffers
        std::vector<::vk::BufferView> _bufferViews {};
    };
}
