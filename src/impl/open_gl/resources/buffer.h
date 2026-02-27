//
// Created by radue on 2/18/2026.
//

#pragma once

#include <iostream>
#include <optional>
#include <GL/glew.h>

#include "core/resources/buffer.h"
#include "utils/ogl_err_handling.h"

namespace gfx::ogl
{
    class Buffer : public gfx::Buffer
    {
    public:
        explicit Buffer(const gfx::Buffer::CreateInfo& createInfo);
        ~Buffer() override;

        void Map() override;
        void Unmap() override;
        void Flush(glm::i64 offset, glm::i64 size) const override;
        void Invalidate(glm::i64 offset, glm::i64 size) const override;

        void CopyFrom(const gfx::Buffer& srcBuffer, glm::i64 srcOffset, glm::i64 dstOffset, glm::i64 size) const override;

        void Bind(GLenum target);
        void Unbind();

        GLuint operator*() const { return _id; }

    private:
        GLuint _id = 0;
        static GLenum GetTargetFromUsage(Flags<gfx::Buffer::Usage> usage);
        static GLenum GetFlagsFromMemoryProperties(Flags<gfx::Buffer::MemoryProperty> memoryProperties);


        std::optional<GLenum> _boundTarget = std::nullopt;

        GLenum _defaultTarget;
    };
}
