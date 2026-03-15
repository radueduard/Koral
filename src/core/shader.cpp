//
// Created by radue on 2/21/2026.
//

#include "../backends/open_gl/shader.h"
#include "../backends/vulkan/shader.h"

#include <shader.h>
#include <window.h>
#include <context.h>
#include <file.h>
#include <framebuffer.h>
#include <surface.h>

#include <iostream>

#include <spirv_cross.hpp>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include "structs.h"


namespace gfx {
    std::unique_ptr<Shader> Shader::Builder::build() const
    {
        switch (Context::Window().getAPI()) {
        case API::eOpenGL:
            return std::make_unique<ogl::Shader>(*this);
        case API::eVulkan:
            return std::make_unique<vk::Shader>(*this);
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    static EShLanguage shaderStageToEShLanguage(const Shader::Stage &stage) {
        switch (stage) {
        case Shader::Stage::eVertex: return EShLangVertex;
        case Shader::Stage::eGeometry: return EShLangGeometry;
        case Shader::Stage::eTessellationControl: return EShLangTessControl;
        case Shader::Stage::eTessellationEvaluation: return EShLangTessEvaluation;
        case Shader::Stage::eFragment: return EShLangFragment;
        case Shader::Stage::eCompute: return EShLangCompute;
        case Shader::Stage::eMesh: return EShLangMeshNV;
        case Shader::Stage::eTask: return EShLangTaskNV;
        case Shader::Stage::eRaygen: return EShLangRayGenNV;
        case Shader::Stage::eIntersection: return EShLangIntersectNV;
        case Shader::Stage::eAnyHit: return EShLangAnyHitNV;
        case Shader::Stage::eClosestHit: return EShLangClosestHitNV;
        case Shader::Stage::eMiss: return EShLangMissNV;
        case Shader::Stage::eCallable: return EShLangCallableNV;
        default: throw std::runtime_error("Unknown shader stage!");
        }
    }

    std::vector<glm::u32> Shader::CompileToSPIRV(const std::filesystem::path& path, const Stage stage)
    {
        const auto source = utils::ReadFileAsString(path);
        glslang::InitializeProcess();
        const auto eShStage = shaderStageToEShLanguage(stage);

        const auto shader = new glslang::TShader(eShStage);
        const auto shaderStrings = new std::string(source);
        const auto shaderStringsPointer = shaderStrings->c_str();
        shader->setStrings(&shaderStringsPointer, 1);

        shader->setEnvInput(glslang::EShSourceGlsl, eShStage, glslang::EShClientVulkan, 130);
        shader->setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
        shader->setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);

        constexpr auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules | EShMsgDefault);

        if (!shader->parse(GetDefaultResources(), 100, false, messages)) {
            std::cerr << shader->getInfoLog() << std::endl;
            throw std::runtime_error("GLSL parsing failed for stage: " + std::to_string(eShStage));
        }

        const auto program = new glslang::TProgram;
        program->addShader(shader);

        if (!program->link(messages)) {
            throw std::runtime_error("GLSL linking failed for stage: " + std::to_string(eShStage));
        }

        std::vector<uint32_t> spirV;
        GlslangToSpv(*program->getIntermediate(eShStage), spirV);

        program->dumpReflection();

        delete program;
        delete shader;
        delete shaderStrings;

        glslang::FinalizeProcess();
        return spirV;
    }

	std::pair<ChannelType, glm::u32> SPIRTypeConverter(const spirv_cross::SPIRType& type)
    {
    	auto rows = type.vecsize;
    	const auto columns = type.columns;
    	const auto base = type.basetype;

    	if (columns > 1) {
    		throw std::runtime_error("Matrices are not supported as shader inputs/outputs!");
    	}

    	switch (base) {
			case spirv_cross::SPIRType::BaseType::Float: return {ChannelType::eFloat, rows};
			case spirv_cross::SPIRType::BaseType::Double: return {ChannelType::eDouble, rows};
			case spirv_cross::SPIRType::BaseType::SByte: return {ChannelType::eByte, rows};
			case spirv_cross::SPIRType::BaseType::UByte: return {ChannelType::eUByte, rows};
			case spirv_cross::SPIRType::BaseType::Short: return {ChannelType::eShort, rows};
			case spirv_cross::SPIRType::BaseType::UShort: return {ChannelType::eUShort, rows};
			case spirv_cross::SPIRType::BaseType::Int: return {ChannelType::eInt, rows};
			case spirv_cross::SPIRType::BaseType::UInt: return {ChannelType::eUInt, rows};
			default: throw std::runtime_error("Unknown base type for shader input/output!");
		}
    }

	glm::u32 GetCount(const spirv_cross::SPIRType& type) {
    	uint32_t count = 1;
    	for (const auto& arraySize : type.array) {
    		count *= arraySize;
    	}
    	return count;
    }

    Shader::MemoryLayout Shader::fetchMemoryLayout(const std::vector<glm::u32>& spirvCode)
    {
        const auto module = spirv_cross::Compiler(spirvCode);
        const auto resources = module.get_shader_resources();

    	MemoryLayout memoryLayout;

        for (const auto& input : resources.stage_inputs) {
			auto location = module.get_decoration(input.id, spv::DecorationLocation);
        	auto locationSpan = 1; // TODO: handle location spans for arrays and structs
			const auto& name = module.get_name(input.id);
			const auto& type = module.get_type(input.type_id);
        	const auto[channelType, channelCount] = SPIRTypeConverter(type);

			memoryLayout.inputs.emplace(location, locationSpan, name, channelType, channelCount);
		} // inputs
		for (const auto& output : resources.stage_outputs) {
			auto location = module.get_decoration(output.id, spv::DecorationLocation);
			auto locationSpan = 1; // TODO: handle location spans for arrays and structs
			auto name = module.get_name(output.id);
			const auto& type = module.get_type(output.type_id);
			const auto[channelType, channelCount] = SPIRTypeConverter(type);

			memoryLayout.outputs.emplace(location, locationSpan, name, channelType, channelCount);
		} // outputs
		for (const auto& sampler : resources.separate_samplers) {
			const uint32_t set = module.get_decoration(sampler.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(sampler.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(sampler.type_id));
			const auto& name = module.get_name(sampler.id);

			memoryLayout.descriptorSets[set].descriptors.emplace(binding, std::make_tuple(DescriptorType::eSampler, name, count));
		} // eSampler
		for (const auto& sampledImage : resources.separate_images) {
			const uint32_t set = module.get_decoration(sampledImage.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(sampledImage.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(sampledImage.type_id));
			const auto& name = module.get_name(sampledImage.id);
			if (module.get_type(sampledImage.type_id).image.dim == spv::DimBuffer) {
				// memoryLayout.descriptorSets[set].descriptors.emplace(binding, std::make_tuple(DescriptorType::eUniformTexelBuffer, name, count));
			}
			else {
				memoryLayout.descriptorSets[set].descriptors.emplace(binding, std::make_tuple(DescriptorType::eStorageImage, name, count));
			}
		} // eSampledImage and eUniformTexelBuffer
		for (const auto& sampledImage : resources.sampled_images) {
			const uint32_t set = module.get_decoration(sampledImage.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(sampledImage.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(sampledImage.type_id));
			const auto& name = module.get_name(sampledImage.id);
			memoryLayout.descriptorSets[set].descriptors.emplace(binding, std::make_tuple(DescriptorType::eCombinedImageSampler, name, count));
		} // eCombinedImageSampler
		for (const auto& image : resources.storage_images) {
			const uint32_t set = module.get_decoration(image.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(image.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(image.type_id));
			const auto& name = module.get_name(image.id);
			if (module.get_type(image.type_id).image.dim == spv::DimBuffer) {
				// memoryLayout.descriptorSets[set].descriptors.emplace(binding, std::make_tuple(DescriptorType::eStorageTexelBuffer, name, count));
			}
			else {
				memoryLayout.descriptorSets[set].descriptors.emplace(binding, std::make_tuple(DescriptorType::eStorageImage, name, count));
			}
		} // eStorageImage and eStorageTexelBuffer
		for (const auto& buffer : resources.uniform_buffers) {
			const uint32_t set = module.get_decoration(buffer.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(buffer.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(buffer.type_id));
			const auto& name = module.get_name(buffer.id);
			memoryLayout.descriptorSets[set].descriptors.emplace(binding, std::make_tuple(DescriptorType::eUniformBuffer, name, count));
		} // eUniformBuffer
		for (const auto& buffer : resources.storage_buffers) {
			const uint32_t set = module.get_decoration(buffer.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(buffer.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(buffer.type_id));
			const auto& name = module.get_name(buffer.id);
			memoryLayout.descriptorSets[set].descriptors.emplace(binding, std::make_tuple(DescriptorType::eStorageBuffer, name, count));
		} // eStorageBuffer

    	return memoryLayout;
    }

    Shader::Shader(const Builder& createInfo) :
        _stage(createInfo.stage),
        _lang(createInfo.lang),
        _path(createInfo.path)
    {
        if (_path.empty()) {
            throw std::runtime_error("Shader path cannot be empty!");
        }
    }
}
