//
// Features asked of the GPU: their names, who asked, and what the device was made with.
//

#include "deviceFeatures.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <utility>

#include "context.h"
#include "featureState.h"
#include "log.h"

namespace kor
{
    namespace
    {
        constexpr std::pair<Feature, const char*> Names[] = {
            { Feature::eShaderFloat64, "ShaderFloat64" },           { Feature::eShaderInt64, "ShaderInt64" },
            { Feature::eShaderInt16, "ShaderInt16" },               { Feature::eShaderFloat16, "ShaderFloat16" },
            { Feature::eShaderInt8, "ShaderInt8" },                 { Feature::eStorage16Bit, "Storage16Bit" },
            { Feature::eStorage8Bit, "Storage8Bit" },               { Feature::eInt64Atomics, "Int64Atomics" },
            { Feature::eAtomicFloat32, "AtomicFloat32" },           { Feature::eAtomicFloat64, "AtomicFloat64" },
            { Feature::eFragmentStoresAndAtomics, "FragmentStoresAndAtomics" },
            { Feature::eVertexStoresAndAtomics, "VertexStoresAndAtomics" },
            { Feature::eSubgroupExtendedTypes, "SubgroupExtendedTypes" }, { Feature::eCooperativeMatrix, "CooperativeMatrix" },
            { Feature::eMultiDrawIndirect, "MultiDrawIndirect" },   { Feature::eDrawIndirectCount, "DrawIndirectCount" },
            { Feature::eGeometryShader, "GeometryShader" },         { Feature::eTessellationShader, "TessellationShader" },
            { Feature::eFillModeNonSolid, "FillModeNonSolid" },     { Feature::eWideLines, "WideLines" },
            { Feature::eSamplerAnisotropy, "SamplerAnisotropy" },   { Feature::eMeshShader, "MeshShader" },
            { Feature::eRayTracing, "RayTracing" },
        };

        struct Registry {
            std::mutex mutex;
            std::vector<FeatureRequest> requests;
            bool deviceUp = false;
            Flags<Feature> available, enabled;
            std::vector<std::string> lateErrors;
        };

        // Behind an accessor, in the library: one registry for the process, whoever registers into it.
        Registry& registry()
        {
            static Registry r;
            return r;
        }
    }

    const char* FeatureName(const Feature feature)
    {
        for (const auto& [f, name] : Names) if (f == feature) return name;
        return "None";
    }

    Feature FeatureNamed(std::string_view name)
    {
        if (name.starts_with('e') || name.starts_with('E')) {
            // "eAtomicFloat32" — but not a name that starts with an E of its own.
            const std::string_view rest = name.substr(1);
            for (const auto& [f, n] : Names)
                if (std::ranges::equal(rest, std::string_view(n), [](const char a, const char b) {
                        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }))
                    return f;
        }
        for (const auto& [f, n] : Names)
            if (std::ranges::equal(name, std::string_view(n), [](const char a, const char b) {
                    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }))
                return f;
        return Feature::eNone;
    }

    std::string FeatureNames(const Flags<Feature> features)
    {
        std::string out;
        for (const auto& [f, name] : Names) {
            if (!(features & f)) continue;
            if (!out.empty()) out += ", ";
            out += name;
        }
        return out;
    }

    int detail::RegisterFeatures(const char* by, const Flags<Feature> required, const Flags<Feature> optional)
    {
        auto& r = registry();
        std::lock_guard lock(r.mutex);
        r.requests.push_back(FeatureRequest{ .by = by ? by : "", .required = required, .optional = optional });
        if (r.deviceUp) {
            // Too late to make the device with it: fine where it was enabled anyway, an error where not.
            const Flags<Feature> missing = required & ~r.enabled;
            if (missing) {
                const std::string message = std::format(
                    "{} requires {}, which the device was made without{}. Ask for it before the device is made: in "
                    "AppSettings::requiredFeatures, koral.json's \"features\", or a library loaded first.",
                    by ? by : "a library", FeatureNames(missing),
                    (missing & ~r.available) ? std::format(" (the GPU has no {})", FeatureNames(missing & ~r.available)) : "");
                log::Error("[features] {}", message);
                r.lateErrors.push_back(message);
            }
        }
        return 0;
    }

    std::vector<FeatureRequest> detail::FeatureRequests()
    {
        auto& r = registry();
        std::lock_guard lock(r.mutex);
        return r.requests;
    }

    std::vector<std::string> detail::TakeLateFeatureErrors()
    {
        auto& r = registry();
        std::lock_guard lock(r.mutex);
        return std::exchange(r.lateErrors, {});
    }

    std::string detail::MissingRequired(const Flags<Feature> available, const std::vector<FeatureRequest>& requests)
    {
        Flags<Feature> lacking;
        for (const auto& request : requests) lacking |= request.required & ~available;
        if (!lacking) return {};
        std::string out = FeatureNames(lacking);
        for (const auto& request : requests)
            if (const Flags<Feature> theirs = request.required & lacking)
                out += std::format("\n  {} (required by {})", FeatureNames(theirs), request.by);
        return out;
    }

    void detail::ResetFeatureRequests()
    {
        auto& r = registry();
        std::lock_guard lock(r.mutex);
        r.requests.clear();
        r.lateErrors.clear();
    }

    detail::WantedFeatures detail::Wanted()
    {
        auto& r = registry();
        std::lock_guard lock(r.mutex);
        WantedFeatures wanted;
        wanted.requests = r.requests;
        for (const auto& request : r.requests) {
            wanted.required |= request.required;
            wanted.optional |= request.optional;
        }
        return wanted;
    }

    void detail::SetDeviceFeatures(const Flags<Feature> available, const Flags<Feature> enabled)
    {
        auto& r = registry();
        std::lock_guard lock(r.mutex);
        r.deviceUp = true;
        r.available = available;
        r.enabled = enabled;
    }

    void detail::ClearDeviceFeatures()
    {
        auto& r = registry();
        std::lock_guard lock(r.mutex);
        r.deviceUp = false;
        r.available = r.enabled = {};
    }

    bool Context::Supports(const Feature feature)
    {
        auto& r = registry();
        std::lock_guard lock(r.mutex);
        return r.enabled & feature;
    }

    bool Context::GpuHas(const Feature feature)
    {
        auto& r = registry();
        std::lock_guard lock(r.mutex);
        return r.available & feature;
    }
}
