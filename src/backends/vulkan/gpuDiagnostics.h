//
// Created by radue on 10/23/2026.
//

#pragma once

#include <cstdint>
#include <string_view>
#include <vulkan/vulkan.hpp>

// Finding what a lost device died on, without the Aftermath SDK. On when KORAL_GPU_DIAGNOSTICS is set:
// the device is made with VK_EXT_device_fault (the address the GPU faulted on), NVIDIA's diagnostic
// checkpoints (the last pass each queue reached, one at the start and end of every timer scope, which
// every frame-graph pass opens) and VK_EXT_device_address_binding_report (every image and buffer bound
// and unbound, with its GPU address), and the report a lost device ends in names the resource whose
// memory the fault address is in — and whether that resource was already gone.
namespace kor::vk::diag
{
    [[nodiscard]] bool Enabled();

    // A string that lives until the process ends: a checkpoint marker is a pointer, read back after the fact.
    [[nodiscard]] const char* Intern(std::string_view text);

    void NoteFrame();
    void NoteImage(VkImage image, const ::vk::ImageCreateInfo& info);
    void NoteBuffer(VkBuffer buffer, const ::vk::BufferCreateInfo& info);
    void NoteFreed(std::uint64_t handle);
    // From the debug messenger: true when the message was an address binding, which it then need not log.
    bool NoteAddressBinding(VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data);

    // Logs the fault, each queue's checkpoints and the resources at the fault's addresses. Once.
    void Report(const char* where);
    // Reports before std::terminate goes on: a device lost on a thread that does not catch it ends there.
    void ReportOnTerminate();
}
