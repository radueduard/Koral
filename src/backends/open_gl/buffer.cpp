//
// Created by radue on 2/18/2026.
//

#include "buffer.h"
#include <iostream>
#include "ogl_err_handling.h"

namespace kor::ogl
{
    Buffer::Buffer(const kor::Buffer::RawBuilder& createInfo) : kor::Buffer(createInfo), _defaultTarget(GetTargetFromUsage(createInfo._usage))
    {
        glCreateBuffers(1, &_id);
        glCheckError();

        const GLbitfield storageFlags = GetFlagsFromType(createInfo._type);
        while (glGetError() != GL_NO_ERROR) {} // clear any pending error before the allocation
        glNamedBufferStorage(_id, createInfo._size, nullptr, storageFlags);

        // A device-local buffer larger than VRAM fails here with OUT_OF_MEMORY, whereas
        // the Vulkan backend's allocator transparently places it in host/GTT memory.
        // Match that: on OOM, recreate the buffer requesting client (host) storage so
        // huge buffers (e.g. the light-index list) still allocate, just from system RAM.
        if (glGetError() == GL_OUT_OF_MEMORY) {
            glDeleteBuffers(1, &_id);
            glCreateBuffers(1, &_id);
            glNamedBufferStorage(_id, createInfo._size, nullptr, storageFlags | GL_CLIENT_STORAGE_BIT);
            if (glGetError() != GL_NO_ERROR)
                kor::log::error("[buffer] failed to allocate {} bytes even from host storage", createInfo._size);
        }
    }

    Buffer::~Buffer()
    {
        glDeleteBuffers(1, &_id);
        glCheckError();
    }

    void Buffer::Map() const
    {
        if (_type == Type::eDeviceLocal) {
            kor::log::error("Attempted to map a device-local buffer! Buffer ID: {}. Device-local buffers are not mappable, as they reside in GPU-only memory. Please use a staging buffer for data transfer to or from device-local buffers.", _id);
            return;
        }

        // glMapNamedBufferRange only accepts the MAP_* access bits; the storage-only
        // bits used at creation (notably GL_DYNAMIC_STORAGE_BIT for eDynamic) are
        // illegal here and raise INVALID_VALUE. Mask to the valid mapping flags.
        constexpr GLbitfield kMapFlagMask =
            GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        const auto accessFlags = GetFlagsFromType(_type) & kMapFlagMask;
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

    void Buffer::Flush(const glm::i64, const glm::u64) const {
        // No-op: our mappings are GL_MAP_COHERENT (staging/readback) or made visible
        // at unmap (dynamic). glFlushMappedNamedBufferRange is only legal on mappings
        // created with GL_MAP_FLUSH_EXPLICIT_BIT, which we never use, so calling it
        // here raised INVALID_OPERATION and did nothing.
    }

    void Buffer::Invalidate(const glm::i64 size, const glm::u64 offset) const {
        glInvalidateBufferSubData(
            _id,
            static_cast<GLintptr>(offset),
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

    GLenum Buffer::GetTargetFromUsage(Flags<kor::Buffer::Usage> usage)
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
        kor::log::error("Unknown buffer usage flags specified! Defaulting to GL_ARRAY_BUFFER. Usage value: {}", static_cast<int>(usage));
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
                kor::log::error("Unknown buffer type specified! Defaulting to GL_DYNAMIC_STORAGE_BIT. Type value: {}", static_cast<int>(type));
                return GL_DYNAMIC_STORAGE_BIT;
        }
    }



}
