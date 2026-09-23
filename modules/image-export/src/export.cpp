//
// Created by radue on 30.07.2026.
//

// How this library announces itself, and nothing else — the same file every module has, named the
// same way. (That it belongs to the image *export* module is a coincidence of naming: this is the
// module-declaration file, as in modules/camera and modules/mesh.)

#include "imageExportModule.h"

// No dependencies — writing an image file needs nothing but the engine.
KORAL_DECLARE_MODULE(kimg::ImageExportModule)
