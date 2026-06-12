//
// Created by radue on 1/15/2026.
//

module;

#include "api.h"

export module gfx.time;

import std;

namespace gfx::io {
	class Window;

	export class GFX_API Time {
	public:
		static float FrameTime();
		static float FixedDeltaTime();
		static float WindowTime();


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
