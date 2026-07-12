//
// Created by radue on 1/15/2026.
//

#pragma once

#include "api.h"

namespace kor {
	class Window;
	class Engine;

	class KORAL_API Time {
	public:
		static float FrameTime();
		static float FixedDeltaTime();
		static float WindowTime();

	private:
		friend class Window;
		friend class Engine;

		// There is a single window, so time is a process-wide singleton (state
		// lives in time.cpp). Driven by the frame loop via these two hooks.
		static void setup();
		static void update();
	};
}
