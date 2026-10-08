//
// For a module's own C interface: what it needs of Koral's handles, which are Koral's to look inside.
//

#pragma once

#include "accelerationStructure.h"
#include "api.h"
#include "buffer.h"
#include "bufferView.h"
#include "computePipeline.h"
#include "framebuffer.h"
#include "graphicsPipeline.h"
#include "image.h"
#include "imageView.h"
#include "mesh.h"
#include "rayTracingPipeline.h"
#include "sampler.h"
#include "shader.h"
#include "koral_c.h"
#include "resource.h"
#include "token.h"

namespace kor::capi
{
    /** @brief The image a KoralImage handle refers to. Throws (std::runtime_error) when it is not one. */
    KORAL_API ResourceRef<const Image> ImageOf(KoralResource* handle);
    /**
     * @brief The object a handle refers to, as the C++ interface has it: what code that builds through the C
     *        interface (kor::builders::Build) records commands with. Each throws (std::runtime_error) for a null
     *        handle or one of another kind. The reference is good while the resource is alive, not only the handle.
     */
    KORAL_API ResourceRef<const Buffer> BufferOf(KoralResource* handle);
    KORAL_API ResourceRef<const ImageView> ImageViewOf(KoralResource* handle);
    KORAL_API ResourceRef<const Sampler> SamplerOf(KoralResource* handle);
    KORAL_API ResourceRef<const BufferView> BufferViewOf(KoralResource* handle);
    KORAL_API ResourceRef<const Shader> ShaderOf(KoralResource* handle);
    KORAL_API ResourceRef<const GraphicsPipeline> GraphicsPipelineOf(KoralResource* handle);
    KORAL_API ResourceRef<const ComputePipeline> ComputePipelineOf(KoralResource* handle);
    KORAL_API ResourceRef<const RayTracingPipeline> RayTracingPipelineOf(KoralResource* handle);
    KORAL_API ResourceRef<const Framebuffer> FramebufferOf(KoralResource* handle);
    KORAL_API ResourceRef<const Mesh> MeshOf(KoralResource* handle);
    KORAL_API ResourceRef<const AccelerationStructure> AccelerationStructureOf(KoralResource* handle);
    /** @brief A borrowed KoralImage handle onto @p image, released with koral_resource_release; null for an empty ref. */
    KORAL_API KoralResource* BorrowImage(const ResourceRef<const Image>& image);
    /** @brief A KoralToken onto @p token, the caller's, freed with koral_token_destroy. */
    KORAL_API KoralToken* MakeToken(const Token& token);
    /** @brief The token a KoralToken handle holds. */
    KORAL_API Token TokenOf(const KoralToken* handle);
}
