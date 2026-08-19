//
// Created by radue on 29.07.2026.
//

#include <vertexLayout.h>

#include <algorithm>
#include <cctype>

#include "shader.h"

namespace kor
{
    namespace
    {
        // Semantics are compared without regard to case: a shader writes them the way its language
        // writes them (Slang's `: POSITION`, GLSL's `#pragma mesh(position)`), and neither spelling
        // is more correct than the other.
        bool sameName(const std::string_view a, const std::string_view b)
        {
            return std::ranges::equal(a, b, [](const unsigned char x, const unsigned char y) {
                return std::toupper(x) == std::toupper(y);
            });
        }

        // Whether an input annotated `ns(semantic)` is answered by an attribute. An empty namespace
        // on either side means "unqualified": a layout that names no vocabulary answers for any,
        // and a shader that names none takes whatever answers.
        bool answers(const VertexLayout::Attribute& attribute,
                     const std::string_view semanticNamespace, const std::string_view semantic)
        {
            if (!sameName(attribute.semantic, semantic)) return false;
            if (semanticNamespace.empty() || attribute.semanticNamespace.empty()) return true;
            return sameName(attribute.semanticNamespace, semanticNamespace);
        }

        // How an annotation reads in a diagnostic: `mesh(POSITION)`, or bare `POSITION` when the
        // shader did not say which vocabulary it meant.
        std::string describe(const std::string_view semanticNamespace, const std::string_view semantic)
        {
            if (semanticNamespace.empty()) return std::string(semantic);
            return std::string(semanticNamespace) + "(" + std::string(semantic) + ")";
        }

        // The first location two attributes both claim, if any. Only the layout can produce one:
        // where the shader decides the locations they are distinct by construction.
        std::optional<glm::u32> duplicateLocation(const std::vector<VertexInputAttributeDescription>& resolved)
        {
            for (std::size_t i = 0; i < resolved.size(); ++i) {
                for (std::size_t j = i + 1; j < resolved.size(); ++j) {
                    if (resolved[i].location == resolved[j].location) return resolved[i].location;
                }
            }
            return std::nullopt;
        }
    }

    std::optional<VertexInputAttributeDescription> VertexLayout::position() const
    {
        if (attributes.empty()) return std::nullopt;

        // Unset means the first attribute: a vertex is written position-first almost without
        // exception, and a format that is not says so rather than being guessed at.
        const std::size_t index = positionAttribute.value_or(0);
        if (index >= attributes.size()) return std::nullopt;

        const auto& attribute = attributes[index];
        return VertexInputAttributeDescription{
            .location     = 0,
            .binding      = attribute.binding,
            .channelCount = attribute.channelCount,
            .channelType  = attribute.channelType,
            .offset       = attribute.offset,
        };
    }

    Result<std::vector<VertexInputAttributeDescription>> VertexLayout::resolve(const Shader& vertexShader) const
    {
        // A view over the shader's reflection, so the matching below needs nothing of the shader
        // but what it declared. The strings are the shader's own and outlive this call.
        std::vector<ShaderInput> inputs;
        inputs.reserve(vertexShader.getMemoryLayout().inputs.size());
        for (const auto& input : vertexShader.getMemoryLayout().inputs) {
            inputs.push_back(ShaderInput{
                .location          = input.startingLocation,
                .name              = input.name,
                .semanticNamespace = input.semanticNamespace,
                .semantic          = input.semantic,
            });
        }
        return resolve(inputs);
    }

    Result<std::vector<VertexInputAttributeDescription>> VertexLayout::resolve(
        const std::span<const ShaderInput> inputs) const
    {
        const bool annotated = std::ranges::any_of(inputs, [](const ShaderInput& input) {
            return !input.semantic.empty();
        });

        std::vector<VertexInputAttributeDescription> resolved;

        // Nothing annotated: the locations are the layout's to decide — each attribute's own, where
        // it names one, and otherwise its place in the list, which is what a vertex layout meant
        // before it could be matched by name. Every attribute is described, whether the shader
        // reads it or not — an unread one is simply not fetched.
        if (!annotated) {
            resolved.reserve(attributes.size());
            for (std::size_t i = 0; i < attributes.size(); ++i) {
                const auto& attribute = attributes[i];
                resolved.push_back(VertexInputAttributeDescription{
                    .location     = attribute.location.value_or(static_cast<glm::u32>(i)),
                    .binding      = attribute.binding,
                    .channelCount = attribute.channelCount,
                    .channelType  = attribute.channelType,
                    .offset       = attribute.offset,
                });
            }

            // Two attributes at one location is a shader reading one of them and never the other.
            // The usual cause is a layout that numbers some of its attributes and leaves the rest
            // to fall back onto a number already taken.
            if (const auto clash = duplicateLocation(resolved)) {
                return fail(ErrorCode::eVertexLayoutMismatch,
                    "Two vertex attributes are described at location {}. A layout that gives any "
                    "attribute an explicit location should give them all one: the rest fall back to "
                    "their position in the list, which is what collided here.",
                    *clash);
            }
            return resolved;
        }

        // Annotated: every input the shader declares is looked up by name, and only the attributes
        // it actually asks for are described. An attribute the shader ignores costs nothing, and
        // one it asks for and the layout does not have is an error rather than a silent zero.
        resolved.reserve(inputs.size());
        for (const auto& input : inputs) {
            const VertexLayout::Attribute* match = nullptr;

            if (input.semantic.empty()) {
                // Not annotated, in a shader where others are: an attribute that names this
                // location answers for it, and failing that the one at that place in the list — so
                // one un-annotated input among annotated ones still works.
                for (const auto& attribute : attributes) {
                    if (attribute.location == input.location) {
                        match = &attribute;
                        break;
                    }
                }
                if (!match && input.location < attributes.size())
                    match = &attributes[input.location];
                if (!match) {
                    return fail(ErrorCode::eVertexLayoutMismatch,
                        "Vertex input '{}' at location {} carries no semantic, and the vertex layout "
                        "has no attribute at that location to fall back to.",
                        input.name, input.location);
                }
            } else {
                for (const auto& attribute : attributes) {
                    if (answers(attribute, input.semanticNamespace, input.semantic)) {
                        match = &attribute;
                        break;
                    }
                }
                if (!match) {
                    // Naming what the layout does carry is what turns "no match" into a fix: the
                    // usual cause is a semantic spelled differently on the two sides.
                    std::string available;
                    for (const auto& attribute : attributes) {
                        if (attribute.semantic.empty()) continue;
                        if (!available.empty()) available += ", ";
                        available += describe(attribute.semanticNamespace, attribute.semantic);
                    }

                    // A layout that names nothing at all is not a layout missing one semantic: it
                    // describes its attributes by location, and the shader is asking it a question
                    // it cannot answer in principle.
                    if (available.empty()) {
                        return fail(ErrorCode::eVertexLayoutMismatch,
                            "Vertex input '{}' asks for {}, but the vertex layout names no semantics at "
                            "all — it describes its {} attribute(s) by location. Either annotate nothing "
                            "in the shader, or give the layout's attributes semantics.",
                            input.name, describe(input.semanticNamespace, input.semantic), attributes.size());
                    }

                    return fail(ErrorCode::eVertexLayoutMismatch,
                        "Vertex input '{}' asks for {}, which the vertex layout does not carry. It has: {}.",
                        input.name, describe(input.semanticNamespace, input.semantic), available);
                }
            }

            resolved.push_back(VertexInputAttributeDescription{
                .location     = input.location,
                .binding      = match->binding,
                .channelCount = match->channelCount,
                .channelType  = match->channelType,
                .offset       = match->offset,
            });
        }

        return resolved;
    }

    // Process-wide, and so behind an accessor rather than inline in the header: an executable and a
    // module that both inlined it would each get their own copy, and the format a module defined
    // would never be the one the engine reads. @see kor::ModuleHost
    namespace
    {
        VertexLayout& defaultLayout()
        {
            static VertexLayout layout;
            return layout;
        }

        bool& defaultLayoutSet()
        {
            static bool set = false;
            return set;
        }
    }

    void VertexLayout::SetDefault(VertexLayout layout)
    {
        if (defaultLayoutSet()) return;
        defaultLayoutSet() = true;
        defaultLayout() = std::move(layout);
    }

    const VertexLayout& VertexLayout::Default()
    {
        return defaultLayout();
    }
}
