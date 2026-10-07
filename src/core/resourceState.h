//
// The lock over the state resources carry between command buffers.
//

#pragma once

#include <mutex>

namespace kor::detail
{
    /**
     * @brief Held while anything reads or changes the state a resource carries from one command buffer to the next:
     *        what the last End() left it in, which the next resolves against, and (Vulkan) its images' layouts,
     *        which emitting moves along. End() holds it, and so does whatever replaces a resource's storage
     *        (Image::Resize), so neither runs halfway through the other on another thread.
     *
     * Recursive, because emitting can run user code (Run's lambda) that ends a command buffer of its own.
     */
    std::recursive_mutex& ResourceStateMutex();
}
