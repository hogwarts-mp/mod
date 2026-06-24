// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Async/Async.h"
#include "Containers/Array.h"
#include "Misc/AssertionMacros.h"
#include "Templates/Function.h"

template <typename StateType>
class TAsyncWorkSequence
{
public:
	typedef TUniqueFunction<void(StateType& state)> WorkFunctionType;

	TAsyncWorkSequence()
	{
	}

	void AddWork(WorkFunctionType&& Work)
	{
		check(Work);
		check(!bFinalized);

		WorkItems.Emplace(MoveTemp(Work));
	}

	template<typename CallableType>
	auto Finalize(EAsyncExecution Execution, CallableType&& Callable)
	{
		check(!bFinalized);
		bFinalized = true;

		return Async(Execution, [WorkItems = MoveTemp(WorkItems), Callable = MoveTemp(Callable)] () mutable
		{
			StateType State;

			for (WorkFunctionType& Work : WorkItems)
			{
				// Moving the Work capture to a local variable so that after the iteration, any
				// heavyweight data in the capture storage is cleaned up instead of waiting
				// to clean them all up at the end.
				const WorkFunctionType WorkLocal = MoveTemp(Work);
				WorkLocal(State);
			}
			WorkItems.Empty();

			return Callable(State);
		});
	}

private:
	TArray<WorkFunctionType> WorkItems;
	bool bFinalized = false;

};
