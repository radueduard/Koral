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

    ResourceRef<const Buffer> Descriptor::getBufferRef() const {
        if (!valid) return {};
        if (const auto* buffer = std::get_if<BufferDescriptor>(&_descriptor)) return buffer->_buffer;
        return {};
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


    // One slot per declared binding, so write() can address them by index.
    void DescriptorSet::Builder::initWrites()
    {
        if (!layout.valid()) return;  // poisoned or absent: create() will refuse to build anyway
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
        initWrites();
    }

    DescriptorSet::Builder::Builder(ResourceRef<const DescriptorSetLayout> layout) : layout(layout)
    {
        initWrites();
    }

    DescriptorSet::Builder::Builder(const DescriptorSetLayout& layout)
        : layout(ResourceRef<const DescriptorSetLayout>(&layout))
    {
        initWrites();
    }

    DescriptorSet::Builder& DescriptorSet::Builder::rejectSemantic(const glm::u32 binding, const char* what,
                                                                   const bool unusable)
    {
        if (!_error) _error = Error{
            .code = ErrorCode::eInvalidArgument,
            .message = unusable
                ? std::format("The resource written to binding {} is unusable ('{}'), so there is "
                              "nothing to fill the block from.", binding, what ? what : "?")
                : std::format("Binding {} was written from a '{}', which cannot fill a semantic "
                              "block: it does not implement kor::SemanticSerializer. Pass a "
                              "kor::Descriptor instead, or make the type serializable.",
                              binding, what ? what : "?"),
        };
        return *this;
    }

    DescriptorSet::Builder& DescriptorSet::Builder::writeSemantic(const glm::u32 binding, SemanticSerializer& serializer)
    {
        return recordSemantic(binding, [&serializer] { return &serializer; }, "<reference>");
    }

    DescriptorSet::Builder& DescriptorSet::Builder::recordSemantic(
        const glm::u32 binding, std::function<SemanticSerializer*()> resolve, std::string what)
    {
        if (_error) return *this;
        semanticWrites[binding] = { std::move(resolve), std::move(what) };
        return *this;
    }

    /**
     * @brief Re-reads the layout and fills every semantic binding from it.
     *
     * Run per attempt, which is the whole point: a shader edit that reshapes a block gives a
     * different answer here, and that is what a rebuild is for.
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

        if (semanticWrites.empty()) return {};

        for (const auto& [binding, semantic] : semanticWrites) {
            if (!layout.valid())
                return reject(std::format("Cannot fill binding {} from semantics: the layout is unusable.", binding));

            const auto& bindings = layout->bindings();
            const auto it = bindings.find(binding);
            if (it == bindings.end())
                return reject(std::format("The layout has no binding {} to fill.", binding));

            const auto& description = it->second;
            if (description.members.empty())
                return reject(std::format(
                    "Binding {} is not a block with fields, so there is nothing for a semantic to "
                    "fill. Only a uniform or storage buffer can be written this way.", binding));

            SemanticSerializer* serializer = semantic.resolve ? semantic.resolve() : nullptr;
            if (!serializer)
                return reject(std::format(
                    "The resource written to binding {} is unusable ('{}'), so there is nothing to "
                    "fill the block from.", binding, semantic.what));

            // The buffer for exactly this shape, from the object that will keep it filled. A shape
            // it has not been asked for before is created here — which is how a block that gained a
            // field arrives with a buffer the right size, already filled.
            auto buffer = serializer->semanticBuffers().acquire(description.members,
                                                                description.blockSize, *serializer);
            if (!buffer) return std::unexpected(buffer.error());

            auto& slots = writes[binding];
            if (slots.empty()) slots.resize(1);
            slots[0] = Descriptor(*buffer);
        }
        return {};
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const glm::u32 binding, const Descriptor& descriptor, const glm::u32 index)
    {
        // Sticky: once a write has failed, later writes are ignored and the first error
        // is surfaced by build(). This keeps the fluent .write(...).write(...) chain.
        if (_error) return *this;

        const auto reject = [&](const ErrorCode code, std::string message) -> DescriptorSet::Builder& {
            _error = Error{ .code = code, .message = std::move(message) };
            return *this;
        };

        if (!writes.contains(binding))
            return reject(ErrorCode::eInvalidArgument, std::format("Binding {} does not exist in the layout.", binding));

        if (index >= writes[binding].size()) {
            // Bindless (variable-count) bindings reflect a count of 0 in the layout but are
            // allocated up to a cap (256, see descriptorSetLayout/descriptorPool). Grow the
            // write list to fit instead of rejecting, so callers can fill them by index.
            bool variableCount = false;
            for (const auto& [b, type, count] : layout->getBindings()) {
                if (b == binding) { variableCount = (count == 0); break; }
            }
            if (variableCount && index < 256) {
                writes[binding].resize(index + 1);
            } else {
                return reject(ErrorCode::eInvalidArgument,
                    std::format("Index {} is out of bounds for binding {} (count {}).", index, binding, writes[binding].size()));
            }
        }
        if (writes[binding][index].isValid())
            return reject(ErrorCode::eInvalidArgument, std::format("Descriptor at binding {} index {} is already written.", binding, index));
        if (!descriptor.isValid())
            return _error = descriptor.error().value_or(Error{ .code = ErrorCode::eInvalidArgument,
                .message = std::format("Descriptor at binding {} index {} is invalid.", binding, index) }), *this;

        // Validate the descriptor holds the type the binding expects. The accessors throw
        // BackendException on a type mismatch; catch and convert into the sticky error.
        try {
            switch (layout->getBindingType(binding))
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
            default:
                return reject(ErrorCode::eInvalidArgument, std::format("Unknown descriptor type for binding {}.", binding));
            }
        } catch (const BackendException& e) {
            _error = e.error;
            return *this;
        } catch (const std::exception& e) {
            return reject(ErrorCode::eInvalidArgument,
                std::format("Descriptor at binding {} index {} does not match the binding type: {}", binding, index, e.what()));
        }

        writes[binding][index] = descriptor;
        return *this;
    }

    kor::Result<std::unique_ptr<DescriptorSet>> DescriptorSet::Builder::create() const
    {
        beginAttempt();

        // Adopted here rather than in the constructor, so that every attempt records the generation
        // its inputs had *this* time. Adopting once at construction would leave the first
        // generations recorded for ever, and dependenciesChanged() would then answer yes on every
        // frame after the first reload.
        if (pipeline.alive() || !layout.alive()) adopt(pipeline, "pipeline");
        if (auto v = resolve(); !v) return std::unexpected(v.error());
        adopt(layout, "descriptor set layout");

        if (_error) return std::unexpected(*_error);
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
