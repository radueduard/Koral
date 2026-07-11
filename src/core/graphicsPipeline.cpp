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
#include "meshLayout.h"
#include "shader.h"

namespace gfx
{
    GraphicsPipeline::Builder & GraphicsPipeline::Builder::setVertexShader(ResourceRef<const Shader> shader) {
        this->vertexShader = shader;
        this->vertexAttributeDescriptions = DefaultMeshRegistry::Attributes();
        this->vertexBindingDescriptions = DefaultMeshRegistry::Bindings();
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::setTessellationState(const TessellationState& tessellationState)
    {
        this->tessellationState = tessellationState;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::setGeometryShader(ResourceRef<const Shader> geometryShader)
    {
        this->geometryShader = geometryShader;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::setFragmentShader(ResourceRef<const Shader> fragmentShader)
    {
        this->fragmentShader = fragmentShader;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::setTaskShader(ResourceRef<const Shader> taskShader)
    {
        this->taskShader = taskShader;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::setMeshShader(ResourceRef<const Shader> meshShader)
    {
        this->meshShader = meshShader;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::setInputAssemblyState(const InputAssemblyState& inputAssemblyState)
    {
        this->inputAssemblyState = inputAssemblyState;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::setRasterizationState(const RasterizationState& rasterizationState)
    {
        this->rasterizationState = rasterizationState;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::setMultisampleState(const MultisampleState& multisampleState)
    {
        this->multisampleState = multisampleState;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::setDepthStencilState(const DepthStencilState& depthStencilState)
    {
        this->depthStencilState = depthStencilState;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::setColorBlendState(const ColorBlendState& colorBlendState)
    {
        this->colorBlendState = colorBlendState;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::setFramebuffer(gfx::ResourceRef<gfx::Framebuffer> framebuffer)
    {
        this->framebuffer = framebuffer;
        return *this;
    }

    gfx::Result<std::unique_ptr<GraphicsPipeline>> GraphicsPipeline::Builder::create() const
    {
        beginAttempt();

        // A pipeline is only as usable as its shaders. Adopting them here is what turns "the
        // fragment shader failed to compile" into "this pipeline is unusable, *because* the
        // fragment shader failed to compile" — and what lets it come back when that is fixed.
        if (vertexShader)   adopt(*vertexShader,   "vertex shader");
        if (geometryShader) adopt(*geometryShader, "geometry shader");
        if (fragmentShader) adopt(*fragmentShader, "fragment shader");
        if (taskShader)     adopt(*taskShader,     "task shader");
        if (meshShader)     adopt(*meshShader,     "mesh shader");
        if (tessellationState) {
            adopt(tessellationState->controlShader, "tessellation control shader");
            adopt(tessellationState->evalShader,    "tessellation evaluation shader");
        }
        if (framebuffer) adopt(*framebuffer, "framebuffer");

        if (auto v = validate(); !v) return std::unexpected(v.error());

        const auto api = Context::activeAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        // Construction runs Validate() (which may throw BackendException with a specific
        // code) and the backend Setup(); guard() turns any escape into a gfx::Error.
        return guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<GraphicsPipeline> {
            return (api == API::eVulkan)
                ? gfx::MakeBackendPtr<GraphicsPipeline, vk::GraphicsPipeline>(*this)
                : gfx::MakeBackendPtr<GraphicsPipeline, ogl::GraphicsPipeline>(*this);
        });
    }

    gfx::Resource<GraphicsPipeline> GraphicsPipeline::Builder::build() const
    {
        auto pipeline = materialize<GraphicsPipeline>(*this, "GraphicsPipeline");
        // Registered even when poisoned: the Repository is what drives the retry that brings it
        // back once its shaders compile again.
        Context::Repository().addRef(ResourceRef<const GraphicsPipeline>(pipeline));
        return pipeline;
    }

    GraphicsPipeline::~GraphicsPipeline()
    {
        if (_vertexShader.has_value()) unsubscribeReload(*_vertexShader);
        if (_tessellationState.has_value()) {
            unsubscribeReload(_tessellationState->controlShader);
            unsubscribeReload(_tessellationState->evalShader);
        }
        if (_geometryShader.has_value()) unsubscribeReload(*_geometryShader);
        if (_fragmentShader.has_value()) unsubscribeReload(*_fragmentShader);
        if (_taskShader.has_value()) unsubscribeReload(*_taskShader);
        if (_meshShader.has_value()) unsubscribeReload(*_meshShader);
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

        if (_vertexShader.has_value()) subscribeReload(*_vertexShader);
        if (_tessellationState.has_value()) {
            subscribeReload(_tessellationState->controlShader);
            subscribeReload(_tessellationState->evalShader);
        }
        if (_geometryShader.has_value()) subscribeReload(*_geometryShader);
        if (_fragmentShader.has_value()) subscribeReload(*_fragmentShader);
        if (_taskShader.has_value()) subscribeReload(*_taskShader);
        if (_meshShader.has_value()) subscribeReload(*_meshShader);
    }

    VoidResult GraphicsPipeline::Validate()
    {
        if (!_vertexShader.has_value() && !_meshShader.has_value())
            return fail(ErrorCode::eMissingShaderStage, "Graphics pipeline must have either a vertex shader or a mesh shader.");
        if (!_vertexShader.has_value() && _tessellationState.has_value())
            return fail(ErrorCode::eShaderStageMismatch, "Tessellation state requires a vertex shader.");
        if (!_vertexShader.has_value() && _geometryShader.has_value())
            return fail(ErrorCode::eShaderStageMismatch, "Geometry shader requires a vertex shader.");
        if (!_meshShader.has_value() && _taskShader.has_value())
            return fail(ErrorCode::eShaderStageMismatch, "Task shader requires a mesh shader.");

        std::vector<gfx::ResourceRef<const Shader>> shaders;
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
        if (!buildLayouts(shaders))
            return fail(ErrorCode::eDescriptorConflict, "Descriptor declarations conflict across the pipeline's shader stages.");

        return {};
    }
}
