//
// Created by radue on 2/23/2026.
//

#include "mesh.h"
#include "impl/open_gl/mesh.h"
#include "impl/vulkan/mesh.h"

#include "io/window.h"

namespace gfx
{
    Mesh::Mesh(Builder& createInfo)
        : _vertexCount(createInfo.vertexCount),
          _vertexBuffer(std::move(createInfo.vertexBuffers)),
          _indexCount(createInfo.indexCount),
          _indexBuffer(std::move(createInfo.indexBuffer)),
          _indexType(createInfo.indexType),
          _indirectBuffer(std::move(createInfo.indirectBuffer)),
          _vertexBindingDescription(std::move(createInfo.vertexBindingDescription)),
          _vertexAttributeDescription(std::move(createInfo.vertexAttributeDescription)) {}

    std::unique_ptr<Mesh> Mesh::Create(Builder& createInfo)
    {
        switch (Context::Window().getAPI())
        {
        case API::eOpenGL:
            return std::make_unique<ogl::Mesh>(createInfo);
        case API::eVulkan:
            return std::make_unique<vk::Mesh>(createInfo);
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }
}
