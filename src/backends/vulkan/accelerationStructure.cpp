//
// Created by radue on 6/23/2026.
//
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include "../../core/tokenState.h"
#include "accelerationStructure.h"

#include <vector>

#include <mesh.h>

#include "allocator.h"
#include "buffer.h"
#include "commandBuffer.h"
#include "device.h"
#include "physicalDevice.h"
#include "runtime.h"
#include "vk_enum_conversions.h"
#include "vulkanContext.h"

namespace kor::vk
{
    namespace
    {
        constexpr kor::u64 alignUp(const kor::u64 value, const kor::u64 alignment)
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        std::pair<::vk::Buffer, VmaAllocation> allocateDeviceBuffer(const kor::u64 size, const ::vk::BufferUsageFlags usage)
        {
            const auto bufferInfo = ::vk::BufferCreateInfo()
                .setSize(size)
                .setUsage(usage)
                .setSharingMode(::vk::SharingMode::eExclusive);
            return Context::Allocator().AllocateBuffer(bufferInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
        }

        kor::u32 scratchAlignment()
        {
            ::vk::PhysicalDeviceAccelerationStructurePropertiesKHR asProperties;
            ::vk::PhysicalDeviceProperties2 properties2;
            properties2.pNext = &asProperties;
            (*Context::Runtime().getPhysicalDevice()).getProperties2(&properties2);
            return asProperties.minAccelerationStructureScratchOffsetAlignment;
        }
    }

    AccelerationStructure::AccelerationStructure(const Builder& createInfo) : kor::AccelerationStructure(createInfo)
    {
        // Not every device advertises the ray tracing extensions (see runtime.h); on one that
        // doesn't, vkCreateAccelerationStructureKHR was never resolved by the loader, so calling it
        // below would be a null function pointer rather than a clean Vulkan error.
        if (!Context::Device().supportsRayTracing()) {
            throw std::runtime_error("Cannot build an acceleration structure: this device has no "
                                      "ray tracing support (VK_KHR_acceleration_structure is not "
                                      "available).");
        }

        if (_type == Type::eBottomLevel) {
            buildBottomLevel(createInfo);
        } else {
            buildTopLevel(createInfo);
        }
    }

    AccelerationStructure::~AccelerationStructure()
    {
        Context::DestroyWhenUnused([handle = _handle, buffer = _asBuffer, allocation = _asAllocation] {
            if (handle) Context::Device()->destroyAccelerationStructureKHR(handle);
            if (buffer) Context::Allocator().FreeBuffer(buffer, allocation);
        });
    }

    void AccelerationStructure::buildBottomLevel(const Builder& createInfo)
    {
        // One triangle geometry per mesh. Each mesh must be indexed and expose a
        // float3 position as the first attribute of its binding-0 vertex buffer.
        std::vector<::vk::AccelerationStructureGeometryKHR> geometries;
        std::vector<::vk::AccelerationStructureBuildRangeInfoKHR> rangeInfos;
        std::vector<kor::u32> primitiveCounts;

        for (const auto& geometry : createInfo.geometries) {
            const auto& mesh = geometry.mesh;
            if (!mesh->HasIndexBuffer()) {
                throw std::runtime_error("A mesh used to build an acceleration structure must have an index buffer!");
            }

            // Take the position from the mesh's declared position attribute (its binding,
            // byte offset and format). Fall back to a float3 at the start of binding 0.
            kor::u32 positionBinding = 0;
            kor::u32 positionOffset = 0;
            ::vk::Format positionFormat = ::vk::Format::eR32G32B32Sfloat;
            if (mesh->PositionAttribute().has_value()) {
                const auto& positionAttribute = mesh->PositionAttribute().value();
                positionBinding = positionAttribute.binding;
                positionOffset = positionAttribute.offset;
                positionFormat = getVkFormat(positionAttribute.channelType, positionAttribute.channelCount);
            }

            const auto& vertexBuffer = dynamic_cast<const Buffer&>(*mesh->VertexBuffers()[positionBinding]);
            const auto& indexBuffer = dynamic_cast<const Buffer&>(*mesh->IndexBuffer().value());

            // The mesh's vertex count spans the whole buffer (the heap's capacity for a
            // MeshHeap), so it gives the stride; the geometry's range selects the subset
            // of vertices/indices that belong to this allocation.
            const kor::u64 bufferVertexCount = mesh->VertexCount();
            const kor::u64 vertexStride = vertexBuffer.size() / bufferVertexCount;

            const kor::u64 firstVertex = geometry.firstVertex;
            const kor::u64 vertexCount = geometry.vertexCount != 0
                ? geometry.vertexCount
                : bufferVertexCount - firstVertex;

            const ChannelType indexType = mesh->IndexType().value();
            const kor::u32 indexSize = SizeofChannelType(indexType);
            const kor::u64 firstIndex = geometry.firstIndex;
            const kor::u64 indexCount = geometry.indexCount != 0
                ? geometry.indexCount
                : mesh->IndexCount().value();
            const kor::u32 triangleCount = static_cast<kor::u32>(indexCount / 3);

            const auto triangles = ::vk::AccelerationStructureGeometryTrianglesDataKHR()
                .setVertexFormat(positionFormat)
                .setVertexData(::vk::DeviceOrHostAddressConstKHR().setDeviceAddress(vertexBuffer.DeviceAddress() + positionOffset))
                .setVertexStride(vertexStride)
                .setMaxVertex(static_cast<kor::u32>(firstVertex + vertexCount - 1))
                .setIndexType(getVkIndexType(indexType))
                .setIndexData(::vk::DeviceOrHostAddressConstKHR().setDeviceAddress(indexBuffer.DeviceAddress()));

            geometries.push_back(::vk::AccelerationStructureGeometryKHR()
                .setGeometryType(::vk::GeometryTypeKHR::eTriangles)
                .setGeometry(::vk::AccelerationStructureGeometryDataKHR()
                    .setTriangles(triangles))
                .setFlags(::vk::GeometryFlagBitsKHR::eOpaque));

            // primitiveOffset is a byte offset into the index buffer; firstVertex is added
            // to each (heap-local) index so it lands on this allocation's vertices.
            rangeInfos.push_back(::vk::AccelerationStructureBuildRangeInfoKHR()
                .setPrimitiveCount(triangleCount)
                .setPrimitiveOffset(static_cast<kor::u32>(firstIndex * indexSize))
                .setFirstVertex(static_cast<kor::u32>(firstVertex))
                .setTransformOffset(0));

            primitiveCounts.push_back(triangleCount);
        }

        auto buildInfo = ::vk::AccelerationStructureBuildGeometryInfoKHR()
            .setType(::vk::AccelerationStructureTypeKHR::eBottomLevel)
            .setFlags(::vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace)
            .setMode(::vk::BuildAccelerationStructureModeKHR::eBuild)
            .setGeometries(geometries);

        const auto sizeInfo = Context::Device()->getAccelerationStructureBuildSizesKHR(
            ::vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, primitiveCounts);

        buildAndStore(buildInfo, rangeInfos.data(), sizeInfo, primitiveCounts);
    }

    void AccelerationStructure::buildTopLevel(const Builder& createInfo)
    {
        std::vector<::vk::AccelerationStructureInstanceKHR> vkInstances;
        vkInstances.reserve(createInfo.instances.size());
        for (const auto& instance : createInfo.instances) {
            const auto& blas = dynamic_cast<const AccelerationStructure&>(*instance.blas);

            // kor::Mat4 is column-major; VkTransformMatrixKHR is a row-major 3x4 matrix.
            ::vk::TransformMatrixKHR transform;
            for (kor::u32 row = 0; row < 3; ++row) {
                for (kor::u32 col = 0; col < 4; ++col) {
                    transform.matrix[row][col] = instance.transform[col][row];
                }
            }

            vkInstances.push_back(::vk::AccelerationStructureInstanceKHR()
                .setTransform(transform)
                .setInstanceCustomIndex(instance.instanceCustomIndex)
                .setMask(0xFF)
                .setInstanceShaderBindingTableRecordOffset(instance.hitGroupIndex)
                .setFlags(::vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable)
                .setAccelerationStructureReference(blas.deviceAddress()));
        }

        // Upload the instance descriptions to a host-visible build-input buffer.
        const kor::u64 instancesSize = vkInstances.size() * sizeof(::vk::AccelerationStructureInstanceKHR);
        const auto instanceBufferInfo = ::vk::BufferCreateInfo()
            .setSize(instancesSize)
            .setUsage(::vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR
                      | ::vk::BufferUsageFlagBits::eShaderDeviceAddress)
            .setSharingMode(::vk::SharingMode::eExclusive);
        auto [instanceBuffer, instanceAllocation] = Context::Allocator().AllocateBuffer(
            instanceBufferInfo, VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

        void* mapped = Context::Allocator().MapMemory(instanceAllocation);
        std::memcpy(mapped, vkInstances.data(), instancesSize);
        Context::Allocator().FlushAllocation(instanceAllocation);
        Context::Allocator().UnmapMemory(instanceAllocation);

        const ::vk::DeviceAddress instanceAddress = Context::Device()->getBufferAddress(
            ::vk::BufferDeviceAddressInfo().setBuffer(instanceBuffer));

        const auto instancesData = ::vk::AccelerationStructureGeometryInstancesDataKHR()
            .setArrayOfPointers(false)
            .setData(::vk::DeviceOrHostAddressConstKHR().setDeviceAddress(instanceAddress));

        std::vector<::vk::AccelerationStructureGeometryKHR> geometries = {
            ::vk::AccelerationStructureGeometryKHR()
                .setGeometryType(::vk::GeometryTypeKHR::eInstances)
                .setGeometry(::vk::AccelerationStructureGeometryDataKHR().setInstances(instancesData))
                .setFlags(::vk::GeometryFlagBitsKHR::eOpaque)
        };

        const auto instanceCount = static_cast<kor::u32>(vkInstances.size());
        const std::vector<kor::u32> primitiveCounts = { instanceCount };
        const std::vector<::vk::AccelerationStructureBuildRangeInfoKHR> rangeInfos = {
            ::vk::AccelerationStructureBuildRangeInfoKHR().setPrimitiveCount(instanceCount)
        };

        auto buildInfo = ::vk::AccelerationStructureBuildGeometryInfoKHR()
            .setType(::vk::AccelerationStructureTypeKHR::eTopLevel)
            .setFlags(::vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace)
            .setMode(::vk::BuildAccelerationStructureModeKHR::eBuild)
            .setGeometries(geometries);

        const auto sizeInfo = Context::Device()->getAccelerationStructureBuildSizesKHR(
            ::vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, primitiveCounts);

        buildAndStore(buildInfo, rangeInfos.data(), sizeInfo, primitiveCounts);

        Context::Allocator().FreeBuffer(instanceBuffer, instanceAllocation);
    }

    void AccelerationStructure::buildAndStore(
        ::vk::AccelerationStructureBuildGeometryInfoKHR buildInfo,
        const ::vk::AccelerationStructureBuildRangeInfoKHR* rangeInfos,
        const ::vk::AccelerationStructureBuildSizesInfoKHR& sizeInfo,
        const std::vector<kor::u32>& primitiveCounts)
    {
        std::tie(_asBuffer, _asAllocation) = allocateDeviceBuffer(sizeInfo.accelerationStructureSize,
            ::vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | ::vk::BufferUsageFlagBits::eShaderDeviceAddress);

        const auto createInfo = ::vk::AccelerationStructureCreateInfoKHR()
            .setBuffer(_asBuffer)
            .setSize(sizeInfo.accelerationStructureSize)
            .setType(buildInfo.type);
        _handle = Context::Device()->createAccelerationStructureKHR(createInfo);

        // Scratch space. Over-allocate so the device address can be aligned up.
        const kor::u64 alignment = scratchAlignment();
        auto [scratchBuffer, scratchAllocation] = allocateDeviceBuffer(sizeInfo.buildScratchSize + alignment,
            ::vk::BufferUsageFlagBits::eStorageBuffer | ::vk::BufferUsageFlagBits::eShaderDeviceAddress);
        const ::vk::DeviceAddress scratchAddress = alignUp(
            Context::Device()->getBufferAddress(::vk::BufferDeviceAddressInfo().setBuffer(scratchBuffer)), alignment);

        buildInfo.setDstAccelerationStructure(_handle)
            .setScratchData(::vk::DeviceOrHostAddressKHR().setDeviceAddress(scratchAddress));

        // Not waited for: anything tracing against it — the frame, or a later build of a structure
        // over it — waits for the build on the GPU. The scratch goes once the GPU is past it.
        kor::detail::noteUpload(Context::Device().runSingleTimeCommand([&](kor::vk::CommandBuffer& commandBuffer) {
            commandBuffer->buildAccelerationStructuresKHR(buildInfo, rangeInfos);
        }, ::vk::QueueFlagBits::eCompute));

        Context::DestroyWhenUnused([scratchBuffer, scratchAllocation] {
            Context::Allocator().FreeBuffer(scratchBuffer, scratchAllocation);
        });

        _deviceAddress = Context::Device()->getAccelerationStructureAddressKHR(
            ::vk::AccelerationStructureDeviceAddressInfoKHR().setAccelerationStructure(_handle));
    }
}
