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
 */

#pragma once

// Foundation & utilities
#include "flags.h"
#include "log.h"
#include "gtime.h"
#include "task.h"
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
