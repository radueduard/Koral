//
// Created by radue on 2/21/2026.
//

#pragma once
#include <filesystem>
#include <memory>

namespace gfx
{
    class Shader {
    public:
        enum class Stage {
            // VTG Graphics Pipeline
            eVertex = 1 << 0,
            eTessellationControl = 1 << 1,
            eTessellationEvaluation = 1 << 2,
            eGeometry = 1 << 3,
            eFragment = 1 << 4,

            // Compute Pipeline
            eCompute = 1 << 5,

            // MT Graphics Pipeline
            eTask = 1 << 6,
            eMesh = 1 << 7,

            // Raytracing Pipeline
            eRaygen = 1 << 8,
            eAnyHit = 1 << 9,
            eClosestHit = 1 << 10,
            eMiss = 1 << 11,
            eIntersection = 1 << 12,

            // Callable
            eCallable = 1 << 13,
        };

        enum class Lang {
            eGLSL,
            eSlang,
            eSPIRV,
        };

        struct Builder {
            Stage stage = Stage::eCompute;
            Lang lang = Lang::eGLSL;
            std::filesystem::path path = std::filesystem::path();

            Builder& setStage(const Stage stage) {
                this->stage = stage;
                return *this;
            }

            Builder& setLang(const Lang lang) {
                this->lang = lang;
                return *this;
            }

            Builder& setPath(const std::filesystem::path& path) {
                this->path = path;
                return *this;
            }

            [[nodiscard]] std::unique_ptr<Shader> build() const;
        };

        virtual ~Shader() = default;

        [[nodiscard]] Stage getStage() const { return _stage; }
        [[nodiscard]] Lang getLang() const { return _lang; }

    protected:
        explicit Shader(const Builder& createInfo);
        Stage _stage;
        Lang _lang;
        std::filesystem::path _path;
    };
}
