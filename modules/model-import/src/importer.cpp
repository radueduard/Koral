//
// Created by radue on 2/23/2026.
//

// Opening a model file, which is all this module's library holds: everything else about an imported
// model is either plain data in the header or a template in koralModelMesh.h.
//
// Assimp lives here rather than in the engine — moving this module out is what took it, and its whole
// tree of format readers, off Koral's link line.

#include <koralModelImport.h>

#include "assimpImporter.h"

namespace kmdl
{
    std::unique_ptr<Importer> Importer::Load(const std::filesystem::path &relativePath) {
        // Resolving the model here is what lets its *textures* resolve too: AssimpImporter reads
        // material texture paths relative to the model file, so it needs the real one, not the
        // relative name the scene asked for.
        auto importer = std::make_unique<AssimpImporter>(kor::AssetPath(relativePath));
        // A file that could not be read, or whose post-processing failed, is no importer at all:
        // handing one back would only move the crash into LoadScene.
        if (!importer->Loaded()) return nullptr;
        return importer;
    }
}
