//
// Created by radue on 28.06.2026.
//

// Umbrella header for the gfx framework: including this pulls in the whole
// public API. Individual headers can still be included directly if you prefer
// to keep translation units lean.

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
#include "tlsfAllocator.h"

// Resources
#include "buffer.h"
#include "image.h"
#include "imageView.h"
#include "sampler.h"
#include "descriptor.h"
#include "descriptorSet.h"
#include "descriptorSetLayout.h"
#include "framebuffer.h"
#include "accelerationStructure.h"

// Geometry & asset import
#include "mesh.h"
#include "meshType.h"
#include "meshLayout.h"
#include "meshHeap.h"
#include "importer.h"

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
#include "scene.h"
#include "job.h"
#include "window.h"
#include "input.h"
#include "surface.h"
#include "gui.h"
