//
// Created by radue on 28.06.2026.
//

/**
 * @file error.h
 * @brief Structured, functional error handling for the Koral API.
 *
 * The public API reports failures as values, not exceptions: builders return
 * @ref kor::Result and the command buffer accumulates @ref kor::Error using a
 * railway model. Every error carries a documented @ref kor::ErrorCode so users
 * get an actionable Koral error instead of a raw backend (Vulkan) one.
 *
 * Backends still throw internally; @ref kor::guard converts any escaping
 * exception into a kor::Error at the API boundary.
 */

#pragma once

#include <cstddef>
#include <expected>
#include <format>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "api.h"

namespace kor
{
    /**
     * @brief Documented error codes returned across the Koral API.
     *
     * Each code maps to a stable one-line description via @ref describe, which is
     * also the source of truth for docs/errors.md. Codes are grouped by domain.
     */
    enum class ErrorCode
    {
        eNone = 0,             ///< No error (success sentinel).

        // --- generic ---
        eUnknownApi,           ///< The active graphics API is not recognised/supported.
        eBackend,              ///< A backend (Vulkan/OpenGL) operation failed; see message.
        eInvalidArgument,      ///< A builder/command argument was invalid.

        // --- buffer ---
        eUniformBufferTooLarge,   ///< A uniform buffer exceeds the 65536-byte maximum.
        eBufferSizeInvalid,       ///< A buffer size or instance count was not strictly positive.
        eBufferRangeOutOfBounds,  ///< A read/write/map range falls outside the buffer.
        eBufferAlreadyMapped,     ///< The buffer is already mapped in a conflicting mode.

        // --- command buffer ---
        eNoGraphicsPipelineBound,    ///< A graphics command was recorded with no graphics pipeline bound.
        eNoComputePipelineBound,     ///< A compute command was recorded with no compute pipeline bound.
        eNoRayTracingPipelineBound,  ///< A ray-tracing command was recorded with no ray-tracing pipeline bound.
        eNoMeshBound,                ///< An indexed/mesh draw was recorded with no mesh bound.
        eMeshHasNoIndexBuffer,       ///< An indexed draw was recorded for a mesh without an index buffer.
        eCopySizeExceedsBuffer,      ///< A copy/clear/fill range exceeds the target buffer size.
        eImageSubresourceOutOfRange, ///< A copy referenced a mip level / array layer outside the image.
        eResolveRequiresMultisample, ///< Resolve requires a multisampled source and single-sampled destination.
        eRayTracingUnsupported,      ///< Ray tracing is not supported on this backend.

        // --- pipeline / shader (populated in later phases) ---
        eMissingShaderStage,   ///< A pipeline is missing a required shader stage.
        eShaderStageMismatch,  ///< A shader was supplied for the wrong pipeline stage.
        eDescriptorConflict,   ///< Descriptor declarations conflict across merged shader stages.
        eShaderCompileFailed,  ///< Shader compilation/linking failed.

        // --- configuration ---
        eConfigInvalid,        ///< A koral.json config file is malformed or has a key of the wrong type.
    };

    /** @brief Stable, human-readable one-line description of an error code. */
    [[nodiscard]] KORAL_API std::string_view describe(ErrorCode code);

    /**
     * @brief A structured API error: a documented code, a human message, an origin,
     *        and optionally the error that caused it.
     *
     * Errors form a chain. When a resource is built from an input that is itself
     * unusable (a pipeline whose shader failed to compile), the resulting error links
     * the input's error as its @ref cause. Walking that chain with @ref history yields
     * the full story, from the symptom the user hit down to the root cause they must fix.
     *
     * The cause is shared, not owned: one broken shader typically poisons several
     * pipelines, and they all point at the same root error rather than copying it.
     */
    struct KORAL_API Error
    {
        ErrorCode code = ErrorCode::eNone;
        std::string message;
        std::source_location where = std::source_location::current();
        std::shared_ptr<const Error> cause;  ///< The error that made our input unusable, if any.

        /** @brief Format this error alone as "kor::Error(code): message [file:line]". */
        [[nodiscard]] std::string toString() const;

        /**
         * @brief Format this error and every error beneath it, one "caused by" per line.
         *
         * This is what gets printed to the console for an unrecoverable failure: it
         * explains the history of the issue so the user knows what to actually fix.
         */
        [[nodiscard]] std::string history() const;

        /** @brief Depth of the cause chain (0 when this error has no cause). */
        [[nodiscard]] std::size_t depth() const;

        /** @brief The deepest error in the chain: the thing the user has to fix. */
        [[nodiscard]] const Error& root() const;
    };

    /** @brief Copy @p e with @p cause linked beneath it. */
    [[nodiscard]] KORAL_API Error causedBy(Error e, std::shared_ptr<const Error> cause);

    /**
     * @brief Exception carrying a kor::Error, thrown by backends and unwrapped at the API boundary.
     *
     * Backend code that can identify an actionable cause throws this so the specific
     * kor::Error survives; @ref guard converts everything else into ErrorCode::eBackend.
     * It is also what @ref Result::valueOrThrow re-throws.
     */
    struct KORAL_API BackendException : std::runtime_error
    {
        Error error;
        explicit BackendException(Error e)
            : std::runtime_error(e.message), error(std::move(e)) {}
    };

    /**
     * @brief Result of a fallible API call: a value on success, a kor::Error on failure.
     *
     * A thin extension of std::expected: all of its operations remain available
     * (operator bool, value(), error(), and_then/or_else/transform). It adds
     * @ref valueOrThrow for call sites that prefer to bridge a failure back into an
     * exception while keeping the fluent `builder.build()` style.
     */
    template<class T>
    struct Result : std::expected<T, Error>
    {
        using std::expected<T, Error>::expected;

        /** @brief Return the value, or throw BackendException(error) on failure. */
        T valueOrThrow() && {
            if (!this->has_value()) throw BackendException(std::move(this->error()));
            return std::move(this->value());
        }

        /** @brief Return the value, or throw BackendException(error) on failure. */
        T valueOrThrow() const & {
            if (!this->has_value()) throw BackendException(this->error());
            return this->value();
        }
    };

    /** @brief Result of a fallible API call with no value. */
    using VoidResult = std::expected<void, Error>;

    /**
     * @brief Build an `unexpected` kor::Error with a formatted message in one line.
     *
     * Usage: `return fail(ErrorCode::eUniformBufferTooLarge, "size {} > 65536", n);`
     */
    template<class... A>
    [[nodiscard]] std::unexpected<Error> fail(const ErrorCode code, const std::format_string<A...> fmt, A&&... a)
    {
        return std::unexpected(Error{ .code = code, .message = std::format(fmt, std::forward<A>(a)...) });
    }

    /** @brief Build an `unexpected` kor::Error using the code's standard description. */
    [[nodiscard]] inline std::unexpected<Error> fail(const ErrorCode code)
    {
        return std::unexpected(Error{ .code = code, .message = std::string(describe(code)) });
    }

    /**
     * @brief Build an `unexpected` kor::Error that links @p cause beneath it.
     *
     * Used when a failure is a consequence of an unusable input rather than an
     * independent problem, so the user is shown the root cause and not just the symptom.
     */
    template<class... A>
    [[nodiscard]] std::unexpected<Error> failCausedBy(const ErrorCode code, std::shared_ptr<const Error> cause,
                                                      const std::format_string<A...> fmt, A&&... a)
    {
        return std::unexpected(Error{
            .code = code,
            .message = std::format(fmt, std::forward<A>(a)...),
            .cause = std::move(cause),
        });
    }

    /**
     * @brief Run @p f, converting any escaping exception into a kor::Error.
     *
     * A thrown @ref BackendException keeps its embedded Error; any other
     * std::exception collapses to @p fallback with the original `what()` message.
     * Guarantees no backend exception crosses the API boundary.
     */
    template<class F>
    [[nodiscard]] auto guard(const ErrorCode fallback, F&& f) -> Result<std::invoke_result_t<F>>
    {
        using R = std::invoke_result_t<F>;
        try {
            if constexpr (std::is_void_v<R>) {
                std::forward<F>(f)();
                return Result<void>{};
            } else {
                return std::forward<F>(f)();
            }
        } catch (const BackendException& e) {
            return std::unexpected(e.error);
        } catch (const std::exception& e) {
            return std::unexpected(Error{ .code = fallback, .message = e.what() });
        }
    }
}
