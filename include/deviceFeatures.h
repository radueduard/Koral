//
// What of a GPU a project asks for, beyond what Koral itself needs.
//

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "api.h"
#include "flags.h"

namespace kor
{
    /**
     * @brief A capability of the GPU that Koral does not need itself, but a project — a physics plugin, a compute
     *        renderer — may: the device is created with it enabled when it is asked for, and refused, by name,
     *        when it is required of a GPU that does not have it.
     *
     * Ask for one with `KORAL_REQUIRE_FEATURES(...)` (without it the project cannot run) or
     * `KORAL_REQUEST_FEATURES(...)` (it can, using it where Context::Supports says it is there), at namespace scope
     * in any library — or in AppSettings, or koral.json's `features`. @see features.h
     */
    enum class Feature : std::uint64_t {
        eNone                     = 0,
        // Shader number types
        eShaderFloat64            = 0x1,   ///< `double` in shaders.
        eShaderInt64              = 0x2,   ///< 64-bit integers in shaders.
        eShaderInt16              = 0x4,   ///< 16-bit integers in shaders.
        eShaderFloat16            = 0x8,   ///< 16-bit floats (half) in shaders.
        eShaderInt8               = 0x10,   ///< 8-bit integers in shaders.
        eStorage16Bit             = 0x20,   ///< 16-bit values read and written in storage and uniform buffers.
        eStorage8Bit              = 0x40,   ///< 8-bit values read and written in storage and uniform buffers.
        // Atomics
        eInt64Atomics             = 0x80,   ///< 64-bit integer atomics on buffers and shared memory.
        eAtomicFloat32            = 0x100,   ///< `atomicAdd` and exchange on 32-bit floats in buffers and shared memory: SPH, particles, splatting.
        eAtomicFloat64            = 0x200,   ///< The same on 64-bit floats.
        eFragmentStoresAndAtomics = 0x400,  ///< Storage writes and atomics from fragment shaders.
        eVertexStoresAndAtomics   = 0x800,  ///< Storage writes and atomics from vertex, tessellation and geometry shaders.
        // Subgroups and matrices
        eSubgroupExtendedTypes    = 0x1000,  ///< Subgroup operations on 8, 16 and 64-bit types.
        eCooperativeMatrix        = 0x2000,  ///< Cooperative matrices (VK_KHR_cooperative_matrix): tensor-core matrix multiplies.
        // Drawing
        eMultiDrawIndirect        = 0x4000,  ///< Indirect draws of more than one draw at a time.
        eDrawIndirectCount        = 0x8000,  ///< Indirect draws whose count is read from a buffer.
        eGeometryShader           = 0x10000,  ///< Geometry shaders.
        eTessellationShader       = 0x20000,  ///< Tessellation shaders.
        eFillModeNonSolid         = 0x40000,  ///< Wireframe and point polygon modes.
        eWideLines                = 0x80000,  ///< Lines wider than one pixel.
        eSamplerAnisotropy        = 0x100000,  ///< Anisotropic filtering.
        eMeshShader               = 0x200000,  ///< Mesh and task shaders. (Enabled where present whether asked for or not.)
        eRayTracing               = 0x400000,  ///< Ray tracing pipelines and acceleration structures. (The same.)
    };
    template <> struct enable_flags<Feature> : std::true_type {};

    /** @brief A feature's name as it is written in koral.json and in messages: "AtomicFloat32". */
    [[nodiscard]] KORAL_API const char* FeatureName(Feature feature);
    /** @brief The feature named @p name ("AtomicFloat32", or "eAtomicFloat32", in any case); eNone for none. */
    [[nodiscard]] KORAL_API Feature FeatureNamed(std::string_view name);
    /** @brief Every feature of @p features, by name: "AtomicFloat32, ShaderInt64". */
    [[nodiscard]] KORAL_API std::string FeatureNames(Flags<Feature> features);

    /** @brief What has asked for features so far, before the device is made from them. */
    struct FeatureRequest {
        std::string by;                 ///< Who: a library's source file, "AppSettings", "koral.json".
        Flags<Feature> required;        ///< Without them it cannot run.
        Flags<Feature> optional;        ///< It uses them where they are there.
    };

    namespace detail
    {
        /**
         * @brief Records what @p by needs and would use. Before the device exists it is what the device is made
         *        with; after, a required feature it was made without is an error that the next library load
         *        reports, and which RegisterFeatures logs. Returns 0, for the macros' static initialisers.
         */
        KORAL_API int RegisterFeatures(const char* by, Flags<Feature> required, Flags<Feature> optional);
        /** @brief Every request so far. */
        [[nodiscard]] KORAL_API std::vector<FeatureRequest> FeatureRequests();
        /** @brief Required features asked for since the device was made, which it lacks: taken, so reported once. */
        [[nodiscard]] KORAL_API std::vector<std::string> TakeLateFeatureErrors();
        /**
         * @brief What a GPU that has @p available lacks of what @p requests require, each missing feature with who
         *        required it — or empty when it has all of them. What the device is refused with.
         */
        [[nodiscard]] KORAL_API std::string MissingRequired(Flags<Feature> available, const std::vector<FeatureRequest>& requests);
        /** @brief Forgets every request: for tests, which make a device after another in one process. */
        KORAL_API void ResetFeatureRequests();
    }
}

#define KORAL_FEATURES_CONCAT_(a, b) a##b
#define KORAL_FEATURES_CONCAT(a, b) KORAL_FEATURES_CONCAT_(a, b)

/**
 * @brief At namespace scope in any source file of a library: the device must have these, or the project cannot
 *        run — `KORAL_REQUIRE_FEATURES(kor::Feature::eAtomicFloat32 | kor::Feature::eShaderInt64);`. Recorded when
 *        the library is loaded, which for the runtime's project is before the device is made.
 */
#define KORAL_REQUIRE_FEATURES(features) \
    [[maybe_unused]] static const int KORAL_FEATURES_CONCAT(koralRequiredFeatures_, __LINE__) = \
        ::kor::detail::RegisterFeatures(__FILE__, ::kor::Flags<::kor::Feature>(features), {})

/** @brief As KORAL_REQUIRE_FEATURES, for features it uses where Context::Supports says they are there. */
#define KORAL_REQUEST_FEATURES(features) \
    [[maybe_unused]] static const int KORAL_FEATURES_CONCAT(koralRequestedFeatures_, __LINE__) = \
        ::kor::detail::RegisterFeatures(__FILE__, {}, ::kor::Flags<::kor::Feature>(features))
