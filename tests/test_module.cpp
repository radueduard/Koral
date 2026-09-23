//
// Tests for the module system's CPU-side behaviour: name resolution failures, the registry when
// nothing is loaded, and the shape of the errors. Loading a real module needs a built library and
// a device, which is the examples' job — everything here is hermetic.
//

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "module.h"

using kor::ErrorCode;
using kor::ModuleHost;

TEST(Module, NothingRequestedIsSuccess)
{
    // The overwhelmingly common case — a project with no "modules" key — must not even look at
    // the search directories, let alone fail because none exist.
    EXPECT_TRUE(ModuleHost::Load({}, {}));
}

TEST(Module, UnknownModuleFailsAndNamesIt)
{
    const std::vector<std::string> requested { "does-not-exist" };
    const std::vector<std::filesystem::path> directories { "/nowhere/at/all" };

    const auto result = ModuleHost::Load(requested, directories);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::eModuleLoadFailed);
    // The error must name the module and where it was looked for — "module not found" with
    // neither is a support ticket, not a diagnostic.
    EXPECT_NE(result.error().message.find("does-not-exist"), std::string::npos);
    EXPECT_NE(result.error().message.find("/nowhere/at/all"), std::string::npos);
}

TEST(Module, ALibraryWithoutTheEntryPointsIsNotAModule)
{
    // Any real shared library that is not a Koral module will do; libKoral itself is guaranteed
    // to be loadable (we are linked against it) and guaranteed not to export korCreateModule.
    // Locating it through the loader keeps the test independent of the build layout.
    const auto self = []() -> std::filesystem::path {
        // koral_tests links Koral, so its build directory contains or links to it; walk the
        // usual candidates rather than assume one.
        for (const char* name : { "libKoral.so", "libKoral.dylib", "Koral.dll" }) {
            std::error_code ec;
            for (auto dir = std::filesystem::current_path(ec); !dir.empty(); dir = dir.parent_path()) {
                if (auto candidate = dir / name; std::filesystem::is_regular_file(candidate, ec))
                    return candidate;
                if (dir.parent_path() == dir) break;
            }
        }
        return {};
    }();

    if (self.empty()) GTEST_SKIP() << "libKoral not found near the working directory";

    const std::vector<std::string> requested { self.string() };
    const auto result = ModuleHost::Load(requested, {});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::eModuleLoadFailed);
    EXPECT_NE(result.error().message.find("korModuleDescriptor"), std::string::npos);
}

TEST(Module, ALinkedModuleIsInTheSetWithoutBeingAskedFor)
{
    // This binary links koral-mesh, for the vertex formats the layout tests use. Linking is the
    // whole request: the library's registrar ran as it was loaded, so the module is in the set
    // with nothing named in a config and nothing loaded by hand. @see kor::ModuleRegistrar
    const auto loaded = ModuleHost::loadedModules();
    EXPECT_NE(std::ranges::find(loaded, "koral.mesh"), loaded.end());
}

// ---- self-registration ------------------------------------------------------------------------
//
// What a linked module does to itself as its library loads, minus the library: Register() is the
// same entry point KORAL_DECLARE_MODULE's registrar calls. The registry is process-wide, so every
// test here starts and ends by tearing it down.

namespace
{
    struct TestModule final : kor::Module {};

    kor::Module* createTestModule() { return new TestModule(); }

    constexpr kor::ModuleDescriptor kAlone { .id = "test.alone", .version = 1 };

    constexpr kor::Dependency kNeedsMissing[] {
        kor::Dependency{ "test.absent", 1, kor::Dependency::Kind::eRequired } };
    constexpr kor::ModuleDescriptor kDependent {
        .id = "test.dependent", .version = 1,
        .dependencies = kNeedsMissing, .dependencyCount = 1 };

    constexpr kor::Dependency kOptionalMissing[] {
        kor::Dependency{ "test.absent", 1, kor::Dependency::Kind::eOptional } };
    constexpr kor::ModuleDescriptor kOptionalDependent {
        .id = "test.optional", .version = 1,
        .dependencies = kOptionalMissing, .dependencyCount = 1 };

    struct ModuleRegistry : testing::Test
    {
        void SetUp() override { ModuleHost::Shutdown(); }
        void TearDown() override { ModuleHost::Shutdown(); }
    };
}

TEST_F(ModuleRegistry, RegisteringAModuleMakesItPartOfTheSet)
{
    ModuleHost::Register(&kAlone, &createTestModule);
    ASSERT_TRUE(ModuleHost::Resolve());

    const auto loaded = ModuleHost::loadedModules();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded.front(), "test.alone");
}

TEST_F(ModuleRegistry, RegisteringTheSameModuleTwiceLoadsItOnce)
{
    // Exactly what a module that is both linked and named in koral.json does.
    ModuleHost::Register(&kAlone, &createTestModule);
    ModuleHost::Register(&kAlone, &createTestModule);

    ASSERT_TRUE(ModuleHost::Resolve());
    EXPECT_EQ(ModuleHost::loadedModules().size(), 1u);
}

TEST_F(ModuleRegistry, GarbageRegistrationsAreIgnored)
{
    ModuleHost::Register(nullptr, &createTestModule);
    ModuleHost::Register(&kAlone, nullptr);

    ASSERT_TRUE(ModuleHost::Resolve());
    EXPECT_TRUE(ModuleHost::loadedModules().empty());
}

TEST_F(ModuleRegistry, AMissingRequiredDependencyFailsResolveAndNamesBoth)
{
    ModuleHost::Register(&kDependent, &createTestModule);

    const auto result = ModuleHost::Resolve();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::eModuleLoadFailed);
    EXPECT_NE(result.error().message.find("test.dependent"), std::string::npos);
    EXPECT_NE(result.error().message.find("test.absent"), std::string::npos);
}

TEST_F(ModuleRegistry, AMissingOptionalDependencyIsFine)
{
    ModuleHost::Register(&kOptionalDependent, &createTestModule);

    ASSERT_TRUE(ModuleHost::Resolve());
    EXPECT_EQ(ModuleHost::loadedModules().size(), 1u);
}
