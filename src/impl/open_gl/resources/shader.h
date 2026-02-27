//
// Created by radue on 2/21/2026.
//

#pragma once
#include <GL/glew.h>

#include "core/resources/shader.h"

namespace gfx::ogl {
    class Shader final : public gfx::Shader {
    public:
        explicit Shader(const gfx::Shader::Builder& createInfo);
        ~Shader() override;

        GLuint operator*() const { return _id; }
    private:
        GLuint _id = 0;

        static GLenum GetShaderTypeFromStage(Stage stage);
    };


}

