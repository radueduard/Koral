namespace Koral.Tests;

// Buffer data from C# values of any type — structs, classes, records — laid out as C++ and the shaders lay them out.

public record struct GpuVertex(Vec3 Position, Vec2 Uv, uint Color);
public record GpuParticle(Vec3 Position, float Mass, Vec3 Velocity, int Id);   // a class: not unmanaged
public record GpuCamera(Mat4 ViewProjection, Vec3 Eye, float Exposure);
public record GpuNested(bool Flag, GpuParticle Particle, float Tail);
public record GpuUnsized(List<float> Values);

public static partial class Cases
{
    public static void GpuDataLayouts()
    {
        Check.Equal(24, GpuLayout.Of<GpuVertex>().Size, "C: the C++ struct's size");
        Check.Equal(System.Runtime.CompilerServices.Unsafe.SizeOf<GpuVertex>(), GpuLayout.Of<GpuVertex>().Size, "and the C# struct's own");
        Check.Equal(32, GpuLayout.Of<GpuParticle>().Size, "a record class too");
        Check.Equal(80, GpuLayout.Of<GpuCamera>().Size, "a matrix, a vec3, a float");
        Check.Equal(32, GpuLayout.Of<GpuParticle>(GpuPacking.Std430).Size, "std430: the float tucked after the vec3");
        Check.Equal(32, GpuLayout.Of<GpuVertex>(GpuPacking.Std430).Size, "std430: rounded to its 16 alignment");
        Check.Equal(4 + 32 + 4, GpuLayout.Of<GpuNested>().Size, "C: bool is a byte");
        Check.Equal(16 + 32 + 16, GpuLayout.Of<GpuNested>(GpuPacking.Std430).Size, "std430: bool is 4, the struct at 16");

        var v = new GpuVertex(new Vec3(1, 2, 3), new Vec2(4, 5), 0xff00ff00);
        Check.That(Gpu.Bytes(v).SequenceEqual(System.Runtime.InteropServices.MemoryMarshal.AsBytes(new ReadOnlySpan<GpuVertex>(in v)).ToArray()),
                   "the derived bytes are the struct's own");
        Check.Equal(v, Gpu.FromBytes<GpuVertex>(Gpu.Bytes(v))[0], "and back");
        var n = new GpuNested(true, new GpuParticle(new Vec3(1), 2f, new Vec3(3), 4), 5f);
        Check.Equal(n, Gpu.FromBytes<GpuNested>(Gpu.Bytes(n, GpuPacking.Std430), GpuPacking.Std430)[0], "a nested record, std430, back");
        Check.That(Gpu.Bytes(new[] { new Vec3(1, 2, 3), new Vec3(4, 5, 6) }).SequenceEqual(Gpu.Bytes(new float[] { 1, 2, 3, 4, 5, 6 })), "vectors are their floats");
        var points = new[] { new Vec3(1, 2, 3), new Vec3(4, 5, 6) };
        Check.That(points.ToFloatArray().ToVec3Array().SequenceEqual(points), "vectors to floats and back");
        Check.Throws<ArgumentException>(() => GpuLayout.Of<GpuUnsized>(), "a list inside a struct has no size");
        Check.Throws<ArgumentException>(() => Gpu.Bytes(new object[] { new Vec2(), new Vec3() }), "mixed types");
    }

    public static void GpuDataThroughAShader()
    {
        using var app = Check.HeadlessApp();
        var directory = Directory.CreateTempSubdirectory("koral-gpudata-");
        try
        {
            var path = Path.Combine(directory.FullName, "particles.comp");
            File.WriteAllText(path, """
                #version 450
                layout(local_size_x = 4) in;
                struct Particle { vec3 position; float mass; vec3 velocity; int id; };
                layout(std430, set = 0, binding = 0) buffer Particles { Particle particles[]; } data;
                void main() {
                    Particle p = data.particles[gl_GlobalInvocationID.x];
                    p.position += p.velocity * p.mass;
                    p.id += 100;
                    data.particles[gl_GlobalInvocationID.x] = p;
                }
                """);
            var pipeline = new ComputePipeline.Builder()
                .SetComputeShader(new Shader.Builder().SetPath(path).SetStage(Shader.Stage.eCompute).Build()).Build();
            Check.That(pipeline.Valid, $"{pipeline.Failure}");
            var particles = Enumerable.Range(0, 4).Select(i => new GpuParticle(new Vec3(i, 0, 0), 2f, new Vec3(0, 1, i), i)).ToList();
            var buffer = new Buffer.RawBuilder().SetData(particles, GpuPacking.Std430)
                .SetUsage(Buffer.Usage.eStorage | Buffer.Usage.eTransferSrc | Buffer.Usage.eTransferDst).SetType(Buffer.Type.eDynamic).Build();
            var set = new DescriptorSet.Builder(pipeline, 0).Write("data", buffer).Build();
            CommandBuffer.SingleTimeCommand(cb => cb.BindComputePipeline(pipeline).BindDescriptorSet(0, set).Dispatch(), CommandBuffer.Usage.eCompute).Wait();
            var expected = particles.Select(p => p with { Position = p.Position + p.Velocity * p.Mass, Id = p.Id + 100 }).ToArray();
            var read = buffer.ReadAs<GpuParticle>(GpuPacking.Std430);
            Check.That(read.SequenceEqual(expected), $"the shader read and wrote the records' layout: [{string.Join(", ", read)}]");

            // Uniforms: one record, std140; and a typed struct buffer written in place.
            var camera = new GpuCamera(KMath.Perspective(1f, 1.5f, 0.1f, 100f), new Vec3(0, 2, 5), 1.25f);
            var uniforms = new Buffer.RawBuilder().SetInstanceCount<GpuCamera>(1, GpuPacking.Std140).SetType(Buffer.Type.eDynamic).Build();
            uniforms.WriteValues(camera, packing: GpuPacking.Std140);
            Check.Equal(camera, uniforms.ReadAs<GpuCamera>(GpuPacking.Std140)[0], "a uniform record");
            var vertices = new Buffer.Builder<GpuVertex>().SetData([new GpuVertex(new Vec3(1), new Vec2(2), 3)]).SetType(Buffer.Type.eDynamic).Build();
            Check.Equal(new GpuVertex(new Vec3(1), new Vec2(2), 3), vertices.ReadAs<GpuVertex>()[0], "a typed buffer reads as values too");
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }
}
