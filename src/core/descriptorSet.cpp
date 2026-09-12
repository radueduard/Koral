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

#include "../backends/open_gl/descriptorSet.h"
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
    // records the reason in `_error`, which DescriptorSet::Builder::build() surfaces.
    Descriptor::Descriptor(const ResourceRef<const Buffer>& buffer, const glm::i64 offset, const glm::i64 range)
        : valid(true), _descriptor(BufferDescriptor{ buffer, offset, range })
    {
        if (!buffer) {
            valid = false; _error = Error{ .code = ErrorCode::eInvalidArgument, .message = "Buffer descriptor has an invalid buffer." };
            return;
        }
        if (offset < 0 || offset >= buffer->getSize()) {
            valid = false; _error = Error{ .code = ErrorCode::eBufferRangeOutOfBounds,
                .message = std::format("Buffer descriptor offset {} is out of range (buffer size {}).", offset, buffer->getSize()) };
            return;
        }
        if (range < 0) {
            valid = false; _error = Error{ .code = ErrorCode::eBufferRangeOutOfBounds,
                .message = std::format("Buffer descriptor has a negative range {}.", range) };
            return;
        }
        if (offset + range > buffer->getSize()) {
            valid = false; _error = Error{ .code = ErrorCode::eBufferRangeOutOfBounds,
                .message = std::format("Buffer descriptor offset {} + range {} exceeds buffer size {}.", offset, range, buffer->getSize()) };
            return;
        }

        if (range == 0) {
            std::visit([](auto& d) {
                using D = std::decay_t<decltype(d)>;
                if constexpr (std::is_same_v<D, BufferDescriptor>) {
                    if (d._range == 0) {
                        d._range = d._buffer->getSize() - d._offset;
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

    ResourceRef<const Buffer> Descriptor::getBufferRef() const {
        if (!valid) return {};
        if (const auto* buffer = std::get_if<BufferDescriptor>(&_descriptor)) return buffer->_buffer;
        // A texel binding is a buffer as far as synchronisation is concerned — the formatting is
        // the shader's business, the hazard is the bytes'. Answering with the underlying buffer
        // here is what makes the barrier resolver see a texel fetch at all.
        if (const auto* texel = std::get_if<TexelBufferDescriptor>(&_descriptor)) {
            if (texel->_bufferView.valid()) return texel->_bufferView->getBuffer();
        }
        return {};
    }

    ResourceRef<const BufferView> Descriptor::getBufferViewRef() const {
        if (!valid) return {};
        if (const auto* texel = std::get_if<TexelBufferDescriptor>(&_descriptor)) return texel->_bufferView;
        return {};
    }

    const BufferView& Descriptor::getBufferView() const {
        if (!valid) {
            kor::log::error("Attempted to get buffer view from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (!std::holds_alternative<TexelBufferDescriptor>(_descriptor)) {
            kor::log::error("Attempted to get buffer view from a descriptor that does not hold one!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        return *std::get<TexelBufferDescriptor>(_descriptor)._bufferView;
    }

    ResourceRef<const ImageView> Descriptor::getImageViewRef() const {
        if (!valid) return {};
        if (const auto* image = std::get_if<ImageDescriptor>(&_descriptor)) return image->_imageView;
        if (const auto* combined = std::get_if<CombinedImageSamplerDescriptor>(&_descriptor)) return combined->_imageView;
        return {};
    }

    const Buffer & Descriptor::getBuffer() const {
        if (!valid) {
            kor::log::error("Attempted to get buffer from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (!std::holds_alternative<BufferDescriptor>(_descriptor)) {
            kor::log::error("Attempted to get buffer from a descriptor that does not hold a buffer!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        return *std::get<BufferDescriptor>(_descriptor)._buffer;
    }

    glm::i64 Descriptor::getOffset() const {
        if (!valid) {
            kor::log::error("Attempted to get offset from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (!std::holds_alternative<BufferDescriptor>(_descriptor)) {
            kor::log::error("Attempted to get offset from a descriptor that does not hold a buffer!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        return std::get<BufferDescriptor>(_descriptor)._offset;
    }

    glm::i64 Descriptor::getRange() const {
        if (!valid) {
            kor::log::error("Attempted to get range from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (!std::holds_alternative<BufferDescriptor>(_descriptor)) {
            kor::log::error("Attempted to get range from a descriptor that does not hold a buffer!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        return std::get<BufferDescriptor>(_descriptor)._range;
    }

    const ImageView & Descriptor::getImageView() const {
        if (!valid) {
            kor::log::error("Attempted to get image view from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (std::holds_alternative<ImageDescriptor>(_descriptor)) {
            return *std::get<ImageDescriptor>(_descriptor)._imageView;
        }
        if (std::holds_alternative<CombinedImageSamplerDescriptor>(_descriptor)) {
            return *std::get<CombinedImageSamplerDescriptor>(_descriptor)._imageView;
        }
        kor::log::error("Attempted to get image view from a descriptor that does not hold an image view!");
        throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor does not hold an image view." });
    }

    const Sampler & Descriptor::getSampler() const {
        if (!valid) {
            kor::log::error("Attempted to get sampler from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (std::holds_alternative<SamplerDescriptor>(_descriptor)) {
            return *std::get<SamplerDescriptor>(_descriptor)._sampler;
        }
        if (std::holds_alternative<CombinedImageSamplerDescriptor>(_descriptor)) {
            return *std::get<CombinedImageSamplerDescriptor>(_descriptor)._sampler;
        }
        kor::log::error("Attempted to get sampler from a descriptor that does not hold a sampler!");
        throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor does not hold a sampler." });
    }

    const AccelerationStructure & Descriptor::getAccelerationStructure() const {
        if (!valid) {
            kor::log::error("Attempted to get acceleration structure from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (!std::holds_alternative<AccelerationStructureDescriptor>(_descriptor)) {
            kor::log::error("Attempted to get acceleration structure from a descriptor that does not hold one!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        return *std::get<AccelerationStructureDescriptor>(_descriptor)._accelerationStructure;
    }



    // One empty slot per declared binding, so resolve() can place writes by index. Const, and run
    // per attempt: the layout it reads is the one *this* attempt found, and a reload that added a
    // binding or lengthened an array gives a different shape.
    void DescriptorSet::Builder::initWrites() const
    {
        writes.clear();
        if (!layout.valid()) return;  // poisoned or absent: resolve() will refuse to build anyway
        for (const auto& [binding, type, count] : layout->getBindings()) {
            writes[binding] = std::vector<Descriptor>();
            writes[binding].resize(count);
        }
    }

    DescriptorSet::Builder::Builder(ResourceRef<const Pipeline> pipeline, const glm::u32 setIndex)
        : pipeline(std::move(pipeline)), setIndex(setIndex)
    {
        // Resolved here *and* on every later attempt: a shader reload can replace the layout, and a
        // rebuild has to fill the new one. A poisoned pipeline has no layouts to ask for — `layout`
        // stays empty, resolve() refuses, and this set is poisoned with the pipeline's error as its
        // cause. @see resolve
        if (this->pipeline.valid()) layout = this->pipeline->getSetLayoutRef(setIndex);
    }

    DescriptorSet::Builder::Builder(ResourceRef<const DescriptorSetLayout> layout) : layout(layout)
    {
    }

    DescriptorSet::Builder::Builder(const DescriptorSetLayout& layout)
        : layout(ResourceRef<const DescriptorSetLayout>(&layout))
    {
    }

    std::pair<std::string_view, glm::u32> DescriptorSet::splitIndex(const std::string_view name)
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

        glm::u32 index = 0;
        for (const char c : digits) {
            if (c < '0' || c > '9') return { name, 0 };
            index = index * 10 + static_cast<glm::u32>(c - '0');
        }
        return { name.substr(0, open), index };
    }

    DescriptorSet::Builder& DescriptorSet::Builder::record(PendingWrite write)
    {
        // Sticky: once a write has failed, later writes are ignored and the first error is
        // surfaced by build(). This keeps the fluent .write(...).write(...) chain.
        if (_error) return *this;
        pending.push_back(std::move(write));
        return *this;
    }

    DescriptorSet::Builder& DescriptorSet::Builder::rejectSemantic(const glm::u32 binding, const char* what,
                                                                   const bool unusable)
    {
        return rejectSemantic(std::to_string(binding), what, unusable);
    }

    DescriptorSet::Builder& DescriptorSet::Builder::rejectSemantic(const std::string_view name, const char* what,
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

    DescriptorSet::Builder& DescriptorSet::Builder::writeSemantic(const glm::u32 binding, SemanticSerializer& serializer)
    {
        return recordSemantic(binding, [&serializer] { return &serializer; }, "<reference>");
    }

    DescriptorSet::Builder& DescriptorSet::Builder::writeSemantic(const std::string_view name, SemanticSerializer& serializer)
    {
        return recordSemantic(name, [&serializer] { return &serializer; }, "<reference>");
    }

    DescriptorSet::Builder& DescriptorSet::Builder::recordSemantic(
        const glm::u32 binding, std::function<SemanticSerializer*()> resolve, std::string what)
    {
        return record({ .binding = binding,
                        .semantic = SemanticWrite{ std::move(resolve), std::move(what) } });
    }

    DescriptorSet::Builder& DescriptorSet::Builder::recordSemantic(
        const std::string_view name, std::function<SemanticSerializer*()> resolve, std::string what)
    {
        const auto [base, index] = splitIndex(name);
        return record({ .name = std::string(base), .index = index,
                        .semantic = SemanticWrite{ std::move(resolve), std::move(what) } });
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const glm::u32 binding, const Descriptor& descriptor,
                                                          const glm::u32 index)
    {
        return record({ .binding = binding, .index = index, .descriptor = descriptor });
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const std::string_view name, const Descriptor& descriptor)
    {
        const auto [base, index] = splitIndex(name);
        return record({ .name = std::string(base), .index = index, .descriptor = descriptor });
    }

    // The resource overloads. Each is the corresponding Descriptor constructor and nothing more —
    // the kind is decided by the argument's type here rather than by the caller naming it, and
    // whether that kind is what the binding actually expects is settled in resolve().
    DescriptorSet::Builder& DescriptorSet::Builder::write(const glm::u32 binding,
        const ResourceRef<const Buffer>& buffer, const glm::u32 index)
    {
        return write(binding, Descriptor(buffer), index);
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const glm::u32 binding,
        const Buffer::Slice& slice, const glm::u32 index)
    {
        return write(binding, Descriptor(slice.buffer, slice.offset, slice.size), index);
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const std::string_view name, const Buffer::Slice& slice)
    {
        return write(name, Descriptor(slice.buffer, slice.offset, slice.size));
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const glm::u32 binding,
        const ResourceRef<const BufferView>& bufferView, const glm::u32 index)
    {
        return write(binding, Descriptor(bufferView), index);
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const std::string_view name,
        const ResourceRef<const BufferView>& bufferView)
    {
        return write(name, Descriptor(bufferView));
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const glm::u32 binding,
        const ResourceRef<const ImageView>& imageView, const glm::u32 index)
    {
        return write(binding, Descriptor(imageView), index);
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const glm::u32 binding,
        const ResourceRef<const ImageView>& imageView, const ResourceRef<const Sampler>& sampler,
        const glm::u32 index)
    {
        return write(binding, Descriptor(imageView, sampler), index);
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const glm::u32 binding,
        const ResourceRef<const Sampler>& sampler, const glm::u32 index)
    {
        return write(binding, Descriptor(sampler), index);
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const glm::u32 binding,
        const ResourceRef<const AccelerationStructure>& accelerationStructure, const glm::u32 index)
    {
        return write(binding, Descriptor(accelerationStructure), index);
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const std::string_view name,
        const ResourceRef<const Buffer>& buffer)
    {
        return write(name, Descriptor(buffer));
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const std::string_view name,
        const ResourceRef<const ImageView>& imageView)
    {
        return write(name, Descriptor(imageView));
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const std::string_view name,
        const ResourceRef<const ImageView>& imageView, const ResourceRef<const Sampler>& sampler)
    {
        return write(name, Descriptor(imageView, sampler));
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const std::string_view name,
        const ResourceRef<const Sampler>& sampler)
    {
        return write(name, Descriptor(sampler));
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const std::string_view name,
        const ResourceRef<const AccelerationStructure>& accelerationStructure)
    {
        return write(name, Descriptor(accelerationStructure));
    }

    // The image overloads. Unlike every other kind, these cannot make their descriptor here: a
    // binding is filled with a *view*, and which view depends on how the shader declared the
    // binding — 2D, cube, array — which is only known once the layout has been read. So the image
    // is recorded as it was given and turned into a view in resolve().
    DescriptorSet::Builder& DescriptorSet::Builder::write(const glm::u32 binding,
        const ResourceRef<const Image>& image, const glm::u32 index)
    {
        return record({ .binding = binding, .index = index, .image = image });
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const glm::u32 binding,
        const ResourceRef<const Image>& image, const ResourceRef<const Sampler>& sampler, const glm::u32 index)
    {
        return record({ .binding = binding, .index = index, .image = image, .imageSampler = sampler });
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const std::string_view name,
        const ResourceRef<const Image>& image)
    {
        const auto [base, index] = splitIndex(name);
        return record({ .name = std::string(base), .index = index, .image = image });
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const std::string_view name,
        const ResourceRef<const Image>& image, const ResourceRef<const Sampler>& sampler)
    {
        const auto [base, index] = splitIndex(name);
        return record({ .name = std::string(base), .index = index, .image = image, .imageSampler = sampler });
    }

    /**
     * @brief Re-reads the layout and rebuilds `writes` from `pending` for this attempt.
     *
     * Run per attempt, which is the whole point: a shader edit can move a binding, lengthen an
     * array or reshape a block, and every one of those gives a different answer here. That is what
     * a rebuild is for.
     */
    VoidResult DescriptorSet::Builder::resolve() const
    {
        const auto reject = [](std::string message) {
            return std::unexpected(Error{ .code = ErrorCode::eInvalidArgument, .message = std::move(message) });
        };

        // From the pipeline every time. A reload that reshaped this set built a *new* layout, and
        // the one captured at construction is expired — asking again is what finds the new one.
        if (pipeline.alive()) {
            if (!pipeline.valid())
                return reject("The pipeline this set belongs to is unusable, so it has no layout to fill.");
            layout = pipeline->getSetLayoutRef(setIndex);
        }

        initWrites();

        if (pending.empty()) return {};

        if (!layout.valid())
            return reject("The layout this set is built against is unusable, so there is nothing to fill.");

        const auto& bindings = layout->bindings();

        for (const auto& write : pending)
        {
            // --- which binding? Numbers are taken as given; names are looked up now, against the
            // layout this attempt found, so a binding that moved is followed rather than missed.
            glm::u32 binding;
            if (write.binding) {
                binding = *write.binding;
                if (!bindings.contains(binding))
                    return reject(std::format("Binding {} does not exist in the layout.", binding));
            } else {
                const auto found = layout->findBinding(write.name);
                if (!found) {
                    // List what it does have. The mistake is nearly always a typo or a stale name,
                    // and both are fixed by seeing the real ones without going back to the shader.
                    const auto names = layout->bindingNames();
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
            if (slots[write.index].isValid())
                return reject(std::format("Descriptor at binding {} index {} is already written.",
                                          binding, write.index));

            // --- what goes there? A semantic write has to make its buffer first, an image write
            // has to become a view first, and an ordinary one already holds its resource.
            Descriptor descriptor = write.descriptor;
            if (write.image.alive() || write.image.poisoned())
            {
                if (!write.image.valid())
                    return reject(std::format(
                        "The image written to binding {} is unusable, so there is nothing to view.", binding));

                // The role decides which usage the image had to be created for, and saying so here
                // beats the driver's version of the same complaint: the fix is one flag on the
                // image's builder, and this is the sentence that names it.
                const auto needs = [&](const Image::Usage usage, const char* flag) -> std::optional<std::string> {
                    if (write.image->getUsage() & usage) return std::nullopt;
                    return std::format(
                        "The image written to binding {} was not created with Image::Usage::{}, which is "
                        "what that binding needs. Add .addUsage(kor::Image::Usage::{}) where it is built.",
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
                // which fails loudly in Image::view rather than silently if it is not.
                const auto shape = description.shape == ImageShape::eUnknown ? ImageShape::e2D : description.shape;
                const auto view = write.image->view(shape);
                if (!view.valid())
                    return reject(std::format(
                        "Binding {} declares an image the written one cannot be viewed as. Check its "
                        "array layers and type against what the shader declares, or bind an "
                        "ImageView you have built yourself.", binding));

                descriptor = write.imageSampler.alive() || write.imageSampler.poisoned()
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
                auto buffer = serializer->semanticBuffers().acquire(description.members,
                                                                    description.blockSize, *serializer);
                if (!buffer) return std::unexpected(buffer.error());
                descriptor = Descriptor(*buffer);
            }

            if (!descriptor.isValid())
                return std::unexpected(descriptor.error().value_or(Error{
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
                    (void)descriptor.getBuffer();
                    break;
                case DescriptorType::eCombinedImageSampler:
                    (void)descriptor.getImageView();
                    (void)descriptor.getSampler();
                    break;
                case DescriptorType::eSampledImage:
                case DescriptorType::eStorageImage:
                    (void)descriptor.getImageView();
                    break;
                case DescriptorType::eSampler:
                    (void)descriptor.getSampler();
                    break;
                case DescriptorType::eAccelerationStructure:
                    (void)descriptor.getAccelerationStructure();
                    break;
                case DescriptorType::eUniformTexelBuffer:
                case DescriptorType::eStorageTexelBuffer:
                    (void)descriptor.getBufferView();
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
    kor::Result<std::unique_ptr<DescriptorSet>> DescriptorSet::Builder::create() const
    {
        beginAttempt();

        // Adopted here rather than in the constructor, so that every attempt records the generation
        // its inputs had *this* time. Adopting once at construction would leave the first
        // generations recorded for ever, and dependenciesChanged() would then answer yes on every
        // frame after the first reload.
        if (pipeline.alive() || !layout.alive()) adopt(pipeline, "pipeline");

        // Before resolve(), not after: a write that was refused outright — something that cannot
        // fill a semantic block at all — is a more specific answer than anything resolving the rest
        // of the set could produce, and there is no point doing that work to discard it.
        if (_error) return std::unexpected(*_error);

        if (auto v = resolve(); !v) return std::unexpected(v.error());
        adopt(layout, "descriptor set layout");

        if (auto v = validate(); !v) return std::unexpected(v.error());

        const auto api = Context::activeAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<DescriptorSet> {
            return (api == API::eVulkan)
                ? kor::MakeBackendPtr<DescriptorSet, vk::DescriptorSet>(*this)
                : kor::MakeBackendPtr<DescriptorSet, ogl::DescriptorSet>(*this);
        });
    }

    kor::Resource<DescriptorSet> DescriptorSet::Builder::build(const std::source_location where) const
    {
        auto set = materialize<DescriptorSet>(*this, "DescriptorSet", where);
        // Registered even when poisoned, exactly as a pipeline is: the Repository's repair pass is
        // what replays the builder when the layout it was built against is reshaped, and what brings
        // the set back once a broken shader compiles again.
        if (Context::HasRepository())
            Context::Repository().addRef(ResourceRef<const DescriptorSet>(set));
        return set;
    }

    std::optional<glm::u32> DescriptorSet::resolveWriteTarget(const std::string_view name) const
    {
        if (!_layout.valid()) {
            log::error("Cannot write to binding '{}': this set's layout is unusable.", name);
            return std::nullopt;
        }
        const auto binding = _layout->findBinding(name);
        if (!binding) {
            const auto names = _layout->bindingNames();
            std::string available;
            for (const auto& candidate : names) {
                if (!available.empty()) available += ", ";
                available += '\'' + candidate + '\'';
            }
            log::error("This set has no binding called '{}'.{}", name,
                       available.empty() ? std::string(" None of its bindings are named.")
                                         : std::format(" It has: {}.", available));
        }
        return binding;
    }

    // The resource overloads of Write(): the matching Descriptor, then the one virtual Write a
    // backend implements. Named ones look the binding up first; a name nothing answers to is
    // reported by resolveWriteTarget and the write is dropped.
    void DescriptorSet::Write(const glm::u32 binding, const ResourceRef<const Buffer>& buffer, const glm::u32 index)
    { Write(binding, Descriptor(buffer), index); }

    void DescriptorSet::Write(const glm::u32 binding, const Buffer::Slice& slice, const glm::u32 index)
    { Write(binding, Descriptor(slice.buffer, slice.offset, slice.size), index); }

    void DescriptorSet::Write(const std::string_view name, const Buffer::Slice& slice)
    { Write(name, Descriptor(slice.buffer, slice.offset, slice.size)); }

    void DescriptorSet::Write(const glm::u32 binding, const ResourceRef<const BufferView>& bufferView, const glm::u32 index)
    { Write(binding, Descriptor(bufferView), index); }

    void DescriptorSet::Write(const std::string_view name, const ResourceRef<const BufferView>& bufferView)
    { Write(name, Descriptor(bufferView)); }

    void DescriptorSet::Write(const glm::u32 binding, const ResourceRef<const ImageView>& imageView, const glm::u32 index)
    { Write(binding, Descriptor(imageView), index); }

    void DescriptorSet::Write(const glm::u32 binding, const ResourceRef<const ImageView>& imageView,
                              const ResourceRef<const Sampler>& sampler, const glm::u32 index)
    { Write(binding, Descriptor(imageView, sampler), index); }

    void DescriptorSet::Write(const glm::u32 binding, const ResourceRef<const Sampler>& sampler, const glm::u32 index)
    { Write(binding, Descriptor(sampler), index); }

    void DescriptorSet::Write(const glm::u32 binding,
                              const ResourceRef<const AccelerationStructure>& accelerationStructure, const glm::u32 index)
    { Write(binding, Descriptor(accelerationStructure), index); }

    void DescriptorSet::Write(const std::string_view name, const Descriptor& descriptor)
    {
        const auto [base, index] = splitIndex(name);
        if (const auto binding = resolveWriteTarget(base)) Write(*binding, descriptor, index);
    }

    void DescriptorSet::Write(const std::string_view name, const ResourceRef<const Buffer>& buffer)
    { Write(name, Descriptor(buffer)); }

    void DescriptorSet::Write(const std::string_view name, const ResourceRef<const ImageView>& imageView)
    { Write(name, Descriptor(imageView)); }

    void DescriptorSet::Write(const std::string_view name, const ResourceRef<const ImageView>& imageView,
                              const ResourceRef<const Sampler>& sampler)
    { Write(name, Descriptor(imageView, sampler)); }

    void DescriptorSet::Write(const std::string_view name, const ResourceRef<const Sampler>& sampler)
    { Write(name, Descriptor(sampler)); }

    void DescriptorSet::Write(const std::string_view name,
                              const ResourceRef<const AccelerationStructure>& accelerationStructure)
    { Write(name, Descriptor(accelerationStructure)); }

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
                        if (binding._buffer->isPerFrame()) {
                            _isPerFrame = true;
                        }
                    }
                    else if constexpr (std::is_same_v<ImageDescriptor, T> || std::is_same_v<CombinedImageSamplerDescriptor, T>)
                    {
                        if (binding._imageView->isPerFrame()) {
                            _isPerFrame = true;
                        }
                    }
                }, descriptor._descriptor);
            }
        }
    }
}
