#pragma once

#include <tuple>
#include <type_traits>
#include <utility>
#include <glm/glm.hpp>

#include "mesh.h"

namespace gfx {
    template<typename T>
    struct VertexAttribute
    {
        using ValueType = T;
    };

    template<typename T>
    struct is_vertex_attribute : std::false_type {};

    template<typename T>
    struct is_vertex_attribute<VertexAttribute<T>> : std::true_type {};

    template<typename T>
    inline constexpr bool is_vertex_attribute_v = is_vertex_attribute<T>::value;

    // Maps scalar types to engine channel metadata.
    template<typename T>
    struct ScalarChannelTraits;

    template<> struct ScalarChannelTraits<float>         { static constexpr ChannelType channelType = ChannelType::eFloat;  };
    template<> struct ScalarChannelTraits<double>        { static constexpr ChannelType channelType = ChannelType::eDouble; };
    template<> struct ScalarChannelTraits<int>           { static constexpr ChannelType channelType = ChannelType::eInt;    };
    template<> struct ScalarChannelTraits<unsigned int>  { static constexpr ChannelType channelType = ChannelType::eUInt;   };
    template<> struct ScalarChannelTraits<short>         { static constexpr ChannelType channelType = ChannelType::eShort;  };
    template<> struct ScalarChannelTraits<unsigned short>{ static constexpr ChannelType channelType = ChannelType::eUShort; };
    template<> struct ScalarChannelTraits<signed char>   { static constexpr ChannelType channelType = ChannelType::eByte;   };
    template<> struct ScalarChannelTraits<unsigned char> { static constexpr ChannelType channelType = ChannelType::eUByte;  };

    template<typename T>
    struct VertexValueTraits
    {
        static constexpr glm::u32 channelCount = 1;
        static constexpr ChannelType channelType = ScalarChannelTraits<std::remove_cv_t<T>>::channelType;
    };

    template<glm::length_t L, typename T, glm::qualifier Q>
    struct VertexValueTraits<glm::vec<L, T, Q>>
    {
        static constexpr glm::u32 channelCount = static_cast<glm::u32>(L);
        static constexpr ChannelType channelType = ScalarChannelTraits<std::remove_cv_t<T>>::channelType;
    };

    // --- helpers for actual vertex storage with flat constructor ---
    template<typename... Ts>
    struct VertexStorage;

    template<>
    struct VertexStorage<>
    {
        constexpr VertexStorage() = default;
    };

    template<typename T, typename... Rest>
    struct VertexStorage<T, Rest...>
    {
        T value{};
        VertexStorage<Rest...> tail{};

        constexpr VertexStorage() = default;
        constexpr VertexStorage(const T& v, const Rest&... rest) : value(v), tail(rest...) {}
    };

    template<std::size_t I, typename T, typename... Rest>
    constexpr auto& StorageGet(VertexStorage<T, Rest...>& storage)
    {
        if constexpr (I == 0) return storage.value;
        else return StorageGet<I - 1>(storage.tail);
    }

    template<std::size_t I, typename T, typename... Rest>
    constexpr const auto& StorageGet(const VertexStorage<T, Rest...>& storage)
    {
        if constexpr (I == 0) return storage.value;
        else return StorageGet<I - 1>(storage.tail);
    }

    class GFX_API DefaultMeshRegistry
    {
    public:
        using BindingGetter = const std::vector<VertexInputBindingDescription>& (*)();
        using AttributeGetter = const std::vector<VertexInputAttributeDescription>& (*)();

        template<MeshType T>
        static void TryRegister()
        {
            std::call_once(_once, []() {
                _bindingGetter = []() -> const std::vector<VertexInputBindingDescription>& {
                    return T::VertexBindingDescription();
                };
                _attributeGetter = []() -> const std::vector<VertexInputAttributeDescription>& {
                    return T::VertexAttributeDescription();
                };
            });
        }

        static const std::vector<VertexInputBindingDescription>& Bindings()
        {
            return _bindingGetter ? _bindingGetter() : NullMesh::VertexBindingDescription();
        }

        static const std::vector<VertexInputAttributeDescription>& Attributes()
        {
            return _attributeGetter ? _attributeGetter() : NullMesh::VertexAttributeDescription();
        }

    private:
        inline static std::once_flag _once{};
        inline static BindingGetter _bindingGetter = nullptr;
        inline static AttributeGetter _attributeGetter = nullptr;
    };

    template<typename... Attrs> requires (is_vertex_attribute_v<Attrs> && ...)
    struct ParamVertex
    {
        using Attributes = std::tuple<Attrs...>;
        using Storage = VertexStorage<typename Attrs::ValueType...>;

        Storage storage{};

        // This enables: VertexSimple{ {..}, {..}, {..} }
        constexpr ParamVertex() = default;
        constexpr ParamVertex(const typename Attrs::ValueType&... values) : storage(values...) {}

        template<std::size_t I>
        constexpr auto& get() { return StorageGet<I>(storage); }

        template<std::size_t I>
        constexpr const auto& get() const { return StorageGet<I>(storage); }

        static constexpr glm::u32 kAttributeCount = static_cast<glm::u32>(sizeof...(Attrs));
        static constexpr glm::u32 kStride = static_cast<glm::u32>(sizeof(Storage));

        // Real offset from actual memory layout (handles alignment correctly)
        template<std::size_t I>
        static glm::u32 OffsetOf()
        {
            ParamVertex v{};
            const auto* base = reinterpret_cast<const unsigned char*>(&v.storage);
            const auto* elem = reinterpret_cast<const unsigned char*>(&v.template get<I>());
            return static_cast<glm::u32>(elem - base);
        }
    };

    template<typename... Streams>
    class ParamMesh : public CustomMesh<ParamMesh<Streams...>>
    {
    public:
        using Self = ParamMesh<Streams...>;
        using Base = CustomMesh<Self>;
        using Builder = typename Base::Builder;

        explicit ParamMesh(Builder& createInfo) : Base(createInfo) {
            DefaultMeshRegistry::TryRegister<Self>(); // first initialized mesh type becomes default
        }

        static void DefineMesh()
        {
            Base::_vertexBindingDescription.clear();
            Base::_vertexAttributeDescription.clear();

            Base::_vertexBindingDescription.reserve(sizeof...(Streams));
            Base::_vertexAttributeDescription.reserve((0u + ... + Streams::kAttributeCount));

            glm::u32 location = 0;
            glm::u32 binding = 0;
            (appendStream<Streams>(binding++, location), ...);
        }

    static std::unique_ptr<Self> Create(std::span<const Streams>... streams)
    {
        Builder builder;
        setVertexBuffers(builder, std::tuple<std::span<const Streams>...>{streams...}, std::index_sequence_for<Streams...>{});
        return builder.Build();
    }

    template<typename IndexT> requires std::is_integral_v<IndexT>
    static std::unique_ptr<Self> Create(std::span<const Streams>... streams, std::span<const IndexT> indices)
    {
        Builder builder;
        setVertexBuffers(builder, std::tuple<std::span<const Streams>...>{streams...}, std::index_sequence_for<Streams...>{});

        if (!indices.empty())
        {
            auto indexBuffer = makeBuffer(indices, Buffer::Usage::eIndex);
            builder.SetIndexBuffer(std::move(indexBuffer), indexChannelType<IndexT>());
        }

        return builder.Build();
    }

    // Convenience: vectors
    static std::unique_ptr<Self> Create(const std::vector<Streams>&... streams)
    {
        return Create(std::span<const Streams>(streams)...);
    }

    template<typename IndexT>
    static std::unique_ptr<Self> Create(const std::vector<Streams>&... streams, const std::vector<IndexT>& indices)
    {
        return Create(std::span<const Streams>(streams)..., std::span<const IndexT>(indices));
    }

    // Convenience: arrays
    template<std::size_t... N>
    static std::unique_ptr<Self> Create(const std::array<Streams, N>&... streams)
    {
        return Create(std::span<const Streams>(streams)...);
    }

    template<typename IndexT, std::size_t... N, std::size_t NI>
    static std::unique_ptr<Self> Create(const std::array<Streams, N>&... streams, const std::array<IndexT, NI>& indices)
    {
        return Create(std::span<const Streams>(streams)..., std::span<const IndexT>(indices));
    }

private:
    template<typename T>
    static std::unique_ptr<Buffer> makeBuffer(std::span<const T> data, Flags<Buffer::Usage> usage)
        {
            const auto byteSize = static_cast<glm::u64>(data.size_bytes());

            // Keep final buffers transfer-capable as requested.
            const auto finalUsage = usage | Buffer::Usage::eTransferDst | Buffer::Usage::eTransferSrc;

            auto finalBuffer = Buffer::Builder()
                .setSize(byteSize)
                .setUsage(finalUsage)
                .setMemoryProperties(Buffer::MemoryProperty::eDeviceLocal)
                .build();

            if (byteSize == 0 || data.empty())
                return finalBuffer;

            auto stagingBuffer = Buffer::Builder()
                .setSize(byteSize)
                .setUsage(Buffer::Usage::eTransferSrc)
                .setMemoryProperties(Buffer::MemoryProperty::eHostVisible)
                .addMemoryProperty(Buffer::MemoryProperty::eHostCoherent)
                .build();

            stagingBuffer->Map();
            stagingBuffer->Write(data);
            stagingBuffer->Unmap();

            finalBuffer->CopyFrom(*stagingBuffer, 0, 0, static_cast<glm::i64>(byteSize));
            return finalBuffer;
        }

    template<typename Tuple, std::size_t... I>
    static void setVertexBuffers(Builder& builder, const Tuple& streamTuple, std::index_sequence<I...>)
    {
        (builder.SetVertexBuffer(static_cast<glm::u32>(I),
            makeBuffer(std::get<I>(streamTuple), Buffer::Usage::eVertex)), ...);
    }

    template<typename IndexT>
    static consteval ChannelType indexChannelType()
    {
        using T = std::remove_cv_t<IndexT>;

        if constexpr (std::is_same_v<T, std::uint32_t>) return ChannelType::eUInt;
        else if constexpr (std::is_same_v<T, std::int32_t>) return ChannelType::eInt;
        else if constexpr (std::is_same_v<T, std::uint16_t>) return ChannelType::eUShort;
        else if constexpr (std::is_same_v<T, std::int16_t>) return ChannelType::eShort;
        else if constexpr (std::is_same_v<T, std::uint8_t>) return ChannelType::eUByte;
        else if constexpr (std::is_same_v<T, std::int8_t>) return ChannelType::eByte;
        else static_assert(!sizeof(T), "Unsupported index type for ParamMesh::Create");
    }

    private:
        template<typename Stream>
        static void appendStream(glm::u32 binding, glm::u32& location)
        {
            Base::_vertexBindingDescription.push_back(VertexInputBindingDescription{
                .binding = binding,
                .stride = Stream::kStride
            });

            appendAttributes<Stream>(binding, location, std::make_index_sequence<Stream::kAttributeCount>{});
        }

        template<typename Stream, std::size_t... I>
        static void appendAttributes(glm::u32 binding, glm::u32& location, std::index_sequence<I...>)
        {
            (appendOneAttribute<Stream, std::tuple_element_t<I, typename Stream::Attributes>, I>(binding, location), ...);
        }

        template<typename Stream, typename Attr, std::size_t I>
        static void appendOneAttribute(glm::u32 binding, glm::u32& location)
        {
            using Value = typename Attr::ValueType;
            using Traits = VertexValueTraits<Value>;

            Base::_vertexAttributeDescription.push_back(VertexInputAttributeDescription{
                .location = location++,
                .binding = binding,
                .channelCount = Traits::channelCount,
                .channelType = Traits::channelType,
                .offset = Stream::template OffsetOf<I>()
            });
        }
    };

    // Common named vertex attributes.
    // These are aliases over VertexAttribute<T>, so they work directly in ParamVertex<...>.
    using Position2  = VertexAttribute<glm::vec2>;
    using Position   = VertexAttribute<glm::vec3>;
    using Position4  = VertexAttribute<glm::vec4>;

    using Normal  = VertexAttribute<glm::vec3>;
    using Normal4 = VertexAttribute<glm::vec4>;

    using Color3     = VertexAttribute<glm::vec3>;
    using Color      = VertexAttribute<glm::vec4>;

    using UV        = VertexAttribute<glm::vec2>;
    using UV3       = VertexAttribute<glm::vec3>;

    using Tangent         = VertexAttribute<glm::vec3>;
    using PackedTangent   = VertexAttribute<glm::vec4>; // xyz + handedness in w
    using Bitangent       = VertexAttribute<glm::vec3>;

    using BoneIds     = VertexAttribute<glm::ivec4>;
    using BoneWeights = VertexAttribute<glm::vec4>;
}

