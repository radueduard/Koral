//
// Created by radue on 29.07.2026.
//

// How this library announces itself, and nothing else. Kept apart from the implementation for the
// same reason a project's export.cpp is kept apart from its scene: this is a fixed, ABI-shaped
// contract with the runtime, while what it hands back is free to be reorganised.
//
// Linking this module is what makes it load, and loading it is what runs the registrar below —
// which is why a project that loads textures has nothing to configure.

#include "imageModule.h"

// No dependencies — reading an image file needs nothing but the engine.
KORAL_DECLARE_MODULE(kimg::ImageImportModule)
