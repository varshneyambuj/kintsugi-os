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
 * MIT License. Copyright 2009, Ingo Weinhold; Copyright 2011-2016, Rene
 * Gollent.
 */

/** @file Jobs.h
    @brief Declarations of all worker jobs used by the Kintsugi debugger
           (CPU/thread state, stack traces, debug-info loading, source/code
           retrieval, value-node resolve and write, memory I/O, expression
           evaluation, and core-file generation). */

#ifndef JOBS_H
#define JOBS_H

#include <Entry.h>
#include <String.h>

#include "ImageDebugInfoLoadingState.h"
#include "ImageDebugInfoProvider.h"
#include "Types.h"
#include "Worker.h"


class Architecture;
class BVariant;
class CpuState;
class DebuggerInterface;
class ExpressionInfo;
class ExpressionResult;
class Function;
class FunctionInstance;
class Image;
class SourceLanguage;
class StackFrame;
class StackFrameValues;
class Team;
class TeamMemory;
class TeamMemoryBlock;
class TeamTypeInformation;
class Thread;
class Type;
class TypeComponentPath;
class Value;
class ValueLocation;
class ValueNode;
class ValueNodeChild;
class ValueNodeContainer;
class ValueNodeManager;
class Variable;


/** @brief Identifiers used to dedupe jobs in the worker queue by type. */
enum {
	JOB_TYPE_GET_THREAD_STATE,
	JOB_TYPE_GET_CPU_STATE,
	JOB_TYPE_GET_STACK_TRACE,
	JOB_TYPE_LOAD_IMAGE_DEBUG_INFO,
	JOB_TYPE_LOAD_SOURCE_CODE,
	JOB_TYPE_GET_STACK_FRAME_VALUE,
	JOB_TYPE_RESOLVE_VALUE_NODE_VALUE,
	JOB_TYPE_WRITE_VALUE_NODE_VALUE,
	JOB_TYPE_GET_MEMORY_BLOCK,
	JOB_TYPE_WRITE_MEMORY,
	JOB_TYPE_EVALUATE_EXPRESSION,
	JOB_TYPE_WRITE_CORE_FILE
};


/**
 * @brief Determines whether a debugged thread is running or stopped and,
 *        when stopped, captures its CPU register state.
 */
class GetThreadStateJob : public Job {
public:
								GetThreadStateJob(
									DebuggerInterface* debuggerInterface,
									Thread* thread);
	virtual						~GetThreadStateJob();

	virtual	const JobKey&		Key() const;
	virtual	status_t			Do();

private:
			SimpleJobKey		fKey;
			DebuggerInterface*	fDebuggerInterface;
			Thread*				fThread;
};


/**
 * @brief Captures a snapshot of a thread's CPU registers and stores it on
 *        the Thread model when the thread is currently stopped.
 */
class GetCpuStateJob : public Job {
public:
								GetCpuStateJob(
									DebuggerInterface* debuggerInterface,
									::Thread* thread);
	virtual						~GetCpuStateJob();

	virtual	const JobKey&		Key() const;
	virtual	status_t			Do();

private:
			SimpleJobKey		fKey;
			DebuggerInterface*	fDebuggerInterface;
			::Thread*			fThread;
};


/**
 * @brief Builds a stack trace for a thread, lazily loading per-image debug
 *        info as needed via the ImageDebugInfoProvider interface.
 */
class GetStackTraceJob : public Job, private ImageDebugInfoProvider {
public:
								GetStackTraceJob(
									DebuggerInterface* debuggerInterface,
									JobListener* jobListener,
									Architecture* architecture,
									::Thread* thread);
	virtual						~GetStackTraceJob();

	virtual	const JobKey&		Key() const;
	virtual	status_t			Do();

private:
	// ImageDebugInfoProvider
	virtual	status_t			GetImageDebugInfo(Image* image,
									ImageDebugInfo*& _info);

private:
			SimpleJobKey		fKey;
			DebuggerInterface*	fDebuggerInterface;
			JobListener*		fJobListener;
			Architecture*		fArchitecture;
			::Thread*			fThread;
			CpuState*			fCpuState;
};


/**
 * @brief Loads (parses) debug information for a debugged Image and attaches
 *        it to the Image, or marks the Image as unavailable on failure.
 */
class LoadImageDebugInfoJob : public Job {
public:
								LoadImageDebugInfoJob(Image* image);
	virtual						~LoadImageDebugInfoJob();

	virtual	const JobKey&		Key() const;
	virtual	status_t			Do();

	static	status_t			ScheduleIfNecessary(Worker* worker,
									Image* image,
									JobListener* jobListener,
									ImageDebugInfo** _imageDebugInfo = NULL);
										// If already loaded returns a
										// reference, if desired. If not loaded
										// schedules a job, but does not wait;
										// returns B_OK and NULL. An error,
										// if scheduling the job failed, or the
										// debug info already failed to load
										// earlier.

			/** @brief Returns the loading-state object used to surface
			 *         user-input requests during debug-info parsing. */
			ImageDebugInfoLoadingState*
									GetLoadingState()
										{ return &fState; }

private:
			SimpleJobKey		fKey;
			Image*				fImage;
			ImageDebugInfoLoadingState
								fState;
};


/**
 * @brief Loads high-level source for a function and/or disassembles a
 *        FunctionInstance, then attaches the result to the function model.
 */
class LoadSourceCodeJob : public Job {
public:
								LoadSourceCodeJob(
									DebuggerInterface* debuggerInterface,
									Architecture* architecture, Team* team,
									FunctionInstance* functionInstance,
									bool loadForFunction);
	virtual						~LoadSourceCodeJob();

	virtual	const JobKey&		Key() const;
	virtual	status_t			Do();

private:
			SimpleJobKey		fKey;
			DebuggerInterface*	fDebuggerInterface;
			Architecture*		fArchitecture;
			Team*				fTeam;
			FunctionInstance*	fFunctionInstance;
			bool				fLoadForFunction;
};


/**
 * @brief Resolves the location and value of a debugged ValueNode, scheduling
 *        recursive resolve jobs for parent nodes and child locations.
 */
class ResolveValueNodeValueJob : public Job {
public:
								ResolveValueNodeValueJob(
									DebuggerInterface* debuggerInterface,
									Architecture* architecture,
									CpuState* cpuState,
									TeamTypeInformation* typeInformation,
									ValueNodeContainer*	container,
									ValueNode* valueNode);
	virtual						~ResolveValueNodeValueJob();

	virtual	const JobKey&		Key() const;
	virtual	status_t			Do();

private:
			status_t			_ResolveNodeValue();
			status_t			_ResolveNodeChildLocation(
									ValueNodeChild* nodeChild);
			status_t			_ResolveParentNodeValue(ValueNode* parentNode);


private:
			SimpleJobKey		fKey;
			DebuggerInterface*	fDebuggerInterface;
			Architecture*		fArchitecture;
			CpuState*			fCpuState;
			TeamTypeInformation*
								fTypeInformation;
			ValueNodeContainer*	fContainer;
			ValueNode*			fValueNode;
};


/**
 * @brief Writes a new Value back into the storage backing a ValueNode and
 *        updates the node's cached location/value.
 */
class WriteValueNodeValueJob : public Job {
public:
								WriteValueNodeValueJob(
									DebuggerInterface* debuggerInterface,
									Architecture* architecture,
									CpuState* cpuState,
									TeamTypeInformation* typeInformation,
									ValueNode* valueNode,
									Value* newValue);
	virtual						~WriteValueNodeValueJob();

	virtual	const JobKey&		Key() const;
	virtual	status_t			Do();

private:
			SimpleJobKey		fKey;
			DebuggerInterface*	fDebuggerInterface;
			Architecture*		fArchitecture;
			CpuState*			fCpuState;
			TeamTypeInformation*
								fTypeInformation;
			ValueNode*			fValueNode;
			Value*				fNewValue;
};


/**
 * @brief Reads a block of bytes from the debugged team's memory into a
 *        TeamMemoryBlock and tags the block with its protection flags.
 */
class RetrieveMemoryBlockJob : public Job {
public:
								RetrieveMemoryBlockJob(Team* team,
									TeamMemory* teamMemory,
									TeamMemoryBlock* memoryBlock);
	virtual						~RetrieveMemoryBlockJob();

	virtual const JobKey&		Key() const;
	virtual status_t			Do();

private:
			SimpleJobKey		fKey;
			Team*				fTeam;
			TeamMemory*			fTeamMemory;
			TeamMemoryBlock*	fMemoryBlock;
};


/**
 * @brief Writes a buffer to a target address in the debugged team's memory
 *        and notifies the Team so observers can refresh.
 */
class WriteMemoryJob : public Job {
public:
								WriteMemoryJob(Team* team,
									TeamMemory* teamMemory,
									target_addr_t address, void* data,
									target_size_t size);
	virtual						~WriteMemoryJob();

	virtual const JobKey&		Key() const;
	virtual status_t			Do();

private:
			SimpleJobKey		fKey;
			Team*				fTeam;
			TeamMemory*			fTeamMemory;
			target_addr_t		fTargetAddress;
			void*				fData;
			target_size_t		fSize;
};


/**
 * @brief Evaluates a debugger expression in the context of a stack frame and
 *        thread, scheduling value-node resolves on demand.
 */
class ExpressionEvaluationJob : public Job {
public:
								ExpressionEvaluationJob(Team* team,
									DebuggerInterface* debuggerInterface,
									SourceLanguage* language,
									ExpressionInfo* info,
									StackFrame* frame,
									Thread* thread);
	virtual						~ExpressionEvaluationJob();

	virtual	const JobKey&		Key() const;
	virtual	status_t			Do();

			/** @brief Returns the most recent evaluation result, or @c NULL
			 *         if evaluation has not produced a value yet. */
			ExpressionResult*	GetResult() const { return fResultValue; }

private:
			status_t			ResolveNodeValue(ValueNode* node);

private:
			SimpleJobKey		fKey;
			Team*				fTeam;
			DebuggerInterface*	fDebuggerInterface;
			Architecture*		fArchitecture;
			TeamTypeInformation* fTypeInformation;
			SourceLanguage*		fLanguage;
			ExpressionInfo*		fExpressionInfo;
			StackFrame*			fFrame;
			Thread*				fThread;
			ValueNodeManager*	fManager;
			ExpressionResult*	fResultValue;
};


/**
 * @brief Writes a core dump of the debugged team to a target path and
 *        notifies the Team that a core file has been generated.
 */
class WriteCoreFileJob : public Job {
public:
								WriteCoreFileJob(Team* team,
									DebuggerInterface* debuggerInterface,
									const entry_ref& targetPath);
	virtual						~WriteCoreFileJob();

	virtual	const JobKey&		Key() const;
	virtual	status_t			Do();

private:
			SimpleJobKey		fKey;
			Team*				fTeam;
			DebuggerInterface*	fDebuggerInterface;
			entry_ref			fTargetPath;
};


#endif	// JOBS_H
