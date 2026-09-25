// Unit tests for the functional error model in error.h / error.cpp:
// ErrorCode -> Describe(), Error::ToString(), Fail(), Guard(), Result.

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

#include <magic_enum/magic_enum.hpp>

#include "error.h"

using namespace kor;

namespace {

// -----------------------------------------------------------------------------
// Describe(): every code must have a real, non-fallback description.
// This catches the common regression of adding an ErrorCode but forgetting to
// extend the Describe() switch (which would silently fall through to the
// "Unknown error." default).
// -----------------------------------------------------------------------------
TEST(Error, EveryCodeHasADescription) {
    for (const ErrorCode code : magic_enum::enum_values<ErrorCode>()) {
        const std::string_view d = Describe(code);
        EXPECT_FALSE(d.empty()) << "empty description for " << magic_enum::enum_name(code);
        EXPECT_NE(d, "Unknown error.")
            << "missing describe() case for " << magic_enum::enum_name(code);
    }
}

TEST(Error, DescribeReturnsStableText) {
    EXPECT_EQ(Describe(ErrorCode::eNone), "No error.");
    EXPECT_EQ(Describe(ErrorCode::eRayTracingUnsupported),
              "Ray tracing is not supported on the active backend.");
}

// -----------------------------------------------------------------------------
// Error::ToString()
// -----------------------------------------------------------------------------
TEST(Error, ToStringContainsCodeNameMessageAndLocation) {
    Error e{.code = ErrorCode::eUniformBufferTooLarge, .message = "size 99999 > 65536"};
    const std::string s = e.ToString();
    EXPECT_NE(s.find("eUniformBufferTooLarge"), std::string::npos) << s;
    EXPECT_NE(s.find("size 99999 > 65536"), std::string::npos) << s;
    EXPECT_NE(s.find("test_error.cpp"), std::string::npos) << s; // default source_location
}

TEST(Error, ToStringDistinguishesCodes) {
    const std::string a = Error{.code = ErrorCode::eBackend, .message = "x"}.ToString();
    const std::string b = Error{.code = ErrorCode::eNoMeshBound, .message = "x"}.ToString();
    EXPECT_NE(a, b);
    EXPECT_NE(a.find("eBackend"), std::string::npos);
    EXPECT_NE(b.find("eNoMeshBound"), std::string::npos);
}

// -----------------------------------------------------------------------------
// Fail()
// -----------------------------------------------------------------------------
TEST(Error, FailWithFormattedMessage) {
    Result<int> r = Fail(ErrorCode::eBufferSizeInvalid, "count {} not > 0", 0);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::eBufferSizeInvalid);
    EXPECT_EQ(r.error().message, "count 0 not > 0");
}

TEST(Error, FailWithDefaultDescription) {
    Result<int> r = Fail(ErrorCode::eNoComputePipelineBound);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::eNoComputePipelineBound);
    EXPECT_EQ(r.error().message, std::string(Describe(ErrorCode::eNoComputePipelineBound)));
}

// -----------------------------------------------------------------------------
// Result::ValueOrThrow()
// -----------------------------------------------------------------------------
TEST(Error, ValueOrThrowReturnsValueOnSuccess) {
    Result<int> r = 7;
    EXPECT_EQ(r.ValueOrThrow(), 7);
}

TEST(Error, ValueOrThrowThrowsBackendExceptionOnFailure) {
    Result<int> r = Fail(ErrorCode::eShaderCompileFailed, "nope");
    try {
        (void)r.ValueOrThrow();
        FAIL() << "expected BackendException";
    } catch (const BackendException& ex) {
        EXPECT_EQ(ex.error.code, ErrorCode::eShaderCompileFailed);
        EXPECT_EQ(ex.error.message, "nope");
    }
}

// -----------------------------------------------------------------------------
// Guard()
// -----------------------------------------------------------------------------
TEST(Error, GuardReturnsValueWhenNoThrow) {
    Result<int> r = Guard(ErrorCode::eBackend, [] { return 42; });
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 42);
}

TEST(Error, GuardPreservesBackendExceptionError) {
    Result<int> r = Guard(ErrorCode::eBackend, []() -> int {
        throw BackendException(Error{.code = ErrorCode::eUniformBufferTooLarge,
                                     .message = "specific"});
    });
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::eUniformBufferTooLarge); // not the fallback
    EXPECT_EQ(r.error().message, "specific");
}

TEST(Error, GuardConvertsGenericExceptionToFallback) {
    Result<int> r = Guard(ErrorCode::eBackend, []() -> int {
        throw std::runtime_error("kaboom");
    });
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::eBackend);
    EXPECT_EQ(r.error().message, "kaboom");
}

TEST(Error, GuardWorksForVoidReturn) {
    bool ran = false;
    Result<void> r = Guard(ErrorCode::eBackend, [&] { ran = true; });
    EXPECT_TRUE(ran);
    EXPECT_TRUE(r.has_value());
}

TEST(Error, GuardVoidPropagatesFailure) {
    Result<void> r = Guard(ErrorCode::eBackend, [] { throw std::runtime_error("boom"); });
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::eBackend);
    EXPECT_EQ(r.error().message, "boom");
}

TEST(Error, GuardVoidPreservesBackendException) {
    Result<void> r = Guard(ErrorCode::eBackend, [] {
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

    VoidResult bad = Fail(ErrorCode::eInvalidArgument, "bad");
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
    EXPECT_EQ(e.Depth(), 0u);
    EXPECT_EQ(e.Root().code, ErrorCode::eBackend);
}

TEST(Error, CausedByLinksAndReportsRoot) {
    auto compile = std::make_shared<const Error>(
        Error{ .code = ErrorCode::eShaderCompileFailed, .message = "undeclared identifier 'colour'" });

    const Error pipeline = CausedBy(
        Error{ .code = ErrorCode::eMissingShaderStage, .message = "pipeline 'forward' is unusable" },
        compile);

    EXPECT_EQ(pipeline.Depth(), 1u);
    ASSERT_NE(pipeline.cause, nullptr);
    EXPECT_EQ(pipeline.cause->code, ErrorCode::eShaderCompileFailed);

    // Root() is what the user must actually go and fix.
    EXPECT_EQ(pipeline.Root().code, ErrorCode::eShaderCompileFailed);
}

TEST(Error, HistoryWalksTheWholeChain) {
    auto compile = std::make_shared<const Error>(
        Error{ .code = ErrorCode::eShaderCompileFailed, .message = "undeclared identifier 'colour'" });
    auto pipeline = std::make_shared<const Error>(CausedBy(
        Error{ .code = ErrorCode::eMissingShaderStage, .message = "pipeline 'forward' is unusable" },
        compile));
    const Error recording = CausedBy(
        Error{ .code = ErrorCode::eNoGraphicsPipelineBound, .message = "cannot bind pipeline" },
        pipeline);

    EXPECT_EQ(recording.Depth(), 2u);

    const std::string h = recording.History();
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

    const Result<int> r = FailCausedBy(ErrorCode::eMissingShaderStage, root, "pipeline '{}' is unusable", "forward");

    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::eMissingShaderStage);
    EXPECT_EQ(r.error().message, "pipeline 'forward' is unusable");
    EXPECT_EQ(r.error().Root().code, ErrorCode::eShaderCompileFailed);
}

// A single root cause is shared by every error that derives from it, rather than
// copied — one broken shader typically poisons several pipelines.
TEST(Error, CauseIsSharedNotCopied) {
    auto root = std::make_shared<const Error>(
        Error{ .code = ErrorCode::eShaderCompileFailed, .message = "syntax error" });

    const Error a = CausedBy(Error{ .code = ErrorCode::eBackend, .message = "pipeline A" }, root);
    const Error b = CausedBy(Error{ .code = ErrorCode::eBackend, .message = "pipeline B" }, root);

    EXPECT_EQ(a.cause.get(), b.cause.get());
    EXPECT_EQ(&a.Root(), &b.Root());
}

} // namespace
