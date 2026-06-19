//
// Created by radue on 14.06.2026.
//

#pragma once

#include <string>
#include <vector>
#include <format>
#include <stdexcept>
#include <stacktrace>

#include "log.h"

struct Builder {
    virtual ~Builder() = default;

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
        std::string message;
        std::stacktrace trace;
    };

    mutable std::vector<Diagnostic> _diagnostics{};

    void fail(std::string message) const {
        _diagnostics.push_back({Severity::Error, std::move(message), std::stacktrace::current(2)});
    }

    void warn(std::string message) const {
        _diagnostics.push_back({Severity::Warning, std::move(message), std::stacktrace::current(2)});
    }

    void flushDiagnostics() const {
        std::string errorSummary;
        for (const auto& [severity, message, trace] : _diagnostics) {
            const std::string where = formatTrace(trace);
            if (severity == Severity::Error) {
                gfx::log::error("[builder] {}\n{}", message, where);
                if (!errorSummary.empty()) errorSummary += "; ";
                errorSummary += message;
            } else {
                gfx::log::warn("[builder] {}\n{}", message, where);
            }
        }

        if (!errorSummary.empty()) {
            throw std::runtime_error("Builder failed: " + errorSummary);
        }
    }

private:
    static std::string formatTrace(const std::stacktrace& trace) {
        std::string out;
        for (const auto& frame : trace) {
            out += std::format("    at {} ({}:{})\n",
                               frame.description(), frame.source_file(), frame.source_line());
            if (frame.description().find("main") != std::string::npos) break;
        }
        return out;
    }
};