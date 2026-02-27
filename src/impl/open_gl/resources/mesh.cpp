//
// Created by radue on 2/23/2026.
//

#include "mesh.h"

#include "buffer.h"
#include "graphicsPipeline.h"
#include "utils/ogl_err_handling.h"

namespace gfx::ogl
{
    Mesh::Mesh(CreateInfo& createInfo) : gfx::Mesh(createInfo)
    {
        glGenVertexArrays(1, &_vao);
        glBindVertexArray(_vao);

        for (size_t i = 0; i < _vertexBuffer.size(); ++i)
        {
            const auto& vertexBuffer = dynamic_cast<const ogl::Buffer&>(*_vertexBuffer[i]);
            glBindBuffer(GL_ARRAY_BUFFER, *vertexBuffer);
            glCheckError();

            const auto& [binding, stride] = _vertexBindingDescription[i];
            for (const auto& attributeDescription : _vertexAttributeDescription)
            {
                if (attributeDescription.binding == binding)
                {
                    glEnableVertexAttribArray(attributeDescription.location);
                    glCheckError();
                    glVertexAttribPointer(
                        attributeDescription.location,
                        attributeDescription.channelCount,
                        toGLChannelType(attributeDescription.channelType),
                        GL_FALSE,
                        stride,
                        nullptr);
                }
            }
        }
        if (_indexBuffer.has_value()) {
            const auto& indexBuffer = dynamic_cast<const ogl::Buffer&>(*_indexBuffer.value());
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, *indexBuffer);
            glCheckError();
        }

        glBindVertexArray(0);
    }

    Mesh::~Mesh()
    {
        glDeleteVertexArrays(1, &_vao);
    }

    GLuint Mesh::operator*() const
    {
        return _vao;
    }

    glm::u64 Mesh::getVertexCount() const
    {
        return _vertexCount;
    }

    bool Mesh::hasIndexBuffer() const
    {
        return _indexBuffer.has_value();
    }

    std::optional<glm::u64> Mesh::getIndexCount() const
    {
        return _indexCount;
    }

    std::optional<ChannelType> Mesh::getIndexType() const
    {
        return _indexType;
    }
}
