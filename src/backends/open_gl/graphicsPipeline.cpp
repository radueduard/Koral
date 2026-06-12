//
// Created by radue on 2/22/2026.
//

module;

#include <ranges>
#include "ogl_err_handling.h"

module ogl.graphicsPipeline;
import ogl.shader;
import gfx.flags;


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

        // liniarize descriptors
        glm::u32 nextDescriptorBindingPoint = 0;
        for (const auto& [set, layout] : _setLayouts) {
            for (const auto& [binding, type, Count] : layout->getBindings()) {
                _setAndBindingToBindingPoint[{ set, binding }] = nextDescriptorBindingPoint++;
            }
        }

        std::vector<GLuint> shaderIds;
        if (_vertexShader.has_value())
        {
            const auto& vertexShader = dynamic_cast<const ogl::Shader&>(*_vertexShader.value());
            const GLuint shaderId = vertexShader.compile(_setAndBindingToBindingPoint);

            glAttachShader(_id, shaderId);
            glCheckError();

            shaderIds.push_back(shaderId);
        }

        if (_tessellationState.has_value())
        {
            const auto& controlShader = dynamic_cast<const ogl::Shader&>(*_tessellationState->controlShader);
            const GLuint controlShaderId = controlShader.compile(_setAndBindingToBindingPoint);

            const auto& evalShader = dynamic_cast<const ogl::Shader&>(*_tessellationState->evalShader);
            const GLuint evalShaderId = evalShader.compile(_setAndBindingToBindingPoint);

            glAttachShader(_id, controlShaderId);
            glAttachShader(_id, evalShaderId);
            glCheckError();

            shaderIds.push_back(controlShaderId);
            shaderIds.push_back(evalShaderId);

        }

        if (_geometryShader.has_value())
        {
            const auto& geometryShader = dynamic_cast<const ogl::Shader&>(**_geometryShader);
            const GLuint shaderId = geometryShader.compile(_setAndBindingToBindingPoint);
            glAttachShader(_id, shaderId);
            glCheckError();
            shaderIds.push_back(shaderId);
        }

        if (_taskShader.has_value())
        {
            const auto& taskShader = dynamic_cast<const ogl::Shader&>(**_taskShader);
            const GLuint shaderId = taskShader.compile(_setAndBindingToBindingPoint);
            glAttachShader(_id, shaderId);
            glCheckError();
            shaderIds.push_back(shaderId);
        }

        if (_meshShader.has_value())
        {
            const auto& meshShader = dynamic_cast<const ogl::Shader&>(**_meshShader);
            const GLuint shaderId = meshShader.compile(_setAndBindingToBindingPoint);
            glAttachShader(_id, shaderId);
            glCheckError();
            shaderIds.push_back(shaderId);
        }

        if (_fragmentShader.has_value())
        {
            const auto& fragmentShader = dynamic_cast<const ogl::Shader&>(**_fragmentShader);
            const GLuint shaderId = fragmentShader.compile(_setAndBindingToBindingPoint);
            glAttachShader(_id, shaderId);
            glCheckError();
            shaderIds.push_back(shaderId);
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

        for (const auto& shaderId : shaderIds) {
            glDetachShader(_id, shaderId);
            glDeleteShader(shaderId);
            glCheckError();
        }
    }

    GraphicsPipeline::~GraphicsPipeline()
    {
        glDeleteProgram(_id);
        glCheckError();
    }

    void GraphicsPipeline::Bind(const gfx::CommandBuffer& commandBuffer) const
    {
        glUseProgram(_id);
        glCheckError();
        _bound = true;

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

        if (_vertexShader.has_value() && _inputAssemblyState.primitiveRestartEnable) {
            glDisable(GL_PRIMITIVE_RESTART);
            glCheckError();
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
        switch (_inputAssemblyState.topology)
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
