//
// Created by radue on 2/18/2026.
//

#pragma once
#include <buffer.h>
#include <optional>

#include <GL/glew.h>

namespace gfx::ogl
{
    class Buffer : public gfx::Buffer
    {
    public:
        explicit Buffer(const gfx::Buffer::Builder& createInfo);
        ~Buffer() override;

        void Map() const override;
        void Unmap() const override;

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
