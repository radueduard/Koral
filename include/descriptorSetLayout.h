//
// Created by radue on 3/4/2026.
//

#pragma once
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

#include "structs.h"

#include "api.h"
#include "error.h"
#include <source_location>

#include "builder.h"
#include "shader.h"

namespace kor
{
    /**
     * @brief The shape of a descriptor set: what kind of resource sits at each binding.
     *
     * Derived from the shader's own reflection when a pipeline is built, so a project rarely
     * constructs one — DescriptorSet::Builder takes the pipeline and the set index and finds the
     * layout itself. Build one by hand only for a set that no pipeline owns.
     */
    class KORAL_API DescriptorSetLayout
    {
    public:
        /**
         * @brief What one binding is, plus what the shader does with it.
         *
         * The type and count are the binding's *interface* and decide layout identity; the access,
         * stages and active flag are what the automatic barrier resolver needs in order to
         * synchronise the bound resource, and deliberately take no part in that identity.
         */
        struct KORAL_API Binding {
            DescriptorType type;                                        ///< What kind of resource belongs here.
            glm::u32 count = 1;                                         ///< How many, for an array binding.
            Shader::AccessKind access = Shader::AccessKind::eRead;      ///< Whether shaders read it, write it, or both.
            Flags<Shader::Stage> stages;                                ///< Which shader stages reach it.
            bool active = true;                                         ///< Whether the entry point actually uses it; an unused binding needs no synchronisation.

            /// The block's fields, for a buffer binding. What lets a semantic-filled descriptor
            /// know the shape the shader asked for. Outside matches(), for the same reason the
            /// rest of the non-interface state is. @see semantics.h
            std::vector<Shader::BlockMember> members;
            glm::u32 blockSize = 0;

            /// What the shader calls this binding, so a set can be written by name rather than by
            /// number. @see Shader::Descriptor::name
            std::string name;

            /// The block type's name, when the binding is a block declared without an instance
            /// name. Either this or @ref name finds the binding. @see Shader::Descriptor::blockName
            std::string blockName;

            /// How the shader shaped this image binding — which is what an Image bound directly is
            /// turned into a view by, since the image alone cannot always say. Outside the
            /// interface comparison below, like the names. @see Shader::ImageShape
            Shader::ImageShape shape = Shader::ImageShape::eUnknown;

            /** @brief Whether @p wanted names this binding, under either of the names it has. */
            [[nodiscard]] bool namedBy(const std::string_view wanted) const {
                return (!name.empty() && name == wanted) || (!blockName.empty() && blockName == wanted);
            }

            /**
             * @brief Whether two bindings present the same interface.
             *
             * Interface only, on purpose. matches() is what decides whether a shader reload can
             * keep the existing layout object, so letting access, stages or the active flag in here
             * would rebuild the layout — and expire every descriptor set holding it — over an edit
             * that merely made a storage buffer read-only. The names are out for the same reason
             * @ref members is: renaming a binding changes nothing the GPU can see, and rebuilding
             * the layout over it would orphan every set written against the old one.
             */
            bool operator==(const Binding& other) const {
                return type == other.type && count == other.count;
            }
        };

        /** @brief Describes a layout binding by binding. */
        class KORAL_API Builder : public ::Builder
        {
            friend class DescriptorSetLayout;
        public:
            /**
             * @brief Declares one binding.
             * @param binding The binding number the shader uses.
             * @param type What kind of resource belongs there.
             * @param count How many, for an array binding.
             * @param access Whether shaders read it, write it, or both.
             * @param stages Which shader stages reach it.
             * @param active Whether the entry point actually uses it.
             */
            Builder& addBinding(glm::u32 binding, DescriptorType type, glm::u32 count = 1,
                                Shader::AccessKind access = Shader::AccessKind::eRead,
                                Flags<Shader::Stage> stages = {}, bool active = true);

            /**
             * @brief Declares one binding from a fully described @ref Binding.
             *
             * What reflection uses, since it has every field to give — the names included, which is
             * what lets a descriptor set be written by name. A hand-written layout is usually
             * better served by the overload above, and may name a binding by filling in
             * Binding::name if it wants to write to it by name too.
             */
            Builder& addBinding(glm::u32 binding, Binding description);

            /**
             * @brief Declares a buffer binding along with the block's fields.
             *
             * Only reflection calls this — the members come out of the compiled shader, and a
             * hand-written layout has none to give. @see semantics.h
             */
            Builder& addBlockBinding(glm::u32 binding, DescriptorType type, glm::u32 count,
                                     Shader::AccessKind access, Flags<Shader::Stage> stages, bool active,
                                     std::vector<Shader::BlockMember> members, glm::u32 blockSize);

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<DescriptorSetLayout>> create() const;

            /** @brief Creates the layout, poisoned rather than thrown if a binding is contradictory. */
            [[nodiscard]] kor::Resource<DescriptorSetLayout> build(std::source_location where = std::source_location::current()) const;
        private:
            std::map<glm::u32, Binding> _bindings;
            std::optional<Error> _error;
        };

        explicit DescriptorSetLayout(const Builder &builder);
        virtual ~DescriptorSetLayout() = default;

        DescriptorSetLayout(const DescriptorSetLayout&) = delete;
        DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;

        /**
         * @brief The bindings, as (binding number, type, count) triples.
         * @return One entry per declared binding, in binding order.
         */
        [[nodiscard]] std::vector<std::tuple<glm::u32, DescriptorType, glm::u32>> getBindings() const;

        /** @brief Every binding, in full — including the block fields reflection found. */
        [[nodiscard]] const std::map<glm::u32, Binding>& bindings() const { return _bindings; }

        /** @brief What kind of resource belongs at @p binding. */
        [[nodiscard]] DescriptorType getBindingType(glm::u32 binding) const;

        /**
         * @brief The number of the binding the shader calls @p name.
         * @return The binding number, or nullopt when no binding of this set has that name.
         *
         * Matches either name a binding has: the variable's, and — for a block declared without an
         * instance name — the block type's. @see Binding::namedBy
         */
        [[nodiscard]] std::optional<glm::u32> findBinding(std::string_view name) const;

        /**
         * @brief Every name this set's bindings answer to, in binding order, for a diagnostic.
         *
         * What a "no such binding" message lists, so the fix is visible without going back to the
         * shader to find out what the binding is actually called.
         */
        [[nodiscard]] std::vector<std::string> bindingNames() const;

        /** @brief Full per-binding description, including what the shader does with it. */
        [[nodiscard]] const std::map<glm::u32, Binding>& getBindingDescriptions() const { return _bindings; }

        /**
         * @brief Whether @p builder describes exactly this layout.
         *
         * Lets Pipeline::buildLayouts keep an existing layout object when a shader reload did not
         * change the set's interface — which keeps every descriptor set built from it alive.
         */
        [[nodiscard]] bool matches(const Builder& builder) const;

        /**
         * @brief Adopts @p builder's block descriptions, keeping this layout's identity.
         * @return Whether anything actually changed.
         *
         * The other half of @ref matches. A shader edit that adds a field to a uniform block leaves
         * the *interface* identical — same binding, same descriptor type — so the Vulkan object and
         * every descriptor set holding it stay valid, and rebuilding would needlessly dangle them.
         * But the block's fields are exactly what a semantic-filled binding is built from, so they
         * have to be brought up to date here. The caller announces the change (Resource::markChanged)
         * so that the sets built against it rebuild themselves.
         */
        bool refreshBlocks(const Builder& builder);

    protected:

        std::map<glm::u32, Binding> _bindings;
    };
}
