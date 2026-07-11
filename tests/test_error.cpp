// Unit tests for the functional error model in error.h / error.cpp:
// ErrorCode -> describe(), Error::toString(), fail(), guard(), Result.

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

#include <magic_enum/magic_enum.hpp>

#include "error.h"

using namespace gfx;

namespace {

// -----------------------------------------------------------------------------
// describe(): every code must have a real, non-fallback description.
// This catches the common regression of adding an ErrorCode but forgetting to
// extend the describe() switch (which would silently fall through to the
// "Unknown error." default).
// -----------------------------------------------------------------------------
TEST(Error, EveryCodeHasADescription) {
    for (const ErrorCode code : magic_enum::enum_values<ErrorCode>()) {
        const std::string_view d = describe(code);
        EXPECT_FALSE(d.empty()) << "empty description for " << magic_enum::enum_name(code);
        EXPECT_NE(d, "Unknown error.")
            << "missing describe() case for " << magic_enum::enum_name(code);
    }
}

TEST(Error, DescribeReturnsStableText) {
    EXPECT_EQ(describe(ErrorCode::eNone), "No error.");
    EXPECT_EQ(describe(ErrorCode::eRayTracingUnsupported),
              "Ray tracing is not supported on the active backend.");
}

// -----------------------------------------------------------------------------
// Error::toString()
// -----------------------------------------------------------------------------
TEST(Error, ToStringContainsCodeNameMessageAndLocation) {
    Error e{.code = ErrorCode::eUniformBufferTooLarge, .message = "size 99999 > 65536"};
    const std::string s = e.toString();
    EXPECT_NE(s.find("eUniformBufferTooLarge"), std::string::npos) << s;
    EXPECT_NE(s.find("size 99999 > 65536"), std::string::npos) << s;
    EXPECT_NE(s.find("test_error.cpp"), std::string::npos) << s; // default source_location
}

TEST(Error, ToStringDistinguishesCodes) {
    const std::string a = Error{.code = ErrorCode::eBackend, .message = "x"}.toString();
    const std::string b = Error{.code = ErrorCode::eNoMeshBound, .message = "x"}.toString();
    EXPECT_NE(a, b);
    EXPECT_NE(a.find("eBackend"), std::string::npos);
    EXPECT_NE(b.find("eNoMeshBound"), std::string::npos);
}

// -----------------------------------------------------------------------------
// fail()
// -----------------------------------------------------------------------------
TEST(Error, FailWithFormattedMessage) {
    Result<int> r = fail(ErrorCode::eBufferSizeInvalid, "count {} not > 0", 0);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::eBufferSizeInvalid);
    EXPECT_EQ(r.error().message, "count 0 not > 0");
}

TEST(Error, FailWithDefaultDescription) {
    Result<int> r = fail(ErrorCode::eNoComputePipelineBound);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::eNoComputePipelineBound);
    EXPECT_EQ(r.error().message, std::string(describe(ErrorCode::eNoComputePipelineBound)));
}

// -----------------------------------------------------------------------------
// Result::valueOrThrow()
// -----------------------------------------------------------------------------
TEST(Error, ValueOrThrowReturnsValueOnSuccess) {
    Result<int> r = 7;
    EXPECT_EQ(r.valueOrThrow(), 7);
}

TEST(Error, ValueOrThrowThrowsBackendExceptionOnFailure) {
    Result<int> r = fail(ErrorCode::eShaderCompileFailed, "nope");
    try {
        (void)r.valueOrThrow();
        FAIL() << "expected BackendException";
    } catch (const BackendException& ex) {
        EXPECT_EQ(ex.error.code, ErrorCode::eShaderCompileFailed);
        EXPECT_EQ(ex.error.message, "nope");
    }
}

// -----------------------------------------------------------------------------
// guard()
// -----------------------------------------------------------------------------
TEST(Error, GuardReturnsValueWhenNoThrow) {
    Result<int> r = guard(ErrorCode::eBackend, [] { return 42; });
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 42);
}

TEST(Error, GuardPreservesBackendExceptionError) {
    Result<int> r = guard(ErrorCode::eBackend, []() -> int {
        throw BackendException(Error{.code = ErrorCode::eUniformBufferTooLarge,
                                     .message = "specific"});
    });
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::eUniformBufferTooLarge); // not the fallback
    EXPECT_EQ(r.error().message, "specific");
}

TEST(Error, GuardConvertsGenericExceptionToFallback) {
    Result<int> r = guard(ErrorCode::eBackend, []() -> int {
        throw std::runtime_error("kaboom");
    });
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::eBackend);
    EXPECT_EQ(r.error().message, "kaboom");
}

TEST(Error, GuardWorksForVoidReturn) {
    bool ran = false;
    Result<void> r = guard(ErrorCode::eBackend, [&] { ran = true; });
    EXPECT_TRUE(ran);
    EXPECT_TRUE(r.has_value());
}

TEST(Error, GuardVoidPropagatesFailure) {
    Result<void> r = guard(ErrorCode::eBackend, [] { throw std::runtime_error("boom"); });
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::eBackend);
    EXPECT_EQ(r.error().message, "boom");
}

TEST(Error, GuardVoidPreservesBackendException) {
    Result<void> r = guard(ErrorCode::eBackend, [] {
        throw BackendException(Error{.code = ErrorCode::eNoMeshBound, .message = "m"});
    });
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::eNoMeshBound);
}

// -----------------------------------------------------------------------------
// VoidResult
// -----------------------------------------------------------------------------
TEST(Error, VoidResultSuccessAndFailure) {
    VoidResult ok{};
    EXPECT_TRUE(ok.has_value());

    VoidResult bad = fail(ErrorCode::eInvalidArgument, "bad");
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, ErrorCode::eInvalidArgument);
}

// -----------------------------------------------------------------------------
// Cause chains. An error raised because an *input* was unusable links that input's
// error beneath it, so the user is shown the thing they have to fix rather than
// the symptom furthest from it.
// -----------------------------------------------------------------------------
TEST(Error, NoCauseByDefault) {
    const Error e{ .code = ErrorCode::eBackend, .message = "boom" };
    EXPECT_EQ(e.cause, nullptr);
    EXPECT_EQ(e.depth(), 0u);
    EXPECT_EQ(e.root().code, ErrorCode::eBackend);
}

TEST(Error, CausedByLinksAndReportsRoot) {
    auto compile = std::make_shared<const Error>(
        Error{ .code = ErrorCode::eShaderCompileFailed, .message = "undeclared identifier 'colour'" });

    const Error pipeline = causedBy(
        Error{ .code = ErrorCode::eMissingShaderStage, .message = "pipeline 'forward' is unusable" },
        compile);

    EXPECT_EQ(pipeline.depth(), 1u);
    ASSERT_NE(pipeline.cause, nullptr);
    EXPECT_EQ(pipeline.cause->code, ErrorCode::eShaderCompileFailed);

    // root() is what the user must actually go and fix.
    EXPECT_EQ(pipeline.root().code, ErrorCode::eShaderCompileFailed);
}

TEST(Error, HistoryWalksTheWholeChain) {
    auto compile = std::make_shared<const Error>(
        Error{ .code = ErrorCode::eShaderCompileFailed, .message = "undeclared identifier 'colour'" });
    auto pipeline = std::make_shared<const Error>(causedBy(
        Error{ .code = ErrorCode::eMissingShaderStage, .message = "pipeline 'forward' is unusable" },
        compile));
    const Error recording = causedBy(
        Error{ .code = ErrorCode::eNoGraphicsPipelineBound, .message = "cannot bind pipeline" },
        pipeline);

    EXPECT_EQ(recording.depth(), 2u);

    const std::string h = recording.history();
    // Symptom first, then each cause beneath it, deepest last.
    EXPECT_NE(h.find("cannot bind pipeline"), std::string::npos);
    EXPECT_NE(h.find("pipeline 'forward' is unusable"), std::string::npos);
    EXPECT_NE(h.find("undeclared identifier 'colour'"), std::string::npos);
    EXPECT_EQ(std::ranges::count(h, '\n'), 2);   // one line per level

    // The root cause is reported last, i.e. deepest in the printout.
    EXPECT_GT(h.find("undeclared identifier 'colour'"), h.find("cannot bind pipeline"));
}

TEST(Error, FailCausedByBuildsAnUnexpectedWithACause) {
    auto root = std::make_shared<const Error>(
        Error{ .code = ErrorCode::eShaderCompileFailed, .message = "syntax error" });

    const Result<int> r = failCausedBy(ErrorCode::eMissingShaderStage, root, "pipeline '{}' is unusable", "forward");

    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::eMissingShaderStage);
    EXPECT_EQ(r.error().message, "pipeline 'forward' is unusable");
    EXPECT_EQ(r.error().root().code, ErrorCode::eShaderCompileFailed);
}

// A single root cause is shared by every error that derives from it, rather than
// copied — one broken shader typically poisons several pipelines.
TEST(Error, CauseIsSharedNotCopied) {
    auto root = std::make_shared<const Error>(
        Error{ .code = ErrorCode::eShaderCompileFailed, .message = "syntax error" });

    const Error a = causedBy(Error{ .code = ErrorCode::eBackend, .message = "pipeline A" }, root);
    const Error b = causedBy(Error{ .code = ErrorCode::eBackend, .message = "pipeline B" }, root);

    EXPECT_EQ(a.cause.get(), b.cause.get());
    EXPECT_EQ(&a.root(), &b.root());
}

} // namespace
