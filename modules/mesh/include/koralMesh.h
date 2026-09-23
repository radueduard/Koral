//
// Created by radue on 29.07.2026.
//

/**
 * @file koralMesh.h
 * @brief The mesh module's public face: vertex formats written as a list of attributes.
 *
 * A vertex format is declared by naming what a vertex holds, not by writing a struct and a table of
 * offsets beside it:
 *
 * @code
 * // CMakeLists.txt:  target_link_libraries(MyScene PRIVATE Koral::Koral Koral::koral-mesh)
 *
 * using Vertex = kmesh::ParamVertex<kmesh::Position, kmesh::Normal, kmesh::UV>;
 * using MyMesh = kmesh::ParamMesh<Vertex>;
 *
 * auto mesh = MyMesh::Create(vertices, indices);
 * pipelineBuilder.setVertexShader(vertexShader, MyMesh::Layout());
 * @endcode
 *
 * The binding stride, each attribute's offset, its channel type and count are all computed from the
 * types, so the pipeline and the buffers cannot disagree — and adding an attribute cannot leave a
 * stale offset behind. Storage is laid out to std430 alignment, matching what a shader expects.
 *
 * @section mesh_semantics How a shader is matched to a format
 *
 * Every built-in attribute carries a *semantic*: `POSITION`, `NORMAL`, `UV`. A vertex shader that
 * names the same semantics on its inputs is fed the right bytes whatever order it declares them in,
 * and asking for one the format does not carry is an error naming both sides rather than a
 * misread buffer:
 *
 * @code{.glsl}
 * #pragma mesh(POSITION)
 * layout(location = 0) in vec3 position;
 * #pragma mesh(UV)
 * layout(location = 1) in vec2 uv;
 * @endcode
 *
 * @code{.slang}
 * float4 vertexMain(float3 position : POSITION, float2 uv : UV) : SV_Position { ... }
 * @endcode
 *
 * The vocabulary below is this module's, not the engine's — the engine holds only the matching.
 * A shader that annotates nothing is matched by declaration order, exactly as before semantics
 * existed. @see kor::VertexLayout
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <mesh.h>
#include <resource.h>
#include <structs.h>
#include <vertexLayout.h>

namespace kmesh
{
    /** @brief This module's identity, for another module that declares a dependency on it. */
    inline constexpr std::string_view ModuleId = "koral.mesh";
    inline constexpr std::uint32_t    ModuleVersion = 1;

    /**
     * @brief What a vertex shader can ask a vertex for, written `mesh(NAME)`.
     *
     * The vocabulary belongs to this module: `mesh` is the name the annotation uses, and these are
     * the semantics that name answers for. A format built from attributes of its own is free to
     * invent more — a semantic is a string, and nothing has to be registered.
     */
    namespace semantics
    {
        /** @brief The name a shader annotates with: the `mesh` of `mesh(POSITION)`. */
        inline constexpr std::string_view Namespace = "mesh";

        inline constexpr std::string_view Position    = "POSITION";
        inline constexpr std::string_view Normal      = "NORMAL";
        inline constexpr std::string_view Color       = "COLOR";
        inline constexpr std::string_view UV          = "UV";
        inline constexpr std::string_view Tangent     = "TANGENT";
        inline constexpr std::string_view Bitangent   = "BITANGENT";
        inline constexpr std::string_view BoneIds     = "BONE_IDS";
        inline constexpr std::string_view BoneWeights = "BONE_WEIGHTS";
    }

    /**
     * @brief Base for a named vertex attribute of type @p T.
     *
     * Deriving from it is what makes an attribute type: `struct Normal : VertexAttribute<glm::vec3>`.
     * The name is what distinguishes two attributes that store the same thing; adding a
     * `Semantic` is what lets a shader ask for it by name.
     */
    template<typename T>
    struct VertexAttribute
    {
        using ValueType = T;    ///< What the attribute stores.
    };

    /** @brief Satisfied by anything derived from VertexAttribute. */
    template<typename T>
    concept VertexAttributeType = requires {
        typename T::ValueType;
    } && std::is_base_of_v<VertexAttribute<typename T::ValueType>, T>;

    /** @brief Maps a scalar C++ type onto the ChannelType that describes it. */
    template<typename T>
    struct ScalarChannelTraits;

    template<> struct ScalarChannelTraits<float>         { static constexpr kor::ChannelType channelType = kor::ChannelType::eFloat;  };
    template<> struct ScalarChannelTraits<double>        { static constexpr kor::ChannelType channelType = kor::ChannelType::eDouble; };
    template<> struct ScalarChannelTraits<int>           { static constexpr kor::ChannelType channelType = kor::ChannelType::eInt;    };
    template<> struct ScalarChannelTraits<unsigned int>  { static constexpr kor::ChannelType channelType = kor::ChannelType::eUInt;   };
    template<> struct ScalarChannelTraits<short>         { static constexpr kor::ChannelType channelType = kor::ChannelType::eShort;  };
    template<> struct ScalarChannelTraits<unsigned short>{ static constexpr kor::ChannelType channelType = kor::ChannelType::eUShort; };
    template<> struct ScalarChannelTraits<signed char>   { static constexpr kor::ChannelType channelType = kor::ChannelType::eByte;   };
    template<> struct ScalarChannelTraits<unsigned char> { static constexpr kor::ChannelType channelType = kor::ChannelType::eUByte;  };

    /** @brief Channel count and channel type of an attribute value — 3 and eFloat for a glm::vec3. */
    template<typename T>
    struct VertexValueTraits
    {
        static constexpr glm::u32 channelCount = 1;
        static constexpr kor::ChannelType channelType = ScalarChannelTraits<std::remove_cv_t<T>>::channelType;
    };

    template<glm::length_t L, typename T, glm::qualifier Q>
    struct VertexValueTraits<glm::vec<L, T, Q>>
    {
        static constexpr glm::u32 channelCount = static_cast<glm::u32>(L);
        static constexpr kor::ChannelType channelType = ScalarChannelTraits<std::remove_cv_t<T>>::channelType;
    };

    /** @brief The std430 alignment an attribute value needs, so C++ storage matches what a shader reads. */
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

    /** @brief Storage for a vertex's attributes, aligned to std430 and constructible from a flat list of values. */
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

    /**
     * @brief A vertex type built from a list of attributes.
     * @tparam Attrs The attributes, in the order they are stored.
     *
     * Construct one from its values in order — `Vertex{position, normal, uv}` — and read them back
     * with get<I>(). The stride and each attribute's offset come from the real memory layout, so
     * alignment padding is accounted for rather than assumed away.
     */
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

        static constexpr glm::u32 AttributeCount = static_cast<glm::u32>(sizeof...(Attrs));  ///< How many attributes this vertex has.
        static constexpr glm::u32 Stride = static_cast<glm::u32>(sizeof(Storage));           ///< Bytes from one vertex to the next.

        /** @brief Byte offset of attribute @p I, measured from the real memory layout so alignment is included. */
        template<std::size_t I>
        static glm::u32 OffsetOf()
        {
            ParamVertex v{};
            const auto* base = reinterpret_cast<const unsigned char*>(&v.storage);
            const auto* elem = reinterpret_cast<const unsigned char*>(&v.template get<I>());
            return static_cast<glm::u32>(elem - base);
        }
    };

    /**
     * @brief Marks an attribute as carrying the vertex position.
     *
     * Only needed by a format whose position is *not* its first attribute: a layout that says
     * nothing is read position-first, which is how a vertex is almost always written. What it
     * decides is where a ray-tracing acceleration structure reads geometry from.
     * @see kor::VertexLayout::positionAttribute
     */
    struct PositionAttribute {};

    /** @brief A vertex stream that exposes enough static reflection to locate its attributes. */
    template<typename T>
    concept ReflectableStream = requires {
        typename T::Attributes;
        { T::AttributeCount } -> std::convertible_to<glm::u32>;
    };

    /**
     * @brief What a shader asks for to be given this attribute, or empty if it answers for nothing.
     *
     * An attribute declares its own semantic as `Semantic`; specialise this instead when the
     * semantic has to be computed, as an indexed attribute's is.
     */
    template<typename Attr>
    struct AttributeSemanticTraits
    {
        static std::string semantic()
        {
            if constexpr (requires { Attr::Semantic; }) return std::string(Attr::Semantic);
            else return {};
        }
    };

    /**
     * @brief Wraps an attribute with a channel index, so one vertex can carry several of a kind.
     * @tparam Attr The attribute being indexed.
     * @tparam Index Which one it is, readable back as @ref Channel.
     *
     * Two UV sets, for instance: `IndexedAttribute<UV, 0>` and `IndexedAttribute<UV, 1>`, which a
     * shader asks for as `mesh(UV0)` and `mesh(UV1)`.
     */
    template<VertexAttributeType Attr, std::size_t Index>
    struct IndexedAttribute : Attr
    {
        /// Which channel this is. The template parameter is named apart from it so the member does
        /// not shadow it.
        static constexpr std::size_t Channel = Index;
    };

    template<VertexAttributeType Attr, std::size_t Channel>
    struct AttributeSemanticTraits<IndexedAttribute<Attr, Channel>>
    {
        static std::string semantic()
        {
            auto base = AttributeSemanticTraits<Attr>::semantic();
            if (base.empty()) return base;
            return base + std::to_string(Channel);
        }
    };

    namespace detail
    {
        /** @brief Appends one stream's binding and attributes to a layout being built. */
        template<typename Stream>
        void appendStream(kor::VertexLayout& layout, const glm::u32 binding)
        {
            if constexpr (ReflectableStream<Stream>) {
                layout.bindings.push_back(kor::VertexInputBindingDescription{
                    .binding = binding,
                    .stride  = Stream::Stride,
                });

                [&]<std::size_t... I>(std::index_sequence<I...>) {
                    ([&]<std::size_t Idx>() {
                        using Attr = std::tuple_element_t<Idx, typename Stream::Attributes>;
                        using Traits = VertexValueTraits<typename Attr::ValueType>;

                        // The position attribute is recorded only when a format says it is not the
                        // first one; saying so twice would be the format contradicting itself, so
                        // the first marked attribute wins.
                        if constexpr (std::is_base_of_v<PositionAttribute, Attr>) {
                            if (!layout.positionAttribute.has_value())
                                layout.positionAttribute = layout.attributes.size();
                        }

                        layout.attributes.push_back(kor::VertexLayout::Attribute{
                            .semantic          = AttributeSemanticTraits<Attr>::semantic(),
                            .semanticNamespace = std::string(semantics::Namespace),
                            .binding           = binding,
                            .offset            = Stream::template OffsetOf<Idx>(),
                            .channelType       = Traits::channelType,
                            .channelCount      = Traits::channelCount,
                        });
                    }.template operator()<I>(), ...);
                }(std::make_index_sequence<Stream::AttributeCount>{});
            } else {
                // A stream of bare values — a heap of glm::vec3 positions, say. It has one
                // attribute and no name for it, so it can only be matched by declaration order.
                using Traits = VertexValueTraits<Stream>;
                layout.bindings.push_back(kor::VertexInputBindingDescription{
                    .binding = binding,
                    .stride  = static_cast<glm::u32>(sizeof(Stream)),
                });
                layout.attributes.push_back(kor::VertexLayout::Attribute{
                    .binding      = binding,
                    .offset       = 0,
                    .channelType  = Traits::channelType,
                    .channelCount = Traits::channelCount,
                });
            }
        }
    }

    /**
     * @brief Builds the runtime layout of a vertex format from its stream types.
     * @tparam Streams One vertex type per binding.
     *
     * What every format hands the engine: the strides, offsets, channel formats and semantics,
     * with no shader locations in it — those come from the shader when a pipeline is built.
     */
    template<typename... Streams>
    kor::VertexLayout MakeVertexLayout()
    {
        kor::VertexLayout layout;
        layout.bindings.reserve(sizeof...(Streams));

        glm::u32 binding = 0;
        (detail::appendStream<Streams>(layout, binding++), ...);
        return layout;
    }

    /**
     * @brief What a class must provide to be used as the vertex format of a CustomMesh.
     *
     * A conforming type declares DefineMesh(), which describes the format once, and Layout(), which
     * hands the description back. CustomMesh supplies the second; a format writes the first.
     */
    template<typename Derived>
    concept MeshType = requires {
        Derived::DefineMesh();
        Derived::Layout();
    };

    /**
     * @brief A mesh whose vertex format is described by the class that derives from it.
     * @tparam Derived The class deriving from this one, which must satisfy the MeshType concept.
     *
     * The curiously recurring pattern: a vertex format declares itself by deriving from
     * CustomMesh<itself> and implementing DefineMesh(), which is called once to fill in the layout
     * a graphics pipeline is matched against. The layout is static, shared by every mesh of that
     * format.
     *
     * @code
     * class Vertex : public kmesh::CustomMesh<Vertex> {
     * public:
     *     static void DefineMesh() { _layout = kmesh::MakeVertexLayout<MyStream>(); }
     *     explicit Vertex(Builder& b) : CustomMesh(b) {}
     * };
     * @endcode
     */
    template<typename Derived>
    class CustomMesh : public kor::Mesh
    {
    public:
        /**
         * @brief Describes the vertex format, once per process.
         *
         * Nothing has to call this: every route to the layout defines it first, so a format is
         * ready the first time it is *used* rather than the first time someone remembers to
         * announce it. Kept because saying it explicitly is harmless, and because a format built on
         * a background thread may want the work done at a chosen moment.
         */
        static void Initialize() { EnsureDefined(); }

        /**
         * @brief Collects the buffers a mesh of this format is built from.
         *
         * One vertex buffer per binding the format declares, plus an optional index buffer. The
         * vertex count is derived from the buffers' sizes, and every binding must agree on it.
         */
        struct Builder
        {
            glm::u64 vertexCount = 0;                                   ///< Derived from the buffers; not set directly.
            std::vector<kor::Resource<kor::Buffer>> vertexBuffers {};   ///< One per binding, sized by the format.

            std::optional<glm::u32> indexCount = std::nullopt;          ///< Derived from the index buffer.
            std::optional<kor::Resource<kor::Buffer>> indexBuffer = std::nullopt;   ///< Optional; without it the mesh is drawn non-indexed.
            std::optional<kor::ChannelType> indexType = std::nullopt;   ///< The width of one index.

            /** @brief Prepares a builder. The vertex format defines itself if it has not already. */
            explicit Builder()
            {
                vertexBuffers.resize(Layout().bindings.size());
            }

            /**
             * @brief Sets the vertex buffer for one binding.
             * @param binding Which binding of the vertex format this buffer feeds.
             * @param vertexBuffer The data. Its size divided by the binding's stride gives the vertex count.
             * @throws std::runtime_error if it implies a different vertex count than a buffer already set.
             */
            Builder& SetVertexBuffer(const glm::u32 binding, kor::Resource<kor::Buffer> vertexBuffer) {
                const auto stride = Layout().bindings[binding].stride;
                if (vertexCount == 0)
                    vertexCount = vertexBuffer->size() / stride;
                else if (vertexCount != vertexBuffer->size() / stride)
                    throw std::runtime_error("All vertex buffers must have the same vertex count!");

                vertexBuffers[binding] = std::move(vertexBuffer);
                return *this;
            }

            /**
             * @brief Gives the mesh an index buffer, making it drawable with DrawIndexed.
             * @param indexBuffer The indices.
             * @param indexType The width of one index; the count follows from the buffer's size.
             */
            Builder& SetIndexBuffer(kor::Resource<kor::Buffer> indexBuffer, const kor::ChannelType indexType) {
                this->indexCount = static_cast<glm::u32>(indexBuffer->size() / kor::sizeofChannelType(indexType));
                this->indexBuffer = std::move(indexBuffer);
                this->indexType = indexType;
                return *this;
            }

            /** @brief Creates the mesh, taking ownership of the buffers set above. */
            kor::Resource<Derived> Build()
            {
                return kor::MakeResource<Derived>(*this);
            }
        };

        /** @brief Constructs the mesh from a builder. Prefer Builder::Build(). */
        explicit CustomMesh(Builder& createInfo)
        {
            static_assert(MeshType<Derived>, "Derived class must satisfy MeshType concept!");
            _vertexCount = createInfo.vertexCount;
            // The buffers were created for this mesh alone, so it takes them over: adopting keeps
            // each one alive and appends it in binding order, which is the order they are in here.
            for (auto& vertexBuffer : createInfo.vertexBuffers)
                adoptVertexBuffer(std::move(vertexBuffer));
            if (createInfo.indexBuffer.has_value())
                adoptIndexBuffer(std::move(*createInfo.indexBuffer),
                                 createInfo.indexType.value_or(kor::ChannelType::eUInt));
            _indexCount = createInfo.indexCount;

            setVertexLayout(Layout());
        }

        /** @brief This format's layout: its bindings, and what each attribute holds and is called. */
        [[nodiscard]] static const kor::VertexLayout& Layout()
        {
            EnsureDefined();
            return _layout;
        }

    protected:
        /**
         * @brief Describes the format if it has not been described yet.
         *
         * Every route to the layout goes through here — the accessor above and the builder alike —
         * so there is no order in which a format can be observed empty. Building a pipeline from a
         * mesh type no mesh has been created from yet is the case this exists for.
         */
        static void EnsureDefined()
        {
            if (DefineMeshParent()) {
                Derived::DefineMesh();
                // The first format a program describes is the one a pipeline gets when it is given
                // none. Formats that describe nothing are skipped: they would make the default
                // "no vertex input" for everyone.
                if (!_layout.empty()) kor::VertexLayout::SetDefault(_layout);
            }
        }

        inline static kor::VertexLayout _layout {};

    private:
        static bool DefineMeshParent()
        {
            static bool defined = false;
            if (defined) return false;
            defined = true;
            return true;
        }
    };

    // Carries the per-binding vertex stream types as a tuple. Generic code — the importer bridge,
    // for one — reflects over `MeshT::Streams` to know how many vertex buffers to build and which
    // stream type backs each binding. Kept in a small base so the alias can be named `Streams`
    // without clashing with ParamMesh's own template parameter pack of the same name.
    template<typename... StreamTs>
    struct ParamMeshStreams
    {
        using Streams = std::tuple<StreamTs...>;
    };

    /**
     * @brief A mesh whose vertex layout is derived from its vertex types.
     * @tparam Streams One vertex type per binding — usually a single ParamVertex, or several when
     *         attributes are split into separate buffers.
     *
     * The layout is computed once from the types, so the bindings, attributes, strides and offsets
     * a pipeline needs are never written by hand.
     *
     * @code
     * using Vertex = kmesh::ParamVertex<kmesh::Position, kmesh::UV>;
     * auto mesh = kmesh::ParamMesh<Vertex>::Create(vertices, indices);
     * @endcode
     */
    template<typename... Streams>
    class ParamMesh : public CustomMesh<ParamMesh<Streams...>>, public ParamMeshStreams<Streams...>
    {
    public:
        using Self = ParamMesh<Streams...>;
        using Base = CustomMesh<Self>;
        using Builder = typename Base::Builder;

        explicit ParamMesh(Builder& createInfo) : Base(createInfo) {}

        /** @brief Computes the layout from the stream types. Called once per format. */
        static void DefineMesh()
        {
            Base::_layout = MakeVertexLayout<Streams...>();
        }

        /**
         * @brief Creates a non-indexed mesh from one span per stream.
         * @param streams The vertex data, in binding order. Copied to device-local buffers.
         */
        static kor::Resource<Self> Create(std::span<const Streams>... streams)
        {
            Builder builder;
            setVertexBuffers(builder, std::tuple<std::span<const Streams>...>{streams...}, std::index_sequence_for<Streams...>{});
            return builder.Build();
        }

        /**
         * @brief Creates an indexed mesh from one span per stream plus the indices.
         * @param streams The vertex data, in binding order.
         * @param indices The index data. Any integral type; the channel type follows from it.
         */
        template<typename IndexT> requires std::is_integral_v<IndexT>
        static kor::Resource<Self> Create(std::span<const Streams>... streams, std::span<const IndexT> indices)
        {
            Builder builder;
            setVertexBuffers(builder, std::tuple<std::span<const Streams>...>{streams...}, std::index_sequence_for<Streams...>{});

            if (!indices.empty())
            {
                auto indexBuffer = kor::Mesh::makeBuffer(indices, kor::Buffer::Usage::eIndex);
                builder.SetIndexBuffer(std::move(indexBuffer), indexChannelType<IndexT>());
            }

            return builder.Build();
        }

        /** @brief Creates a non-indexed mesh from vectors. */
        static kor::Resource<Self> Create(const std::vector<Streams>&... streams)
        {
            return Create(std::span<const Streams>(streams)...);
        }

        /** @brief Creates an indexed mesh from vectors. */
        template<typename IndexT>
        static kor::Resource<Self> Create(const std::vector<Streams>&... streams, const std::vector<IndexT>& indices)
        {
            return Create(std::span<const Streams>(streams)..., std::span<const IndexT>(indices));
        }

        /** @brief Creates a non-indexed mesh from arrays. */
        template<std::size_t... N>
        static kor::Resource<Self> Create(const std::array<Streams, N>&... streams)
        {
            return Create(std::span<const Streams>(streams)...);
        }

        /** @brief Creates an indexed mesh from arrays. */
        template<typename IndexT, std::size_t... N, std::size_t NI>
        static kor::Resource<Self> Create(const std::array<Streams, N>&... streams, const std::array<IndexT, NI>& indices)
        {
            return Create(std::span<const Streams>(streams)..., std::span<const IndexT>(indices));
        }

    private:
        template<typename Tuple, std::size_t... I>
        static void setVertexBuffers(Builder& builder, const Tuple& streamTuple, std::index_sequence<I...>)
        {
            (builder.SetVertexBuffer(static_cast<glm::u32>(I),
                kor::Mesh::makeBuffer(std::get<I>(streamTuple), kor::Buffer::Usage::eVertex)), ...);
        }

        template<typename IndexT>
        static consteval kor::ChannelType indexChannelType()
        {
            using T = std::remove_cv_t<IndexT>;

            if constexpr (std::is_same_v<T, std::uint32_t>) return kor::ChannelType::eUInt;
            else if constexpr (std::is_same_v<T, std::int32_t>) return kor::ChannelType::eInt;
            else if constexpr (std::is_same_v<T, std::uint16_t>) return kor::ChannelType::eUShort;
            else if constexpr (std::is_same_v<T, std::int16_t>) return kor::ChannelType::eShort;
            else if constexpr (std::is_same_v<T, std::uint8_t>) return kor::ChannelType::eUByte;
            else if constexpr (std::is_same_v<T, std::int8_t>) return kor::ChannelType::eByte;
            else static_assert(!sizeof(T), "Unsupported index type for ParamMesh::Create");
        }
    };

    /**
     * @brief A mesh format that declares no vertex attributes at all.
     *
     * For draws whose vertices come from somewhere other than a vertex buffer — a full-screen
     * triangle generated from gl_VertexIndex, or geometry a shader reads out of a storage buffer.
     */
    class NullMesh : public CustomMesh<NullMesh>
    {
    public:
        /** @brief Declares nothing: this format has no bindings and no attributes. */
        static void DefineMesh() {}
        explicit NullMesh(Builder& createInfo) : CustomMesh(createInfo) {}
    };

    // ---- Ready-made attributes ----------------------------------------------------------------
    // The vocabulary a vertex is normally written in, and the semantics a shader asks for them by.
    // The Position types carry PositionAttribute, which only matters for a format that does not
    // put its position first.

    struct Position2  : VertexAttribute<glm::vec2>, PositionAttribute { static constexpr std::string_view Semantic = semantics::Position; };   ///< 2D position.
    struct Position   : VertexAttribute<glm::vec3>, PositionAttribute { static constexpr std::string_view Semantic = semantics::Position; };   ///< 3D position. The usual one.
    struct Position4  : VertexAttribute<glm::vec4>, PositionAttribute { static constexpr std::string_view Semantic = semantics::Position; };   ///< Homogeneous position.

    struct Normal     : VertexAttribute<glm::vec3> { static constexpr std::string_view Semantic = semantics::Normal; };    ///< Surface normal.
    struct Normal4    : VertexAttribute<glm::vec4> { static constexpr std::string_view Semantic = semantics::Normal; };    ///< Surface normal with a spare channel.

    struct Color3     : VertexAttribute<glm::vec3> { static constexpr std::string_view Semantic = semantics::Color; };     ///< Vertex colour, no alpha.
    struct Color      : VertexAttribute<glm::vec4> { static constexpr std::string_view Semantic = semantics::Color; };     ///< Vertex colour with alpha.

    struct UV         : VertexAttribute<glm::vec2> { static constexpr std::string_view Semantic = semantics::UV; };        ///< Texture coordinates.
    struct UV3        : VertexAttribute<glm::vec3> { static constexpr std::string_view Semantic = semantics::UV; };        ///< Three-dimensional texture coordinates, for volume or cube lookups.

    struct Tangent    : VertexAttribute<glm::vec3> { static constexpr std::string_view Semantic = semantics::Tangent; };   ///< Surface tangent, for normal mapping.
    struct PackedTangent : VertexAttribute<glm::vec4> { static constexpr std::string_view Semantic = semantics::Tangent; };///< Tangent in xyz, bitangent handedness in w.
    struct Bitangent  : VertexAttribute<glm::vec3> { static constexpr std::string_view Semantic = semantics::Bitangent; }; ///< Surface bitangent.

    struct BoneIds     : VertexAttribute<glm::ivec4> { static constexpr std::string_view Semantic = semantics::BoneIds; };     ///< Skinning: which bones influence this vertex.
    struct BoneWeights : VertexAttribute<glm::vec4>  { static constexpr std::string_view Semantic = semantics::BoneWeights; }; ///< Skinning: how much each of them does.
}
