//
// Created by radue on 30.07.2026.
//

// How this library announces itself, and nothing else. The same file every module has.

#include "imageCompressModule.h"

// Compression decodes its input through the import module, so it declares the dependency: the loader
// then orders the two, and a missing import module is a startup error naming both of them.
KORAL_DECLARE_MODULE_DEPS(kimg::ImageCompressModule,
    kor::Dependency{ kimg::kImportModuleId, kimg::kImportModuleVersion })
