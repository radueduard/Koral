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
    DescriptorSetLayout::Builder& DescriptorSetLayout::Builder::AddBinding(kor::u32 binding, DescriptorType type,
        kor::u32 count, Shader::AccessKind access, Flags<Shader::Stage> stages, bool active)
    {
        // Defer the failure to Build() (which returns a Result) rather than throwing here.
        if (_bindings.contains(binding)) {
            if (!_error) _error = Error{ .code = ErrorCode::eInvalidArgument,
                .message = std::format("Binding {} already exists in the layout.", binding) };
            return *this;
        }
        _bindings[binding] = { type, count, access, stages, active };
        return *this;
    }

    DescriptorSetLayout::Builder& DescriptorSetLayout::Builder::AddBinding(const kor::u32 binding, Binding description)
    {
        // Same deferral as the overload above: report at Build(), which can return it.
        if (_bindings.contains(binding)) {
            if (!_error) _error = Error{ .code = ErrorCode::eInvalidArgument,
                .message = std::format("Binding {} already exists in the layout.", binding) };
            return *this;
        }
        _bindings[binding] = std::move(description);
        return *this;
    }

    DescriptorSetLayout::Builder& DescriptorSetLayout::Builder::AddBlockBinding(
        const kor::u32 binding, const DescriptorType type, const kor::u32 count,
        const Shader::AccessKind access, const Flags<Shader::Stage> stages, const bool active,
        std::vector<Shader::BlockMember> members, const kor::u32 blockSize)
    {
        AddBinding(binding, type, count, access, stages, active);
        if (const auto it = _bindings.find(binding); it != _bindings.end()) {
            it->second.members = std::move(members);
            it->second.blockSize = blockSize;
        }
        return *this;
    }

    bool DescriptorSetLayout::Matches(const Builder& builder) const
    {
        return _bindings == builder._bindings;
    }

    bool DescriptorSetLayout::RefreshBlocks(const Builder& builder)
    {
        bool changed = false;
        for (auto& [binding, description] : _bindings) {
            const auto it = builder._bindings.find(binding);
            if (it == builder._bindings.end()) continue;   // Matches() guarantees there is one

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

    std::optional<kor::u32> DescriptorSetLayout::FindBinding(const std::string_view name) const
    {
        if (name.empty()) return std::nullopt;
        for (const auto& [binding, description] : _bindings) {
            if (description.NamedBy(name)) return binding;
        }
        return std::nullopt;
    }

    std::vector<std::string> DescriptorSetLayout::BindingNames() const
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

    Result<std::unique_ptr<DescriptorSetLayout>> DescriptorSetLayout::Builder::Create() const
    {
        BeginAttempt();
        if (_error) return std::unexpected(*_error);
        if (auto v = Validate(); !v) return std::unexpected(v.error());

        const auto api = Context::ActiveAPI();
        if (api != API::eVulkan)
            return Fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return Guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<DescriptorSetLayout> {
            return (api == API::eVulkan)
                ? kor::MakeBackendPtr<DescriptorSetLayout, vk::DescriptorSetLayout>(*this)
                : std::unique_ptr<DescriptorSetLayout>(std::make_unique<DescriptorSetLayout>(*this));
        });
    }

    kor::Resource<DescriptorSetLayout> DescriptorSetLayout::Builder::Build(const std::source_location where) const
    {
        return Materialize<DescriptorSetLayout>(*this, "DescriptorSetLayout", where);
    }

    DescriptorType DescriptorSetLayout::BindingType(const kor::u32 binding) const
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
