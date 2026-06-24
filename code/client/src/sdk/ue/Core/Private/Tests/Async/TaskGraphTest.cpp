// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Misc/Timespan.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformProcess.h"
#include "Stats/Stats.h"
#include "Misc/AutomationTest.h"
#include "Async/TaskGraphInterfaces.h"
#include "Async/ParallelFor.h"
#include "HAL/ThreadHeartBeat.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Math/RandomStream.h"
#include "Containers/CircularQueue.h"
#include "Containers/Queue.h"
#include <atomic>

#if WITH_DEV_AUTOMATION_TESTS

namespace OldTaskGraphTests
{
	static FORCEINLINE void DoWork(const void* Hash, FThreadSafeCounter& Counter, FThreadSafeCounter& Cycles, int32 Work)
	{
		if (Work > 0)
		{
			uint32 CyclesStart = FPlatformTime::Cycles();
			Counter.Increment();
			int32 Sum = 0;
			for (int32 Index = 0; Index < Work; Index++)
			{
				Sum += PointerHash(((const uint64*)Hash) + Index);
			}
			Cycles.Add(FPlatformTime::Cycles() - CyclesStart + (Sum & 1));
		}
		else if (Work == 0)
		{
			Counter.Increment();
		}
	}

	void PrintResult(double& StartTime, double& QueueTime, double& EndTime, FThreadSafeCounter& Counter, FThreadSafeCounter& Cycles, const TCHAR* Message)
	{
		UE_LOG(LogConsoleResponse, Display, TEXT("Total %6.3fms   %6.3fms queue   %6.3fms wait   %6.3fms work   : %s")
			, float(1000.0 * (EndTime - StartTime)), float(1000.0 * (QueueTime - StartTime)), float(1000.0 * (EndTime - QueueTime)), float(FPlatformTime::GetSecondsPerCycle() * double(Cycles.GetValue()) * 1000.0)
			, Message
		);

		Counter.Reset();
		Cycles.Reset();
		StartTime = 0.0;
		QueueTime = 0.0;
		EndTime = 0.0;
	}

	static void TaskGraphBenchmark(const TArray<FString>& Args)
	{
		FSlowHeartBeatScope SuspendHeartBeat;

		double StartTime, QueueTime, EndTime;
		FThreadSafeCounter Counter;
		FThreadSafeCounter Cycles;

		if (!FTaskGraphInterface::IsMultithread())
		{
			UE_LOG(LogConsoleResponse, Display, TEXT("WARNING: TaskGraphBenchmark disabled for non multi-threading platforms"));
			return;
		}

		if (Args.Num() == 1 && Args[0] == TEXT("infinite"))
		{
			while (true)
			{
				{
					StartTime = FPlatformTime::Seconds();

					ParallelFor(1000,
						[&Counter, &Cycles](int32 Index)
						{
							FFunctionGraphTask::CreateAndDispatchWhenReady(
								[&Counter, &Cycles](ENamedThreads::Type CurrentThread, const FGraphEventRef& CompletionEvent)
								{
									DoWork(&CompletionEvent, Counter, Cycles, -1);
								},
								TStatId{}, nullptr, ENamedThreads::GameThread_Local
							);
						}
					);
					QueueTime = FPlatformTime::Seconds();
					FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread_Local);
					EndTime = FPlatformTime::Seconds();
				}
			}
		}
		{
			StartTime = FPlatformTime::Seconds();
			FGraphEventArray Tasks;
			Tasks.Reserve(1000);
			for (int32 Index = 0; Index < 1000; Index++)
			{
				Tasks.Add(FFunctionGraphTask::CreateAndDispatchWhenReady([] {}, TStatId{}, nullptr, ENamedThreads::GameThread_Local));
			}
			QueueTime = FPlatformTime::Seconds();
			FTaskGraphInterface::Get().WaitUntilTasksComplete(MoveTemp(Tasks), ENamedThreads::GameThread_Local);
			EndTime = FPlatformTime::Seconds();
		}
		PrintResult(StartTime, QueueTime, EndTime, Counter, Cycles, TEXT("1000 tasks, ordinary local GT start"));
		{
			StartTime = FPlatformTime::Seconds();
			FGraphEventArray Tasks;
			Tasks.Reserve(1000);
			for (int32 Index = 0; Index < 1000; Index++)
			{
				Tasks.Add(FFunctionGraphTask::CreateAndDispatchWhenReady(
					[&Counter, &Cycles](ENamedThreads::Type CurrentThread, const FGraphEventRef& CompletionEvent)
					{
						DoWork(&CompletionEvent, Counter, Cycles, 100);
					},
					TStatId{}, nullptr, ENamedThreads::GameThread_Local
				));
			}
			QueueTime = FPlatformTime::Seconds();
			FTaskGraphInterface::Get().WaitUntilTasksComplete(MoveTemp(Tasks), ENamedThreads::GameThread_Local);
			EndTime = FPlatformTime::Seconds();
		}
		PrintResult(StartTime, QueueTime, EndTime, Counter, Cycles, TEXT("1000 tasks, ordinary local GT start, with work"));
		{
			StartTime = FPlatformTime::Seconds();
			FGraphEventArray Tasks;
			Tasks.AddZeroed(1000);

			ParallelFor(1000,
				[&Tasks](int32 Index)
				{
					Tasks[Index] = FFunctionGraphTask::CreateAndDispatchWhenReady([] {});
				}
			);
			QueueTime = FPlatformTime::Seconds();
			FTaskGraphInterface::Get().WaitUntilTasksComplete(MoveTemp(Tasks), ENamedThreads::GameThread);
			EndTime = FPlatformTime::Seconds();
		}
		PrintResult(StartTime, QueueTime, EndTime, Counter, Cycles, TEXT("1000 tasks, ParallelFor start"));
		{
			StartTime = FPlatformTime::Seconds();
			FGraphEventArray Tasks;
			Tasks.AddZeroed(10);

			ParallelFor(10,
				[&Tasks](int32 Index)
				{
					FGraphEventArray InnerTasks;
					InnerTasks.AddZeroed(100);
					for (int32 InnerIndex = 0; InnerIndex < 100; InnerIndex++)
					{
						InnerTasks[InnerIndex] = FFunctionGraphTask::CreateAndDispatchWhenReady([] {});
					}
					// join the above tasks
					Tasks[Index] = FFunctionGraphTask::CreateAndDispatchWhenReady([] {}, TStatId{}, &InnerTasks, ENamedThreads::AnyThread);
				}
			);
			QueueTime = FPlatformTime::Seconds();
			FTaskGraphInterface::Get().WaitUntilTasksComplete(MoveTemp(Tasks), ENamedThreads::GameThread);
			EndTime = FPlatformTime::Seconds();
			PrintResult(StartTime, QueueTime, EndTime, Counter, Cycles, TEXT("1000 tasks, ParallelFor start, batched completion 10x100"));
		}

		{
			StartTime = FPlatformTime::Seconds();
			FGraphEventArray Tasks;
			Tasks.AddZeroed(100);

			ParallelFor(100,
				[&Tasks](int32 Index)
				{
					FGraphEventArray InnerTasks;
					InnerTasks.AddZeroed(10);
					for (int32 InnerIndex = 0; InnerIndex < 10; InnerIndex++)
					{
						InnerTasks[InnerIndex] = FFunctionGraphTask::CreateAndDispatchWhenReady([] {});
					}
					// join the above tasks
					Tasks[Index] = FFunctionGraphTask::CreateAndDispatchWhenReady([] {}, TStatId{}, &InnerTasks, ENamedThreads::AnyThread);
				}
			);
			QueueTime = FPlatformTime::Seconds();
			FTaskGraphInterface::Get().WaitUntilTasksComplete(MoveTemp(Tasks), ENamedThreads::GameThread);
			EndTime = FPlatformTime::Seconds();
		}
		PrintResult(StartTime, QueueTime, EndTime, Counter, Cycles, TEXT("1000 tasks, ParallelFor start, batched completion 100x10"));

		{
			StartTime = FPlatformTime::Seconds();

			ParallelFor(1000,
				[&Counter, &Cycles](int32 Index)
				{
					FFunctionGraphTask::CreateAndDispatchWhenReady([&Counter, &Cycles](ENamedThreads::Type CurrentThread, const FGraphEventRef& CompletionEvent) { DoWork(&CompletionEvent, Counter, Cycles, 0); });
				}
			);
			QueueTime = FPlatformTime::Seconds();
			while (Counter.GetValue() < 1000)
			{
				FPlatformMisc::MemoryBarrier();
			}
			EndTime = FPlatformTime::Seconds();
		}
		PrintResult(StartTime, QueueTime, EndTime, Counter, Cycles, TEXT("1000 tasks, ParallelFor, counter tracking"));

		{
			StartTime = FPlatformTime::Seconds();

			static bool Output[1000];
			FPlatformMemory::Memzero(Output, 1000);

			ParallelFor(1000,
				[](int32 Index)
				{
					bool& Out = Output[Index];
					FFunctionGraphTask::CreateAndDispatchWhenReady([&Out] { Out = true; });
				}
			);
			QueueTime = FPlatformTime::Seconds();
			for (int32 Index = 0; Index < 1000; Index++)
			{
				while (!Output[Index])
				{
					FPlatformProcess::Yield();
				}
			}
			EndTime = FPlatformTime::Seconds();
		}
		PrintResult(StartTime, QueueTime, EndTime, Counter, Cycles, TEXT("1000 tasks, ParallelFor, bool* tracking"));

		{
			StartTime = FPlatformTime::Seconds();

			ParallelFor(1000,
				[&Counter, &Cycles](int32 Index)
				{
					FFunctionGraphTask::CreateAndDispatchWhenReady([&Counter, &Cycles](ENamedThreads::Type CurrentThread, const FGraphEventRef& CompletionEvent) { DoWork(&CompletionEvent, Counter, Cycles, 1000); });
				}
			);
			QueueTime = FPlatformTime::Seconds();
			while (Counter.GetValue() < 1000)
			{
				FPlatformProcess::Yield();
			}
			EndTime = FPlatformTime::Seconds();
		}
		PrintResult(StartTime, QueueTime, EndTime, Counter, Cycles, TEXT("1000 tasks, ParallelFor, counter tracking, with work"));
		{
			StartTime = FPlatformTime::Seconds();
			for (int32 Index = 0; Index < 1000; Index++)
			{
				FFunctionGraphTask::CreateAndDispatchWhenReady([&Counter, &Cycles](ENamedThreads::Type CurrentThread, const FGraphEventRef& CompletionEvent) { DoWork(&CompletionEvent, Counter, Cycles, 1000); });
			}
			QueueTime = FPlatformTime::Seconds();
			while (Counter.GetValue() < 1000)
			{
				FPlatformProcess::Yield();
			}
			EndTime = FPlatformTime::Seconds();
		}
		PrintResult(StartTime, QueueTime, EndTime, Counter, Cycles, TEXT("1000 tasks, GT submit, counter tracking, with work"));
		{
			StartTime = FPlatformTime::Seconds();

			ParallelFor(1000,
				[&Counter, &Cycles](int32 Index)
				{
					FFunctionGraphTask::CreateAndDispatchWhenReady(
						[&Counter, &Cycles](ENamedThreads::Type CurrentThread, const FGraphEventRef& CompletionEvent)
						{
							DoWork(&CompletionEvent, Counter, Cycles, -1);
						},
						TStatId{}, nullptr, ENamedThreads::GameThread_Local
					);
				}
			);
			QueueTime = FPlatformTime::Seconds();
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread_Local);
			EndTime = FPlatformTime::Seconds();
		}
		PrintResult(StartTime, QueueTime, EndTime, Counter, Cycles, TEXT("1000 local GT tasks, ParallelFor, no tracking (none needed)"));

		{
			StartTime = FPlatformTime::Seconds();
			QueueTime = StartTime;
			ParallelFor(1000,
				[&Counter, &Cycles](int32 Index)
				{
					DoWork(&Counter, Counter, Cycles, -1);
				}
			);
			EndTime = FPlatformTime::Seconds();
		}
		PrintResult(StartTime, QueueTime, EndTime, Counter, Cycles, TEXT("1000 element do-nothing ParallelFor"));
		{
			StartTime = FPlatformTime::Seconds();
			QueueTime = StartTime;
			ParallelFor(1000,
				[&Counter, &Cycles](int32 Index)
				{
					DoWork(&Counter, Counter, Cycles, 1000);
				}
			);
			EndTime = FPlatformTime::Seconds();
		}
		PrintResult(StartTime, QueueTime, EndTime, Counter, Cycles, TEXT("1000 element ParallelFor, with work"));

		{
			StartTime = FPlatformTime::Seconds();
			QueueTime = StartTime;
			ParallelFor(1000,
				[&Counter, &Cycles](int32 Index)
				{
					DoWork(&Counter, Counter, Cycles, 1000);
				},
				true
			);
			EndTime = FPlatformTime::Seconds();
		}
		PrintResult(StartTime, QueueTime, EndTime, Counter, Cycles, TEXT("1000 element ParallelFor, single threaded, with work"));
	}

	static FAutoConsoleCommand TaskGraphBenchmarkCmd(
		TEXT("TaskGraph.Benchmark"),
		TEXT("Prints the time to run 1000 no-op tasks."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&TaskGraphBenchmark)
	);

	struct FTestStruct
	{
		int32 Index;
		int32 Constant;
		FTestStruct(int32 InIndex)
			: Index(InIndex)
			, Constant(0xfe05abcd)
		{
		}
	};

	struct FTestRigFIFO
	{
		FLockFreePointerFIFOBase<FTestStruct, PLATFORM_CACHE_LINE_SIZE> Test1;
		FLockFreePointerFIFOBase<FTestStruct, 1> Test2;
		FLockFreePointerFIFOBase<FTestStruct, 1, 1 << 4> Test3;
	};

	struct FTestRigLIFO
	{
		FLockFreePointerListLIFOBase<FTestStruct, PLATFORM_CACHE_LINE_SIZE> Test1;
		FLockFreePointerListLIFOBase<FTestStruct, 1> Test2;
		FLockFreePointerListLIFOBase<FTestStruct, 1, 1 << 4> Test3;
	};

	static void TestLockFree(int32 OuterIters = 3)
	{
		FSlowHeartBeatScope SuspendHeartBeat;


		if (!FTaskGraphInterface::IsMultithread())
		{
			UE_LOG(LogConsoleResponse, Display, TEXT("WARNING: TestLockFree disabled for non multi-threading platforms"));
			return;
		}

		const int32 NumWorkers = FTaskGraphInterface::Get().GetNumWorkerThreads();
		// If we have too many threads active at once, they become too slow due to contention.  Set a reasonable maximum for how many are required to guarantee correctness of our LockFreePointers.
		const int32 MaxWorkersForTest = 5;
		const int32 MinWorkersForTest = 2; // With less than two threads we're not testing threading at all, so the test is pointless.
		if (NumWorkers < MinWorkersForTest)
		{
			UE_LOG(LogConsoleResponse, Display, TEXT("WARNING: TestLockFree disabled for current machine because of not enough worker threads.  Need %d, have %d."), MinWorkersForTest, NumWorkers);
			return;
		}

		FScopedDurationTimeLogger DurationLogger(TEXT("TestLockFree Runtime"));
		const uint32 NumWorkersForTest = static_cast<uint32>(FMath::Clamp(NumWorkers, MinWorkersForTest, MaxWorkersForTest));
		auto RunWorkersSynchronous = [NumWorkersForTest](const TFunction<void(uint32)>& WorkerTask)
		{
			FGraphEventArray Tasks;
			for (uint32 Index = 0; Index < NumWorkersForTest; Index++)
			{
				TUniqueFunction<void()> WorkerTaskWithIndex{ [Index, &WorkerTask] { WorkerTask(Index); } };
				Tasks.Add(FFunctionGraphTask::CreateAndDispatchWhenReady(MoveTemp(WorkerTaskWithIndex), TStatId{}, nullptr, ENamedThreads::AnyNormalThreadHiPriTask));
			}
			FTaskGraphInterface::Get().WaitUntilTasksComplete(MoveTemp(Tasks));
		};

		for (int32 Iter = 0; Iter < OuterIters; Iter++)
		{
			{
				UE_LOG(LogTemp, Display, TEXT("******************************* Iter FIFO %d"), Iter);
				FTestRigFIFO Rig;
				for (int32 Index = 0; Index < 1000; Index++)
				{
					Rig.Test1.Push(new FTestStruct(Index));
				}
				TFunction<void(uint32)> Broadcast =
					[&Rig](uint32 WorkerIndex)
				{
					FRandomStream Stream(((int32)WorkerIndex) * 7 + 13);
					for (int32 Index = 0; Index < 1000000; Index++)
					{
						if (Index % 200000 == 1)
						{
							//UE_LOG(LogTemp, Log, TEXT("%8d iters thread=%d"), Index, int32(WorkerIndex));
						}
						if (Stream.FRand() < .03f)
						{
							TArray<FTestStruct*> Items;
							{
								float r = Stream.FRand();
								if (r < .33f)
								{
									Rig.Test1.PopAll(Items);
								}
								else if (r < .66f)
								{
									Rig.Test2.PopAll(Items);
								}
								else
								{
									Rig.Test3.PopAll(Items);
								}
							}
							for (FTestStruct* Item : Items)
							{
								float r = Stream.FRand();
								if (r < .33f)
								{
									Rig.Test1.Push(Item);
								}
								else if (r < .66f)
								{
									Rig.Test2.Push(Item);
								}
								else
								{
									Rig.Test3.Push(Item);
								}
							}
						}
						else
						{
							FTestStruct* Item;
							{
								float r = Stream.FRand();
								if (r < .33f)
								{
									Item = Rig.Test1.Pop();
								}
								else if (r < .66f)
								{
									Item = Rig.Test2.Pop();
								}
								else
								{
									Item = Rig.Test3.Pop();
								}
							}
							if (Item)
							{
								float r = Stream.FRand();
								if (r < .33f)
								{
									Rig.Test1.Push(Item);
								}
								else if (r < .66f)
								{
									Rig.Test2.Push(Item);
								}
								else
								{
									Rig.Test3.Push(Item);
								}
							}
						}
					}
				};
				RunWorkersSynchronous(Broadcast);

				TArray<FTestStruct*> Items;
				Rig.Test1.PopAll(Items);
				Rig.Test2.PopAll(Items);
				Rig.Test3.PopAll(Items);

				checkf(Items.Num() == 1000, TEXT("Items %d"), Items.Num());

				for (int32 LookFor = 0; LookFor < 1000; LookFor++)
				{
					bool bFound = false;
					for (int32 Index = 0; Index < 1000; Index++)
					{
						if (Items[Index]->Index == LookFor && Items[Index]->Constant == 0xfe05abcd)
						{
							check(!bFound);
							bFound = true;
						}
					}
					check(bFound);
				}
				for (FTestStruct* Item : Items)
				{
					delete Item;
				}

				UE_LOG(LogTemp, Display, TEXT("******************************* Pass FTestRigFIFO"));

			}
			{
				UE_LOG(LogTemp, Display, TEXT("******************************* Iter LIFO %d"), Iter);
				FTestRigLIFO Rig;
				for (int32 Index = 0; Index < 1000; Index++)
				{
					Rig.Test1.Push(new FTestStruct(Index));
				}
				TFunction<void(uint32)> Broadcast =
					[&Rig](uint32 WorkerIndex)
				{
					FRandomStream Stream(((int32)WorkerIndex) * 7 + 13);
					for (int32 Index = 0; Index < 1000000; Index++)
					{
						if (Index % 200000 == 1)
						{
							//UE_LOG(LogTemp, Log, TEXT("%8d iters thread=%d"), Index, int32(WorkerIndex));
						}
						if (Stream.FRand() < .03f)
						{
							TArray<FTestStruct*> Items;
							{
								float r = Stream.FRand();
								if (r < .33f)
								{
									Rig.Test1.PopAll(Items);
								}
								else if (r < .66f)
								{
									Rig.Test2.PopAll(Items);
								}
								else
								{
									Rig.Test3.PopAll(Items);
								}
							}
							for (FTestStruct* Item : Items)
							{
								float r = Stream.FRand();
								if (r < .33f)
								{
									Rig.Test1.Push(Item);
								}
								else if (r < .66f)
								{
									Rig.Test2.Push(Item);
								}
								else
								{
									Rig.Test3.Push(Item);
								}
							}
						}
						else
						{
							FTestStruct* Item;
							{
								float r = Stream.FRand();
								if (r < .33f)
								{
									Item = Rig.Test1.Pop();
								}
								else if (r < .66f)
								{
									Item = Rig.Test2.Pop();
								}
								else
								{
									Item = Rig.Test3.Pop();
								}
							}
							if (Item)
							{
								float r = Stream.FRand();
								if (r < .33f)
								{
									Rig.Test1.Push(Item);
								}
								else if (r < .66f)
								{
									Rig.Test2.Push(Item);
								}
								else
								{
									Rig.Test3.Push(Item);
								}
							}
						}
					}
				};
				RunWorkersSynchronous(Broadcast);

				TArray<FTestStruct*> Items;
				Rig.Test1.PopAll(Items);
				Rig.Test2.PopAll(Items);
				Rig.Test3.PopAll(Items);

				checkf(Items.Num() == 1000, TEXT("Items %d"), Items.Num());

				for (int32 LookFor = 0; LookFor < 1000; LookFor++)
				{
					bool bFound = false;
					for (int32 Index = 0; Index < 1000; Index++)
					{
						if (Items[Index]->Index == LookFor && Items[Index]->Constant == 0xfe05abcd)
						{
							check(!bFound);
							bFound = true;
						}
					}
					check(bFound);
				}
				for (FTestStruct* Item : Items)
				{
					delete Item;
				}

				UE_LOG(LogTemp, Display, TEXT("******************************* Pass FTestRigLIFO"));

			}
		}
	}

	static void TestLockFree(const TArray<FString>& Args)
	{
		TestLockFree(10);
	}

	static FAutoConsoleCommand TestLockFreeCmd(
		TEXT("TaskGraph.TestLockFree"),
		TEXT("Test lock free lists"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&TestLockFree)
	);

	static void TestLowToHighPri(const TArray<FString>& Args)
	{
		UE_LOG(LogTemp, Display, TEXT("Starting latency test...."));

		auto ForegroundTask = [](uint64 StartCycles)
		{
			float Latency = float(double(FPlatformTime::Cycles64() - StartCycles) * FPlatformTime::GetSecondsPerCycle64() * 1000.0 * 1000.0);
			//UE_LOG(LogTemp, Display, TEXT("Latency %6.2fus"), Latency);
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Latency %6.2fus\r\n"), Latency);
		};

		auto BackgroundTask = [&ForegroundTask](ENamedThreads::Type CurrentThread, const FGraphEventRef& CompletionEvent)
		{
			while (true)
			{
				uint32 RunningCrc = 0;
				for (int32 Index = 0; Index < 1000000; Index++)
				{
					FCrc::MemCrc32(CompletionEvent.GetReference(), sizeof(FGraphEvent), RunningCrc);
				}
				uint64 StartTime = FPlatformTime::Cycles64();
				FFunctionGraphTask::CreateAndDispatchWhenReady([StartTime, &ForegroundTask] { ForegroundTask(StartTime); }, TStatId{}, nullptr, ENamedThreads::AnyHiPriThreadHiPriTask);
			}
		};

#if 0
		const int NumBackgroundTasks = 32;
		const int NumNormalTasks = 32;
		for (int32 Index = 0; Index < NumBackgroundTasks; Index++)
		{
			FFunctionGraphTask::CreateAndDispatchWhenReady(BackgroundTask, TStatId{}, nullptr, ENamedThreads::AnyNormalThreadNormalTask);
		}
		for (int32 Index = 0; Index < NumNormalTasks; Index++)
		{
			FFunctionGraphTask::CreateAndDispatchWhenReady(BackgroundTask, TStatId{}, nullptr, ENamedThreads::AnyBackgroundThreadNormalTask);
		}
		while (true)
		{
			FPlatformProcess::Sleep(25.0f);
		}
#else
		FFunctionGraphTask::CreateAndDispatchWhenReady(MoveTemp(BackgroundTask), TStatId{}, nullptr, ENamedThreads::AnyBackgroundThreadNormalTask);
#endif
	}

	static FAutoConsoleCommand TestLowToHighPriCmd(
		TEXT("TaskGraph.TestLowToHighPri"),
		TEXT("Test latency of high priority tasks when low priority tasks are saturating the CPU"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&TestLowToHighPri)
	);


	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOldBenchmark, "System.Core.Async.TaskGraph.OldBenchmark", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter);

	bool FOldBenchmark::RunTest(const FString& Parameters)
	{
		TArray<FString> Args;
		TaskGraphBenchmark(Args);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLockFreeTest, "System.Core.Async.TaskGraph.LockFree", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter);

	bool FLockFreeTest::RunTest(const FString& Parameters)
	{
		TestLockFree(3);
		return true;
	}
}

namespace TaskGraphTests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphEventTest, "System.Core.Async.TaskGraph.GraphEventTest", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter);

	bool FGraphEventTest::RunTest(const FString& Parameters)
	{
		{	// task completes before it's waited for
			FGraphEventRef Event = FFunctionGraphTask::CreateAndDispatchWhenReady(
				[]
				{
					//UE_LOG(LogTemp, Log, TEXT("Main task"));
				}
			);
			while (!Event->IsComplete()) /* NOOP */;
			Event->Wait(ENamedThreads::GameThread);
		}

		{	// task completes after it's waited for
			FGraphEventRef Event = FFunctionGraphTask::CreateAndDispatchWhenReady([]()
				{
					//UE_LOG(LogTemp, Log, TEXT("Main task"));
					FPlatformProcess::Sleep(0.1f); // pause for a bit to let waiting start
				}
			);
			check(!Event->IsComplete());
			Event->Wait(ENamedThreads::GameThread);
		}

		{	// event w/o a task, signaled by explicit call to DispatchSubsequents before it's waited for
			FGraphEventRef Event = FGraphEvent::CreateGraphEvent();
			FFunctionGraphTask::CreateAndDispatchWhenReady(
				[&Event]
				{
					Event->DispatchSubsequents();
				}
			);
			while (!Event->IsComplete()) /* NOOP */;
			Event->Wait(ENamedThreads::GameThread);
		}

		{	// event w/o a task, signaled by explicit call to DispatchSubsequents after it's waited for
			FGraphEventRef Event = FGraphEvent::CreateGraphEvent();
			FFunctionGraphTask::CreateAndDispatchWhenReady(
				[&Event]
				{
					FPlatformProcess::Sleep(0.1f); // pause for a bit to let waiting start
					Event->DispatchSubsequents();
				}
			);
			check(!Event->IsComplete());
			Event->Wait(ENamedThreads::GameThread);
		}

		{	// wait for prereq by DontCompleteUntil
			FGraphEventRef Event = FFunctionGraphTask::CreateAndDispatchWhenReady(
				[](ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
				{
					//UE_LOG(LogTemp, Log, TEXT("Main task"));

					FGraphEventRef PrereqHolder = FGraphEvent::CreateGraphEvent();
					PrereqHolder->SetDebugName(TEXT("PrereqHolder"));

					FGraphEventRef Prereq = FFunctionGraphTask::CreateAndDispatchWhenReady(
						[PrereqHolder]
						{
							//UE_LOG(LogTemp, Log, TEXT("Prereq"));

							PrereqHolder->Wait(); // hold it until it's used for `DontCompleteUntil`
						}
					);
					Prereq->SetDebugName(TEXT("Prereq"));

					MyCompletionGraphEvent->DontCompleteUntil(Prereq);
					check(!Prereq->IsComplete()); // check that prereq was incomplete during DontCompleteUntil ^^

					// now that Prereq was registered in DontCompleteUntil, unlock it
					PrereqHolder->DispatchSubsequents();
				}
			);
			Event->SetDebugName(TEXT("MainEvent"));
			check(!Event->IsComplete());
			Event->Wait(ENamedThreads::GameThread);
		}

		{	// prereq is completed when DontCompleteUntil is called
			FGraphEventRef Prereq = FFunctionGraphTask::CreateAndDispatchWhenReady(
				[]
				{
					//UE_LOG(LogTemp, Log, TEXT("Prereq"));
				}
			);
			Prereq->SetDebugName(TEXT("Prereq"));
			Prereq->Wait(ENamedThreads::GameThread);

			FGraphEventRef Event = FFunctionGraphTask::CreateAndDispatchWhenReady(
				[&Prereq](ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
				{
					MyCompletionGraphEvent->DontCompleteUntil(Prereq);
					//UE_LOG(LogTemp, Log, TEXT("Main task"));
				}
			);
			Event->SetDebugName(TEXT("MainEvent"));
			while (!Event->IsComplete()) /* NOOP */;
			Event->Wait(ENamedThreads::GameThread);
		}

		{	// "taskless" event with prereq
			// forget about it, it's illegal as DontCompleteUntil() can be called only in associated task execution context
		}

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTaskGraphRecursionTest, "System.Core.Async.TaskGraph.RecursionTest", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::Disabled);

	bool FTaskGraphRecursionTest::RunTest(const FString& Parameters)
	{
		{	// recursive call on game thread
			FGraphEventRef Event = FFunctionGraphTask::CreateAndDispatchWhenReady(
				[]
				{
					FGraphEventRef Inner = FFunctionGraphTask::CreateAndDispatchWhenReady(
						[]
						{
							check(IsInGameThread());
						},
						TStatId{}, nullptr, ENamedThreads::GameThread
					);
					Inner->Wait(ENamedThreads::GameThread);
				},
				TStatId{}, nullptr, ENamedThreads::GameThread
			);
			Event->Wait(ENamedThreads::GameThread);
		}

		//{	// didn't work in the old version
		//	FGraphEventRef Event = FFunctionGraphTask::CreateAndDispatchWhenReady([] {}, TStatId{}, nullptr, ENamedThreads::GameThread_Local);
		//	Event->Wait(ENamedThreads::GameThread);
		//}

		return true;
	}

	template<uint32 NumRuns, typename TestT>
	void Benchmark(const TCHAR* TestName, TestT&& TestBody)
	{
		UE_LOG(LogTemp, Display, TEXT("\n-------------------------------\n%s"), TestName);
		double MinTime = TNumericLimits<double>::Max();
		double TotalTime = 0;
		for (uint32 RunNo = 0; RunNo != NumRuns; ++RunNo)
		{
			double Time = FPlatformTime::Seconds();
			TestBody();
			Time = FPlatformTime::Seconds() - Time;

			UE_LOG(LogTemp, Display, TEXT("#%d: %f secs"), RunNo, Time);

			TotalTime += Time;
			if (MinTime > Time)
			{
				MinTime = Time;
			}
		}
		UE_LOG(LogTemp, Display, TEXT("min: %f secs, avg: %f secs\n-------------------------------\n"), MinTime, TotalTime / NumRuns);

#if NO_LOGGING
		printf("min: %f\n", MinTime);
#endif
	}

#define BENCHMARK(NumRuns, ...) Benchmark<NumRuns>(TEXT(#__VA_ARGS__), __VA_ARGS__)

	// it's fast because tasks are too lightweight and so are executed almost as fast
	template<int NumTasks>
	void TestPerfBasic()
	{
		int32 CompletedTasks = 0;

		for (int32 TaskIndex = 0; TaskIndex < NumTasks; ++TaskIndex)
		{
			FFunctionGraphTask::CreateAndDispatchWhenReady([&CompletedTasks] { FPlatformAtomics::InterlockedIncrement(&CompletedTasks); });
		}

		while ((CompletedTasks < NumTasks))
		{
			FPlatformProcess::Yield();
		}
	}

	template<int32 NumTasks, int32 BatchSize>
	void TestPerfBatch()
	{
		static_assert(NumTasks % BatchSize == 0, "`NumTasks` must be divisible by `BatchSize`");
		constexpr int32 NumBatches = NumTasks / BatchSize;
		
		int32 CompletedTasks = 0;

		for (int32 BatchIndex = 0; BatchIndex < NumBatches; ++BatchIndex)
		{
			FFunctionGraphTask::CreateAndDispatchWhenReady(
				[&CompletedTasks]
				{
					for (int32 TaskIndex = 0; TaskIndex < BatchSize; ++TaskIndex)
					{
						FFunctionGraphTask::CreateAndDispatchWhenReady([&CompletedTasks] { FPlatformAtomics::InterlockedIncrement(&CompletedTasks); });
					}
				}
			);
		}

		while ((CompletedTasks < NumTasks))
		{
			FPlatformProcess::Yield();
		}
	}

	template<int32 NumTasks, int32 BatchSize>
	void TestPerfBatchOptimised()
	{
		static_assert(NumTasks % BatchSize == 0, "`NumTasks` must be divisible by `BatchSize`");
		constexpr int32 NumBatches = NumTasks / BatchSize;
		
		FGraphEventRef SpawnSignal = FGraphEvent::CreateGraphEvent();
		FGraphEventArray AllDone;

		for (int32 BatchIndex = 0; BatchIndex < NumBatches; ++BatchIndex)
		{
			AllDone.Add(FFunctionGraphTask::CreateAndDispatchWhenReady(
				[] (ENamedThreads::Type CurrentThread, const FGraphEventRef& CompletionEvent)
				{
					FGraphEventRef RunSignal = FGraphEvent::CreateGraphEvent();
					for (int32 TaskIndex = 0; TaskIndex < BatchSize; ++TaskIndex)
					{
						CompletionEvent->DontCompleteUntil(FFunctionGraphTask::CreateAndDispatchWhenReady([] {}, TStatId{}, RunSignal, ENamedThreads::AnyThread));
					}
					RunSignal->DispatchSubsequents();
				},
				TStatId{}, SpawnSignal
			));
		}

		SpawnSignal->DispatchSubsequents();
		FTaskGraphInterface::Get().WaitUntilTasksComplete(MoveTemp(AllDone));
	}

	template<int NumTasks>
	void TestLatency()
	{
		for (uint32 TaskIndex = 0; TaskIndex != NumTasks; ++TaskIndex)
		{
			FGraphEventRef GraphEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([] {});
			GraphEvent->Wait(ENamedThreads::GameThread);
		}
	}

	int64 Fibonacci(int64 N)
	{
		check(N > 0);
		if (N <= 2)
		{
			return 1;
		}
		else
		{
			std::atomic<int64> F1{ -1 };
			std::atomic<int64> F2{ -1 };
			FGraphEventArray GraphEvents;
			GraphEvents.Add(FFunctionGraphTask::CreateAndDispatchWhenReady([&F1, N] { F1 = Fibonacci(N - 1); }));
			GraphEvents.Add(FFunctionGraphTask::CreateAndDispatchWhenReady([&F2, N] { F2 = Fibonacci(N - 2); }));

			FTaskGraphInterface::Get().WaitUntilTasksComplete(MoveTemp(GraphEvents));
			check(F1 > 0 && F2 > 0);

			return F1 + F2;
		}
	}

	FGraphEventRef Fib(int64 N, int64* Res)
	{
		if (N <= 2)
		{
			*Res = 1;
			FGraphEventRef ResEvent = FGraphEvent::CreateGraphEvent();
			ResEvent->DispatchSubsequents();
			return ResEvent;
		}
		else
		{
			TUniquePtr<int64> F1 = MakeUnique<int64>();
			TUniquePtr<int64> F2 = MakeUnique<int64>();

			FGraphEventArray SubTasks;

			auto FibTask = [](int64 N, int64* Res)
			{
				return FFunctionGraphTask::CreateAndDispatchWhenReady
				(
					[N, Res]
					(ENamedThreads::Type, const FGraphEventRef& CompletionEvent)
					{
						FGraphEventRef ResEvent = Fib(N, Res);
						CompletionEvent->DontCompleteUntil(ResEvent);
					}
				);
			};

			SubTasks.Add(FibTask(N - 1, F1.Get()));
			SubTasks.Add(FibTask(N - 2, F2.Get()));

			FGraphEventRef ResEvent = FFunctionGraphTask::CreateAndDispatchWhenReady
			(
				[F1 = MoveTemp(F1), F2 = MoveTemp(F2), Res]
				{
					*Res = *F1 + *F2;
				}, 
				TStatId{}, &SubTasks
			);

			return ResEvent;
		}
	}

	template<int64 N>
	void Fib()
	{
		TUniquePtr<int64> Res = MakeUnique<int64>();
		FGraphEventRef ResEvent = Fib(N, Res.Get());
		ResEvent->Wait();
		UE_LOG(LogTemp, Display, TEXT("Fibonacci(%d) = %d"), N, *Res);
	}

	namespace Queues
	{
		template<uint32 Num>
		void TestTCircularQueue()
		{
			TCircularQueue<uint32> Queue{ 100 };
			std::atomic<bool> bStop{ false };

			FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
				[&bStop, &Queue]
				{
					while (!bStop)
					{
						Queue.Enqueue(0);
					}
				}
				);

			uint32 It = 0;
			while (It != Num)
			{
				uint32 El;
				if (Queue.Dequeue(El))
				{
					++It;
				}
			}

			bStop = true;

			Task->Wait(ENamedThreads::GameThread);
		}

		template<uint32 Num, EQueueMode Mode>
		void TestTQueue()
		{
			TQueue<uint32, Mode> Queue;
			std::atomic<bool> bStop{ false };

			FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
				[&bStop, &Queue]
				{
					while (!bStop)
					{
						Queue.Enqueue(0);
					}
				}
				);

			uint32 It = 0;
			while (It != Num)
			{
				uint32 El;
				if (Queue.Dequeue(El))
				{
					++It;
				}
			}

			bStop = true;

			Task->Wait(ENamedThreads::GameThread);
		}

		template<uint32 Num>
		void TestMpscTQueue()
		{
			TQueue<uint32, EQueueMode::Mpsc> Queue;
			std::atomic<bool> bStop{ false };

			int32 NumProducers = FPlatformMisc::NumberOfCoresIncludingHyperthreads() - 1;
			FGraphEventArray Tasks;
			for (int32 i = 0; i != NumProducers; ++i)
			{
				Tasks.Add(FFunctionGraphTask::CreateAndDispatchWhenReady(
					[&bStop, &Queue]
					{
						while (!bStop)
						{
							Queue.Enqueue(0);
						}
					}
					));
			}

			uint32 It = 0;
			while (It != Num)
			{
				uint32 El;
				if (Queue.Dequeue(El))
				{
					++It;
				}
			}

			bStop = true;

			FTaskGraphInterface::Get().WaitUntilTasksComplete(MoveTemp(Tasks), ENamedThreads::GameThread);
		}

		void Test()
		{
			BENCHMARK(5, TestTCircularQueue<10'000'000>);
			BENCHMARK(5, TestTQueue<10'000'000, EQueueMode::Spsc>);
			BENCHMARK(5, TestTQueue<10'000'000, EQueueMode::Mpsc>);
			BENCHMARK(5, TestMpscTQueue<1'000'000>);
		}
	}

	template<int NumTasks>
	void TestFGraphEventPerf()
	{
		FGraphEventRef Prereq = FGraphEvent::CreateGraphEvent();
		int32 CompletedTasks = 0;

		FGraphEventArray Tasks;
		for (int i = 0; i != NumTasks; ++i)
		{
			Tasks.Add(FFunctionGraphTask::CreateAndDispatchWhenReady(
				[&Prereq, &CompletedTasks](ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
				{
					MyCompletionGraphEvent->DontCompleteUntil(Prereq);
					FPlatformAtomics::InterlockedIncrement(&CompletedTasks);
				}
			));
		}

		Prereq->DispatchSubsequents(ENamedThreads::GameThread);

		FTaskGraphInterface::Get().WaitUntilTasksComplete(MoveTemp(Tasks), ENamedThreads::GameThread);

		check(CompletedTasks == NumTasks);
	}

	template<int NumTasks>
	void TestSpawning()
	{
		{
			FGraphEventArray Tasks;
			Tasks.Reserve(NumTasks);
			double StartTime = FPlatformTime::Seconds();
			for (uint32 TaskNo = 0; TaskNo != NumTasks; ++TaskNo)
			{
				Tasks.Add(FFunctionGraphTask::CreateAndDispatchWhenReady([] {}));
			}

			double Duration = FPlatformTime::Seconds() - StartTime;
			UE_LOG(LogTemp, Display, TEXT("Spawning %d empty trackable tasks took %f secs"), NumTasks, Duration);

			FTaskGraphInterface::Get().WaitUntilTasksComplete(MoveTemp(Tasks));
		}
		{
			double StartTime = FPlatformTime::Seconds();
			for (uint32 TaskNo = 0; TaskNo != NumTasks; ++TaskNo)
			{
				FFunctionGraphTask::CreateAndDispatchWhenReady([] {});
			}

			double Duration = FPlatformTime::Seconds() - StartTime;
			UE_LOG(LogTemp, Display, TEXT("Spawning %d empty non-trackable tasks took %f secs"), NumTasks, Duration);
		}
	}

	template<int NumTasks>
	void TestBatchSpawning()
	{
			double StartTime = FPlatformTime::Seconds();
			FGraphEventRef Trigger = FGraphEvent::CreateGraphEvent();
			for (uint32 TaskNo = 0; TaskNo != NumTasks; ++TaskNo)
			{
				FFunctionGraphTask::CreateAndDispatchWhenReady([] {}, TStatId{}, Trigger);
			}

			double SpawnedTime = FPlatformTime::Seconds();
			Trigger->DispatchSubsequents();

			double EndTime = FPlatformTime::Seconds();
			UE_LOG(LogTemp, Display, TEXT("Spawning %d empty non-trackable tasks took %f secs total, %f secs spawning and %f secs dispatching"), NumTasks, EndTime - StartTime, SpawnedTime - StartTime, EndTime - SpawnedTime);

			//FTaskGraphInterface::Get().WaitUntilTasksComplete(MoveTemp(Tasks));
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerfTest, "System.Core.Async.TaskGraph.PerfTest", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter);

	bool FPerfTest::RunTest(const FString& Parameters)
	{
		// profiling
		//TestBatchSpawning<10000000>();
		//BENCHMARK(5, TestPerfBasic<1 << 25>);
		//BENCHMARK(5, TestPerfOptimised<1 << 24>);
		//BENCHMARK(10, TestFGraphEventPerf<1 << 22>);
		//BENCHMARK(10, TestSpawning<100000000>); // for profiling
		//BENCHMARK(10, Fib<36>); // for profiling

		BENCHMARK(5, Fib<18>);
		//BENCHMARK(5, [] { Fibonacci(15); });

		BENCHMARK(5, TestFGraphEventPerf<1 << 16>);
		BENCHMARK(5, TestPerfBasic<1 << 17>);
		BENCHMARK(5, TestPerfBatch<1 << 17, 1 << 13>);
		BENCHMARK(5, TestPerfBatchOptimised<1 << 17, 1 << 13>);
		BENCHMARK(5, TestLatency<10'000>);

		//BENCHMARK(10, TestSpawning<1>);
		BENCHMARK(5, TestSpawning<100'000>);
		BENCHMARK(5, TestBatchSpawning<100'000>);

		//Queues::Test();

		return true;
	}

#undef BENCHMARK
}
#endif //WITH_DEV_AUTOMATION_TESTS
