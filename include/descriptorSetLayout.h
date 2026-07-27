//
// Created by radue on 3/4/2026.
//

#pragma once
#include <map>
#include <memory>
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
    class KORAL_API DescriptorSetLayout
    {
    public:
        // What a binding is, plus what the shader does with it. The type and count are the
        // binding's *interface* and decide layout identity (see matches()); the access, stages
        // and active flag are only what the automatic barrier resolver needs to know to
        // synchronise the bound resource, and deliberately take no part in that identity.
        struct KORAL_API Binding {
            DescriptorType type;
            glm::u32 count = 1;
            Shader::AccessKind access = Shader::AccessKind::eRead;
            Flags<Shader::Stage> stages;
            bool active = true;

            // Interface only, on purpose. matches() is what decides whether a shader reload
            // can keep the existing layout object, so letting access/stages/active in here
            // would rebuild the layout — and expire every descriptor set holding it — over an
            // edit that merely made a storage buffer read-only.
            bool operator==(const Binding& other) const {
                return type == other.type && count == other.count;
            }
        };

        class KORAL_API Builder : public ::Builder
        {
            friend class DescriptorSetLayout;
        public:
            Builder& addBinding(glm::u32 binding, DescriptorType type, glm::u32 count = 1,
                                Shader::AccessKind access = Shader::AccessKind::eRead,
                                Flags<Shader::Stage> stages = {}, bool active = true);
            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<DescriptorSetLayout>> create() const;
            [[nodiscard]] kor::Resource<DescriptorSetLayout> build(std::source_location where = std::source_location::current()) const;
        private:
            std::map<glm::u32, Binding> _bindings;
            std::optional<Error> _error;
        };

        explicit DescriptorSetLayout(const Builder &builder);
        virtual ~DescriptorSetLayout() = default;

        DescriptorSetLayout(const DescriptorSetLayout&) = delete;
        DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;

        [[nodiscard]] std::vector<std::tuple<glm::u32, DescriptorType, glm::u32>> getBindings() const;
        [[nodiscard]] DescriptorType getBindingType(glm::u32 binding) const;

        /** @brief Full per-binding description, including what the shader does with it. */
        [[nodiscard]] const std::map<glm::u32, Binding>& getBindingDescriptions() const { return _bindings; }

        /**
         * @brief Whether @p builder describes exactly this layout.
         *
         * Lets Pipeline::buildLayouts keep an existing layout object when a shader reload did not
         * change the set's interface — which keeps every descriptor set built from it alive.
         */
        [[nodiscard]] bool matches(const Builder& builder) const;

    protected:

        std::map<glm::u32, Binding> _bindings;
    };
}
