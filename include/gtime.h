//
// Created by radue on 1/15/2026.
//

#pragma once

#include <cstdint>

#include "api.h"

namespace kor {
	class App;

	/**
	 * @brief One scene's clock.
	 *
	 * Every scene has its own, reached inside it as `Time::` (Scene::Time) and from outside as
	 * Scene::SceneTime(). The application advances every scene's clock once per frame, before the
	 * scene is updated, so every call within one frame sees the same values. Anything that should
	 * move at a constant speed regardless of frame rate scales by FrameTime():
	 *
	 * @code
	 * position += velocity * Time::FrameTime();
	 * @endcode
	 *
	 * A scene's clock can run slow, fast or not at all (SetTimeScale) without touching any other
	 * scene's — a paused game under a pause menu, say.
	 */
	class KORAL_API Time {
	public:
		/**
		 * @brief How long the previous frame took, in seconds, scaled by TimeScale().
		 *
		 * Inside Scene::FixedUpdate, the fixed step instead — so code scaling by it works in either.
		 */
		[[nodiscard]] float FrameTime() const { return _inFixedStep ? _fixedDeltaTime : _frameTime * _timeScale; }

		/** @brief How long the previous frame took, in seconds, whatever the time scale. */
		[[nodiscard]] float UnscaledFrameTime() const { return _frameTime; }

		/**
		 * @brief How long one fixed step is, in seconds of scene time.
		 *
		 * Scene::FixedUpdate runs once per step, as many times a frame as the scene's time has moved
		 * on by — none on a fast frame, several on a slow one — so a simulation advances in steps of
		 * the same size whatever the frame rate. The time scale changes how many steps run, never how
		 * long one is: a scene at half speed takes half as many. 1/60 s unless SetFixedDeltaTime.
		 */
		[[nodiscard]] float FixedDeltaTime() const { return _fixedDeltaTime; }
		/** @brief Sets the fixed step, in seconds; clamped to at least 1/10000 s. */
		void SetFixedDeltaTime(float seconds);

		/**
		 * @brief How far scene time has got past the last fixed step, as a fraction of one: 0 to 1.
		 *
		 * For drawing what the fixed step moves smoothly between its last two states:
		 * `mix(previous, current, Time::FixedStepFraction())`.
		 */
		[[nodiscard]] float FixedStepFraction() const { return _fixedAccumulator / _fixedDeltaTime; }

		/** @brief The most fixed steps one frame runs; a frame further behind drops the rest rather than fall ever further behind. */
		static constexpr std::uint32_t MaxFixedSteps = 8;

		/** @brief Whether Scene::FixedUpdate is running: FrameTime() is then FixedDeltaTime(). */
		[[nodiscard]] bool InFixedStep() const { return _inFixedStep; }

		/** @brief Scaled seconds since the scene was opened. */
		[[nodiscard]] float Elapsed() const { return _elapsed; }

		/** @brief Frames this scene has been updated for. */
		[[nodiscard]] std::uint64_t FrameCount() const { return _frames; }

		/** @brief How fast this scene's time runs: 1 is real time, 0 stops it. */
		[[nodiscard]] float TimeScale() const { return _timeScale; }
		void SetTimeScale(float scale);

	private:
		friend class App;
		/** @brief Moves the clock on by one frame of @p frameTime real seconds. */
		void Advance(float frameTime);

		/**
		 * @brief How many fixed steps the scene's time has moved on by since the last call — at most
		 *        MaxFixedSteps, the rest dropped — taking them off what is owed.
		 */
		std::uint32_t TakeFixedSteps();

		float _frameTime = 0.f;
		float _fixedDeltaTime = 1.f / 60.f;
		float _fixedAccumulator = 0.f;
		bool _inFixedStep = false;
		float _elapsed = 0.f;
		float _timeScale = 1.f;
		std::uint64_t _frames = 0;
	};
}
