//
// Created by radue on 2/18/2026.
//

#include "buffer.h"
#include <iostream>
#include "ogl_err_handling.h"

namespace gfx::ogl
{
    Buffer::Buffer(const gfx::Buffer::Builder& createInfo) : gfx::Buffer(createInfo), _defaultTarget(GetTargetFromUsage(createInfo.usage))
    {
        if (createInfo.usage & Usage::eUniform) {
            if (createInfo.size > 0xFFFF) {
                std::cerr << "Warning: Uniform buffer size exceeds the maximum allowed size of 65536 bytes! Attempting to create a uniform buffer of size " << createInfo.size << " bytes." << std::endl;
            }
        }

        glCreateBuffers(1, &_id);
        glCheckError();

        glNamedBufferStorage(_id, createInfo.size, nullptr, GetFlagsFromMemoryProperties(createInfo.memoryProperties));
        glCheckError();
    }

    Buffer::~Buffer()
    {
        glDeleteBuffers(1, &_id);
        glCheckError();
    }

    void Buffer::Map() const
    {
        if (!(_memoryProperties & MemoryProperty::eHostVisible)) {
            std::cerr << "Error: Attempting to map a buffer that is not host visible! Buffer ID: " << _id << std::endl;
            return;
        }

        if (_mappedPtr) {
            std::cerr << "Warning: Buffer is already mapped! Attempting to map buffer with ID " << _id << " again." << std::endl;
            return;
        }

        auto accessFlags = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
        if (_memoryProperties & MemoryProperty::eHostCoherent) {
            accessFlags |= GL_MAP_COHERENT_BIT;
        } else if (_memoryProperties & MemoryProperty::eHostVisible) {
            accessFlags |= GL_MAP_FLUSH_EXPLICIT_BIT;
        }

        _mappedPtr = glMapNamedBufferRange(_id, 0, _size, accessFlags);
        glCheckError();
        if (!_mappedPtr) {
            std::cerr << "Failed to map buffer with ID " << _id << "!" << std::endl;
        }
    }

    void Buffer::Unmap() const
    {
        if (!_mappedPtr) {
            std::cerr << "Warning: Buffer is not mapped! Attempting to unmap buffer with ID " << _id << " which is not currently mapped." << std::endl;
            return;
        }

        glUnmapNamedBuffer(_id);
        _mappedPtr = nullptr;
        glCheckError();
    }

    void Buffer::CopyFrom(const gfx::Buffer& srcBuffer, const glm::i64 srcOffset, const glm::i64 dstOffset, const glm::i64 size) const
    {
        const auto& srcBufferGL = dynamic_cast<Buffer&>(const_cast<gfx::Buffer&>(srcBuffer));

        if (!(srcBufferGL.getUsage() & Usage::eTransferSrc)) {
            std::cerr << "Error: Source buffer is not a transfer source! Buffer ID: " << srcBufferGL._id << std::endl;
            return;
        }
        if (!(_usage & Usage::eTransferDst)) {
            std::cerr << "Error: Destination buffer is not a transfer destination! Buffer ID: " << _id << std::endl;
            return;
        }
        if (srcOffset + getSize() > srcBufferGL.getSize()) {
            std::cerr << "Error: Source buffer copy range exceeds buffer size! Buffer ID: " << srcBufferGL._id << ", Source Offset: " << srcOffset << ", Size: " << size << ", Buffer Size: " << srcBufferGL._size << std::endl;
            return;
        }
        if (dstOffset + size > size) {
            std::cerr << "Error: Destination buffer copy range exceeds buffer size! Buffer ID: " << _id << ", Destination Offset: " << dstOffset << ", Size: " << size << ", Buffer Size: " << size << std::endl;
            return;
        }

        glCopyNamedBufferSubData(srcBufferGL._id, _id, srcOffset, dstOffset, size);
        glCheckError();
    }

    void Buffer::Bind(GLenum target)
    {
        glBindBuffer(target, _id);

        if (!glCheckError())
        {
            _boundTarget = target;
        }
    }

    void Buffer::Unbind()
    {
        if (_boundTarget) {
            glBindBuffer(*_boundTarget, 0);
            glCheckError();
            _boundTarget = std::nullopt;
        } else {
            std::cerr << "Warning: Attempting to unbind a buffer that is not currently bound! Buffer ID: " << _id << std::endl;
        }
    }

    GLenum Buffer::GetTargetFromUsage(Flags<gfx::Buffer::Usage> usage)
    {
        if (usage & Usage::eStorage) {
            return GL_SHADER_STORAGE_BUFFER;
        }
        if (usage & Usage::eVertex) {
            return GL_ARRAY_BUFFER;
        }
        if (usage & Usage::eIndex) {
            return GL_ELEMENT_ARRAY_BUFFER;
        }
        if (usage & Usage::eUniform) {
            return GL_UNIFORM_BUFFER;
        }
        if (usage & Usage::eIndirect) {
            return GL_DRAW_INDIRECT_BUFFER;
        }
        if (usage & Usage::eTransferSrc) {
            return GL_COPY_READ_BUFFER;
        }
        if (usage & Usage::eTransferDst) {
            return GL_COPY_WRITE_BUFFER;
        }
        if (usage & Usage::eTexel) {
            return GL_TEXTURE_BUFFER;
        }
        std::cerr << "Warning: Buffer usage does not specify a valid target! Defaulting to GL_ARRAY_BUFFER." << std::endl;
        return GL_ARRAY_BUFFER;
    }

    GLenum Buffer::GetFlagsFromMemoryProperties(Flags<gfx::Buffer::MemoryProperty> memoryProperties)
    {
        GLenum flags = 0;
        if (memoryProperties & MemoryProperty::eHostVisible) {
            flags |= GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
        }
        if (memoryProperties & MemoryProperty::eHostCoherent) {
            flags |= GL_MAP_COHERENT_BIT | GL_MAP_PERSISTENT_BIT;
        }
        if (memoryProperties & MemoryProperty::eHostCached) {
            flags |= GL_MAP_PERSISTENT_BIT;
        }
        return flags;
    }



}
