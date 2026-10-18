package koral

import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.SegmentAllocator
import java.lang.foreign.ValueLayout
import koral.interop.KoralLayouts
import koral.interop.KoralNative

/**
 * kor::DescriptorIndex: an index into one of the bindless tables — what a shader is handed to reach a
 * resource. Push it ([CommandBuffer.pushConstant]) or store [index] in a buffer.
 */
data class DescriptorIndex(val index: Int, val kind: BindlessKind) {
    /** Whether it names a slot. An invalid one was refused, and the reason logged. */
    val valid: Boolean get() = kind != BindlessKind.eNone && index != -1

    internal fun native(a: SegmentAllocator): MemorySegment = a.allocate(KoralLayouts.KoralDescriptorIndex).also {
        it.set(ValueLayout.JAVA_INT, 0, index)
        it.set(ValueLayout.JAVA_INT, 4, kind.value)
    }

    internal companion object {
        fun of(native: MemorySegment) = DescriptorIndex(
            native.get(ValueLayout.JAVA_INT, 0), BindlessKind.of(native.get(ValueLayout.JAVA_INT, 4)))
    }
}

/**
 * kor::Bindless: hands out the indices a shader picks resources by at run time, under either descriptor
 * model. Shaders import `koral_bindless` (Slang) or include `koral_bindless.glsl`.
 */
object Bindless {
    /** The descriptor set the bindless tables are declared at; a pipeline's own sets stay below it. */
    const val SET = 7

    private inline fun index(call: (Arena) -> MemorySegment) = Arena.ofConfined().use { a -> DescriptorIndex.of(call(a)) }

    fun sampled(image: Image, shape: ImageShape = ImageShape.e2D) = index { KoralNative.koral_bindless_sampled(it, image.native, shape.value) }
    fun sampled(view: ImageView) = index { KoralNative.koral_bindless_sampled(it, view.native, 0) }
    fun storage(image: Image, shape: ImageShape = ImageShape.e2D) = index { KoralNative.koral_bindless_storage(it, image.native, shape.value) }
    fun storage(view: ImageView) = index { KoralNative.koral_bindless_storage(it, view.native, 0) }
    fun storageBuffer(buffer: Buffer) = index { KoralNative.koral_bindless_storage_buffer(it, buffer.native) }
    fun uniformBuffer(buffer: Buffer) = index { KoralNative.koral_bindless_uniform_buffer(it, buffer.native) }
    fun sampler(sampler: Sampler) = index { KoralNative.koral_bindless_sampler(it, sampler.native) }
    fun accelerationStructure(structure: AccelerationStructure) = index { KoralNative.koral_bindless_acceleration_structure(it, structure.native) }

    /** Gives an index back before its resource goes (destroying the resource does it too). */
    fun release(index: DescriptorIndex) = Arena.ofConfined().use { a -> KoralNative.koral_bindless_release(index.native(a)) }
    fun capacity(kind: BindlessKind): Int = KoralNative.koral_bindless_capacity(kind.value)
    val count: Long get() = KoralNative.koral_bindless_count()
}

/**
 * kor::DescriptorHeap: slots of the descriptor heap a project fills itself, from the CPU or the GPU. Only
 * under [DescriptorModel.eHeap]; under descriptor sets it builds poisoned.
 */
class DescriptorHeap internal constructor(native: MemorySegment) : Resource(native) {
    /** kor::DescriptorHeap::Builder. */
    class Builder {
        private var role = HeapRole.eResource
        private var slots = 0
        fun setRole(role: HeapRole) = apply { this.role = role }
        fun setSlotCount(count: Int) = apply { slots = count }
        fun build() = DescriptorHeap(checked(KoralNative.koral_descriptor_heap_new(role.value, slots), "making a descriptor heap"))
    }

    val slotCount: Int get() = KoralNative.koral_descriptor_heap_slot_count(native)
    /** Where slot 0 is in the whole heap: the bindless index of slot 0. */
    val firstSlot: Int get() = KoralNative.koral_descriptor_heap_first_slot(native)

    /** Writes [resource] — a buffer, buffer view, image, image view, sampler or acceleration structure — into [slot] as [type]. */
    fun write(slot: Int, resource: Resource, type: DescriptorType) =
        checked(KoralNative.koral_descriptor_heap_write(native, slot, resource.native, type.value), "writing a descriptor heap slot")

    /** The bindless index a shader reaches [slot] by, through the table of [kind]. */
    fun index(slot: Int, kind: BindlessKind) = Arena.ofConfined().use { a ->
        DescriptorIndex.of(KoralNative.koral_descriptor_heap_index(a, native, slot, kind.value))
    }

    /** Where the GPU finds [slot], for a shader that writes descriptors itself. */
    fun deviceAddress(slot: Int = 0): Long = KoralNative.koral_descriptor_heap_device_address(native, slot)

    companion object {
        /** Bytes between two slots of a heap of [role]. */
        fun slotSize(role: HeapRole): Int = KoralNative.koral_descriptor_heap_slot_size(role.value)

        /** A descriptor's bytes, as written into a slot: for a buffer of them copied in with copyDescriptors. */
        fun encode(resource: Resource, type: DescriptorType): ByteArray = Arena.ofConfined().use { a ->
            val size = slotSize(if (type == DescriptorType.eSampler) HeapRole.eSampler else HeapRole.eResource)
            val out = a.allocate(maxOf(1, size).toLong())
            val written = KoralNative.koral_descriptor_heap_encode(resource.native, type.value, out, size.toLong())
            check(written > 0) { "encoding a descriptor: ${KoralNative.koral_last_error()}" }
            out.asSlice(0, written).toArray(ValueLayout.JAVA_BYTE)
        }
    }
}
