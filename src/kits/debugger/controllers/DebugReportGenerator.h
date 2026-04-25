/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2012-2016, Rene Gollent, rene@gollent.com.
 */

/** @file DebugReportGenerator.h
    @brief Asynchronous generator that snapshots a Team's debugger state into a textual report. */

#ifndef DEBUG_REPORT_GENERATOR_H
#define DEBUG_REPORT_GENERATOR_H


#include <Looper.h>

#include "Function.h"
#include "Team.h"
#include "TeamMemoryBlock.h"
#include "ValueNodeContainer.h"


struct entry_ref;
class Architecture;
class AreaInfo;
class BFile;
class DebuggerInterface;
class SemaphoreInfo;
class StackFrame;
class Team;
class Thread;
class UserInterfaceListener;
class Value;
class ValueNode;
class ValueNodeChild;
class ValueNodeManager;


/** @brief BLooper that walks a debugged team and produces a human-readable
           crash/state report covering threads, images, areas, semaphores, and
           per-frame disassembly with resolved variables. */
class DebugReportGenerator : public BLooper, private Team::Listener,
	private TeamMemoryBlock::Listener, private ValueNodeContainer::Listener,
	private Function::Listener {
public:
								DebugReportGenerator(::Team* team,
									UserInterfaceListener* listener,
									DebuggerInterface* interface);
								~DebugReportGenerator();

			status_t			Init();

	static	DebugReportGenerator* Create(::Team* team,
									UserInterfaceListener* listener,
									DebuggerInterface* interface);

	virtual void				MessageReceived(BMessage* message);

private:
	// Team::Listener
	virtual	void				ThreadStackTraceChanged(
									const Team::ThreadEvent& event);

	// TeamMemoryBlock::Listener
	virtual	void				MemoryBlockRetrieved(TeamMemoryBlock* block);
	virtual	void				MemoryBlockRetrievalFailed(
									TeamMemoryBlock* block, status_t result);

	// ValueNodeContainer::Listener
	virtual	void				ValueNodeValueChanged(ValueNode* node);

	// Function::Listener
	virtual	void				FunctionSourceCodeChanged(Function* function);

private:
			status_t			_GenerateReport(const char* outputPath);
			status_t			_GenerateReportHeader(BFile& _output);
			status_t			_DumpLoadedImages(BFile& _output);
			status_t			_DumpAreas(BFile& _output);
			status_t			_DumpSemaphores(BFile& _output);
			status_t			_DumpRunningThreads(BFile& _output);
			status_t			_DumpDebuggedThreadInfo(BFile& _output,
									::Thread* thread);
			status_t			_DumpFunctionDisassembly(BFile& _output,
									target_addr_t instructionPointer);
			status_t			_DumpStackFrameMemory(BFile& _output,
									CpuState* state,
									target_addr_t framePointer,
									uint8 stackDirection);

			status_t			_ResolveValueIfNeeded(ValueNode* node,
									StackFrame* frame, int32 maxDepth);

			void				_HandleMemoryBlockRetrieved(
									TeamMemoryBlock* block, status_t result);

	static	int					_CompareAreas(const AreaInfo* a,
									const AreaInfo* b);
	static	int					_CompareImages(const Image* a, const Image* b);
	static	int					_CompareSemaphores(const SemaphoreInfo* a,
									const SemaphoreInfo* b);
	static	int					_CompareThreads(const ::Thread* a,
									const ::Thread* b);
private:
			::Team*				fTeam;
			Architecture*		fArchitecture;
			DebuggerInterface*	fDebuggerInterface;
			sem_id				fTeamDataSem;
			ValueNodeManager*	fNodeManager;
			UserInterfaceListener* fListener;
			ValueNode*			fWaitingNode;
			TeamMemoryBlock*	fCurrentBlock;
			status_t			fBlockRetrievalStatus;
			::Thread*			fTraceWaitingThread;
			Function*			fSourceWaitingFunction;
};

#endif // DEBUG_REPORT_GENERATOR_H
