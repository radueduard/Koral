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
// For Buffer::Slice, which is how a binding is given part of a buffer rather than all of it.
#include "buffer.h"
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
    class Buffer;
    class Image;
    class ImageView;
    class Sampler;
    class AccelerationStructure;
    class BufferView;

    /**
     * @brief Whether a resource type is something a shader binding can hold directly.
     *
     * What tells DescriptorSet::Builder::write which of its two jobs it is being asked to do: bind
     * a resource at a binding, or fill a block from an object that answers for semantics. Declared
     * as a trait rather than a concept so that it costs no includes — the specialisations below
     * name types this header only forward-declares, and a caller pays for the definition of the one
     * type it actually passes.
     */
    template<typename T> struct IsBindableResource : std::false_type {};
    template<> struct IsBindableResource<Buffer> : std::true_type {};
    template<> struct IsBindableResource<Image> : std::true_type {};
    template<> struct IsBindableResource<ImageView> : std::true_type {};
    template<> struct IsBindableResource<Sampler> : std::true_type {};
    template<> struct IsBindableResource<AccelerationStructure> : std::true_type {};
    template<> struct IsBindableResource<BufferView> : std::true_type {};

    /**
     * @brief The resources bound to one set index of a shader: its textures, buffers and samplers.
     *
     * A shader declares its resources in numbered sets (`set = 0`, `space0`), and a descriptor set
     * fills one of them. Build it against the pipeline that will use it, so the layout comes from
     * the shader's own reflection rather than being restated by hand:
     *
     * @code
     * auto frameSet = kor::DescriptorSet::Builder(pipeline, 0)
     *     .write("camera", cameraBuffer)
     *     .write("albedo", albedoView, sampler)
     *     .build();
     *
     * commandBuffer.BindDescriptorSet(0, frameSet);
     * @endcode
     *
     * Bindings may be addressed by the name the shader gives them, as above, or by number. The name
     * is resolved on every build attempt, so a shader edit that moves a binding is followed rather
     * than silently mis-bound; a number is exactly as fast to write and never goes looking.
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

            /**
             * @brief The resolved contents: what goes at each binding, the vector for array
             *        bindings.
             *
             * Derived, not authored. Every attempt clears this and rebuilds it from @ref pending
             * against the layout as it stands *that* time, which is what a rebuild is for: a shader
             * edit can move a binding, change an array's length, or reshape a block, and the answer
             * to "what goes where" is different afterwards.
             */
            mutable std::map<glm::u32, std::vector<Descriptor>> writes;

            /**
             * @brief A binding filled from a semantic serializer rather than from a resource.
             *
             * The resolver holds the resource reference the caller passed, so an object destroyed
             * in the meantime is reported rather than dereferenced.
             */
            struct SemanticWrite
            {
                std::function<SemanticSerializer*()> resolve;   ///< Null return: gone or unusable.
                std::string what;                               ///< What it was, for the message.
            };

            /**
             * @brief One authored write, kept as written until an attempt resolves it.
             *
             * Nothing is resolved at write() time, because none of the answers are stable: a
             * binding addressed by name may sit at a different number after a reload, an array may
             * have grown, and which buffer answers for a semantic block depends on the shape of the
             * block. Keeping what the caller *said* — and working out what it means once per
             * attempt — is what lets a set come back correct after a shader edit rather than
             * correct only for the shader it was first built against.
             */
            struct PendingWrite
            {
                std::optional<glm::u32> binding;    ///< The binding number, when addressed by one.
                std::string name;                   ///< The shader's name for it, otherwise.
                glm::u32 index = 0;                 ///< Which element, for an array binding.

                Descriptor descriptor;                  ///< The resource, for an ordinary write.
                std::optional<SemanticWrite> semantic;  ///< The serializer, for a semantic write.

                /// The image, when the write was given one rather than a view. Deliberately not
                /// turned into a view here: which view a binding wants depends on how the *shader*
                /// declared it, and that is not known until the layout is read, which happens per
                /// attempt. @see kor::ImageShape
                ResourceRef<const Image> image;
                ResourceRef<const Sampler> imageSampler;  ///< The sampler paired with @ref image, if any.
            };

            /// The writes as authored, in the order they were made. @see PendingWrite
            std::vector<PendingWrite> pending;

            /**
             * @brief Puts a resource at one binding.
             * @param binding The binding number the shader declares.
             * @param descriptor What to bind there; its kind must match what the binding expects.
             * @param index Which element, for an array binding. Zero otherwise.
             *
             * The explicit form, and the only one that can bind part of a buffer — everything else
             * is better served by the overloads below, which take the resource itself.
             */
            Builder& write(glm::u32 binding, const Descriptor& descriptor, glm::u32 index = 0);

            /** @brief Puts a resource at the binding the shader calls @p name. @see write(std::string_view, const ResourceRef<const Buffer>&, glm::u32) */
            Builder& write(std::string_view name, const Descriptor& descriptor);

            /**
             * @name Binding a resource
             *
             * One overload per kind of thing a binding can hold, so the resource goes in as itself:
             *
             * @code
             * .write(0, cameraBuffer)               // uniform or storage buffer
             * .write(1, albedoView, linearSampler)  // combined image sampler
             * .write(2, storageView)                // storage image, or a separate sampled image
             * .write(3, linearSampler)              // a sampler on its own
             * .write(4, tlas)                       // acceleration structure
             * @endcode
             *
             * Which kind the binding actually expects is checked against the layout, so binding a
             * buffer where the shader declared a texture poisons the set and says so rather than
             * being written as the wrong kind of descriptor.
             *
             * @param binding The binding number the shader declares.
             * @param index Which element, for an array binding. Zero otherwise.
             */
            ///@{
            Builder& write(glm::u32 binding, const ResourceRef<const Buffer>& buffer, glm::u32 index = 0);
            Builder& write(glm::u32 binding, const Buffer::Slice& slice, glm::u32 index = 0);
            Builder& write(glm::u32 binding, const ResourceRef<const BufferView>& bufferView, glm::u32 index = 0);
            Builder& write(glm::u32 binding, const ResourceRef<const ImageView>& imageView, glm::u32 index = 0);
            Builder& write(glm::u32 binding, const ResourceRef<const ImageView>& imageView,
                           const ResourceRef<const Sampler>& sampler, glm::u32 index = 0);
            Builder& write(glm::u32 binding, const ResourceRef<const Sampler>& sampler, glm::u32 index = 0);
            Builder& write(glm::u32 binding, const ResourceRef<const AccelerationStructure>& accelerationStructure,
                           glm::u32 index = 0);
            ///@}

            /**
             * @name Binding an image, without constructing a view for it
             *
             * @code
             * .write(0, albedoTexture, linearSampler)   // a texture, straight from the Image
             * .write(1, storageImage)                   // a storage image, likewise
             * @endcode
             *
             * Nothing binds an image to a shader directly — a view does — but which view is a
             * question the shader has already answered, so there is no reason to make you answer it
             * again. The binding's declaration says whether it wants a 2D texture, a cube map, an
             * array or a volume, and the view is built to match and owned by the image, so two sets
             * binding the same texture share one. @see Image::view
             *
             * Reach for ImageView yourself when you want less than the whole image: one mip level,
             * one layer of an array, a channel swizzle. This covers the whole-image case, which is
             * nearly all of them.
             *
             * @param binding The binding number the shader declares.
             * @param index Which element, for an array binding. Zero otherwise.
             */
            ///@{
            Builder& write(glm::u32 binding, const ResourceRef<const Image>& image, glm::u32 index = 0);
            Builder& write(glm::u32 binding, const ResourceRef<const Image>& image,
                           const ResourceRef<const Sampler>& sampler, glm::u32 index = 0);
            Builder& write(std::string_view name, const ResourceRef<const Image>& image);
            Builder& write(std::string_view name, const ResourceRef<const Image>& image,
                           const ResourceRef<const Sampler>& sampler);
            ///@}

            /**
             * @name Binding a resource by the name the shader gave it
             *
             * The same, addressed the way the shader reads rather than by a number restated in C++:
             *
             * @code
             * .write("camera", cameraBuffer)
             * .write("albedo", albedoView, linearSampler)
             * .write("textures[3]", detailView, linearSampler)   // one element of an array binding
             * @endcode
             *
             * A trailing `[n]` selects an element of an array binding; without one the write goes to
             * element zero. The name is whatever the shader calls the binding — the variable's name,
             * or, for a block declared without an instance name (`uniform Model { ... };`), the
             * block type's. A name no binding of this set answers to poisons the set with a message
             * listing the names it does have.
             *
             * Names are resolved on every build attempt, not once. A shader edit that moves a
             * binding to a different number therefore costs nothing: the set is rebuilt and finds it
             * where it now is, which a number written in C++ could not do.
             */
            ///@{
            Builder& write(std::string_view name, const ResourceRef<const Buffer>& buffer);
            Builder& write(std::string_view name, const Buffer::Slice& slice);
            Builder& write(std::string_view name, const ResourceRef<const BufferView>& bufferView);
            Builder& write(std::string_view name, const ResourceRef<const ImageView>& imageView);
            Builder& write(std::string_view name, const ResourceRef<const ImageView>& imageView,
                           const ResourceRef<const Sampler>& sampler);
            Builder& write(std::string_view name, const ResourceRef<const Sampler>& sampler);
            Builder& write(std::string_view name, const ResourceRef<const AccelerationStructure>& accelerationStructure);
            ///@}

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
             *
             * Constrained away from the resource kinds a binding can hold directly, and only from
             * those. Without that this template is an *exact* match for any Resource or
             * ResourceRef, so `.write(0, someBuffer)` — the obvious thing to write — bound here,
             * compiled, and failed at runtime complaining that a Buffer cannot fill a semantic
             * block. Anything that is not a bindable resource still arrives here and still gets
             * that answer at runtime, which is the point of the paragraph above.
             */
            template<typename T>
                requires (!IsBindableResource<std::remove_const_t<T>>::value)
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
                requires (!IsBindableResource<std::remove_const_t<T>>::value)
            Builder& write(const glm::u32 binding, const Resource<T>& object)
            {
                return write(binding, ResourceRef<const T>(object));
            }

            /**
             * @brief Fills the block the shader calls @p name from a semantic serializer.
             * @see write(glm::u32, const ResourceRef<T>&) for what filling a block means,
             *      and the named overloads above for how the name is resolved.
             */
            template<typename T>
                requires (!IsBindableResource<std::remove_const_t<T>>::value)
            Builder& write(const std::string_view name, const ResourceRef<T>& object)
            {
                if (!object.alive())
                    return rejectSemantic(name, "<destroyed>", true);
                if (!object.valid())
                    return rejectSemantic(name, object.name().c_str(), true);

                auto& mutableObject = const_cast<std::remove_const_t<T>&>(*object);
                if (!dynamic_cast<SemanticSerializer*>(&mutableObject))
                    return rejectSemantic(name, typeid(std::remove_const_t<T>).name(), false);

                return recordSemantic(name, [object]() -> SemanticSerializer* {
                    if (!object.valid()) return nullptr;
                    return dynamic_cast<SemanticSerializer*>(
                        const_cast<std::remove_const_t<T>*>(object.get()));
                }, object.name());
            }

            /** @brief The same, from an owning resource. @see write(std::string_view, const ResourceRef<T>&) */
            template<typename T>
                requires (!IsBindableResource<std::remove_const_t<T>>::value)
            Builder& write(const std::string_view name, const Resource<T>& object)
            {
                return write(name, ResourceRef<const T>(object));
            }

            /**
             * @brief The semantic half of write(), for a serializer held by raw reference.
             *
             * Prefer write(binding, resource): a reference cannot be lifetime-tracked, so if this
             * set is rebuilt after the object is gone, the resolver has nothing to check.
             */
            Builder& writeSemantic(glm::u32 binding, SemanticSerializer& serializer);

            /** @brief The same, for the block the shader calls @p name. @see writeSemantic(glm::u32, SemanticSerializer&) */
            Builder& writeSemantic(std::string_view name, SemanticSerializer& serializer);

            /** @brief Records a binding to be filled from whatever @p resolve yields, per attempt. */
            Builder& recordSemantic(glm::u32 binding, std::function<SemanticSerializer*()> resolve,
                                    std::string what);

            /** @brief The same, addressing the binding by name. @see recordSemantic(glm::u32, std::function<SemanticSerializer*()>, std::string) */
            Builder& recordSemantic(std::string_view name, std::function<SemanticSerializer*()> resolve,
                                    std::string what);

            /**
             * @brief Records why a resource could not fill a semantic block.
             * @param unusable Whether it was destroyed or poisoned, rather than simply the wrong
             *        kind of thing — two different mistakes that deserve different sentences.
             */
            Builder& rejectSemantic(glm::u32 binding, const char* what, bool unusable);

            /** @brief The same, for a binding named rather than numbered. @see rejectSemantic(glm::u32, const char*, bool) */
            Builder& rejectSemantic(std::string_view name, const char* what, bool unusable);

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<DescriptorSet>> create() const;

            /**
             * @brief Allocates the set and writes the resources into it.
             * @return The set as a Resource; poisoned rather than thrown if a descriptor is invalid
             *         or does not match the binding it was written to.
             */
            [[nodiscard]] kor::Resource<DescriptorSet> build(std::source_location where = std::source_location::current()) const;

        private:
            /** @brief Records one authored write, whatever it addresses and whatever it holds. */
            Builder& record(PendingWrite write);

            /**
             * @brief Re-reads the layout and rebuilds @ref writes from @ref pending for this
             *        attempt.
             *
             * Where every name is looked up, every array slot is sized, every semantic block is
             * filled, and every descriptor is checked against the kind its binding expects — all
             * of it against the layout as it stands now, because all of it can change when a shader
             * is edited.
             */
            [[nodiscard]] VoidResult resolve() const;

            /**
             * @brief Seeds @ref writes with one empty slot per declared binding, for one attempt.
             *
             * Run from resolve() rather than from the constructors, because the layout it reads is
             * the one this attempt found — a reload that added a binding or lengthened an array
             * gives a different shape, and seeding once at construction would keep filling the old
             * one.
             */
            void initWrites() const;

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

        /**
         * @name Rewriting a binding with the resource itself
         *
         * The same overloads the builder takes, for a set that is already built:
         *
         * @code
         * set->Write(0, newCameraBuffer);
         * set->Write("albedo", newView, sampler);
         * @endcode
         *
         * Not virtual — each one is the matching kor::Descriptor and a call to the overload above,
         * so a backend implements one Write and gets all of them. The warning there applies here
         * too: this takes effect immediately, including for work already recorded.
         *
         * @param index Which element, for an array binding. A name may carry `[n]` instead.
         */
        ///@{
        void Write(glm::u32 binding, const ResourceRef<const Buffer>& buffer, glm::u32 index = 0);
        void Write(glm::u32 binding, const Buffer::Slice& slice, glm::u32 index = 0);
        void Write(glm::u32 binding, const ResourceRef<const BufferView>& bufferView, glm::u32 index = 0);
        void Write(glm::u32 binding, const ResourceRef<const ImageView>& imageView, glm::u32 index = 0);
        void Write(glm::u32 binding, const ResourceRef<const ImageView>& imageView,
                   const ResourceRef<const Sampler>& sampler, glm::u32 index = 0);
        void Write(glm::u32 binding, const ResourceRef<const Sampler>& sampler, glm::u32 index = 0);
        void Write(glm::u32 binding, const ResourceRef<const AccelerationStructure>& accelerationStructure,
                   glm::u32 index = 0);

        void Write(std::string_view name, const Descriptor& descriptor);
        void Write(std::string_view name, const ResourceRef<const Buffer>& buffer);
        void Write(std::string_view name, const Buffer::Slice& slice);
        void Write(std::string_view name, const ResourceRef<const BufferView>& bufferView);
        void Write(std::string_view name, const ResourceRef<const ImageView>& imageView);
        void Write(std::string_view name, const ResourceRef<const ImageView>& imageView,
                   const ResourceRef<const Sampler>& sampler);
        void Write(std::string_view name, const ResourceRef<const Sampler>& sampler);
        void Write(std::string_view name, const ResourceRef<const AccelerationStructure>& accelerationStructure);
        ///@}

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

        /**
         * @brief Splits `"textures[3]"` into the name and the element it selects.
         *
         * Shared by the builder and by Write(): both address a binding the same way, so both take
         * the subscript off the name the same way. Anything that is not a well-formed trailing
         * subscript is left as part of the name.
         */
        static std::pair<std::string_view, glm::u32> splitIndex(std::string_view name);

        /**
         * @brief The binding this set's layout calls @p name, for a Write() addressed by name.
         * @return The binding number, or nullopt — after logging why — when there is no such
         *         binding, or no usable layout to ask.
         *
         * Write() returns void and is called on a live set, so a bad name cannot poison anything:
         * it is reported and the write is dropped, rather than throwing through a setter.
         */
        [[nodiscard]] std::optional<glm::u32> resolveWriteTarget(std::string_view name) const;
    };
}
