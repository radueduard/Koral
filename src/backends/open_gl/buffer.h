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
        explicit Buffer(const gfx::Buffer::RawBuilder& createInfo);
        ~Buffer() override;

        void CopyFrom(const gfx::Buffer& srcBuffer, glm::u64 srcOffset, glm::u64 dstOffset, glm::u64 size) const override;

        void Bind(GLenum target);
        void Unbind();

        GLuint operator*() const { return _id; }

        void Map() const override;
        void Unmap() const override;
        void Flush(glm::i64 size, glm::u64 offset) const override;
        void Invalidate(glm::i64 size, glm::u64 offset) const override;

    private:
        GLuint _id = 0;
        static GLenum GetTargetFromUsage(Flags<gfx::Buffer::Usage> usage);
        static GLenum GetFlagsFromType(Type type);

        std::optional<GLenum> _boundTarget = std::nullopt;

        GLenum _defaultTarget;
    };
}
