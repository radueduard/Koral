//
// Created by radue on 3/4/2026.
//

#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <glm/fwd.hpp>

#include "descriptorSetLayout.h"
#include "resource.h"
#include "descriptor.h"
#include "api.h"
#include <source_location>

#include <typeinfo>
#include <type_traits>

#include "builder.h"
#include "error.h"
#include <optional>

namespace kor
{
    class CommandBuffer;
    class Descriptor;
    class SemanticSerializer;
    class Pipeline;

    /**
     * @brief The resources bound to one set index of a shader: its textures, buffers and samplers.
     *
     * A shader declares its resources in numbered sets (`set = 0`, `space0`), and a descriptor set
     * fills one of them. Build it against the pipeline that will use it, so the layout comes from
     * the shader's own reflection rather than being restated by hand:
     *
     * @code
     * kor::DescriptorSet::Builder builder(pipeline, 0);
     * auto frameSet = builder
     *     .write(0, kor::Descriptor(cameraBuffer))
     *     .write(1, kor::Descriptor(albedoView, sampler))
     *     .build();
     *
     * commandBuffer.BindDescriptorSet(0, frameSet);
     * @endcode
     *
     * Grouping matters for performance: put what changes per frame in one set and what changes per
     * material in another, and bind the first once instead of per draw. What a bound set holds is
     * also how the automatic barriers know what a draw is about to read, so a resource reached
     * through one needs no explicit barrier.
     */
    class KORAL_API DescriptorSet
    {
    public:
        /** @brief Collects the resources to bind at each binding of one set. */
        struct KORAL_API Builder : ::Builder
        {
            /**
             * @brief Builds against a set of a pipeline — the preferred form.
             * @param pipeline The pipeline whose shader declares the set.
             * @param setIndex Which set index of it to fill.
             *
             * Taking the pipeline rather than its layout means an unusable pipeline poisons this
             * descriptor set instead of being dereferenced, and the layout is tracked, so a shader
             * reload that replaces it cannot leave this set pointing at freed memory.
             */
            Builder(ResourceRef<const Pipeline> pipeline, glm::u32 setIndex);

            /** @brief Builds against a standalone layout, one not owned by a pipeline. */
            explicit Builder(ResourceRef<const DescriptorSetLayout> layout);

            /**
             * @brief Builds against a layout held by raw reference.
             *
             * Kept for callers that already hold one that way. Prefer either overload above, which
             * can track the layout's lifetime.
             */
            explicit Builder(const DescriptorSetLayout& layout);

            /**
             * @brief Kept, and replayed when the layout it was built against is reshaped.
             *
             * A descriptor set is the one resource whose *contents* are decided by something else:
             * the layout says how big each block is and which semantic fills each field, and a
             * shader edit can change both without changing the binding. So unlike a pipeline — which
             * reloads in place, keeping its identity — a set has to be built again from the same
             * configuration. That is what these two say, and why the builder is small enough to
             * keep: it holds lifetime-tracked references, never the data behind them.
             */
            static constexpr bool Recoverable = true;
            static constexpr bool RebuildsOnInputChange = true;

            /// The pipeline this set belongs to, when built from one. Kept rather than only its
            /// layout, because a reload may *replace* the layout and the new one is asked for here.
            ResourceRef<const Pipeline> pipeline;
            glm::u32 setIndex = 0;

            /// The layout being filled. Re-resolved from @ref pipeline on every attempt.
            mutable ResourceRef<const DescriptorSetLayout> layout;

            /// What goes at each binding; the vector is for array bindings. Mutable because a
            /// semantic binding's buffer is resolved per attempt — @see semanticWrites.
            mutable std::map<glm::u32, std::vector<Descriptor>> writes;

            /**
             * @brief The bindings filled from a semantic serializer, resolved on every attempt.
             *
             * Not resolved once at write() time: which buffer answers for a binding depends on the
             * shape of the block, and the whole point of rebuilding is that the shape changed. The
             * resolver holds the resource reference the caller passed, so an object destroyed in the
             * meantime is reported rather than dereferenced.
             */
            struct SemanticWrite
            {
                std::function<SemanticSerializer*()> resolve;   ///< Null return: gone or unusable.
                std::string what;                               ///< What it was, for the message.
            };
            std::map<glm::u32, SemanticWrite> semanticWrites;

            /**
             * @brief Puts a resource at one binding.
             * @param binding The binding number the shader declares.
             * @param descriptor What to bind there; its kind must match what the binding expects.
             * @param index Which element, for an array binding. Zero otherwise.
             */
            Builder& write(glm::u32 binding, const Descriptor& descriptor, glm::u32 index = 0);

            /**
             * @brief Fills a binding from a resource that can answer for the semantics its block
             *        declares — a camera, a light, anything implementing kor::SemanticSerializer.
             *
             * @code
             * .write(0, _camera)
             * @endcode
             *
             * The buffer is created here, laid out exactly as the shader declared the block, and
             * owned by @p object so a second shader wanting different fields gets its own. Fields
             * with no annotation are left alone; a semantic on a field of the wrong type, one
             * belonging to a different module, or one @p object does not answer for, poisons the
             * set with a message naming the field — those are mistakes in the shader, so editing
             * the shader brings the set back.
             *
             * Takes the resource rather than a kor::SemanticSerializer&, and asks at runtime what
             * it can do: a project passes what it holds, exactly as it does for every other
             * binding, and being told "a Mesh cannot fill a semantic block" beats a compile error
             * about an unrelated base class.
             */
            template<typename T>
            Builder& write(const glm::u32 binding, const ResourceRef<T>& object)
            {
                if (!object.alive())
                    return rejectSemantic(binding, "<destroyed>", true);
                if (!object.valid())
                    return rejectSemantic(binding, object.name().c_str(), true);

                // const_cast: filling a block does not change what the object *is*, but it does
                // hand out storage the object owns, and SemanticBuffers is not const. Every other
                // route to a resource is const-agnostic in the same way.
                auto& mutableObject = const_cast<std::remove_const_t<T>&>(*object);
                if (!dynamic_cast<SemanticSerializer*>(&mutableObject))
                    return rejectSemantic(binding, typeid(std::remove_const_t<T>).name(), false);

                // Recorded rather than resolved. The ref is captured by value, so it is the object's
                // own lifetime that decides whether a later attempt can still read it.
                return recordSemantic(binding, [object]() -> SemanticSerializer* {
                    if (!object.valid()) return nullptr;
                    return dynamic_cast<SemanticSerializer*>(
                        const_cast<std::remove_const_t<T>*>(object.get()));
                }, object.name());
            }

            /** @brief The same, from an owning resource. @see write(glm::u32, const ResourceRef<T>&) */
            template<typename T>
            Builder& write(const glm::u32 binding, const Resource<T>& object)
            {
                return write(binding, ResourceRef<const T>(object));
            }

            /**
             * @brief The semantic half of write(), for a serializer held by raw reference.
             *
             * Prefer write(binding, resource): a reference cannot be lifetime-tracked, so if this
             * set is rebuilt after the object is gone, the resolver has nothing to check.
             */
            Builder& writeSemantic(glm::u32 binding, SemanticSerializer& serializer);

            /** @brief Records a binding to be filled from whatever @p resolve yields, per attempt. */
            Builder& recordSemantic(glm::u32 binding, std::function<SemanticSerializer*()> resolve,
                                    std::string what);

            /**
             * @brief Records why a resource could not fill a semantic block.
             * @param unusable Whether it was destroyed or poisoned, rather than simply the wrong
             *        kind of thing — two different mistakes that deserve different sentences.
             */
            Builder& rejectSemantic(glm::u32 binding, const char* what, bool unusable);

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<DescriptorSet>> create() const;

            /**
             * @brief Allocates the set and writes the resources into it.
             * @return The set as a Resource; poisoned rather than thrown if a descriptor is invalid
             *         or does not match the binding it was written to.
             */
            [[nodiscard]] kor::Resource<DescriptorSet> build(std::source_location where = std::source_location::current()) const;

        private:
            void initWrites();

            /** @brief Re-reads the layout and fills every semantic binding for this attempt. */
            [[nodiscard]] VoidResult resolve() const;

            mutable std::optional<Error> _error;
        };

        virtual ~DescriptorSet() = default;

        /** @brief Binds the set at @p index. Called by the backend; a scene uses CommandBuffer::BindDescriptorSet. */
        virtual void bind(const CommandBuffer& commandBuffer, glm::u32 index) const {};

        /**
         * @brief Replaces what is bound at one binding, after the set was built.
         * @param binding The binding number.
         * @param descriptor The new resource.
         * @param index Which element, for an array binding.
         *
         * @warning Takes effect immediately, including for work already recorded but not yet
         *          submitted. Rewriting a set the GPU may still be reading is a race; give
         *          per-frame data a per-frame buffer, or a set per frame in flight.
         */
        virtual void Write(glm::u32 binding, const Descriptor& descriptor, glm::u32 index) = 0;

        /** @brief Logs what the set currently holds, binding by binding. A debugging aid. */
        virtual void DebugPrint() const {};

        /** @brief The layout this set was allocated against — what each binding is, and what shaders do with it. */
        [[nodiscard]] ResourceRef<const DescriptorSetLayout> getLayout() const { return _layout; }

        /**
         * @brief What is currently written into the set, by binding.
         * @return The descriptors at each binding; the vector holds one entry per element of an
         *         array binding. Together with the layout, this is what tells the barrier resolver
         *         which resources a draw will touch.
         */
        [[nodiscard]] const std::map<glm::u32, std::vector<Descriptor>>& getWrites() const { return _writes; }

    protected:
        explicit DescriptorSet(const Builder &builder);
        bool _isPerFrame = false;
        // A ref, not a reference: a shader reload can replace the pipeline's layouts, and a raw
        // reference into that map would be silently left dangling. See Pipeline::buildLayouts.
        kor::ResourceRef<const kor::DescriptorSetLayout> _layout;
        std::map<glm::u32, std::vector<Descriptor>> _writes;
    };
}
