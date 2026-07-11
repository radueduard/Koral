//
// Created by radue on 2/21/2026.
//

#pragma once
#include <GL/glew.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../../../include/shader.h"

namespace gfx::ogl {
    // A bindless material-texture array (Vulkan descriptor-indexed `sampler2D[]`).
    // OpenGL can't bind hundreds of textures to units, so these are emitted as
    // GL_ARB_bindless_texture arrays and driven by sampler handles instead. One is
    // produced per combined image-sampler array in the shader; the pipeline resolves
    // `uniformName` to a location after link and the descriptor bind sets handles.
    struct BindlessSamplerArray {
        std::string uniformName;   // as emitted in the GLSL (query its uniform location)
        glm::u32 imageSet = 0;     // descriptor set/binding that supplies the textures
        glm::u32 imageBinding = 0;
        glm::u32 samplerSet = 0;   // descriptor set/binding that supplies the sampler
        glm::u32 samplerBinding = 0;
        glm::u32 count = 0;        // array length (fixed bindless count)
        GLint location = -1;       // resolved by the pipeline after link (glGetUniformLocation)
    };

    class Shader final : public gfx::Shader {
    public:
        explicit Shader(const gfx::Shader::Builder& createInfo);
        ~Shader() override;

        // `pushConstantBinding`, when set, is the UBO binding point the pipeline has
        // reserved for this shader's push_constant block. spirv-cross emits push
        // constants as a uniform buffer (see emit_push_constant_as_uniform_buffer), so
        // it needs an explicit binding to match the UBO the command buffer uploads to.
        // `outBindless`, when non-null, is filled with the bindless sampler arrays the
        // shader declares (see BindlessSamplerArray).
        GLuint compile(const std::map<std::pair<glm::u32, glm::u32>, glm::u32>& rebindings,
                       std::optional<glm::u32> pushConstantBinding = std::nullopt,
                       std::vector<BindlessSamplerArray>* outBindless = nullptr) const;

    private:
        static GLenum GetShaderTypeFromStage(Stage stage);
        static std::string TranspileSPIRVToGLSL(const std::vector<uint32_t>& spirvCode, Stage stage);
    };
}

