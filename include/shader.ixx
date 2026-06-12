//
// Created by radue on 2/21/2026.
//

module;

#include <glm/fwd.hpp>
#include "api.h"

export module gfx.shader;

import std;
import gfx.structs;
import gfx.flags;
import gfx.resource;

namespace gfx
{
    export class GFX_API Shader {
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

        struct GFX_API InputOutput
        {
            glm::u32 startingLocation;
            glm::u32 locationSpan;
            std::string name;
            ChannelType channelType;
            glm::u32 channelCount;

            auto operator<=>(const InputOutput& other) const {
                return startingLocation <=> other.startingLocation;
            }
        };

        struct GFX_API Descriptor {
            DescriptorType type;
            std::string name;
            glm::u32 count;
            Flags<Stage> stages;

            auto operator<=>(const Descriptor& other) const {
                return std::tie(type, name, count) <=> std::tie(other.type, other.name, other.count);
            }

            auto operator==(const Descriptor& other) const {
                return std::tie(type, name, count) == std::tie(other.type, other.name, other.count);
            }
        };

        struct GFX_API DescriptorSet
        {
            std::map<glm::u32, Descriptor> descriptors;
        };

        struct GFX_API PushConstant {
            std::string name;
            glm::u32 size;
            glm::u32 offset;
            Flags<Stage> stages;

            auto operator<=>(const PushConstant& other) const {
                return std::tie(name, size, offset) <=> std::tie(other.name, other.size, other.offset);
            }
            auto operator==(const PushConstant& other) const {
                return std::tie(name, size, offset) == std::tie(other.name, other.size, other.offset);
            }
        };

        struct GFX_API MemoryLayout
        {
            std::set<InputOutput> inputs;
            std::set<InputOutput> outputs;
            std::map<glm::u32, DescriptorSet> descriptorSets;
            std::map<glm::u32, PushConstant> pushConstants;
        };

        struct GFX_API Builder {
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

            [[nodiscard]] Resource<Shader> build() const;
            [[nodiscard]] ResourceRef<const Shader> buildManaged(const std::string& identifier) const;
        };

        virtual ~Shader() = default;

        [[nodiscard]] Stage getStage() const { return _stage; }
        [[nodiscard]] Lang getLang() const { return _lang; }

        static std::vector<glm::u32> CompileToSPIRV(const std::filesystem::path& path, Stage stage);
        static MemoryLayout fetchMemoryLayout(const std::vector<glm::u32>& spirvCode);

        const MemoryLayout& getMemoryLayout() const { return _memoryLayout; }

    protected:
        explicit Shader(const Builder& createInfo);
        Stage _stage;
        Lang _lang;
        std::filesystem::path _path;

        std::vector<glm::u32> _spirvCode;
        MemoryLayout _memoryLayout;
    };
}
