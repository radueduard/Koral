module;

#include <glm/glm.hpp>

export module gfx:meshLayout;
import std;
import :meshBase;
import :buffer;
import :types;
import resource;

namespace gfx {
    export template<typename T>
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

    template<typename T>
    struct Std430AlignTraits
    {
        // scalar base alignment = sizeof(scalar)
        static constexpr std::size_t alignment = sizeof(std::remove_cv_t<T>);
    };

    template<glm::length_t L, typename T, glm::qualifier Q>
    struct Std430AlignTraits<glm::vec<L, T, Q>>
    {
        static constexpr std::size_t N = sizeof(std::remove_cv_t<T>);
        // std430 vector base alignment:
        // vec1 -> N, vec2 -> 2N, vec3/vec4 -> 4N
        static constexpr std::size_t alignment =
            (L == 1 ? N : (L == 2 ? 2 * N : 4 * N));
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
        alignas(Std430AlignTraits<T>::alignment) T value{};
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

    template<typename... Attrs> requires (is_vertex_attribute_v<Attrs> && ...)
    struct ParamVertex
    {
        using Attributes = std::tuple<Attrs...>;
        using Storage = VertexStorage<typename Attrs::ValueType...>;

        Storage storage{};

        constexpr ParamVertex() = default;
        constexpr ParamVertex(const Attrs::ValueType&... values) : storage(values...) {}

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
            const auto* elem = reinterpret_cast<const unsigned char*>(&v.get<I>());
            return static_cast<glm::u32>(elem - base);
        }
    };

    template<typename... Streams>
    class ParamMesh : public CustomMesh<ParamMesh<Streams...>>
    {
    public:
        using Self = ParamMesh;
        using Base = CustomMesh<Self>;
        using Builder = Base::Builder;

        explicit ParamMesh(Builder& createInfo) : Base(createInfo) {
            DefineMesh();
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

    static Resource<Self> Create(std::span<const Streams>... streams)
    {
        Builder builder;
        setVertexBuffers(builder, std::tuple<std::span<const Streams>...>{streams...}, std::index_sequence_for<Streams...>{});
        return builder.Build();
    }

    template<typename IndexT> requires std::is_integral_v<IndexT>
    static Resource<Self> Create(std::span<const Streams>... streams, std::span<const IndexT> indices)
    {
        Builder builder;
        setVertexBuffers(builder, std::tuple<std::span<const Streams>...>{streams...}, std::index_sequence_for<Streams...>{});

        if (!indices.empty())
        {
            auto indexBuffer = CustomMesh<ParamMesh<Streams...>>::makeBuffer(indices, Buffer::Usage::eIndex);
            builder.SetIndexBuffer(std::move(indexBuffer), indexChannelType<IndexT>());
        }

        return builder.Build();
    }

    // Convenience: vectors
    static Resource<Self> Create(const std::vector<Streams>&... streams)
    {
        return Create(std::span<const Streams>(streams)...);
    }

    template<typename IndexT>
    static Resource<Self> Create(const std::vector<Streams>&... streams, const std::vector<IndexT>& indices)
    {
        return Create(std::span<const Streams>(streams)..., std::span<const IndexT>(indices));
    }

    // Convenience: arrays
    template<std::size_t... N>
    static Resource<Self> Create(const std::array<Streams, N>&... streams)
    {
        return Create(std::span<const Streams>(streams)...);
    }

    template<typename IndexT, std::size_t... N, std::size_t NI>
    static Resource<Self> Create(const std::array<Streams, N>&... streams, const std::array<IndexT, NI>& indices)
    {
        return Create(std::span<const Streams>(streams)..., std::span<const IndexT>(indices));
    }

private:
    template<typename Tuple, std::size_t... I>
    static void setVertexBuffers(Builder& builder, const Tuple& streamTuple, std::index_sequence<I...>)
    {
        (builder.SetVertexBuffer(static_cast<glm::u32>(I),
            CustomMesh<ParamMesh<Streams...>>::makeBuffer(std::get<I>(streamTuple), Buffer::Usage::eVertex)), ...);
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
        return ChannelType::eUInt; // dummy return to satisfy compiler
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
            using Value = Attr::ValueType;
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
    export using Position2  = VertexAttribute<glm::vec2>;
    export using Position   = VertexAttribute<glm::vec3>;
    export using Position4  = VertexAttribute<glm::vec4>;

    export using Normal  = VertexAttribute<glm::vec3>;
    export using Normal4 = VertexAttribute<glm::vec4>;

    export using Color3     = VertexAttribute<glm::vec3>;
    export using Color      = VertexAttribute<glm::vec4>;

    export using UV        = VertexAttribute<glm::vec2>;
    export using UV3       = VertexAttribute<glm::vec3>;

    export using Tangent         = VertexAttribute<glm::vec3>;
    export using PackedTangent   = VertexAttribute<glm::vec4>; // xyz + handedness in w
    export using Bitangent       = VertexAttribute<glm::vec3>;

    export using BoneIds     = VertexAttribute<glm::ivec4>;
    export using BoneWeights = VertexAttribute<glm::vec4>;
}

