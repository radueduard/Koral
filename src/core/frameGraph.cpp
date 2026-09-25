//
// Created by radue on 9/24/2026.
//

#include "frameGraph.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <ranges>
#include <set>

#include <imgui.h>

#include "commandBuffer.h"
#include "context.h"
#include "framebuffer.h"
#include "gtime.h"
#include "log.h"
#include "scheduler.h"
#include "task.h"
#include "window.h"
#include "frameGraphCompiler.h"

namespace kor {
    struct PassBuilder::Impl {
        graph::PassDecl decl;
        std::vector<std::pair<std::string, ImageDesc>> images;
        std::vector<std::pair<std::string, BufferDesc>> buffers;
    };

    PassBuilder& PassBuilder::Read(const std::string_view name) {
        _impl.decl.uses.push_back({std::string(name), graph::Access::eRead});
        return *this;
    }

    PassBuilder& PassBuilder::Write(const std::string_view name) {
        _impl.decl.uses.push_back({std::string(name), graph::Access::eWrite});
        return *this;
    }

    PassBuilder& PassBuilder::Create(const std::string_view name, const ImageDesc& desc) {
        _impl.decl.uses.push_back({std::string(name), graph::Access::eCreate});
        _impl.images.emplace_back(std::string(name), desc);
        return *this;
    }

    PassBuilder& PassBuilder::Create(const std::string_view name, const BufferDesc& desc) {
        _impl.decl.uses.push_back({std::string(name), graph::Access::eCreate});
        _impl.buffers.emplace_back(std::string(name), desc);
        return *this;
    }

    PassBuilder& PassBuilder::Consume(const std::string_view name, const std::string_view as) {
        _impl.decl.uses.push_back({std::string(name), graph::Access::eConsume, std::string(as)});
        return *this;
    }

    PassBuilder& PassBuilder::SideEffect() {
        _impl.decl.sideEffect = true;
        return *this;
    }

    PassBuilder& PassBuilder::ReadPrevious(const std::string_view name) {
        _impl.decl.uses.push_back({std::string(name), graph::Access::eReadPrevious});
        return *this;
    }

    ResourceRef<const Image> PassResources::ImageNamed(const std::string_view name) const {
        if (name == FrameGraph::Screen) {
            const auto framebuffer = Context::DefaultFramebuffer();
            return framebuffer.Valid() ? framebuffer->ColorImage(0) : ResourceRef<const Image>{};
        }
        const auto it = _graph._images.find(name);
        if (it == _graph._images.end()) {
            log::Error("[frame graph] no image named '{}'", name);
            return {};
        }
        return it->second;
    }

    ResourceRef<const Buffer> PassResources::BufferNamed(const std::string_view name) const {
        const auto it = _graph._buffers.find(name);
        if (it == _graph._buffers.end()) {
            log::Error("[frame graph] no buffer named '{}'", name);
            return {};
        }
        return it->second;
    }

    ResourceRef<const Image> PassResources::PreviousImageNamed(const std::string_view name) const {
        const auto it = _graph._previousImages.find(name);
        if (it == _graph._previousImages.end()) {
            log::Error("[frame graph] no previous frame of an image named '{}' is kept; declare ReadPrevious(\"{}\")", name, name);
            return {};
        }
        return it->second;
    }

    ResourceRef<const Buffer> PassResources::PreviousBufferNamed(const std::string_view name) const {
        const auto it = _graph._previousBuffers.find(name);
        if (it == _graph._previousBuffers.end()) {
            log::Error("[frame graph] no previous frame of a buffer named '{}' is kept; declare ReadPrevious(\"{}\")", name, name);
            return {};
        }
        return it->second;
    }

    glm::uvec2 PassResources::Extent(const std::string_view name) const {
        const auto img = ImageNamed(name);
        return img.Alive() ? glm::uvec2(img->Extent()) : glm::uvec2(0);
    }

    ResourceRef<const Image> FrameGraph::ImageNamed(const std::string_view name) const {
        const auto it = _images.find(name);
        return it != _images.end() ? it->second : ResourceRef<const Image>{};
    }

    bool FrameGraph::HasPrevious(const std::string_view name) const {
        return _historyFrames > 0 && (_previousImages.contains(name) || _previousBuffers.contains(name));
    }

    bool RenderPass::HasPrevious(const std::string_view name) const {
        return _graph && _graph->HasPrevious(name);
    }

    void RenderPass::SetEnabled(const bool enabled) {
        if (_enabled == enabled) return;
        _enabled = enabled;
        if (_graph) _graph->Invalidate();
    }

    // ---- statistics -----------------------------------------------------------------------------

    struct FrameGraph::Stats {
        static constexpr double Smoothing = 0.1;   // weight of the newest sample in each average
        static constexpr std::size_t History = 240;

        static void average(double& into, const double sample) {
            into = into == 0.0 ? sample : into + (sample - into) * Smoothing;
        }

        std::map<std::string, PassTiming, std::less<>> passes;
        GraphTiming graph;
        std::array<float, History> frameMs {};   // CPU frame time, a ring
        std::array<float, History> gpuMs {};     // the graph's GPU time, a ring
        std::size_t next = 0;
    };

    FrameGraph::FrameGraph() : _stats(std::make_shared<Stats>()) {}
    FrameGraph::~FrameGraph() = default;

    std::vector<FrameGraph::PassTiming> FrameGraph::PassTimings() const {
        std::vector<PassTiming> out;
        for (const RenderPass* pass : _order)
            if (const auto it = _stats->passes.find(pass->Name()); it != _stats->passes.end()) out.push_back(it->second);
        return out;
    }

    FrameGraph::GraphTiming FrameGraph::Timing() const { return _stats->graph; }

    void FrameGraph::Adopt(std::unique_ptr<RenderPass> pass) {
        pass->_graph = this;
        _passes.push_back(std::move(pass));
        _dirty = true;
    }

    void FrameGraph::Import(std::string name, ResourceRef<const Image> image) {
        // Passes build their descriptor sets from these in Initialize, so a name that now means a
        // different resource needs them built again.
        const auto it = _importedImages.find(name);
        if (it == _importedImages.end() || it->second.Get() != image.Get()) _dirty = true;
        _importedImages.insert_or_assign(std::move(name), std::move(image));
    }

    void FrameGraph::Import(std::string name, ResourceRef<const Buffer> buffer) {
        const auto it = _importedBuffers.find(name);
        if (it == _importedBuffers.end() || it->second.Get() != buffer.Get()) _dirty = true;
        _importedBuffers.insert_or_assign(std::move(name), std::move(buffer));
    }

    bool FrameGraph::Build() {
        _dirty = false;
        _broken = false;
        _order.clear();
        _schedule.clear();
        _culled.clear();
        _skipped.clear();

        // ---- declarations -------------------------------------------------------------------------
        // Disabled passes are declared too: the compiler needs what they would have made to take
        // them out without breaking the passes that depended on them.
        std::vector<PassBuilder::Impl> declarations;
        for (const auto& pass : _passes) {
            PassBuilder::Impl impl{.decl = {.name = pass->Name(), .enabled = pass->Enabled()}};
            PassBuilder builder(impl);
            pass->Setup(builder);
            declarations.push_back(std::move(impl));
        }
        std::vector<graph::PassDecl> decls;
        for (const auto& d : declarations) decls.push_back(d.decl);

        std::set<std::string> imported{std::string(Screen)};
        for (const auto& name : _importedImages | std::views::keys) imported.insert(name);
        for (const auto& name : _importedBuffers | std::views::keys) imported.insert(name);

        const auto compiled = graph::compile(decls, imported);
        if (!compiled) {
            log::Error("[frame graph] {}", compiled.error().message);
            _broken = true;
            return false;
        }

        // ---- allocation ------------------------------------------------------------------------------
        // Replaced, not resized: what the old resources are still doing on the GPU is deferred
        // destruction's business, and the passes rebuild everything that referred to them anyway.
        _ownedImages.clear();
        _ownedBuffers.clear();
        _images.clear();
        _buffers.clear();
        _imageDescs.clear();
        _previousImages.clear();
        _previousBuffers.clear();
        _imageHistory.clear();
        _bufferHistory.clear();
        _historyClears.clear();
        _history = compiled->history;
        _historyFrames = 0;
        const std::set<std::string, std::less<>> history(compiled->history.begin(), compiled->history.end());
        for (const auto& [name, image] : _importedImages) _images.emplace(name, image);
        for (const auto& [name, buffer] : _importedBuffers) _buffers.emplace(name, buffer);

        std::map<std::string, ImageDesc, std::less<>> imageDescs;
        std::map<std::string, BufferDesc, std::less<>> bufferDescs;
        for (const auto& d : declarations) {
            for (const auto& [name, desc] : d.images) imageDescs.emplace(name, desc);
            for (const auto& [name, desc] : d.buffers) bufferDescs.emplace(name, desc);
        }
        for (const auto& lifetime : compiled->lifetimes) {
            if (const auto it = imageDescs.find(lifetime.resource); it != imageDescs.end()) {
                const ImageDesc& desc = it->second;
                const bool keepsHistory = history.contains(lifetime.resource);
                const glm::uvec2 size = desc.extent.value_or(glm::max(
                    glm::uvec2(glm::vec2(_extent) * desc.scale), glm::uvec2(1)));
                auto image = Image::Builder()
                    .SetIsPerFrame(true)  // one per frame in flight, so the next frame can start
                    .SetFormat(desc.format)
                    .SetUsage(keepsHistory ? desc.usage | Image::Usage::eTransferSrc : desc.usage)
                    .SetExtent(size)
                    .SetMipLevels(desc.mipLevels)
                    .Build();
                image.SetName(lifetime.resource);
                _images.emplace(lifetime.resource, ResourceRef<const Image>(image));
                _imageDescs.emplace(lifetime.resource, desc);
                if (keepsHistory) {
                    // One image, not one per frame: it carries a value from one frame to the next,
                    // and the GPU runs frames in order, so the barriers are all it needs.
                    // Written by the copy; readable by one too, so it can be inspected or read back.
                    auto usage = desc.usage | Image::Usage::eTransferDst | Image::Usage::eTransferSrc;
                    if (IsDepthStencilFormat(desc.format)) usage |= Image::Usage::eDepthStencilAttachment;
                    auto previous = Image::Builder()
                        .SetFormat(desc.format)
                        .SetUsage(usage)
                        .SetExtent(size)
                        .SetMipLevels(desc.mipLevels)
                        .Build();
                    previous.SetName(lifetime.resource + " (previous frame)");
                    _previousImages.emplace(lifetime.resource, ResourceRef<const Image>(previous));
                    _imageHistory.emplace_back(ResourceRef<const Image>(image), std::move(previous));
                }
                _ownedImages.push_back(std::move(image));
            } else if (const auto bt = bufferDescs.find(lifetime.resource); bt != bufferDescs.end()) {
                const bool keepsHistory = history.contains(lifetime.resource);
                auto buffer = Buffer::RawBuilder()
                    .SetRawSize(bt->second.size)
                    .SetUsage(keepsHistory ? bt->second.usage | Buffer::Usage::eTransferSrc : bt->second.usage)
                    .SetType(bt->second.type)
                    .Build();
                buffer.SetName(lifetime.resource);
                _buffers.emplace(lifetime.resource, ResourceRef<const Buffer>(buffer));
                if (keepsHistory) {
                    auto previous = Buffer::RawBuilder()
                        .SetRawSize(bt->second.size)
                        .SetUsage(bt->second.usage | Buffer::Usage::eTransferDst | Buffer::Usage::eTransferSrc)
                        .SetType(bt->second.type)
                        .Build();
                    previous.SetName(lifetime.resource + " (previous frame)");
                    _previousBuffers.emplace(lifetime.resource, ResourceRef<const Buffer>(previous));
                    _bufferHistory.emplace_back(ResourceRef<const Buffer>(buffer), std::move(previous));
                }
                _ownedBuffers.push_back(std::move(buffer));
            }
        }
        for (const auto& [alias, root] : compiled->aliases) {
            if (const auto it = _images.find(root); it != _images.end()) _images.emplace(alias, it->second);
            if (const auto it = _buffers.find(root); it != _buffers.end()) _buffers.emplace(alias, it->second);
            if (const auto it = _previousImages.find(root); it != _previousImages.end()) _previousImages.emplace(alias, it->second);
            if (const auto it = _previousBuffers.find(root); it != _previousBuffers.end()) _previousBuffers.emplace(alias, it->second);
        }

        // ---- schedule ---------------------------------------------------------------------------------
        for (std::size_t i = 0; i < compiled->order.size(); ++i) {
            RenderPass* pass = _passes[compiled->order[i]].get();
            _order.push_back(pass);
            _schedule.push_back({pass->Name(), compiled->level[i]});
        }
        for (const auto index : compiled->culled) _culled.push_back(_passes[index]->Name());
        for (const auto& [index, resource, source] : compiled->skipped)
            _skipped.push_back({_passes[index]->Name(), resource, _passes[source]->Name()});

        const PassResources resources(*this);
        for (RenderPass* pass : _order) pass->Initialize(resources);
        return true;
    }

    namespace {
        using Clock = std::chrono::steady_clock;
        double millisecondsSince(const Clock::time_point start) {
            return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }

        // Records a pass inside a GPU timer scope named after it, and times the recording itself.
        void RecordTimed(RenderPass& pass, std::unique_ptr<CommandBuffer>& out, double& recordMs) {
            const auto start = Clock::now();
            auto cb = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
            cb->Begin();
            cb->BeginTimer(pass.Name());
            pass.Record(*cb);
            cb->EndTimer();
            out = std::move(cb);
            recordMs = millisecondsSince(start);
        }

        Task<void> RecordOnBackground(RenderPass& pass, std::unique_ptr<CommandBuffer>& out, double& recordMs) {
            co_await Context::SwitchToBackgroundThread();
            RecordTimed(pass, out, recordMs);
        }
    }

    bool FrameGraph::Execute() {
        if (_passes.empty()) return false;
        if (const glm::uvec2 extent = Context::Window().Extent(); extent != _extent) {
            _extent = extent;
            _dirty = true;
        }
        if (_dirty) Build();
        if (_broken || _order.empty()) return false;

        std::vector<double> prepareMs(_order.size(), 0.0);
        const auto prepareStart = Clock::now();
        for (std::size_t i = 0; i < _order.size(); ++i) {
            const auto start = Clock::now();
            _order[i]->Prepare();
            prepareMs[i] = millisecondsSince(start);
        }
        const double prepareTotal = millisecondsSince(prepareStart);

        auto& scheduler = Context::Scheduler();

        // Freshly made history has nothing in it yet: give it a defined value before any pass reads it.
        if (_historyFrames == 0 && (!_imageHistory.empty() || !_bufferHistory.empty())) {
            auto clear = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
            clear->Begin();
            for (const auto& previous : _imageHistory | std::views::values) {
                if (IsDepthStencilFormat(previous->PixelFormat())) {
                    auto framebuffer = Framebuffer::Builder().SetDepth({.view = previous, .depth = 1.f}).Build();
                    clear->BeginRendering(framebuffer).EndRendering();
                    _historyClears.push_back(std::move(framebuffer));
                } else {
                    clear->ClearColorImage(previous, glm::vec4(0.f));
                }
            }
            for (const auto& previous : _bufferHistory | std::views::values) clear->ClearBuffer(previous);
            scheduler.Execute(std::move(clear));
        }

        // Every pass records at once. The order they finish in does not matter: the frame ends
        // their command buffers — which is where barriers are worked out — in execution order.
        std::vector<std::unique_ptr<CommandBuffer>> recorded(_order.size());
        std::vector<double> recordMs(_order.size(), 0.0);
        const auto recordStart = Clock::now();
        if (Context::ActiveAPI() == API::eVulkan) {
            std::vector<Task<void>> tasks;
            tasks.reserve(_order.size());
            for (std::size_t i = 0; i < _order.size(); ++i) tasks.push_back(RecordOnBackground(*_order[i], recorded[i], recordMs[i]));
            auto all = WhenAll(std::move(tasks));
            all.Wait();
            if (const auto result = all.Take(); !result) log::Error("[frame graph] a pass failed to record: {}", result.error());
        } else {
            // OpenGL is bound to this thread, so its passes record here, one after another.
            for (std::size_t i = 0; i < _order.size(); ++i) RecordTimed(*_order[i], recorded[i], recordMs[i]);
        }
        const double recordWall = millisecondsSince(recordStart);

        // The CPU side is known now; the GPU side arrives once the frame is done, per pass.
        auto& stats = *_stats;
        double recordWork = 0.0;
        for (std::size_t i = 0; i < _order.size(); ++i) {
            auto& timing = stats.passes[_order[i]->Name()];
            timing.name = _order[i]->Name();
            Stats::average(timing.prepareMs, prepareMs[i]);
            Stats::average(timing.recordMs, recordMs[i]);
            recordWork += recordMs[i];
            if (recorded[i]) {
                recorded[i]->OnTimings([weak = std::weak_ptr(_stats), name = _order[i]->Name()](const std::vector<TimerResult>& results) {
                    const auto statsNow = weak.lock();
                    if (!statsNow) return;   // the graph is gone
                    for (const auto& result : results) {
                        if (result.depth != 0 || result.label != name) continue;
                        auto& timing = statsNow->passes[name];
                        Stats::average(timing.gpuMs, result.milliseconds);
                        timing.gpuMeasured = true;
                    }
                });
            }
        }
        Stats::average(stats.graph.prepareMs, prepareTotal);
        Stats::average(stats.graph.recordWallMs, recordWall);
        Stats::average(stats.graph.recordWorkMs, recordWork);
        double gpuSum = 0.0;
        for (const RenderPass* pass : _order) gpuSum += stats.passes[pass->Name()].gpuMs;
        stats.graph.gpuMs = gpuSum;
        stats.frameMs[stats.next] = Time::FrameTime() * 1000.f;
        stats.gpuMs[stats.next] = static_cast<float>(gpuSum);
        stats.next = (stats.next + 1) % Stats::History;

        bool touchedScreen = false;
        const auto framebuffer = Context::DefaultFramebuffer();
        const auto screen = framebuffer.Valid() && !framebuffer->ColorAttachments().empty()
            ? framebuffer->ColorImage(0) : ResourceRef<const Image>{};
        for (auto& cb : recorded) {
            if (!cb) continue;  // its pass threw; already reported
            if (screen.Alive() && cb->HasTouched(screen)) touchedScreen = true;
            scheduler.Execute(std::move(cb));
        }

        // After the last pass: what this frame left becomes the next frame's previous frame.
        if (!_imageHistory.empty() || !_bufferHistory.empty()) {
            auto keep = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
            keep->Begin();
            for (const auto& [current, previous] : _imageHistory) keep->CopyImage(current, previous);
            for (const auto& [current, previous] : _bufferHistory) keep->CopyBuffer(current, previous);
            scheduler.Execute(std::move(keep));
            ++_historyFrames;
        }
        return touchedScreen;
    }

    void FrameGraph::DrawMenuItems() {
        ImGui::MenuItem("Frame graph", nullptr, &_showSchedule);
        ImGui::MenuItem("Performance", nullptr, &_showPerformance);
        ImGui::MenuItem("Pass settings", nullptr, &_showPassWindows);
    }

    void FrameGraph::DrawGUI() {
        // Begin/End pair up whatever Begin returns, and Begin may clear the flag (its close button) —
        // so the flag is read once, before either.
        if (_showSchedule) DrawSchedule();
        if (_showPerformance) DrawPerformance();
        if (_showPassWindows) for (RenderPass* pass : _order) pass->DrawGUI();
    }

    void FrameGraph::DrawSchedule() {
        if (ImGui::Begin("Frame Graph", &_showSchedule)) {
            ImGui::TextDisabled("Passes on one level do not depend on each other.");
            for (const auto& [name, level] : _schedule) ImGui::Text("%*s%u  %s", static_cast<int>(level * 2), "", level, name.c_str());
            if (!_skipped.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("Skipped (an input comes from a disabled pass):");
                for (const auto& [name, resource, source] : _skipped)
                    ImGui::BulletText("%s: needs '%s' from %s", name.c_str(), resource.c_str(), source.c_str());
            }
            if (!_culled.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("Culled (nothing reads what they make):");
                for (const auto& name : _culled) ImGui::BulletText("%s", name.c_str());
            }
            if (!_history.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("Kept for the next frame:");
                for (const auto& name : _history) ImGui::BulletText("%s", name.c_str());
            }
            ImGui::Separator();
            for (const auto& pass : _passes) {
                bool on = pass->Enabled();
                if (ImGui::Checkbox(pass->Name().c_str(), &on)) pass->SetEnabled(on);
            }
            if (_broken) ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "The graph does not compile; see the log.");
        }
        ImGui::End();
    }

    void FrameGraph::DrawPerformance() {
        if (ImGui::Begin("Performance", &_showPerformance)) {
            const auto& stats = *_stats;

            // The frame as a whole: CPU wall time between frames, and the graph's GPU time.
            float frameAverage = 0.f, frameWorst = 0.f;
            for (const float ms : stats.frameMs) { frameAverage += ms; frameWorst = std::max(frameWorst, ms); }
            frameAverage /= static_cast<float>(Stats::History);
            ImGui::Text("Frame  %.2f ms  (%.0f fps)   worst %.2f ms", frameAverage,
                        frameAverage > 0.f ? 1000.f / frameAverage : 0.f, frameWorst);
            const float ceiling = std::max(frameWorst * 1.2f, 16.7f);
            ImGui::PlotLines("##frame", stats.frameMs.data(), static_cast<int>(Stats::History),
                             static_cast<int>(stats.next), "CPU frame time", 0.f, ceiling, ImVec2(-FLT_MIN, 50.f));
            ImGui::PlotLines("##gpu", stats.gpuMs.data(), static_cast<int>(Stats::History),
                             static_cast<int>(stats.next), "GPU, frame graph", 0.f, ceiling, ImVec2(-FLT_MIN, 50.f));

            const auto& graph = stats.graph;
            ImGui::Text("Graph GPU %.2f ms   prepare %.2f ms   record %.2f ms (%.2f ms of work across threads)",
                        graph.gpuMs, graph.prepareMs, graph.recordWallMs, graph.recordWorkMs);
            ImGui::Separator();

            // Pass by pass, in the order they run, with a bar for each one's share of the GPU time.
            if (ImGui::BeginTable("passes", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 1.4f);
                ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, 60.f);
                ImGui::TableSetupColumn("share", ImGuiTableColumnFlags_WidthStretch, 1.f);
                ImGui::TableSetupColumn("Record ms", ImGuiTableColumnFlags_WidthFixed, 70.f);
                ImGui::TableSetupColumn("Prepare ms", ImGuiTableColumnFlags_WidthFixed, 72.f);
                ImGui::TableHeadersRow();
                for (const auto& timing : PassTimings()) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(timing.name.c_str());
                    ImGui::TableNextColumn();
                    if (timing.gpuMeasured) ImGui::Text("%.3f", timing.gpuMs); else ImGui::TextDisabled("n/a");
                    ImGui::TableNextColumn();
                    ImGui::ProgressBar(graph.gpuMs > 0.0 ? static_cast<float>(timing.gpuMs / graph.gpuMs) : 0.f,
                                       ImVec2(-FLT_MIN, 0.f), "");
                    ImGui::TableNextColumn(); ImGui::Text("%.3f", timing.recordMs);
                    ImGui::TableNextColumn(); ImGui::Text("%.3f", timing.prepareMs);
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("Averaged over recent frames. GPU times arrive once the GPU has run the frame.");
        }
        ImGui::End();
    }
}
