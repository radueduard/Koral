package koral.tests

import java.nio.file.Files
import kotlin.io.path.writeText
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue
import koral.*

// Buffer data from Kotlin values: vectors, matrices, data classes and lists of them, laid out as C++ and the
// shaders lay them out.

data class Vertex(val position: Vec3, val uv: Vec2, val color: UInt)
data class Particle(val position: Vec3, val mass: Float, val velocity: Vec3, val id: Int)
data class Camera(val viewProjection: Mat4, val eye: Vec3, val exposure: Float)
data class Nested(val flag: Boolean, val particle: Particle, val tail: Float)
enum class Tag { A, B, C }
data class Tagged(val tag: Tag, val weight: Float)
class Unsized(val values: List<Float>)

class GpuDataTest {
    @Test
    fun layoutsAreCppsAndTheShaders() {
        // C packing: the same bytes as the same C++ struct (kor::Vec3 is 12 bytes, aligned to 4).
        assertEquals(24, gpuLayout<Vertex>().size)
        assertEquals(32, gpuLayout<Particle>().size)
        assertEquals(80, gpuLayout<Camera>().size)
        // std430: a vec3 aligned to 16, a float tucked into its last 4 bytes.
        assertEquals(32, gpuLayout<Particle>(GpuPacking.Std430).size)
        assertEquals(32, gpuLayout<Vertex>(GpuPacking.Std430).size)   // vec3 at 0, vec2 at 16, uint at 24 → 28, rounded to 16
        assertEquals(16, gpuLayout<Vertex>(GpuPacking.Std430).alignment)
        // C: bool is a byte, so the particle starts at 4; std430: 4 bytes, and the particle at 16.
        assertEquals(4 + 32 + 4, gpuLayout<Nested>().size.toInt())
        assertEquals(16 + 32 + 16, gpuLayout<Nested>(GpuPacking.Std430).size.toInt())
        // std140 rounds every struct up to 16.
        assertEquals(16, gpuLayout<Tagged>(GpuPacking.Std140).size)
        assertEquals(8, gpuLayout<Tagged>().size)

        val v = Vertex(Vec3(1f, 2f, 3f), Vec2(4f, 5f), 0xff00ff00u)
        val bytes = gpuBytes(v)
        assertEquals(24, bytes.size)
        assertEquals(listOf(v), fromGpuBytes<Vertex>(bytes))
        assertContentEquals(gpuBytes(listOf(1f, 2f, 3f, 4f, 5f)), gpuBytes(floatArrayOf(1f, 2f, 3f)) + gpuBytes(floatArrayOf(4f, 5f)))
        assertContentEquals(gpuBytes(listOf(Vec3(1f, 2f, 3f), Vec3(4f, 5f, 6f))), gpuBytes(floatArrayOf(1f, 2f, 3f, 4f, 5f, 6f)))
        assertEquals(listOf(Tagged(Tag.C, 2f)), fromGpuBytes<Tagged>(gpuBytes(Tagged(Tag.C, 2f))))
        assertEquals(listOf(Nested(true, Particle(Vec3(1f), 2f, Vec3(3f), 4), 5f)),
                     fromGpuBytes<Nested>(gpuBytes(Nested(true, Particle(Vec3(1f), 2f, Vec3(3f), 4), 5f), GpuPacking.Std430), GpuPacking.Std430))

        // Conversions both ways.
        val points = listOf(Vec3(1f, 2f, 3f), Vec3(4f, 5f, 6f))
        assertEquals(points, points.toFloatArray().toVec3List())
        assertEquals(listOf(IVec2(1, 2)), listOf(IVec2(1, 2)).toIntArray().toIVec2List())

        assertFailsWith<IllegalArgumentException> { gpuLayout<Unsized>() }
        assertFailsWith<IllegalArgumentException> { gpuBytes(listOf(Vec2(0f), Vec3(0f))) }
    }

    @Test
    fun buffersTakeAndGiveValues() = headlessApp().use {
        val vertices = listOf(Vertex(Vec3(1f, 2f, 3f), Vec2(0f, 1f), 7u), Vertex(Vec3(-1f), Vec2(1f, 0f), 9u))
        val buffer = Buffer.Builder().setData(vertices).setType(BufferType.eDynamic).build()
        assertEquals(48, buffer.size)
        assertEquals(vertices, buffer.readAs<Vertex>())

        val camera = Camera(perspective(1f, 1.5f, 0.1f, 100f), Vec3(0f, 2f, 5f), 1.25f)
        val uniforms = Buffer.Builder().setInstanceCount<Camera>(1, GpuPacking.Std140).setType(BufferType.eDynamic).build()
        uniforms.map { it.write(camera, packing = GpuPacking.Std140) }
        assertEquals(listOf(camera), uniforms.readAs<Camera>(packing = GpuPacking.Std140))

        val positions = Buffer.Builder().setData(listOf(Vec4(1f), Vec4(2f))).setType(BufferType.eDynamic).build()
        positions.write(Vec4(3f), offset = 16)
        assertEquals(listOf(Vec4(1f), Vec4(3f)), positions.readAs<Vec4>())
        assertEquals(listOf(1f, 1f, 1f, 1f), positions.readFloats(4).toList())
        Unit
    }

    @Test
    fun aShaderReadsTheStructs() = headlessApp().use {
        val dir = Files.createTempDirectory("koral-kotlin-")
        val path = dir.resolve("particles.comp").also {
            it.writeText("""
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
            """.trimIndent())
        }.toString()
        val pipeline = ComputePipeline.Builder().setComputeShader(Shader.Builder().setPath(path).setStage(ShaderStage.eCompute).build()).build()
        assertTrue(pipeline.isValid, "${pipeline.failure}")
        val particles = List(4) { Particle(Vec3(it.toFloat(), 0f, 0f), 2f, Vec3(0f, 1f, it.toFloat()), it) }
        val buffer = Buffer.Builder().setData(particles, GpuPacking.Std430)
            .setUsage(BufferUsage.eStorage, BufferUsage.eTransferSrc, BufferUsage.eTransferDst).setType(BufferType.eDynamic).build()
        val set = DescriptorSet.Builder(pipeline, 0).write("data", buffer).build()
        CommandBuffer.singleTimeCommand(CommandBufferUsage.eCompute) { it.bindComputePipeline(pipeline).bindDescriptorSet(0, set).dispatch() }.waitBlocking()
        val expected = particles.map { it.copy(position = it.position + it.velocity * it.mass, id = it.id + 100) }
        assertEquals(expected, buffer.readAs<Particle>(packing = GpuPacking.Std430), "the shader read and wrote the data classes' layout")
    }
}
