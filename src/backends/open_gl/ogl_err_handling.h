//
// Created by radue on 2/18/2026.
//

#pragma once
#include <GL/glew.h>
#include <iostream>

inline bool glCheckError_(const char *file, int line)
{
    bool error = false;
    GLenum errorCode;
    while ((errorCode = glGetError()) != GL_NO_ERROR)
    {
        error = true;
        std::string error;
        switch (errorCode)
        {
        case GL_INVALID_ENUM:                  error = "INVALID_ENUM"; break;
        case GL_INVALID_VALUE:                 error = "INVALID_VALUE"; break;
        case GL_INVALID_OPERATION:             error = "INVALID_OPERATION"; break;
        case GL_STACK_OVERFLOW:                error = "STACK_OVERFLOW"; break;
        case GL_STACK_UNDERFLOW:               error = "STACK_UNDERFLOW"; break;
        case GL_OUT_OF_MEMORY:                 error = "OUT_OF_MEMORY"; break;
        case GL_INVALID_FRAMEBUFFER_OPERATION: error = "INVALID_FRAMEBUFFER_OPERATION"; break;
        default:                               error = "UNKNOWN_ERROR"; break;
        }
        std::cerr << error << " | " << file << " (" << line << ")" << std::endl;
    }
    return error;
}
#define glCheckError() glCheckError_(__FILE__, __LINE__)

/// Report why a framebuffer is not complete, once, at the point it was built.
///
/// Without this an incomplete framebuffer is close to undiagnosable: GL does not complain when the
/// framebuffer is *made* incomplete, only when something is drawn to it — and then every clear and
/// every draw for the rest of the run returns GL_INVALID_FRAMEBUFFER_OPERATION, so the log fills
/// with thousands of identical errors attributed to call sites that are all innocent.
inline bool glCheckFramebufferComplete_(const GLuint fbo, const char *file, const int line)
{
    const GLenum status = glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE) return true;

    std::string reason;
    switch (status)
    {
    case GL_FRAMEBUFFER_UNDEFINED:                     reason = "UNDEFINED"; break;
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:         reason = "INCOMPLETE_ATTACHMENT"; break;
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: reason = "INCOMPLETE_MISSING_ATTACHMENT"; break;
    case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:        reason = "INCOMPLETE_DRAW_BUFFER"; break;
    case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:        reason = "INCOMPLETE_READ_BUFFER"; break;
    case GL_FRAMEBUFFER_UNSUPPORTED:                   reason = "UNSUPPORTED"; break;
    case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:        reason = "INCOMPLETE_MULTISAMPLE"; break;
    case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:      reason = "INCOMPLETE_LAYER_TARGETS"; break;
    default:                                           reason = "UNKNOWN_STATUS"; break;
    }
    std::cerr << "FRAMEBUFFER_" << reason << " (fbo " << fbo << ") | " << file << " (" << line << ")"
              << std::endl;
    return false;
}
#define glCheckFramebufferComplete(fbo) glCheckFramebufferComplete_(fbo, __FILE__, __LINE__)