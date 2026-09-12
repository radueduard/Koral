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
        glm::u32 count, Shader::AccessKind access, Flags<Shader::Stage> stages, bool active)
    {
        // Defer the failure to build() (which returns a Result) rather than throwing here.
        if (_bindings.contains(binding)) {
            if (!_error) _error = Error{ .code = ErrorCode::eInvalidArgument,
                .message = std::format("Binding {} already exists in the layout.", binding) };
            return *this;
        }
        _bindings[binding] = { type, count, access, stages, active };
        return *this;
    }

    DescriptorSetLayout::Builder& DescriptorSetLayout::Builder::addBinding(const glm::u32 binding, Binding description)
    {
        // Same deferral as the overload above: report at build(), which can return it.
        if (_bindings.contains(binding)) {
            if (!_error) _error = Error{ .code = ErrorCode::eInvalidArgument,
                .message = std::format("Binding {} already exists in the layout.", binding) };
            return *this;
        }
        _bindings[binding] = std::move(description);
        return *this;
    }

    DescriptorSetLayout::Builder& DescriptorSetLayout::Builder::addBlockBinding(
        const glm::u32 binding, const DescriptorType type, const glm::u32 count,
        const Shader::AccessKind access, const Flags<Shader::Stage> stages, const bool active,
        std::vector<Shader::BlockMember> members, const glm::u32 blockSize)
    {
        addBinding(binding, type, count, access, stages, active);
        if (const auto it = _bindings.find(binding); it != _bindings.end()) {
            it->second.members = std::move(members);
            it->second.blockSize = blockSize;
        }
        return *this;
    }

    bool DescriptorSetLayout::matches(const Builder& builder) const
    {
        return _bindings == builder._bindings;
    }

    bool DescriptorSetLayout::refreshBlocks(const Builder& builder)
    {
        bool changed = false;
        for (auto& [binding, description] : _bindings) {
            const auto it = builder._bindings.find(binding);
            if (it == builder._bindings.end()) continue;   // matches() guarantees there is one

            // The names ride along with the blocks, and for the same reason: they are outside the
            // layout's identity, so a reload that only renames a binding keeps this object — but
            // the new name is what the shader now calls it, and what a set written by name has to
            // find. Adopting it silently would leave a set that was written under the old name
            // looking up something that no longer exists, so a rename counts as a change and the
            // sets built against this layout are rebuilt (and re-report) against the new names.
            if (description.blockSize == it->second.blockSize &&
                description.members == it->second.members &&
                description.name == it->second.name &&
                description.blockName == it->second.blockName &&
                description.shape == it->second.shape) continue;

            description.members = it->second.members;
            description.blockSize = it->second.blockSize;
            description.name = it->second.name;
            description.blockName = it->second.blockName;
            description.shape = it->second.shape;
            changed = true;
        }
        return changed;
    }

    std::optional<glm::u32> DescriptorSetLayout::findBinding(const std::string_view name) const
    {
        if (name.empty()) return std::nullopt;
        for (const auto& [binding, description] : _bindings) {
            if (description.namedBy(name)) return binding;
        }
        return std::nullopt;
    }

    std::vector<std::string> DescriptorSetLayout::bindingNames() const
    {
        std::vector<std::string> names;
        for (const auto& [binding, description] : _bindings) {
            // Both names, when a block has two: either one is accepted, so listing only one of
            // them would send the reader looking for a binding the message says does not exist.
            if (!description.name.empty()) names.push_back(description.name);
            if (!description.blockName.empty() && description.blockName != description.name)
                names.push_back(description.blockName);
        }
        return names;
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
        for (const auto& [binding, description] : _bindings) {
            bindings.emplace_back(binding, description.type, description.count);
        }
        return bindings;
    }

    DescriptorType DescriptorSetLayout::getBindingType(const glm::u32 binding) const
    {
        if (!_bindings.contains(binding)) {
            throw std::runtime_error("Binding " + std::to_string(binding) + " does not exist in the layout!");
        }
        return _bindings.at(binding).type;
    }

    DescriptorSetLayout::DescriptorSetLayout(const Builder& builder) : _bindings(builder._bindings)
    {
    }
}
