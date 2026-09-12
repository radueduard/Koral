//
// Created by radue on 1/15/2026.
//

#pragma once

#include "api.h"

namespace kor {
	class Window;
	class Engine;

	/**
	 * @brief Frame timing for the running application.
	 *
	 * The run loop advances these once per frame, before the scene is updated, so every call within
	 * one frame sees the same values. Anything that should move at a constant speed regardless of
	 * frame rate must scale by frameTime():
	 *
	 * @code
	 * position += velocity * kor::Time::frameTime();
	 * @endcode
	 */
	class KORAL_API Time {
	public:
		/**
		 * @brief How long the previous frame took, in seconds.
		 * @return The elapsed time between the last two frame starts. Multiply per-second rates by
		 *         it to make motion frame-rate independent.
		 */
		static float frameTime();

		/**
		 * @brief The interval of the fixed-rate update, in seconds.
		 * @return Time since the last fixed step, which the loop takes at 60 Hz. Intended for
		 *         simulation that needs a steady step rather than a variable one.
		 */
		static float fixedDeltaTime();

		/**
		 * @brief How long the window has been open, in seconds.
		 * @return Seconds accumulated since the first frame. Useful for driving animation from a
		 *         clock rather than from accumulated deltas.
		 */
		static float windowTime();

	private:
		friend class Window;
		friend class Engine;

		static void setup();
		static void update();
	};
}
