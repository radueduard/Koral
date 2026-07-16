//
// Created by radue on 14.06.2026.
//

/**
 * @file builder.h
 * @brief Common base for every Koral builder: diagnostics, input adoption, and materialization.
 *
 * A builder collects problems as it is configured (@ref Builder::addError from a setter) and
 * as it is built (@ref Builder::adopt on each resource input), then turns itself into a
 * @ref kor::Resource that is either valid or *poisoned* — carrying the reason it could not be
 * built rather than throwing.
 *
 * Builders are values, and @ref Builder::materialize captures a copy of the builder inside the
 * resource it produces. That copy is what a poisoned resource is later retried with: fix the
 * shader, and the pipeline built from it can rebuild itself from the very same configuration.
 */

#pragma once

#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "error.h"
#include "log.h"
#include "resource.h"
#include "stacktrace.h"

struct Builder {
    virtual ~Builder() = default;

    // Whether a resource built by this builder could ever be repaired at runtime, and therefore
    // whether it is worth keeping the builder around to retry with.
    //
    // Only shaders and the pipelines made from them say yes: their failures are fixed by editing a
    // file. A buffer or image that failed to allocate is not fixed by anything the user can type,
    // so retaining its builder would buy nothing — and cost a great deal, because those builders
    // own the resource's initial data (a whole vertex buffer, a whole texture). Retaining that for
    // the lifetime of the resource would silently double its host memory.
    static constexpr bool Recoverable = false;

    [[nodiscard]] bool hasErrors() const noexcept {
        for (const auto& d : _diagnostics) {
            if (d.severity == Severity::Error) return true;
        }
        return false;
    }

protected:
    enum class Severity { Warning, Error };

    struct Diagnostic {
        Severity severity;
        kor::ErrorCode code;
        std::string message;
        kor::Stacktrace trace;
        std::shared_ptr<const kor::Error> cause;  ///< The input error responsible, if any.
    };

    mutable std::vector<Diagnostic> _diagnostics{};

    // The resource inputs this attempt consumed, each paired with the generation it had when we
    // read it. A poisoned resource is only worth retrying once one of these has moved on, which
    // is what stops a broken pipeline from rebuilding itself every frame while you edit a shader.
    mutable std::vector<std::pair<std::weak_ptr<kor::ResourceStateBase>, std::uint64_t>> _deps{};

    void addError(const kor::ErrorCode code, std::string message,
                  std::shared_ptr<const kor::Error> cause = nullptr) const {
        _diagnostics.push_back({Severity::Error, code, std::move(message),
                                kor::Stacktrace::current(2), std::move(cause)});
    }

    void warn(std::string message) const {
        _diagnostics.push_back({Severity::Warning, kor::ErrorCode::eNone, std::move(message),
                                kor::Stacktrace::current(2), nullptr});
    }

    // Start a build attempt: forget everything the *previous attempt* discovered, keeping what the
    // setters recorded. Without this, a retried builder would accumulate a fresh copy of its
    // inputs' problems on every attempt.
    //
    // Setters record too, not just create(): a builder that stores a plain reference to its input
    // (Framebuffer's attachments, DescriptorSet's layout) has to inspect that input at the moment
    // it is handed over, because storing it means dereferencing it. Those findings are permanent —
    // the builder never saw a usable input, so there is nothing for a retry to recover.
    void beginAttempt() const {
        if (!_configWatermark) {
            _configWatermark = _diagnostics.size();
            _depWatermark = _deps.size();
        }
        _diagnostics.resize(*_configWatermark);
        _deps.resize(*_depWatermark);
    }

    // Consume a resource input. If the input is unusable then so are we, and its error becomes
    // the *cause* of ours — so the user is shown the shader that failed to compile, not merely
    // the pipeline that could not be assembled from it. The input's generation is recorded
    // either way, so that a later repair can be noticed.
    template<typename T>
    void adopt(const kor::ResourceRef<T>& input, const std::string_view what) const {
        if (!input.alive()) {
            addError(kor::ErrorCode::eInvalidArgument, std::format("{} has been destroyed.", what));
            return;
        }
        if (input.poisoned()) {
            const auto name = input.name();
            addError(input.error()->code,
                     std::format("{} '{}' is unusable.", what, name.empty() ? "<unnamed>" : name),
                     input.errorPtr());
        }
        _deps.emplace_back(input.state(), input.generation());
    }

    template<typename T>
    void adopt(const kor::Resource<T>& input, const std::string_view what) const {
        adopt(kor::ResourceRef<const T>(input), what);
    }

    // Logs warnings and returns the first error, with its cause chain attached. Errors are not
    // logged here: they end up poisoning the resource, and materialize() prints the complete
    // history in one piece rather than a line per level.
    [[nodiscard]] kor::VoidResult validate() const {
        std::optional<kor::Error> firstError;
        for (const auto& [severity, code, message, trace, cause] : _diagnostics) {
            if (severity == Severity::Error) {
                if (!firstError) {
                    firstError = kor::Error{
                        .code = code,
                        .message = std::format("{}\n{}", message, formatTrace(trace)),
                        .cause = cause,
                    };
                }
            } else {
                kor::log::warn("[builder] {}\n{}", message, formatTrace(trace));
            }
        }

        if (firstError) return std::unexpected(*firstError);
        return {};
    }

    /**
     * @brief Turn a builder into a Resource<T>: valid on success, poisoned on failure.
     *
     * @p SelfBuilder must expose `kor::Result<std::unique_ptr<T>> create() const`, which performs
     * one build attempt. A copy of the builder is stored inside the resource so that a poisoned
     * resource can be rebuilt later from the same configuration, once whatever broke it is fixed.
     *
     * Never throws and never returns nothing: a failure is a poisoned resource that keeps its
     * identity, so refs taken from it stay valid and come good if it is ever repaired.
     */
    template<typename T, typename SelfBuilder>
    [[nodiscard]] kor::Resource<T> materialize(const SelfBuilder& self, std::string name) const {
        kor::Resource<T> resource;

        if constexpr (SelfBuilder::Recoverable) {
            // Attempt on a copy, and keep it: a retry must be independent of the caller's builder
            // (usually a temporary), and the dependency generations recorded have to belong to the
            // builder that will actually be replayed. Safe to retain only because a recoverable
            // builder holds nothing but small, lifetime-tracked inputs (shader refs, pipeline state).
            auto attempt = std::make_shared<SelfBuilder>(self);
            auto created = attempt->create();

            resource = created
                ? kor::Resource<T>(std::move(*created), name)
                : kor::Resource<T>::failed(std::move(created.error()), name);

            for (const auto& [state, generation] : attempt->_deps) {
                resource.addDependency(state.lock(), generation);
            }
            resource.setRebuild([attempt] { return attempt->create(); });
            // create() re-adopts the builder's inputs on every run, so reading _deps back after an
            // attempt is what keeps the dependency list (and its generations) honest across repairs.
            resource.setDependencyProbe([attempt] { return attempt->_deps; });
        } else {
            // Build straight off the caller's builder and keep nothing. This resource cannot be
            // repaired at runtime, so a rebuild closure would never fire — and holding the builder
            // would pin its initial data (the buffer's contents, the image's pixels) in host memory
            // for as long as the resource lives.
            auto created = self.create();

            resource = created
                ? kor::Resource<T>(std::move(*created), name)
                : kor::Resource<T>::failed(std::move(created.error()), name);

            for (const auto& [state, generation] : self._deps) {
                resource.addDependency(state.lock(), generation);
            }
        }

        // Report the failure once, here, with its whole history — the symptom the caller hit and
        // the root cause they have to go and fix. Retries are gated on a dependency actually
        // changing, so this does not become per-frame spam.
        if (resource.poisoned()) {
            kor::log::error("Could not build {}:\n{}", name, resource.error()->history());
        }

        return resource;
    }

private:
    mutable std::optional<std::size_t> _configWatermark;  ///< #diagnostics produced by setters.
    mutable std::optional<std::size_t> _depWatermark;     ///< #dependencies adopted by setters.

    static std::string formatTrace(const kor::Stacktrace& trace) {
        std::string out;
        for (const auto& frame : trace) {
            out += std::format("    at {} ({}:{})\n",
                               frame.description(), frame.source_file(), frame.source_line());
            if (frame.description().find("main") != std::string::npos) break;
        }
        return out;
    }
};
