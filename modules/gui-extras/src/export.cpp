//
// Created by radue on 30.07.2026.
//

// How this library announces itself, and nothing else. The same file every module has.
//
// Being an ImGui client is what makes the gui.h include necessary: it carries the registrar that hands
// this library the engine's ImGui context and allocators on platforms where globals do not cross a
// library boundary. Same rule as a scene, and the reason a widget drawn from here reaches the same
// context the engine renders.

#include "guiExtrasModule.h"

#include <gui.h>

// The widgets are header-only, so nothing would ever compile them if this file did not: a consumer's
// build would be the first to find a syntax error in them. Included here to make this library's own
// build the first instead.
#include <koralGuiExtras.h>

// No dependencies — widgets need the engine's GUI and nothing else.
KORAL_DECLARE_MODULE(kgui::GuiExtrasModule)
