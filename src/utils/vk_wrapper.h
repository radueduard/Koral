//
// Created by radue on 3/1/2025.
//

#pragma once

namespace gfx::vk
{
	template <typename U>
	class Wrapper {
	public:
		virtual ~Wrapper() = default;

		U operator*() const { return _handle; }
		const U* operator->() const { return &_handle; }

	protected:
		U _handle;
	};
}