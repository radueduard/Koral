// Guards the recoverable/unrecoverable split that decides whether a resource keeps a copy of its
// builder around to retry with.
//
// This is a memory-safety invariant, not a stylistic one. Builder::materialize() retains the
// builder only when Recoverable is true. A builder that owns the resource's initial data —
// Buffer::Builder<T>::_ownedData is a copy of the buffer's contents, Image::Builder::data is a copy
// of the texture's pixels — would pin that data in host memory for the whole life of the resource
// if it were retained. Buffer::Builder additionally holds a std::span into its own _ownedData, so a
// retained *copy* of it would carry a span into the caller's (usually temporary) builder.
//
// None of that is worth paying for, because none of those failures can be repaired at runtime: a
// failed allocation is not fixed by editing a file. Only shaders and the pipelines built from them
// are repairable, and their builders hold nothing but small, lifetime-tracked inputs.
//
// So: if you ever find yourself marking one of the resource-data builders Recoverable, this test is
// the reason not to.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "buffer.h"
#include "computePipeline.h"
#include "descriptorSet.h"
#include "framebuffer.h"
#include "graphicsPipeline.h"
#include "image.h"
#include "imageView.h"
#include "rayTracingPipeline.h"
#include "sampler.h"
#include "shader.h"

namespace {

// Repairable: a failure here is fixed by editing a source file, and the builder is cheap to keep.
static_assert(kor::Shader::Builder::Recoverable);
static_assert(kor::ComputePipeline::Builder::Recoverable);
static_assert(kor::GraphicsPipeline::Builder::Recoverable);
static_assert(kor::RayTracingPipeline::Builder::Recoverable);

// Not repairable, and expensive or unsafe to retain. These must never keep their builder.
static_assert(!kor::Buffer::RawBuilder::Recoverable);
static_assert(!kor::Buffer::Builder<std::byte>::Recoverable);  // owns a copy of the buffer's data
static_assert(!kor::Image::Builder::Recoverable);              // owns a copy of the texture's pixels
static_assert(!kor::ImageView::Builder::Recoverable);
static_assert(!kor::Sampler::Builder::Recoverable);
static_assert(!kor::Framebuffer::Builder::Recoverable);        // holds raw refs to its attachments
static_assert(!kor::DescriptorSet::Builder::Recoverable);      // holds a raw ref to its layout

TEST(BuilderRecoverable, SplitIsEnforcedAtCompileTime) {
    SUCCEED();  // the static_asserts above are the test
}

// Buffer::Builder used to store a std::span into its own _ownedData. Copying it (or moving it, or
// growing the vector) left that span pointing into the *source's* storage, so a copy that outlived
// its source read freed memory. The view is now derived from _ownedData rather than stored, which
// is what makes a copy independent — this pins that.
TEST(BufferBuilder, CopyDoesNotAliasTheSourcesData) {
    const std::vector<std::uint32_t> data{1, 2, 3, 4};

    auto original = std::make_unique<kor::Buffer::Builder<std::uint32_t>>();
    original->setData(data);  // copies the data in; the view must point at *our* copy

    const kor::Buffer::Builder<std::uint32_t> copy = *original;

    // Kill the source. A stored span would now dangle.
    original.reset();

    ASSERT_EQ(copy.dataView().size(), data.size());
    EXPECT_TRUE(std::ranges::equal(copy.dataView(), data));
}

// A view onto memory the caller owns (setDataView) must stay pointing at the caller's memory, not
// be silently copied into the builder.
TEST(BufferBuilder, ExternalViewSurvivesACopyAndStillAliasesTheCaller) {
    std::vector<std::uint32_t> data{7, 8, 9};

    kor::Buffer::Builder<std::uint32_t> original;
    original.setDataView(std::span<const std::uint32_t>(data));

    const kor::Buffer::Builder<std::uint32_t> copy = original;

    ASSERT_EQ(copy.dataView().size(), data.size());
    EXPECT_EQ(copy.dataView().data(), data.data());  // still the caller's memory, not a copy
}

} // namespace
