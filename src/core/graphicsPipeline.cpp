//
// Created by radue on 2/22/2026.
//

#include <graphicsPipeline.h>
#include <descriptorSetLayout.h>
#include <framebuffer.h>
#include <surface.h>
#include <window.h>

#include "../backends/open_gl/graphicsPipeline.h"
#include "../backends/vulkan/graphicsPipeline.h"

#include "context.h"
#include "shader.h"
#include "vertexLayout.h"

namespace kor
{
    GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetVertexShader(ResourceRef<const Shader> shader,
                                                                         const VertexLayout& layout) {
        this->vertexShader = shader;
        this->vertexLayout = layout;
        return *this;
    }

    GraphicsPipeline::Builder & GraphicsPipeline::Builder::SetVertexShader(ResourceRef<const Shader> shader) {
        return SetVertexShader(std::move(shader), VertexLayout::Default());
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetTessellationState(const TessellationState& tessellationState)
    {
        this->tessellationState = tessellationState;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetGeometryShader(ResourceRef<const Shader> geometryShader)
    {
        this->geometryShader = geometryShader;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetFragmentShader(ResourceRef<const Shader> fragmentShader)
    {
        this->fragmentShader = fragmentShader;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetTaskShader(ResourceRef<const Shader> taskShader)
    {
        this->taskShader = taskShader;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetMeshShader(ResourceRef<const Shader> meshShader)
    {
        this->meshShader = meshShader;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetInputAssemblyState(const InputAssemblyState& inputAssemblyState)
    {
        this->inputAssemblyState = inputAssemblyState;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetRasterizationState(const RasterizationState& rasterizationState)
    {
        this->rasterizationState = rasterizationState;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetMultisampleState(const MultisampleState& multisampleState)
    {
        this->multisampleState = multisampleState;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetDepthStencilState(const DepthStencilState& depthStencilState)
    {
        this->depthStencilState = depthStencilState;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetColorBlendState(const ColorBlendState& colorBlendState)
    {
        this->colorBlendState = colorBlendState;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetFramebuffer(kor::ResourceRef<const kor::Framebuffer> framebuffer)
    {
        this->framebuffer = framebuffer;
        return *this;
    }

    kor::Result<std::unique_ptr<GraphicsPipeline>> GraphicsPipeline::Builder::Create() const
    {
        BeginAttempt();

        // A pipeline is only as usable as its shaders. Adopting them here is what turns "the
        // fragment shader failed to compile" into "this pipeline is unusable, *because* the
        // fragment shader failed to compile" — and what lets it come back when that is fixed.
        if (vertexShader)   Adopt(*vertexShader,   "vertex shader");
        if (geometryShader) Adopt(*geometryShader, "geometry shader");
        if (fragmentShader) Adopt(*fragmentShader, "fragment shader");
        if (taskShader)     Adopt(*taskShader,     "task shader");
        if (meshShader)     Adopt(*meshShader,     "mesh shader");
        if (tessellationState) {
            Adopt(tessellationState->controlShader, "tessellation control shader");
            Adopt(tessellationState->evalShader,    "tessellation evaluation shader");
        }
        if (framebuffer) Adopt(*framebuffer, "framebuffer");

        if (auto v = Validate(); !v) return std::unexpected(v.error());

        const auto api = Context::ActiveAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return Fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        // The vertex layout becomes locations here rather than when it was set, because the shader
        // it is matched against can be recompiled underneath us: a reload runs Create() again, and
        // the attributes follow wherever the new shader put its inputs.
        Builder resolved = *this;
        if (vertexLayout.has_value() && vertexShader.has_value()) {
            auto attributes = vertexLayout->Resolve(**vertexShader);
            if (!attributes) {
                // The shader is what has to be edited, so the error names it as the place to look.
                return Fail(attributes.error().code, "{} (vertex shader '{}')",
                            attributes.error().message, (*vertexShader)->SourcePath().string());
            }
            resolved.vertexAttributeDescriptions = std::move(*attributes);
            resolved.vertexBindingDescriptions = vertexLayout->bindings;
        }

        // Construction runs Validate() (which may throw BackendException with a specific
        // code) and the backend Setup(); Guard() turns any escape into a kor::Error.
        return Guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<GraphicsPipeline> {
            return (api == API::eVulkan)
                ? kor::MakeBackendPtr<GraphicsPipeline, vk::GraphicsPipeline>(resolved)
                : kor::MakeBackendPtr<GraphicsPipeline, ogl::GraphicsPipeline>(resolved);
        });
    }

    kor::Resource<GraphicsPipeline> GraphicsPipeline::Builder::Build(const std::source_location where) const
    {
        auto pipeline = Materialize<GraphicsPipeline>(*this, "GraphicsPipeline", where);
        // Registered even when poisoned: the Repository is what drives the retry that brings it
        // back once its shaders compile again.
        Context::Repository().AddRef(ResourceRef<const GraphicsPipeline>(pipeline));
        return pipeline;
    }

    GraphicsPipeline::~GraphicsPipeline()
    {
        if (_vertexShader.has_value()) UnsubscribeReload(*_vertexShader);
        if (_tessellationState.has_value()) {
            UnsubscribeReload(_tessellationState->controlShader);
            UnsubscribeReload(_tessellationState->evalShader);
        }
        if (_geometryShader.has_value()) UnsubscribeReload(*_geometryShader);
        if (_fragmentShader.has_value()) UnsubscribeReload(*_fragmentShader);
        if (_taskShader.has_value()) UnsubscribeReload(*_taskShader);
        if (_meshShader.has_value()) UnsubscribeReload(*_meshShader);
    }

    GraphicsPipeline::GraphicsPipeline(const Builder& createInfo)
        : _vertexShader(createInfo.vertexShader),
        _tessellationState(createInfo.tessellationState),
        _geometryShader(createInfo.geometryShader),
        _fragmentShader(createInfo.fragmentShader),
        _taskShader(createInfo.taskShader),
        _meshShader(createInfo.meshShader),
        _inputAssemblyState(createInfo.inputAssemblyState),
        _rasterizationState(createInfo.rasterizationState),
        _multisampleState(createInfo.multisampleState),
        _framebuffer(createInfo.framebuffer.has_value() ? createInfo.framebuffer.value() : Context::DefaultFramebuffer()),
        _depthStencilState(createInfo.depthStencilState),
        _colorBlendState(createInfo.colorBlendState),
        _vertexAttributeDescriptions(createInfo.vertexAttributeDescriptions),
        _vertexBindingDescriptions(createInfo.vertexBindingDescriptions),
        _specConstantsMetadata(createInfo.specConstantsMetadata),
        _specConstantsData(createInfo.specConstantsData)
    {
        if (auto v = Validate(); !v) throw BackendException(v.error());

        if (_vertexShader.has_value()) SubscribeReload(*_vertexShader);
        if (_tessellationState.has_value()) {
            SubscribeReload(_tessellationState->controlShader);
            SubscribeReload(_tessellationState->evalShader);
        }
        if (_geometryShader.has_value()) SubscribeReload(*_geometryShader);
        if (_fragmentShader.has_value()) SubscribeReload(*_fragmentShader);
        if (_taskShader.has_value()) SubscribeReload(*_taskShader);
        if (_meshShader.has_value()) SubscribeReload(*_meshShader);
    }

    VoidResult GraphicsPipeline::Validate()
    {
        if (!_vertexShader.has_value() && !_meshShader.has_value())
            return Fail(ErrorCode::eMissingShaderStage, "Graphics pipeline must have either a vertex shader or a mesh shader.");
        if (!_vertexShader.has_value() && _tessellationState.has_value())
            return Fail(ErrorCode::eShaderStageMismatch, "Tessellation state requires a vertex shader.");
        if (!_vertexShader.has_value() && _geometryShader.has_value())
            return Fail(ErrorCode::eShaderStageMismatch, "Geometry shader requires a vertex shader.");
        if (!_meshShader.has_value() && _taskShader.has_value())
            return Fail(ErrorCode::eShaderStageMismatch, "Task shader requires a mesh shader.");

        std::vector<kor::ResourceRef<const Shader>> shaders;
        if (_vertexShader.has_value()) shaders.push_back(*_vertexShader);
        if (_tessellationState.has_value()) {
            shaders.push_back(_tessellationState->controlShader);
            shaders.push_back(_tessellationState->evalShader);
        }
        if (_geometryShader.has_value()) shaders.push_back(*_geometryShader);
        if (_fragmentShader.has_value()) shaders.push_back(*_fragmentShader);
        if (_taskShader.has_value()) shaders.push_back(*_taskShader);
        if (_meshShader.has_value()) shaders.push_back(*_meshShader);

        // Merge descriptor set layouts and push constants across all stages.
        if (auto merged = BuildLayouts(shaders); !merged)
            return std::unexpected(merged.error());

        return {};
    }
}
