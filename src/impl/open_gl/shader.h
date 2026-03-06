//
// Created by radue on 2/21/2026.
//

#pragma once
#include <GL/glew.h>

#include "core/shader.h"

namespace gfx::ogl {
    class Shader final : public gfx::Shader {
    public:
        explicit Shader(const gfx::Shader::Builder& createInfo);
        ~Shader() override;

        GLuint compile(const std::map<std::pair<glm::u32, glm::u32>, glm::u32>& rebindings) const;

    private:
        static GLenum GetShaderTypeFromStage(Stage stage);
        static std::string TranspileSPIRVToGLSL(const std::vector<uint32_t>& spirvCode, Stage stage);
    };
}

