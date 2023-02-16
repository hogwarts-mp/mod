// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/* Custom run-loop modes for UE4 that process only certain kinds of events to simulate Windows event ordering. */
CORE_API extern NSString* UE4NilEventMode; /* Process only mandatory events */
CORE_API extern NSString* UE4ShowEventMode; /* Process only show window events */
CORE_API extern NSString* UE4ResizeEventMode; /* Process only resize/move window events */
CORE_API extern NSString* UE4FullscreenEventMode; /* Process only fullscreen mode events */
CORE_API extern NSString* UE4CloseEventMode; /* Process only close window events */
CORE_API extern NSString* UE4IMEEventMode; /* Process only input method events */

@interface NSThread (FCocoaThread)
+ (NSThread*) gameThread; // Returns the main game thread, or nil if has yet to be constructed.
+ (bool) isGameThread; // True if the current thread is the main game thread, else false.
- (bool) isGameThread; // True if this thread object is the main game thread, else false.
@end

@interface FCocoaGameThread : NSThread
- (id)init; // Override that sets the variable backing +[NSThread gameThread], do not override in a subclass.
- (id)initWithTarget:(id)Target selector:(SEL)Selector object:(id)Argument; // Override that sets the variable backing +[NSThread gameThread], do not override in a subclass.
- (void)dealloc; // Override that clears the variable backing +[NSThread gameThread], do not override in a subclass.
- (void)main; // Override that sets the variable backing +[NSRunLoop gameRunLoop], do not override in a subclass.
@end

CORE_API void MainThreadCall(dispatch_block_t Block, NSString* WaitMode = NSDefaultRunLoopMode, bool const bWait = true);

template<typename ReturnType>
ReturnType MainThreadReturn(ReturnType (^Block)(void), NSString* WaitMode = NSDefaultRunLoopMode)
{
	__block ReturnType ReturnValue;
	MainThreadCall(^{ ReturnValue = Block(); }, WaitMode, true);
	return ReturnValue;
}

CORE_API void GameThreadCall(dispatch_block_t Block, NSArray* SendModes = @[ NSDefaultRunLoopMode ], bool const bWait = true);

template<typename ReturnType>
ReturnType GameThreadReturn(ReturnType (^Block)(void), NSArray* SendModes = @[ NSDefaultRunLoopMode ])
{
	__block ReturnType ReturnValue;
	GameThreadCall(^{ ReturnValue = Block(); }, SendModes, true);
	return ReturnValue;
}

CORE_API void RunGameThread(id Target, SEL Selector);

CORE_API void ProcessGameThreadEvents(void);
