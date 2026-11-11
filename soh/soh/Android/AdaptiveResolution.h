#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace SohAndroid {

// Reduces the render resolution only after sustained missed refresh intervals, and
// only keeps the reduction if measured frame time improves. The configured scale is
// never changed; callers can restore it by disabling the controller or changing scene.
class AdaptiveResolutionController {
  public:
    float Update(float configuredScale, float frameTimeSeconds, uint32_t targetFps, bool enabled, int sceneId) {
        if (!std::isfinite(configuredScale)) {
            configuredScale = 1.0f;
        }
        configuredScale = std::clamp(configuredScale, 0.5f, 2.0f);

        const bool active = enabled && targetFps >= 30;
        if (!mInitialized || configuredScale != mConfiguredScale || targetFps != mTargetFps || sceneId != mSceneId ||
            active != mActive) {
            Reset(configuredScale, targetFps, sceneId, active);
        }

        if (!active || !std::isfinite(frameTimeSeconds) || frameTimeSeconds <= 0.0f || frameTimeSeconds > 0.25f) {
            ClearWindow();
            if (mEvaluating) {
                // A pause or app switch invalidates the before/after timing comparison.
                mEvaluating = false;
                mEvaluationFrames = 0;
                mEvaluationTime = 0.0f;
            }
            return mScale;
        }

        if (mEvaluating) {
            mEvaluationTime += frameTimeSeconds;
            ++mEvaluationFrames;
            if (mEvaluationFrames >= kEvaluationFrames) {
                const float evaluationAverage = mEvaluationTime / static_cast<float>(mEvaluationFrames);
                // Don't trade image quality for a resolution change that didn't make a
                // measurable difference; that generally means the bottleneck is elsewhere.
                if (evaluationAverage >= mBaselineAverage * (1.0f - kRequiredImprovement)) {
                    mScale = mPreviousScale;
                    mDownscaleWasIneffective = true;
                }
                mEvaluating = false;
                mEvaluationFrames = 0;
                mEvaluationTime = 0.0f;
                ClearWindow();
            }
            return mScale;
        }

        mWindowTime += frameTimeSeconds;
        ++mWindowFrames;
        if (frameTimeSeconds > (1.5f / static_cast<float>(targetFps))) {
            ++mMissedFrames;
        }

        if (mWindowFrames >= kObservationWindowFrames) {
            const float averageFrameTime = mWindowTime / static_cast<float>(mWindowFrames);
            if (mMissedFrames >= kMissedFramesToAct && !mDownscaleWasIneffective) {
                const float minimumScale = std::min(mConfiguredScale, 0.5f);
                const float nextScale = std::max(minimumScale, mScale * kScaleStep);
                if (nextScale < mScale) {
                    mPreviousScale = mScale;
                    mScale = nextScale;
                    mBaselineAverage = averageFrameTime;
                    mEvaluating = true;
                    mEvaluationFrames = 0;
                    mEvaluationTime = 0.0f;
                }
            }
            ClearWindow();
        }

        return mScale;
    }

  private:
    static constexpr uint32_t kObservationWindowFrames = 60;
    static constexpr uint32_t kEvaluationFrames = 120;
    static constexpr uint32_t kMissedFramesToAct = 4;
    static constexpr float kScaleStep = 0.9f;
    static constexpr float kRequiredImprovement = 0.03f;

    void Reset(float configuredScale, uint32_t targetFps, int sceneId, bool active) {
        mConfiguredScale = configuredScale;
        mTargetFps = targetFps;
        mSceneId = sceneId;
        mActive = active;
        mInitialized = true;
        mScale = configuredScale;
        mPreviousScale = configuredScale;
        mBaselineAverage = 0.0f;
        mDownscaleWasIneffective = false;
        mEvaluating = false;
        mEvaluationFrames = 0;
        mEvaluationTime = 0.0f;
        ClearWindow();
    }

    void ClearWindow() {
        mWindowFrames = 0;
        mMissedFrames = 0;
        mWindowTime = 0.0f;
    }

    float mConfiguredScale = 1.0f;
    float mScale = 1.0f;
    float mPreviousScale = 1.0f;
    float mBaselineAverage = 0.0f;
    float mWindowTime = 0.0f;
    float mEvaluationTime = 0.0f;
    uint32_t mTargetFps = 0;
    uint32_t mWindowFrames = 0;
    uint32_t mMissedFrames = 0;
    uint32_t mEvaluationFrames = 0;
    int mSceneId = -1;
    bool mInitialized = false;
    bool mActive = false;
    bool mEvaluating = false;
    bool mDownscaleWasIneffective = false;
};

} // namespace SohAndroid
