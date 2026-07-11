//
// Created by radue on 3/4/2026.
//

#include <descriptorSet.h>
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


namespace gfx
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

    const Buffer & Descriptor::getBuffer() const {
        if (!valid) {
            gfx::log::error("Attempted to get buffer from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (!std::holds_alternative<BufferDescriptor>(_descriptor)) {
            gfx::log::error("Attempted to get buffer from a descriptor that does not hold a buffer!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        return *std::get<BufferDescriptor>(_descriptor)._buffer;
    }

    glm::i64 Descriptor::getOffset() const {
        if (!valid) {
            gfx::log::error("Attempted to get offset from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (!std::holds_alternative<BufferDescriptor>(_descriptor)) {
            gfx::log::error("Attempted to get offset from a descriptor that does not hold a buffer!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        return std::get<BufferDescriptor>(_descriptor)._offset;
    }

    glm::i64 Descriptor::getRange() const {
        if (!valid) {
            gfx::log::error("Attempted to get range from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (!std::holds_alternative<BufferDescriptor>(_descriptor)) {
            gfx::log::error("Attempted to get range from a descriptor that does not hold a buffer!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        return std::get<BufferDescriptor>(_descriptor)._range;
    }

    const ImageView & Descriptor::getImageView() const {
        if (!valid) {
            gfx::log::error("Attempted to get image view from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (std::holds_alternative<ImageDescriptor>(_descriptor)) {
            return *std::get<ImageDescriptor>(_descriptor)._imageView;
        }
        if (std::holds_alternative<CombinedImageSamplerDescriptor>(_descriptor)) {
            return *std::get<CombinedImageSamplerDescriptor>(_descriptor)._imageView;
        }
        gfx::log::error("Attempted to get image view from a descriptor that does not hold an image view!");
        throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor does not hold an image view." });
    }

    const Sampler & Descriptor::getSampler() const {
        if (!valid) {
            gfx::log::error("Attempted to get sampler from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (std::holds_alternative<SamplerDescriptor>(_descriptor)) {
            return *std::get<SamplerDescriptor>(_descriptor)._sampler;
        }
        if (std::holds_alternative<CombinedImageSamplerDescriptor>(_descriptor)) {
            return *std::get<CombinedImageSamplerDescriptor>(_descriptor)._sampler;
        }
        gfx::log::error("Attempted to get sampler from a descriptor that does not hold a sampler!");
        throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor does not hold a sampler." });
    }

    const AccelerationStructure & Descriptor::getAccelerationStructure() const {
        if (!valid) {
            gfx::log::error("Attempted to get acceleration structure from an invalid descriptor!");
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message = "Descriptor accessor used on an invalid or incompatible descriptor." });
        }
        if (!std::holds_alternative<AccelerationStructureDescriptor>(_descriptor)) {
            gfx::log::error("Attempted to get acceleration structure from a descriptor that does not hold one!");
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
    {
        adopt(pipeline, "pipeline");
        // A poisoned pipeline has no layouts to ask for. Leave `layout` empty: create() refuses to
        // build, and this descriptor set is poisoned with the pipeline's error as its cause.
        if (pipeline.valid()) layout = pipeline->getSetLayoutRef(setIndex);
        initWrites();
    }

    DescriptorSet::Builder::Builder(ResourceRef<const DescriptorSetLayout> layout) : layout(layout)
    {
        adopt(layout, "descriptor set layout");
        initWrites();
    }

    DescriptorSet::Builder::Builder(const DescriptorSetLayout& layout)
        : layout(ResourceRef<const DescriptorSetLayout>(&layout))
    {
        initWrites();
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

    gfx::Result<std::unique_ptr<DescriptorSet>> DescriptorSet::Builder::create() const
    {
        beginAttempt();

        if (_error) return std::unexpected(*_error);
        if (auto v = validate(); !v) return std::unexpected(v.error());

        const auto api = Context::activeAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<DescriptorSet> {
            return (api == API::eVulkan)
                ? gfx::MakeBackendPtr<DescriptorSet, vk::DescriptorSet>(*this)
                : gfx::MakeBackendPtr<DescriptorSet, ogl::DescriptorSet>(*this);
        });
    }

    gfx::Resource<DescriptorSet> DescriptorSet::Builder::build() const
    {
        return materialize<DescriptorSet>(*this, "DescriptorSet");
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
