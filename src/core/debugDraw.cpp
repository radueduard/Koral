//
// Debug lines: kept on the CPU, uploaded once a frame, drawn as a line list pulled from a buffer.
//

#include "debugDraw.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>

#include <glm/gtc/matrix_inverse.hpp>

#include "commandBuffer.h"
#include "image.h"
#include "imageView.h"
#include "log.h"
#include "shader.h"

namespace kor
{
    DebugDraw::DebugDraw() = default;
    DebugDraw::~DebugDraw() = default;

    // ---- shapes -----------------------------------------------------------------------------------

    void DebugDraw::Line(const glm::vec3 from, const glm::vec3 to, const Style& style)
    {
        _lines.push_back({from, to, style.color, style.duration, style.onTop});
    }

    void DebugDraw::Box(const glm::vec3 min, const glm::vec3 max, const Style& style)
    {
        const glm::vec3 center = (min + max) * 0.5f;
        glm::mat4 transform(1.f);
        transform[0].x = max.x - min.x;
        transform[1].y = max.y - min.y;
        transform[2].z = max.z - min.z;
        transform[3] = glm::vec4(center, 1.f);
        Box(transform, style);
    }

    void DebugDraw::Box(const glm::mat4& transform, const Style& style)
    {
        std::array<glm::vec3, 8> corners;
        for (int i = 0; i < 8; ++i) {
            const glm::vec4 local { (i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f, 1.f };
            corners[i] = glm::vec3(transform * local);
        }
        // Each corner joined to the ones differing from it in exactly one axis.
        for (int i = 0; i < 8; ++i)
            for (const int bit : {1, 2, 4})
                if (!(i & bit)) Line(corners[i], corners[i | bit], style);
    }

    void DebugDraw::Circle(const glm::vec3 center, const glm::vec3 normal, const float radius, const Style& style, const int segments)
    {
        const glm::vec3 n = glm::normalize(normal);
        const glm::vec3 helper = std::abs(n.y) < 0.99f ? glm::vec3(0.f, 1.f, 0.f) : glm::vec3(1.f, 0.f, 0.f);
        const glm::vec3 u = glm::normalize(glm::cross(n, helper));
        const glm::vec3 v = glm::cross(n, u);
        const int count = std::max(segments, 3);
        glm::vec3 previous = center + u * radius;
        for (int i = 1; i <= count; ++i) {
            const float angle = 2.f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(count);
            const glm::vec3 next = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
            Line(previous, next, style);
            previous = next;
        }
    }

    void DebugDraw::Sphere(const glm::vec3 center, const float radius, const Style& style, const int segments)
    {
        Circle(center, {1.f, 0.f, 0.f}, radius, style, segments);
        Circle(center, {0.f, 1.f, 0.f}, radius, style, segments);
        Circle(center, {0.f, 0.f, 1.f}, radius, style, segments);
    }

    void DebugDraw::Arrow(const glm::vec3 from, const glm::vec3 to, const Style& style)
    {
        Line(from, to, style);
        const glm::vec3 along = to - from;
        const float length = glm::length(along);
        if (length <= 0.f) return;
        const glm::vec3 direction = along / length;
        const glm::vec3 helper = std::abs(direction.y) < 0.99f ? glm::vec3(0.f, 1.f, 0.f) : glm::vec3(1.f, 0.f, 0.f);
        const glm::vec3 side = glm::normalize(glm::cross(direction, helper));
        const glm::vec3 up = glm::cross(side, direction);
        const float head = length * 0.2f;
        const glm::vec3 base = to - direction * head;
        for (const glm::vec3 offset : {side, -side, up, -up}) Line(to, base + offset * head * 0.4f, style);
    }

    void DebugDraw::Point(const glm::vec3 position, const float size, const Style& style)
    {
        const float h = size * 0.5f;
        Line(position - glm::vec3(h, 0.f, 0.f), position + glm::vec3(h, 0.f, 0.f), style);
        Line(position - glm::vec3(0.f, h, 0.f), position + glm::vec3(0.f, h, 0.f), style);
        Line(position - glm::vec3(0.f, 0.f, h), position + glm::vec3(0.f, 0.f, h), style);
    }

    void DebugDraw::Axes(const glm::mat4& transform, const float size, const float duration)
    {
        const glm::vec3 origin(transform[3]);
        Arrow(origin, origin + glm::vec3(transform[0]) * size, {.color = {1.f, 0.2f, 0.2f, 1.f}, .duration = duration});
        Arrow(origin, origin + glm::vec3(transform[1]) * size, {.color = {0.2f, 1.f, 0.2f, 1.f}, .duration = duration});
        Arrow(origin, origin + glm::vec3(transform[2]) * size, {.color = {0.3f, 0.5f, 1.f, 1.f}, .duration = duration});
    }

    void DebugDraw::Grid(const glm::vec3 center, const float size, const int cells, const Style& style)
    {
        const int count = std::max(cells, 1);
        const float half = size * 0.5f;
        for (int i = 0; i <= count; ++i) {
            const float t = -half + size * static_cast<float>(i) / static_cast<float>(count);
            Line(center + glm::vec3(t, 0.f, -half), center + glm::vec3(t, 0.f, half), style);
            Line(center + glm::vec3(-half, 0.f, t), center + glm::vec3(half, 0.f, t), style);
        }
    }

    void DebugDraw::Frustum(const glm::mat4& viewProjection, const Style& style)
    {
        // Clip space's corners, back through the inverse: depth 0 to 1, as Vulkan's clip space has it.
        const glm::mat4 inverse = glm::inverse(viewProjection);
        std::array<glm::vec3, 8> corners;
        for (int i = 0; i < 8; ++i) {
            const glm::vec4 clip { (i & 1) ? 1.f : -1.f, (i & 2) ? 1.f : -1.f, (i & 4) ? 1.f : 0.f, 1.f };
            const glm::vec4 world = inverse * clip;
            corners[i] = glm::vec3(world) / world.w;
        }
        for (int i = 0; i < 8; ++i)
            for (const int bit : {1, 2, 4})
                if (!(i & bit)) Line(corners[i], corners[i | bit], style);
    }

    void DebugDraw::Clear() { _lines.clear(); }

    void DebugDraw::BeginFrame(const std::uint64_t frame) { _frame = frame; }

    void DebugDraw::EndFrame(const float frameTime)
    {
        std::erase_if(_lines, [&](Stored& line) {
            line.remaining -= frameTime;
            return line.remaining <= 0.f;
        });
    }

    // ---- drawing ----------------------------------------------------------------------------------

    namespace {
        std::string formatsOf(const Framebuffer& target)
        {
            std::string key;
            for (glm::u32 i = 0; i < target.ColorAttachmentCount(); ++i)
                key += std::format("c{},", static_cast<int>(target.ColorImage(i)->PixelFormat()));
            if (const auto depth = target.DepthAttachment(); depth.Valid())
                key += std::format("d{}", static_cast<int>(depth->SourceImage()->PixelFormat()));
            return key;
        }
    }

    void DebugDraw::Prepare(const ResourceRef<const Framebuffer>& target)
    {
        // The lines, once a frame however many passes draw them: depth-tested first, then on top.
        if (_uploaded != _frame) {
            _uploaded = _frame;
            std::vector<Vertex> vertices;
            vertices.reserve(_lines.size() * 2);
            for (const bool onTop : {false, true}) {
                for (const auto& line : _lines) {
                    if (line.onTop != onTop) continue;
                    vertices.push_back({glm::vec4(line.from, 1.f), line.color});
                    vertices.push_back({glm::vec4(line.to, 1.f), line.color});
                }
                (onTop ? _onTopCount : _testedCount) = static_cast<std::uint32_t>(vertices.size()) - (onTop ? _testedCount : 0);
            }
            if (vertices.size() > _capacity) {
                _capacity = std::max<std::size_t>(vertices.size() * 2, 1024);
                _buffer = Buffer::RawBuilder{}
                    .SetRawSize(static_cast<glm::i64>(_capacity * sizeof(Vertex)))
                    .SetUsage(Buffer::Usage::eStorage)
                    .SetType(Buffer::Type::eDynamic)
                    .SetIsPerFrame(true)
                    .Build();
                _buffer.SetName("debug lines");
            }
            if (!vertices.empty() && _buffer.Valid()) _buffer->Write(std::span<const Vertex>(vertices), 0);
        }
        if (!target.Valid() || !_buffer.Valid()) return;

        // A pipeline for what the target is made of, drawing lines — tested against its depth when it
        // has one — and a set for each, written for the buffer as it now is.
        auto& entry = _targets[formatsOf(*target)];
        if (!entry.tested.Valid() && !entry.tested.Poisoned()) {
            const auto vertex = Shader::Builder{}.SetPath(ShaderPath("koralDebugLines.vert.glsl")).GetOrBuild();
            const auto fragment = Shader::Builder{}.SetPath(ShaderPath("koralDebugLines.frag.glsl")).GetOrBuild();
            const bool hasDepth = target->DepthAttachment().Valid();
            ColorBlendState blend;
            blend.attachments.resize(target->ColorAttachmentCount(), ColorBlendState::AttachmentState{
                .blendEnable = true, .srcColorBlendFactor = BlendFactor::eSrcAlpha, .dstColorBlendFactor = BlendFactor::eOneMinusSrcAlpha});
            const auto make = [&](const bool depthTest) {
                return GraphicsPipeline::Builder()
                    .SetVertexShader(vertex)
                    .SetFragmentShader(fragment)
                    .SetFramebuffer(target)
                    .SetInputAssemblyState({.topology = Topology::eLineList})
                    .SetDepthStencilState({.depthTestEnable = depthTest && hasDepth, .depthWriteEnable = false,
                                           .depthCompareOp = CompareOp::eLessOrEqual})
                    .SetColorBlendState(blend)
                    .Build();
            };
            entry.tested = make(true);
            entry.onTop = make(false);
            if (!entry.tested.Valid()) log::Error("[debug draw] the line pipeline could not be made: {}",
                                                  entry.tested.Failure() ? entry.tested.Failure()->message : "no reason given");
        }
        if (entry.buffer != _buffer.Get() && entry.tested.Valid() && entry.onTop.Valid()) {
            entry.buffer = _buffer.Get();
            entry.testedSet = DescriptorSet::Builder(ResourceRef<const GraphicsPipeline>(entry.tested), 0).Write(0, _buffer).Build();
            entry.onTopSet = DescriptorSet::Builder(ResourceRef<const GraphicsPipeline>(entry.onTop), 0).Write(0, _buffer).Build();
        }
    }

    void DebugDraw::Record(CommandBuffer& commandBuffer, const glm::mat4& viewProjection, const ResourceRef<const Framebuffer>& target) const
    {
        if ((_testedCount == 0 && _onTopCount == 0) || !target.Valid()) return;
        const auto it = _targets.find(formatsOf(*target));
        if (it == _targets.end() || !it->second.testedSet.Valid() || !it->second.onTopSet.Valid()) return;
        const auto& entry = it->second;

        commandBuffer.BeginRendering(RenderInfo(target)
            .SetColorLoadOperation(LoadOperation::eLoad)
            .SetDepthLoadOperation(LoadOperation::eLoad)
            .SetStencilLoadOperation(LoadOperation::eLoad));
        if (_testedCount > 0) {
            commandBuffer.BindGraphicsPipeline(entry.tested).BindDescriptorSet(0, entry.testedSet)
                .PushConstant("viewProjection", viewProjection).Draw(_testedCount, 1, 0);
        }
        if (_onTopCount > 0) {
            commandBuffer.BindGraphicsPipeline(entry.onTop).BindDescriptorSet(0, entry.onTopSet)
                .PushConstant("viewProjection", viewProjection).Draw(_onTopCount, 1, _testedCount);
        }
        commandBuffer.EndRendering();
    }

    void DebugDraw::Render(CommandBuffer& commandBuffer, const glm::mat4& viewProjection, const ResourceRef<const Framebuffer> target)
    {
        Prepare(target);
        Record(commandBuffer, viewProjection, target);
    }

    // ---- as a pass --------------------------------------------------------------------------------

    DebugDrawPass::DebugDrawPass(DebugDraw& draw, std::function<glm::mat4()> viewProjection, std::string target, std::string depth)
        : RenderPass("Debug lines"), _draw(draw), _viewProjection(std::move(viewProjection)),
          _target(std::move(target)), _depth(std::move(depth)) {}

    void DebugDrawPass::Setup(PassBuilder& builder)
    {
        builder.Write(_target, Image::Usage::eColorAttachment);
        if (!_depth.empty()) builder.Write(_depth, Image::Usage::eDepthStencilAttachment);
    }

    void DebugDrawPass::Initialize(const PassResources& resources)
    {
        auto framebuffer = Framebuffer::Builder().AddColor({ .view = resources.ImageNamed(_target) });
        if (!_depth.empty()) {
            const auto depth = resources.ImageNamed(_depth);
            if (depth.Valid() && IsStencilFormat(depth->PixelFormat())) framebuffer.SetDepthStencil({ .view = depth });
            else framebuffer.SetDepth({ .view = depth });
        }
        _framebuffer = framebuffer.Build();
    }

    void DebugDrawPass::Prepare()
    {
        _matrix = _viewProjection ? _viewProjection() : glm::mat4(1.f);
        _draw.Prepare(_framebuffer);
    }

    void DebugDrawPass::Record(CommandBuffer& commandBuffer) const
    {
        _draw.Record(commandBuffer, _matrix, _framebuffer);
    }
}
