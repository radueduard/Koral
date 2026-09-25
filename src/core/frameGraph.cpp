//
// Created by radue on 9/24/2026.
//

#include "frameGraph.h"

#include <algorithm>
#include <ranges>
#include <set>

#include <imgui.h>

#include "commandBuffer.h"
#include "context.h"
#include "framebuffer.h"
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

    glm::uvec2 PassResources::Extent(const std::string_view name) const {
        const auto img = ImageNamed(name);
        return img.Alive() ? glm::uvec2(img->Extent()) : glm::uvec2(0);
    }

    ResourceRef<const Image> FrameGraph::ImageNamed(const std::string_view name) const {
        const auto it = _images.find(name);
        return it != _images.end() ? it->second : ResourceRef<const Image>{};
    }

    void RenderPass::SetEnabled(const bool enabled) {
        if (_enabled == enabled) return;
        _enabled = enabled;
        if (_graph) _graph->Invalidate();
    }

    FrameGraph::FrameGraph() = default;
    FrameGraph::~FrameGraph() = default;

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
                const glm::uvec2 size = desc.extent.value_or(glm::max(
                    glm::uvec2(glm::vec2(_extent) * desc.scale), glm::uvec2(1)));
                auto image = Image::Builder()
                    .SetIsPerFrame(true)  // one per frame in flight, so the next frame can start
                    .SetFormat(desc.format)
                    .SetUsage(desc.usage)
                    .SetExtent(size)
                    .SetMipLevels(desc.mipLevels)
                    .Build();
                image.SetName(lifetime.resource);
                _images.emplace(lifetime.resource, ResourceRef<const Image>(image));
                _imageDescs.emplace(lifetime.resource, desc);
                _ownedImages.push_back(std::move(image));
            } else if (const auto bt = bufferDescs.find(lifetime.resource); bt != bufferDescs.end()) {
                auto buffer = Buffer::RawBuilder()
                    .SetRawSize(bt->second.size)
                    .SetUsage(bt->second.usage)
                    .SetType(bt->second.type)
                    .Build();
                buffer.SetName(lifetime.resource);
                _buffers.emplace(lifetime.resource, ResourceRef<const Buffer>(buffer));
                _ownedBuffers.push_back(std::move(buffer));
            }
        }
        for (const auto& [alias, root] : compiled->aliases) {
            if (const auto it = _images.find(root); it != _images.end()) _images.emplace(alias, it->second);
            if (const auto it = _buffers.find(root); it != _buffers.end()) _buffers.emplace(alias, it->second);
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
        Task<void> RecordOnBackground(RenderPass& pass, std::unique_ptr<CommandBuffer>& out) {
            co_await Context::SwitchToBackgroundThread();
            auto cb = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
            cb->Begin();
            pass.Record(*cb);
            out = std::move(cb);
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

        for (RenderPass* pass : _order) pass->Prepare();

        // Every pass records at once. The order they finish in does not matter: the frame ends
        // their command buffers — which is where barriers are worked out — in execution order.
        std::vector<std::unique_ptr<CommandBuffer>> recorded(_order.size());
        if (Context::ActiveAPI() == API::eVulkan) {
            std::vector<Task<void>> tasks;
            tasks.reserve(_order.size());
            for (std::size_t i = 0; i < _order.size(); ++i) tasks.push_back(RecordOnBackground(*_order[i], recorded[i]));
            auto all = WhenAll(std::move(tasks));
            all.Wait();
            if (const auto result = all.Take(); !result) log::Error("[frame graph] a pass failed to record: {}", result.error());
        } else {
            // OpenGL is bound to this thread, so its passes record here, one after another.
            for (std::size_t i = 0; i < _order.size(); ++i) {
                recorded[i] = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
                recorded[i]->Begin();
                _order[i]->Record(*recorded[i]);
            }
        }

        bool touchedScreen = false;
        const auto framebuffer = Context::DefaultFramebuffer();
        const auto screen = framebuffer.Valid() && !framebuffer->ColorAttachments().empty()
            ? framebuffer->ColorImage(0) : ResourceRef<const Image>{};
        auto& scheduler = Context::Scheduler();
        for (auto& cb : recorded) {
            if (!cb) continue;  // its pass threw; already reported
            if (screen.Alive() && cb->HasTouched(screen)) touchedScreen = true;
            scheduler.Execute(std::move(cb));
        }
        return touchedScreen;
    }

    void FrameGraph::DrawGUI() {
        if (ImGui::Begin("Frame Graph")) {
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
            ImGui::Separator();
            for (const auto& pass : _passes) {
                bool on = pass->Enabled();
                if (ImGui::Checkbox(pass->Name().c_str(), &on)) pass->SetEnabled(on);
            }
            if (_broken) ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "The graph does not compile; see the log.");
        }
        ImGui::End();
        for (RenderPass* pass : _order) pass->DrawGUI();
    }
}
