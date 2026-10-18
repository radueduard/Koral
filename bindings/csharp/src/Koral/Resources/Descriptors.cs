using Koral.Native;

namespace Koral;

/// <summary>
/// kor::DescriptorIndex: an index into one of the bindless tables — what a shader is handed to reach a
/// resource. Plain data: push it (<see cref="CommandBuffer.PushConstant(string, DescriptorIndex)"/>) or
/// store <see cref="Index"/> in a buffer.
/// </summary>
public readonly record struct DescriptorIndex(uint Index, BindlessKind Kind)
{
    /// <summary>Whether it names a slot. An invalid one was refused, and the reason logged.</summary>
    public bool Valid => Kind != BindlessKind.eNone && Index != uint.MaxValue;

    internal KoralDescriptorIndex Native => new() { index = Index, kind = (uint)Kind };
    internal static DescriptorIndex From(KoralDescriptorIndex native) => new(native.index, (BindlessKind)native.kind);
}

/// <summary>
/// kor::Bindless: hands out the indices a shader picks resources by at run time, under either descriptor
/// model. Shaders import <c>koral_bindless</c> (Slang) or include <c>koral_bindless.glsl</c>.
/// </summary>
public static class Bindless
{
    /// <summary>The descriptor set the bindless tables are declared at; a pipeline's own sets stay below it.</summary>
    public const uint Set = 7;

    public static DescriptorIndex Sampled(Image image, ImageShape shape = ImageShape.e2D) =>
        DescriptorIndex.From(KoralNative.koral_bindless_sampled(image.Handle, (uint)shape));
    public static DescriptorIndex Sampled(ImageView view) => DescriptorIndex.From(KoralNative.koral_bindless_sampled(view.Handle, 0));
    public static DescriptorIndex Storage(Image image, ImageShape shape = ImageShape.e2D) =>
        DescriptorIndex.From(KoralNative.koral_bindless_storage(image.Handle, (uint)shape));
    public static DescriptorIndex Storage(ImageView view) => DescriptorIndex.From(KoralNative.koral_bindless_storage(view.Handle, 0));
    public static DescriptorIndex StorageBuffer(Buffer buffer) => DescriptorIndex.From(KoralNative.koral_bindless_storage_buffer(buffer.Handle));
    public static DescriptorIndex UniformBuffer(Buffer buffer) => DescriptorIndex.From(KoralNative.koral_bindless_uniform_buffer(buffer.Handle));
    public static DescriptorIndex Sampler(Sampler sampler) => DescriptorIndex.From(KoralNative.koral_bindless_sampler(sampler.Handle));
    public static DescriptorIndex AccelerationStructure(AccelerationStructure structure) =>
        DescriptorIndex.From(KoralNative.koral_bindless_acceleration_structure(structure.Handle));

    /// <summary>Gives an index back before its resource goes (destroying the resource does it too).</summary>
    public static void Release(DescriptorIndex index) => KoralNative.koral_bindless_release(index.Native);
    public static uint Capacity(BindlessKind kind) => KoralNative.koral_bindless_capacity((uint)kind);
    public static ulong Count => KoralNative.koral_bindless_count();
}

/// <summary>
/// kor::DescriptorHeap: slots of the descriptor heap a project fills itself, from the CPU or the GPU.
/// Only under <see cref="DescriptorModel.eHeap"/>; under descriptor sets it builds poisoned.
/// </summary>
public sealed unsafe class DescriptorHeap : Resource
{
    internal DescriptorHeap(IntPtr native) : base(native) { }

    /// <summary>kor::DescriptorHeap::Builder.</summary>
    public sealed class Builder
    {
        private HeapRole _role = HeapRole.eResource;
        private uint _slots;

        public Builder SetRole(HeapRole role) { _role = role; return this; }
        public Builder SetSlotCount(uint count) { _slots = count; return this; }
        public DescriptorHeap Build() => new(KoralNative.Check(KoralNative.koral_descriptor_heap_new((uint)_role, _slots)));
    }

    public uint SlotCount => KoralNative.koral_descriptor_heap_slot_count(Handle);
    /// <summary>Where slot 0 is in the whole heap: the bindless index of slot 0.</summary>
    public uint FirstSlot => KoralNative.koral_descriptor_heap_first_slot(Handle);

    /// <summary>
    /// Writes <paramref name="resource"/> — a buffer, buffer view, image, image view, sampler or acceleration
    /// structure — into <paramref name="slot"/> as the <paramref name="type"/> its shader declares.
    /// </summary>
    public void Write(uint slot, Resource resource, DescriptorType type) =>
        KoralNative.Check(KoralNative.koral_descriptor_heap_write(Handle, slot, resource.Handle, (uint)type));

    /// <summary>The bindless index a shader reaches <paramref name="slot"/> by, through the table of <paramref name="kind"/>.</summary>
    public DescriptorIndex Index(uint slot, BindlessKind kind) => DescriptorIndex.From(KoralNative.koral_descriptor_heap_index(Handle, slot, (uint)kind));

    /// <summary>Where the GPU finds <paramref name="slot"/>, for a shader that writes descriptors itself.</summary>
    public ulong DeviceAddress(uint slot = 0) => KoralNative.koral_descriptor_heap_device_address(Handle, slot);

    /// <summary>Bytes between two slots of a heap of <paramref name="role"/>.</summary>
    public static uint SlotSize(HeapRole role) => KoralNative.koral_descriptor_heap_slot_size((uint)role);

    /// <summary>A descriptor's bytes, as written into a slot: for a buffer of them copied in with CopyDescriptors.</summary>
    public static byte[] Encode(Resource resource, DescriptorType type)
    {
        var bytes = new byte[SlotSize(type == DescriptorType.eSampler ? HeapRole.eSampler : HeapRole.eResource)];
        nuint written;
        fixed (byte* pointer = bytes) written = KoralNative.koral_descriptor_heap_encode(resource.Handle, (uint)type, pointer, (nuint)bytes.Length);
        if (written == 0) KoralNative.Check();
        return bytes;
    }
}
