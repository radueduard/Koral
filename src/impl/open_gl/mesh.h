//
// Created by radue on 2/23/2026.
//

#pragma once
#include <GL/glew.h>

#include "buffer.h"
#include "../../core/mesh.h"

namespace gfx::ogl
{
    class Mesh : public gfx::Mesh
    {
    public:
        explicit Mesh(Builder& createInfo);
        ~Mesh() override;

        GLuint operator*() const;

        glm::u64 getVertexCount() const;

        bool hasIndexBuffer() const;
        std::optional<glm::u64> getIndexCount() const;
        std::optional<ChannelType> getIndexType() const;

    private:
        GLuint _vao = 0;
    };
}
