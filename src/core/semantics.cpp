//
// Created by radue on 29.07.2026.
//

#include "semantics.h"

#include <cstring>
#include <format>
#include <ranges>
#include <unordered_map>

#include "buffer.h"
#include "shader.h"

namespace kor
{
    // ---- SemanticSlot ---------------------------------------------------------------------------

    SemanticSlot::SemanticSlot(const std::string_view semantic, const std::string_view field,
                               const Scalar scalar, const std::uint8_t rows, const std::uint8_t columns,
                               const std::span<std::byte> destination)
        : _semantic(semantic), _field(field), _scalar(scalar), _rows(rows), _columns(columns),
          _destination(destination) {}

    std::string SemanticSlot::describe(const Scalar scalar, const std::uint8_t rows, const std::uint8_t columns)
    {
        const char* base = "?";
        switch (scalar) {
        case Scalar::eFloat:  base = "float"; break;
        case Scalar::eInt:    base = "int"; break;
        case Scalar::eUInt:   base = "uint"; break;
        case Scalar::eBool:   base = "bool"; break;
        case Scalar::eDouble: base = "double"; break;
        case Scalar::eOther:  base = "<unsupported>"; break;
        }
        if (columns > 1) return std::format("{}{}x{}", base, rows, columns);
        if (rows > 1)    return std::format("{}{}", base, rows);
        return base;
    }

    bool SemanticSlot::write(const Scalar scalar, const std::uint8_t rows, const std::uint8_t columns,
                             const void* bytes, const std::size_t size)
    {
        if (scalar != _scalar || rows != _rows || columns != _columns) {
            // Reported rather than silently skipped: a semantic on the wrong type is a mistake in
            // the shader, and one the author can only fix if they are told which field.
            _error = Error{
                .code = ErrorCode::eInvalidArgument,
                .message = std::format(
                    "'{}' is declared as {} but {} fills a {}.",
                    _field, describe(_scalar, _rows, _columns), _semantic, describe(scalar, rows, columns)),
            };
            return false;
        }

        // The reflected size is what the block actually reserves, which for a matrix includes the
        // padding between columns. Refusing rather than truncating: a short copy would leave the
        // tail of the field holding whatever was there before, which is far harder to see than an
        // error.
        if (size > _destination.size()) {
            _error = Error{
                .code = ErrorCode::eInvalidArgument,
                .message = std::format("'{}' reserves {} bytes but {} needs {}.",
                                       _field, _destination.size(), _semantic, size),
            };
            return false;
        }

        std::memcpy(_destination.data(), bytes, size);
        _written = true;
        _error.reset();
        return true;
    }

    void SemanticSlot::set(const float value)          { write(Scalar::eFloat, 1, 1, &value, sizeof(value)); }
    void SemanticSlot::set(const std::int32_t value)   { write(Scalar::eInt, 1, 1, &value, sizeof(value)); }
    void SemanticSlot::set(const std::uint32_t value)  { write(Scalar::eUInt, 1, 1, &value, sizeof(value)); }
    void SemanticSlot::set(const glm::vec2& value)     { write(Scalar::eFloat, 2, 1, &value, sizeof(value)); }
    void SemanticSlot::set(const glm::vec3& value)     { write(Scalar::eFloat, 3, 1, &value, sizeof(value)); }
    void SemanticSlot::set(const glm::vec4& value)     { write(Scalar::eFloat, 4, 1, &value, sizeof(value)); }
    void SemanticSlot::set(const glm::mat4& value)     { write(Scalar::eFloat, 4, 4, &value, sizeof(value)); }

    void SemanticSlot::set(const glm::mat3& value)
    {
        // std140 pads each column of a mat3 out to 16 bytes, so the tight glm::mat3 cannot be
        // copied straight in. Expanded here rather than made the caller's problem.
        if (_scalar != Scalar::eFloat || _rows != 3 || _columns != 3)
        {
            write(Scalar::eFloat, 3, 3, &value, sizeof(value));   // reports the mismatch
            return;
        }

        alignas(16) float padded[12] {};
        for (int column = 0; column < 3; ++column) {
            for (int row = 0; row < 3; ++row) padded[column * 4 + row] = value[column][row];
        }
        write(Scalar::eFloat, 3, 3, padded, sizeof(padded));
    }

    // ---- SemanticBuffers ------------------------------------------------------------------------

    /**
     * @brief One block shape this object has been asked to fill, and the buffer holding it.
     *
     * Keyed by the shape rather than by the shader: two shaders that declare the same block get
     * one buffer between them, and one shader that declares two different blocks gets two.
     */
    struct SemanticBuffers::Block
    {
        std::vector<Shader::BlockMember> members;
        glm::u32 size = 0;
        Resource<Buffer> buffer;
        std::vector<std::byte> staging;     ///< Assembled here, then compared before uploading.
        std::optional<Error> error;         ///< A mismatch, which poisons whatever binds it.
    };

    struct SemanticBuffers::State
    {
        std::unordered_map<std::size_t, Block> blocks;   ///< Keyed by the shape's hash.
    };

    SemanticBuffers::SemanticBuffers() : _state(std::make_unique<State>()) {}
    SemanticBuffers::~SemanticBuffers() = default;

    std::size_t SemanticBuffers::blockCount() const { return _state->blocks.size(); }

    namespace
    {
        /**
         * @brief A block's shape, as a number.
         *
         * Everything that decides how the block is filled: where each field sits, how big it is,
         * what type it is and what it was annotated with. Two shaders that declare the same block
         * hash the same and share one buffer; renaming a field changes nothing, since the name is
         * only ever used to report a mistake.
         */
        std::size_t shapeOf(const std::vector<Shader::BlockMember>& members, const glm::u32 blockSize)
        {
            std::size_t hash = std::hash<glm::u32>{}(blockSize);
            const auto mix = [&hash](const std::size_t value) {
                hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
            };
            for (const auto& member : members) {
                mix(std::hash<std::string>{}(member.semanticNamespace));
                mix(std::hash<std::string>{}(member.semantic));
                mix(member.offset);
                mix(member.size);
                mix(static_cast<std::size_t>(member.scalar) << 16 |
                    static_cast<std::size_t>(member.rows) << 8 | member.columns);
            }
            return hash;
        }
    }

    Result<ResourceRef<const Buffer>> SemanticBuffers::acquire(
        const std::vector<Shader::BlockMember>& members, const std::uint32_t blockSize,
        const SemanticSerializer& owner)
    {
        const std::size_t shape = shapeOf(members, blockSize);

        if (const auto existing = _state->blocks.find(shape); existing != _state->blocks.end()) {
            if (existing->second.error) return std::unexpected(*existing->second.error);
            return ResourceRef<const Buffer>(existing->second.buffer);
        }

        Block block;
        block.members = members;
        block.size = blockSize;
        block.staging.resize(blockSize, std::byte{});

        // Fill it once before deciding anything, so a mismatch is reported by the write that asked
        // for it rather than one frame later, and so the buffer is correct the moment it is bound.
        for (const auto& member : block.members) {
            if (member.semantic.empty()) continue;   // undecorated: the shader's own business

            // The annotation names the module that should fill it, so writing the wrong kind of
            // object here is caught by name rather than leaving every field silently at zero.
            if (member.semanticNamespace != owner.semanticNamespace()) {
                block.error = Error{
                    .code = ErrorCode::eInvalidArgument,
                    .message = std::format(
                        "'{}' asks for {}({}), but a '{}' was written to this binding.",
                        member.name, member.semanticNamespace, member.semantic,
                        owner.semanticNamespace()),
                };
                break;
            }

            SemanticSlot slot(member.semantic, member.name,
                              static_cast<SemanticSlot::Scalar>(member.scalar),
                              member.rows, member.columns,
                              std::span(block.staging).subspan(member.offset, member.size));

            if (!owner.serialize(member.semantic, slot)) {
                block.error = Error{
                    .code = ErrorCode::eInvalidArgument,
                    .message = std::format(
                        "'{}' does not answer for {}({}), which '{}' is annotated with — the "
                        "semantic is misspelled, or that module does not provide it.",
                        owner.semanticNamespace(), member.semanticNamespace, member.semantic,
                        member.name),
                };
                break;
            }
            if (slot.error()) { block.error = slot.error(); break; }
        }

        if (block.error) {
            const Error error = *block.error;
            _state->blocks.emplace(shape, std::move(block));   // remembered, so it is reported once
            return std::unexpected(error);
        }

        block.buffer = Buffer::RawBuilder()
            .setRawSize(static_cast<glm::i64>(blockSize))
            .setUsage(Buffer::Usage::eUniform)
            .setIsPerFrame(true)
            .setType(Buffer::Type::eDynamic)
            .build();

        if (!block.buffer.valid()) {
            return std::unexpected(block.buffer.error()
                ? *block.buffer.error()
                : Error{ .code = ErrorCode::eInvalidArgument, .message = "Could not allocate the block." });
        }

        block.buffer->Write(block.staging, 0);
        const auto [it, inserted] = _state->blocks.emplace(shape, std::move(block));
        return ResourceRef<const Buffer>(it->second.buffer);
    }

    void SemanticBuffers::refresh(const SemanticSerializer& owner)
    {
        for (auto& block : _state->blocks | std::views::values) {
            if (!block.buffer.valid() || block.error) continue;

            std::vector<std::byte> next(block.staging.size(), std::byte{});
            for (const auto& member : block.members) {
                if (member.semantic.empty()) continue;

                SemanticSlot slot(member.semantic, member.name,
                                  static_cast<SemanticSlot::Scalar>(member.scalar),
                                  member.rows, member.columns,
                                  std::span(next).subspan(member.offset, member.size));
                owner.serialize(member.semantic, slot);
            }

            // Only when something moved. A camera that has not been touched since the last frame
            // costs a compare rather than a write into memory the GPU may be about to read.
            if (next != block.staging) {
                block.staging = std::move(next);
                block.buffer->Write(block.staging, 0);
            }
        }
    }
}
