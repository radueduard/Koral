//
// koral-ui: keeping the GPU's copy of a layer tree current with as few bytes as possible, and drawing it.
//
// Every layer has slots of its own in the frame's tables — its instances, mesh vertices, clips,
// gradients and element parameters — which stay where they are while the layer lives. A layer whose
// picture changed rewrites its slots and nothing else; one that only moved rewrites its entry in the
// layer table. What puts them in paint order is a pair of small index arrays, the draw order, which
// the shaders read through: so draws still merge across layers into one, and a change in the middle
// of the frame moves four bytes an instance instead of ninety-six.
//

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <map>
#include <span>
#include <unordered_map>

#include <buffer.h>
#include <context.h>
#include <descriptorSet.h>
#include <graphicsPipeline.h>
#include <image.h>
#include <log.h>
#include <sampler.h>
#include <shader.h>

#include <kui/render.h>
#include <kui/widgets.h>

#include "atlas.h"
#include "gpu.h"

namespace kui
{
    using namespace detail;

    namespace {
        /** @brief Space in a table: first fit, with neighbouring free blocks merged. */
        class Allocator {
        public:
            std::uint32_t Allocate(const std::uint32_t count)
            {
                if (count == 0) return 0;
                for (auto it = _free.begin(); it != _free.end(); ++it) {
                    if (it->second < count) continue;
                    const std::uint32_t offset = it->first;
                    it->first += count;
                    it->second -= count;
                    if (it->second == 0) _free.erase(it);
                    return offset;
                }
                const std::uint32_t offset = _end;
                _end += count;
                return offset;
            }

            void Free(const std::uint32_t offset, const std::uint32_t count)
            {
                if (count == 0) return;
                auto it = std::ranges::lower_bound(_free, offset, {}, &std::pair<std::uint32_t, std::uint32_t>::first);
                it = _free.insert(it, { offset, count });
                // Merge with the next, then with the previous.
                if (const auto next = std::next(it); next != _free.end() && it->first + it->second == next->first) {
                    it->second += next->second;
                    _free.erase(next);
                }
                if (it != _free.begin()) {
                    if (const auto prev = std::prev(it); prev->first + prev->second == it->first) {
                        prev->second += it->second;
                        it = _free.erase(it);
                        it = std::prev(it);
                    }
                }
                // A free block at the end gives the space back.
                if (it->first + it->second == _end) {
                    _end = it->first;
                    _free.erase(it);
                }
            }

            [[nodiscard]] std::uint32_t End() const { return _end; }
            [[nodiscard]] std::uint32_t FreeCount() const
            {
                std::uint32_t n = 0;
                for (const auto& [o, c] : _free) n += c;
                return n;
            }
            void Reset() { _free.clear(); _end = 0; }

        private:
            std::vector<std::pair<std::uint32_t, std::uint32_t>> _free;   // sorted by offset
            std::uint32_t _end = 0;
        };

        struct Slot {
            std::uint32_t offset = 0, capacity = 0, count = 0;
        };

        /**
         * @brief One table of the frame: the CPU's copy, which ranges of it changed since the last
         *        upload, and the per-frame storage buffer the GPU reads.
         */
        template <typename T>
        struct GpuTable {
            std::vector<T> cpu;
            Allocator allocator;
            std::vector<std::pair<std::uint32_t, std::uint32_t>> dirty;   // offset, count
            kor::Resource<kor::Buffer> buffer;
            std::size_t capacity = 0;
            const char* name = "kui";

            /** @brief Room for @p count, as a slot that may grow in place by a quarter. */
            Slot Allocate(const std::uint32_t count)
            {
                Slot s;
                s.capacity = count + count / 4;
                s.offset = allocator.Allocate(s.capacity);
                s.count = count;
                if (cpu.size() < allocator.End()) cpu.resize(allocator.End());
                return s;
            }

            /** @brief @p slot, made to hold @p count: kept when it fits, moved when it does not. */
            void Fit(Slot& slot, const std::uint32_t count)
            {
                if (count <= slot.capacity && slot.capacity > 0) { slot.count = count; return; }
                allocator.Free(slot.offset, slot.capacity);
                slot = Allocate(count);
            }

            void Release(Slot& slot)
            {
                allocator.Free(slot.offset, slot.capacity);
                slot = {};
            }

            void MarkDirty(const std::uint32_t offset, const std::uint32_t count) { if (count) dirty.emplace_back(offset, count); }

            /** @return whether the buffer was replaced (so descriptor sets naming it are stale). */
            bool Upload(std::size_t& uploaded)
            {
                const std::size_t needed = std::max<std::size_t>(allocator.End(), 1);
                if (cpu.size() < needed) cpu.resize(needed);
                if (needed > capacity || !buffer.Valid()) {
                    capacity = std::max<std::size_t>(needed + needed / 2, 64);
                    buffer = kor::Buffer::RawBuilder{}
                        .SetRawSize(static_cast<kor::i64>(capacity * sizeof(T)))
                        .SetUsage(kor::Buffer::Usage::eStorage)
                        .SetType(kor::Buffer::Type::eDeviceDynamic)
                        .SetIsPerFrame(true)
                        .Build();
                    buffer.SetName(name);
                    if (buffer.Valid()) {
                        buffer->Write(std::span<const T>(cpu.data(), needed), 0);
                        uploaded += needed * sizeof(T);
                    }
                    dirty.clear();
                    return true;
                }
                if (dirty.empty()) return false;
                // Merged, so a run of neighbouring changes is one write.
                std::ranges::sort(dirty);
                std::uint32_t start = dirty[0].first, end = dirty[0].first + dirty[0].second;
                const auto flush = [&] {
                    end = std::min<std::uint32_t>(end, static_cast<std::uint32_t>(cpu.size()));
                    if (end > start) {
                        buffer->Write(std::span<const T>(cpu.data() + start, end - start), start);
                        uploaded += (end - start) * sizeof(T);
                    }
                };
                for (std::size_t i = 1; i < dirty.size(); ++i) {
                    if (dirty[i].first <= end) { end = std::max(end, dirty[i].first + dirty[i].second); continue; }
                    flush();
                    start = dirty[i].first;
                    end = dirty[i].first + dirty[i].second;
                }
                flush();
                dirty.clear();
                return false;
            }
        };

        /**
         * @brief The draw order: rebuilt whole when the frame's structure changes, but uploaded only
         *        where it differs from what the GPU has.
         */
        struct OrderTable {
            std::vector<std::uint32_t> cpu, shadow;
            kor::Resource<kor::Buffer> buffer;
            std::size_t capacity = 0;
            const char* name = "kui order";

            bool Upload(std::size_t& uploaded)
            {
                const std::size_t needed = std::max<std::size_t>(cpu.size(), 1);
                if (needed > capacity || !buffer.Valid()) {
                    capacity = std::max<std::size_t>(needed + needed / 2, 256);
                    buffer = kor::Buffer::RawBuilder{}
                        .SetRawSize(static_cast<kor::i64>(capacity * sizeof(std::uint32_t)))
                        .SetUsage(kor::Buffer::Usage::eStorage)
                        .SetType(kor::Buffer::Type::eDeviceDynamic)
                        .SetIsPerFrame(true)
                        .Build();
                    buffer.SetName(name);
                    if (!cpu.empty() && buffer.Valid()) {
                        buffer->Write(std::span<const std::uint32_t>(cpu), 0);
                        uploaded += cpu.size() * sizeof(std::uint32_t);
                    }
                    shadow = cpu;
                    return true;
                }
                constexpr std::size_t page = 1024;
                std::size_t i = 0;
                while (i < cpu.size()) {
                    std::size_t end = std::min(i + page, cpu.size());
                    const auto differs = [&](const std::size_t a, const std::size_t b) {
                        return b > shadow.size() || std::memcmp(cpu.data() + a, shadow.data() + a, (b - a) * sizeof(std::uint32_t)) != 0;
                    };
                    if (!differs(i, end)) { i = end; continue; }
                    const std::size_t start = i;
                    while (end < cpu.size()) {
                        const std::size_t next = std::min(end + page, cpu.size());
                        if (!differs(end, next)) break;
                        end = next;
                    }
                    buffer->Write(std::span<const std::uint32_t>(cpu.data() + start, end - start), start);
                    uploaded += (end - start) * sizeof(std::uint32_t);
                    i = end;
                }
                shadow = cpu;
                return false;
            }
        };

        bool isSrgb(const kor::Image::Format format)
        {
            using F = kor::Image::Format;
            switch (format) {
            case F::eRGB8_SRGB: case F::eRGBA8_SRGB: return true;
            default: return false;
            }
        }

        std::string formatsOf(const kor::Framebuffer& target)
        {
            std::string key;
            for (kor::u32 i = 0; i < target.ColorAttachmentCount(); ++i)
                key += std::format("c{},", static_cast<int>(target.ColorImage(i)->PixelFormat()));
            return key;
        }

        struct Push {
            kor::Vec2 viewport;
            float scale;
            std::uint32_t flags;
            float time;
        };

        using RunKind = Picture::Data::Run::Kind;
    }

    struct Renderer::Impl {
        std::shared_ptr<Layer> root;
        float scale = 1.f;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

        GpuTable<Instance> instances { .name = "kui instances" };
        GpuTable<Vertex> vertices { .name = "kui vertices" };
        GpuTable<GpuLayer> layers { .name = "kui layers" };
        GpuTable<GpuClip> clips { .name = "kui clips" };
        GpuTable<GpuGradient> gradients { .name = "kui gradients" };
        OrderTable instanceOrder { .name = "kui instance order" };
        OrderTable vertexOrder { .name = "kui vertex order" };

        /** @brief An element shader's parameters: one table of fixed-size records. */
        struct Parameters {
            std::shared_ptr<ElementShader> shader;
            std::uint32_t stride = 0;
            GpuTable<std::byte> bytes { .name = "kui element parameters" };
            Allocator records;   // in records, not bytes
        };
        std::map<std::uint32_t, Parameters> parameters;   // by shader id

        /** @brief The textures every layer draws from: slot 0 is the glyph atlas. Slots stay put while used. */
        // The generation is the image's as it was bound: an image resized in place (kor::Image::Generation)
        // is another GPU image under the same handle, and the set has to be written again for it.
        struct Texture { kor::ResourceRef<const kor::Image> image; std::uint32_t uses = 0; kor::u64 generation = 0; };
        std::vector<Texture> textures { 1 };
        bool texturesChanged = true;

        /**
         * @brief What is behind the interface, as glass shows it (Canvas::DrawBackdrop): a picture of the
         *        target at half its size, with its smaller copies — the blur — taken while the frame is
         *        recorded, each time a backdrop is come to with something drawn since the last. It has a
         *        slot among the textures from the first backdrop drawn, and an image once there is a target.
         */
        std::uint32_t backdropSlot = 0;
        kor::Resource<kor::Image> backdrop;
        static constexpr int BackdropCaptures = 4;      ///< In a frame, at the most.

        std::uint32_t BackdropSlot()
        {
            if (backdropSlot == 0) {
                textures.push_back({ {}, 1 });      // held for good: never another's
                backdropSlot = static_cast<std::uint32_t>(textures.size() - 1);
                texturesChanged = true;
            }
            return backdropSlot;
        }

        /** @brief What a layer has in the tables. */
        struct LayerState {
            std::shared_ptr<Layer> layer;
            std::shared_ptr<const Picture> picture;   // the one converted
            std::uint64_t content = 0, placement = 0;
            std::uint32_t index = 0;                  // in the layer table
            std::uint32_t inheritedClip = None;
            Slot instanceSlot, vertexSlot, clipSlot, gradientSlot;
            std::map<std::uint32_t, Slot> parameterSlots;   // by shader id, in records
            std::vector<std::uint32_t> textureSlots;   // held, to let go of
            // Where it is: drawn by its parent at `at`, inside the parent's clip.
            LayerState* parent = nullptr;
            Transform at;
            Transform world;
            bool seen = false;
            /// Its runs, with what they draw in global indices.
            struct Run { RunKind kind; std::shared_ptr<ElementShader> shader; std::uint32_t first, count; LayerState* child; };
            std::vector<Run> runs;
        };
        std::unordered_map<const Layer*, std::unique_ptr<LayerState>> states;
        Allocator layerIndices;

        struct Draw {
            RunKind kind;
            std::shared_ptr<ElementShader> shader;
            std::uint32_t first, count;   // into the instance order, or the vertex order for a mesh
        };
        std::vector<Draw> draws;

        std::shared_ptr<Layer> builtRoot;
        bool built = false;
        std::uint64_t seenEpoch = 0;

        // ---- the GPU's side
        std::uint64_t tablesVersion = 1;
        kor::Resource<kor::Sampler> sampler;

        struct Element {
            kor::Resource<kor::GraphicsPipeline> pipeline;
            kor::Resource<kor::DescriptorSet> set0, set1;
            std::uint64_t set0Version = 0;
            const void* parameterBuffer = nullptr;
            bool hasParameters = false;
        };
        struct Target {
            kor::Resource<kor::GraphicsPipeline> primitives, mesh;
            kor::Resource<kor::DescriptorSet> primitiveSet, meshSet;
            std::uint64_t setVersion = 0;
            std::map<std::uint32_t, Element> elements;
            bool srgb = false;
            bool failed = false;
        };
        std::map<std::string, Target> targets;
        Target* current = nullptr;
        kor::Vec2 viewport {};

        Statistics stats;

        ~Impl()
        {
            for (auto& [layer, state] : states) ReleaseTextures(*state);
        }

        // ---- textures --------------------------------------------------------------------------------

        std::uint32_t AcquireTexture(const kor::ResourceRef<const kor::Image>& image)
        {
            for (std::size_t i = 1; i < textures.size(); ++i)
                if (textures[i].uses > 0 && textures[i].image.Get() == image.Get()) { ++textures[i].uses; return static_cast<std::uint32_t>(i); }
            for (std::size_t i = 1; i < textures.size(); ++i)
                if (textures[i].uses == 0) { textures[i] = { image, 1 }; texturesChanged = true; return static_cast<std::uint32_t>(i); }
            if (textures.size() >= 256) {
                kor::log::Error("[kui] a frame can draw at most 255 different images; the rest show the glyph atlas");
                return 0;
            }
            textures.push_back({ image, 1 });
            texturesChanged = true;
            return static_cast<std::uint32_t>(textures.size() - 1);
        }

        void ReleaseTextures(LayerState& s)
        {
            for (const std::uint32_t slot : s.textureSlots) {
                if (slot == 0 || slot >= textures.size()) continue;
                if (--textures[slot].uses == 0) { textures[slot].image = {}; texturesChanged = true; }
            }
            s.textureSlots.clear();
        }

        // ---- converting a layer's picture into its slots ------------------------------------------------

        void Convert(LayerState& s)
        {
            const auto& d = s.picture->Contents();
            ReleaseTextures(s);

            instances.Fit(s.instanceSlot, static_cast<std::uint32_t>(d.instances.size()));
            vertices.Fit(s.vertexSlot, static_cast<std::uint32_t>(d.vertices.size()));
            clips.Fit(s.clipSlot, static_cast<std::uint32_t>(d.clips.size()));
            gradients.Fit(s.gradientSlot, static_cast<std::uint32_t>(d.gradients.size()));

            const auto clipOf = [&](const std::uint32_t clip) { return clip == None ? s.inheritedClip : s.clipSlot.offset + clip; };

            std::vector<std::uint32_t> textureMap(d.textures.size(), 0);
            for (std::size_t i = 1; i < d.textures.size(); ++i) {
                textureMap[i] = AcquireTexture(d.textures[i]);
                s.textureSlots.push_back(textureMap[i]);
            }

            // Element parameters: each shader's records, in the order its elements appear.
            std::map<std::uint32_t, std::vector<std::uint32_t>> elementsOf;   // shader id -> picture parameter indices
            std::vector<std::pair<std::uint32_t, std::uint32_t>> elementRecord(d.parameters.size(), { 0, 0 });   // shader id, record
            for (const auto& run : d.runs) {
                if (run.kind != RunKind::eElement || !run.shader) continue;
                for (std::uint32_t i = run.first; i < run.first + run.count; ++i) {
                    const std::uint32_t p = d.instances[i].paint;
                    auto& list = elementsOf[run.shader->Id()];
                    elementRecord[p] = { run.shader->Id(), static_cast<std::uint32_t>(list.size()) };
                    list.push_back(p);
                }
            }
            for (auto& [id, slot] : s.parameterSlots)
                if (!elementsOf.contains(id)) { parameters[id].records.Free(slot.offset, slot.capacity); slot = {}; }
            std::erase_if(s.parameterSlots, [](const auto& e) { return e.second.capacity == 0; });
            for (const auto& run : d.runs) {
                if (run.kind != RunKind::eElement || !run.shader) continue;
                auto& p = parameters[run.shader->Id()];
                p.shader = run.shader;
                if (p.stride == 0) p.stride = static_cast<std::uint32_t>(std::max<std::size_t>((d.parameters[d.instances[run.first].paint].size() + 15) / 16 * 16, 16));
            }
            for (const auto& [id, list] : elementsOf) {
                auto& p = parameters[id];
                auto& slot = s.parameterSlots[id];
                const auto count = static_cast<std::uint32_t>(list.size());
                if (count > slot.capacity) {
                    p.records.Free(slot.offset, slot.capacity);
                    slot.capacity = count + count / 4;
                    slot.offset = p.records.Allocate(slot.capacity);
                }
                slot.count = count;
                const std::size_t bytes = static_cast<std::size_t>(p.records.End()) * p.stride;
                if (p.bytes.cpu.size() < bytes) p.bytes.cpu.resize(bytes);
                if (p.bytes.allocator.End() < bytes) p.bytes.allocator.Allocate(static_cast<std::uint32_t>(bytes - p.bytes.allocator.End()));
                for (std::uint32_t r = 0; r < count; ++r) {
                    const auto& src = d.parameters[list[r]];
                    std::byte* dst = p.bytes.cpu.data() + static_cast<std::size_t>(slot.offset + r) * p.stride;
                    std::memset(dst, 0, p.stride);
                    std::memcpy(dst, src.data(), std::min<std::size_t>(src.size(), p.stride));
                }
                p.bytes.MarkDirty(slot.offset * p.stride, count * p.stride);
            }

            for (std::size_t i = 0; i < d.instances.size(); ++i) {
                Instance it = d.instances[i];
                it.SetLayerClip(s.index, clipOf(it.Clip()));
                if (it.Flags() & eGradient) it.paint += s.gradientSlot.offset;
                it.SetTexture(textureMap[std::min<std::size_t>(it.Texture(), textureMap.size() - 1)]);
                if (it.Kind() == eBackdrop) it.SetTexture(BackdropSlot());
                if (it.Kind() == eCustom) {
                    const auto [id, record] = elementRecord[it.paint];
                    it.paint = s.parameterSlots[id].offset + record;
                }
                instances.cpu[s.instanceSlot.offset + i] = it;
            }
            instances.MarkDirty(s.instanceSlot.offset, s.instanceSlot.count);
            for (std::size_t i = 0; i < d.vertices.size(); ++i) {
                Vertex v = d.vertices[i];
                v.instance += s.instanceSlot.offset;
                vertices.cpu[s.vertexSlot.offset + i] = v;
            }
            vertices.MarkDirty(s.vertexSlot.offset, s.vertexSlot.count);
            std::copy(d.gradients.begin(), d.gradients.end(), gradients.cpu.begin() + s.gradientSlot.offset);
            gradients.MarkDirty(s.gradientSlot.offset, s.gradientSlot.count);

            // Its runs, in global indices; the child layers are found as the tree is walked.
            s.runs.clear();
            for (const auto& run : d.runs) {
                switch (run.kind) {
                case RunKind::ePrimitives:
                case RunKind::eBackdrop:
                case RunKind::eElement:
                    if (run.kind == RunKind::eElement && (!run.shader || !run.shader->Valid())) break;
                    s.runs.push_back({ run.kind, run.kind == RunKind::eElement ? run.shader : nullptr, s.instanceSlot.offset + run.first, run.count, nullptr });
                    break;
                case RunKind::eMesh:
                    s.runs.push_back({ run.kind, nullptr, s.vertexSlot.offset + run.first, run.count, nullptr });
                    break;
                case RunKind::eLayer:
                    s.runs.push_back({ run.kind, nullptr, run.first, 0, nullptr });
                    break;
                }
            }
        }

        void Release(LayerState& s)
        {
            instances.Release(s.instanceSlot);
            vertices.Release(s.vertexSlot);
            clips.Release(s.clipSlot);
            gradients.Release(s.gradientSlot);
            for (auto& [id, slot] : s.parameterSlots) parameters[id].records.Free(slot.offset, slot.capacity);
            s.parameterSlots.clear();
            ReleaseTextures(s);
            layerIndices.Free(s.index, 1);
        }

        // ---- the tree ---------------------------------------------------------------------------------

        /**
         * @brief Visits @p layer and what it shows: converting whatever changed, and writing the draw
         *        order. @return whether anything about the frame's structure changed.
         */
        void Visit(const std::shared_ptr<Layer>& layer, LayerState* parent, const Transform& at, const std::uint32_t inheritedClip, bool& changed)
        {
            auto& entry = states[layer.get()];
            if (!entry) {
                entry = std::make_unique<LayerState>();
                entry->layer = layer;
                entry->index = layerIndices.Allocate(1);
                if (layers.cpu.size() <= entry->index) layers.cpu.resize(entry->index + 1);
                if (layers.allocator.End() <= entry->index) layers.allocator.Allocate(entry->index + 1 - layers.allocator.End());
                changed = true;
            }
            LayerState& s = *entry;
            if (s.seen) return;   // shown twice: once is all a layer can be
            s.seen = true;
            s.parent = parent;
            s.at = at;
            const auto& picture = layer->GetPicture();
            if (picture != s.picture || inheritedClip != s.inheritedClip || s.content != layer->ContentVersion()) {
                s.picture = picture;
                s.inheritedClip = inheritedClip;
                s.content = layer->ContentVersion();
                if (s.picture) Convert(s);
                else { s.runs.clear(); }
                changed = true;
            }
            if (!s.picture) return;
            const auto& d = s.picture->Contents();
            for (const auto& run : s.runs) {
                if (run.kind != RunKind::eLayer) continue;
                const auto& ref = d.layers[run.first];
                if (!ref.layer || ref.layer.get() == layer.get()) continue;
                const std::uint32_t clip = ref.clip == None ? s.inheritedClip : s.clipSlot.offset + ref.clip;
                Visit(ref.layer, &s, ref.transform, clip, changed);
            }
        }

        void AddDraw(const RunKind kind, const std::shared_ptr<ElementShader>& shader, const std::uint32_t first, const std::uint32_t count)
        {
            if (count == 0) return;
            if (!draws.empty()) {
                auto& last = draws.back();
                if (last.kind == kind && last.shader == shader && last.first + last.count == first) { last.count += count; return; }
            }
            draws.push_back({ kind, shader, first, count });
        }

        void WriteOrder(LayerState& s)
        {
            if (!s.picture) return;
            const auto& d = s.picture->Contents();
            for (const auto& run : s.runs) {
                switch (run.kind) {
                case RunKind::ePrimitives:
                case RunKind::eBackdrop:
                case RunKind::eElement: {
                    const auto first = static_cast<std::uint32_t>(instanceOrder.cpu.size());
                    for (std::uint32_t i = 0; i < run.count; ++i) instanceOrder.cpu.push_back(run.first + i);
                    AddDraw(run.kind, run.shader, first, run.count);
                    break;
                }
                case RunKind::eMesh: {
                    const auto first = static_cast<std::uint32_t>(vertexOrder.cpu.size());
                    for (std::uint32_t i = 0; i < run.count; ++i) vertexOrder.cpu.push_back(run.first + i);
                    AddDraw(run.kind, nullptr, first, run.count);
                    break;
                }
                case RunKind::eLayer: {
                    const auto& ref = d.layers[run.first];
                    if (!ref.layer || ref.layer.get() == s.layer.get()) break;
                    const auto it = states.find(ref.layer.get());
                    if (it != states.end() && it->second->parent == &s) WriteOrder(*it->second);
                    break;
                }
                }
            }
        }

        /** @brief Where every layer is, and so where its clips are: the layer and clip tables. */
        void Place(LayerState& s)
        {
            const Transform parentWorld = s.parent ? s.parent->world : Transform::Identity();
            const float parentOpacity = s.parent ? layers.cpu[s.parent->index].opacity : 1.f;
            s.world = parentWorld * s.at * s.layer->GetTransform();
            s.placement = s.layer->PlacementVersion();
            const GpuLayer entry { Pack(s.world), { s.world.tx, s.world.ty }, parentOpacity * s.layer->Opacity(), 0 };
            if (std::memcmp(&layers.cpu[s.index], &entry, sizeof(entry)) != 0) {
                layers.cpu[s.index] = entry;
                layers.MarkDirty(s.index, 1);
            }
            if (s.picture) {
                const auto& d = s.picture->Contents();
                for (std::size_t i = 0; i < d.clips.size(); ++i) {
                    const auto& c = d.clips[i];
                    const Transform inverse = (s.world * c.transform).Inverse();
                    const std::uint32_t parent = c.parent == None ? s.inheritedClip : s.clipSlot.offset + c.parent;
                    const GpuClip gpu { Pack(inverse), { inverse.tx, inverse.ty }, parent, 0,
                                        { c.rect.left, c.rect.top, c.rect.right, c.rect.bottom },
                                        { c.radii.topLeft, c.radii.topRight, c.radii.bottomRight, c.radii.bottomLeft } };
                    auto& slot = clips.cpu[s.clipSlot.offset + i];
                    if (std::memcmp(&slot, &gpu, sizeof(gpu)) != 0) { slot = gpu; clips.MarkDirty(s.clipSlot.offset + static_cast<std::uint32_t>(i), 1); }
                }
                for (const auto& run : s.runs) {
                    if (run.kind != RunKind::eLayer) continue;
                    const auto it = states.find(d.layers[run.first].layer.get());
                    if (it != states.end() && it->second->parent == &s) Place(*it->second);
                }
            }
        }

        /**
         * @brief Brings the tables up to date. Nothing changed: nothing is done. A layer moved: the
         *        layer and clip tables. A picture changed: that layer's slots, and the draw order.
         */
        void Update()
        {
            // Nothing anywhere changed since the last look: nothing here did either.
            const std::uint64_t epoch = Layer::Epoch();
            if (built && root == builtRoot && epoch == seenEpoch) return;
            seenEpoch = epoch;
            bool structural = !built || root != builtRoot;
            bool moved = false;
            if (!structural) {
                for (const auto& [key, s] : states) {
                    if (s->layer->ContentVersion() != s->content || s->layer->GetPicture() != s->picture) { structural = true; break; }
                    if (s->layer->PlacementVersion() != s->placement) moved = true;
                }
            }
            if (structural) {
                for (auto& [key, s] : states) s->seen = false;
                bool changed = false;
                if (root) Visit(root, nullptr, Transform::Identity(), None, changed);
                // Layers no longer shown let their slots go.
                for (auto it = states.begin(); it != states.end();) {
                    if (it->second->seen) { ++it; continue; }
                    Release(*it->second);
                    it = states.erase(it);
                }
                instanceOrder.cpu.clear();
                vertexOrder.cpu.clear();
                draws.clear();
                if (root) if (const auto it = states.find(root.get()); it != states.end()) WriteOrder(*it->second);
                builtRoot = root;
                built = true;
                stats.recomposed = true;
                moved = true;
            }
            if (moved && root) if (const auto it = states.find(root.get()); it != states.end()) Place(*it->second);
        }

        bool Upload()
        {
            std::size_t& uploaded = stats.uploadedBytes;
            bool replaced = false;
            replaced |= instances.Upload(uploaded);
            replaced |= vertices.Upload(uploaded);
            replaced |= layers.Upload(uploaded);
            replaced |= clips.Upload(uploaded);
            replaced |= gradients.Upload(uploaded);
            replaced |= instanceOrder.Upload(uploaded);
            replaced |= vertexOrder.Upload(uploaded);
            for (auto& [id, p] : parameters) p.bytes.Upload(uploaded);   // its set notices the buffer changed

            const auto atlas = AtlasImage();
            if (textures[0].image.Get() != atlas.Get()) { textures[0].image = atlas; texturesChanged = true; }
            for (auto& texture : textures)
                if (texture.image.Valid() && texture.image->Generation() != texture.generation) {
                    texture.generation = texture.image->Generation();
                    texturesChanged = true;
                }
            if (texturesChanged) { texturesChanged = false; replaced = true; }
            if (replaced) ++tablesVersion;
            return replaced;
        }

        // ---- pipelines --------------------------------------------------------------------------------

        /**
         * @brief Set 0 for @p pipeline: every table its layout has a binding for. Which those are
         *        depends on the shaders — a stage only declares the texture array when it samples it,
         *        and Slang drops what an entry point never touches.
         */
        kor::Resource<kor::DescriptorSet> WriteSet0(const kor::Resource<kor::GraphicsPipeline>& pipeline)
        {
            const auto& bindings = pipeline->SetLayout(0).Bindings();
            const auto has = [&](const kor::u32 b) { return bindings.contains(b); };
            auto builder = kor::DescriptorSet::Builder(kor::ResourceRef<const kor::GraphicsPipeline>(pipeline), 0);
            if (has(0)) builder.Write(0, instances.buffer);
            if (has(1)) builder.Write(1, layers.buffer);
            if (has(2)) builder.Write(2, clips.buffer);
            if (has(3)) builder.Write(3, gradients.buffer);
            if (has(4)) builder.Write(4, vertices.buffer);
            if (has(5)) builder.Write(5, sampler);
            if (has(6)) builder.Write(6, instanceOrder.buffer);
            if (has(7)) builder.Write(7, vertexOrder.buffer);
            if (has(8))
                for (std::size_t i = 0; i < textures.size(); ++i)
                    if (textures[i].image.Valid()) builder.Write(8, textures[i].image, static_cast<kor::u32>(i));
            return builder.Build();
        }

        kor::Resource<kor::GraphicsPipeline> MakePipeline(const kor::ResourceRef<const kor::Framebuffer>& target,
                                                          const kor::ResourceRef<const kor::Shader>& vertex,
                                                          const kor::ResourceRef<const kor::Shader>& fragment)
        {
            kor::ColorBlendState blend;
            // Premultiplied: the shaders write colour already multiplied by its coverage.
            blend.attachments.resize(target->ColorAttachmentCount(), kor::ColorBlendState::AttachmentState {
                .blendEnable = true,
                .srcColorBlendFactor = kor::BlendFactor::eOne, .dstColorBlendFactor = kor::BlendFactor::eOneMinusSrcAlpha,
                .srcAlphaBlendFactor = kor::BlendFactor::eOne, .dstAlphaBlendFactor = kor::BlendFactor::eOneMinusSrcAlpha });
            return kor::GraphicsPipeline::Builder()
                .SetVertexShader(vertex)
                .SetFragmentShader(fragment)
                .SetFramebuffer(target)
                .SetInputAssemblyState({ .topology = kor::Topology::eTriangleList })
                .SetDepthStencilState({ .depthTestEnable = false, .depthWriteEnable = false })
                .SetColorBlendState(blend)
                .Build();
        }

        void PrepareTarget(const kor::ResourceRef<const kor::Framebuffer>& target)
        {
            auto& t = targets[formatsOf(*target)];
            current = &t;
            if (t.failed) return;
            // Glass in the frame: the picture of the target it shows through, as big as half the target.
            if (backdropSlot != 0 && std::ranges::any_of(draws, [](const Draw& d) { return d.kind == RunKind::eBackdrop; })) {
                const auto full = target->ColorImage(0)->Extent();
                const kor::UVec2 extent = kor::Max(kor::UVec2(full.x, full.y) / 2u, kor::UVec2(1u));
                const auto format = target->ColorImage(0)->PixelFormat();
                if (!backdrop.Valid() || kor::UVec2(backdrop->Extent().x, backdrop->Extent().y) != extent || backdrop->PixelFormat() != format) {
                    kor::u32 levels = 1;
                    for (kor::u32 side = std::max(extent.x, extent.y); side > 8u && levels < 7u; side /= 2u) ++levels;
                    backdrop = kor::Image::Builder{}
                        .SetFormat(format).SetExtent(extent).SetMipLevels(levels)
                        .SetUsage(kor::Image::Usage::eSampled | kor::Image::Usage::eTransferSrc | kor::Image::Usage::eTransferDst)
                        .Build();
                    if (!backdrop.Valid())
                        kor::log::Error("[kui] the picture glass shows through could not be made: {}",
                                        backdrop.Failure() ? backdrop.Failure()->message : "no reason given");
                    textures[backdropSlot].image = backdrop.Valid() ? kor::ResourceRef<const kor::Image>(backdrop) : kor::ResourceRef<const kor::Image>{};
                    ++tablesVersion;
                }
            }
            if (!t.primitives.Valid()) {
                t.srgb = target->ColorAttachmentCount() > 0 && isSrgb(target->ColorImage(0)->PixelFormat());
                const auto vertex = kor::Shader::Builder{}.SetPath("koralUI.vert.glsl").GetOrBuild();
                const auto fragment = kor::Shader::Builder{}.SetPath("koralUI.frag.glsl").GetOrBuild();
                const auto meshVertex = kor::Shader::Builder{}.SetPath("koralUIMesh.vert.glsl").GetOrBuild();
                const auto meshFragment = kor::Shader::Builder{}.SetPath("koralUIMesh.frag.glsl").GetOrBuild();
                t.primitives = MakePipeline(target, vertex, fragment);
                t.mesh = MakePipeline(target, meshVertex, meshFragment);
                if (!t.primitives.Valid() || !t.mesh.Valid()) {
                    const auto& failed = !t.primitives.Valid() ? t.primitives : t.mesh;
                    kor::log::Error("[kui] the UI pipelines could not be made: {}",
                                    failed.Failure() ? failed.Failure()->message : "no reason given");
                    t.failed = true;
                    return;
                }
            }
            if (t.setVersion != tablesVersion || !t.primitiveSet.Valid()) {
                t.primitiveSet = WriteSet0(t.primitives);
                t.meshSet = WriteSet0(t.mesh);
                t.setVersion = tablesVersion;
            }
            // A pipeline for each element shader drawn, with its parameters.
            for (auto& [id, p] : parameters) {
                if (p.records.End() == 0 || !p.shader) continue;
                auto& e = t.elements[id];
                if (!e.pipeline.Valid() && !e.pipeline.Poisoned()) {
                    const auto vertex = kor::Shader::Builder{}.SetPath("koralUI.vert.glsl").GetOrBuild();
                    e.pipeline = MakePipeline(target, vertex, p.shader->Shader());
                    if (!e.pipeline.Valid()) {
                        kor::log::Error("[kui] an element shader's pipeline could not be made: {}",
                                        e.pipeline.Failure() ? e.pipeline.Failure()->message : "no reason given");
                        continue;
                    }
                    try { (void)e.pipeline->SetLayoutRef(1); e.hasParameters = true; }
                    catch (const std::exception&) { e.hasParameters = false; }
                }
                if (!e.pipeline.Valid()) continue;
                if (e.set0Version != tablesVersion || !e.set0.Valid()) {
                    e.set0 = WriteSet0(e.pipeline);
                    e.set0Version = tablesVersion;
                }
                if (e.hasParameters && p.bytes.buffer.Valid() && (e.parameterBuffer != p.bytes.buffer.Get() || !e.set1.Valid())) {
                    e.set1 = kor::DescriptorSet::Builder(kor::ResourceRef<const kor::GraphicsPipeline>(e.pipeline), 1)
                        .Write(0, p.bytes.buffer).Build();
                    e.parameterBuffer = p.bytes.buffer.Get();
                }
            }
        }
    };

    Renderer::Renderer() : _impl(std::make_unique<Impl>()) {}
    Renderer::~Renderer() = default;

    void Renderer::SetRoot(std::shared_ptr<Layer> root) { _impl->root = std::move(root); }
    const std::shared_ptr<Layer>& Renderer::Root() const { return _impl->root; }
    void Renderer::SetScale(const float scale) { _impl->scale = std::max(scale, 0.01f); }
    float Renderer::Scale() const { return _impl->scale; }
    const Renderer::Statistics& Renderer::Stats() const { return _impl->stats; }

    void Renderer::Prepare(const kor::ResourceRef<const kor::Framebuffer>& target)
    {
        auto& impl = *_impl;
        impl.stats.uploadedBytes = 0;
        impl.stats.recomposed = false;
        if (!impl.sampler.Valid()) {
            impl.sampler = kor::Sampler::Builder{}
                .SetMinFilter(kor::Filter::eLinear).SetMagFilter(kor::Filter::eLinear)
                .SetAddressModeU(kor::Sampler::AddressMode::eClampToEdge)
                .SetAddressModeV(kor::Sampler::AddressMode::eClampToEdge)
                .Build();
        }

        const auto begin = std::chrono::steady_clock::now();
        impl.Update();
        impl.stats.composeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        const auto uploading = std::chrono::steady_clock::now();
        impl.Upload();
        impl.stats.uploadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - uploading).count();

        impl.stats.instances = impl.instanceOrder.cpu.size();
        impl.stats.vertices = impl.vertexOrder.cpu.size();
        impl.stats.layers = impl.states.size();
        impl.stats.clips = impl.clips.allocator.End() - impl.clips.allocator.FreeCount();
        impl.stats.draws = impl.draws.size();

        if (!target.Valid() || target->ColorAttachmentCount() == 0) { impl.current = nullptr; return; }
        const auto extent = target->ColorImage(0)->Extent();
        impl.viewport = { static_cast<float>(extent.x), static_cast<float>(extent.y) };
        impl.PrepareTarget(target);
        impl.stats.prepareMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    }

    void Renderer::Record(kor::CommandBuffer& commandBuffer, const kor::ResourceRef<const kor::Framebuffer>& target) const
    {
        const auto& impl = *_impl;
        const auto* t = impl.current;
        if (!t || t->failed || impl.draws.empty() || !target.Valid() || !t->primitiveSet.Valid() || !t->meshSet.Valid()) return;

        const Push push {
            impl.viewport, impl.scale, t->srgb ? 1u : 0u,
            std::chrono::duration<float>(std::chrono::steady_clock::now() - impl.start).count(),
        };
        commandBuffer.BeginRendering(kor::RenderInfo(target).SetColorLoadOperation(kor::LoadOperation::eLoad));
        const void* bound = nullptr;
        // Glass: a picture of the target is taken when a backdrop is come to with something drawn since
        // the last was — the first of the frame always is: the scene is behind the interface.
        bool drawnSince = true;
        int captures = 0;
        const auto source = target->ColorImage(0);
        const bool canCapture = impl.backdrop.Valid() && source.Valid() && (source->UsageFlags() & kor::Image::Usage::eTransferSrc);
        for (const auto& draw : impl.draws) {
            if (draw.kind == RunKind::eBackdrop) {
                if (canCapture && drawnSince && captures < Impl::BackdropCaptures) {
                    commandBuffer.EndRendering();
                    kor::Blit whole;
                    whole.filtering = kor::Filter::eLinear;
                    commandBuffer.Blit(source, impl.backdrop, whole);
                    commandBuffer.GenerateMipmaps(impl.backdrop);
                    commandBuffer.BeginRendering(kor::RenderInfo(target).SetColorLoadOperation(kor::LoadOperation::eLoad));
                    bound = nullptr;
                    ++captures;
                    drawnSince = false;
                }
            } else {
                drawnSince = true;
            }
            switch (draw.kind) {
            case RunKind::eBackdrop:
            case RunKind::ePrimitives:
                if (bound != t->primitives.Get()) {
                    commandBuffer.BindGraphicsPipeline(t->primitives).BindDescriptorSet(0, t->primitiveSet).PushConstantBlock(push);
                    bound = t->primitives.Get();
                }
                // The instance index is a place in the draw order, which names the instance.
                commandBuffer.Draw(6, draw.count, 0, draw.first);
                break;
            case RunKind::eMesh:
                if (bound != t->mesh.Get()) {
                    commandBuffer.BindGraphicsPipeline(t->mesh).BindDescriptorSet(0, t->meshSet).PushConstantBlock(push);
                    bound = t->mesh.Get();
                }
                commandBuffer.Draw(draw.count, 1, draw.first, 0);
                break;
            case RunKind::eElement: {
                const auto it = t->elements.find(draw.shader->Id());
                if (it == t->elements.end() || !it->second.pipeline.Valid() || !it->second.set0.Valid()) break;
                const auto& e = it->second;
                if (bound != e.pipeline.Get()) {
                    commandBuffer.BindGraphicsPipeline(e.pipeline).BindDescriptorSet(0, e.set0);
                    if (e.hasParameters && e.set1.Valid()) commandBuffer.BindDescriptorSet(1, e.set1);
                    commandBuffer.PushConstantBlock(push);
                    bound = e.pipeline.Get();
                }
                commandBuffer.Draw(6, draw.count, 0, draw.first);
                break;
            }
            case RunKind::eLayer: break;
            }
        }
        commandBuffer.EndRendering();
    }

    // ---- the pass -------------------------------------------------------------------------------------

    UiPass::UiPass(Renderer& renderer, std::string target)
        : RenderPass("UI"), _renderer(renderer), _target(std::move(target)) {}

    UiPass::UiPass(Ui& ui, std::string target) : UiPass(ui.GetRenderer(), std::move(target)) {}

    void UiPass::Setup(kor::PassBuilder& builder) { builder.Write(_target, kor::Image::Usage::eColorAttachment); }

    void UiPass::Initialize(const kor::PassResources& resources)
    {
        _framebuffer = kor::Framebuffer::Builder().AddColor({ .view = resources.ImageNamed(_target) }).Build();
    }

    void UiPass::Prepare() { _renderer.Prepare(_framebuffer); }

    void UiPass::Record(kor::CommandBuffer& commandBuffer) const { _renderer.Record(commandBuffer, _framebuffer); }

    // ---- element shaders --------------------------------------------------------------------------------

    std::shared_ptr<ElementShader> ElementShader::Load(const std::filesystem::path& path, std::string entry)
    {
        static std::uint32_t nextId = 1;
        auto shader = std::shared_ptr<ElementShader>(new ElementShader());
        // Slang finds modules by name, through the search roots: a module named by where it is makes
        // its directory one of them, so it — and anything beside it that it imports — resolves.
        if (path.extension() == ".slang" && path.has_parent_path()) {
            const auto dir = path.is_absolute() ? path.parent_path() : kor::ShaderPath(path).parent_path();
            if (!dir.empty() && std::ranges::find(kor::Shader::SearchPaths(), dir) == kor::Shader::SearchPaths().end())
                kor::Shader::AddSearchPath(dir);
        }
        auto builder = kor::Shader::Builder{}.SetPath(path).SetStage(kor::Shader::Stage::eFragment);
        if (!entry.empty()) builder.SetEntryPoint(std::move(entry));
        shader->_shader = builder.GetOrBuild();
        shader->_id = nextId++;
        if (!shader->_shader.Valid())
            kor::log::Error("[kui] the element shader {} could not be built: {}", path.string(),
                            shader->_shader.Failure() ? shader->_shader.Failure()->message : "no reason given");
        return shader;
    }
}
