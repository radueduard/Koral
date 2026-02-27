//
// Created by radue on 2/22/2026.
//

#include "graphicsPipeline.h"
#include "impl/open_gl/resources/graphicsPipeline.h"

#include "context.h"
#include "io/window.h"

namespace gfx
{
    GraphicsPipeline::Builder& GraphicsPipeline::Builder::setVertexShader(const Shader& vertexShader)
    {
        this->vertexShader = vertexShader;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::setTessellationState(const TessellationState& tessellationState)
    {
        this->tessellationState = tessellationState;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::setGeometryShader(const Shader& geometryShader)
    {
        this->geometryShader = geometryShader;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::setFragmentShader(const Shader& fragmentShader)
    {
        this->fragmentShader = fragmentShader;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::setTaskShader(const Shader& taskShader)
    {
        this->taskShader = taskShader;
        return *this;
    }

    GraphicsPipeline::Builder& GraphicsPipeline::Builder::setMeshShader(const Shader& meshShader)
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

    std::unique_ptr<GraphicsPipeline> GraphicsPipeline::Builder::build() const
    {
        switch (Context::Window().getAPI()) {
        case API::OpenGL:
            return std::make_unique<ogl::GraphicsPipeline>(*this);
        case API::Vulkan:
            throw std::runtime_error("vulkan is not supported");
        default:
            throw std::runtime_error("Unknown API");
        }
    }

    GraphicsPipeline::GraphicsPipeline(const Builder& createInfo) :
        _vertexShader(createInfo.vertexShader),
        _tessellationState(createInfo.tessellationState),
        _geometryShader(createInfo.geometryShader),
        _fragmentShader(createInfo.fragmentShader),
        _taskShader(createInfo.taskShader),
        _meshShader(createInfo.meshShader),
        _inputAssemblyState(createInfo.inputAssemblyState),
        _rasterizationState(createInfo.rasterizationState),
        _multisampleState(createInfo.multisampleState),
        _depthStencilState(createInfo.depthStencilState),
        _colorBlendState(createInfo.colorBlendState)
    {
        if (!_vertexShader.has_value() && !_meshShader.has_value()) {
            throw std::runtime_error("Graphics pipeline must have either a vertex shader or a mesh shader!");
        }

        if (!_vertexShader.has_value() && _tessellationState.has_value()) {
            throw std::runtime_error("Tessellation state cannot be set if there is no vertex shader!");
        }

        if (!_vertexShader.has_value() && _geometryShader.has_value()) {
            throw std::runtime_error("Geometry shader cannot be set if there is no vertex shader!");
        }

        if (!_meshShader.has_value() && _taskShader.has_value()) {
            throw std::runtime_error("Task shader cannot be set if there is no mesh shader!");
        }
    }
}
