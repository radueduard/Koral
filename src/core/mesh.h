//
// Created by radue on 2/23/2026.
//

#pragma once

#include <filesystem>

#include "comandBuffer.h"
#include "graphicsPipeline.h"
#include "buffer.h"


namespace gfx
{
    struct VertexInputAttributeDescription
    {
        glm::u32 location;
        glm::u32 binding;
        glm::u32 channelCount;
        ChannelType channelType;
        glm::u32 offset;
    };

    struct VertexInputBindingDescription
    {
        glm::u32 binding;
        glm::u32 stride;
    };

    class Mesh
    {
    public:
        struct Builder
        {
            glm::u64 vertexCount;
            std::vector<std::unique_ptr<Buffer>> vertexBuffers;

            std::optional<glm::u64> indexCount = std::nullopt;
            std::optional<std::unique_ptr<Buffer>> indexBuffer = std::nullopt;
            std::optional<ChannelType> indexType = std::nullopt;
            std::optional<std::unique_ptr<Buffer>> indirectBuffer = std::nullopt;

            std::vector<VertexInputBindingDescription> vertexBindingDescription = {};
            std::vector<VertexInputAttributeDescription> vertexAttributeDescription = {};

            Builder& AddVertexBuffer(std::unique_ptr<Buffer> vertexBuffer, const VertexInputBindingDescription& bindingDescription) {
                vertexCount = vertexBuffer->getSize() / bindingDescription.stride;
                vertexBuffers.emplace_back(std::move(vertexBuffer));
                vertexBindingDescription.emplace_back(bindingDescription);
                return *this;
            }

            Builder& SetIndexBuffer(std::unique_ptr<Buffer> indexBuffer, const ChannelType indexType) {
                this->indexCount = indexBuffer->getSize() / sizeofChannelType(indexType);
                this->indexBuffer = std::move(indexBuffer);
                this->indexType = indexType;
                return *this;
            }

            Builder& SetIndirectBuffer(std::unique_ptr<Buffer> indirectBuffer) {
                this->indirectBuffer = std::move(indirectBuffer);
                return *this;
            }

            Builder& AddVertexAttributeDescription(const VertexInputAttributeDescription& attributeDescription) {
                vertexAttributeDescription.emplace_back(attributeDescription);
                return *this;
            }

            std::unique_ptr<Mesh> Build() {
                return Mesh::Create(*this);
            }
        };

        virtual ~Mesh() = default;

    protected:
        static std::unique_ptr<Mesh> Create(Builder& createInfo);

        explicit Mesh(Builder& createInfo);

        glm::u64 _vertexCount;
        std::vector<std::unique_ptr<Buffer>> _vertexBuffer = {};

        std::optional<glm::u64> _indexCount = std::nullopt;
        std::optional<std::unique_ptr<Buffer>> _indexBuffer = std::nullopt;
        std::optional<ChannelType> _indexType = std::nullopt;
        std::optional<std::unique_ptr<Buffer>> _indirectBuffer = std::nullopt;

        std::vector<VertexInputBindingDescription> _vertexBindingDescription = {};
        std::vector<VertexInputAttributeDescription> _vertexAttributeDescription = {};
    };
}
