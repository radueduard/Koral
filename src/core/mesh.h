//
// Created by radue on 2/23/2026.
//

#pragma once

#include <filesystem>
#include <optional>

#include "buffer.h"
#include "structs.h"
#include "utils/meshType.h"

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
        Mesh() = default;
        virtual ~Mesh() = default;

        [[nodiscard]] glm::u64 getVertexCount() const { return _vertexCount; }
        [[nodiscard]] bool hasIndexBuffer() const { return _indexBuffer.has_value(); }
        [[nodiscard]] std::optional<glm::u64> getIndexCount() const { return _indexCount; }
        [[nodiscard]] std::optional<ChannelType> getIndexType() const { return _indexType; }

        [[nodiscard]] std::vector<std::reference_wrapper<const Buffer>> getVertexBuffers() const
        {
            std::vector<std::reference_wrapper<const Buffer>> vertexBuffers;
            for (const auto& vertexBuffer : _vertexBuffers)
            {
                vertexBuffers.emplace_back(*vertexBuffer);
            }
            return vertexBuffers;
        }
        [[nodiscard]] std::optional<std::reference_wrapper<const Buffer>> getIndexBuffer() const {
            if (!_indexBuffer.has_value())
                return std::nullopt;
            return std::make_optional(std::reference_wrapper<const Buffer>(*_indexBuffer.value()));
        }

    protected:
        glm::u64 _vertexCount;
        std::vector<std::unique_ptr<Buffer>> _vertexBuffers = {};

        std::optional<glm::u64> _indexCount = std::nullopt;
        std::optional<std::unique_ptr<Buffer>> _indexBuffer = std::nullopt;
        std::optional<ChannelType> _indexType = std::nullopt;
        std::optional<std::unique_ptr<Buffer>> _indirectBuffer = std::nullopt;
    };

    template<typename Derived>
    class CustomMesh : public Mesh
    {
    public:
        struct Builder
        {
            glm::u64 vertexCount = 0;
            std::vector<std::unique_ptr<Buffer>> vertexBuffers {};

            std::optional<glm::u64> indexCount = std::nullopt;
            std::optional<std::unique_ptr<Buffer>> indexBuffer = std::nullopt;
            std::optional<ChannelType> indexType = std::nullopt;
            std::optional<std::unique_ptr<Buffer>> indirectBuffer = std::nullopt;

            explicit Builder()
            {
                if (DefineMeshParent())
                    Derived::DefineMesh();
                vertexBuffers.resize(Derived::VertexBindingDescription().size());
            }

            Builder& SetVertexBuffer(const glm::u32 binding, std::unique_ptr<Buffer> vertexBuffer) {
                if (vertexCount == 0)
                    vertexCount = vertexBuffer->getSize() / _vertexBindingDescription[binding].stride;
                else if (vertexCount != vertexBuffer->getSize() / _vertexBindingDescription[binding].stride)
                    throw std::runtime_error("All vertex buffers must have the same vertex count!");

                vertexBuffers[binding] = std::move(vertexBuffer);
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

            std::unique_ptr<Derived> Build()
            {
                return std::make_unique<Derived>(*this);
            }
        };

        explicit CustomMesh(Builder& createInfo)
        {
            static_assert(MeshType<Derived>, "Derived class must satisfy MeshType concept!");
            _vertexCount = createInfo.vertexCount;
            _vertexBuffers = std::move(createInfo.vertexBuffers);
            _indexCount = createInfo.indexCount;
            _indexBuffer = std::move(createInfo.indexBuffer);
            _indexType = createInfo.indexType;
            _indirectBuffer = std::move(createInfo.indirectBuffer);
        }

        [[nodiscard]] static const std::vector<VertexInputBindingDescription>& VertexBindingDescription() { return _vertexBindingDescription; }
        [[nodiscard]] static const std::vector<VertexInputAttributeDescription>& VertexAttributeDescription() { return _vertexAttributeDescription; }
    protected:

        inline static std::vector<VertexInputBindingDescription> _vertexBindingDescription {};
        inline static std::vector<VertexInputAttributeDescription> _vertexAttributeDescription {};

    private:
        static bool DefineMeshParent()
        {
            static bool defined = false;
            if (defined) return false;
            defined = true;
            return true;
        }
    };

    class NullMesh : public CustomMesh<NullMesh>
    {
    public:
        static void DefineMesh() {}
        explicit NullMesh(Builder& createInfo) : CustomMesh(createInfo) {}
    };
}
