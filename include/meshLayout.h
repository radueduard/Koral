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
    concept VertexAttributeType = requires {
        typename T::ValueType;
    } && std::is_base_of_v<VertexAttribute<typename T::ValueType>, T>;

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

    template<typename... Attrs> requires (VertexAttributeType<Attrs> && ...)
    struct ParamVertex
    {
        using Attributes = std::tuple<Attrs...>;
        using Storage = VertexStorage<typename Attrs::ValueType...>;

        Storage storage{};

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

    // Marker for vertex attributes that carry the vertex position. A mesh records
    // its position attribute (see Mesh::getPositionAttribute) so ray-tracing
    // acceleration structures know which buffer/offset/format to read geometry from.
    struct PositionAttribute {};

    // A vertex stream that exposes enough static reflection to locate its attributes.
    template<typename T>
    concept ReflectableStream = requires {
        typename T::Attributes;
        { T::kAttributeCount } -> std::convertible_to<glm::u32>;
    };

    // Scans a parameter pack of vertex streams (one per binding) and returns the
    // description of the first attribute marked with PositionAttribute, if any.
    // Used by MeshHeap so heap suballocations can back ray-tracing acceleration
    // structures the same way standalone ParamMeshes do.
    template<typename... Streams>
    std::optional<VertexInputAttributeDescription> FindPositionAttribute()
    {
        std::optional<VertexInputAttributeDescription> result = std::nullopt;
        glm::u32 binding = 0;

        ([&]<typename Stream>() {
            if constexpr (ReflectableStream<Stream>) {
                [&]<std::size_t... I>(std::index_sequence<I...>) {
                    ([&]<std::size_t Idx>() {
                        using Attr = std::tuple_element_t<Idx, typename Stream::Attributes>;
                        if constexpr (std::is_base_of_v<PositionAttribute, Attr>) {
                            if (!result.has_value()) {
                                using Value  = typename Attr::ValueType;
                                using Traits = VertexValueTraits<Value>;
                                result = VertexInputAttributeDescription{
                                    .location     = 0,
                                    .binding      = binding,
                                    .channelCount = Traits::channelCount,
                                    .channelType  = Traits::channelType,
                                    .offset       = Stream::template OffsetOf<Idx>()
                                };
                            }
                        }
                    }.template operator()<I>(), ...);
                }(std::make_index_sequence<Stream::kAttributeCount>{});
            }
            ++binding;
        }.template operator()<Streams>(), ...);

        return result;
    }

    // Carries the per-binding vertex stream types as a tuple. Generic code such as
    // Importer::LoadMesh reflects over `MeshT::Streams` to know how many vertex
    // buffers to build and which stream type backs each binding. Kept in a small base
    // so the alias can be named `Streams` without clashing with ParamMesh's own
    // template parameter pack of the same name.
    template<typename... StreamTs>
    struct ParamMeshStreams
    {
        using Streams = std::tuple<StreamTs...>;
    };

    template<typename... Streams>
    class ParamMesh : public CustomMesh<ParamMesh<Streams...>>, public ParamMeshStreams<Streams...>
    {
    public:
        using Self = ParamMesh<Streams...>;
        using Base = CustomMesh<Self>;
        using Builder = typename Base::Builder;

        explicit ParamMesh(Builder& createInfo) : Base(createInfo) {
            DefaultMeshRegistry::TryRegister<Self>(); // first initialized mesh type becomes default
            this->_positionAttribute = _positionAttributeDescription;
        }

        static void DefineMesh()
        {
            Base::_vertexBindingDescription.clear();
            Base::_vertexAttributeDescription.clear();
            _positionAttributeDescription = std::nullopt;

            Base::_vertexBindingDescription.reserve(sizeof...(Streams));
            Base::_vertexAttributeDescription.reserve((0u + ... + Streams::kAttributeCount));

            glm::u32 location = 0;
            glm::u32 binding = 0;
            (appendStream<Streams>(binding++, location), ...);
        }

    static gfx::Resource<Self> Create(std::span<const Streams>... streams)
    {
        Builder builder;
        setVertexBuffers(builder, std::tuple<std::span<const Streams>...>{streams...}, std::index_sequence_for<Streams...>{});
        return builder.Build();
    }

    template<typename IndexT> requires std::is_integral_v<IndexT>
    static gfx::Resource<Self> Create(std::span<const Streams>... streams, std::span<const IndexT> indices)
    {
        Builder builder;
        setVertexBuffers(builder, std::tuple<std::span<const Streams>...>{streams...}, std::index_sequence_for<Streams...>{});

        if (!indices.empty())
        {
            auto indexBuffer = Mesh::makeBuffer(indices, Buffer::Usage::eIndex);
            builder.SetIndexBuffer(std::move(indexBuffer), indexChannelType<IndexT>());
        }

        return builder.Build();
    }

    // Convenience: vectors
    static gfx::Resource<Self> Create(const std::vector<Streams>&... streams)
    {
        return Create(std::span<const Streams>(streams)...);
    }

    template<typename IndexT>
    static gfx::Resource<Self> Create(const std::vector<Streams>&... streams, const std::vector<IndexT>& indices)
    {
        return Create(std::span<const Streams>(streams)..., std::span<const IndexT>(indices));
    }

    // Convenience: arrays
    template<std::size_t... N>
    static gfx::Resource<Self> Create(const std::array<Streams, N>&... streams)
    {
        return Create(std::span<const Streams>(streams)...);
    }

    template<typename IndexT, std::size_t... N, std::size_t NI>
    static gfx::Resource<Self> Create(const std::array<Streams, N>&... streams, const std::array<IndexT, NI>& indices)
    {
        return Create(std::span<const Streams>(streams)..., std::span<const IndexT>(indices));
    }

private:
    template<typename Tuple, std::size_t... I>
    static void setVertexBuffers(Builder& builder, const Tuple& streamTuple, std::index_sequence<I...>)
    {
        (builder.SetVertexBuffer(static_cast<glm::u32>(I),
            Mesh::makeBuffer(std::get<I>(streamTuple), Buffer::Usage::eVertex)), ...);
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

            const auto attribute = VertexInputAttributeDescription{
                .location = location++,
                .binding = binding,
                .channelCount = Traits::channelCount,
                .channelType = Traits::channelType,
                .offset = Stream::template OffsetOf<I>()
            };
            Base::_vertexAttributeDescription.push_back(attribute);

            // Record the position attribute so acceleration structures can locate geometry.
            if constexpr (std::is_base_of_v<PositionAttribute, Attr>) {
                _positionAttributeDescription = attribute;
            }
        }

        inline static std::optional<VertexInputAttributeDescription> _positionAttributeDescription = std::nullopt;
    };

    struct Position2  : VertexAttribute<glm::vec2>, PositionAttribute {};
    struct Position   : VertexAttribute<glm::vec3>, PositionAttribute {};
    struct Position4  : VertexAttribute<glm::vec4>, PositionAttribute {};

    struct Normal     : VertexAttribute<glm::vec3> {};
    struct Normal4    : VertexAttribute<glm::vec4> {};

    struct Color3     : VertexAttribute<glm::vec3> {};
    struct Color      : VertexAttribute<glm::vec4> {};

    struct UV         : VertexAttribute<glm::vec2> {};
    struct UV3        : VertexAttribute<glm::vec3> {};

    struct Tangent    : VertexAttribute<glm::vec3> {};
    struct PackedTangent : VertexAttribute<glm::vec4> {}; // xyz + handedness in w
    struct Bitangent  : VertexAttribute<glm::vec3> {};

    using BoneIds     = VertexAttribute<glm::ivec4>;
    using BoneWeights = VertexAttribute<glm::vec4>;

    // Wraps any VertexAttributeType with a channel index.
    // Use this to have multiple attributes of the same type in one vertex,
    // e.g. two UV sets: IndexedAttribute<UV, 0>, IndexedAttribute<UV, 1>.
    template<VertexAttributeType Attr, std::size_t Channel>
    struct IndexedAttribute : Attr
    {
        static constexpr std::size_t kChannel = Channel;
    };
}

