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
#include "builder.h"

namespace kor
{
    class KORAL_API DescriptorSetLayout
    {
    public:
        class KORAL_API Builder : public ::Builder
        {
            friend class DescriptorSetLayout;
        public:
            Builder& addBinding(glm::u32 binding, DescriptorType type, glm::u32 count = 1);
            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<DescriptorSetLayout>> create() const;
            [[nodiscard]] kor::Resource<DescriptorSetLayout> build() const;
        private:
            std::map<glm::u32, std::pair<DescriptorType, glm::u32>> _bindings;
            std::optional<Error> _error;
        };

        explicit DescriptorSetLayout(const Builder &builder);
        virtual ~DescriptorSetLayout() = default;

        DescriptorSetLayout(const DescriptorSetLayout&) = delete;
        DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;

        [[nodiscard]] std::vector<std::tuple<glm::u32, DescriptorType, glm::u32>> getBindings() const;
        [[nodiscard]] DescriptorType getBindingType(glm::u32 binding) const;

        /**
         * @brief Whether @p builder describes exactly this layout.
         *
         * Lets Pipeline::buildLayouts keep an existing layout object when a shader reload did not
         * change the set's interface — which keeps every descriptor set built from it alive.
         */
        [[nodiscard]] bool matches(const Builder& builder) const;

    protected:

        std::map<glm::u32, std::pair<DescriptorType, glm::u32>> _bindings;
    };
}
