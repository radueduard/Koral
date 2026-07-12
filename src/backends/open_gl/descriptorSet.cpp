//
// Created by radue on 3/5/2026.
//

#include "descriptorSet.h"

#include <descriptorSetLayout.h>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "buffer.h"
#include "commandBuffer.h"
#include "descriptor.h"
#include "imageView.h"
#include "sampler.h"


#include "ogl_err_handling.h"

namespace kor::ogl
{
    DescriptorSet::DescriptorSet(const Builder& builder): kor::DescriptorSet(builder) {}

    void DescriptorSet::Write(const glm::u32 binding, const Descriptor &descriptor, const glm::u32 index) {
        // GL has no persistent descriptor objects: bind() re-issues every write each
        // time the set is bound, so a runtime write only has to update the stored
        // descriptor and the next bind picks it up.
        auto it = _writes.find(binding);
        if (it == _writes.end())
            throw std::runtime_error("Cannot write to binding " + std::to_string(binding) + ": it is not part of this descriptor set's layout!");
        if (index >= it->second.size()) {
            // Bindless (variable-count) bindings reflect a layout count of 0 and are
            // filled by index at runtime; grow the write list to fit, matching the
            // builder's behaviour. See DescriptorSet::Builder::write.
            bool variableCount = false;
            for (const auto& [b, type, count] : _layout->getBindings())
                if (b == binding) { variableCount = (count == 0); break; }
            if (variableCount)
                it->second.resize(index + 1);
            else
                throw std::runtime_error("Descriptor index " + std::to_string(index) + " out of range for binding " + std::to_string(binding) + "!");
        }
        it->second[index] = descriptor;
    }

    // GL_ARB_bindless_texture handles must be made resident before use, and making an
    // already-resident handle resident again is an error. Forming a handle also freezes
    // the texture immutable, so we do it exactly once per (texture, sampler) pair and
    // cache the resident handle globally (a pair's handle is stable, and the same
    // texture may appear in several descriptor sets). Never freed — material textures
    // live for the whole session. Key: (texture << 32) | sampler.
    namespace {
        std::unordered_map<glm::u64, GLuint64>& handleCache() {
            static std::unordered_map<glm::u64, GLuint64> cache;
            return cache;
        }
    }

    void DescriptorSet::bind(const kor::CommandBuffer& commandBuffer, glm::u32 index) const
    {
        const auto& oglCommandBuffer = dynamic_cast<const CommandBuffer&>(commandBuffer);
        const auto& layoutRemappings =  oglCommandBuffer.getRemappingTableForBoundPipeline();

        // Bindless material arrays declared by the bound pipeline that draw their
        // textures from THIS descriptor set. Their image/sampler bindings are driven by
        // sampler handles below, not by texture units, so skip them in the normal loop.
        const auto& bindlessArrays = oglCommandBuffer.getBoundPipelineBindlessArrays();
        std::unordered_set<glm::u32> bindlessConsumedBindings;
        for (const auto& arr : bindlessArrays) {
            if (arr.imageSet == index) {
                bindlessConsumedBindings.insert(arr.imageBinding);
                if (arr.samplerSet == index) bindlessConsumedBindings.insert(arr.samplerBinding);
            }
        }

        for (const auto& [binding, descriptors] : _writes)
        {
            if (bindlessConsumedBindings.contains(binding)) continue;
            const auto type = _layout->getBindingType(binding);
            for (size_t i = 0; i < descriptors.size(); ++i) {
                const auto& descriptor = descriptors[i];

                auto setBinding = std::make_pair(index, binding);
                auto bindingPoint = layoutRemappings.find(setBinding);
                if (bindingPoint == layoutRemappings.end()) {
                    throw std::runtime_error("No binding point found for set " + std::to_string(index) + " binding " + std::to_string(binding) + "!");
                }
                switch (type)
                {
                case DescriptorType::eUniformBuffer:
                    {
                        const auto& buffer = dynamic_cast<const ogl::Buffer&>(descriptor.getBuffer());
                        glBindBufferRange(GL_UNIFORM_BUFFER, bindingPoint->second, *buffer, descriptor.getOffset(), descriptor.getRange());
                        glCheckError();
                        break;
                    }
                case DescriptorType::eStorageBuffer:
                    {
                        const auto& buffer = dynamic_cast<const ogl::Buffer&>(descriptor.getBuffer());
                        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, bindingPoint->second, *buffer, descriptor.getOffset(), descriptor.getRange());
                        glCheckError();
                        break;
                    }
                case DescriptorType::eCombinedImageSampler:
                    {
                        const auto& sampler = dynamic_cast<const ogl::Sampler&>(descriptor.getSampler());
                        const auto& imageView = dynamic_cast<const ogl::ImageView&>(descriptor.getImageView());

                        glBindSampler(bindingPoint->second, *sampler);
                        glCheckError();
                        glActiveTexture(GL_TEXTURE0 + bindingPoint->second);
                        GLenum target = GL_TEXTURE_2D;
                        switch (imageView.getViewType())
                        {
                        case ImageView::Type::e1D:
                            target = GL_TEXTURE_1D;
                            break;
                        case ImageView::Type::e2D:
                            target = GL_TEXTURE_2D;
                            break;
                        case ImageView::Type::e3D:
                            target = GL_TEXTURE_3D;
                            break;
                        case ImageView::Type::eCube:
                            target = GL_TEXTURE_CUBE_MAP;
                            break;
                        case ImageView::Type::e1DArray:
                            target = GL_TEXTURE_1D_ARRAY;
                            break;
                        case ImageView::Type::e2DArray:
                            target = GL_TEXTURE_2D_ARRAY;
                            break;
                        case ImageView::Type::eCubeArray:
                            target = GL_TEXTURE_CUBE_MAP_ARRAY;
                            break;
                        }
                        glBindTexture(target, *imageView);
                        glCheckError();
                        break;
                    }
                case DescriptorType::eStorageImage:
                    {
                        const auto& imageView = dynamic_cast<const ogl::ImageView&>(descriptor.getImageView());

                        glBindImageTexture(bindingPoint->second,
                                           *imageView,
                                           imageView.getBaseMipLevel(),
                                           imageView.getArrayLayerCount() > 1,
                                           imageView.getBaseArrayLayer(),
                                           GL_READ_WRITE,
                                           imageView.getFormat());
                        glCheckError();
                        break;
                    }
                case DescriptorType::eSampledImage:
                    {
                        const auto& imageView = dynamic_cast<const ogl::ImageView&>(descriptor.getImageView());
                        glBindImageTexture(bindingPoint->second,
                                             *imageView,
                                             imageView.getBaseMipLevel(),
                                             imageView.getArrayLayerCount() > 1,
                                             imageView.getBaseArrayLayer(),
                                             GL_READ_ONLY,
                                             imageView.getFormat());
                        glCheckError();
                        break;
                    }
                case DescriptorType::eSampler:
                    {
                        const auto& sampler = dynamic_cast<const ogl::Sampler&>(descriptor.getSampler());
                        glBindSampler(bindingPoint->second, *sampler);
                        glCheckError();
                        break;
                    }
                default:
                    throw std::runtime_error("Unknown descriptor type for binding " + std::to_string(binding) + " index " + std::to_string(i) + "!");
                }
            }
        }

        // Bindless material arrays: for each element, form a resident texture+sampler
        // handle and set it into the pipeline's sampler2D[] uniform at location + i.
        // The shader indexes this array by material id (see albedo.frag).
        if (!bindlessArrays.empty()) {
            const GLuint program = oglCommandBuffer.getBoundPipelineProgram();
            for (const auto& arr : bindlessArrays) {
                if (arr.imageSet != index || arr.location < 0) continue;

                // Sampler shared by every element of this array (single descriptor).
                GLuint samplerId = 0;
                if (const auto s = _writes.find(arr.samplerBinding);
                    s != _writes.end() && !s->second.empty() && s->second[0].isValid())
                    samplerId = *dynamic_cast<const ogl::Sampler&>(s->second[0].getSampler());

                const auto imgIt = _writes.find(arr.imageBinding);
                if (imgIt == _writes.end()) continue;
                const auto& images = imgIt->second;
                for (size_t i = 0; i < images.size(); ++i) {
                    if (!images[i].isValid()) continue;
                    const GLuint texture = *dynamic_cast<const ogl::ImageView&>(images[i].getImageView());

                    // Cache the resident handle per (texture, sampler): forming a handle
                    // freezes the texture immutable, so we do it once, and skip the whole
                    // per-frame re-forming. A handle is stable for a given pair.
                    const glm::u64 key = (static_cast<glm::u64>(texture) << 32) | samplerId;
                    auto cached = handleCache().find(key);
                    if (cached == handleCache().end()) {
                        while (glGetError() != GL_NO_ERROR) {}
                        const GLuint64 handle = samplerId != 0
                            ? glGetTextureSamplerHandleARB(texture, samplerId)
                            : glGetTextureHandleARB(texture);
                        // Incomplete texture (still uploading): leave the slot on its
                        // previous handle and retry next frame instead of writing 0
                        // (which would sample black and get alpha-discarded).
                        if (handle == 0 || glGetError() != GL_NO_ERROR)
                            continue;
                        glMakeTextureHandleResidentARB(handle);
                        glCheckError();
                        cached = handleCache().emplace(key, handle).first;
                    }
                    glProgramUniformHandleui64ARB(program, arr.location + static_cast<GLint>(i), cached->second);
                    glCheckError();
                }
            }
        }
    }
}
