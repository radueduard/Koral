//
// Created by radue on 3/4/2026.
//

#include <descriptorSetLayout.h>
#include <framebuffer.h>
#include <surface.h>

#include "../backends/vulkan/descriptorSetLayout.h"

#include <stdexcept>
#include <string>
#include <format>

#include "context.h"
#include "../../include/window.h"

namespace kor
{
    DescriptorSetLayout::Builder& DescriptorSetLayout::Builder::addBinding(glm::u32 binding, DescriptorType type,
        glm::u32 count)
    {
        // Defer the failure to build() (which returns a Result) rather than throwing here.
        if (_bindings.contains(binding)) {
            if (!_error) _error = Error{ .code = ErrorCode::eInvalidArgument,
                .message = std::format("Binding {} already exists in the layout.", binding) };
            return *this;
        }
        _bindings[binding] = { type, count };
        return *this;
    }

    bool DescriptorSetLayout::matches(const Builder& builder) const
    {
        return _bindings == builder._bindings;
    }

    Result<std::unique_ptr<DescriptorSetLayout>> DescriptorSetLayout::Builder::create() const
    {
        beginAttempt();
        if (_error) return std::unexpected(*_error);
        if (auto v = validate(); !v) return std::unexpected(v.error());

        const auto api = Context::activeAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<DescriptorSetLayout> {
            return (api == API::eVulkan)
                ? kor::MakeBackendPtr<DescriptorSetLayout, vk::DescriptorSetLayout>(*this)
                : std::unique_ptr<DescriptorSetLayout>(std::make_unique<DescriptorSetLayout>(*this));
        });
    }

    kor::Resource<DescriptorSetLayout> DescriptorSetLayout::Builder::build(const std::source_location where) const
    {
        return materialize<DescriptorSetLayout>(*this, "DescriptorSetLayout", where);
    }

    std::vector<std::tuple<glm::u32, DescriptorType, glm::u32>> DescriptorSetLayout::getBindings() const
    {
        std::vector<std::tuple<glm::u32, DescriptorType, glm::u32>> bindings;
        for (const auto& [binding, typeAndCount] : _bindings) {
            const auto& [type, count] = typeAndCount;
            bindings.emplace_back(binding, type, count);
        }
        return bindings;
    }

    DescriptorType DescriptorSetLayout::getBindingType(const glm::u32 binding) const
    {
        if (!_bindings.contains(binding)) {
            throw std::runtime_error("Binding " + std::to_string(binding) + " does not exist in the layout!");
        }
        return _bindings.at(binding).first;
    }

    DescriptorSetLayout::DescriptorSetLayout(const Builder& builder) : _bindings(builder._bindings)
    {
    }
}
