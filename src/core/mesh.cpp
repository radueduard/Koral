//
// Created by radue on 09.08.2026.
//

#include <mesh.h>

#include <format>
#include <ranges>

namespace kor {

    namespace {
        // How many bytes one index of this type occupies, and 0 for a channel type that cannot be
        // an index at all. Signed types answer for their unsigned counterpart: an index is never
        // negative, so the two are the same bytes, and the backends only know the unsigned ones.
        glm::u32 indexWidth(const ChannelType type)
        {
            switch (type) {
            case ChannelType::eByte:
            case ChannelType::eUByte:  return 1;
            case ChannelType::eShort:
            case ChannelType::eUShort: return 2;
            case ChannelType::eInt:
            case ChannelType::eUInt:   return 4;
            default:                   return 0;
            }
        }

        ChannelType unsignedIndexType(const ChannelType type)
        {
            switch (indexWidth(type)) {
            case 1:  return ChannelType::eUByte;
            case 2:  return ChannelType::eUShort;
            default: return ChannelType::eUInt;
            }
        }
    }

    Mesh::Builder& Mesh::Builder::setVertexBuffer(const glm::u32 binding, ResourceRef<Buffer> vertexBuffer)
    {
        if (_vertexBuffers.size() <= binding)
            _vertexBuffers.resize(binding + 1);
        _vertexBuffers[binding] = std::move(vertexBuffer);
        return *this;
    }

    Mesh::Builder& Mesh::Builder::setVertexBuffer(const glm::u32 binding, Resource<Buffer>&& vertexBuffer)
    {
        auto owned = std::make_shared<Resource<Buffer>>(std::move(vertexBuffer));
        setVertexBuffer(binding, ResourceRef<Buffer>(*owned));
        _ownedBuffers.push_back(std::move(owned));
        return *this;
    }

    Mesh::Builder& Mesh::Builder::setIndexBuffer(ResourceRef<Buffer> indexBuffer, const ChannelType indexType)
    {
        _indexBuffer = std::move(indexBuffer);
        _indexType = indexType;
        return *this;
    }

    Mesh::Builder& Mesh::Builder::setIndexBuffer(Resource<Buffer>&& indexBuffer, const ChannelType indexType)
    {
        auto owned = std::make_shared<Resource<Buffer>>(std::move(indexBuffer));
        setIndexBuffer(ResourceRef<Buffer>(*owned), indexType);
        _ownedBuffers.push_back(std::move(owned));
        return *this;
    }

    Mesh::Builder& Mesh::Builder::setVertexLayout(VertexLayout layout)
    {
        _vertexLayout = std::move(layout);
        return *this;
    }

    VoidResult Mesh::Builder::populate(Mesh& mesh) const
    {
        // Vertex buffers are addressed by binding number, both here and where they are bound: the
        // backends hand the whole list to the API starting at binding 0, so index i *is* binding i.
        glm::u32 bindingCount = 0;
        for (const auto& binding : _vertexLayout.bindings)
            bindingCount = std::max(bindingCount, binding.binding + 1);

        std::vector<ResourceRef<Buffer>> vertexBuffers(bindingCount);
        glm::u64 vertexCount = 0;
        bool counted = false;

        for (const auto& [binding, stride] : _vertexLayout.bindings)
        {
            if (binding >= _vertexBuffers.size() || !_vertexBuffers[binding].alive()) {
                addError(ErrorCode::eInvalidArgument,
                         std::format("no vertex buffer was set for binding {}, which the vertex layout declares.",
                                     binding));
                continue;
            }

            const auto& buffer = _vertexBuffers[binding];
            adopt(buffer, std::format("the vertex buffer for binding {}", binding));
            if (!buffer.valid()) continue;   // poisoned or destroyed; adopt() has already said so

            if (stride == 0) {
                addError(ErrorCode::eInvalidArgument,
                         std::format("binding {} declares a stride of 0 bytes, so it describes no vertices.",
                                     binding));
                continue;
            }
            if (!(buffer->getUsage() & Buffer::Usage::eVertex)) {
                addError(ErrorCode::eInvalidArgument,
                         std::format("the buffer set for binding {} was not created with Buffer::Usage::eVertex.",
                                     binding));
                continue;
            }

            const auto count = buffer->getSize() / stride;
            if (!counted) {
                vertexCount = count;
                counted = true;
            } else if (count != vertexCount) {
                addError(ErrorCode::eInvalidArgument,
                         std::format("the vertex buffers disagree on how many vertices there are: binding {} holds "
                                     "{} at a stride of {} bytes, where an earlier binding held {}.",
                                     binding, count, stride, vertexCount));
                continue;
            }

            vertexBuffers[binding] = buffer;
        }

        // A buffer set for a binding the layout says nothing about would never be read, and is far
        // more likely to be a layout that was forgotten or written with the wrong binding numbers.
        for (glm::u32 binding = 0; binding < _vertexBuffers.size(); ++binding) {
            if (!_vertexBuffers[binding].alive()) continue;
            const auto declared = std::ranges::any_of(_vertexLayout.bindings,
                [binding](const auto& b) { return b.binding == binding; });
            if (declared) continue;

            if (_vertexLayout.bindings.empty())
                addError(ErrorCode::eInvalidArgument,
                         "a vertex buffer was set but no vertex layout was: nothing describes what it holds.");
            else
                addError(ErrorCode::eInvalidArgument,
                         std::format("a vertex buffer was set for binding {}, which the vertex layout does not declare.",
                                     binding));
        }

        std::optional<ResourceRef<Buffer>> indexBuffer;
        std::optional<glm::u32> indexCount;
        std::optional<ChannelType> indexType;

        if (_indexBuffer.has_value())
        {
            adopt(*_indexBuffer, "the index buffer");
            if (_indexBuffer->valid())
            {
                const auto width = indexWidth(_indexType);
                if (width == 0) {
                    addError(ErrorCode::eInvalidArgument,
                             "the index type must be ChannelType::eUByte, eUShort or eUInt.");
                } else if (!((*_indexBuffer)->getUsage() & Buffer::Usage::eIndex)) {
                    addError(ErrorCode::eInvalidArgument,
                             "the index buffer was not created with Buffer::Usage::eIndex.");
                } else {
                    indexBuffer = *_indexBuffer;
                    indexType = unsignedIndexType(_indexType);
                    indexCount = static_cast<glm::u32>((*_indexBuffer)->getSize() / width);
                }
            }
        }

        if (auto valid = validate(); !valid) return valid;

        mesh._vertexCount = vertexCount;
        mesh._vertexBuffers = std::move(vertexBuffers);
        mesh._indexBuffer = indexBuffer;
        mesh._indexCount = indexCount;
        mesh._indexType = indexType;
        mesh._ownedBuffers = _ownedBuffers;
        mesh.setVertexLayout(_vertexLayout);
        return {};
    }

    Result<std::unique_ptr<Mesh>> Mesh::Builder::create() const
    {
        beginAttempt();

        auto mesh = std::make_unique<Mesh>();
        if (auto filled = populate(*mesh); !filled) return std::unexpected(filled.error());
        return mesh;
    }

    Resource<Mesh> Mesh::Builder::build(const std::source_location where) const
    {
        return materialize<Mesh>(*this, "Mesh", where);
    }
}
