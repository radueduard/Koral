//
// Created by radue on 2/22/2026.
//

#include "graphicsPipeline.h"

namespace gfx::ogl
{
    GLenum toGLPolygonMode(const PolygonMode mode) {
        switch (mode) {
        case PolygonMode::eFill: return GL_FILL;
        case PolygonMode::eLine: return GL_LINE;
        case PolygonMode::ePoint: return GL_POINT;
        default: throw std::runtime_error("Unknown polygon mode!");
        }
    }

    GLenum toGLOperator(const CompareOp op) {
        switch (op) {
        case CompareOp::eNever: return GL_NEVER;
        case CompareOp::eLess: return GL_LESS;
        case CompareOp::eEqual: return GL_EQUAL;
        case CompareOp::eLessOrEqual: return GL_LEQUAL;
        case CompareOp::eGreater: return GL_GREATER;
        case CompareOp::eNotEqual: return GL_NOTEQUAL;
        case CompareOp::eGreaterOrEqual: return GL_GEQUAL;
        case CompareOp::eAlways: return GL_ALWAYS;
        default: throw std::runtime_error("Unknown compare operator!");
        }
    }

    GraphicsPipeline::GraphicsPipeline(const Builder& createInfo): gfx::GraphicsPipeline(createInfo)
    {
        _id = glCreateProgram();

        if (_vertexState.has_value())
        {
            const auto& vertexShader = dynamic_cast<const ogl::Shader&>(_vertexState->shader.get());
            glAttachShader(_id, *vertexShader);
            glCheckError();
        }

        if (_tessellationState.has_value())
        {
            const auto& controlShader = dynamic_cast<const ogl::Shader&>(_tessellationState->controlShader.get());
            const auto& evalShader = dynamic_cast<const ogl::Shader&>(_tessellationState->evalShader.get());
            glAttachShader(_id, *controlShader);
            glAttachShader(_id, *evalShader);
            glCheckError();
        }

        if (_geometryShader.has_value())
        {
            const auto& geometryShader = dynamic_cast<const ogl::Shader&>(_geometryShader->get());
            glAttachShader(_id, *geometryShader);
            glCheckError();
        }

        if (_taskShader.has_value())
        {
            const auto& taskShader = dynamic_cast<const ogl::Shader&>(_taskShader->get());
            glAttachShader(_id, *taskShader);
            glCheckError();
        }

        if (_meshShader.has_value())
        {
            const auto& meshShader = dynamic_cast<const ogl::Shader&>(_meshShader->get());
            glAttachShader(_id, *meshShader);
            glCheckError();
        }

        if (_fragmentShader.has_value())
        {
            const auto& fragmentShader = dynamic_cast<const ogl::Shader&>(_fragmentShader->get());
            glAttachShader(_id, *fragmentShader);
            glCheckError();
        }

        glLinkProgram(_id);
        glValidateProgram(_id);

        glCheckError();

        GLint success;
        glGetProgramiv(_id, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetProgramInfoLog(_id, 512, nullptr, infoLog);
            throw std::runtime_error(std::string("Failed to link graphics pipeline: ") + infoLog);
        }
    }

    GraphicsPipeline::~GraphicsPipeline()
    {
        glDeleteProgram(_id);
    }

    void GraphicsPipeline::Bind() const
    {
        glUseProgram(_id);
        glCheckError();
        _bound = true;

        if (_vertexState.has_value())
        {
            for (const auto& [binding, stride] : _vertexState->vertexInputState.bindings) {
                glEnableVertexAttribArray(binding);
                glCheckError();
                for (const auto& attr : _vertexState->vertexInputState.attributes) {
                    if (attr.binding == binding) {
                        glVertexAttribPointer(attr.location, attr.channelCount, static_cast<GLenum>(attr.channelType), GL_FALSE, stride, reinterpret_cast<void*>(attr.offset));
                        glCheckError();
                    }
                }
            }

            if (_vertexState->inputAssemblyState.primitiveRestartEnable) {
                glEnable(GL_PRIMITIVE_RESTART);
                glCheckError();
            }
        }

        if (_tessellationState.has_value()) {
            glPatchParameteri(GL_PATCH_VERTICES, _tessellationState->patchControlPoints);
            glCheckError();
        }

        // Rasterization settings
        glPolygonMode(GL_FRONT_AND_BACK, toGLPolygonMode(_rasterizationState.polygonMode));
        glCheckError();
        if (_rasterizationState.cullMode != Flags<CullMode>()) {
            glEnable(GL_CULL_FACE);
            if (_rasterizationState.cullMode & CullMode::eFront) {
                glCullFace(GL_FRONT);
            } else if (_rasterizationState.cullMode & CullMode::eBack) {
                glCullFace(GL_BACK);
            } else {
                glCullFace(GL_FRONT_AND_BACK);
            }
        } else {
            glDisable(GL_CULL_FACE);
        }

        glCheckError();
        glFrontFace(_rasterizationState.frontFace == FrontFace::eCounterClockwise ? GL_CCW : GL_CW);
        glCheckError();

        if (_rasterizationState.depthBiasEnable) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(_rasterizationState.depthBiasSlopeFactor, _rasterizationState.depthBiasConstantFactor);
        } else {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        glCheckError();

        if (_rasterizationState.depthClampEnable) {
            glEnable(GL_DEPTH_CLAMP);
        } else {
            glDisable(GL_DEPTH_CLAMP);
        }
        glCheckError();

        if (_rasterizationState.rasterizerDiscardEnable) {
            glEnable(GL_RASTERIZER_DISCARD);
        } else {
            glDisable(GL_RASTERIZER_DISCARD);
        }
        glCheckError();


        // TODO Multisampling settings
        // if (_multisampleState.sampleShadingEnable) {
        //     glEnable(GL_SAMPLE_SHADING);
        //     glMinSampleShading(_multisampleState.minSampleShading);
        // } else {
        //     glDisable(GL_SAMPLE_SHADING);
        // }

        // Depth/stencil settings
        if (_depthStencilState.depthTestEnable) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(toGLOperator(_depthStencilState.depthCompareOp));
            glDepthMask(_depthStencilState.depthWriteEnable ? GL_TRUE : GL_FALSE);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        glCheckError();

        if (_depthStencilState.depthBoundsEnable) {
            glEnable(GL_DEPTH_BOUNDS_TEST_EXT);
        } else {
            glDisable(GL_DEPTH_BOUNDS_TEST_EXT);
        }

        if (_depthStencilState.stencilEnable) {
            glEnable(GL_STENCIL_TEST);
            // TODO stencil settings
        } else {
            glDisable(GL_STENCIL_TEST);
        }
    }

    void GraphicsPipeline::Unbind() const
    {
        if (!_bound) {
            throw std::runtime_error("Graphics pipeline is not bound!");
        }

        if (_vertexState.has_value() && _vertexState->inputAssemblyState.primitiveRestartEnable) {
            glDisable(GL_PRIMITIVE_RESTART);
            glCheckError();

            for (const auto& [binding, _] : _vertexState->vertexInputState.bindings) {
                glDisableVertexAttribArray(binding);
                glCheckError();
            }
        }

        if (_tessellationState.has_value()) {
            glPatchParameteri(GL_PATCH_VERTICES, 0);
            glCheckError();
        }

        if (_rasterizationState.cullMode != Flags<CullMode>()) {
            glDisable(GL_CULL_FACE);
            glCheckError();
        }

        if (_rasterizationState.depthBiasEnable) {
            glDisable(GL_POLYGON_OFFSET_FILL);
            glCheckError();
        }

        if (_rasterizationState.depthClampEnable) {
            glDisable(GL_DEPTH_CLAMP);
            glCheckError();
        }

        if (_rasterizationState.rasterizerDiscardEnable) {
            glDisable(GL_RASTERIZER_DISCARD);
            glCheckError();
        }

        if (_depthStencilState.depthTestEnable) {
            glDisable(GL_DEPTH_TEST);
            glCheckError();
        }

        if (_depthStencilState.depthBoundsEnable) {
            glDisable(GL_DEPTH_BOUNDS_TEST_EXT);
            glCheckError();
        }

        if (_depthStencilState.stencilEnable) {
            glDisable(GL_STENCIL_TEST);
            glCheckError();
        }

        glUseProgram(0);
        glCheckError();
        _bound = false;
    }

    GLenum GraphicsPipeline::getMode() const
    {
        switch (_vertexState->inputAssemblyState.topology)
        {
        case Topology::ePointList: return GL_POINTS;
        case Topology::eLineList: return GL_LINES;
        case Topology::eLineStrip: return GL_LINE_STRIP;
        case Topology::eTriangleList: return GL_TRIANGLES;
        case Topology::eTriangleStrip: return GL_TRIANGLE_STRIP;
        case Topology::eTriangleFan: return GL_TRIANGLE_FAN;
        case Topology::eLineListAdjacency: return GL_LINES_ADJACENCY;
        case Topology::eLineStripAdjacency: return GL_LINE_STRIP_ADJACENCY;
        case Topology::eTriangleListAdjacency: return GL_TRIANGLES_ADJACENCY;
        case Topology::eTriangleStripAdjacency: return GL_TRIANGLE_STRIP_ADJACENCY;
        case Topology::ePatchList: return GL_PATCHES;
        default: throw std::runtime_error("Unknown topology!");
        }
    }
}
