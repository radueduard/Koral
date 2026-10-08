/*
 * Koral's C interface: the C++ API, object for object, for bindings to other languages.
 *
 * Plain C (C99), so any language with a C foreign-function interface can call it, and nothing about it
 * depends on the C++ compiler that built Koral. It is not a second API: every function is a member of a
 * C++ class, named `koral_<class>_<member>` in snake case — koral_image_builder_set_format is
 * kor::Image::Builder::SetFormat, koral_cmd_draw_indexed is kor::CommandBuffer::DrawIndexed — and does
 * what the C++ one does, so the C++ headers are its documentation. What is written here is only what
 * the translation itself adds.
 *
 * Conventions:
 *  - A function that can fail returns KoralStatus (or a null handle) and leaves the reason in
 *    koral_last_error(), per thread. Nothing throws across the boundary.
 *  - Resources (buffers, images, pipelines, ...) are KoralResource handles, one type for all of them;
 *    the typedefs below only name what each function expects, and a handle of the wrong kind is refused.
 *    A handle is either *owned* — what a builder's _build returns, a kor::Resource<T> — or *borrowed* —
 *    what a lookup returns, a kor::ResourceRef<const T>. Release every handle with
 *    koral_resource_release: that frees an owned resource, and only the handle of a borrowed one.
 *  - A builder is made with koral_<class>_builder_new, configured with its setters, turned into a
 *    resource with _build (which may be *poisoned*: see koral_resource_poisoned; never null, but for an
 *    exception at the boundary), and freed with koral_builder_destroy.
 *  - Enumerations and flags are passed as the C++ enumerators' values: kor::Image::Format::eRGBA8_UNORM
 *    is (uint32_t)kor::Image::Format::eRGBA8_UNORM. Flags are the bits of kor::Flags<E>.
 *  - Scenes, windows, inputs, clocks, frame graphs, passes and command buffers are borrowed pointers to
 *    the C++ objects, valid for as long as those are; a KoralScene handle kept past its scene is refused.
 *  - Strings passed in are copied; strings returned are valid until the next call on the same thread.
 *  - Everything runs on the thread that created the application, except a pass's `record` callback and
 *    the command-buffer calls made from it, which run on a worker thread alongside other passes.
 */

#ifndef KORAL_C_H
#define KORAL_C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==== results and the log ============================================================================ */

typedef enum KoralStatus { KORAL_OK = 0, KORAL_ERROR = 1 } KoralStatus;

/** Why the last call on this thread failed. Empty after a success. */
KORAL_API const char* koral_last_error(void);
/** Sets (or, with null, clears) koral_last_error: for a module's own C interface, so its failures are reported the same way. */
KORAL_API void koral_set_last_error(const char* message);

typedef enum KoralLogLevel { KORAL_LOG_INFO = 0, KORAL_LOG_WARN = 1, KORAL_LOG_ERROR = 2 } KoralLogLevel;
/** kor::log::Info / Warn / Error. */
KORAL_API void koral_log(KoralLogLevel level, const char* message);
/** kor::Describe(ErrorCode). */
KORAL_API const char* koral_error_describe(uint32_t code);

/* ==== resources ======================================================================================= */

typedef struct KoralResource KoralResource;
typedef KoralResource KoralBuffer;
typedef KoralResource KoralImage;
typedef KoralResource KoralImageView;
typedef KoralResource KoralSampler;
typedef KoralResource KoralBufferView;
typedef KoralResource KoralShader;
typedef KoralResource KoralGraphicsPipeline;
typedef KoralResource KoralComputePipeline;
typedef KoralResource KoralRayTracingPipeline;
typedef KoralResource KoralDescriptorSet;
typedef KoralResource KoralDescriptorSetLayout;
typedef KoralResource KoralFramebuffer;
typedef KoralResource KoralMesh;
typedef KoralResource KoralAccelerationStructure;

typedef enum KoralResourceKind {
    KORAL_RESOURCE_BUFFER = 1, KORAL_RESOURCE_IMAGE, KORAL_RESOURCE_IMAGE_VIEW, KORAL_RESOURCE_SAMPLER,
    KORAL_RESOURCE_BUFFER_VIEW, KORAL_RESOURCE_SHADER, KORAL_RESOURCE_GRAPHICS_PIPELINE,
    KORAL_RESOURCE_COMPUTE_PIPELINE, KORAL_RESOURCE_RAY_TRACING_PIPELINE, KORAL_RESOURCE_DESCRIPTOR_SET,
    KORAL_RESOURCE_DESCRIPTOR_SET_LAYOUT, KORAL_RESOURCE_FRAMEBUFFER, KORAL_RESOURCE_MESH,
    KORAL_RESOURCE_ACCELERATION_STRUCTURE
} KoralResourceKind;

/** Frees the handle — and, for an owned one, the resource. Null is fine. */
KORAL_API void koral_resource_release(KoralResource* resource);
/** A new, borrowed handle onto the same resource (a ResourceRef). */
KORAL_API KoralResource* koral_resource_borrow(KoralResource* resource);
KORAL_API KoralResourceKind koral_resource_kind(KoralResource* resource);
KORAL_API bool koral_resource_owned(KoralResource* resource);
/** Resource::Valid / ResourceRef::Alive / Poisoned / Name / SetName (owned only) / Retry. */
KORAL_API bool koral_resource_alive(KoralResource* resource);
KORAL_API bool koral_resource_valid(KoralResource* resource);
KORAL_API bool koral_resource_poisoned(KoralResource* resource);
KORAL_API const char* koral_resource_name(KoralResource* resource);
KORAL_API void koral_resource_set_name(KoralResource* resource, const char* name);
KORAL_API bool koral_resource_retry(KoralResource* resource);
/** Which resource it is: two handles onto the same one have the same identity. */
KORAL_API const void* koral_resource_identity(KoralResource* resource);
/** Its Failure(), when poisoned: ErrorCode, message, and the whole History(). */
KORAL_API uint32_t koral_resource_error_code(KoralResource* resource);
KORAL_API const char* koral_resource_error_message(KoralResource* resource);
KORAL_API const char* koral_resource_error_history(KoralResource* resource);

/* ---- builders --------------------------------------------------------------------------------------- */

typedef struct KoralBuilder KoralBuilder;
typedef KoralBuilder KoralBufferBuilder;
typedef KoralBuilder KoralImageBuilder;
typedef KoralBuilder KoralImageViewBuilder;
typedef KoralBuilder KoralSamplerBuilder;
typedef KoralBuilder KoralBufferViewBuilder;
typedef KoralBuilder KoralShaderBuilder;
typedef KoralBuilder KoralGraphicsPipelineBuilder;
typedef KoralBuilder KoralComputePipelineBuilder;
typedef KoralBuilder KoralRayTracingPipelineBuilder;
typedef KoralBuilder KoralDescriptorSetBuilder;
typedef KoralBuilder KoralFramebufferBuilder;
typedef KoralBuilder KoralMeshBuilder;
typedef KoralBuilder KoralAccelerationStructureBuilder;

KORAL_API void koral_builder_destroy(KoralBuilder* builder);
/** Builder::HasErrors: whether something set so far will make the build fail. */
KORAL_API bool koral_builder_has_errors(KoralBuilder* builder);

/* ---- kor::Buffer (sizes and offsets in bytes: the C++ Builder<std::byte>) ----------------------------- */

KORAL_API KoralBufferBuilder* koral_buffer_builder_new(void);
KORAL_API void koral_buffer_builder_set_instance_count(KoralBufferBuilder* builder, int64_t bytes);
KORAL_API void koral_buffer_builder_set_data(KoralBufferBuilder* builder, const void* data, uint64_t bytes);
KORAL_API void koral_buffer_builder_set_usage(KoralBufferBuilder* builder, uint32_t usage);
KORAL_API void koral_buffer_builder_set_type(KoralBufferBuilder* builder, uint32_t type);
KORAL_API void koral_buffer_builder_set_is_per_frame(KoralBufferBuilder* builder, bool value);
KORAL_API void koral_buffer_builder_set_shared_across_queues(KoralBufferBuilder* builder, bool shared);
KORAL_API KoralBuffer* koral_buffer_builder_build(KoralBufferBuilder* builder);

KORAL_API uint64_t koral_buffer_size(KoralBuffer* buffer);
KORAL_API uint32_t koral_buffer_usage_flags(KoralBuffer* buffer);
KORAL_API uint32_t koral_buffer_memory_type(KoralBuffer* buffer);
KORAL_API bool koral_buffer_is_host_visible(KoralBuffer* buffer);
KORAL_API bool koral_buffer_is_per_frame(KoralBuffer* buffer);
KORAL_API bool koral_buffer_is_shared_across_queues(KoralBuffer* buffer);
KORAL_API uint64_t koral_buffer_device_address(KoralBuffer* buffer);
KORAL_API uint32_t koral_buffer_copy_count(KoralBuffer* buffer);
KORAL_API KoralStatus koral_buffer_read(KoralBuffer* buffer, void* into, uint64_t bytes, uint64_t offset);
KORAL_API KoralStatus koral_buffer_write(KoralBuffer* buffer, const void* data, uint64_t bytes, uint64_t offset);

/**
 * Buffer::ReadAsync: starts copying @p bytes from @p offset out, without waiting, and returns what they
 * will be read from. @p done (the caller's) is signalled once they can be: then koral_readback_read.
 */
typedef struct KoralReadback KoralReadback;
typedef struct KoralToken KoralToken;
KORAL_API KoralReadback* koral_buffer_read_async(KoralBuffer* buffer, uint64_t bytes, uint64_t offset, KoralToken** done);
KORAL_API KoralStatus koral_readback_read(KoralReadback* readback, void* into);
KORAL_API void koral_readback_destroy(KoralReadback* readback);

/** Buffer::Map: a ConstMapping (mutable false) or a MutableMapping (true) over bytes, until released. */
typedef struct KoralMapping KoralMapping;
KORAL_API KoralMapping* koral_buffer_map(KoralBuffer* buffer, uint64_t bytes, uint64_t offset, bool mutable_);
KORAL_API void koral_mapping_release(KoralMapping* mapping);
/** Where the mapped bytes are (AsSpan), and how many. Reading is free; write through koral_mapping_write
    so that a per-frame buffer carries the write to its other copies, as MutableMapping::Write does. */
KORAL_API void* koral_mapping_data(KoralMapping* mapping);
KORAL_API uint64_t koral_mapping_size(KoralMapping* mapping);
KORAL_API KoralStatus koral_mapping_write(KoralMapping* mapping, const void* data, uint64_t bytes, uint64_t offset);
KORAL_API KoralStatus koral_mapping_flush(KoralMapping* mapping, uint64_t offset, uint64_t bytes);
KORAL_API KoralStatus koral_mapping_invalidate(KoralMapping* mapping, uint64_t offset, uint64_t bytes);

/* ---- kor::Image -------------------------------------------------------------------------------------- */

KORAL_API KoralImageBuilder* koral_image_builder_new(void);
KORAL_API void koral_image_builder_set_is_per_frame(KoralImageBuilder* builder, bool value);
KORAL_API void koral_image_builder_set_shared_across_queues(KoralImageBuilder* builder, bool shared);
KORAL_API void koral_image_builder_set_type(KoralImageBuilder* builder, uint32_t type);
KORAL_API void koral_image_builder_set_format(KoralImageBuilder* builder, uint32_t format);
KORAL_API void koral_image_builder_set_extent(KoralImageBuilder* builder, uint32_t x, uint32_t y, uint32_t z);
KORAL_API void koral_image_builder_set_mip_levels(KoralImageBuilder* builder, uint32_t levels);
KORAL_API void koral_image_builder_set_array_layers(KoralImageBuilder* builder, uint32_t layers);
KORAL_API void koral_image_builder_set_sample_count(KoralImageBuilder* builder, uint32_t samples);
KORAL_API void koral_image_builder_set_usage(KoralImageBuilder* builder, uint32_t usage);
KORAL_API void koral_image_builder_set_data(KoralImageBuilder* builder, const void* pixels, uint64_t bytes);
KORAL_API KoralImage* koral_image_builder_build(KoralImageBuilder* builder);

KORAL_API void koral_image_resize(KoralImage* image, uint32_t x, uint32_t y, uint32_t z);
KORAL_API uint64_t koral_image_generation(KoralImage* image);
KORAL_API void koral_image_extent(KoralImage* image, uint32_t* x, uint32_t* y, uint32_t* z);
KORAL_API uint32_t koral_image_image_type(KoralImage* image);
KORAL_API uint32_t koral_image_pixel_format(KoralImage* image);
KORAL_API bool koral_image_is_bgr_order(KoralImage* image);
KORAL_API uint32_t koral_image_samples(KoralImage* image);
KORAL_API uint32_t koral_image_usage_flags(KoralImage* image);
KORAL_API uint32_t koral_image_mip_levels(KoralImage* image);
KORAL_API uint32_t koral_image_array_layers(KoralImage* image);
KORAL_API bool koral_image_is_per_frame(KoralImage* image);
KORAL_API bool koral_image_is_shared_across_queues(KoralImage* image);
KORAL_API uint32_t koral_image_copy_index(KoralImage* image);
KORAL_API uint32_t koral_image_natural_shape(KoralImage* image);
/** Image::View(shape, coverage): borrowed. */
KORAL_API KoralImageView* koral_image_view(KoralImage* image, uint32_t shape, uint32_t coverage);
KORAL_API uint32_t koral_image_channel_size(uint32_t format);
KORAL_API uint32_t koral_image_channel_count(uint32_t format);
KORAL_API bool koral_image_is_format_supported(uint32_t format, uint32_t usage);
KORAL_API bool koral_image_is_block_compressed(uint32_t format);
KORAL_API void koral_image_block_extent(uint32_t format, uint32_t* x, uint32_t* y);
KORAL_API uint32_t koral_image_block_size(uint32_t format);
KORAL_API uint64_t koral_image_size_of_region(uint32_t format, uint32_t x, uint32_t y, uint32_t z, uint32_t layers);
KORAL_API bool koral_is_depth_stencil_format(uint32_t format);
KORAL_API bool koral_is_stencil_format(uint32_t format);

/* ---- kor::ImageView, kor::Sampler, kor::BufferView ---------------------------------------------------- */

KORAL_API KoralImageViewBuilder* koral_image_view_builder_new(KoralImage* image);
KORAL_API void koral_image_view_builder_set_view_type(KoralImageViewBuilder* builder, uint32_t type);
KORAL_API void koral_image_view_builder_set_base_mip_level(KoralImageViewBuilder* builder, uint32_t level);
KORAL_API void koral_image_view_builder_set_mip_level_count(KoralImageViewBuilder* builder, uint32_t count);
KORAL_API void koral_image_view_builder_set_base_array_layer(KoralImageViewBuilder* builder, uint32_t layer);
KORAL_API void koral_image_view_builder_set_array_layer_count(KoralImageViewBuilder* builder, uint32_t count);
/** Four kor::ImageView::Swizzle: r, g, b, a. */
KORAL_API void koral_image_view_builder_set_component_mapping(KoralImageViewBuilder* builder, const uint32_t swizzle[4]);
KORAL_API KoralImageView* koral_image_view_builder_build(KoralImageViewBuilder* builder);
KORAL_API KoralImage* koral_image_view_source_image(KoralImageView* view);
KORAL_API uint32_t koral_image_view_view_type(KoralImageView* view);
KORAL_API uint32_t koral_image_view_base_mip_level(KoralImageView* view);
KORAL_API uint32_t koral_image_view_mip_level_count(KoralImageView* view);
KORAL_API uint32_t koral_image_view_base_array_layer(KoralImageView* view);
KORAL_API uint32_t koral_image_view_array_layer_count(KoralImageView* view);
KORAL_API void koral_image_view_components(KoralImageView* view, uint32_t swizzle[4]);
KORAL_API bool koral_image_view_is_per_frame(KoralImageView* view);

KORAL_API KoralSamplerBuilder* koral_sampler_builder_new(void);
KORAL_API void koral_sampler_builder_set_min_filter(KoralSamplerBuilder* builder, uint32_t filter);
KORAL_API void koral_sampler_builder_set_mag_filter(KoralSamplerBuilder* builder, uint32_t filter);
KORAL_API void koral_sampler_builder_set_mipmap_mode(KoralSamplerBuilder* builder, uint32_t mode);
KORAL_API void koral_sampler_builder_set_address_mode_u(KoralSamplerBuilder* builder, uint32_t mode);
KORAL_API void koral_sampler_builder_set_address_mode_v(KoralSamplerBuilder* builder, uint32_t mode);
KORAL_API void koral_sampler_builder_set_address_mode_w(KoralSamplerBuilder* builder, uint32_t mode);
KORAL_API void koral_sampler_builder_set_mip_lod_bias(KoralSamplerBuilder* builder, float bias);
KORAL_API void koral_sampler_builder_set_anisotropy_enable(KoralSamplerBuilder* builder, bool enable);
KORAL_API void koral_sampler_builder_set_max_anisotropy(KoralSamplerBuilder* builder, float anisotropy);
KORAL_API void koral_sampler_builder_set_compare_enable(KoralSamplerBuilder* builder, bool enable);
KORAL_API void koral_sampler_builder_set_compare_op(KoralSamplerBuilder* builder, uint32_t op);
KORAL_API void koral_sampler_builder_set_min_lod(KoralSamplerBuilder* builder, float lod);
KORAL_API void koral_sampler_builder_set_max_lod(KoralSamplerBuilder* builder, float lod);
KORAL_API void koral_sampler_builder_set_unnormalized_coordinates(KoralSamplerBuilder* builder, bool value);
KORAL_API KoralSampler* koral_sampler_builder_build(KoralSamplerBuilder* builder);

KORAL_API KoralBufferViewBuilder* koral_buffer_view_builder_new(KoralBuffer* buffer);
KORAL_API void koral_buffer_view_builder_set_format(KoralBufferViewBuilder* builder, uint32_t format);
KORAL_API void koral_buffer_view_builder_set_offset(KoralBufferViewBuilder* builder, int64_t offset);
KORAL_API void koral_buffer_view_builder_set_range(KoralBufferViewBuilder* builder, int64_t range);
KORAL_API KoralBufferView* koral_buffer_view_builder_build(KoralBufferViewBuilder* builder);
KORAL_API KoralBuffer* koral_buffer_view_source_buffer(KoralBufferView* view);
KORAL_API uint32_t koral_buffer_view_pixel_format(KoralBufferView* view);
KORAL_API int64_t koral_buffer_view_offset(KoralBufferView* view);
KORAL_API int64_t koral_buffer_view_range(KoralBufferView* view);
KORAL_API uint64_t koral_buffer_view_texel_count(KoralBufferView* view);

/* ---- kor::Shader --------------------------------------------------------------------------------------- */

KORAL_API KoralShaderBuilder* koral_shader_builder_new(void);
KORAL_API void koral_shader_builder_set_stage(KoralShaderBuilder* builder, uint32_t stage);
KORAL_API void koral_shader_builder_set_path(KoralShaderBuilder* builder, const char* path);
/** SetEntryPoint(entry), or SetEntryPoint(module, entry) when @p module is not null. */
KORAL_API void koral_shader_builder_set_entry_point(KoralShaderBuilder* builder, const char* module, const char* entry);
KORAL_API void koral_shader_builder_set_lang(KoralShaderBuilder* builder, uint32_t lang);
KORAL_API KoralShader* koral_shader_builder_build(KoralShaderBuilder* builder);
/** GetOrBuild(identifier): borrowed — the repository keeps the shader. @p identifier may be null. */
KORAL_API KoralShader* koral_shader_builder_get_or_build(KoralShaderBuilder* builder, const char* identifier);
KORAL_API uint32_t koral_shader_shader_stage(KoralShader* shader);
KORAL_API uint32_t koral_shader_language(KoralShader* shader);
KORAL_API const char* koral_shader_source_path(KoralShader* shader);
KORAL_API void koral_shader_add_search_path(const char* directory, bool front);

/* What reflection says of a shader. Strings point into the shader's own reflection: valid until it is reloaded or destroyed. */

/** One descriptor the shader declares (kor::Shader::Descriptor), and where the compiler put it. */
typedef struct KoralShaderParameter {
    const char* name;                  /* as in the source; empty for a block declared without an instance name */
    const char* block_name;            /* the block's type name, for a uniform or storage buffer; otherwise empty */
    uint32_t type;                     /* kor::DescriptorType */
    uint32_t count;                    /* array elements, or 1 */
    uint32_t set, binding;             /* where the compiler put it; a pipeline may put it elsewhere (set_binding) */
    uint32_t access;                   /* kor::Shader::AccessKind: 0 read, 1 write, 2 both */
    uint32_t shape;                    /* kor::ImageShape, for an image */
    bool active;                       /* whether the entry point reaches it */
} KoralShaderParameter;
/** One field of a push-constant block (kor::Shader::PushConstantField), flattened: `model`, `material.albedo`, `weights[2]`. */
typedef struct KoralShaderPushConstant {
    const char* name;
    uint32_t offset, size;             /* within the pipeline's push-constant range */
    uint32_t scalar;                   /* 0 float, 1 int, 2 uint, 3 bool, 4 double, 5 anything else */
    uint32_t rows, columns, count;     /* vector components or matrix rows; matrix columns; array elements */
    bool aggregate;                    /* a struct or a whole array: written only as raw bytes */
} KoralShaderPushConstant;
/** A specialization constant (kor::Shader::SpecializationConstant). */
typedef struct KoralShaderSpecializationConstant {
    const char* name;
    uint32_t id;                       /* its constant_id */
    uint32_t scalar;                   /* as KoralShaderPushConstant's */
    uint32_t size;                     /* bytes in specialization data: a bool takes 4 */
    uint64_t default_value;            /* its bits, in the low size bytes */
} KoralShaderSpecializationConstant;

KORAL_API size_t koral_shader_parameter_count(KoralShader* shader);
/** Writes the @p index th descriptor into @p out; false when there is no such. */
KORAL_API bool koral_shader_parameter(KoralShader* shader, size_t index, KoralShaderParameter* out);
KORAL_API size_t koral_shader_push_constant_count(KoralShader* shader);
KORAL_API bool koral_shader_push_constant(KoralShader* shader, size_t index, KoralShaderPushConstant* out);
KORAL_API size_t koral_shader_specialization_constant_count(KoralShader* shader);
KORAL_API bool koral_shader_specialization_constant(KoralShader* shader, size_t index, KoralShaderSpecializationConstant* out);

/* ---- pipelines ---------------------------------------------------------------------------------------- */

/** kor::InputAssemblyState, kor::RasterizationState, kor::MultisampleState, kor::DepthStencilState and
    kor::ColorBlendState, field for field. */
typedef struct KoralInputAssemblyState { uint32_t topology; bool primitive_restart_enable; } KoralInputAssemblyState;
typedef struct KoralRasterizationState {
    bool depth_clamp_enable, rasterizer_discard_enable;
    uint32_t polygon_mode, cull_mode, front_face;
    bool depth_bias_enable;
    float depth_bias_constant_factor, depth_bias_clamp, depth_bias_slope_factor, line_width;
} KoralRasterizationState;
typedef struct KoralMultisampleState { uint32_t sample_count; bool sample_shading_enable; float min_sample_shading; } KoralMultisampleState;
typedef struct KoralStencilOpState {
    uint32_t fail_op, pass_op, depth_fail_op, compare_op, compare_mask, write_mask, reference;
} KoralStencilOpState;
typedef struct KoralDepthStencilState {
    bool depth_test_enable, depth_write_enable;
    uint32_t depth_compare_op;
    bool depth_bounds_enable, stencil_enable;
    KoralStencilOpState stencil_front, stencil_back;
    float min_depth, max_depth;
} KoralDepthStencilState;
typedef struct KoralColorBlendAttachment {
    bool blend_enable;
    uint32_t src_color_blend_factor, dst_color_blend_factor, color_blend_op;
    uint32_t src_alpha_blend_factor, dst_alpha_blend_factor, alpha_blend_op;
    uint32_t color_write_mask;
} KoralColorBlendAttachment;
typedef struct KoralColorBlendState {
    bool enable_logic_op;
    uint32_t logic_op;
    const KoralColorBlendAttachment* attachments;
    size_t attachment_count;
    float blend_constants[4];
} KoralColorBlendState;

/** kor::VertexLayout: its bindings, and attributes matched to shader inputs by semantic (or at a location, >= 0). */
typedef struct KoralVertexBinding {
    uint32_t binding, stride;
    uint32_t input_rate;               /* kor::VertexInputRate: 0 a vertex at a time, 1 an instance at a time */
} KoralVertexBinding;
typedef struct KoralVertexAttribute {
    const char* semantic;
    const char* semantic_namespace;
    uint32_t binding, offset, channel_type, channel_count;
    int64_t location;                  /* -1: matched by semantic */
    uint32_t locations;                /* consecutive locations it fills: 4 for a mat4, N for an array of N; 0 is 1 */
    uint32_t location_stride;          /* bytes from one of them to the next; 0: packed */
} KoralVertexAttribute;
typedef struct KoralVertexLayout {
    const KoralVertexBinding* bindings;
    size_t binding_count;
    const KoralVertexAttribute* attributes;
    size_t attribute_count;
    int64_t position_attribute;       /* -1: none */
} KoralVertexLayout;

KORAL_API KoralGraphicsPipelineBuilder* koral_graphics_pipeline_builder_new(void);
/** SetVertexShader(shader), or SetVertexShader(shader, layout) when @p layout is not null. */
KORAL_API void koral_graphics_pipeline_builder_set_vertex_shader(KoralGraphicsPipelineBuilder* builder, KoralShader* shader,
                                                                  const KoralVertexLayout* layout);
KORAL_API void koral_graphics_pipeline_builder_set_tessellation_state(KoralGraphicsPipelineBuilder* builder, KoralShader* control,
                                                                       KoralShader* evaluation, uint32_t patch_control_points);
KORAL_API void koral_graphics_pipeline_builder_set_geometry_shader(KoralGraphicsPipelineBuilder* builder, KoralShader* shader);
KORAL_API void koral_graphics_pipeline_builder_set_fragment_shader(KoralGraphicsPipelineBuilder* builder, KoralShader* shader);
KORAL_API void koral_graphics_pipeline_builder_set_task_shader(KoralGraphicsPipelineBuilder* builder, KoralShader* shader);
KORAL_API void koral_graphics_pipeline_builder_set_mesh_shader(KoralGraphicsPipelineBuilder* builder, KoralShader* shader);
KORAL_API void koral_graphics_pipeline_builder_set_input_assembly_state(KoralGraphicsPipelineBuilder* builder, const KoralInputAssemblyState* state);
KORAL_API void koral_graphics_pipeline_builder_set_rasterization_state(KoralGraphicsPipelineBuilder* builder, const KoralRasterizationState* state);
KORAL_API void koral_graphics_pipeline_builder_set_multisample_state(KoralGraphicsPipelineBuilder* builder, const KoralMultisampleState* state);
KORAL_API void koral_graphics_pipeline_builder_set_depth_stencil_state(KoralGraphicsPipelineBuilder* builder, const KoralDepthStencilState* state);
KORAL_API void koral_graphics_pipeline_builder_set_color_blend_state(KoralGraphicsPipelineBuilder* builder, const KoralColorBlendState* state);
KORAL_API void koral_graphics_pipeline_builder_set_framebuffer(KoralGraphicsPipelineBuilder* builder, KoralFramebuffer* framebuffer);
/** SetSpecializationConstant(id, value): a 4- or 8-byte value. */
KORAL_API void koral_graphics_pipeline_builder_set_specialization_constant(KoralGraphicsPipelineBuilder* builder, uint32_t id,
                                                                            const void* value, uint32_t bytes);
/** SetSpecializationConstant(name, value), by the name its shader gave it: a 4- or 8-byte value. */
KORAL_API void koral_graphics_pipeline_builder_set_specialization_constant_named(KoralGraphicsPipelineBuilder* builder, const char* name,
                                                                                  const void* value, uint32_t bytes);
/** SetBinding(name, set, binding): puts the descriptor @p name there, whatever its shader said. */
KORAL_API void koral_graphics_pipeline_builder_set_binding(KoralGraphicsPipelineBuilder* builder, const char* name, uint32_t set,
                                                            uint32_t binding);
KORAL_API KoralGraphicsPipeline* koral_graphics_pipeline_builder_build(KoralGraphicsPipelineBuilder* builder);

KORAL_API KoralComputePipelineBuilder* koral_compute_pipeline_builder_new(void);
KORAL_API void koral_compute_pipeline_builder_set_compute_shader(KoralComputePipelineBuilder* builder, KoralShader* shader);
KORAL_API void koral_compute_pipeline_builder_set_specialization_constant(KoralComputePipelineBuilder* builder, uint32_t id,
                                                                           const void* value, uint32_t bytes);
KORAL_API void koral_compute_pipeline_builder_set_specialization_constant_named(KoralComputePipelineBuilder* builder, const char* name,
                                                                                 const void* value, uint32_t bytes);
KORAL_API void koral_compute_pipeline_builder_set_binding(KoralComputePipelineBuilder* builder, const char* name, uint32_t set,
                                                           uint32_t binding);
KORAL_API KoralComputePipeline* koral_compute_pipeline_builder_build(KoralComputePipelineBuilder* builder);

KORAL_API KoralRayTracingPipelineBuilder* koral_ray_tracing_pipeline_builder_new(void);
KORAL_API void koral_ray_tracing_pipeline_builder_set_raygen_shader(KoralRayTracingPipelineBuilder* builder, KoralShader* shader);
KORAL_API void koral_ray_tracing_pipeline_builder_add_miss_shader(KoralRayTracingPipelineBuilder* builder, KoralShader* shader);
/** AddHitGroup: any of the three may be null. */
KORAL_API void koral_ray_tracing_pipeline_builder_add_hit_group(KoralRayTracingPipelineBuilder* builder, KoralShader* closest_hit,
                                                                 KoralShader* any_hit, KoralShader* intersection);
KORAL_API void koral_ray_tracing_pipeline_builder_add_callable_shader(KoralRayTracingPipelineBuilder* builder, KoralShader* shader);
KORAL_API void koral_ray_tracing_pipeline_builder_set_max_recursion_depth(KoralRayTracingPipelineBuilder* builder, uint32_t depth);
KORAL_API void koral_ray_tracing_pipeline_builder_set_specialization_constant(KoralRayTracingPipelineBuilder* builder, uint32_t id,
                                                                               const void* value, uint32_t bytes);
KORAL_API void koral_ray_tracing_pipeline_builder_set_specialization_constant_named(KoralRayTracingPipelineBuilder* builder, const char* name,
                                                                                     const void* value, uint32_t bytes);
KORAL_API void koral_ray_tracing_pipeline_builder_set_binding(KoralRayTracingPipelineBuilder* builder, const char* name, uint32_t set,
                                                               uint32_t binding);
KORAL_API KoralRayTracingPipeline* koral_ray_tracing_pipeline_builder_build(KoralRayTracingPipelineBuilder* builder);
KORAL_API uint32_t koral_ray_tracing_pipeline_max_recursion_depth(KoralRayTracingPipeline* pipeline);

/* Any of the three pipelines: */
/** Pipeline::SetLayoutRef(index): borrowed. */
KORAL_API KoralDescriptorSetLayout* koral_pipeline_set_layout(KoralResource* pipeline, uint32_t index);
KORAL_API bool koral_pipeline_uses_device_addresses(KoralResource* pipeline);
KORAL_API bool koral_pipeline_has_push_constant(KoralResource* pipeline, const char* name);
/** DescriptorSetLayout::FindBinding: the binding, or -1. */
KORAL_API int64_t koral_descriptor_set_layout_find_binding(KoralDescriptorSetLayout* layout, const char* name);

/* ---- kor::DescriptorSet -------------------------------------------------------------------------------- */

/** Builder(pipeline, set) for any pipeline, or Builder(layout) when @p pipeline is a layout. */
KORAL_API KoralDescriptorSetBuilder* koral_descriptor_set_builder_new(KoralResource* pipeline_or_layout, uint32_t set);
/*
 * The Write overloads. Each writes at @p name when it is not null ("textures[3]" for an element),
 * otherwise at @p binding, element @p index. Samplers may be null.
 */
KORAL_API void koral_descriptor_set_builder_write_buffer(KoralDescriptorSetBuilder* builder, const char* name, uint32_t binding,
                                                          uint32_t index, KoralBuffer* buffer);
KORAL_API void koral_descriptor_set_builder_write_buffer_slice(KoralDescriptorSetBuilder* builder, const char* name, uint32_t binding,
                                                                uint32_t index, KoralBuffer* buffer, int64_t offset, int64_t size);
KORAL_API void koral_descriptor_set_builder_write_buffer_view(KoralDescriptorSetBuilder* builder, const char* name, uint32_t binding,
                                                               uint32_t index, KoralBufferView* view);
KORAL_API void koral_descriptor_set_builder_write_image(KoralDescriptorSetBuilder* builder, const char* name, uint32_t binding,
                                                         uint32_t index, KoralImage* image, KoralSampler* sampler);
KORAL_API void koral_descriptor_set_builder_write_image_view(KoralDescriptorSetBuilder* builder, const char* name, uint32_t binding,
                                                              uint32_t index, KoralImageView* view, KoralSampler* sampler);
KORAL_API void koral_descriptor_set_builder_write_sampler(KoralDescriptorSetBuilder* builder, const char* name, uint32_t binding,
                                                           uint32_t index, KoralSampler* sampler);
KORAL_API void koral_descriptor_set_builder_write_acceleration_structure(KoralDescriptorSetBuilder* builder, const char* name,
                                                                          uint32_t binding, uint32_t index,
                                                                          KoralAccelerationStructure* structure);
KORAL_API KoralDescriptorSet* koral_descriptor_set_builder_build(KoralDescriptorSetBuilder* builder);
/* The Rebind overloads, the same way round. */
KORAL_API KoralStatus koral_descriptor_set_rebind_buffer(KoralDescriptorSet* set, const char* name, uint32_t binding, uint32_t index,
                                                         KoralBuffer* buffer);
KORAL_API KoralStatus koral_descriptor_set_rebind_buffer_slice(KoralDescriptorSet* set, const char* name, uint32_t binding,
                                                               uint32_t index, KoralBuffer* buffer, int64_t offset, int64_t size);
KORAL_API KoralStatus koral_descriptor_set_rebind_buffer_view(KoralDescriptorSet* set, const char* name, uint32_t binding,
                                                              uint32_t index, KoralBufferView* view);
KORAL_API KoralStatus koral_descriptor_set_rebind_image_view(KoralDescriptorSet* set, const char* name, uint32_t binding,
                                                             uint32_t index, KoralImageView* view, KoralSampler* sampler);
KORAL_API KoralStatus koral_descriptor_set_rebind_sampler(KoralDescriptorSet* set, const char* name, uint32_t binding,
                                                          uint32_t index, KoralSampler* sampler);
KORAL_API KoralStatus koral_descriptor_set_rebind_acceleration_structure(KoralDescriptorSet* set, const char* name, uint32_t binding,
                                                                         uint32_t index, KoralAccelerationStructure* structure);
KORAL_API KoralDescriptorSetLayout* koral_descriptor_set_layout(KoralDescriptorSet* set);

/* ---- kor::Framebuffer ------------------------------------------------------------------------------------ */

/** kor::ClearColor, the std::variant: a float, int or uint (scalar_type 0, 1, 2) vector of 1 to 4. */
typedef struct KoralClearColor {
    uint32_t scalar_type;
    uint32_t components;               /* 0: none (a RenderInfo index left to the framebuffer's own) */
    union { float f[4]; int32_t i[4]; uint32_t u[4]; } value;
} KoralClearColor;

KORAL_API KoralFramebufferBuilder* koral_framebuffer_builder_new(void);
/** AddColor: @p view is an ImageView or an Image (an AttachmentSource), @p resolve may be null. */
KORAL_API void koral_framebuffer_builder_add_color(KoralFramebufferBuilder* builder, const char* name, KoralResource* view,
                                                    KoralResource* resolve, const KoralClearColor* clear);
/** SetDepth, SetStencil or SetDepthStencil (which: 0, 1, 2). */
KORAL_API void koral_framebuffer_builder_set_depth_stencil(KoralFramebufferBuilder* builder, uint32_t which, const char* name,
                                                            KoralResource* view, KoralResource* resolve, float depth,
                                                            int32_t stencil);
KORAL_API void koral_framebuffer_builder_set_resolve_mode(KoralFramebufferBuilder* builder, uint32_t mode);
KORAL_API KoralFramebuffer* koral_framebuffer_builder_build(KoralFramebufferBuilder* builder);

KORAL_API bool koral_framebuffer_is_default(KoralFramebuffer* framebuffer);
KORAL_API uint32_t koral_framebuffer_color_attachment_count(KoralFramebuffer* framebuffer);
KORAL_API uint32_t koral_framebuffer_samples(KoralFramebuffer* framebuffer);
KORAL_API void koral_framebuffer_extent(KoralFramebuffer* framebuffer, uint32_t* x, uint32_t* y);
/* Borrowed, or null when there is none. */
KORAL_API KoralImageView* koral_framebuffer_color_attachment(KoralFramebuffer* framebuffer, uint32_t index);
KORAL_API bool koral_framebuffer_has_depth_attachment(KoralFramebuffer* framebuffer);
KORAL_API KoralImageView* koral_framebuffer_depth_attachment(KoralFramebuffer* framebuffer);
KORAL_API bool koral_framebuffer_has_stencil_attachment(KoralFramebuffer* framebuffer);
KORAL_API KoralImageView* koral_framebuffer_stencil_attachment(KoralFramebuffer* framebuffer);
KORAL_API bool koral_framebuffer_has_resolve_attachments(KoralFramebuffer* framebuffer);
KORAL_API KoralImageView* koral_framebuffer_resolve_attachment(KoralFramebuffer* framebuffer, uint32_t index);
KORAL_API KoralImageView* koral_framebuffer_attachment_named(KoralFramebuffer* framebuffer, const char* name);
KORAL_API KoralImage* koral_framebuffer_image_named(KoralFramebuffer* framebuffer, const char* name);
KORAL_API KoralImage* koral_framebuffer_color_image(KoralFramebuffer* framebuffer, uint32_t index);
KORAL_API KoralImage* koral_framebuffer_depth_image(KoralFramebuffer* framebuffer);
/** AttachmentNames, one at a time: how many there are, and the one at @p index. */
KORAL_API uint32_t koral_framebuffer_attachment_name_count(KoralFramebuffer* framebuffer);
KORAL_API const char* koral_framebuffer_attachment_name(KoralFramebuffer* framebuffer, uint32_t index);
KORAL_API void koral_framebuffer_clear_color_at(KoralFramebuffer* framebuffer, uint32_t index, KoralClearColor* color);
KORAL_API float koral_framebuffer_clear_depth(KoralFramebuffer* framebuffer);
KORAL_API int32_t koral_framebuffer_clear_stencil(KoralFramebuffer* framebuffer);
KORAL_API uint32_t koral_framebuffer_resolve_method(KoralFramebuffer* framebuffer);
KORAL_API void koral_framebuffer_resize(KoralFramebuffer* framebuffer, uint32_t x, uint32_t y);

/* ---- kor::Mesh, kor::AccelerationStructure ------------------------------------------------------------- */

KORAL_API KoralMeshBuilder* koral_mesh_builder_new(void);
KORAL_API void koral_mesh_builder_set_vertex_buffer(KoralMeshBuilder* builder, uint32_t binding, KoralBuffer* buffer);
KORAL_API void koral_mesh_builder_set_index_buffer(KoralMeshBuilder* builder, KoralBuffer* buffer, uint32_t index_type);
KORAL_API void koral_mesh_builder_set_vertex_layout(KoralMeshBuilder* builder, const KoralVertexLayout* layout);
KORAL_API KoralMesh* koral_mesh_builder_build(KoralMeshBuilder* builder);
KORAL_API uint64_t koral_mesh_vertex_count(KoralMesh* mesh);
KORAL_API bool koral_mesh_has_index_buffer(KoralMesh* mesh);
/* IndexCount / IndexType: false when the mesh has none. */
KORAL_API bool koral_mesh_index_count(KoralMesh* mesh, uint32_t* count);
KORAL_API bool koral_mesh_index_type(KoralMesh* mesh, uint32_t* type);
KORAL_API uint32_t koral_mesh_vertex_buffer_count(KoralMesh* mesh);
KORAL_API KoralBuffer* koral_mesh_vertex_buffer(KoralMesh* mesh, uint32_t index);
KORAL_API KoralBuffer* koral_mesh_index_buffer(KoralMesh* mesh);

KORAL_API KoralAccelerationStructureBuilder* koral_acceleration_structure_builder_new(void);
KORAL_API void koral_acceleration_structure_builder_add_mesh(KoralAccelerationStructureBuilder* builder, KoralMesh* mesh);
KORAL_API void koral_acceleration_structure_builder_add_geometry(KoralAccelerationStructureBuilder* builder, KoralMesh* mesh,
                                                                  uint64_t first_vertex, uint64_t vertex_count,
                                                                  uint64_t first_index, uint64_t index_count);
/** AddInstance: @p transform is 16 floats, column-major. */
KORAL_API void koral_acceleration_structure_builder_add_instance(KoralAccelerationStructureBuilder* builder,
                                                                  KoralAccelerationStructure* blas, const float transform[16],
                                                                  uint32_t custom_index, uint32_t hit_group_index);
KORAL_API KoralAccelerationStructure* koral_acceleration_structure_builder_build(KoralAccelerationStructureBuilder* builder);
KORAL_API uint32_t koral_acceleration_structure_structure_type(KoralAccelerationStructure* structure);

/* ==== kor::Token ======================================================================================= */

/* KoralToken: a copy of a kor::Token, the caller's, freed with koral_token_destroy (declared with ReadAsync above). */
KORAL_API KoralToken* koral_token_create(void);
KORAL_API KoralToken* koral_token_copy(KoralToken* token);
KORAL_API void koral_token_destroy(KoralToken* token);
KORAL_API bool koral_token_ready(KoralToken* token);
KORAL_API void koral_token_wait(KoralToken* token);
KORAL_API void koral_token_signal(KoralToken* token);
KORAL_API uint64_t koral_token_value(KoralToken* token);

/* ==== kor::CommandBuffer =================================================================================
 * Every command is the C++ member of the same name. The recording ones return nothing here: a command
 * that goes wrong is recorded on the command buffer (koral_cmd_ok, koral_cmd_error), as in C++.
 */

typedef struct KoralCommandBuffer KoralCommandBuffer;

/** kor::RenderInfo: null framebuffer is the window's (or view's) own. */
typedef struct KoralRenderInfo {
    KoralFramebuffer* framebuffer;
    uint32_t color_load, depth_load, stencil_load;        /* kor::LoadOperation */
    uint32_t color_store, depth_store, stencil_store;     /* kor::StoreOperation */
    const KoralClearColor* clear_colors;                  /* by attachment; components 0 leaves one alone */
    size_t clear_color_count;
    bool has_clear_depth;
    float clear_depth;
    bool has_clear_stencil;
    int32_t clear_stencil;
} KoralRenderInfo;

typedef struct KoralBufferBarrier { KoralBuffer* buffer; uint32_t dst_access; uint64_t offset, size; } KoralBufferBarrier;
/** Each optional field is -1 when absent. */
typedef struct KoralImageBarrier {
    KoralImage* image;
    uint32_t dst_access;
    int64_t base_mip_level, level_count, base_array_layer, layer_count;
} KoralImageBarrier;

typedef struct KoralBlit {
    int32_t src_offset[3], src_extent[3], dst_offset[3], dst_extent[3];
    uint32_t src_base_array_layer, dst_base_array_layer, layer_count, src_mip_level, dst_mip_level;
    uint32_t filtering;
} KoralBlit;
typedef struct KoralResolveInfo {
    int32_t src_offset[3], src_extent[3], dst_offset[3], dst_extent[3];
    uint32_t src_base_array_layer, dst_base_array_layer, layer_count, src_mip_level, dst_mip_level;
} KoralResolveInfo;
typedef struct KoralCopy {
    uint64_t buffer_offset, buffer_row_length, buffer_image_height;
    int32_t image_offset[3], image_extent[3];
    uint32_t image_base_array_layer, image_layer_count, image_mip_level;
} KoralCopy;

/** kor::ValueShape: what a push constant's value is, checked against what the shader declares. */
typedef struct KoralValueShape { uint32_t scalar, rows, columns, count; bool known; } KoralValueShape;

/* Command buffers of one's own (most are handed to a hook or pass). */
KORAL_API KoralCommandBuffer* koral_cmd_create(uint32_t usage);
KORAL_API void koral_cmd_destroy(KoralCommandBuffer* commands);
/** SingleTimeCommand: @p record is called with the command buffer, now; the token is the caller's. */
KORAL_API KoralToken* koral_cmd_single_time_command(void (*record)(KoralCommandBuffer* commands, void* user), void* user,
                                                     uint32_t usage);
KORAL_API void koral_cmd_begin(KoralCommandBuffer* commands);
KORAL_API void koral_cmd_end(KoralCommandBuffer* commands);
KORAL_API bool koral_cmd_is_recording(KoralCommandBuffer* commands);
KORAL_API KoralStatus koral_cmd_submit(KoralCommandBuffer* commands, KoralToken* const* wait_for, size_t wait_count,
                                        KoralToken* const* signal, size_t signal_count);
KORAL_API void koral_cmd_reset(KoralCommandBuffer* commands);
KORAL_API void koral_cmd_wait_for_fence(KoralCommandBuffer* commands);

/* What went wrong while recording (Ok, Errors). */
KORAL_API bool koral_cmd_ok(KoralCommandBuffer* commands);
KORAL_API uint32_t koral_cmd_error_count(KoralCommandBuffer* commands);
KORAL_API const char* koral_cmd_error(KoralCommandBuffer* commands, uint32_t index);
KORAL_API bool koral_cmd_has_touched(KoralCommandBuffer* commands, KoralImage* image);
/** CommandBuffer::ScreenImage: borrowed. */
KORAL_API KoralImage* koral_cmd_screen_image(void);

/* Timers. */
KORAL_API void koral_cmd_begin_timer(KoralCommandBuffer* commands, const char* label);
KORAL_API void koral_cmd_end_timer(KoralCommandBuffer* commands);
KORAL_API KoralStatus koral_cmd_collect_timer(KoralCommandBuffer* commands, const char* label, double* milliseconds);
/** CollectTimings: how many there are; koral_cmd_timing reads one (its label, milliseconds and depth). */
KORAL_API uint32_t koral_cmd_collect_timings(KoralCommandBuffer* commands);
KORAL_API const char* koral_cmd_timing(KoralCommandBuffer* commands, uint32_t index, double* milliseconds, uint32_t* depth);
KORAL_API bool koral_cmd_supports_timers(KoralCommandBuffer* commands);
KORAL_API uint64_t koral_cmd_last_frame_command_count(KoralCommandBuffer* commands);

KORAL_API void koral_cmd_begin_rendering(KoralCommandBuffer* commands, const KoralRenderInfo* info);
KORAL_API void koral_cmd_end_rendering(KoralCommandBuffer* commands);
KORAL_API void koral_cmd_set_viewport(KoralCommandBuffer* commands, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
KORAL_API void koral_cmd_set_scissor(KoralCommandBuffer* commands, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
KORAL_API void koral_cmd_set_line_width(KoralCommandBuffer* commands, float width);
KORAL_API void koral_cmd_set_depth_bias(KoralCommandBuffer* commands, float constant_factor, float clamp, float slope_factor);
KORAL_API void koral_cmd_set_blend_constants(KoralCommandBuffer* commands, const float constants[4]);
KORAL_API void koral_cmd_set_stencil_compare_mask(KoralCommandBuffer* commands, uint32_t face, uint32_t mask);
KORAL_API void koral_cmd_set_stencil_write_mask(KoralCommandBuffer* commands, uint32_t face, uint32_t mask);
KORAL_API void koral_cmd_set_stencil_reference(KoralCommandBuffer* commands, uint32_t face, uint32_t reference);
KORAL_API void koral_cmd_set_cull_mode(KoralCommandBuffer* commands, uint32_t cull_mode);
KORAL_API void koral_cmd_set_front_face(KoralCommandBuffer* commands, uint32_t front_face);
KORAL_API void koral_cmd_set_depth_test_enable(KoralCommandBuffer* commands, bool enable);
KORAL_API void koral_cmd_set_depth_write_enable(KoralCommandBuffer* commands, bool enable);
KORAL_API void koral_cmd_set_depth_compare_op(KoralCommandBuffer* commands, uint32_t op);
KORAL_API void koral_cmd_set_stencil_test_enable(KoralCommandBuffer* commands, bool enable);
KORAL_API void koral_cmd_set_stencil_op(KoralCommandBuffer* commands, uint32_t face, uint32_t fail_op, uint32_t pass_op,
                                         uint32_t depth_fail_op, uint32_t compare_op);
KORAL_API void koral_cmd_set_depth_bias_enable(KoralCommandBuffer* commands, bool enable);
KORAL_API void koral_cmd_set_rasterizer_discard_enable(KoralCommandBuffer* commands, bool enable);
KORAL_API void koral_cmd_set_primitive_restart_enable(KoralCommandBuffer* commands, bool enable);
KORAL_API void koral_cmd_bind_compute_pipeline(KoralCommandBuffer* commands, KoralComputePipeline* pipeline);
KORAL_API void koral_cmd_bind_graphics_pipeline(KoralCommandBuffer* commands, KoralGraphicsPipeline* pipeline);
KORAL_API void koral_cmd_bind_ray_tracing_pipeline(KoralCommandBuffer* commands, KoralRayTracingPipeline* pipeline);
KORAL_API void koral_cmd_bind_descriptor_set(KoralCommandBuffer* commands, uint32_t index, KoralDescriptorSet* set);
KORAL_API void koral_cmd_bind_mesh(KoralCommandBuffer* commands, KoralMesh* mesh);
/** BindVertexBuffer: @p buffer feeds vertex binding @p binding from @p offset bytes in — per-instance data beside a mesh. */
KORAL_API void koral_cmd_bind_vertex_buffer(KoralCommandBuffer* commands, uint32_t binding, KoralBuffer* buffer, uint64_t offset);
KORAL_API void koral_cmd_push_constant_block(KoralCommandBuffer* commands, const void* data, uint32_t bytes, uint32_t offset);
KORAL_API void koral_cmd_push_constant(KoralCommandBuffer* commands, const char* name, const void* data, uint32_t bytes,
                                        const KoralValueShape* shape);
KORAL_API void koral_cmd_barrier(KoralCommandBuffer* commands, const KoralBufferBarrier* buffers, size_t buffer_count,
                                  const KoralImageBarrier* images, size_t image_count);
KORAL_API void koral_cmd_begin_debug_label(KoralCommandBuffer* commands, const char* label, const float color[4]);
KORAL_API void koral_cmd_end_debug_label(KoralCommandBuffer* commands);
KORAL_API void koral_cmd_insert_debug_label(KoralCommandBuffer* commands, const char* label, const float color[4]);
KORAL_API void koral_cmd_dispatch(KoralCommandBuffer* commands, uint32_t x, uint32_t y, uint32_t z);
KORAL_API void koral_cmd_dispatch_indirect(KoralCommandBuffer* commands, KoralBuffer* buffer, uint64_t offset);
KORAL_API void koral_cmd_trace_rays(KoralCommandBuffer* commands, uint32_t width, uint32_t height, uint32_t depth);
KORAL_API void koral_cmd_draw(KoralCommandBuffer* commands, uint64_t vertex_count, uint32_t instance_count,
                               uint32_t first_vertex, uint32_t first_instance);
KORAL_API void koral_cmd_draw_indexed(KoralCommandBuffer* commands, uint64_t index_count, uint32_t instance_count,
                                       uint32_t first_index, int32_t vertex_offset, uint32_t first_instance);
KORAL_API void koral_cmd_draw_mesh(KoralCommandBuffer* commands, KoralMesh* mesh, uint32_t instance_count, uint32_t base_instance);
KORAL_API void koral_cmd_draw_sub_mesh(KoralCommandBuffer* commands, KoralMesh* mesh, uint32_t base_index, uint32_t index_count);
KORAL_API void koral_cmd_draw_mesh_tasks(KoralCommandBuffer* commands, uint32_t x, uint32_t y, uint32_t z);
KORAL_API void koral_cmd_draw_indirect(KoralCommandBuffer* commands, KoralBuffer* buffer, uint64_t offset, uint32_t draw_count,
                                        uint32_t stride);
KORAL_API void koral_cmd_draw_indexed_indirect(KoralCommandBuffer* commands, KoralBuffer* buffer, uint64_t offset,
                                                uint32_t draw_count, uint32_t stride);
/** Draws as many of @p buffer's commands as the uint32 at @p count_offset in @p count says, at most @p max_draw_count.
 *  Needs the kor::Feature::eDrawIndirectCount bit (0x8000) in required_features. */
KORAL_API void koral_cmd_draw_indirect_count(KoralCommandBuffer* commands, KoralBuffer* buffer, uint64_t offset, KoralBuffer* count,
                                              uint64_t count_offset, uint32_t max_draw_count, uint32_t stride);
KORAL_API void koral_cmd_draw_indexed_indirect_count(KoralCommandBuffer* commands, KoralBuffer* buffer, uint64_t offset, KoralBuffer* count,
                                                      uint64_t count_offset, uint32_t max_draw_count, uint32_t stride);
KORAL_API void koral_cmd_draw_mesh_tasks_indirect(KoralCommandBuffer* commands, KoralBuffer* buffer, uint64_t offset,
                                                   uint32_t draw_count, uint32_t stride);
KORAL_API void koral_cmd_clear_buffer(KoralCommandBuffer* commands, KoralBuffer* buffer, uint64_t offset, uint64_t size);
KORAL_API void koral_cmd_clear_color_image(KoralCommandBuffer* commands, KoralImage* image, const float color[4]);
KORAL_API void koral_cmd_fill_buffer(KoralCommandBuffer* commands, KoralBuffer* buffer, const void* data, uint64_t offset,
                                      uint64_t size);
KORAL_API void koral_cmd_copy_buffer(KoralCommandBuffer* commands, KoralBuffer* source, KoralBuffer* destination, uint64_t size,
                                      uint64_t source_offset, uint64_t destination_offset);
KORAL_API void koral_cmd_blit_to_screen(KoralCommandBuffer* commands, KoralImage* source, const KoralBlit* blit);
KORAL_API void koral_cmd_blit(KoralCommandBuffer* commands, KoralImage* source, KoralImage* destination, const KoralBlit* blit);
KORAL_API void koral_cmd_copy_image(KoralCommandBuffer* commands, KoralImage* source, KoralImage* destination);
KORAL_API void koral_cmd_resolve_to_screen(KoralCommandBuffer* commands, KoralImage* source, const KoralResolveInfo* resolve);
KORAL_API void koral_cmd_resolve(KoralCommandBuffer* commands, KoralImage* source, KoralImage* destination,
                                  const KoralResolveInfo* resolve);
KORAL_API void koral_cmd_generate_mipmaps(KoralCommandBuffer* commands, KoralImage* image);
KORAL_API void koral_cmd_copy_buffer_to_image(KoralCommandBuffer* commands, KoralBuffer* buffer, KoralImage* image,
                                               const KoralCopy* copy);
KORAL_API void koral_cmd_copy_image_to_buffer(KoralCommandBuffer* commands, KoralImage* image, KoralBuffer* buffer,
                                               const KoralCopy* copy);
/** Run: @p command is called with the command buffer when the backend runs it. */
KORAL_API void koral_cmd_run(KoralCommandBuffer* commands, void (*command)(KoralCommandBuffer* commands, void* user), void* user);

/* ==== the frame graph ======================================================================================= */

typedef struct KoralFrameGraph KoralFrameGraph;       /* borrowed: a scene's, or a view's */
typedef struct KoralRenderPass KoralRenderPass;       /* borrowed: valid while its graph has it */
typedef struct KoralPassBuilder KoralPassBuilder;     /* borrowed: during setup */
typedef struct KoralPassResources KoralPassResources; /* borrowed: during initialize */

/** The name the screen goes by in a graph: the scene's (or view's) window. */
#define KORAL_SCREEN "screen"

/**
 * kor::ResourceSet: images and buffers under one name, whose members change between frames. Made with
 * koral_resource_set_new and freed with koral_resource_set_destroy (a graph that imported it keeps it as long as
 * it needs it); the one koral_pass_resources_set_named hands out is the graph's, borrowed.
 */
typedef struct KoralResourceSet KoralResourceSet;
KORAL_API KoralResourceSet* koral_resource_set_new(void);
KORAL_API void koral_resource_set_destroy(KoralResourceSet* set);
/** Add: an image or a buffer. */
KORAL_API void koral_resource_set_add(KoralResourceSet* set, KoralResource* resource);
KORAL_API void koral_resource_set_remove(KoralResourceSet* set, KoralResource* resource);
KORAL_API void koral_resource_set_clear(KoralResourceSet* set);
KORAL_API uint32_t koral_resource_set_image_count(KoralResourceSet* set);
/** A borrowed handle onto its @p index th image: koral_resource_release frees the handle. */
KORAL_API KoralImage* koral_resource_set_image(KoralResourceSet* set, uint32_t index);
KORAL_API uint32_t koral_resource_set_buffer_count(KoralResourceSet* set);
KORAL_API KoralBuffer* koral_resource_set_buffer(KoralResourceSet* set, uint32_t index);
KORAL_API uint64_t koral_resource_set_generation(KoralResourceSet* set);

/**
 * A kor::RenderPass (or, with `run`, a kor::CpuPass): each hook optional but `record` (`run`), which runs on
 * a worker thread alongside other passes. `destroy` frees `user`, once.
 */
typedef struct KoralPassCallbacks {
    void* user;
    void (*setup)(KoralRenderPass* pass, KoralPassBuilder* builder, void* user);
    void (*initialize)(KoralRenderPass* pass, KoralPassResources* resources, void* user);
    void (*prepare)(KoralRenderPass* pass, void* user);
    void (*record)(KoralRenderPass* pass, KoralCommandBuffer* commands, void* user);
    void (*run)(KoralRenderPass* pass, void* user);       /* set instead of record: a CpuPass */
    void (*destroy)(void* user);
} KoralPassCallbacks;

/** kor::ImageDesc and kor::BufferDesc. */
typedef struct KoralImageDesc {
    uint32_t format, usage;
    float scale;
    const char* size_of;             /* may be null */
    bool has_extent;
    uint32_t extent[2];
    uint32_t mip_levels;
} KoralImageDesc;
typedef struct KoralBufferDesc { int64_t size; uint32_t usage, type; } KoralBufferDesc;

KORAL_API KoralRenderPass* koral_graph_add(KoralFrameGraph* graph, const char* name, const KoralPassCallbacks* pass);
KORAL_API void koral_graph_import_image(KoralFrameGraph* graph, const char* name, KoralImage* image);
KORAL_API void koral_graph_import_buffer(KoralFrameGraph* graph, const char* name, KoralBuffer* buffer);
/** ImportSet: @p set's members, whatever they are as passes record, under one name. The graph shares the set. */
KORAL_API void koral_graph_import_set(KoralFrameGraph* graph, const char* name, KoralResourceSet* set);
KORAL_API void koral_graph_invalidate(KoralFrameGraph* graph);
KORAL_API bool koral_graph_empty(KoralFrameGraph* graph);
/** Schedule, one entry at a time: the pass's name, level and whether it runs on the async queue. */
KORAL_API uint32_t koral_graph_schedule_count(KoralFrameGraph* graph);
KORAL_API const char* koral_graph_schedule(KoralFrameGraph* graph, uint32_t index, uint32_t* level, bool* async);
KORAL_API uint32_t koral_graph_culled_pass_count(KoralFrameGraph* graph);
KORAL_API const char* koral_graph_culled_pass(KoralFrameGraph* graph, uint32_t index);
KORAL_API uint32_t koral_graph_skipped_pass_count(KoralFrameGraph* graph);
/** A skipped pass: its name, and (through the out-parameters, valid until the next call) which resource and source. */
KORAL_API const char* koral_graph_skipped_pass(KoralFrameGraph* graph, uint32_t index, const char** resource, const char** source);
KORAL_API KoralImage* koral_graph_image_named(KoralFrameGraph* graph, const char* name, uint32_t usage);
KORAL_API void koral_graph_set_aliasing(KoralFrameGraph* graph, bool enabled);
KORAL_API bool koral_graph_aliasing(KoralFrameGraph* graph);
KORAL_API void koral_graph_memory(KoralFrameGraph* graph, uint64_t* bytes, uint64_t* unshared_bytes, uint32_t* resources,
                                   uint32_t* allocations);
KORAL_API void koral_graph_timing(KoralFrameGraph* graph, double* prepare_ms, double* record_wall_ms, double* record_work_ms,
                                   double* gpu_ms);
KORAL_API bool koral_graph_has_previous(KoralFrameGraph* graph, const char* name);

KORAL_API const char* koral_pass_name(KoralRenderPass* pass);
KORAL_API bool koral_pass_enabled(KoralRenderPass* pass);
KORAL_API void koral_pass_set_enabled(KoralRenderPass* pass, bool enabled);
/** The protected members, for a pass's own hooks. */
KORAL_API bool koral_pass_has_previous(KoralRenderPass* pass, const char* name);
KORAL_API void koral_pass_request_initialize(KoralRenderPass* pass);

/* PassBuilder: kind 0 names a resource without saying which (Read(name)), 1 an image, 2 a buffer. */
KORAL_API void koral_pass_builder_read(KoralPassBuilder* builder, const char* name, uint32_t kind, uint32_t usage);
KORAL_API void koral_pass_builder_write(KoralPassBuilder* builder, const char* name, uint32_t kind, uint32_t usage);
KORAL_API void koral_pass_builder_read_previous(KoralPassBuilder* builder, const char* name, uint32_t kind, uint32_t usage);
KORAL_API void koral_pass_builder_consume(KoralPassBuilder* builder, const char* name, const char* as, uint32_t kind, uint32_t usage);
KORAL_API void koral_pass_builder_create_image(KoralPassBuilder* builder, const char* name, const KoralImageDesc* desc);
KORAL_API void koral_pass_builder_create_buffer(KoralPassBuilder* builder, const char* name, const KoralBufferDesc* desc);
KORAL_API void koral_pass_builder_side_effect(KoralPassBuilder* builder);
KORAL_API void koral_pass_builder_async_compute(KoralPassBuilder* builder);

/* PassResources: borrowed, valid until the pass is initialized again. */
KORAL_API KoralImage* koral_pass_resources_image_named(KoralPassResources* resources, const char* name);
KORAL_API KoralBuffer* koral_pass_resources_buffer_named(KoralPassResources* resources, const char* name);
KORAL_API KoralBuffer* koral_pass_resources_writable_buffer_named(KoralPassResources* resources, const char* name);
KORAL_API void koral_pass_resources_extent(KoralPassResources* resources, const char* name, uint32_t* x, uint32_t* y);
KORAL_API KoralImage* koral_pass_resources_previous_image_named(KoralPassResources* resources, const char* name);
KORAL_API KoralBuffer* koral_pass_resources_previous_buffer_named(KoralPassResources* resources, const char* name);
/** SetNamed: the set imported as @p name, borrowed, or null. */
KORAL_API KoralResourceSet* koral_pass_resources_set_named(KoralPassResources* resources, const char* name);

/* ==== the application and its scenes ======================================================================== */

typedef struct KoralScene KoralScene;
typedef struct KoralWindow KoralWindow;
typedef struct KoralInput KoralInput;
typedef struct KoralTime KoralTime;
typedef struct KoralDebugDraw KoralDebugDraw;
typedef struct KoralView KoralView;

typedef enum KoralPlatform {
    KORAL_PLATFORM_AUTO = 0, KORAL_PLATFORM_X11 = 1, KORAL_PLATFORM_WAYLAND = 2,
    KORAL_PLATFORM_NONE = 3     /* no windowing system: offscreen scenes only, no display needed */
} KoralPlatform;

/** kor::AppSettings, kor::WindowSettings and kor::OffscreenSettings. The _default functions give the C++
    defaults, to change from — a zeroed struct is not them. */
typedef struct KoralAppSettings {
    uint32_t api;
    KoralPlatform platform;
    uint32_t frames_in_flight;
    const char* gpu;
    uint64_t required_features;       /* kor::Feature bits: the device must have them */
    uint64_t optional_features;       /* kor::Feature bits: enabled where the GPU has them */
} KoralAppSettings;
typedef struct KoralWindowSettings {
    const char* title;
    uint32_t extent[2];
    bool resizable, fullscreen, decorated, transparent_framebuffer, vsync;
    const uint32_t* formats;          /* kor::Window::Format, most wanted first */
    size_t format_count;
} KoralWindowSettings;
typedef struct KoralOffscreenSettings {
    const char* title;
    uint32_t extent[2];
    uint32_t format;
} KoralOffscreenSettings;
KORAL_API KoralAppSettings koral_app_settings_default(void);
KORAL_API KoralWindowSettings koral_window_settings_default(void);
KORAL_API KoralOffscreenSettings koral_offscreen_settings_default(void);

/**
 * A kor::Scene written as callbacks: its hooks, each optional. `user` is handed back to every one;
 * `destroy` is called once, last, to free it. State kept across a reload is JSON: save_state's return is
 * copied at once, and handed to load_state before initialize.
 */
typedef struct KoralSceneCallbacks {
    void* user;
    void (*initialize)(KoralScene* scene, void* user);
    void (*fixed_update)(KoralScene* scene, void* user);
    void (*update)(KoralScene* scene, void* user);
    void (*late_update)(KoralScene* scene, void* user);
    void (*render)(KoralScene* scene, KoralCommandBuffer* commands, void* user);
    void (*on_resize)(KoralScene* scene, uint32_t width, uint32_t height, void* user);
    void (*on_suspend)(KoralScene* scene, void* user);
    void (*on_resume)(KoralScene* scene, void* user);
    bool (*on_close_requested)(KoralScene* scene, void* user);   /* null: close */
    void (*shutdown)(KoralScene* scene, void* user);
    const char* (*save_state)(void* user);
    void (*load_state)(const char* json, void* user);
    void (*destroy)(void* user);
} KoralSceneCallbacks;

/**
 * Makes a scene from the arguments it is opened with (a JSON object of strings). A factory that cannot
 * make its scene returns callbacks with every member zero, having logged why: the scene is not opened.
 */
typedef KoralSceneCallbacks (*KoralSceneFactory)(const char* arguments_json, void* factory_user);

/**
 * Runs @p body on the process's first thread, and returns once it has. On macOS windows open only there, and a
 * program whose main() runs elsewhere — a JVM's, unless started with -XstartOnFirstThread — runs its whole
 * application through this: the thread it is on waits, while the first thread (parked in its run loop by the
 * JVM's launcher) runs it. Anywhere else, and on the first thread already, it simply calls @p body.
 */
typedef void (*KoralMainThreadBody)(void* user);
KORAL_API void koral_run_on_main_thread(KoralMainThreadBody body, void* user);

KORAL_API KoralStatus koral_app_create(const KoralAppSettings* settings);
KORAL_API void koral_app_destroy(void);
KORAL_API bool koral_app_exists(void);
KORAL_API KoralStatus koral_app_register(const char* name, KoralSceneFactory factory, void* factory_user);
KORAL_API KoralStatus koral_app_load_library(const char* path);
KORAL_API KoralStatus koral_app_unload_library(const char* path);
KORAL_API KoralStatus koral_app_reload_library(const char* path);
KORAL_API KoralStatus koral_app_reload_scenes(const char* const* names, size_t count);
/** SceneNames, one at a time. */
KORAL_API uint32_t koral_app_scene_name_count(void);
KORAL_API const char* koral_app_scene_name(uint32_t index);
/* Open / OpenOffscreen, by name (settings may be null: the defaults), or a scene made here (Open(name, scene, window)). */
KORAL_API KoralScene* koral_app_open(const char* name, const KoralWindowSettings* window, const char* arguments_json);
KORAL_API KoralScene* koral_app_open_scene(const char* name, const KoralSceneCallbacks* scene, const KoralWindowSettings* window);
KORAL_API KoralScene* koral_app_open_offscreen(const char* name, const KoralOffscreenSettings* target, const char* arguments_json);
KORAL_API KoralScene* koral_app_open_offscreen_scene(const char* name, const KoralSceneCallbacks* scene,
                                                     const KoralOffscreenSettings* target);
/** Scenes: up to @p capacity of them into @p out; returns how many there are. */
KORAL_API size_t koral_app_scenes(KoralScene** out, size_t capacity);
KORAL_API bool koral_app_is_open(KoralScene* scene);
KORAL_API int koral_app_run(void);
KORAL_API bool koral_app_frame(void);
KORAL_API void koral_app_quit(void);
KORAL_API void koral_app_replace(KoralScene* shown, const char* name, const char* arguments_json);
KORAL_API void koral_app_push(KoralScene* over, const char* name, const char* arguments_json);
KORAL_API void koral_app_pop(KoralScene* shown);
KORAL_API void koral_app_close(KoralScene* shown);

/* kor::Navigator. */
KORAL_API KoralScene* koral_navigator_open(const char* name, const KoralWindowSettings* window, const char* arguments_json);
KORAL_API KoralScene* koral_navigator_open_offscreen(const char* name, const KoralOffscreenSettings* target,
                                                     const char* arguments_json);
KORAL_API void koral_navigator_replace(const char* name, const char* arguments_json);
KORAL_API void koral_navigator_push(const char* name, const char* arguments_json);
KORAL_API void koral_navigator_pop(void);
KORAL_API void koral_navigator_close(void);
KORAL_API void koral_navigator_quit(void);

/* kor::Scene. */
KORAL_API KoralScene* koral_scene_current(void);
KORAL_API const char* koral_scene_name(KoralScene* scene);
KORAL_API KoralFrameGraph* koral_scene_graph(KoralScene* scene);
KORAL_API KoralWindow* koral_scene_scene_window(KoralScene* scene);
KORAL_API KoralInput* koral_scene_scene_input(KoralScene* scene);
KORAL_API KoralTime* koral_scene_scene_time(KoralScene* scene);
KORAL_API KoralDebugDraw* koral_scene_scene_debug(KoralScene* scene);
KORAL_API const char* koral_scene_save_state(KoralScene* scene);
KORAL_API KoralStatus koral_scene_load_state(KoralScene* scene, const char* json);
KORAL_API KoralView* koral_scene_add_view(KoralScene* scene, const char* name, const KoralOffscreenSettings* target);
KORAL_API void koral_scene_remove_view(KoralScene* scene, const char* name);
KORAL_API KoralView* koral_scene_find_view(KoralScene* scene, const char* name);
KORAL_API uint32_t koral_scene_view_count(KoralScene* scene);
KORAL_API KoralView* koral_scene_view(KoralScene* scene, uint32_t index);

/**
 * Scene::Life: a weak reference to a scene, which knows when the scene is gone — even if another scene has
 * since been made at the same address (as a reload's often is), which a KoralScene* alone cannot tell.
 */
typedef struct KoralSceneLife KoralSceneLife;
KORAL_API KoralSceneLife* koral_scene_life(KoralScene* scene);
/** The scene, or null once it is gone. */
KORAL_API KoralScene* koral_scene_life_scene(KoralSceneLife* life);
KORAL_API void koral_scene_life_release(KoralSceneLife* life);

/**
 * detail::SceneScope: makes @p scene the current one until the scope is exited — what a kor::Task does
 * around each resumption, for a binding resuming its own coroutines. Scopes nest; exit them in reverse.
 */
typedef struct KoralSceneScope KoralSceneScope;
KORAL_API KoralSceneScope* koral_scene_scope_enter(KoralScene* scene);
KORAL_API void koral_scene_scope_exit(KoralSceneScope* scope);

/* kor::View. */
KORAL_API const char* koral_view_name(KoralView* view);
KORAL_API KoralFrameGraph* koral_view_graph(KoralView* view);
KORAL_API KoralWindow* koral_view_target(KoralView* view);
KORAL_API KoralImage* koral_view_image(KoralView* view);
KORAL_API void koral_view_resize(KoralView* view, uint32_t x, uint32_t y);
KORAL_API bool koral_view_enabled(KoralView* view);
KORAL_API void koral_view_set_enabled(KoralView* view, bool enabled);

/* The current scene's own (Scene::Window::Get() and the rest): null outside a scene. */
KORAL_API KoralWindow* koral_current_window(void);
KORAL_API KoralInput* koral_current_input(void);
KORAL_API KoralTime* koral_current_time(void);
KORAL_API KoralDebugDraw* koral_current_debug(void);

/* ==== kor::Window ==================================================================================== */

KORAL_API bool koral_window_should_close(KoralWindow* window);
KORAL_API void koral_window_close(KoralWindow* window);
KORAL_API bool koral_window_is_offscreen(KoralWindow* window);
KORAL_API KoralImage* koral_window_image(KoralWindow* window);
KORAL_API void koral_window_resize(KoralWindow* window, uint32_t x, uint32_t y);
KORAL_API void koral_window_extent(KoralWindow* window, uint32_t* x, uint32_t* y);
KORAL_API bool koral_window_is_paused(KoralWindow* window);
KORAL_API bool koral_window_is_resizable(KoralWindow* window);
KORAL_API bool koral_window_is_fullscreen(KoralWindow* window);
KORAL_API bool koral_window_is_decorated(KoralWindow* window);
KORAL_API bool koral_window_is_vsync(KoralWindow* window);
KORAL_API bool koral_window_is_framebuffer_transparent(KoralWindow* window);
KORAL_API uint32_t koral_window_pixel_format(KoralWindow* window);
KORAL_API void koral_window_pause(KoralWindow* window);
KORAL_API void koral_window_unpause(KoralWindow* window);
KORAL_API void koral_window_set_title(KoralWindow* window, const char* title);
KORAL_API const char* koral_window_title(KoralWindow* window);
KORAL_API KoralFramebuffer* koral_window_default_framebuffer(KoralWindow* window);
KORAL_API bool koral_window_has_resized(KoralWindow* window);
KORAL_API void koral_window_set_icon(KoralWindow* window, const char* path);
KORAL_API bool koral_window_is_focused(KoralWindow* window);
KORAL_API bool koral_window_is_shown_this_frame(KoralWindow* window);

/* ==== kor::Input ======================================================================================== */

/** kor::InputSource: a key, mouse button, gamepad button or axis (kind 0-3), times @p scale. */
typedef struct KoralInputSource { uint32_t kind, code; float scale; } KoralInputSource;
KORAL_API bool koral_input_source_parse(const char* name, KoralInputSource* source);
KORAL_API const char* koral_input_source_name(const KoralInputSource* source);

KORAL_API uint32_t koral_input_state_of(KoralInput* input, uint32_t key);
KORAL_API uint32_t koral_input_mouse_button_state(KoralInput* input, uint32_t button);
KORAL_API bool koral_input_first_key_pressed(KoralInput* input, uint32_t* key);
KORAL_API bool koral_input_first_mouse_button_pressed(KoralInput* input, uint32_t* button);
KORAL_API bool koral_input_interface_wants_mouse(KoralInput* input);
KORAL_API bool koral_input_interface_wants_keyboard(KoralInput* input);
KORAL_API void koral_input_mouse_position(KoralInput* input, float* x, float* y);
KORAL_API void koral_input_mouse_position_delta(KoralInput* input, float* x, float* y);
KORAL_API void koral_input_mouse_scroll_delta(KoralInput* input, float* x, float* y);
KORAL_API void koral_input_last_mouse_position(KoralInput* input, float* x, float* y);
KORAL_API void koral_input_set_cursor_mode(KoralInput* input, uint32_t mode);
KORAL_API uint32_t koral_input_current_cursor_mode(KoralInput* input);
KORAL_API const char* koral_input_describe_key(uint32_t key);
KORAL_API const char* koral_input_describe_mouse_button(uint32_t button);
KORAL_API bool koral_input_is_gamepad_connected(KoralInput* input, int pad);
KORAL_API const char* koral_input_gamepad_name(KoralInput* input, int pad);
KORAL_API uint32_t koral_input_gamepad_button_state(KoralInput* input, uint32_t button, int pad);
KORAL_API float koral_input_gamepad_axis_value(KoralInput* input, uint32_t axis, int pad);
KORAL_API void koral_input_set_gamepad_dead_zone(KoralInput* input, float dead_zone);
KORAL_API void koral_input_bind_action(KoralInput* input, const char* action, const KoralInputSource* sources, size_t count);
KORAL_API void koral_input_bind_axis(KoralInput* input, const char* axis, const KoralInputSource* sources, size_t count);
KORAL_API uint32_t koral_input_action_state(KoralInput* input, const char* action);
KORAL_API float koral_input_axis(KoralInput* input, const char* axis);
KORAL_API void koral_input_axis_2d(KoralInput* input, const char* x, const char* y, float* out_x, float* out_y);
/** Bindings / SetBindings, as the JSON of kor::InputBindings. */
KORAL_API const char* koral_input_bindings(KoralInput* input);
KORAL_API KoralStatus koral_input_set_bindings(KoralInput* input, const char* json);
KORAL_API void koral_input_feed_key(KoralInput* input, uint32_t key, bool down);
KORAL_API void koral_input_feed_mouse_button(KoralInput* input, uint32_t button, bool down);
KORAL_API void koral_input_feed_mouse_position(KoralInput* input, float x, float y);
KORAL_API void koral_input_feed_mouse_delta(KoralInput* input, float x, float y);
KORAL_API void koral_input_feed_scroll(KoralInput* input, float x, float y);
/** Input::FeedText: UTF-8 text, as code points the scene's TypedText() will hold. */
KORAL_API void koral_input_feed_text(KoralInput* input, const char* text);
KORAL_API void koral_input_feed_key_repeat(KoralInput* input, uint32_t key);
/** Input::TypedText, as UTF-8. */
KORAL_API const char* koral_input_typed_text(KoralInput* input);
KORAL_API bool koral_input_is_key_repeated(KoralInput* input, uint32_t key);
KORAL_API void koral_input_feed_gamepad_button(KoralInput* input, uint32_t button, bool down, int pad);
KORAL_API void koral_input_feed_gamepad_axis(KoralInput* input, uint32_t axis, float value, int pad);
KORAL_API void koral_input_release_all(KoralInput* input);

/* ==== kor::Time ========================================================================================= */

KORAL_API float koral_time_frame_time(KoralTime* time);
KORAL_API float koral_time_unscaled_frame_time(KoralTime* time);
KORAL_API float koral_time_fixed_delta_time(KoralTime* time);
KORAL_API void koral_time_set_fixed_delta_time(KoralTime* time, float seconds);
KORAL_API float koral_time_fixed_step_fraction(KoralTime* time);
KORAL_API bool koral_time_in_fixed_step(KoralTime* time);
KORAL_API float koral_time_elapsed(KoralTime* time);
KORAL_API uint64_t koral_time_frame_count(KoralTime* time);
KORAL_API float koral_time_time_scale(KoralTime* time);
KORAL_API void koral_time_set_time_scale(KoralTime* time, float scale);

/* ==== kor::DebugDraw ===================================================================================== */

/**
 * kor::DebugStyle. A fill with an alpha of 0 is none; @p fill_only leaves the outline out (kor::DebugStyle::outline =
 * false); @p line_width is in pixels, and 0 is 1.
 */
typedef struct KoralDebugStyle { float color[4]; float duration; bool on_top; float fill[4]; bool fill_only; float line_width; } KoralDebugStyle;
KORAL_API KoralDebugStyle koral_debug_style_default(void);

/* Vectors are 3 floats; matrices 16, column-major. A null style is the default one. */
KORAL_API void koral_debug_line(KoralDebugDraw* draw, const float from[3], const float to[3], const KoralDebugStyle* style);
KORAL_API void koral_debug_box(KoralDebugDraw* draw, const float min[3], const float max[3], const KoralDebugStyle* style);
KORAL_API void koral_debug_box_transform(KoralDebugDraw* draw, const float transform[16], const KoralDebugStyle* style);
KORAL_API void koral_debug_circle(KoralDebugDraw* draw, const float center[3], const float normal[3], float radius,
                                  const KoralDebugStyle* style, int segments);
KORAL_API void koral_debug_sphere(KoralDebugDraw* draw, const float center[3], float radius, const KoralDebugStyle* style,
                                  int segments);
KORAL_API void koral_debug_arrow(KoralDebugDraw* draw, const float from[3], const float to[3], const KoralDebugStyle* style);
KORAL_API void koral_debug_point(KoralDebugDraw* draw, const float position[3], float size, const KoralDebugStyle* style);
KORAL_API void koral_debug_axes(KoralDebugDraw* draw, const float transform[16], float size, float duration);
KORAL_API void koral_debug_grid(KoralDebugDraw* draw, const float center[3], float size, int cells, const KoralDebugStyle* style);
KORAL_API void koral_debug_frustum(KoralDebugDraw* draw, const float view_projection[16], const KoralDebugStyle* style);
KORAL_API void koral_debug_clear(KoralDebugDraw* draw);
KORAL_API uint64_t koral_debug_line_count(KoralDebugDraw* draw);
KORAL_API void koral_debug_triangle(KoralDebugDraw* draw, const float a[3], const float b[3], const float c[3], const KoralDebugStyle* style);
KORAL_API void koral_debug_quad(KoralDebugDraw* draw, const float a[3], const float b[3], const float c[3], const float d[3],
                                const KoralDebugStyle* style);
KORAL_API void koral_debug_plane(KoralDebugDraw* draw, const float center[3], const float normal[3], const float size[2],
                                 const KoralDebugStyle* style);
KORAL_API void koral_debug_cylinder(KoralDebugDraw* draw, const float from[3], const float to[3], float radius,
                                    const KoralDebugStyle* style, int segments);
KORAL_API void koral_debug_cone(KoralDebugDraw* draw, const float base[3], const float tip[3], float radius, const KoralDebugStyle* style,
                                int segments);
KORAL_API void koral_debug_capsule(KoralDebugDraw* draw, const float from[3], const float to[3], float radius,
                                   const KoralDebugStyle* style, int segments);
KORAL_API void koral_debug_camera(KoralDebugDraw* draw, const float view[16], const float projection[16], float size,
                                  const KoralDebugStyle* style);
KORAL_API void koral_debug_point_light(KoralDebugDraw* draw, const float position[3], float range, const KoralDebugStyle* style);
KORAL_API void koral_debug_spot_light(KoralDebugDraw* draw, const float position[3], const float direction[3], float range,
                                      float outer_angle, float inner_angle, const KoralDebugStyle* style);
KORAL_API void koral_debug_directional_light(KoralDebugDraw* draw, const float position[3], const float direction[3], float size,
                                             const KoralDebugStyle* style);
KORAL_API uint64_t koral_debug_triangle_count(KoralDebugDraw* draw);

/** kor::GizmoPointer: @p has_position false when the pointer is not over the image (or is something else's). */
typedef struct KoralGizmoPointer { float position[2]; bool has_position; float viewport[2]; bool down, pressed; } KoralGizmoPointer;
/** kor::GizmoOptions; space is a kor::GizmoSpace. */
typedef struct KoralGizmoOptions { uint32_t space; float size; float snap; } KoralGizmoOptions;
KORAL_API KoralGizmoOptions koral_gizmo_options_default(void);
/**
 * kor::DebugDraw::Gizmo: @p mode is a kor::GizmoMode; @p transform (16 floats) is read and, while a handle is
 * dragged, written. A null options is the default ones. Returns whether @p transform changed.
 */
KORAL_API bool koral_debug_gizmo(KoralDebugDraw* draw, uint32_t mode, float transform[16], const float view_projection[16],
                                 const KoralGizmoPointer* pointer, const KoralGizmoOptions* options, uint64_t id);
KORAL_API bool koral_debug_gizmo_active(KoralDebugDraw* draw);
KORAL_API bool koral_debug_gizmo_hovered(KoralDebugDraw* draw);
/** kor::Scene::Debug::Gizmo: the current scene's gizmo, with its own mouse over its window. */
KORAL_API bool koral_current_gizmo(uint32_t mode, float transform[16], const float view_projection[16], const KoralGizmoOptions* options,
                                   uint64_t id);
/**
 * Adds a kor::DebugDrawPass to @p graph: @p draw's lines into @p target (null: the screen), tested against
 * the depth image @p depth (null: on top), with the camera @p view_projection writes each frame (16 floats,
 * column-major). @p destroy (may be null) frees @p user when the pass goes.
 */
KORAL_API KoralRenderPass* koral_graph_add_debug_draw_pass(KoralFrameGraph* graph, KoralDebugDraw* draw,
                                                           void (*view_projection)(float out[16], void* user), void* user,
                                                           void (*destroy)(void* user), const char* target, const char* depth);

/* ==== kor::Context, paths, and a project's configuration ================================================== */

KORAL_API bool koral_context_has_device(void);
KORAL_API bool koral_context_supports_ray_tracing(void);
KORAL_API bool koral_context_supports_async_compute(void);
/** Context::Supports: whether the device was made with @p feature (a kor::Feature bit) enabled. */
KORAL_API bool koral_context_supports_feature(uint64_t feature);
/** Context::GpuHas: whether the GPU has @p feature, enabled or not. */
KORAL_API bool koral_context_gpu_has_feature(uint64_t feature);
/** kor::FeatureName: "AtomicFloat32". The text lives as long as the library. */
KORAL_API const char* koral_feature_name(uint64_t feature);
KORAL_API bool koral_context_async_compute_is_separate_family(void);
KORAL_API const char* koral_asset_path(const char* relative);
KORAL_API const char* koral_shader_path(const char* relative);
KORAL_API void koral_add_asset_search_path(const char* directory, bool front);

typedef struct KoralProject KoralProject;
/**
 * Reads a project's configuration the way the runtime does — koral.json from `--config` in the arguments,
 * KORAL_CONFIG, or the nearest one at or above @p search_from; then the flags over it — and applies what
 * applies before the application exists: the asset and shader search paths, and the modules it lists.
 * Null on a bad file or flag (koral_last_error says which; koral_project_usage lists the flags).
 */
KORAL_API KoralProject* koral_project_load(const char* search_from, int argc, const char* const* argv);
KORAL_API void koral_project_destroy(KoralProject* project);
/** Which scene to open: `scene` in koral.json or `--scene`; empty when neither says. */
KORAL_API const char* koral_project_scene(KoralProject* project);
KORAL_API bool koral_project_hot_reload(KoralProject* project);
/** Fills @p settings from the project. Its strings and arrays are the project's, valid while it lives. */
KORAL_API void koral_project_app_settings(KoralProject* project, KoralAppSettings* settings);
KORAL_API void koral_project_window_settings(KoralProject* project, KoralWindowSettings* settings);
KORAL_API const char* koral_project_usage(void);

#ifdef __cplusplus
}
#endif

#endif /* KORAL_C_H */
