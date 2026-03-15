//
// Created by radue on 1/15/2026.
//

#pragma once
#include <chrono>

#include "api.h"

namespace gfx::io {
	class Window;

	class GFX_API Time {
	public:
		static float FrameTime();
		static float FixedDeltaTime();
		static float WindowTime();

		friend class Window;

	private:
		struct GFX_API State
		{
			float timeSinceStart = 0;

			std::chrono::time_point<std::chrono::high_resolution_clock> lastFrameStart;
			float frameDeltaTime;

			std::chrono::time_point<std::chrono::high_resolution_clock> lastFixedPoint;
			float fixedDeltaTime;
			bool shouldRunFixedUpdate = false;

			void setup();
			void update();
		};
	};
}
