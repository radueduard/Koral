//
// Created by radue on 28.06.2026.
//

/**
 * @file koral.h
 * @brief Umbrella header for the Koral framework: including this pulls in the whole public API.
 *
 * What a scene normally includes. Individual headers can still be included directly to keep a
 * translation unit lean, but note that scene.h pulls in the Dear ImGui binding a scene needs, so a
 * project that reaches for individual headers should include scene.h among them.
 *
 * The API divides into: resources built by builders and held as a kor::Resource (buffers, images,
 * shaders, pipelines, descriptor sets); the kor::CommandBuffer that records a frame's GPU work;
 * kor::Context, kor::Window and kor::Input for the running application; and kor::Scene or kor::Job
 * as the entry point a project implements.
 *
 * ## Naming
 *
 * Case carries meaning here, and it is worth knowing which side of the line a name falls on.
 *
 * **UpperCamel is a verb somebody performs.** Four kinds of thing qualify, and nothing else does:
 *  - commands recorded into a command buffer — `Draw`, `SetViewport`, `BindMesh`, `CopyBuffer`;
 *  - lifecycle hooks the engine calls on you — `Initialize`, `Update`, `Render`, `OnResize`;
 *  - factories that hand back a new object — `Create`, `CreateDefault`, `MakeResource`;
 *  - transfers that move data between host and device — `Read`, `Write`, `Map`, `Flush`.
 *
 * The command buffer is the reason for the split: a frame reads as a list of instructions, and
 * those instructions are easier to pick out when they are cased apart from the questions asked
 * around them.
 *
 * **lowerCamel answers a question or sets a value.** Accessors (`extent`, `bindings`),
 * predicates (`isDefault`, `supportsRayTracing`, `hasDepthAttachment`), queries
 * (`collectTimings`, `sizeOfRegion`, `frameTime`), every builder setter, and everything on a
 * builder up to and including `build()`.
 *
 * Compile-time constants are UpperCamel with no prefix — `kmesh::semantics::Position`,
 * `ProjectConfig::FileName`, `CommandBuffer::MaxTimerScopes` — the same casing as a type, since
 * both are things named rather than things done. A constant local to one translation unit (a test's
 * `kW`, `kCount`) is not API and keeps whatever reads best there.
 *
 * Enumerators take an `e` prefix — `Format::eRGBA8_UNORM`, `Usage::eVertex` — so that an
 * enumerator never collides with a type or a macro. Every public enumeration states its underlying
 * type, which is what makes it forward-declarable; an opaque re-declaration has to repeat that type
 * exactly. Enumerations meant to be combined with `|` opt in through `kor::enable_flags`;
 * @see flags.h.
 *
 * ## Integer types
 *
 * GPU-facing surface speaks glm's scalar aliases — `glm::u32` for counts, indices and extents,
 * `glm::u64` for byte sizes and offsets. The infrastructure underneath it (kor::Resource,
 * kor::Error, kor::log, kor::Task) speaks `std::uintN_t` and `std::size_t` instead, because none of
 * those headers otherwise needs glm and pulling it in for a spelling would be a poor trade. The
 * line is whether the header deals in GPU quantities.
 *
 * Sizes go in signed and come out unsigned, which is deliberate. A builder takes `glm::i64`, so a
 * caller's negative arrives as a negative and is reported — `setRawSize(-1)` fails with a message
 * rather than allocating sixteen exabytes. An accessor returns `glm::u64`, because by then the
 * value is stored and cannot be negative.
 */

#pragma once

// Foundation & utilities
#include "flags.h"
#include "log.h"
#include "gtime.h"
#include "task.h"
// Named here rather than left to arrive through something else: kor::Result and kor::Error are in
// every builder's signature, kor::RangeOf in every upload, kor::ValueShape in PushConstant and
// kor::SemanticSerializer in DescriptorSet::Builder::write. They did reach a project transitively,
// but only because some other header happened to include them — reordering one would have broken a
// build that had not changed.
#include "error.h"
#include "stacktrace.h"
#include "dataRange.h"
#include "shaderValue.h"
#include "semantics.h"
#include "resource.h"
#include "builder.h"
#include "file.h"
#include "structs.h"

// Resources
#include "buffer.h"
#include "image.h"
#include "imageView.h"
#include "bufferView.h"
#include "sampler.h"
// descriptor.h is deliberately absent: kor::Descriptor is the record a descriptor set keeps of what
// is bound where, not something a project constructs. Bind resources through
// DescriptorSet::Builder::write, which takes them as themselves. The header is still reachable for
// anything walking a built set's contents.
#include "descriptorSet.h"
#include "descriptorSetLayout.h"
#include "framebuffer.h"
#include "accelerationStructure.h"

// Geometry
// Asset *import* is not here at all — it is modules, one per kind of asset, and a project includes
// the ones it uses: vertex formats in koral-mesh (<koralMesh.h>), images in the three image modules
// (<koralImageImport.h>, <koralImageExport.h>, <koralImageCompress.h>), and model files in
// koral-model-import (<koralModelImport.h>). What is here is what the engine itself deals in: a mesh
// as buffers, plus the runtime description of how those buffers are laid out.
#include "mesh.h"
#include "vertexLayout.h"

// Shaders & pipelines
#include "shader.h"
#include "pipeline.h"
#include "graphicsPipeline.h"
#include "computePipeline.h"
#include "rayTracingPipeline.h"

// Commands & scheduling
#include "commandBuffer.h"
#include "scheduler.h"

// Context, scene, window, input & GUI
#include "context.h"
#include "module.h"
#include "scene.h"
#include "job.h"
#include "window.h"
#include "input.h"
#include "surface.h"
#include "gui.h"
