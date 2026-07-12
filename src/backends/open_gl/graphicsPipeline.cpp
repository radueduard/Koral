//
#include <iostream>
// Created by radue on 2/22/2026.
//

#include "graphicsPipeline.h"

#include <ranges>

#include <descriptorSetLayout.h>

#include "ogl_err_handling.h"
#include "shader.h"

namespace kor::ogl
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

    GLenum toGLBlendFactor(const BlendFactor factor) {
        switch (factor) {
        case BlendFactor::eZero: return GL_ZERO;
        case BlendFactor::eOne: return GL_ONE;
        case BlendFactor::eSrcColor: return GL_SRC_COLOR;
        case BlendFactor::eOneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::eDstColor: return GL_DST_COLOR;
        case BlendFactor::eOneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
        case BlendFactor::eSrcAlpha: return GL_SRC_ALPHA;
        case BlendFactor::eOneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::eDstAlpha: return GL_DST_ALPHA;
        case BlendFactor::eOneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
        case BlendFactor::eConstantColor: return GL_CONSTANT_COLOR;
        case BlendFactor::eOneMinusConstantColor: return GL_ONE_MINUS_CONSTANT_COLOR;
        case BlendFactor::eSrcAlphaSaturate: return GL_SRC_ALPHA_SATURATE;
        default: throw std::runtime_error("Unknown blend factor!");
        }
    }

    GLenum toGLBlendOp(const BlendOp op) {
        switch (op) {
        case BlendOp::eAdd: return GL_FUNC_ADD;
        case BlendOp::eSubtract: return GL_FUNC_SUBTRACT;
        case BlendOp::eReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
        case BlendOp::eMin: return GL_MIN;
        case BlendOp::eMax: return GL_MAX;
        default: throw std::runtime_error("Unknown blend operator!");
        }
    }

    GLenum toGLStencilOp(const StencilOp op) {
        switch (op) {
        case StencilOp::eKeep: return GL_KEEP;
        case StencilOp::eZero: return GL_ZERO;
        case StencilOp::eReplace: return GL_REPLACE;
        case StencilOp::eIncrementAndClamp: return GL_INCR;
        case StencilOp::eDecrementAndClamp: return GL_DECR;
        case StencilOp::eInvert: return GL_INVERT;
        case StencilOp::eIncrementAndWrap: return GL_INCR_WRAP;
        case StencilOp::eDecrementAndWrap: return GL_DECR_WRAP;
        default: throw std::runtime_error("Unknown stencil operator!");
        }
    }

    GLenum toGLLogicOp(const LogicOp op) {
        switch (op) {
        case LogicOp::eClear: return GL_CLEAR;
        case LogicOp::eAnd: return GL_AND;
        case LogicOp::eAndReverse: return GL_AND_REVERSE;
        case LogicOp::eCopy: return GL_COPY;
        case LogicOp::eAndInverted: return GL_AND_INVERTED;
        case LogicOp::eNoOp: return GL_NOOP;
        case LogicOp::eXor: return GL_XOR;
        case LogicOp::eOr: return GL_OR;
        case LogicOp::eNor: return GL_NOR;
        case LogicOp::eEquivalent: return GL_EQUIV;
        case LogicOp::eInvert: return GL_INVERT;
        case LogicOp::eOrReverse: return GL_OR_REVERSE;
        case LogicOp::eCopyInverted: return GL_COPY_INVERTED;
        case LogicOp::eOrInverted: return GL_OR_INVERTED;
        case LogicOp::eNand: return GL_NAND;
        case LogicOp::eSet: return GL_SET;
        default: throw std::runtime_error("Unknown logic operator!");
        }
    }

    GraphicsPipeline::GraphicsPipeline(const Builder& createInfo): kor::GraphicsPipeline(createInfo)
    {
        Setup();
    }

    GraphicsPipeline::~GraphicsPipeline()
    {
        Teardown();
    }

    void GraphicsPipeline::Bind(const kor::CommandBuffer& commandBuffer) const
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


        // Line width (dynamic-capable; applied here as the pipeline default so the
        // deferred command replay starts from the correct state).
        glLineWidth(_rasterizationState.lineWidth);
        glCheckError();

        // Multisampling: per-sample shading. GL enables MSAA implicitly for
        // multisampled framebuffers; sample shading forces per-sample execution.
        if (_multisampleState.sampleShadingEnable) {
            glEnable(GL_SAMPLE_SHADING);
            glMinSampleShading(_multisampleState.minSampleShading);
        } else {
            glDisable(GL_SAMPLE_SHADING);
        }
        glCheckError();

        // Depth settings
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
        glCheckError();

        // Stencil settings: mirror Vulkan's independent front/back state.
        if (_depthStencilState.stencilEnable) {
            glEnable(GL_STENCIL_TEST);
            const auto& front = _depthStencilState.stencilFront;
            const auto& back = _depthStencilState.stencilBack;
            glStencilFuncSeparate(GL_FRONT, toGLOperator(front.compareOp), static_cast<GLint>(front.reference), front.compareMask);
            glStencilOpSeparate(GL_FRONT, toGLStencilOp(front.failOp), toGLStencilOp(front.depthFailOp), toGLStencilOp(front.passOp));
            glStencilMaskSeparate(GL_FRONT, front.writeMask);
            glStencilFuncSeparate(GL_BACK, toGLOperator(back.compareOp), static_cast<GLint>(back.reference), back.compareMask);
            glStencilOpSeparate(GL_BACK, toGLStencilOp(back.failOp), toGLStencilOp(back.depthFailOp), toGLStencilOp(back.passOp));
            glStencilMaskSeparate(GL_BACK, back.writeMask);
        } else {
            glDisable(GL_STENCIL_TEST);
        }
        glCheckError();

        // Primitive restart: Vulkan restarts strips at the max index value, which
        // matches GL's fixed-index variant (0xFFFF / 0xFFFFFFFF).
        if (_inputAssemblyState.primitiveRestartEnable) {
            glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
        } else {
            glDisable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
        }
        glCheckError();

        // Color blending: logic op takes precedence over blending in GL, just as in
        // Vulkan (enabling logic op disables blending for integer/unorm attachments).
        if (_colorBlendState.enableLogicOp) {
            glEnable(GL_COLOR_LOGIC_OP);
            glLogicOp(toGLLogicOp(_colorBlendState.logicOp));
        } else {
            glDisable(GL_COLOR_LOGIC_OP);
        }
        glCheckError();

        glBlendColor(
            _colorBlendState.blendConstants[0], _colorBlendState.blendConstants[1],
            _colorBlendState.blendConstants[2], _colorBlendState.blendConstants[3]);
        glCheckError();

        // Determine how many draw-buffer slots we need to configure. With no explicit
        // per-attachment state we still reset slot 0 to GL defaults (blend off, full
        // write mask) so a previously bound pipeline can't leak its blend state.
        const auto& attachments = _colorBlendState.attachments;
        const GLuint attachmentCount = attachments.empty() ? 1u : static_cast<GLuint>(attachments.size());
        for (GLuint i = 0; i < attachmentCount; ++i) {
            if (attachments.empty()) {
                glDisablei(GL_BLEND, i);
                glColorMaski(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                continue;
            }
            const auto& a = attachments[i];
            if (a.blendEnable) {
                glEnablei(GL_BLEND, i);
                glBlendFuncSeparatei(i,
                    toGLBlendFactor(a.srcColorBlendFactor), toGLBlendFactor(a.dstColorBlendFactor),
                    toGLBlendFactor(a.srcAlphaBlendFactor), toGLBlendFactor(a.dstAlphaBlendFactor));
                glBlendEquationSeparatei(i, toGLBlendOp(a.colorBlendOp), toGLBlendOp(a.alphaBlendOp));
            } else {
                glDisablei(GL_BLEND, i);
            }
            glColorMaski(i,
                (a.colorWriteMask & ColorComponent::eR) ? GL_TRUE : GL_FALSE,
                (a.colorWriteMask & ColorComponent::eG) ? GL_TRUE : GL_FALSE,
                (a.colorWriteMask & ColorComponent::eB) ? GL_TRUE : GL_FALSE,
                (a.colorWriteMask & ColorComponent::eA) ? GL_TRUE : GL_FALSE);
        }
        glCheckError();
    }

    void GraphicsPipeline::Unbind() const
    {
        if (!_bound) {
            throw std::runtime_error("Graphics pipeline is not bound!");
        }

        if (_vertexShader.has_value() && _inputAssemblyState.primitiveRestartEnable) {
            glDisable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
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

    void GraphicsPipeline::Setup()
    {
        _id = glCreateProgram();

        // liniarize descriptors
        glm::u32 nextDescriptorBindingPoint = 0;
        for (const auto& [set, layout] : _setLayouts) {
            for (const auto& [binding, type, Count] : layout->getBindings()) {
                _setAndBindingToBindingPoint[{ set, binding }] = nextDescriptorBindingPoint++;
            }
        }

        // Reserve a UBO binding point for push constants (emitted as a std140 uniform
        // buffer) and size a backing buffer to the largest declared range end.
        _pushConstantSize = 0;
        for (const auto& [offset, pc] : _pushConstantRanges) {
            const glm::u32 end = pc.offset + pc.size;
            if (end > _pushConstantSize) _pushConstantSize = end;
        }
        std::optional<glm::u32> pushConstantBinding;
        if (_pushConstantSize > 0) {
            _pushConstantBindingPoint = nextDescriptorBindingPoint++;
            pushConstantBinding = _pushConstantBindingPoint;
            glCreateBuffers(1, &_pushConstantUBO);
            glNamedBufferData(_pushConstantUBO, _pushConstantSize, nullptr, GL_DYNAMIC_DRAW);
            glCheckError();
        }

        std::vector<GLuint> shaderIds;
        if (_vertexShader.has_value())
        {
            const auto& vertexShader = dynamic_cast<const ogl::Shader&>(*_vertexShader.value());
            const GLuint shaderId = vertexShader.compile(_setAndBindingToBindingPoint, pushConstantBinding, &_bindlessArrays);

            glAttachShader(_id, shaderId);
            glCheckError();

            shaderIds.push_back(shaderId);
        }

        if (_tessellationState.has_value())
        {
            const auto& controlShader = dynamic_cast<const ogl::Shader&>(*_tessellationState->controlShader);
            const GLuint controlShaderId = controlShader.compile(_setAndBindingToBindingPoint, pushConstantBinding, &_bindlessArrays);

            const auto& evalShader = dynamic_cast<const ogl::Shader&>(*_tessellationState->evalShader);
            const GLuint evalShaderId = evalShader.compile(_setAndBindingToBindingPoint, pushConstantBinding, &_bindlessArrays);

            glAttachShader(_id, controlShaderId);
            glAttachShader(_id, evalShaderId);
            glCheckError();

            shaderIds.push_back(controlShaderId);
            shaderIds.push_back(evalShaderId);

        }

        if (_geometryShader.has_value())
        {
            const auto& geometryShader = dynamic_cast<const ogl::Shader&>(**_geometryShader);
            const GLuint shaderId = geometryShader.compile(_setAndBindingToBindingPoint, pushConstantBinding, &_bindlessArrays);
            glAttachShader(_id, shaderId);
            glCheckError();
            shaderIds.push_back(shaderId);
        }

        if (_taskShader.has_value())
        {
            const auto& taskShader = dynamic_cast<const ogl::Shader&>(**_taskShader);
            const GLuint shaderId = taskShader.compile(_setAndBindingToBindingPoint, pushConstantBinding, &_bindlessArrays);
            glAttachShader(_id, shaderId);
            glCheckError();
            shaderIds.push_back(shaderId);
        }

        if (_meshShader.has_value())
        {
            const auto& meshShader = dynamic_cast<const ogl::Shader&>(**_meshShader);
            const GLuint shaderId = meshShader.compile(_setAndBindingToBindingPoint, pushConstantBinding, &_bindlessArrays);
            glAttachShader(_id, shaderId);
            glCheckError();
            shaderIds.push_back(shaderId);
        }

        if (_fragmentShader.has_value())
        {
            const auto& fragmentShader = dynamic_cast<const ogl::Shader&>(**_fragmentShader);
            const GLuint shaderId = fragmentShader.compile(_setAndBindingToBindingPoint, pushConstantBinding, &_bindlessArrays);
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

        // Resolve the uniform location of each bindless material array now that the
        // program is linked; the descriptor bind sets sampler handles at location + i.
        for (auto& bindless : _bindlessArrays) {
            bindless.location = glGetUniformLocation(_id, (bindless.uniformName + "[0]").c_str());
            if (bindless.location < 0)
                bindless.location = glGetUniformLocation(_id, bindless.uniformName.c_str());
            glCheckError();
        }
    }

    void GraphicsPipeline::Teardown()
    {
        if (_pushConstantUBO != 0) {
            glDeleteBuffers(1, &_pushConstantUBO);
            _pushConstantUBO = 0;
            glCheckError();
        }
        glDeleteProgram(_id);
        glCheckError();
    }
}
