//
// Created by radue on 6/23/2026.
//

#include <accelerationStructure.h>

#include "../backends/vulkan/accelerationStructure.h"

#include "context.h"
#include "window.h"

namespace gfx
{
    AccelerationStructure::Builder& AccelerationStructure::Builder::addMesh(ResourceRef<const Mesh> mesh)
    {
        return addGeometry(Geometry{ .mesh = mesh });
    }

    AccelerationStructure::Builder& AccelerationStructure::Builder::addGeometry(const Geometry& geometry)
    {
        this->geometries.push_back(geometry);
        return *this;
    }

    AccelerationStructure::Builder& AccelerationStructure::Builder::addInstance(const Instance& instance)
    {
        this->instances.push_back(instance);
        return *this;
    }

    gfx::Result<std::unique_ptr<AccelerationStructure>> AccelerationStructure::Builder::create() const
    {
        beginAttempt();

        for (const auto& geometry : geometries) adopt(geometry.mesh, "geometry mesh");
        for (const auto& instance : instances)  adopt(instance.blas, "instance's bottom-level structure");

        if (geometries.empty() == instances.empty())
            addError(ErrorCode::eInvalidArgument,
                "An acceleration structure must be built from either meshes (bottom-level) or instances (top-level), but not both.");

        if (auto v = validate(); !v) return std::unexpected(v.error());

        const auto api = Context::activeAPI();
        if (api == API::eOpenGL)
            return fail(ErrorCode::eRayTracingUnsupported, "Acceleration structures are not supported on the OpenGL backend.");
        if (api != API::eVulkan)
            return fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<AccelerationStructure> {
            return gfx::MakeBackendPtr<AccelerationStructure, vk::AccelerationStructure>(*this);
        });
    }

    gfx::Resource<AccelerationStructure> AccelerationStructure::Builder::build() const
    {
        return materialize<AccelerationStructure>(*this, "AccelerationStructure");
    }

    AccelerationStructure::AccelerationStructure(const Builder& createInfo)
        : _type(createInfo.instances.empty() ? Type::eBottomLevel : Type::eTopLevel)
    {
    }

    AccelerationStructure::~AccelerationStructure() = default;
}
