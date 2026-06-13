//
// Created by radue on 2/18/2026.
//

module;

#include <GL/glew.h>
#include <glm/glm.hpp>

export module gfx:ogl_buffer;
import :ogl_types;

import std;
import :buffer;
import flags;

namespace gfx::ogl
{
    class Buffer : public gfx::Buffer
    {
    public:
        explicit Buffer(const gfx::Buffer::RawBuilder& createInfo);
        ~Buffer() override;

        void Bind(GLenum target);
        void Unbind();

        GLuint operator*() const { return _id; }

        void Map() const override;
        void Unmap() const override;
        void Flush(glm::i64 size, glm::u64 offset) const override;
        void Invalidate(glm::i64 size, glm::u64 offset) const override;

        static GLenum GetTargetFromUsage(Flags<gfx::Buffer::Usage> usage);
        static GLenum GetFlagsFromType(Type type);
    private:
        GLuint _id = 0;
        std::optional<GLenum> _boundTarget = std::nullopt;

        GLenum _defaultTarget;

    public:
        void automaticUpdate() override {}
    };
}
