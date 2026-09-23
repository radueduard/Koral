//
// Created by radue on 30.07.2026.
//

// Pointing ImGuizmo at the engine's ImGui context.
//
// ImGuizmo is its own static library, so it has its own copy of ImGui's globals — the same situation a
// scene DLL is in, and the reason gui.h has a registrar at all. ImGuizmo cannot be handed those globals
// through that registrar, though, because they are *its* copy and only it can set them: it exposes
// ImGuizmo::SetImGuiContext for exactly this. So it gets its own registration, whose setter forwards
// to that function, and GUI::Init points it at the one real context along with everything else.
//
// Without this, the first ImGuizmo call after GUI::Init dereferences a null context on Windows — and on
// Linux it happens to work, which is the worse failure: it would ship broken.

// imgui.h first, and it matters: ImGuizmo.h uses ImGui's types without including it, and says so with
// an #error — which is the only reason this order is worth a comment.
#include <imgui.h>
#include <ImGuizmo.h>

#include <gui.h>

namespace
{
    // ImGuizmo has no allocator hooks of its own — it allocates through ImGui, whose allocators are set
    // on ImGui's side — so only the context setter does anything here.
    void setImGuizmoAllocators(ImGuiMemAllocFunc, ImGuiMemFreeFunc, void*) {}

    const kor::detail::ImGuiModule kImGuizmoBinding {
        .version = IMGUI_VERSION,
        .sizeOfIO = sizeof(ImGuiIO),
        .sizeOfStyle = sizeof(ImGuiStyle),
        .sizeOfVec2 = sizeof(ImVec2),
        .sizeOfVec4 = sizeof(ImVec4),
        .sizeOfDrawVert = sizeof(ImDrawVert),
        .sizeOfDrawIdx = sizeof(ImDrawIdx),
        .setCurrentContext = &ImGuizmo::SetImGuiContext,
        .setAllocatorFunctions = &setImGuizmoAllocators,
    };

    // Registered as this library loads, exactly as the inline registrar in gui.h does for the library
    // itself. Two registrations from one image is fine: they name different copies of the globals.
    const struct Registrar {
        Registrar() { kor::detail::registerImGuiModule(kImGuizmoBinding); }
    } registrar;
}
