//
// Created by radue on 2/23/2026.
//

#include "mesh.h"
#include "impl/open_gl/mesh.h"

#include "io/window.h"

namespace gfx
{
    Mesh::Mesh(CreateInfo& createInfo)
        : _vertexCount(std::move(createInfo.vertexCount)),
          _vertexBuffer(std::move(createInfo.vertexBuffers)),
          _indexCount(std::move(createInfo.indexCount)),
          _indexBuffer(std::move(createInfo.indexBuffer)),
          _indexType(std::move(createInfo.indexType)),
          _indirectBuffer(std::move(createInfo.indirectBuffer)),
          _vertexBindingDescription(std::move(createInfo.vertexBindingDescription)),
          _vertexAttributeDescription(std::move(createInfo.vertexAttributeDescription)) {}

    std::unique_ptr<Mesh> Mesh::Create(CreateInfo& createInfo)
    {
        switch (Context::Window().getAPI())
        {
        case API::eOpenGL:
            return std::make_unique<ogl::Mesh>(createInfo);
        case API::eVulkan:
            throw std::runtime_error("Vulkan mesh creation not implemented yet!");
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }
}
