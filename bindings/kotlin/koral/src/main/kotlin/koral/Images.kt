package koral

import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.ValueLayout
import koral.interop.KoralNative

/** Reads three uint32s a C function writes through pointers. */
internal inline fun <T> threeInts(read: (MemorySegment, MemorySegment, MemorySegment) -> Unit, make: (Int, Int, Int) -> T): T =
    Arena.ofConfined().use { a ->
        val x = a.allocate(ValueLayout.JAVA_INT)
        val y = a.allocate(ValueLayout.JAVA_INT)
        val z = a.allocate(ValueLayout.JAVA_INT)
        read(x, y, z)
        make(x.get(ValueLayout.JAVA_INT, 0), y.get(ValueLayout.JAVA_INT, 0), z.get(ValueLayout.JAVA_INT, 0))
    }

/**
 * kor::Image: a texture, a render target, a storage image.
 *
 * ```
 * val albedo = Image.Builder()
 *     .setFormat(ImageFormat.eRGBA8_SRGB)
 *     .setExtent(UVec2(width, height))
 *     .setData(pixels)
 *     .setUsage(ImageUsage.eSampled, ImageUsage.eTransferDst)
 *     .build()
 * ```
 */
class Image internal constructor(native: MemorySegment) : Resource(native) {
    /** kor::Image::Builder. */
    class Builder : koral.Builder(KoralNative.koral_image_builder_new()) {
        fun setIsPerFrame(value: Boolean) = apply { KoralNative.koral_image_builder_set_is_per_frame(native, value) }
        fun setSharedAcrossQueues(shared: Boolean) = apply { KoralNative.koral_image_builder_set_shared_across_queues(native, shared) }
        fun setType(type: ImageType) = apply { KoralNative.koral_image_builder_set_type(native, type.value) }
        fun setFormat(format: ImageFormat) = apply { KoralNative.koral_image_builder_set_format(native, format.value) }
        /** The same extent in every dimension. */
        fun setExtent(extent: Int) = apply { KoralNative.koral_image_builder_set_extent(native, extent, extent, extent) }
        fun setExtent(extent: UVec2) = apply { KoralNative.koral_image_builder_set_extent(native, extent.x, extent.y, 1) }
        fun setExtent(extent: UVec3) = apply { KoralNative.koral_image_builder_set_extent(native, extent.x, extent.y, extent.z) }
        fun setMipLevels(mipLevels: Int) = apply { KoralNative.koral_image_builder_set_mip_levels(native, mipLevels) }
        fun setArrayLayers(arrayLayers: Int) = apply { KoralNative.koral_image_builder_set_array_layers(native, arrayLayers) }
        fun setSampleCount(sampleCount: SampleCount) = apply { KoralNative.koral_image_builder_set_sample_count(native, sampleCount.value) }
        fun setUsage(vararg usage: ImageUsage) = apply { KoralNative.koral_image_builder_set_usage(native, bits(usage)) }
        /** Its pixels — a primitive array or a MemorySegment — copied now (and the transfer usages added, as in C++). */
        fun setData(pixels: Any) = apply {
            Arena.ofConfined().use { a ->
                val s = segmentOf(a, pixels)
                KoralNative.koral_image_builder_set_data(native, s, s.byteSize())
            }
        }
        fun build(): Image = built(KoralNative.koral_image_builder_build(native), "building an image")
    }

    fun resize(extent: UVec3) {
        KoralNative.koral_image_resize(native, extent.x, extent.y, extent.z)
        checkLastError()
    }

    val generation: Long get() = KoralNative.koral_image_generation(native)
    val extent: UVec3 get() = threeInts({ x, y, z -> KoralNative.koral_image_extent(native, x, y, z) }, ::UVec3).also { checkLastError() }
    val imageType: ImageType get() = ImageType.of(KoralNative.koral_image_image_type(native))
    val format: ImageFormat get() = ImageFormat.of(KoralNative.koral_image_pixel_format(native))
    val isBgrOrder: Boolean get() = KoralNative.koral_image_is_bgr_order(native)
    val samples: SampleCount get() = SampleCount.of(KoralNative.koral_image_samples(native))
    val usage: Set<ImageUsage> get() = ImageUsage.flagsOf(KoralNative.koral_image_usage_flags(native))
    val mipLevels: Int get() = KoralNative.koral_image_mip_levels(native)
    val arrayLayers: Int get() = KoralNative.koral_image_array_layers(native)
    val isPerFrame: Boolean get() = KoralNative.koral_image_is_per_frame(native)
    val isSharedAcrossQueues: Boolean get() = KoralNative.koral_image_is_shared_across_queues(native)
    val copyIndex: Int get() = KoralNative.koral_image_copy_index(native)
    val naturalShape: ImageShape get() = ImageShape.of(KoralNative.koral_image_natural_shape(native))

    /** View(shape, coverage): the image's own view of that shape, kept by the image. */
    fun view(shape: ImageShape = ImageShape.e2D, coverage: ImageViewCoverage = ImageViewCoverage.eWholeImage): ImageView =
        borrowed(KoralNative.koral_image_view(native, shape.value, coverage.value))!!

    companion object {
        fun channelSize(format: ImageFormat): Int = KoralNative.koral_image_channel_size(format.value)
        fun channelCount(format: ImageFormat): Int = KoralNative.koral_image_channel_count(format.value)
        fun isFormatSupported(format: ImageFormat, usage: ImageUsage = ImageUsage.eSampled): Boolean =
            KoralNative.koral_image_is_format_supported(format.value, usage.value)
        fun isBlockCompressed(format: ImageFormat): Boolean = KoralNative.koral_image_is_block_compressed(format.value)
        fun blockExtent(format: ImageFormat): UVec2 = twoInts({ x, y -> KoralNative.koral_image_block_extent(format.value, x, y) }, ::UVec2)
        fun blockSize(format: ImageFormat): Int = KoralNative.koral_image_block_size(format.value)
        fun sizeOfRegion(format: ImageFormat, extent: UVec3, layerCount: Int = 1): Long =
            KoralNative.koral_image_size_of_region(format.value, extent.x, extent.y, extent.z, layerCount)
        /** kor::IsDepthStencilFormat. */
        fun isDepthStencilFormat(format: ImageFormat): Boolean = KoralNative.koral_is_depth_stencil_format(format.value)
        /** kor::IsStencilFormat. */
        fun isStencilFormat(format: ImageFormat): Boolean = KoralNative.koral_is_stencil_format(format.value)
    }
}

/** kor::ImageView: a part of an image, seen as some shape. */
class ImageView internal constructor(native: MemorySegment) : Resource(native) {
    /** kor::ImageView::ComponentMapping. */
    data class ComponentMapping(val r: ImageViewSwizzle = ImageViewSwizzle.eIdentity, val g: ImageViewSwizzle = ImageViewSwizzle.eIdentity,
                                val b: ImageViewSwizzle = ImageViewSwizzle.eIdentity, val a: ImageViewSwizzle = ImageViewSwizzle.eIdentity)

    /** kor::ImageView::Builder(image). */
    class Builder(image: Image) : koral.Builder(KoralNative.koral_image_view_builder_new(image.native)) {
        fun setViewType(viewType: ImageViewType) = apply { KoralNative.koral_image_view_builder_set_view_type(native, viewType.value) }
        fun setBaseMipLevel(level: Int) = apply { KoralNative.koral_image_view_builder_set_base_mip_level(native, level) }
        fun setMipLevelCount(count: Int) = apply { KoralNative.koral_image_view_builder_set_mip_level_count(native, count) }
        fun setBaseArrayLayer(layer: Int) = apply { KoralNative.koral_image_view_builder_set_base_array_layer(native, layer) }
        fun setArrayLayerCount(count: Int) = apply { KoralNative.koral_image_view_builder_set_array_layer_count(native, count) }
        fun setComponentMapping(mapping: ComponentMapping) = apply {
            Arena.ofConfined().use { a ->
                KoralNative.koral_image_view_builder_set_component_mapping(native,
                    a.allocateFrom(ValueLayout.JAVA_INT, mapping.r.value, mapping.g.value, mapping.b.value, mapping.a.value))
            }
        }
        fun build(): ImageView = built(KoralNative.koral_image_view_builder_build(native), "building an image view")
    }

    val image: Image get() = borrowed(KoralNative.koral_image_view_source_image(native))!!
    val viewType: ImageViewType get() = ImageViewType.of(KoralNative.koral_image_view_view_type(native))
    val baseMipLevel: Int get() = KoralNative.koral_image_view_base_mip_level(native)
    val mipLevelCount: Int get() = KoralNative.koral_image_view_mip_level_count(native)
    val baseArrayLayer: Int get() = KoralNative.koral_image_view_base_array_layer(native)
    val arrayLayerCount: Int get() = KoralNative.koral_image_view_array_layer_count(native)
    val isPerFrame: Boolean get() = KoralNative.koral_image_view_is_per_frame(native)
    val components: ComponentMapping
        get() = Arena.ofConfined().use { a ->
            val s = a.allocate(ValueLayout.JAVA_INT, 4)
            KoralNative.koral_image_view_components(native, s)
            checkLastError()
            fun at(i: Long) = ImageViewSwizzle.of(s.getAtIndex(ValueLayout.JAVA_INT, i))
            ComponentMapping(at(0), at(1), at(2), at(3))
        }
}

/** kor::Sampler: how an image is sampled. */
class Sampler internal constructor(native: MemorySegment) : Resource(native) {
    /** kor::Sampler::Builder. */
    class Builder : koral.Builder(KoralNative.koral_sampler_builder_new()) {
        fun setMinFilter(filter: Filter) = apply { KoralNative.koral_sampler_builder_set_min_filter(native, filter.value) }
        fun setMagFilter(filter: Filter) = apply { KoralNative.koral_sampler_builder_set_mag_filter(native, filter.value) }
        fun setMipmapMode(mode: SamplerMipmapMode) = apply { KoralNative.koral_sampler_builder_set_mipmap_mode(native, mode.value) }
        fun setAddressModeU(mode: SamplerAddressMode) = apply { KoralNative.koral_sampler_builder_set_address_mode_u(native, mode.value) }
        fun setAddressModeV(mode: SamplerAddressMode) = apply { KoralNative.koral_sampler_builder_set_address_mode_v(native, mode.value) }
        fun setAddressModeW(mode: SamplerAddressMode) = apply { KoralNative.koral_sampler_builder_set_address_mode_w(native, mode.value) }
        fun setMipLodBias(bias: Float) = apply { KoralNative.koral_sampler_builder_set_mip_lod_bias(native, bias) }
        fun setAnisotropyEnable(enable: Boolean) = apply { KoralNative.koral_sampler_builder_set_anisotropy_enable(native, enable) }
        fun setMaxAnisotropy(anisotropy: Float) = apply { KoralNative.koral_sampler_builder_set_max_anisotropy(native, anisotropy) }
        fun setCompareEnable(enable: Boolean) = apply { KoralNative.koral_sampler_builder_set_compare_enable(native, enable) }
        fun setCompareOp(op: CompareOp) = apply { KoralNative.koral_sampler_builder_set_compare_op(native, op.value) }
        fun setMinLod(lod: Float) = apply { KoralNative.koral_sampler_builder_set_min_lod(native, lod) }
        fun setMaxLod(lod: Float) = apply { KoralNative.koral_sampler_builder_set_max_lod(native, lod) }
        fun setUnnormalizedCoordinates(value: Boolean) = apply { KoralNative.koral_sampler_builder_set_unnormalized_coordinates(native, value) }
        fun build(): Sampler = built(KoralNative.koral_sampler_builder_build(native), "building a sampler")
    }
}

/** kor::BufferView: a buffer read as texels. */
class BufferView internal constructor(native: MemorySegment) : Resource(native) {
    /** kor::BufferView::Builder(buffer). */
    class Builder(buffer: Buffer) : koral.Builder(KoralNative.koral_buffer_view_builder_new(buffer.native)) {
        fun setFormat(format: ImageFormat) = apply { KoralNative.koral_buffer_view_builder_set_format(native, format.value) }
        fun setOffset(offset: Long) = apply { KoralNative.koral_buffer_view_builder_set_offset(native, offset) }
        fun setRange(range: Long) = apply { KoralNative.koral_buffer_view_builder_set_range(native, range) }
        fun build(): BufferView = built(KoralNative.koral_buffer_view_builder_build(native), "building a buffer view")
    }

    val buffer: Buffer get() = borrowed(KoralNative.koral_buffer_view_source_buffer(native))!!
    val format: ImageFormat get() = ImageFormat.of(KoralNative.koral_buffer_view_pixel_format(native))
    val offset: Long get() = KoralNative.koral_buffer_view_offset(native)
    val range: Long get() = KoralNative.koral_buffer_view_range(native)
    val texelCount: Long get() = KoralNative.koral_buffer_view_texel_count(native)
}

/**
 * kor::Shader: a stage of a pipeline, from a GLSL or Slang file, compiled again when the file changes —
 * and what was built from it with it.
 */
class Shader internal constructor(native: MemorySegment) : Resource(native) {
    /** kor::Shader::Builder. */
    class Builder : koral.Builder(KoralNative.koral_shader_builder_new()) {
        fun setStage(stage: ShaderStage) = apply { KoralNative.koral_shader_builder_set_stage(native, stage.value) }
        fun setPath(path: String) = apply { KoralNative.koral_shader_builder_set_path(native, path) }
        fun setEntryPoint(entry: String) = apply { KoralNative.koral_shader_builder_set_entry_point(native, null, entry) }
        fun setEntryPoint(module: String, entry: String) = apply { KoralNative.koral_shader_builder_set_entry_point(native, module, entry) }
        fun setLang(lang: ShaderLang) = apply { KoralNative.koral_shader_builder_set_lang(native, lang.value) }
        fun build(): Shader = built(KoralNative.koral_shader_builder_build(native), "building a shader")
        /** GetOrBuild(identifier): the repository's shader for this configuration, built the first time. Borrowed. */
        fun getOrBuild(identifier: String? = null): Shader =
            borrowed(KoralNative.koral_shader_builder_get_or_build(native, identifier)) ?: throw KoralException("getOrBuild returned nothing")
    }

    val stage: ShaderStage get() = ShaderStage.of(KoralNative.koral_shader_shader_stage(native))
    val language: ShaderLang get() = ShaderLang.of(KoralNative.koral_shader_language(native))
    val sourcePath: String get() = KoralNative.koral_shader_source_path(native)

    companion object {
        /** Where shader paths are looked for, after (or, with [front], before) those already known. */
        fun addSearchPath(directory: String, front: Boolean = false) = KoralNative.koral_shader_add_search_path(directory, front)
        /** kor::ShaderPath: where a relative shader path resolves to. */
        fun path(relative: String): String = KoralNative.koral_shader_path(relative)
    }
}
