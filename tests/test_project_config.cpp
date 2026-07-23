//
// Tests for the run configuration: koral.json parsing, the layering of compiled-in defaults under
// the config file under the command line, and the resolution of relative directories.
//
// GPU-independent by construction — none of this touches a device.
//

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <algorithm>
#include <vector>

#include "context.h"
#include "projectConfig.h"
#include "shader.h"

using kor::API;
using kor::ErrorCode;
using kor::ProjectConfig;

namespace
{
    // applyOverrides takes the arguments alone, as the runtime passes them (argv + 2).
    kor::VoidResult override_(ProjectConfig& config, const std::vector<std::string>& args)
    {
        return config.applyOverrides(args);
    }

    // A base directory that is absolute but need not exist: merge() only joins paths, it does not
    // go to the filesystem, and keeping the tests off disk keeps them fast and hermetic.
    const std::filesystem::path kBase = std::filesystem::path("/projects/game").lexically_normal();
}

// --- the document ----------------------------------------------------------------------------

TEST(ProjectConfig, ReadsEveryKey)
{
    ProjectConfig config;
    const auto result = config.merge(R"({
        "schemaVersion": 1,
        "name": "My Game",
        "rendering": {
            "api": "OpenGL",
            "platform": "wayland",
            "window": {
                "width": 1600, "height": 900,
                "resizable": true, "fullscreen": true, "borderless": true,
                "transparent": true, "vsync": false,
                "imguiIni": "state/imgui.ini"
            }
        },
        "paths": {
            "assetDirectories":  ["assets", "../shared/assets"],
            "shaderDirectories": ["shaders"]
        }
    })", kBase);

    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(config.api, API::eOpenGL);
    EXPECT_EQ(config.title, "My Game");
    EXPECT_EQ(config.extent.x, 1600u);
    EXPECT_EQ(config.extent.y, 900u);
    EXPECT_TRUE(config.fullscreen);
    EXPECT_TRUE(config.resizable);
    EXPECT_FALSE(config.decorated);
    EXPECT_TRUE(config.transparentFramebuffer);
    EXPECT_FALSE(config.vsync);
    EXPECT_EQ(config.platform, kor::WindowPlatform::eWayland);
    EXPECT_EQ(config.imguiIni, std::filesystem::path("/projects/game/state/imgui.ini"));

    ASSERT_EQ(config.assetDirectories.size(), 2u);
    EXPECT_EQ(config.assetDirectories[0], "/projects/game/assets");
    EXPECT_EQ(config.assetDirectories[1], "/projects/shared/assets");
    ASSERT_EQ(config.shaderDirectories.size(), 1u);
    EXPECT_EQ(config.shaderDirectories[0], "/projects/game/shaders");
}

// The layering only works if an absent key is a no-op. A config that mentioned nothing but the
// window size must not quietly reset the API the project was compiled to ask for.
TEST(ProjectConfig, AbsentKeysLeaveTheLayerBeneathAlone)
{
    ProjectConfig config;
    config.api = API::eOpenGL;
    config.title = "From the binary";
    config.vsync = false;
    config.assetDirectories = { "/compiled/in" };

    const auto result = config.merge(R"({ "rendering": { "window": { "width": 800 } } })", kBase);

    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(config.extent.x, 800u);
    EXPECT_EQ(config.extent.y, 720u);              // the default, untouched
    EXPECT_EQ(config.api, API::eOpenGL);           // still what the binary asked for
    EXPECT_EQ(config.title, "From the binary");
    EXPECT_FALSE(config.vsync);
    ASSERT_EQ(config.assetDirectories.size(), 1u);
    EXPECT_EQ(config.assetDirectories[0], "/compiled/in");
}

TEST(ProjectConfig, EmptyDocumentIsValid)
{
    ProjectConfig config;
    EXPECT_TRUE(config.merge("{}", kBase));
}

// A directory list is replaced, not appended to — otherwise a config could never drop a root the
// binary hard-coded, and re-merging would duplicate entries.
TEST(ProjectConfig, DirectoryListsAreReplacedNotAppended)
{
    ProjectConfig config;
    config.assetDirectories = { "/compiled/in" };

    ASSERT_TRUE(config.merge(R"({ "paths": { "assetDirectories": ["assets"] } })", kBase));

    ASSERT_EQ(config.assetDirectories.size(), 1u);
    EXPECT_EQ(config.assetDirectories[0], "/projects/game/assets");
}

TEST(ProjectConfig, AbsoluteDirectoriesAreLeftAlone)
{
    ProjectConfig config;
    ASSERT_TRUE(config.merge(R"({ "paths": { "assetDirectories": ["/opt/shared/textures"] } })", kBase));

    ASSERT_EQ(config.assetDirectories.size(), 1u);
    EXPECT_EQ(config.assetDirectories[0], "/opt/shared/textures");
}

TEST(ProjectConfig, UnknownKeysAreIgnored)
{
    // Forward compatibility: a config written by a newer Hub must still load in an older runtime.
    ProjectConfig config;
    const auto result = config.merge(
        R"({ "rendering": { "window": { "width": 640 } }, "somethingNew": { "nested": [1, 2] } })", kBase);

    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(config.extent.x, 640u);
}

TEST(ProjectConfig, MalformedJsonIsAnError)
{
    ProjectConfig config;
    const auto result = config.merge("{ not json", kBase);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::eConfigInvalid);
}

TEST(ProjectConfig, WrongTypeIsAnError)
{
    ProjectConfig config;
    const auto result = config.merge(R"({ "rendering": { "window": { "width": "wide" } } })", kBase);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::eConfigInvalid);
}

TEST(ProjectConfig, UnknownApiIsAnErrorAndNamesTheOffendingValue)
{
    ProjectConfig config;
    const auto result = config.merge(R"({ "rendering": { "api": "Metal" } })", kBase);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::eConfigInvalid);
    EXPECT_NE(result.error().message.find("Metal"), std::string::npos);
}

TEST(ProjectConfig, ApiNameIsCaseInsensitiveAndAcceptsTheEnumeratorSpelling)
{
    for (const auto* name : { "Vulkan", "vulkan", "VULKAN", "eVulkan" }) {
        ProjectConfig config;
        config.api = API::eOpenGL;
        ASSERT_TRUE(config.merge(
            std::string(R"({ "rendering": { "api": ")") + name + R"(" } })", kBase)) << name;
        EXPECT_EQ(config.api, API::eVulkan) << name;
    }
}

TEST(ProjectConfig, ReadsTheWindowingPlatform)
{
    ProjectConfig config;
    ASSERT_TRUE(config.merge(R"({ "rendering": { "platform": "wayland" } })", kBase));
    EXPECT_EQ(config.platform, kor::WindowPlatform::eWayland);
}

TEST(ProjectConfig, PlatformDefaultsToAutoAndIsLeftAloneWhenAbsent)
{
    ProjectConfig config;                              // default
    EXPECT_EQ(config.platform, kor::WindowPlatform::eAuto);
    ASSERT_TRUE(config.merge(R"({ "rendering": { "api": "Vulkan" } })", kBase));
    EXPECT_EQ(config.platform, kor::WindowPlatform::eAuto) << "an absent key must not reset it";
}

TEST(ProjectConfig, PlatformNameIsCaseInsensitiveAndAcceptsTheEnumeratorSpelling)
{
    for (const auto* name : { "x11", "X11", "eX11" }) {
        ProjectConfig config;
        ASSERT_TRUE(config.merge(
            std::string(R"({ "rendering": { "platform": ")") + name + R"(" } })", kBase)) << name;
        EXPECT_EQ(config.platform, kor::WindowPlatform::eX11) << name;
    }
}

TEST(ProjectConfig, UnknownPlatformIsAnErrorAndNamesTheOffendingValue)
{
    ProjectConfig config;
    const auto result = config.merge(R"({ "rendering": { "platform": "mir" } })", kBase);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::eConfigInvalid);
    EXPECT_NE(result.error().message.find("mir"), std::string::npos);
}

TEST(ProjectConfig, PlatformFlagOverridesTheFile)
{
    ProjectConfig config;
    ASSERT_TRUE(config.merge(R"({ "rendering": { "platform": "x11" } })", kBase));
    ASSERT_TRUE(override_(config, { "--platform", "wayland" }));
    EXPECT_EQ(config.platform, kor::WindowPlatform::eWayland);
}

TEST(ProjectConfig, UnknownPlatformFlagValueIsAnError)
{
    ProjectConfig config;
    const auto result = override_(config, { "--platform", "mir" });

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::eInvalidArgument);
    EXPECT_NE(result.error().message.find("mir"), std::string::npos);
}

TEST(ProjectConfig, ImguiIniResolvesAgainstTheConfigDirectory)
{
    ProjectConfig config;
    ASSERT_TRUE(config.merge(R"({ "rendering": { "window": { "imguiIni": "layout/imgui.ini" } } })", kBase));
    EXPECT_EQ(config.imguiIni, std::filesystem::path("/projects/game/layout/imgui.ini"));
}

TEST(ProjectConfig, AbsoluteImguiIniIsLeftAlone)
{
    ProjectConfig config;
    ASSERT_TRUE(config.merge(R"({ "rendering": { "window": { "imguiIni": "/var/state/imgui.ini" } } })", kBase));
    EXPECT_EQ(config.imguiIni, std::filesystem::path("/var/state/imgui.ini"));
}

TEST(ProjectConfig, ImguiIniIsEmptyByDefault)
{
    // The "beside koral.json" default is applied by the runtime once it knows the config file's
    // location; the config object itself leaves it empty until something sets it.
    ProjectConfig config;
    EXPECT_TRUE(config.imguiIni.empty());
    ASSERT_TRUE(config.merge(R"({ "rendering": { "api": "Vulkan" } })", kBase));
    EXPECT_TRUE(config.imguiIni.empty()) << "an absent key must not invent a path";
}

TEST(ProjectConfig, ImguiIniFlagResolvesAgainstTheWorkingDirectory)
{
    ProjectConfig config;
    ASSERT_TRUE(override_(config, { "--imgui-ini", "state/ui.ini" }));
    EXPECT_TRUE(config.imguiIni.is_absolute()) << "a command-line path is made absolute against the cwd";
    EXPECT_EQ(config.imguiIni.filename(), "ui.ini");
}

TEST(ProjectConfig, ImguiIniFlagOverridesTheFile)
{
    ProjectConfig config;
    ASSERT_TRUE(config.merge(R"({ "rendering": { "window": { "imguiIni": "from-file.ini" } } })", kBase));
    ASSERT_TRUE(override_(config, { "--imgui-ini", "from-flag.ini" }));
    EXPECT_EQ(config.imguiIni.filename(), "from-flag.ini");
}

TEST(ProjectConfig, MissingImguiIniFlagValueIsAnError)
{
    ProjectConfig config;
    const auto result = override_(config, { "--imgui-ini" });

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::eInvalidArgument);
}

// koral.json IS the Hub's project file. The Hub writes it, we read it, and the two share no code —
// they cannot, the Hub is a separate Rust program that consumes engine *releases*. This schema is
// therefore the entire contract between them, and this is that contract: a document exactly as the
// Hub's serde serializes it (src-tauri/src/model.rs), keys the engine ignores and all.
//
// If this ever stops parsing, every project launched from the Hub breaks, and nothing in the Hub's
// own build or tests would say a word about it.
TEST(ProjectConfig, ParsesADocumentInTheShapeTheHubWrites)
{
    ProjectConfig config;
    const auto result = config.merge(R"({
      "schemaVersion": 1,
      "name": "MyProject",
      "color": [0.55, 0.72, 0.61],
      "frameworkVersion": "0.0.1",
      "kind": "Scene",
      "rendering": {
        "api": "Vulkan",
        "window": {
          "width": 1280,
          "height": 720,
          "resizable": true,
          "fullscreen": false,
          "borderless": false,
          "transparent": false,
          "vsync": true
        }
      },
      "paths": {
        "assetDirectories": ["assets"],
        "shaderDirectories": ["shaders"]
      },
      "libraries": []
    })", kBase);

    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(config.api, API::eVulkan);
    EXPECT_EQ(config.title, "MyProject") << "the project's name is the window title";
    EXPECT_EQ(config.extent.x, 1280u);
    EXPECT_EQ(config.extent.y, 720u);
    EXPECT_TRUE(config.resizable);
    EXPECT_FALSE(config.fullscreen);
    EXPECT_TRUE(config.decorated) << "borderless:false must mean decorated:true";
    EXPECT_FALSE(config.transparentFramebuffer);
    EXPECT_TRUE(config.vsync);
    ASSERT_EQ(config.assetDirectories.size(), 1u);
    EXPECT_EQ(config.assetDirectories[0], "/projects/game/assets");
    ASSERT_EQ(config.shaderDirectories.size(), 1u);
    EXPECT_EQ(config.shaderDirectories[0], "/projects/game/shaders");
}

// The Hub's `kind` decides Scene vs Job, but the engine decides that by which symbol the library
// exports — so `kind` is one of the keys we deliberately ignore rather than honour. Same for the
// Hub's package-manager and presentation keys. Ignoring them has to be silent, not an error.
TEST(ProjectConfig, TheHubsOwnKeysAreIgnoredNotRejected)
{
    ProjectConfig config;
    EXPECT_TRUE(config.merge(
        R"({ "kind": "Job", "color": [1,0,0], "frameworkVersion": "9.9.9",
             "libraries": [{ "vcpkgPort": "eigen3", "minVersion": "3.4" }] })", kBase));
}

// Projects scaffolded before the directory lists existed wrote a single string. They must keep
// finding their content — a silently-unloaded assets folder is a miserable thing to debug.
TEST(ProjectConfig, TheOriginalSingularPathKeysStillWork)
{
    ProjectConfig config;
    ASSERT_TRUE(config.merge(
        R"({ "paths": { "assetsDir": "assets", "shadersDir": "shaders" } })", kBase));

    ASSERT_EQ(config.assetDirectories.size(), 1u);
    EXPECT_EQ(config.assetDirectories[0], "/projects/game/assets");
    ASSERT_EQ(config.shaderDirectories.size(), 1u);
    EXPECT_EQ(config.shaderDirectories[0], "/projects/game/shaders");
}

// --- the command line ------------------------------------------------------------------------

TEST(ProjectConfig, FlagsOverrideTheConfigFile)
{
    ProjectConfig config;
    ASSERT_TRUE(config.merge(R"({
        "rendering": {
            "api": "Vulkan",
            "window": { "width": 1280, "height": 720, "vsync": true, "fullscreen": true }
        }
    })", kBase));

    ASSERT_TRUE(override_(config, { "--width", "1920", "--no-vsync", "--api", "OpenGL" }));

    EXPECT_EQ(config.extent.x, 1920u);   // overridden
    EXPECT_EQ(config.extent.y, 720u);    // from the file, untouched
    EXPECT_FALSE(config.vsync);          // overridden
    EXPECT_TRUE(config.fullscreen);      // from the file, untouched
    EXPECT_EQ(config.api, API::eOpenGL); // overridden
}

TEST(ProjectConfig, NegativeFlagsTurnConfigSettingsOff)
{
    ProjectConfig config;
    ASSERT_TRUE(config.merge(R"({
        "rendering": {
            "window": { "fullscreen": true, "resizable": true, "borderless": true, "transparent": true }
        }
    })", kBase));

    ASSERT_TRUE(override_(config,
        { "--no-fullscreen", "--no-resizable", "--decorated", "--no-transparent" }));

    EXPECT_FALSE(config.fullscreen);
    EXPECT_FALSE(config.resizable);
    EXPECT_TRUE(config.decorated);
    EXPECT_FALSE(config.transparentFramebuffer);
}

TEST(ProjectConfig, DirectoryFlagsArePrependedAheadOfTheConfigs)
{
    ProjectConfig config;
    ASSERT_TRUE(config.merge(R"({ "paths": { "assetDirectories": ["/from/config"] } })", kBase));

    ASSERT_TRUE(override_(config, { "--assets", "/from/cli" }));

    ASSERT_EQ(config.assetDirectories.size(), 2u);
    EXPECT_EQ(config.assetDirectories[0], "/from/cli");     // searched first
    EXPECT_EQ(config.assetDirectories[1], "/from/config");
}

TEST(ProjectConfig, TheLastDirectoryFlagWins)
{
    // Repeatable, and consistent with every other flag: the last one typed takes precedence.
    ProjectConfig config;
    ASSERT_TRUE(override_(config, { "--shaders", "/first", "--shaders", "/second" }));

    ASSERT_EQ(config.shaderDirectories.size(), 2u);
    EXPECT_EQ(config.shaderDirectories[0], "/second");
    EXPECT_EQ(config.shaderDirectories[1], "/first");
}

TEST(ProjectConfig, RelativeDirectoryFlagsResolveAgainstTheWorkingDirectory)
{
    // Unlike the config's, which resolve against the config file: a path typed at a shell prompt
    // means what it means there.
    ProjectConfig config;
    ASSERT_TRUE(override_(config, { "--assets", "textures" }));

    ASSERT_EQ(config.assetDirectories.size(), 1u);
    EXPECT_EQ(config.assetDirectories[0], std::filesystem::current_path() / "textures");
}

TEST(ProjectConfig, ConfigFlagIsSkippedRatherThanRejected)
{
    // The runtime consumes --config before applyOverrides ever sees it, but it is still in the
    // argument list — it must not be mistaken for an unknown option, and its value must not be
    // mistaken for one either.
    ProjectConfig config;
    ASSERT_TRUE(override_(config, { "--config", "/some/koral.json", "--width", "640" }));
    EXPECT_EQ(config.extent.x, 640u);
}

TEST(ProjectConfig, UnknownFlagIsAnError)
{
    // A typo'd flag is a mistake worth stopping for: silently ignoring it means the user watches
    // their setting fail to take effect with no idea why.
    ProjectConfig config;
    const auto result = override_(config, { "--fulscreen" });

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::eInvalidArgument);
    EXPECT_NE(result.error().message.find("--fulscreen"), std::string::npos);
}

TEST(ProjectConfig, MissingFlagValueIsAnError)
{
    ProjectConfig config;
    const auto result = override_(config, { "--width" });

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::eInvalidArgument);
}

TEST(ProjectConfig, NonNumericSizeIsAnError)
{
    ProjectConfig config;
    const auto result = override_(config, { "--width", "1920x1080" });

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::eInvalidArgument);
}

// --- finding the file ------------------------------------------------------------------------

class ProjectConfigFile : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _root = std::filesystem::temp_directory_path() /
                ("koral-config-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
                 "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(_root);
        std::filesystem::create_directories(_root / "cmake-build-debug");
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(_root, ec);
    }

    void writeConfig(const std::string_view contents) const
    {
        std::ofstream out(_root / ProjectConfig::kFileName);
        out << contents;
    }

    std::filesystem::path _root;
};

// The runtime is handed a scene library inside a build directory; the config lives at the project
// root above it. Finding it is the whole reason the search walks up.
TEST_F(ProjectConfigFile, IsFoundByWalkingUpFromTheSceneLibrary)
{
    writeConfig(R"({ "rendering": { "window": { "width": 1024 } } })");

    const auto found = ProjectConfig::find(_root / "cmake-build-debug");

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, _root / ProjectConfig::kFileName);
}

TEST_F(ProjectConfigFile, IsNotFoundWhenThereIsNone)
{
    EXPECT_FALSE(ProjectConfig::find(_root / "cmake-build-debug").has_value());
}

// The file's own directory is the base for its relative paths, so a project keeps working when it
// is moved or checked out somewhere else.
TEST_F(ProjectConfigFile, RelativeDirectoriesResolveAgainstTheFile)
{
    writeConfig(R"({ "paths": { "assetDirectories": ["assets"], "shaderDirectories": ["shaders"] } })");

    ProjectConfig config;
    const auto result = config.mergeFile(_root / ProjectConfig::kFileName);

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(config.assetDirectories.size(), 1u);
    EXPECT_EQ(config.assetDirectories[0], _root / "assets");
    ASSERT_EQ(config.shaderDirectories.size(), 1u);
    EXPECT_EQ(config.shaderDirectories[0], _root / "shaders");
}

TEST_F(ProjectConfigFile, MissingFileIsAnError)
{
    ProjectConfig config;
    const auto result = config.mergeFile(_root / "nope.json");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::eConfigInvalid);
}

TEST_F(ProjectConfigFile, MalformedFileNamesItselfInTheError)
{
    writeConfig("{ oops");

    ProjectConfig config;
    const auto result = config.mergeFile(_root / ProjectConfig::kFileName);

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find(ProjectConfig::kFileName), std::string::npos);
}

// --- resolution ------------------------------------------------------------------------------
//
// The point of the whole feature: a scene asks for "textures/wood.png" and gets the file, because
// the project said where its assets live. These register into the process-wide search roots, so
// they use directories unique to each test and only ever prepend.

TEST_F(ProjectConfigFile, RegisteredDirectoriesResolveRelativeAssetPaths)
{
    std::filesystem::create_directories(_root / "assets" / "textures");
    std::ofstream(_root / "assets" / "textures" / "wood.png") << "not really a png";
    writeConfig(R"({ "paths": { "assetDirectories": ["assets"] } })");

    ProjectConfig config;
    ASSERT_TRUE(config.mergeFile(_root / ProjectConfig::kFileName));
    config.registerSearchPaths();

    EXPECT_EQ(kor::assetPath("textures/wood.png"), _root / "assets" / "textures" / "wood.png");
}

TEST_F(ProjectConfigFile, RegisteredDirectoriesResolveRelativeShaderPaths)
{
    std::filesystem::create_directories(_root / "shaders");
    std::ofstream(_root / "shaders" / "blur.comp.glsl") << "// shader";
    writeConfig(R"({ "paths": { "shaderDirectories": ["shaders"] } })");

    ProjectConfig config;
    ASSERT_TRUE(config.mergeFile(_root / ProjectConfig::kFileName));
    config.registerSearchPaths();

    EXPECT_EQ(kor::shaderPath("blur.comp.glsl"), _root / "shaders" / "blur.comp.glsl");
}

// Config roots go in front, but the engine's own stay reachable behind them — a project that
// brings its own assets must not thereby lose the fonts and shaders that ship with Koral.
TEST_F(ProjectConfigFile, RegisteringDirectoriesKeepsTheEnginesOwnRoots)
{
    const auto enginesOwn = kor::assetSearchPaths();
    ASSERT_FALSE(enginesOwn.empty()) << "expected Koral's own assets/ to be registered";

    std::filesystem::create_directories(_root / "assets");
    writeConfig(R"({ "paths": { "assetDirectories": ["assets"] } })");

    ProjectConfig config;
    ASSERT_TRUE(config.mergeFile(_root / ProjectConfig::kFileName));
    config.registerSearchPaths();

    const auto& roots = kor::assetSearchPaths();
    EXPECT_EQ(roots.front(), _root / "assets") << "the config's root should be searched first";
    for (const auto& root : enginesOwn)
        EXPECT_NE(std::ranges::find(roots, root), roots.end()) << root.string() << " was dropped";
}

TEST_F(ProjectConfigFile, AbsoluteAssetPathsAreNeverResolved)
{
    const auto absolute = _root / "somewhere" / "else.png";
    EXPECT_EQ(kor::assetPath(absolute), absolute);
}

// Before the search roots existed, a relative path just meant "relative to where you launched
// from", and code that still passes one has to keep finding its file. The working directory is
// consulted last, so it can never shadow a project's own directories.
TEST_F(ProjectConfigFile, WorkingDirectoryIsTheLastResort)
{
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(_root);

    std::ofstream(_root / "beside-the-cwd.png") << "not really a png";
    EXPECT_EQ(kor::assetPath("beside-the-cwd.png"), std::filesystem::path("beside-the-cwd.png"));

    std::filesystem::current_path(previous);
}

TEST_F(ProjectConfigFile, AConfigRootBeatsTheWorkingDirectory)
{
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(_root);

    // The same name in both places: the project's own directory has to win.
    std::filesystem::create_directories(_root / "assets");
    std::ofstream(_root / "assets" / "shared.png") << "from the project";
    std::ofstream(_root / "shared.png") << "from the working directory";
    writeConfig(R"({ "paths": { "assetDirectories": ["assets"] } })");

    ProjectConfig config;
    ASSERT_TRUE(config.mergeFile(_root / ProjectConfig::kFileName));
    config.registerSearchPaths();

    EXPECT_EQ(kor::assetPath("shared.png"), _root / "assets" / "shared.png");

    std::filesystem::current_path(previous);
}
