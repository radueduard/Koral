//
// Created by radue on 3/4/2026.
//

#include <descriptorSet.h>
#include <semantics.h>
#include <descriptor.h>
#include <descriptorSetLayout.h>
#include <pipeline.h>
#include <window.h>
#include <framebuffer.h>
#include <surface.h>

#include "../backends/vulkan/descriptorSet.h"

#include <ranges>
#include <format>

#include "accelerationStructure.h"
#include "buffer.h"
#include "image.h"
#include "bufferView.h"
#include "imageView.h"


namespace kor
{
    // The constructors no longer throw: an invalid input flips `valid` to false and
    // records the reason in `_error`, which DescriptorSet::Builder::Build() surfaces.
    Descriptor::Descriptor(const ResourceRef<const Buffer>& buffer, const kor::i64 offset, const kor::i64 range)
        : valid(true), _descriptor(BufferDescriptor{ buffer, offset, range })
    {
        if (!buffer) {
            valid = false; _error = Error{ .code = ErrorCode::eInvalidArgument, .message = "Buffer descriptor has an invalid buffer." };
            return;
        }
        if (offset < 0 || offset >= buffer->size()) {
            valid = false; _error = Error{ .code = ErrorCode::eBufferRangeOutOfBounds,
                .message = std::format("Buffer descriptor offset {} is out of range (buffer size {}).", offset, buffer->size()) };
            return;
        }
        if (range < 0) {
            valid = false; _error = Error{ .code = ErrorCode::eBufferRangeOutOfBounds,
                .message = std::format("Buffer descriptor has a negative range {}.", range) };
            return;
        }
        if (offset + range > buffer->size()) {
            valid = false; _error = Error{ .code = ErrorCode::eBufferRangeOutOfBounds,
                .message = std::format("Buffer descriptor offset {} + range {} exceeds buffer size {}.", offset, range, buffer->size()) };
            return;
        }

        if (range == 0) {
            std::visit([](auto& d) {
                using D = std::decay_t<decltype(d)>;
                if constexpr (std::is_same_v<D, BufferDescriptor>) {
                    if (d._range == 0) {
                        d._range = d._buffer->size() - d._offset;
                    }
                }
            }, _descriptor);
        }
    }

    Descriptor::Descriptor(const ResourceRef<const ImageView>&imageView, const ResourceRef<const Sampler>&sampler)
        : valid(true), _descriptor(CombinedImageSamplerDescriptor{ imageView, sampler })
    {
        if (!imageView) {
            valid = false; _error = Error{ .code = ErrorCode::eInvalidArgument, .message = "Combined image-sampler descriptor has an invalid image view." };
        } else if (!sampler) {
            valid = false; _error = Error{ .code = ErrorCode::eInvalidArgument, .message = "Combined image-sampler descriptor has an invalid sampler." };
        }
    }

    Descriptor::Descriptor(const ResourceRef<const ImageView>&imageView)
        : valid(true), _descriptor(ImageDescriptor{ imageView })
    {
        if (!imageView) {
            valid = false; _error = Error{ .code = ErrorCode::eInvalidArgument, .message = "Image descriptor has an invalid image view." };
        }
    }

    Descriptor::Descriptor(const ResourceRef<const Sampler>&sampler)
        : valid(true), _descriptor(SamplerDescriptor{ sampler }) {
        if (!sampler) {
            valid = false; _error = Error{ .code = ErrorCode::eInvalidArgument, .message = "Sampler descriptor has an invalid sampler." };
        }
    }

    Descriptor::Descriptor(const ResourceRef<const AccelerationStructure>& accelerationStructure)
        : valid(true), _descriptor(AccelerationStructureDescriptor{ accelerationStructure }) {
        if (!accelerationStructure) {
            valid = false; _error = Error{ .code = ErrorCode::eInvalidArgument, .message = "Acceleration-structure descriptor has an invalid acceleration structure." };
        }
    }

    Descriptor::Descriptor(const ResourceRef<const BufferView>& bufferView)
        : valid(true), _descriptor(TexelBufferDescriptor{ bufferView }) {
        if (!bufferView) {
            valid = false; _error = Error{ .code = ErrorCode::eInvalidArgument, .message = "Texel-buffer descriptor has an invalid buffer view." };
        }
    }

    ResourceRef<const Buffer> Descriptor::BufferRef() const {
        if (!valid) return {};
        if (const auto* buffer = std::get_if<BufferDescriptor>(&_descriptor)) return buffer->_buffer;
        // A texel binding is a buffer as far as synchronisation is concerned — the formatting is
        // the shader's business, the hazard is the bytes'. Answering with the underlying buffer
        // here is what makes the barrier resolver see a texel fetch at all.
        if (const auto* texel = std::get_if<TexelBufferDescriptor>(&_descriptor)) {
            if (texel->_bufferView.Valid()) return texel->_bufferView->SourceBuffer();
        }
        return {};
    }

    ResourceRef<const BufferView> Descriptor::BufferViewRef() const {
        if (!valid) return {};
        if (const auto* texel = std::get_if<TexelBufferDescriptor>(&_descriptor)) return texel->_bufferView;
        return {};
    }

    const BufferView& Descriptor::BoundBufferView() const {
        if (!valid) {
            kor::log::Error("Attempted to get buffer view from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (!std::holds_alternative<TexelBufferDescriptor>(_descriptor)) {
            kor::log::Error("Attempted to get buffer view from a descriptor that does not hold one!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        return *std::get<TexelBufferDescriptor>(_descriptor)._bufferView;
    }

    ResourceRef<const ImageView> Descriptor::ImageViewRef() const {
        if (!valid) return {};
        if (const auto* image = std::get_if<ImageDescriptor>(&_descriptor)) return image->_imageView;
        if (const auto* combined = std::get_if<CombinedImageSamplerDescriptor>(&_descriptor)) return combined->_imageView;
        return {};
    }

    const Buffer & Descriptor::BoundBuffer() const {
        if (!valid) {
            kor::log::Error("Attempted to get buffer from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (!std::holds_alternative<BufferDescriptor>(_descriptor)) {
            kor::log::Error("Attempted to get buffer from a descriptor that does not hold a buffer!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        return *std::get<BufferDescriptor>(_descriptor)._buffer;
    }

    kor::i64 Descriptor::Offset() const {
        if (!valid) {
            kor::log::Error("Attempted to get offset from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (!std::holds_alternative<BufferDescriptor>(_descriptor)) {
            kor::log::Error("Attempted to get offset from a descriptor that does not hold a buffer!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        return std::get<BufferDescriptor>(_descriptor)._offset;
    }

    kor::i64 Descriptor::Range() const {
        if (!valid) {
            kor::log::Error("Attempted to get range from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (!std::holds_alternative<BufferDescriptor>(_descriptor)) {
            kor::log::Error("Attempted to get range from a descriptor that does not hold a buffer!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        return std::get<BufferDescriptor>(_descriptor)._range;
    }

    const ImageView & Descriptor::BoundImageView() const {
        if (!valid) {
            kor::log::Error("Attempted to get image view from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (std::holds_alternative<ImageDescriptor>(_descriptor)) {
            return *std::get<ImageDescriptor>(_descriptor)._imageView;
        }
        if (std::holds_alternative<CombinedImageSamplerDescriptor>(_descriptor)) {
            return *std::get<CombinedImageSamplerDescriptor>(_descriptor)._imageView;
        }
        kor::log::Error("Attempted to get image view from a descriptor that does not hold an image view!");
        throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor does not hold an image view." });
    }

    const Sampler & Descriptor::BoundSampler() const {
        if (!valid) {
            kor::log::Error("Attempted to get sampler from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (std::holds_alternative<SamplerDescriptor>(_descriptor)) {
            return *std::get<SamplerDescriptor>(_descriptor)._sampler;
        }
        if (std::holds_alternative<CombinedImageSamplerDescriptor>(_descriptor)) {
            return *std::get<CombinedImageSamplerDescriptor>(_descriptor)._sampler;
        }
        kor::log::Error("Attempted to get sampler from a descriptor that does not hold a sampler!");
        throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor does not hold a sampler." });
    }

    const AccelerationStructure & Descriptor::BoundAccelerationStructure() const {
        if (!valid) {
            kor::log::Error("Attempted to get acceleration structure from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (!std::holds_alternative<AccelerationStructureDescriptor>(_descriptor)) {
            kor::log::Error("Attempted to get acceleration structure from a descriptor that does not hold one!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        return *std::get<AccelerationStructureDescriptor>(_descriptor)._accelerationStructure;
    }



    // One empty slot per declared binding, so Resolve() can place writes by index. Const, and run
    // per attempt: the layout it reads is the one *this* attempt found, and a reload that added a
    // binding or lengthened an array gives a different shape.
    void DescriptorSet::Builder::InitWrites() const
    {
        writes.clear();
        if (!layout.Valid()) return;  // poisoned or absent: Resolve() will refuse to build anyway
        for (const auto& [binding, description] : layout->Bindings()) {
            writes[binding] = std::vector<Descriptor>();
            writes[binding].resize(description.count);
        }
    }

    DescriptorSet::Builder::Builder(ResourceRef<const Pipeline> pipeline, const kor::u32 setIndex)
        : pipeline(std::move(pipeline)), setIndex(setIndex)
    {
        // Resolved here *and* on every later attempt: a shader reload can replace the layout, and a
        // rebuild has to fill the new one. A poisoned pipeline has no layouts to ask for — `layout`
        // stays empty, Resolve() refuses, and this set is poisoned with the pipeline's error as its
        // cause. @see resolve
        if (this->pipeline.Valid()) layout = this->pipeline->SetLayoutRef(setIndex);
    }

    DescriptorSet::Builder::Builder(ResourceRef<const DescriptorSetLayout> layout) : layout(layout)
    {
    }

    DescriptorSet::Builder::Builder(const DescriptorSetLayout& layout)
        : layout(ResourceRef<const DescriptorSetLayout>(&layout))
    {
    }

    std::pair<std::string_view, kor::u32> DescriptorSet::SplitIndex(const std::string_view name)
    {
        // `textures[3]` selects element 3 of the binding called `textures`. Anything that is not a
        // well-formed trailing subscript is left alone and treated as part of the name, so a
        // binding whose name genuinely contains a bracket is still findable — and a malformed one
        // is reported as "no such binding", naming what was actually looked for.
        if (name.size() < 4 || name.back() != ']') return { name, 0 };

        const auto open = name.rfind('[');
        if (open == std::string_view::npos || open == 0) return { name, 0 };

        const auto digits = name.substr(open + 1, name.size() - open - 2);
        if (digits.empty()) return { name, 0 };

        kor::u32 index = 0;
        for (const char c : digits) {
            if (c < '0' || c > '9') return { name, 0 };
            index = index * 10 + static_cast<kor::u32>(c - '0');
        }
        return { name.substr(0, open), index };
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Record(PendingWrite write)
    {
        // Sticky: once a write has failed, later writes are ignored and the first error is
        // surfaced by Build(). This keeps the fluent .Write(...).Write(...) chain.
        if (_error) return *this;
        pending.push_back(std::move(write));
        return *this;
    }

    DescriptorSet::Builder& DescriptorSet::Builder::RejectSemantic(const kor::u32 binding, const char* what,
                                                                   const bool unusable)
    {
        return RejectSemantic(std::to_string(binding), what, unusable);
    }

    DescriptorSet::Builder& DescriptorSet::Builder::RejectSemantic(const std::string_view name, const char* what,
                                                                   const bool unusable)
    {
        if (!_error) _error = Error{
            .code = ErrorCode::eInvalidArgument,
            .message = unusable
                ? std::format("The resource written to binding {} is unusable ('{}'), so there is "
                              "nothing to fill the block from.", name, what ? what : "?")
                : std::format("Binding {} was written from a '{}', which cannot fill a semantic "
                              "block: it does not implement kor::SemanticSerializer. Bind it as a "
                              "resource instead, or make the type serializable.",
                              name, what ? what : "?"),
        };
        return *this;
    }

    DescriptorSet::Builder& DescriptorSet::Builder::WriteSemantic(const kor::u32 binding, SemanticSerializer& serializer)
    {
        return RecordSemantic(binding, [&serializer] { return &serializer; }, "<reference>");
    }

    DescriptorSet::Builder& DescriptorSet::Builder::WriteSemantic(const std::string_view name, SemanticSerializer& serializer)
    {
        return RecordSemantic(name, [&serializer] { return &serializer; }, "<reference>");
    }

    DescriptorSet::Builder& DescriptorSet::Builder::RecordSemantic(
        const kor::u32 binding, std::function<SemanticSerializer*()> resolve, std::string what)
    {
        return Record({ .binding = binding,
                        .semantic = SemanticWrite{ std::move(resolve), std::move(what) } });
    }

    DescriptorSet::Builder& DescriptorSet::Builder::RecordSemantic(
        const std::string_view name, std::function<SemanticSerializer*()> resolve, std::string what)
    {
        const auto [base, index] = SplitIndex(name);
        return Record({ .name = std::string(base), .index = index,
                        .semantic = SemanticWrite{ std::move(resolve), std::move(what) } });
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const kor::u32 binding, const Descriptor& descriptor,
                                                          const kor::u32 index)
    {
        return Record({ .binding = binding, .index = index, .descriptor = descriptor });
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const std::string_view name, const Descriptor& descriptor)
    {
        const auto [base, index] = SplitIndex(name);
        return Record({ .name = std::string(base), .index = index, .descriptor = descriptor });
    }

    // The resource overloads. Each is the corresponding Descriptor constructor and nothing more —
    // the kind is decided by the argument's type here rather than by the caller naming it, and
    // whether that kind is what the binding actually expects is settled in Resolve().
    DescriptorSet::Builder& DescriptorSet::Builder::Write(const kor::u32 binding,
        const ResourceRef<const Buffer>& buffer, const kor::u32 index)
    {
        return Write(binding, Descriptor(buffer), index);
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const kor::u32 binding,
        const Buffer::Slice& slice, const kor::u32 index)
    {
        return Write(binding, Descriptor(slice.buffer, slice.offset, slice.size), index);
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const std::string_view name, const Buffer::Slice& slice)
    {
        return Write(name, Descriptor(slice.buffer, slice.offset, slice.size));
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const kor::u32 binding,
        const ResourceRef<const BufferView>& bufferView, const kor::u32 index)
    {
        return Write(binding, Descriptor(bufferView), index);
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const std::string_view name,
        const ResourceRef<const BufferView>& bufferView)
    {
        return Write(name, Descriptor(bufferView));
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const kor::u32 binding,
        const ResourceRef<const ImageView>& imageView, const kor::u32 index)
    {
        return Write(binding, Descriptor(imageView), index);
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const kor::u32 binding,
        const ResourceRef<const ImageView>& imageView, const ResourceRef<const Sampler>& sampler,
        const kor::u32 index)
    {
        return Write(binding, Descriptor(imageView, sampler), index);
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const kor::u32 binding,
        const ResourceRef<const Sampler>& sampler, const kor::u32 index)
    {
        return Write(binding, Descriptor(sampler), index);
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const kor::u32 binding,
        const ResourceRef<const AccelerationStructure>& accelerationStructure, const kor::u32 index)
    {
        return Write(binding, Descriptor(accelerationStructure), index);
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const std::string_view name,
        const ResourceRef<const Buffer>& buffer)
    {
        return Write(name, Descriptor(buffer));
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const std::string_view name,
        const ResourceRef<const ImageView>& imageView)
    {
        return Write(name, Descriptor(imageView));
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const std::string_view name,
        const ResourceRef<const ImageView>& imageView, const ResourceRef<const Sampler>& sampler)
    {
        return Write(name, Descriptor(imageView, sampler));
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const std::string_view name,
        const ResourceRef<const Sampler>& sampler)
    {
        return Write(name, Descriptor(sampler));
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const std::string_view name,
        const ResourceRef<const AccelerationStructure>& accelerationStructure)
    {
        return Write(name, Descriptor(accelerationStructure));
    }

    // The image overloads. Unlike every other kind, these cannot make their descriptor here: a
    // binding is filled with a *view*, and which view depends on how the shader declared the
    // binding — 2D, cube, array — which is only known once the layout has been read. So the image
    // is recorded as it was given and turned into a view in Resolve().
    DescriptorSet::Builder& DescriptorSet::Builder::Write(const kor::u32 binding,
        const ResourceRef<const Image>& image, const kor::u32 index)
    {
        return Record({ .binding = binding, .index = index, .image = image });
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const kor::u32 binding,
        const ResourceRef<const Image>& image, const ResourceRef<const Sampler>& sampler, const kor::u32 index)
    {
        return Record({ .binding = binding, .index = index, .image = image, .imageSampler = sampler });
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const std::string_view name,
        const ResourceRef<const Image>& image)
    {
        const auto [base, index] = SplitIndex(name);
        return Record({ .name = std::string(base), .index = index, .image = image });
    }

    DescriptorSet::Builder& DescriptorSet::Builder::Write(const std::string_view name,
        const ResourceRef<const Image>& image, const ResourceRef<const Sampler>& sampler)
    {
        const auto [base, index] = SplitIndex(name);
        return Record({ .name = std::string(base), .index = index, .image = image, .imageSampler = sampler });
    }

    /**
     * @brief Re-reads the layout and rebuilds `writes` from `pending` for this attempt.
     *
     * Run per attempt, which is the whole point: a shader edit can move a binding, lengthen an
     * array or reshape a block, and every one of those gives a different answer here. That is what
     * a rebuild is for.
     */
    VoidResult DescriptorSet::Builder::Resolve() const
    {
        const auto reject = [](std::string message) {
            return std::unexpected(Error{ .code = ErrorCode::eInvalidArgument, .message = std::move(message) });
        };

        // From the pipeline every time. A reload that reshaped this set built a *new* layout, and
        // the one captured at construction is expired — asking again is what finds the new one.
        if (pipeline.Alive()) {
            if (!pipeline.Valid())
                return reject("The pipeline this set belongs to is unusable, so it has no layout to fill.");
            layout = pipeline->SetLayoutRef(setIndex);
        }

        InitWrites();

        if (pending.empty()) return {};

        if (!layout.Valid())
            return reject("The layout this set is built against is unusable, so there is nothing to fill.");

        const auto& bindings = layout->Bindings();

        for (const auto& write : pending)
        {
            // --- which binding? Numbers are taken as given; names are looked up now, against the
            // layout this attempt found, so a binding that moved is followed rather than missed.
            kor::u32 binding;
            if (write.binding) {
                binding = *write.binding;
                if (!bindings.contains(binding))
                    return reject(std::format("Binding {} does not exist in the layout.", binding));
            } else {
                const auto found = layout->FindBinding(write.name);
                if (!found) {
                    // List what it does have. The mistake is nearly always a typo or a stale name,
                    // and both are fixed by seeing the real ones without going back to the shader.
                    const auto names = layout->BindingNames();
                    if (names.empty())
                        return reject(std::format(
                            "This set has no binding called '{}'; none of its bindings are named, so "
                            "they can only be written by number.", write.name));
                    std::string available;
                    for (const auto& candidate : names) {
                        if (!available.empty()) available += ", ";
                        available += '\'' + candidate + '\'';
                    }
                    return reject(std::format("This set has no binding called '{}'. It has: {}.",
                                              write.name, available));
                }
                binding = *found;
            }

            const auto& description = bindings.at(binding);
            auto& slots = writes[binding];

            // --- which element? A variable-count (bindless) binding reflects a count of 0 and is
            // allocated up to a cap, so grow to fit instead of rejecting.
            if (write.index >= slots.size()) {
                if (description.count == 0 && write.index < 256) {
                    slots.resize(write.index + 1);
                } else {
                    return reject(std::format("Index {} is out of bounds for binding {} (count {}).",
                                              write.index, binding, slots.size()));
                }
            }
            if (slots[write.index].IsValid())
                return reject(std::format("Descriptor at binding {} index {} is already written.",
                                          binding, write.index));

            // --- what goes there? A semantic write has to make its buffer first, an image write
            // has to become a view first, and an ordinary one already holds its resource.
            Descriptor descriptor = write.descriptor;
            if (write.image.Alive() || write.image.Poisoned())
            {
                if (!write.image.Valid())
                    return reject(std::format(
                        "The image written to binding {} is unusable, so there is nothing to view.", binding));

                // The role decides which usage the image had to be created for, and saying so here
                // beats the driver's version of the same complaint: the fix is one flag on the
                // image's builder, and this is the sentence that names it.
                const auto needs = [&](const Image::Usage usage, const char* flag) -> std::optional<std::string> {
                    if (write.image->UsageFlags() & usage) return std::nullopt;
                    return std::format(
                        "The image written to binding {} was not created with Image::Usage::{}, which is "
                        "what that binding needs. Add .setUsage(kor::Image::Usage::{}) where it is built.",
                        binding, flag, flag);
                };

                std::optional<std::string> missing;
                switch (description.type) {
                case DescriptorType::eCombinedImageSampler:
                case DescriptorType::eSampledImage:  missing = needs(Image::Usage::eSampled, "eSampled"); break;
                case DescriptorType::eStorageImage:  missing = needs(Image::Usage::eStorage, "eStorage"); break;
                default:
                    return reject(std::format(
                        "Binding {} does not hold an image, so an image cannot be written to it.", binding));
                }
                if (missing) return reject(std::move(*missing));

                // The shape the *shader* declared. Only it can settle the cases the image cannot:
                // six layers are equally a cube map and a 2D array. A binding reflection could not
                // shape is taken as an ordinary 2D image, which is what it almost always is, and
                // which fails loudly in Image::View rather than silently if it is not.
                const auto shape = description.shape == ImageShape::eUnknown ? ImageShape::e2D : description.shape;
                const auto view = write.image->View(shape);
                if (!view.Valid())
                    return reject(std::format(
                        "Binding {} declares an image the written one cannot be viewed as. Check its "
                        "array layers and type against what the shader declares, or bind an "
                        "ImageView you have built yourself.", binding));

                descriptor = write.imageSampler.Alive() || write.imageSampler.Poisoned()
                    ? Descriptor(view, write.imageSampler)
                    : Descriptor(view);
            }
            else if (write.semantic)
            {
                if (description.members.empty())
                    return reject(std::format(
                        "Binding {} is not a block with fields, so there is nothing for a semantic to "
                        "fill. Only a uniform or storage buffer can be written this way.", binding));

                SemanticSerializer* serializer = write.semantic->resolve ? write.semantic->resolve() : nullptr;
                if (!serializer)
                    return reject(std::format(
                        "The resource written to binding {} is unusable ('{}'), so there is nothing to "
                        "fill the block from.", binding, write.semantic->what));

                // The buffer for exactly this shape, from the object that will keep it filled. A
                // shape it has not been asked for before is created here — which is how a block that
                // gained a field arrives with a buffer the right size, already filled.
                auto buffer = serializer->SemanticStorage().Acquire(description.members,
                                                                    description.blockSize, *serializer);
                if (!buffer) return std::unexpected(buffer.error());
                descriptor = Descriptor(*buffer);
            }

            if (!descriptor.IsValid())
                return std::unexpected(descriptor.Failure().value_or(Error{
                    .code = ErrorCode::eInvalidArgument,
                    .message = std::format("Descriptor at binding {} index {} is invalid.", binding, write.index) }));

            // The descriptor has to hold the kind the binding expects. The accessors throw
            // BackendException on a type mismatch; catch and convert into a rejection, so a texture
            // bound where the shader declared a buffer is reported rather than written as the wrong
            // kind of descriptor.
            try {
                switch (description.type)
                {
                case DescriptorType::eUniformBuffer:
                case DescriptorType::eStorageBuffer:
                    (void)descriptor.BoundBuffer();
                    break;
                case DescriptorType::eCombinedImageSampler:
                    (void)descriptor.BoundImageView();
                    (void)descriptor.BoundSampler();
                    break;
                case DescriptorType::eSampledImage:
                case DescriptorType::eStorageImage:
                    (void)descriptor.BoundImageView();
                    break;
                case DescriptorType::eSampler:
                    (void)descriptor.BoundSampler();
                    break;
                case DescriptorType::eAccelerationStructure:
                    (void)descriptor.BoundAccelerationStructure();
                    break;
                case DescriptorType::eUniformTexelBuffer:
                case DescriptorType::eStorageTexelBuffer:
                    (void)descriptor.BoundBufferView();
                    break;
                default:
                    return reject(std::format("Unknown descriptor type for binding {}.", binding));
                }
            } catch (const BackendException& e) {
                return std::unexpected(e.error);
            } catch (const std::exception& e) {
                return reject(std::format(
                    "Descriptor at binding {} index {} does not match the binding type: {}",
                    binding, write.index, e.what()));
            }

            slots[write.index] = descriptor;
        }
        return {};
    }
    kor::Result<std::unique_ptr<DescriptorSet>> DescriptorSet::Builder::Create() const
    {
        BeginAttempt();

        // Adopted here rather than in the constructor, so that every attempt records the generation
        // its inputs had *this* time. Adopting once at construction would leave the first
        // generations recorded for ever, and DependenciesChanged() would then answer yes on every
        // frame after the first reload.
        if (pipeline.Alive() || !layout.Alive()) Adopt(pipeline, "pipeline");

        // Before Resolve(), not after: a write that was refused outright — something that cannot
        // fill a semantic block at all — is a more specific answer than anything resolving the rest
        // of the set could produce, and there is no point doing that work to discard it.
        if (_error) return std::unexpected(*_error);

        if (auto v = Resolve(); !v) return std::unexpected(v.error());
        Adopt(layout, "descriptor set layout");

        if (auto v = Validate(); !v) return std::unexpected(v.error());

        const auto api = Context::ActiveAPI();
        if (api != API::eVulkan)
            return Fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return Guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<DescriptorSet> {
            return kor::MakeBackendPtr<DescriptorSet, vk::DescriptorSet>(*this);
        });
    }

    kor::Resource<DescriptorSet> DescriptorSet::Builder::Build(const std::source_location where) const
    {
        auto set = Materialize<DescriptorSet>(*this, "DescriptorSet", where);
        // Registered even when poisoned, exactly as a pipeline is: the Repository's repair pass is
        // what replays the builder when the layout it was built against is reshaped, and what brings
        // the set back once a broken shader compiles again.
        if (Context::HasRepository())
            Context::Repository().AddRef(ResourceRef<const DescriptorSet>(set));
        return set;
    }

    std::optional<kor::u32> DescriptorSet::ResolveWriteTarget(const std::string_view name) const
    {
        if (!_layout.Valid()) {
            log::Error("Cannot write to binding '{}': this set's layout is unusable.", name);
            return std::nullopt;
        }
        const auto binding = _layout->FindBinding(name);
        if (!binding) {
            const auto names = _layout->BindingNames();
            std::string available;
            for (const auto& candidate : names) {
                if (!available.empty()) available += ", ";
                available += '\'' + candidate + '\'';
            }
            log::Error("This set has no binding called '{}'.{}", name,
                       available.empty() ? std::string(" None of its bindings are named.")
                                         : std::format(" It has: {}.", available));
        }
        return binding;
    }

    // The resource overloads of Rebind(): the matching Descriptor, then the one virtual rebind a
    // backend implements. Named ones look the binding up first; a name nothing answers to is
    // reported by resolveWriteTarget and the rebind is dropped.
    void DescriptorSet::Rebind(const kor::u32 binding, const ResourceRef<const Buffer>& buffer, const kor::u32 index)
    { Rebind(binding, Descriptor(buffer), index); }

    void DescriptorSet::Rebind(const kor::u32 binding, const Buffer::Slice& slice, const kor::u32 index)
    { Rebind(binding, Descriptor(slice.buffer, slice.offset, slice.size), index); }

    void DescriptorSet::Rebind(const std::string_view name, const Buffer::Slice& slice)
    { Rebind(name, Descriptor(slice.buffer, slice.offset, slice.size)); }

    void DescriptorSet::Rebind(const kor::u32 binding, const ResourceRef<const BufferView>& bufferView, const kor::u32 index)
    { Rebind(binding, Descriptor(bufferView), index); }

    void DescriptorSet::Rebind(const std::string_view name, const ResourceRef<const BufferView>& bufferView)
    { Rebind(name, Descriptor(bufferView)); }

    void DescriptorSet::Rebind(const kor::u32 binding, const ResourceRef<const ImageView>& imageView, const kor::u32 index)
    { Rebind(binding, Descriptor(imageView), index); }

    void DescriptorSet::Rebind(const kor::u32 binding, const ResourceRef<const ImageView>& imageView,
                              const ResourceRef<const Sampler>& sampler, const kor::u32 index)
    { Rebind(binding, Descriptor(imageView, sampler), index); }

    void DescriptorSet::Rebind(const kor::u32 binding, const ResourceRef<const Sampler>& sampler, const kor::u32 index)
    { Rebind(binding, Descriptor(sampler), index); }

    void DescriptorSet::Rebind(const kor::u32 binding,
                              const ResourceRef<const AccelerationStructure>& accelerationStructure, const kor::u32 index)
    { Rebind(binding, Descriptor(accelerationStructure), index); }

    void DescriptorSet::Rebind(const std::string_view name, const Descriptor& descriptor)
    {
        const auto [base, index] = SplitIndex(name);
        if (const auto binding = ResolveWriteTarget(base)) Rebind(*binding, descriptor, index);
    }

    void DescriptorSet::Rebind(const std::string_view name, const ResourceRef<const Buffer>& buffer)
    { Rebind(name, Descriptor(buffer)); }

    void DescriptorSet::Rebind(const std::string_view name, const ResourceRef<const ImageView>& imageView)
    { Rebind(name, Descriptor(imageView)); }

    void DescriptorSet::Rebind(const std::string_view name, const ResourceRef<const ImageView>& imageView,
                              const ResourceRef<const Sampler>& sampler)
    { Rebind(name, Descriptor(imageView, sampler)); }

    void DescriptorSet::Rebind(const std::string_view name, const ResourceRef<const Sampler>& sampler)
    { Rebind(name, Descriptor(sampler)); }

    void DescriptorSet::Rebind(const std::string_view name,
                              const ResourceRef<const AccelerationStructure>& accelerationStructure)
    { Rebind(name, Descriptor(accelerationStructure)); }

    DescriptorSet::DescriptorSet(const Builder& builder) : _layout(builder.layout), _writes(builder.writes)
    {
        _isPerFrame = false;
        for (const auto& write : _writes | std::views::values) {
            for (const auto& descriptor : write)
            {
                std::visit([this]<typename T0>(T0 binding)
                {
                    using T = std::decay_t<T0>;
                    if constexpr (std::is_same_v<BufferDescriptor, T>)
                    {
                        if (binding._buffer->IsPerFrame()) {
                            _isPerFrame = true;
                        }
                    }
                    else if constexpr (std::is_same_v<ImageDescriptor, T> || std::is_same_v<CombinedImageSamplerDescriptor, T>)
                    {
                        if (binding._imageView->IsPerFrame()) {
                            _isPerFrame = true;
                        }
                    }
                }, descriptor._descriptor);
            }
        }
    }
}
