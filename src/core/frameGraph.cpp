//
// Created by radue on 9/24/2026.
//

#include "frameGraph.h"
#include "scene.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
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
        // How each use asked for the resource, for the usage it is made with.
        std::vector<std::pair<std::string, Flags<Image::Usage>>> imageUses;
        std::vector<std::pair<std::string, Flags<Buffer::Usage>>> bufferUses;
        std::vector<std::pair<std::string, Flags<Image::Usage>>> previousImageUses;
        std::vector<std::pair<std::string, Flags<Buffer::Usage>>> previousBufferUses;
    };

    PassBuilder& PassBuilder::Read(const std::string_view name) {
        _impl.decl.uses.push_back({std::string(name), graph::Access::eRead});
        return *this;
    }

    namespace {
        // The states a read can declare, as the compiler compares them. An image is read in a
        // layout: sampled, or copied from — anything else (storage, attachments, several usages at
        // once) is left unknown, which keeps passes on different queues from reading it together.
        // A buffer has no layout, so any two reads of one agree.
        constexpr std::uint32_t sampledState = 1, transferSrcState = 2, bufferReadState = 1;

        graph::Use imageRead(std::string name, const graph::Access access, const Flags<Image::Usage> usage) {
            const std::uint32_t state = usage == Flags<Image::Usage>(Image::Usage::eSampled) ? sampledState
                                      : usage == Flags<Image::Usage>(Image::Usage::eTransferSrc) ? transferSrcState : 0;
            return {std::move(name), access, {}, state, true};
        }

        // What the pass that makes an image leaves it as, for readers that share a state.
        ResourceAccess handoffAccess(const std::uint32_t state) {
            return state == transferSrcState ? ResourceAccess::eTransferSrc : ResourceAccess::eAllShaderRead;
        }
    }

    PassBuilder& PassBuilder::Read(const std::string_view name, const Flags<Image::Usage> usage) {
        _impl.imageUses.emplace_back(std::string(name), usage);
        _impl.decl.uses.push_back(imageRead(std::string(name), graph::Access::eRead, usage));
        return *this;
    }

    PassBuilder& PassBuilder::Read(const std::string_view name, const Flags<Buffer::Usage> usage) {
        _impl.bufferUses.emplace_back(std::string(name), usage);
        _impl.decl.uses.push_back({std::string(name), graph::Access::eRead, {}, bufferReadState, false});
        return *this;
    }

    PassBuilder& PassBuilder::Write(const std::string_view name) {
        _impl.decl.uses.push_back({std::string(name), graph::Access::eWrite});
        return *this;
    }

    PassBuilder& PassBuilder::Write(const std::string_view name, const Flags<Image::Usage> usage) {
        _impl.imageUses.emplace_back(std::string(name), usage);
        return Write(name);
    }

    PassBuilder& PassBuilder::Write(const std::string_view name, const Flags<Buffer::Usage> usage) {
        _impl.bufferUses.emplace_back(std::string(name), usage);
        return Write(name);
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

    PassBuilder& PassBuilder::Consume(const std::string_view name, const std::string_view as, const Flags<Image::Usage> usage) {
        _impl.imageUses.emplace_back(std::string(name), usage);
        return Consume(name, as);
    }

    PassBuilder& PassBuilder::Consume(const std::string_view name, const std::string_view as, const Flags<Buffer::Usage> usage) {
        _impl.bufferUses.emplace_back(std::string(name), usage);
        return Consume(name, as);
    }

    PassBuilder& PassBuilder::SideEffect() {
        _impl.decl.sideEffect = true;
        return *this;
    }

    PassBuilder& PassBuilder::ReadPrevious(const std::string_view name) {
        _impl.decl.uses.push_back({std::string(name), graph::Access::eReadPrevious});
        return *this;
    }

    PassBuilder& PassBuilder::ReadPrevious(const std::string_view name, const Flags<Image::Usage> usage) {
        _impl.previousImageUses.emplace_back(std::string(name), usage);
        _impl.decl.uses.push_back(imageRead(std::string(name), graph::Access::eReadPrevious, usage));
        return *this;
    }

    PassBuilder& PassBuilder::ReadPrevious(const std::string_view name, const Flags<Buffer::Usage> usage) {
        _impl.previousBufferUses.emplace_back(std::string(name), usage);
        _impl.decl.uses.push_back({std::string(name), graph::Access::eReadPrevious, {}, bufferReadState, false});
        return *this;
    }

    PassBuilder& PassBuilder::AsyncCompute() {
        _impl.decl.async = true;
        return *this;
    }

    Scene* FrameGraph::OwnerScene() const { return _scene ? _scene : Scene::Current(); }

    Window* FrameGraph::TargetWindow() const
    {
        if (_target) return _target;
        const Scene* scene = OwnerScene();
        return scene ? &scene->SceneWindow() : nullptr;
    }

    namespace {
        // The image the graph's screen is this frame — its view's, or its scene's window's — or empty
        // when there is nothing to ask.
        ResourceRef<const Image> screenOf(const Window* window) {
            if (!window) return {};
            const auto framebuffer = window->DefaultFramebuffer();
            return framebuffer.Valid() && !framebuffer->ColorAttachments().empty()
                ? framebuffer->ColorImage(0) : ResourceRef<const Image>{};
        }
    }

    ResourceRef<const Image> PassResources::ImageNamed(const std::string_view name) const {
        if (name == FrameGraph::Screen) return screenOf(_graph.TargetWindow());
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

    ResourceRef<Buffer> PassResources::WritableBufferNamed(const std::string_view name) const {
        const auto id = _graph._resourceIds.find(name);
        const auto it = id == _graph._resourceIds.end() ? _graph._allocated.end()
            : std::ranges::find(_graph._allocated, id->second, &FrameGraph::Allocated::id);
        if (it == _graph._allocated.end() || !it->buffer.Valid()) {
            log::Error("[frame graph] no buffer named '{}' that the graph made", name);
            return {};
        }
        if (it->buffer->MemoryType() == Buffer::Type::eDeviceLocal) {
            log::Error("[frame graph] '{}' is device-local: the CPU cannot write it. Create it with a host-visible "
                       "type (Buffer::Type::eDynamic).", name);
            return {};
        }
        return ResourceRef<Buffer>(it->buffer);
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

    ResourceRef<const Image> FrameGraph::ImageNamed(const std::string_view name, const Flags<Image::Usage> usage) const {
        const auto it = _images.find(name);
        if (it == _images.end()) return {};
        // Whoever asks means to look at it after the frame, so it gets an image to itself — made with
        // whatever they said they would do with it. Not from a pass recording: nothing may change then.
        if (!_recording) {
            const auto kept = _kept.find(name);
            if (kept == _kept.end()) {
                _kept.emplace(std::string(name), usage);
                if (_aliasing || usage.Value() != 0) _dirty = true;
            } else if ((kept->second | usage) != kept->second) {
                kept->second |= usage;
                _dirty = true;
            }
        }
        return it->second;
    }

    bool FrameGraph::HasPrevious(const std::string_view name) const {
        const auto it = _historyIndex.find(name);
        return it != _historyIndex.end() && _historyCopies[it->second].frames > 0;
    }

    bool RenderPass::HasPrevious(const std::string_view name) const {
        return _graph && _graph->HasPrevious(name);
    }

    void RenderPass::SetEnabled(const bool enabled) {
        if (_enabled == enabled) return;
        if (_graph && !_graph->Mutable("Switching a pass on or off")) return;
        _enabled = enabled;
        if (_graph) _graph->Invalidate();
    }

    void RenderPass::RequestInitialize() {
        if (_graph && !_graph->Mutable("RequestInitialize")) return;
        _initializeRequested = true;
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

    FrameGraph::FrameGraph() : _stats(std::make_shared<Stats>()), _owner(std::this_thread::get_id()) {}
    FrameGraph::~FrameGraph() = default;

    std::vector<FrameGraph::PassTiming> FrameGraph::PassTimings() const {
        std::vector<PassTiming> out;
        for (const RenderPass* pass : _order)
            if (const auto it = _stats->passes.find(pass->Name()); it != _stats->passes.end()) out.push_back(it->second);
        return out;
    }

    FrameGraph::GraphTiming FrameGraph::Timing() const { return _stats->graph; }

    bool FrameGraph::Mutable(const std::string_view what) const {
        if (_recording) {
            log::Error("[frame graph] {} while the passes record: ignored. Record must leave the graph alone; "
                       "change it from Update or Prepare.", what);
            return false;
        }
        if (std::this_thread::get_id() != _owner) {
            log::Error("[frame graph] {} from another thread: ignored. Change the graph from the thread that runs it "
                       "(co_await kor::Context::SwitchToMainThread() first).", what);
            return false;
        }
        return true;
    }

    void FrameGraph::Adopt(std::unique_ptr<RenderPass> pass) {
        if (!Mutable(std::format("Adding pass '{}'", pass->Name()))) {
            // Kept, never run: the caller holds a reference to it.
            _refused.push_back(std::move(pass));
            return;
        }
        pass->_graph = this;
        _passes.push_back(std::move(pass));
        _dirty = true;
    }

    void FrameGraph::Invalidate() {
        if (!Mutable("Invalidate")) return;
        _dirty = true;
    }

    void FrameGraph::SetAliasing(const bool enabled) {
        if (_aliasing == enabled || !Mutable("SetAliasing")) return;
        _aliasing = enabled;
        _dirty = true;
    }

    void FrameGraph::Import(std::string name, ResourceRef<const Image> image) {
        if (!Mutable(std::format("Importing '{}'", name))) return;
        // Passes build their descriptor sets from these in Initialize, so a name that now means a
        // different resource needs the passes using it initialized again.
        const auto it = _importedImages.find(name);
        if (it == _importedImages.end() || it->second.Get() != image.Get()) {
            _dirty = true;
            _importIds[name] = _nextId++;
        }
        _importedImages.insert_or_assign(std::move(name), std::move(image));
    }

    void FrameGraph::Import(std::string name, ResourceRef<const Buffer> buffer) {
        if (!Mutable(std::format("Importing '{}'", name))) return;
        const auto it = _importedBuffers.find(name);
        if (it == _importedBuffers.end() || it->second.Get() != buffer.Get()) {
            _dirty = true;
            _importIds[name] = _nextId++;
        }
        _importedBuffers.insert_or_assign(std::move(name), std::move(buffer));
    }

    namespace {
        std::string shapeOf(const Image::Format format, const glm::uvec2 size, const glm::u32 mips) {
            return std::format("image {} {}x{} {}", static_cast<int>(format), size.x, size.y, mips);
        }
        std::string shapeOf(const glm::i64 size, const Buffer::Type type) {
            return std::format("buffer {} {}", size, static_cast<int>(type));
        }
        glm::u64 bytesOf(const Image::Format format, const glm::uvec2 size, const glm::u32 mips) {
            glm::u64 bytes = 0;
            for (glm::u32 level = 0; level < std::max(mips, 1u); ++level) {
                const glm::uvec2 extent = glm::max(size >> level, glm::uvec2(1));
                bytes += Image::SizeOfRegion(format, glm::uvec3(extent, 1u));
            }
            return bytes;
        }
    }

    bool FrameGraph::Build() {
        _dirty = false;
        _broken = false;
        _order.clear();
        _schedule.clear();
        _culled.clear();
        _skipped.clear();
        _dependencies.clear();

        // ---- declarations -------------------------------------------------------------------------
        // Disabled passes are declared too: the compiler needs what they would have made to take
        // them out without breaking the passes that depended on them.
        std::vector<PassBuilder::Impl> declarations;
        for (const auto& pass : _passes) {
            PassBuilder::Impl impl{.decl = {.name = pass->Name(), .enabled = pass->Enabled(), .cpu = pass->RunsOnCpu()}};
            PassBuilder builder(impl);
            pass->Setup(builder);
            declarations.push_back(std::move(impl));
        }
        std::vector<graph::PassDecl> decls;
        for (auto& d : declarations) {
            // The screen's image is acquired and presented on the graphics queue, and waited for there.
            if (d.decl.async && std::ranges::any_of(d.decl.uses, [](const graph::Use& use) { return use.resource == Screen; })) {
                log::Warn("[frame graph] pass '{}' uses the screen, so it runs on the graphics queue rather than "
                          "the async compute queue", d.decl.name);
                d.decl.async = false;
            }
            // Two queue families: what an async pass imports has to have been made shared between them.
            if (d.decl.async && Context::AsyncComputeIsSeparateFamily()) {
                for (const auto& use : d.decl.uses) {
                    bool unshared = false;
                    if (const auto image = _importedImages.find(use.resource); image != _importedImages.end())
                        unshared = image->second.Valid() && !image->second->IsSharedAcrossQueues();
                    if (const auto buffer = _importedBuffers.find(use.resource); buffer != _importedBuffers.end())
                        unshared = buffer->second.Valid() && !buffer->second->IsSharedAcrossQueues();
                    if (!unshared) continue;
                    if (_demoted.insert(d.decl.name).second)
                        log::Warn("[frame graph] pass '{}' imports '{}', which was not made shared across queues "
                                  "(SetSharedAcrossQueues), so it runs on the graphics queue", d.decl.name, use.resource);
                    d.decl.async = false;
                    break;
                }
            }
            decls.push_back(d.decl);
        }

        std::set<std::string> imported{std::string(Screen)};
        for (const auto& name : _importedImages | std::views::keys) imported.insert(name);
        for (const auto& name : _importedBuffers | std::views::keys) imported.insert(name);

        const auto compiled = graph::compile(decls, imported);
        if (!compiled) {
            log::Error("[frame graph] {}", compiled.error().message);
            _broken = true;
            return false;
        }
        const auto rootOf = [&](const std::string& name) {
            const auto it = compiled->aliases.find(name);
            return it != compiled->aliases.end() ? it->second : name;
        };

        // ---- what each resource the graph makes has to be ------------------------------------------
        // The creator's description, and the usage every pass asked for, whichever name it used.
        std::map<std::string, ImageDesc, std::less<>> imageDescs;
        std::map<std::string, BufferDesc, std::less<>> bufferDescs;
        std::map<std::string, Flags<Image::Usage>, std::less<>> imageUsage, previousImageUsage;
        std::map<std::string, Flags<Buffer::Usage>, std::less<>> bufferUsage, previousBufferUsage;
        for (std::size_t p = 0; p < declarations.size(); ++p) {
            const auto& d = declarations[p];
            if (!_passes[p]->Enabled()) continue;   // a disabled pass asks for nothing
            for (const auto& [name, desc] : d.images) { imageDescs.emplace(name, desc); imageUsage[name] |= desc.usage; }
            for (const auto& [name, desc] : d.buffers) { bufferDescs.emplace(name, desc); bufferUsage[name] |= desc.usage; }
            for (const auto& [name, usage] : d.imageUses) imageUsage[rootOf(name)] |= usage;
            for (const auto& [name, usage] : d.bufferUses) bufferUsage[rootOf(name)] |= usage;
            for (const auto& [name, usage] : d.previousImageUses) previousImageUsage[rootOf(name)] |= usage;
            for (const auto& [name, usage] : d.previousBufferUses) previousBufferUsage[rootOf(name)] |= usage;
        }
        const std::set<std::string, std::less<>> history(compiled->history.begin(), compiled->history.end());

        // ---- which can share memory ------------------------------------------------------------------
        struct Planned {
            std::string name;
            bool image = false;
            std::string shape;
            ImageDesc imageDesc;
            glm::uvec2 size {0};
            Flags<Image::Usage> imageUsage {};
            BufferDesc bufferDesc;
            Flags<Buffer::Usage> bufferUsage {};
            bool perFrame = false;
            bool shared = false;   ///< An async pass uses it, and the queues are two families.
            glm::u64 bytes = 0;
        };
        const bool separateFamily = Context::AsyncComputeIsSeparateFamily();
        std::vector<Planned> planned(compiled->lifetimes.size());
        std::vector<std::string> shareKeys(compiled->lifetimes.size());
        std::vector<std::string> problems;
        for (std::size_t i = 0; i < compiled->lifetimes.size(); ++i) {
            const std::string& name = compiled->lifetimes[i].resource;
            planned[i].shared = separateFamily && compiled->lifetimes[i].async;
            const bool keepsHistory = history.contains(name);
            Planned& plan = planned[i];
            plan.name = name;
            bool shareable = _aliasing && !keepsHistory && !_kept.contains(name);
            if (const auto it = imageDescs.find(name); it != imageDescs.end()) {
                plan.image = true;
                plan.imageDesc = it->second;
                glm::uvec2 base = _extent;
                if (const auto& of = plan.imageDesc.sizeOf; !of.empty() && !plan.imageDesc.extent) {
                    ResourceRef<const Image> reference;
                    if (const auto imported = _importedImages.find(of); imported != _importedImages.end()) reference = imported->second;
                    if (reference.Alive()) base = glm::uvec2(reference->Extent());
                    else problems.push_back(std::format("'{}' is sized after '{}', which is not an imported image.", name, of));
                }
                plan.size = plan.imageDesc.extent.value_or(glm::max(
                    glm::uvec2(glm::vec2(base) * plan.imageDesc.scale), glm::uvec2(1)));
                plan.imageUsage = imageUsage[name];
                if (const auto kept = _kept.find(name); kept != _kept.end()) plan.imageUsage |= kept->second;
                if (keepsHistory) plan.imageUsage |= Image::Usage::eTransferSrc;
                if (plan.imageUsage.Value() == 0) problems.push_back(std::format(
                    "'{}': no pass says how it uses it. Give its Create a usage, or the Read/Write that uses it one.", name));
                plan.shape = shapeOf(plan.imageDesc.format, plan.size, plan.imageDesc.mipLevels);
                plan.bytes = bytesOf(plan.imageDesc.format, plan.size, plan.imageDesc.mipLevels);
            } else if (const auto bt = bufferDescs.find(name); bt != bufferDescs.end()) {
                plan.bufferDesc = bt->second;
                plan.bufferUsage = bufferUsage[name];
                if (keepsHistory) plan.bufferUsage |= Buffer::Usage::eTransferSrc;
                // The CPU writes any buffer that is not device-local: one per frame in flight, so the
                // frame being written is never the one the GPU reads, and never shared.
                plan.perFrame = plan.bufferDesc.type != Buffer::Type::eDeviceLocal;
                shareable = shareable && !plan.perFrame;
                plan.shape = shapeOf(plan.bufferDesc.size, plan.bufferDesc.type);
                plan.bytes = static_cast<glm::u64>(std::max<glm::i64>(plan.bufferDesc.size, 0))
                           * (plan.perFrame ? Context::Scheduler().ImageCount() : 1u);
            } else {
                continue;
            }
            // What an async pass uses is in use at times the order does not describe.
            if (shareable && !compiled->lifetimes[i].async) shareKeys[i] = plan.shape;
        }
        if (!problems.empty()) {
            std::string message;
            for (const auto& problem : problems) message += (message.empty() ? "" : "\n") + problem;
            log::Error("[frame graph] {}", message);
            _broken = true;
            return false;
        }
        const auto slots = graph::packLifetimes(compiled->lifetimes, shareKeys);
        const std::size_t slotCount = slots.empty() ? 0 : *std::ranges::max_element(slots) + 1;

        // ---- allocation, reusing what still fits ---------------------------------------------------
        // What a slot is allocated as: the shape its resources share, the usage they add up to, and
        // for one resource alone, its name — so it keeps its own image across a rebuild.
        std::vector<std::vector<std::size_t>> members(slotCount);
        for (std::size_t i = 0; i < planned.size(); ++i) if (!planned[i].shape.empty()) members[slots[i]].push_back(i);

        auto previous = std::move(_allocated);
        _allocated.clear();
        const auto takeReusable = [&](const std::string& key) -> std::optional<Allocated> {
            const auto it = std::ranges::find(previous, key, &Allocated::key);
            if (it == previous.end()) return std::nullopt;
            Allocated found = std::move(*it);
            previous.erase(it);
            return found;
        };

        _images.clear();
        _buffers.clear();
        _resourceIds.clear();
        for (const auto& [name, image] : _importedImages) _images.emplace(name, image);
        for (const auto& [name, buffer] : _importedBuffers) _buffers.emplace(name, buffer);
        for (const auto& [name, id] : _importIds) _resourceIds.emplace(name, id);

        MemoryUse memory;
        for (const auto& slot : members) {
            if (slot.empty()) continue;
            const Planned& first = planned[slot.front()];
            std::string names;
            for (const auto i : slot) names += (names.empty() ? "" : " + ") + planned[i].name;
            memory.resources += static_cast<glm::u32>(slot.size());
            for (const auto i : slot) memory.unsharedBytes += planned[i].bytes;
            memory.bytes += first.bytes;

            if (first.image) {
                Flags<Image::Usage> usage {};
                for (const auto i : slot) usage |= planned[i].imageUsage;
                std::string key = std::format("{} usage {}{}", first.shape, static_cast<unsigned>(usage.Value()), first.shared ? " shared" : "");
                if (slot.size() == 1 && shareKeys[slot.front()].empty()) key += " own " + first.name;
                auto allocation = takeReusable(key);
                if (!allocation) {
                    auto image = Image::Builder()
                        .SetFormat(first.imageDesc.format)
                        .SetUsage(usage)
                        .SetExtent(first.size)
                        .SetMipLevels(first.imageDesc.mipLevels)
                        .SetSharedAcrossQueues(first.shared)
                        .Build();
                    allocation = Allocated{.key = key, .id = _nextId++, .image = std::move(image)};
                }
                allocation->image.SetName(names);
                for (const auto i : slot) {
                    _images.emplace(planned[i].name, ResourceRef<const Image>(allocation->image));
                    _resourceIds.emplace(planned[i].name, allocation->id);
                }
                _allocated.push_back(std::move(*allocation));
            } else {
                Flags<Buffer::Usage> usage {};
                for (const auto i : slot) usage |= planned[i].bufferUsage;
                std::string key = std::format("{} usage {}{}", first.shape, static_cast<unsigned>(usage.Value()), first.shared ? " shared" : "");
                if (slot.size() == 1 && shareKeys[slot.front()].empty()) key += " own " + first.name;
                auto allocation = takeReusable(key);
                if (!allocation) {
                    Buffer::RawBuilder builder;
                    builder.SetRawSize(first.bufferDesc.size).SetUsage(usage).SetType(first.bufferDesc.type)
                        .SetSharedAcrossQueues(first.shared);
                    if (first.perFrame) builder.SetIsPerFrame(true);
                    auto buffer = builder.Build();
                    allocation = Allocated{.key = key, .id = _nextId++, .buffer = std::move(buffer)};
                }
                allocation->buffer.SetName(names);
                for (const auto i : slot) {
                    _buffers.emplace(planned[i].name, ResourceRef<const Buffer>(allocation->buffer));
                    _resourceIds.emplace(planned[i].name, allocation->id);
                }
                _allocated.push_back(std::move(*allocation));
            }
        }
        memory.allocations = static_cast<glm::u32>(_allocated.size());

        // ---- previous frames ---------------------------------------------------------------------------
        // A second copy of each, kept from one frame to the next. Reused across a rebuild when it still
        // fits, so a rebuild that did not touch the resource does not throw last frame away.
        auto previousHistory = std::move(_historyCopies);
        _historyCopies.clear();
        _historyIndex.clear();
        _previousImages.clear();
        _previousBuffers.clear();
        _historyClears.clear();
        _history = compiled->history;
        for (const auto& name : compiled->history) {
            const auto plan = std::ranges::find(planned, name, &Planned::name);
            if (plan == planned.end() || plan->shape.empty()) continue;
            History entry{.name = name};
            if (plan->image) {
                // One image, not one per frame: it carries a value from one frame to the next, and the
                // GPU runs frames in order, so the barriers are all it needs. Written by the copy;
                // readable by one too, so it can be inspected or read back.
                auto usage = plan->imageUsage | previousImageUsage[name]
                           | Image::Usage::eTransferDst | Image::Usage::eTransferSrc;
                if (IsDepthStencilFormat(plan->imageDesc.format)) usage |= Image::Usage::eDepthStencilAttachment;
                entry.key = std::format("{} usage {}", plan->shape, static_cast<unsigned>(usage.Value()));
                entry.image = _images.at(name);
                if (const auto old = std::ranges::find_if(previousHistory, [&](const History& h) {
                        return h.name == name && h.key == entry.key; }); old != previousHistory.end()) {
                    entry.previousImage = std::move(old->previousImage);
                    entry.id = old->id;
                    entry.frames = old->frames;
                } else {
                    entry.previousImage = Image::Builder()
                        .SetFormat(plan->imageDesc.format)
                        .SetUsage(usage)
                        .SetExtent(plan->size)
                        .SetMipLevels(plan->imageDesc.mipLevels)
                        .SetSharedAcrossQueues(separateFamily)   // an async pass may read last frame's
                        .Build();
                    entry.previousImage.SetName(name + " (previous frame)");
                    entry.id = _nextId++;
                }
            } else {
                const auto usage = plan->bufferUsage | previousBufferUsage[name]
                                 | Buffer::Usage::eTransferDst | Buffer::Usage::eTransferSrc;
                entry.key = std::format("{} usage {}", plan->shape, static_cast<unsigned>(usage.Value()));
                entry.buffer = _buffers.at(name);
                if (const auto old = std::ranges::find_if(previousHistory, [&](const History& h) {
                        return h.name == name && h.key == entry.key; }); old != previousHistory.end()) {
                    entry.previousBuffer = std::move(old->previousBuffer);
                    entry.id = old->id;
                    entry.frames = old->frames;
                } else {
                    entry.previousBuffer = Buffer::RawBuilder()
                        .SetRawSize(plan->bufferDesc.size)
                        .SetUsage(usage)
                        .SetType(plan->bufferDesc.type)
                        .SetSharedAcrossQueues(separateFamily)
                        .Build();
                    entry.previousBuffer.SetName(name + " (previous frame)");
                    entry.id = _nextId++;
                }
            }
            memory.bytes += plan->bytes;
            memory.unsharedBytes += plan->bytes;
            ++memory.allocations;
            if (entry.previousImage.Valid()) _previousImages.emplace(name, ResourceRef<const Image>(entry.previousImage));
            if (entry.previousBuffer.Valid()) _previousBuffers.emplace(name, ResourceRef<const Buffer>(entry.previousBuffer));
            _resourceIds.emplace("previous " + name, entry.id);
            _historyIndex.emplace(name, _historyCopies.size());
            _historyCopies.push_back(std::move(entry));
        }
        _memory = memory;

        for (const auto& [alias, root] : compiled->aliases) {
            if (const auto it = _images.find(root); it != _images.end()) _images.emplace(alias, it->second);
            if (const auto it = _buffers.find(root); it != _buffers.end()) _buffers.emplace(alias, it->second);
            if (const auto it = _previousImages.find(root); it != _previousImages.end()) _previousImages.emplace(alias, it->second);
            if (const auto it = _previousBuffers.find(root); it != _previousBuffers.end()) _previousBuffers.emplace(alias, it->second);
            if (const auto it = _resourceIds.find(root); it != _resourceIds.end()) _resourceIds.emplace(alias, it->second);
            if (const auto it = _resourceIds.find("previous " + root); it != _resourceIds.end()) _resourceIds.emplace("previous " + alias, it->second);
            if (const auto it = _historyIndex.find(root); it != _historyIndex.end()) _historyIndex.emplace(alias, it->second);
        }

        // ---- schedule ---------------------------------------------------------------------------------
        for (std::size_t i = 0; i < compiled->order.size(); ++i) {
            RenderPass* pass = _passes[compiled->order[i]].get();
            _order.push_back(pass);
            _schedule.push_back({pass->Name(), compiled->level[i], compiled->async[i]});
        }
        _dependencies = compiled->dependencies;
        _async = compiled->async;
        _waits = compiled->waits;
        _handoffs.assign(_order.size(), {});
        for (const auto& [pass, resource, state] : compiled->handoffs) {
            if (const auto it = _images.find(resource); it != _images.end())
                _handoffs[pass].push_back({it->second, handoffAccess(state)});
        }
        for (const auto index : compiled->culled) _culled.push_back(decls[index].name);
        for (const auto& [index, resource, source] : compiled->skipped)
            _skipped.push_back({decls[index].name, resource, decls[source].name});

        // ---- Initialize what changed ------------------------------------------------------------------
        // A pass is initialized again only when something it names is not what it was given last time.
        const PassResources resources(*this);
        for (RenderPass* pass : _order) {
            const auto& decl = declarations[static_cast<std::size_t>(
                std::ranges::find(_passes, pass, &std::unique_ptr<RenderPass>::get) - _passes.begin())].decl;
            std::vector<glm::u64> signature;
            for (const auto& use : decl.uses) {
                const std::string name = use.access == graph::Access::eReadPrevious ? "previous " + use.resource : use.resource;
                if (use.resource == Screen) {
                    const auto screen = screenOf(TargetWindow());
                    signature.push_back(screen.Alive() ? reinterpret_cast<std::uintptr_t>(screen.Get()) : 0);
                    signature.push_back(screen.Alive() ? screen->Generation() : 0);
                    continue;
                }
                const auto it = _resourceIds.find(name);
                signature.push_back(it != _resourceIds.end() ? it->second : 0);
            }
            const auto last = _initializedWith.find(pass);
            if (pass->_initializeRequested || last == _initializedWith.end() || last->second != signature) {
                pass->_initializeRequested = false;
                pass->Initialize(resources);
                _initializedWith.insert_or_assign(pass, std::move(signature));
            }
        }
        return true;
    }

    namespace {
        using Clock = std::chrono::steady_clock;
        double millisecondsSince(const Clock::time_point start) {
            return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }

        // Runs a CPU pass, or records a GPU one inside a GPU timer scope named after it; either way
        // timing the CPU side.
        void RunTimed(RenderPass& pass, const bool onCpu, const bool async, const std::vector<ImageBarrier>& handoffs,
                      std::unique_ptr<CommandBuffer>& out, double& ms) {
            const auto start = Clock::now();
            if (onCpu) {
                static_cast<CpuPass&>(pass).Run();
            } else {
                auto cb = CommandBuffer::Create(async ? CommandBuffer::Usage::eAsyncCompute : CommandBuffer::Usage::eGraphics);
                cb->Begin();
                cb->BeginTimer(pass.Name());
                static_cast<const RenderPass&>(pass).Record(*cb);
                // Images read on both queues next, left in the state they are read in — so no reader
                // moves one under another running at the same time.
                if (!handoffs.empty()) cb->Barrier({}, handoffs);
                cb->EndTimer();
                out = std::move(cb);
            }
            ms = millisecondsSince(start);
        }

        // Runs on the background pool once the CPU passes this one depends on are done. It moves there
        // before it waits: a token resumes whoever awaits it on the executor they awaited from, and the
        // main thread is blocked until every pass has run.
        Task<void> RunOnBackground(RenderPass& pass, const bool onCpu, const bool async, const std::vector<ImageBarrier>& handoffs,
                                   std::unique_ptr<CommandBuffer>& out, double& ms,
                                   std::vector<Token> after, std::shared_ptr<detail::SceneLife> scene, Window* target) {
            co_await Context::SwitchToBackgroundThread();
            if (!after.empty()) co_await WhenAll(std::move(after));
            // The scene the graph belongs to is current while its passes record, as it is while the
            // scene runs: `Window::` in a pass, and a render pass opened without a framebuffer, are its.
            detail::SceneScope scope(scene);
            detail::WindowScope window(target);
            RunTimed(pass, onCpu, async, handoffs, out, ms);
        }
    }

    bool FrameGraph::Execute() {
        _owner = std::this_thread::get_id();
        if (_passes.empty()) return false;
        Scene* scene = OwnerScene();
        if (!scene) {
            log::Error("[frame graph] runs inside a scene: its window is the one it draws into");
            return false;
        }
        detail::SceneScope scope(scene);
        detail::WindowScope window(_target);
        if (const glm::uvec2 extent = TargetWindow()->Extent(); extent != _extent) {
            _extent = extent;
            _dirty = true;
        }
        // The window's image can be another one at the same size — a window minimized and brought back,
        // a swap chain made again for the system's reasons: what the passes made from the old one is stale.
        if (const auto screen = screenOf(TargetWindow()); screen.Alive()) {
            const auto which = reinterpret_cast<std::uintptr_t>(screen.Get());
            if (which != _screenSeen || screen->Generation() != _screenGeneration) {
                _screenSeen = which;
                _screenGeneration = screen->Generation();
                _dirty = true;
            }
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
        if (std::ranges::any_of(_historyCopies, [](const History& h) { return h.frames == 0; })) {
            auto clear = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
            clear->Begin();
            for (const auto& h : _historyCopies) {
                if (h.frames != 0) continue;
                if (h.previousImage.Valid()) {
                    if (IsDepthStencilFormat(h.previousImage->PixelFormat())) {
                        auto framebuffer = Framebuffer::Builder().SetDepth({.view = h.previousImage, .depth = 1.f}).Build();
                        clear->BeginRendering(framebuffer).EndRendering();
                        _historyClears.push_back(std::move(framebuffer));
                    } else {
                        clear->ClearColorImage(h.previousImage, glm::vec4(0.f));
                    }
                }
                if (h.previousBuffer.Valid()) clear->ClearBuffer(h.previousBuffer);
            }
            scheduler.Execute(std::move(clear));
        }

        // Every pass at once, each as soon as the CPU passes it depends on are done. The order GPU
        // passes finish recording in does not matter: the frame ends their command buffers — which
        // is where barriers are worked out — in execution order.
        std::vector<std::unique_ptr<CommandBuffer>> recorded(_order.size());
        std::vector<double> recordMs(_order.size(), 0.0);
        const auto recordStart = Clock::now();
        _recording = true;
        {
            std::vector<Task<void>> tasks;
            tasks.reserve(_order.size());
            for (std::size_t i = 0; i < _order.size(); ++i) {
                std::vector<Token> after;
                for (const auto dependency : _dependencies[i])
                    if (_order[dependency]->RunsOnCpu()) after.push_back(tasks[dependency].Completion());
                tasks.push_back(RunOnBackground(*_order[i], _order[i]->RunsOnCpu(), _async[i], _handoffs[i], recorded[i], recordMs[i], std::move(after),
                                                scene->Life().lock(), _target));
            }
            auto all = WhenAll(std::move(tasks));
            all.Wait();
            if (const auto result = all.Take(); !result) log::Error("[frame graph] a pass failed to run: {}", result.error());
        }
        _recording = false;
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
        stats.frameMs[stats.next] = scene->SceneTime().UnscaledFrameTime() * 1000.f;
        stats.gpuMs[stats.next] = static_cast<float>(gpuSum);
        stats.next = (stats.next + 1) % Stats::History;

        // Handed over in order, as one group: the graph orders its own passes, so a pass on the async
        // queue waits only for the pass on the graphics queue it needs — and the other way round —
        // and runs alongside the rest.
        bool touchedScreen = false;
        const auto screen = screenOf(TargetWindow());
        const auto group = reinterpret_cast<std::uintptr_t>(this);
        std::vector<Token> done(_order.size());
        std::optional<std::size_t> lastAsync;
        for (std::size_t i = 0; i < recorded.size(); ++i) {
            auto& cb = recorded[i];
            if (!cb) continue;  // a CPU pass, or one that threw (already reported)
            if (screen.Alive() && cb->HasTouched(screen)) touchedScreen = true;
            Scheduler::ExecuteInfo info{ .group = group };
            if (_waits[i]) info.after.push_back(done[*_waits[i]]);
            if (_async[i]) lastAsync = i;
            done[i] = scheduler.Execute(std::move(cb), std::move(info));
        }

        // After the last pass: what this frame left becomes the next frame's previous frame.
        if (!_historyCopies.empty()) {
            auto keep = CommandBuffer::Create(CommandBuffer::Usage::eGraphics);
            keep->Begin();
            for (auto& h : _historyCopies) {
                if (h.previousImage.Valid()) keep->CopyImage(h.image, h.previousImage);
                if (h.previousBuffer.Valid()) keep->CopyBuffer(h.buffer, h.previousBuffer);
                ++h.frames;
            }
            // After every pass, the async ones included.
            Scheduler::ExecuteInfo info{ .group = group };
            if (lastAsync) info.after.push_back(done[*lastAsync]);
            scheduler.Execute(std::move(keep), std::move(info));
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
            for (const auto& [name, level, async] : _schedule)
                ImGui::Text("%*s%u  %s%s", static_cast<int>(level * 2), "", level, name.c_str(), async ? "  (async compute)" : "");
            if (std::ranges::any_of(_schedule, &Scheduled::async) && !Context::SupportsAsyncCompute())
                ImGui::TextDisabled("This device has no async compute queue: async passes run in order.");
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
            const auto megabytes = [](const glm::u64 bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); };
            ImGui::Text("Memory  %.1f MB: %u resources in %u allocations", megabytes(_memory.bytes),
                        _memory.resources, _memory.allocations);
            if (_memory.unsharedBytes > _memory.bytes)
                ImGui::TextDisabled("%.1f MB saved by sharing", megabytes(_memory.unsharedBytes - _memory.bytes));
            if (bool aliasing = _aliasing; ImGui::Checkbox("Share memory between resources", &aliasing)) SetAliasing(aliasing);
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
