//
// Created by radue on 10/23/2026.
//

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include "gpuDiagnostics.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <exception>
#include <format>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "device.h"
#include "log.h"
#include "vulkanContext.h"

namespace kor::vk::diag
{
    namespace
    {
        struct Binding {
            std::uint64_t handle;
            VkObjectType type;
            VkDeviceAddress base;
            VkDeviceSize size;
            bool bound;      // false: the memory was unbound here
            bool internal;   // the driver's own, not one of ours
            std::uint64_t frame;
            std::uint64_t sequence;
        };

        struct Described {
            std::string what;
            std::uint64_t createdFrame;
            std::uint64_t freedFrame = 0;   // 0 while alive
        };

        std::mutex mutex;
        std::deque<std::string> interned;
        std::unordered_set<std::string_view> internedIndex;
        std::vector<Binding> bindings;
        std::unordered_map<std::uint64_t, Described> described;
        std::atomic<std::uint64_t> frame{0};
        std::uint64_t sequence = 0;
        // Held for the whole report: a second thread that lost the device too waits for it rather than
        // going on to std::terminate and ending the process halfway through it.
        std::mutex reporting;
        bool reported = false;
        std::terminate_handler previousTerminate = nullptr;

        std::string describe(const std::uint64_t handle, const VkObjectType type) {
            if (const auto it = described.find(handle); it != described.end()) {
                return std::format("{} (made in frame {}{})", it->second.what, it->second.createdFrame,
                                   it->second.freedFrame ? std::format(", freed in frame {}", it->second.freedFrame) : std::string{});
            }
            return std::format("{} {:#x} (not made through the allocator)", ::vk::to_string(static_cast<::vk::ObjectType>(type)), handle);
        }
    }

    bool Enabled() {
        static const bool enabled = std::getenv("KORAL_GPU_DIAGNOSTICS") != nullptr;
        return enabled;
    }

    const char* Intern(const std::string_view text) {
        std::lock_guard lock(mutex);
        if (const auto it = internedIndex.find(text); it != internedIndex.end()) return it->data();
        const auto& kept = interned.emplace_back(text);
        internedIndex.insert(kept);
        return kept.c_str();
    }

    void NoteFrame() { frame.fetch_add(1, std::memory_order_relaxed); }

    void NoteImage(VkImage image, const ::vk::ImageCreateInfo& info) {
        if (!Enabled()) return;
        std::lock_guard lock(mutex);
        described[reinterpret_cast<std::uint64_t>(image)] = {
            .what = std::format("image {:#x} {}x{}x{} {} mips {} layers {} usage {}",
                                reinterpret_cast<std::uint64_t>(image), info.extent.width, info.extent.height, info.extent.depth,
                                ::vk::to_string(info.format), info.mipLevels, info.arrayLayers, ::vk::to_string(info.usage)),
            .createdFrame = frame.load(),
        };
    }

    void NoteBuffer(VkBuffer buffer, const ::vk::BufferCreateInfo& info) {
        if (!Enabled()) return;
        std::lock_guard lock(mutex);
        described[reinterpret_cast<std::uint64_t>(buffer)] = {
            .what = std::format("buffer {:#x} {} bytes usage {}", reinterpret_cast<std::uint64_t>(buffer), info.size,
                                ::vk::to_string(info.usage)),
            .createdFrame = frame.load(),
        };
    }

    void NoteFreed(const std::uint64_t handle) {
        if (!Enabled()) return;
        std::lock_guard lock(mutex);
        if (const auto it = described.find(handle); it != described.end()) it->second.freedFrame = frame.load();
    }

    bool NoteAddressBinding(const VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data) {
        if (!(type & VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT)) return false;
        const auto* next = static_cast<const VkBaseInStructure*>(data->pNext);
        while (next && next->sType != VK_STRUCTURE_TYPE_DEVICE_ADDRESS_BINDING_CALLBACK_DATA_EXT) next = next->pNext;
        if (!next || data->objectCount == 0) return true;
        const auto& binding = *reinterpret_cast<const VkDeviceAddressBindingCallbackDataEXT*>(next);
        std::lock_guard lock(mutex);
        bindings.push_back({
            .handle = data->pObjects[0].objectHandle,
            .type = data->pObjects[0].objectType,
            .base = binding.baseAddress,
            .size = binding.size,
            .bound = binding.bindingType == VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT,
            .internal = (binding.flags & VK_DEVICE_ADDRESS_BINDING_INTERNAL_OBJECT_BIT_EXT) != 0,
            .frame = frame.load(),
            .sequence = sequence++,
        });
        return true;
    }

    void Report(const char* where) {
        if (!Enabled()) return;
        std::lock_guard once(reporting);
        if (reported) return;
        reported = true;
        log::Error("[gpu-diag] ===== device lost ({}) at frame {} =====", where, frame.load());
        const ::vk::Device device = *Context::Device();

        std::vector<std::pair<VkDeviceAddress, VkDeviceAddress>> faults;
        if (const auto getFault = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceFaultInfoEXT) {
            VkDeviceFaultCountsEXT counts{.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
            const auto first = getFault(device, &counts, nullptr);
            std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
            std::vector<VkDeviceFaultVendorInfoEXT> vendors(counts.vendorInfoCount);
            std::vector<std::byte> binary(counts.vendorBinarySize);
            VkDeviceFaultInfoEXT info{.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT,
                                      .pAddressInfos = addresses.data(), .pVendorInfos = vendors.data(),
                                      .pVendorBinaryData = binary.empty() ? nullptr : binary.data()};
            const auto second = getFault(device, &counts, &info);
            log::Error("[gpu-diag] fault info ({} / {}): \"{}\", {} addresses, {} vendor records",
                       ::vk::to_string(static_cast<::vk::Result>(first)), ::vk::to_string(static_cast<::vk::Result>(second)),
                       info.description, counts.addressInfoCount, counts.vendorInfoCount);
            for (const auto& a : addresses) {
                const VkDeviceAddress precision = a.addressPrecision ? a.addressPrecision : 1;
                const VkDeviceAddress lo = a.reportedAddress & ~(precision - 1);
                faults.emplace_back(lo, lo + precision);
                log::Error("[gpu-diag]   address {:#x} ({}), precision {:#x}: [{:#x}, {:#x})", a.reportedAddress,
                           ::vk::to_string(static_cast<::vk::DeviceFaultAddressTypeEXT>(a.addressType)), a.addressPrecision, lo, lo + precision);
            }
            for (const auto& v : vendors)
                log::Error("[gpu-diag]   vendor: \"{}\" code {:#x} data {:#x}", v.description, v.vendorFaultCode, v.vendorFaultData);
        } else {
            log::Error("[gpu-diag] VK_EXT_device_fault not loaded");
        }

        {
            std::lock_guard lock(mutex);
            log::Error("[gpu-diag] {} distinct checkpoint markers were recorded", interned.size());
        }
        if (const auto getCheckpoints = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetQueueCheckpointDataNV) {
            for (const auto queue : Context::Device().queuesForDiagnostics()) {
                std::uint32_t count = 0;
                getCheckpoints(queue, &count, nullptr);
                std::vector<VkCheckpointDataNV> data(count, {.sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV});
                getCheckpoints(queue, &count, data.data());
                log::Error("[gpu-diag] queue {:#x}: {} checkpoints", reinterpret_cast<std::uint64_t>(static_cast<VkQueue>(queue)), count);
                for (const auto& c : data) {
                    // Only markers this process interned are strings; anything else is the driver's.
                    std::lock_guard lock(mutex);
                    const bool ours = std::ranges::any_of(interned, [&](const std::string& t) { return t.c_str() == c.pCheckpointMarker; });
                    log::Error("[gpu-diag]   {}: {} {}", ::vk::to_string(static_cast<::vk::PipelineStageFlagBits>(c.stage)),
                               c.pCheckpointMarker, ours ? static_cast<const char*>(c.pCheckpointMarker) : "(not ours)");
                }
            }
        }

        std::lock_guard lock(mutex);
        log::Error("[gpu-diag] {} address bindings recorded", bindings.size());
        for (const auto& [lo, hi] : faults) {
            // Every range that ever held the fault's addresses, latest last: what had them before, and whether
            // whatever holds them now was bound before the fault or is still unbound.
            std::vector<const Binding*> hits;
            for (const auto& b : bindings)
                if (b.base < hi && lo < b.base + b.size) hits.push_back(&b);
            log::Error("[gpu-diag] [{:#x}, {:#x}) was in {} binding records", lo, hi, hits.size());
            const std::size_t from = hits.size() > 24 ? hits.size() - 24 : 0;
            for (std::size_t i = from; i < hits.size(); ++i) {
                const auto& b = *hits[i];
                log::Error("[gpu-diag]   #{} frame {} {} [{:#x}, {:#x}){} {}", b.sequence, b.frame, b.bound ? "bind  " : "UNBIND",
                           b.base, b.base + b.size, b.internal ? " (driver-internal)" : "", describe(b.handle, b.type));
            }
            // Nearest neighbours, when nothing held it: a read just past a resource's end.
            if (hits.empty()) {
                const Binding* below = nullptr;
                const Binding* above = nullptr;
                for (const auto& b : bindings) {
                    if (b.base + b.size <= lo && (!below || b.base + b.size > below->base + below->size)) below = &b;
                    if (b.base >= hi && (!above || b.base < above->base)) above = &b;
                }
                if (below) log::Error("[gpu-diag]   below: #{} {} ends {:#x}: {}", below->sequence, below->bound ? "bind" : "UNBIND",
                                      below->base + below->size, describe(below->handle, below->type));
                if (above) log::Error("[gpu-diag]   above: #{} {} starts {:#x}: {}", above->sequence, above->bound ? "bind" : "UNBIND",
                                      above->base, describe(above->handle, above->type));
            }
        }
        log::Error("[gpu-diag] ===== end =====");
    }

    void ReportOnTerminate() {
        if (!Enabled() || previousTerminate) return;
        previousTerminate = std::set_terminate([] {
            Report("std::terminate");
            if (previousTerminate) previousTerminate();
            std::abort();
        });
    }
}
