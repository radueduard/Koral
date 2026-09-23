//
// Created by radue on 30.07.2026.
//

// How this library announces itself, and nothing else. The same file every module has.

#include "modelImportModule.h"

// No dependencies — deliberately. koralModelMesh.h pours imported geometry into one of the mesh
// module's vertex formats, but it is all templates, compiled into whoever includes it: this library
// never calls kmesh and never needs it loaded. Declaring the dependency here would make every project
// that reads a model file drag in the mesh module, including the ones that only want the CPU-side
// arrays. The bridge header asks for both modules itself. @see koralModelMesh.h
KORAL_DECLARE_MODULE(kmdl::ModelImportModule)
