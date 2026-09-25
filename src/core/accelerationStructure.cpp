//
// Created by radue on 6/23/2026.
//

#include <accelerationStructure.h>

#include "../backends/vulkan/accelerationStructure.h"

#include "context.h"
#include "window.h"

namespace kor
{
    AccelerationStructure::Builder& AccelerationStructure::Builder::AddMesh(ResourceRef<const Mesh> mesh)
    {
        return AddGeometry(Geometry{ .mesh = mesh });
    }

    AccelerationStructure::Builder& AccelerationStructure::Builder::AddGeometry(const Geometry& geometry)
    {
        this->geometries.push_back(geometry);
        return *this;
    }

    AccelerationStructure::Builder& AccelerationStructure::Builder::AddInstance(const Instance& instance)
    {
        this->instances.push_back(instance);
        return *this;
    }

    kor::Result<std::unique_ptr<AccelerationStructure>> AccelerationStructure::Builder::Create() const
    {
        BeginAttempt();

        for (const auto& geometry : geometries) Adopt(geometry.mesh, "geometry mesh");
        for (const auto& instance : instances)  Adopt(instance.blas, "instance's bottom-level structure");

        if (geometries.empty() == instances.empty())
            AddError(ErrorCode::eInvalidArgument,
                "An acceleration structure must be built from either meshes (bottom-level) or instances (top-level), but not both.");

        if (auto v = Validate(); !v) return std::unexpected(v.error());

        const auto api = Context::ActiveAPI();
        if (api != API::eVulkan)
            return Fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return Guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<AccelerationStructure> {
            return kor::MakeBackendPtr<AccelerationStructure, vk::AccelerationStructure>(*this);
        });
    }

    kor::Resource<AccelerationStructure> AccelerationStructure::Builder::Build(const std::source_location where) const
    {
        return Materialize<AccelerationStructure>(*this, "AccelerationStructure", where);
    }

    AccelerationStructure::AccelerationStructure(const Builder& createInfo)
        : _type(createInfo.instances.empty() ? Type::eBottomLevel : Type::eTopLevel)
    {
    }

    AccelerationStructure::~AccelerationStructure() = default;
}
