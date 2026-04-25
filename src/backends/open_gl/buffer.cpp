//
// Created by radue on 2/18/2026.
//

#include "buffer.h"
#include <iostream>
#include "ogl_err_handling.h"

namespace gfx::ogl
{
    Buffer::Buffer(const gfx::Buffer::RawBuilder& createInfo) : gfx::Buffer(createInfo), _defaultTarget(GetTargetFromUsage(createInfo.usage))
    {
        if (createInfo.usage & Usage::eUniform) {
            if (createInfo.size > 0xFFFF) {
                std::cerr << "Warning: Uniform buffer size exceeds the maximum allowed size of 65536 bytes! Attempting to create a uniform buffer of size " << createInfo.size << " bytes." << std::endl;
            }
        }

        glCreateBuffers(1, &_id);
        glCheckError();

        glNamedBufferStorage(_id, createInfo.size, nullptr, GetFlagsFromType(createInfo.type));
        glCheckError();
    }

    Buffer::~Buffer()
    {
        glDeleteBuffers(1, &_id);
        glCheckError();
    }

    void Buffer::Map() const
    {
        if (_type == Type::eDeviceLocal) {
            gfx::log::error("Attempted to map a device-local buffer! Buffer ID: {}. Device-local buffers are not mappable, as they reside in GPU-only memory. Please use a staging buffer for data transfer to or from device-local buffers.", _id);
            return;
        }

        const auto accessFlags = GetFlagsFromType(_type);
        _mappedPtr = glMapNamedBufferRange(
            _id,
            0,
            static_cast<GLsizeiptr>(_size),
            accessFlags
        );
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

    void Buffer::Flush(const glm::i64 size, const glm::u64 offset) const {
        glFlushMappedNamedBufferRange(
            _id,
            static_cast<GLintptr>(offset),
            static_cast<GLsizeiptr>(size)
        );
        glCheckError();
    }

    void Buffer::Invalidate(const glm::i64 size, const glm::u64 offset) const {
        glInvalidateBufferSubData(
            _id,
            static_cast<GLintptr>(offset),
            static_cast<GLsizeiptr>(size)
        );
        glCheckError();
    }

    void Buffer::CopyFrom(const gfx::Buffer& srcBuffer, const glm::u64 srcOffset, const glm::u64 dstOffset, const glm::u64 size) const
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
        if (srcOffset > srcBufferGL.getSize() || size > (srcBufferGL.getSize() - srcOffset)) {
            std::cerr << "Error: Source buffer copy range exceeds buffer size! Buffer ID: " << srcBufferGL._id << ", Source Offset: " << srcOffset << ", Size: " << size << ", Buffer Size: " << srcBufferGL._size << std::endl;
            return;
        }
        if (dstOffset > getSize() || size > (getSize() - dstOffset)) {
            std::cerr << "Error: Destination buffer copy range exceeds buffer size! Buffer ID: " << _id << ", Destination Offset: " << dstOffset << ", Size: " << size << ", Buffer Size: " << getSize() << std::endl;
            return;
        }

        glCopyNamedBufferSubData(
            srcBufferGL._id,
            _id,
            static_cast<GLintptr>(srcOffset),
            static_cast<GLintptr>(dstOffset),
            static_cast<GLsizeiptr>(size)
        );
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
        gfx::log::error("Unknown buffer usage flags specified! Defaulting to GL_ARRAY_BUFFER. Usage value: {}", static_cast<int>(usage));
        return GL_ARRAY_BUFFER;
    }

    GLenum Buffer::GetFlagsFromType(const Type type) {
        switch (type) {
            case Type::eDeviceLocal:
                return GL_DYNAMIC_STORAGE_BIT;
            case Type::eStaging:
                return GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            case Type::eReadback:
                return GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            case Type::eDynamic:
                return GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT | GL_MAP_READ_BIT;
            default:
                gfx::log::error("Unknown buffer type specified! Defaulting to GL_DYNAMIC_STORAGE_BIT. Type value: {}", static_cast<int>(type));
                return GL_DYNAMIC_STORAGE_BIT;
        }
    }



}
