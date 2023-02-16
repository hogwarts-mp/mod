// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericPlatformAffinity.h"

class FLuminAffinity : public FGenericPlatformAffinity
{
public:
	static const CORE_API uint64 GetMainGameMask() {
		return GameThreadMask;
	}

	static const CORE_API uint64 GetRenderingThreadMask() {
		return RenderingThreadMask;
	}

	static const CORE_API uint64 GetRHIThreadMask() {
		return RHIThreadMask;
	}

	static const CORE_API uint64 GetRTHeartBeatMask() {
		return RTHeartBeatMask;
	}

	static const CORE_API uint64 GetPoolThreadMask() {
		return PoolThreadMask;
	}

	static const CORE_API uint64 GetTaskGraphThreadMask() {
		return TaskGraphThreadMask;
	}

	static const CORE_API uint64 GetStatsThreadMask() {
		return StatsThreadMask;
	}

	static const CORE_API uint64 GetAudioThreadMask() {
		return AudioThreadMask;
	}

	static const CORE_API uint64 GetNoAffinityMask() {
		return 0xFFFFFFFFFFFFFFFF;
	}

	static const CORE_API uint64 GetTaskGraphBackgroundTaskMask() {
		return TaskGraphBGTaskMask;
	}

	static EThreadPriority GetRenderingThreadPriority() {
		return TPri_Normal;
	}

	static EThreadPriority GetRHIThreadPriority() {
		return TPri_SlightlyBelowNormal;
	}

public:
	// Default mask definitions in LuminPlatformProcess.cpp
	static int64 GameThreadMask;
	static int64 RenderingThreadMask;
	static int64 RTHeartBeatMask;
	static int64 RHIThreadMask;
	static int64 PoolThreadMask;
	static int64 TaskGraphThreadMask;
	static int64 TaskGraphBGTaskMask;
	static int64 StatsThreadMask;
	static int64 AudioThreadMask;
};

typedef FLuminAffinity FPlatformAffinity;
