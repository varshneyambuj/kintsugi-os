/*
 * Copyright 2026 Kintsugi OS Project. All rights reserved.
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
 * Authors:
 *     Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2012-2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DebugReportGenerator.cpp
 * @brief Asynchronous helper that walks a debugged Team and emits a textual
 *        crash/state report.
 *
 * The generator runs in its own BLooper. _GenerateReport() blocks on a
 * semaphore while it waits for asynchronous data the team must produce
 * (resolved value nodes, retrieved memory blocks, lazily-loaded source) and
 * is woken by the corresponding listener callbacks. The result is a multi-
 * section text report covering the system header, loaded images, areas,
 * semaphores, every thread (including disassembly and stack-frame memory
 * dumps for the focused thread), suitable for pasting into a bug report.
 */


#include "DebugReportGenerator.h"

#include <cpu_type.h>

#include <AutoLocker.h>
#include <Entry.h>
#include <File.h>
#include <Path.h>
#include <StringForSize.h>

#include "Architecture.h"
#include "AreaInfo.h"
#include "AutoDeleter.h"
#include "CpuState.h"
#include "DebuggerInterface.h"
#include "DisassembledCode.h"
#include "FunctionInstance.h"
#include "Image.h"
#include "ImageDebugInfo.h"
#include "MessageCodes.h"
#include "Register.h"
#include "SemaphoreInfo.h"
#include "StackFrame.h"
#include "StackTrace.h"
#include "Statement.h"
#include "SystemInfo.h"
#include "Team.h"
#include "TeamDebugInfo.h"
#include "Thread.h"
#include "Type.h"
#include "UiUtils.h"
#include "UserInterface.h"
#include "Value.h"
#include "ValueLoader.h"
#include "ValueLocation.h"
#include "ValueNode.h"
#include "ValueNodeManager.h"


/**
 * @brief Writes @a data into @a output and returns from the enclosing function
 *        if the write fails. Used everywhere a section emits text.
 */
#define WRITE_AND_CHECK(output, data) \
	{ \
		ssize_t error = output.Write(data.String(), data.Length()); \
		if (error < 0) \
			return error; \
	}


/**
 * @brief Constructs the generator and registers it as a listener of the team
 *        and architecture it will report on.
 *
 * @param team       Team to report on; the generator becomes a Team::Listener.
 * @param listener   UI-level listener used to drive on-demand operations such
 *                   as stack-trace and source-code resolution.
 * @param interface  Debugger interface used for memory/area/semaphore queries.
 */
DebugReportGenerator::DebugReportGenerator(::Team* team,
	UserInterfaceListener* listener, DebuggerInterface* interface)
	:
	BLooper("DebugReportGenerator"),
	fTeam(team),
	fArchitecture(team->GetArchitecture()),
	fDebuggerInterface(interface),
	fTeamDataSem(-1),
	fNodeManager(NULL),
	fListener(listener),
	fWaitingNode(NULL),
	fCurrentBlock(NULL),
	fBlockRetrievalStatus(B_OK),
	fTraceWaitingThread(NULL),
	fSourceWaitingFunction(NULL)
{
	fTeam->AddListener(this);
	fArchitecture->AcquireReference();
}


/**
 * @brief Tears down the generator after detaching from the team and dropping
 *        any in-flight memory block, semaphore, and value-node-manager
 *        references.
 */
DebugReportGenerator::~DebugReportGenerator()
{
	fTeam->RemoveListener(this);
	fArchitecture->ReleaseReference();
	if (fNodeManager != NULL) {
		fNodeManager->RemoveListener(this);
		fNodeManager->ReleaseReference();
	}

	if (fCurrentBlock != NULL)
		fCurrentBlock->ReleaseReference();

	if (fTeamDataSem >= 0)
		delete_sem(fTeamDataSem);
}


/**
 * @brief Allocates the wait semaphore and value-node manager and starts the looper.
 *
 * @return B_OK on success, an error from create_sem(), or B_NO_MEMORY if the
 *         value-node manager cannot be allocated.
 */
status_t
DebugReportGenerator::Init()
{
	fTeamDataSem = create_sem(0, "debug_controller_data_wait");
	if (fTeamDataSem < B_OK)
		return fTeamDataSem;

	fNodeManager = new(std::nothrow) ValueNodeManager();
	if (fNodeManager == NULL)
		return B_NO_MEMORY;

	fNodeManager->AddListener(this);

	Run();

	return B_OK;
}


/**
 * @brief Convenience factory that constructs and initializes a generator in one step.
 *
 * @param team       Team to report on.
 * @param listener   UI-level listener used during report generation.
 * @param interface  Debugger interface used for off-team queries.
 * @return Newly-allocated generator running on its own looper.
 * @note Caller takes ownership; rethrows on failure after deleting the half-built object.
 */
DebugReportGenerator*
DebugReportGenerator::Create(::Team* team, UserInterfaceListener* listener,
	DebuggerInterface* interface)
{
	DebugReportGenerator* self = new DebugReportGenerator(team, listener,
		interface);

	try {
		self->Init();
	} catch (...) {
		delete self;
		throw;
	}

	return self;
}


/**
 * @brief Drives the full report generation pipeline against @a outputPath.
 *
 * Truncates or creates the output file, then emits the header, running-thread
 * dump (which carries the heaviest payload), image list, areas, and
 * semaphores. On success the team is notified so any UI listening for the
 * outcome can refresh.
 *
 * @param outputPath  Filesystem path to write the report to.
 * @return B_OK on success, the InitCheck() error from the BFile, or any
 *         section-level error if a dump helper fails.
 */
status_t
DebugReportGenerator::_GenerateReport(const char* outputPath)
{
	BFile file(outputPath, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t result = file.InitCheck();
	if (result != B_OK)
		return result;

	result = _GenerateReportHeader(file);
	if (result != B_OK)
		return result;

	result = _DumpRunningThreads(file);
	if (result != B_OK)
		return result;

	result = _DumpLoadedImages(file);
	if (result != B_OK)
		return result;

	result = _DumpAreas(file);
	if (result != B_OK)
		return result;

	result = _DumpSemaphores(file);
	if (result != B_OK)
		return result;

	AutoLocker< ::Team> teamLocker(fTeam);
	fTeam->NotifyDebugReportChanged(outputPath, B_OK);

	return B_OK;
}


/**
 * @brief BLooper hook: handles MSG_GENERATE_DEBUG_REPORT requests.
 *
 * @param message  Incoming message; for MSG_GENERATE_DEBUG_REPORT the "target"
 *                 entry_ref names the output file. Other codes are forwarded
 *                 to BLooper.
 */
void
DebugReportGenerator::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_GENERATE_DEBUG_REPORT:
		{
			entry_ref ref;
			if (message->FindRef("target", &ref) == B_OK) {
				BPath path(&ref);
				status_t error = _GenerateReport(path.Path());
				if (error != B_OK)
					fTeam->NotifyDebugReportChanged(path.Path(), error);
			}
			break;
		}

		default:
			BLooper::MessageReceived(message);
			break;
	}
}


/**
 * @brief Team listener hook: wakes the generator when the awaited stack trace arrives.
 *
 * @param event  Stack-trace-change event delivered by the Team.
 */
void
DebugReportGenerator::ThreadStackTraceChanged(const ::Team::ThreadEvent& event)
{
	if (fTraceWaitingThread == event.GetThread()) {
		fTraceWaitingThread = NULL;
		release_sem(fTeamDataSem);
	}
}


/**
 * @brief Memory-block listener hook: forwards a successful retrieval to the
 *        common handler.
 *
 * @param block  Block whose contents have been read from the team.
 */
void
DebugReportGenerator::MemoryBlockRetrieved(TeamMemoryBlock* block)
{
	_HandleMemoryBlockRetrieved(block, B_OK);
}


/**
 * @brief Memory-block listener hook: forwards a failed retrieval with status code.
 *
 * @param block   Block whose retrieval failed.
 * @param result  Failure status code.
 */
void
DebugReportGenerator::MemoryBlockRetrievalFailed(TeamMemoryBlock* block,
	status_t result)
{
	_HandleMemoryBlockRetrieved(block, result);
}


/**
 * @brief Value-node listener hook: wakes the generator when the awaited node resolves.
 *
 * @param node  Value node whose value just became available.
 */
void
DebugReportGenerator::ValueNodeValueChanged(ValueNode* node)
{
	if (node == fWaitingNode) {
		fWaitingNode = NULL;
		release_sem(fTeamDataSem);
	}
}


/**
 * @brief Function listener hook: wakes the generator once source-code state
 *        reaches a terminal value (loaded, suppressed, or unavailable).
 *
 * @param function  Function whose source-code state has changed.
 */
void
DebugReportGenerator::FunctionSourceCodeChanged(Function* function)
{
	AutoLocker< ::Team> teamLocker(fTeam);
	if (function == fSourceWaitingFunction) {
		function_source_state state = function->SourceCodeState();

		switch (state) {
			case FUNCTION_SOURCE_LOADED:
			case FUNCTION_SOURCE_SUPPRESSED:
			case FUNCTION_SOURCE_UNAVAILABLE:
			{
				fSourceWaitingFunction->RemoveListener(this);
				fSourceWaitingFunction = NULL;
				release_sem(fTeamDataSem);
				// fall through
			}
			default:
				break;
		}
	}
}

/**
 * @brief Emits the report's preamble: team name/id, CPU description, memory
 *        usage, and host system identification.
 *
 * @param _output  File to write to.
 * @return B_OK on success, an error from any failed write, or B_NO_MEMORY if
 *         the CPU topology buffer cannot be allocated.
 */
status_t
DebugReportGenerator::_GenerateReportHeader(BFile& _output)
{
	AutoLocker< ::Team> locker(fTeam);

	BString data;
	data.SetToFormat("Debug information for team %s (%" B_PRId32 "):\n",
		fTeam->Name(), fTeam->ID());
	WRITE_AND_CHECK(_output, data);

	cpu_platform platform = B_CPU_UNKNOWN;
	cpu_vendor cpuVendor = B_CPU_VENDOR_UNKNOWN;
	uint32 cpuModel = 0;
	uint32 topologyNodeCount = 0;
	cpu_topology_node_info* topology = NULL;
	get_cpu_topology_info(NULL, &topologyNodeCount);
	if (topologyNodeCount != 0) {
		topology = new(std::nothrow) cpu_topology_node_info[topologyNodeCount];
		if (topology == NULL)
			return B_NO_MEMORY;

		BPrivate::ArrayDeleter<cpu_topology_node_info> deleter(topology);
		get_cpu_topology_info(topology, &topologyNodeCount);

		for (uint32 i = 0; i < topologyNodeCount; i++) {
			switch (topology[i].type) {
				case B_TOPOLOGY_ROOT:
					platform = topology[i].data.root.platform;
					break;

				case B_TOPOLOGY_PACKAGE:
					cpuVendor = topology[i].data.package.vendor;
					break;

				case B_TOPOLOGY_CORE:
					cpuModel = topology[i].data.core.model;
					break;

				default:
					break;
			}
		}
	}

	SystemInfo sysInfo;
	if (fDebuggerInterface->GetSystemInfo(sysInfo) == B_OK) {
		const system_info &info = sysInfo.GetSystemInfo();
		const char* vendor = get_cpu_vendor_string(cpuVendor);
		const char* model = get_cpu_model_string(platform, cpuVendor, cpuModel);

		data.SetToFormat("CPU(s): %" B_PRId32 "x %s %s\n",
			info.cpu_count, vendor != NULL ? vendor : "unknown",
			model != NULL ? model : "unknown");
		WRITE_AND_CHECK(_output, data);

		char maxSize[32];
		char usedSize[32];

		data.SetToFormat("Memory: %s total, %s used\n",
			BPrivate::string_for_size((int64)info.max_pages * B_PAGE_SIZE,
				maxSize, sizeof(maxSize)),
			BPrivate::string_for_size((int64)info.used_pages * B_PAGE_SIZE,
				usedSize, sizeof(usedSize)));
		WRITE_AND_CHECK(_output, data);

		const utsname& name = sysInfo.GetSystemName();
		data.SetToFormat("Haiku revision: %s (%s)\n", name.version,
			name.machine);
		WRITE_AND_CHECK(_output, data);
	}

	return B_OK;
}


/**
 * @brief Emits a sorted table of every image currently loaded in the team.
 *
 * @param _output  File to write to.
 * @return B_OK on success, or any error from the underlying write calls.
 */
status_t
DebugReportGenerator::_DumpLoadedImages(BFile& _output)
{
	AutoLocker< ::Team> locker(fTeam);

	BString data("\nLoaded Images:\n");
	WRITE_AND_CHECK(_output, data);
	BObjectList<Image> images;
	for (ImageList::ConstIterator it = fTeam->Images().GetIterator();
		 Image* image = it.Next();) {
		 images.AddItem(image);
	}

	images.SortItems(&_CompareImages);

	Image* image = NULL;
	data.SetToFormat("\tID\t\tText Base\tText End\tData Base\tData"
		" End\tType\tName\n\t");
	WRITE_AND_CHECK(_output, data);
	data.Truncate(0L);
	data.Append('-', 80);
	data.Append("\n");
	WRITE_AND_CHECK(_output, data);
	for (int32 i = 0; (image = images.ItemAt(i)) != NULL; i++) {
		const ImageInfo& info = image->Info();
		char buffer[32];
		try {
			target_addr_t textBase = info.TextBase();
			target_addr_t dataBase = info.DataBase();

			data.SetToFormat("\t%" B_PRId32 "\t0x%08" B_PRIx64 "\t"
				"0x%08" B_PRIx64 "\t0x%08" B_PRIx64 "\t0x%08" B_PRIx64 "\t"
				"%-7s\t%s\n", info.ImageID(), textBase, textBase + info.TextSize(),
				dataBase, dataBase + info.DataSize(),
				UiUtils::ImageTypeToString(info.Type(), buffer,
					sizeof(buffer)), info.Name().String());

			WRITE_AND_CHECK(_output, data);
		} catch (...) {
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


/**
 * @brief Emits a sorted table of every area mapped in the team.
 *
 * @param _output  File to write to.
 * @return B_OK on success, an error from GetAreaInfos(), or any write error.
 */
status_t
DebugReportGenerator::_DumpAreas(BFile& _output)
{
	BObjectList<AreaInfo, true> areas(20);
	status_t result = fDebuggerInterface->GetAreaInfos(areas);
	if (result != B_OK)
		return result;

	areas.SortItems(&_CompareAreas);

	BString data("\nAreas:\n");
	WRITE_AND_CHECK(_output, data);
	data.SetToFormat("\tID\t\tBase\t\tEnd\t\t\tSize (KiB)\tProtection\tLocking\t\t\tName\n\t");
	WRITE_AND_CHECK(_output, data);
	data.Truncate(0L);
	data.Append('-', 80);
	data.Append("\n");
	WRITE_AND_CHECK(_output, data);
	AreaInfo* info;
	BString protectionBuffer;
	char lockingBuffer[32];
	for (int32 i = 0; (info = areas.ItemAt(i)) != NULL; i++) {
		try {
			data.SetToFormat("\t%" B_PRId32 "\t0x%08" B_PRIx64 "\t"
				"0x%08" B_PRIx64 "\t%10" B_PRId64 "\t%-11s\t%-14s\t%s\n",
				info->AreaID(), info->BaseAddress(), info->BaseAddress()
					+ info->Size(), info->Size() / 1024,
				UiUtils::AreaProtectionFlagsToString(info->Protection(),
					protectionBuffer).String(),
				UiUtils::AreaLockingFlagsToString(info->Lock(), lockingBuffer,
					sizeof(lockingBuffer)), info->Name().String());

			WRITE_AND_CHECK(_output, data);
		} catch (...) {
			return B_NO_MEMORY;
		}
	}

	data = "\nProtection Flags: r - read, w - write, x - execute, "
		"s - stack, o - overcommit, c - cloneable, S - shared, k - kernel\n";
	WRITE_AND_CHECK(_output, data);

	return B_OK;
}


/**
 * @brief Emits a sorted table of every semaphore owned by the team.
 *
 * @param _output  File to write to.
 * @return B_OK on success, an error from GetSemaphoreInfos(), or any write error.
 */
status_t
DebugReportGenerator::_DumpSemaphores(BFile& _output)
{
	BObjectList<SemaphoreInfo, true> semaphores(20);
	status_t error = fDebuggerInterface->GetSemaphoreInfos(semaphores);
	if (error != B_OK)
		return error;

	semaphores.SortItems(&_CompareSemaphores);

	BString data = "\nSemaphores:\n";
	WRITE_AND_CHECK(_output, data);
	data.SetToFormat("\tID\t\tCount\tLast Holder\tName\n\t");
	WRITE_AND_CHECK(_output, data);
	data.Truncate(0L);
	data.Append('-', 60);
	data.Append("\n");
	WRITE_AND_CHECK(_output, data);
	SemaphoreInfo* info;
	for (int32 i = 0; (info = semaphores.ItemAt(i)) != NULL; i++) {
		try {
			data.SetToFormat("\t%" B_PRId32 "\t%5" B_PRId32 "\t%11" B_PRId32
				"\t%s\n", info->SemID(), info->Count(),
				info->LatestHolder(), info->Name().String());

			WRITE_AND_CHECK(_output, data);
		} catch (...) {
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


/**
 * @brief Emits a per-thread block listing every active thread, with a deep
 *        dump for any thread that is currently stopped.
 *
 * @param _output  File to write to.
 * @return B_OK on success, B_NO_MEMORY on string allocation failure, or any
 *         error returned by _DumpDebuggedThreadInfo().
 */
status_t
DebugReportGenerator::_DumpRunningThreads(BFile& _output)
{
	AutoLocker< ::Team> locker(fTeam);

	BString data("\nActive Threads:\n");
	WRITE_AND_CHECK(_output, data);
	BObjectList< ::Thread> threads;
	::Thread* thread;
	for (ThreadList::ConstIterator it = fTeam->Threads().GetIterator();
		  (thread = it.Next());) {
		 threads.AddItem(thread);
		 thread->AcquireReference();
	}

	status_t error = B_OK;
	threads.SortItems(&_CompareThreads);
	for (int32 i = 0; (thread = threads.ItemAt(i)) != NULL; i++) {
		try {
			data.SetToFormat("\tthread %" B_PRId32 ": %s %s\n", thread->ID(),
					thread->Name(), thread->IsMainThread()
						? "(main)" : "");
			WRITE_AND_CHECK(_output, data);

			if (thread->State() == THREAD_STATE_STOPPED) {
				data.SetToFormat("\t\tstate: %s",
					UiUtils::ThreadStateToString(thread->State(),
							thread->StoppedReason()));
				const BString& stoppedInfo = thread->StoppedReasonInfo();
				if (stoppedInfo.Length() != 0)
					data << " (" << stoppedInfo << ")";
				data << "\n\n";
				WRITE_AND_CHECK(_output, data);

				// we need to release our lock on the team here
				// since we might need to block and wait
				// on the stack trace.
				locker.Unlock();
				error = _DumpDebuggedThreadInfo(_output, thread);
				locker.Lock();
				if (error != B_OK)
					break;
			}
		} catch (...) {
			error = B_NO_MEMORY;
		}
	}

	for (int32 i = 0; (thread = threads.ItemAt(i)) != NULL; i++)
		thread->ReleaseReference();

	return error;
}


/**
 * @brief Dumps the focused-thread report: stack trace, per-frame source
 *        location, register state, disassembly, variables, and stack memory.
 *
 * Blocks (via fTeamDataSem) while waiting for a stack trace and for any
 * functions whose source code must be loaded on demand. Releases the team
 * lock around those waits so the rest of the debugger keeps running.
 *
 * @param _output  File to write to.
 * @param thread   Thread whose detailed state should be emitted; must be in
 *                 THREAD_STATE_STOPPED.
 * @return B_OK on success, or any error from a blocked semaphore wait, value
 *         resolution, source loading, or write call.
 */
status_t
DebugReportGenerator::_DumpDebuggedThreadInfo(BFile& _output,
	::Thread* thread)
{
	AutoLocker< ::Team> locker;
	if (thread->State() != THREAD_STATE_STOPPED)
		return B_OK;

	status_t error;
	StackTrace* trace = NULL;
	for (;;) {
		trace = thread->GetStackTrace();
		if (trace != NULL)
			break;

		locker.Unlock();
		fTraceWaitingThread = thread;
		do {
			error = acquire_sem(fTeamDataSem);
		} while (error == B_INTERRUPTED);

		if (error != B_OK)
			return error;

		locker.Lock();
	}

	BString data("\t\tFrame\t\tIP\t\t\tFunction Name\n");
	WRITE_AND_CHECK(_output, data);
	data = "\t\t-----------------------------------------------\n";
	WRITE_AND_CHECK(_output, data);
	for (int32 i = 0; StackFrame* frame = trace->FrameAt(i); i++) {
		char functionName[512];
		BString sourcePath;

		target_addr_t ip = frame->InstructionPointer();
		Image* image = fTeam->ImageByAddress(ip);
		FunctionInstance* functionInstance = NULL;
		if (image != NULL && image->ImageDebugInfoState()
				== IMAGE_DEBUG_INFO_LOADED) {
			ImageDebugInfo* info = image->GetImageDebugInfo();
			functionInstance = info->FunctionAtAddress(ip);
		}

		if (functionInstance != NULL) {
			Function* function = functionInstance->GetFunction();
			if (function->SourceCodeState() == FUNCTION_SOURCE_NOT_LOADED
				&& functionInstance->SourceCodeState()
					== FUNCTION_SOURCE_NOT_LOADED) {
				fSourceWaitingFunction = function;
				fSourceWaitingFunction->AddListener(this);
				fListener->FunctionSourceCodeRequested(functionInstance);

				locker.Unlock();

				do {
					error = acquire_sem(fTeamDataSem);
				} while (error == B_INTERRUPTED);

				if (error != B_OK)
					return error;

				locker.Lock();
			}
		}

		Statement* statement;
		if (fTeam->GetStatementAtAddress(ip,
				functionInstance, statement) == B_OK) {
			BReference<Statement> statementReference(statement, true);

			int32 line = statement->StartSourceLocation().Line();
			LocatableFile* sourceFile = functionInstance->GetFunction()
				->SourceFile();
			if (sourceFile != NULL) {
				sourceFile->GetPath(sourcePath);
				sourcePath.SetToFormat("(%s:%" B_PRId32 ")",
					sourcePath.String(), line);
			}
		}


		data.SetToFormat("\t\t%#08" B_PRIx64 "\t%#08" B_PRIx64 "\t%s %s\n",
			frame->FrameAddress(), ip, UiUtils::FunctionNameForFrame(
				frame, functionName, sizeof(functionName)),
				sourcePath.String());

		WRITE_AND_CHECK(_output, data);

		// only dump the topmost frame
		if (i == 0) {
			locker.Unlock();
			error = _DumpFunctionDisassembly(_output,
				frame->InstructionPointer());
			if (error != B_OK)
				return error;
			error = _DumpStackFrameMemory(_output, thread->GetCpuState(),
				frame->FrameAddress(), thread->GetTeam()->GetArchitecture()
					->StackGrowthDirection());
			if (error != B_OK)
				return error;
			locker.Lock();
		}

		if (frame->CountParameters() == 0 && frame->CountLocalVariables() == 0)
			continue;

		data = "\t\t\tVariables:\n";
		WRITE_AND_CHECK(_output, data);
		error = fNodeManager->SetStackFrame(thread, frame);
		if (error != B_OK)
			continue;

		ValueNodeContainer* container = fNodeManager->GetContainer();
		AutoLocker<ValueNodeContainer> containerLocker(container);
		for (int32 i = 0; i < container->CountChildren(); i++) {
			data.Truncate(0L);
			ValueNodeChild* child = container->ChildAt(i);
			containerLocker.Unlock();
			_ResolveValueIfNeeded(child->Node(), frame, 1);
			containerLocker.Lock();
			UiUtils::PrintValueNodeGraph(data, child, 3, 1);
			WRITE_AND_CHECK(_output, data);
		}
		data = "\n";
		WRITE_AND_CHECK(_output, data);
	}

	data = "\n\t\tRegisters:\n";
	WRITE_AND_CHECK(_output, data);

	CpuState* state = thread->GetCpuState();
	BVariant value;
	const Register* reg = NULL;
	for (int32 i = 0; i < fArchitecture->CountRegisters(); i++) {
		reg = fArchitecture->Registers() + i;
		state->GetRegisterValue(reg, value);

		if (reg->Format() == REGISTER_FORMAT_SIMD) {
			data.SetToFormat("\t\t\t%5s:\t%s\n", reg->Name(),
				UiUtils::FormatSIMDValue(value, reg->BitSize(),
					SIMD_RENDER_FORMAT_INT16, data).String());
		} else {
			char buffer[64];
			data.SetToFormat("\t\t\t%5s:\t%s\n", reg->Name(),
				UiUtils::VariantToString(value, buffer, sizeof(buffer)));
		}

		WRITE_AND_CHECK(_output, data);
	}

	return B_OK;
}


/**
 * @brief Emits the disassembly of the function containing @a instructionPointer
 *        up to and including the line that contains it.
 *
 * Falls back to a "not available" line whenever any required piece (image,
 * debug info, function instance, statement) is missing — this never returns
 * an error to its caller in those cases.
 *
 * @param _output             File to write to.
 * @param instructionPointer  Faulting instruction address.
 * @return B_OK on success or whenever the fallback path runs; an error from
 *         underlying writes if those fail.
 */
status_t
DebugReportGenerator::_DumpFunctionDisassembly(BFile& _output,
	target_addr_t instructionPointer)
{
	AutoLocker< ::Team> teamLocker(fTeam);
	BString data;
	FunctionInstance* instance = NULL;
	Image* image = fTeam->ImageByAddress(instructionPointer);
	if (image == NULL) {
		data.SetToFormat("\t\t\tUnable to retrieve disassembly for IP %#"
			B_PRIx64 ": address not contained in any valid image.\n",
			instructionPointer);
		WRITE_AND_CHECK(_output, data);
		return B_OK;
	}

	ImageDebugInfo* info = image->GetImageDebugInfo();
	if (info == NULL) {
		data.SetToFormat("\t\t\tUnable to retrieve disassembly for IP %#"
			B_PRIx64 ": no debug info available for image '%s'.\n",
			instructionPointer,	image->Name().String());
		WRITE_AND_CHECK(_output, data);
		return B_OK;
	}

	instance = info->FunctionAtAddress(instructionPointer);
	if (instance == NULL) {
		data.SetToFormat("\t\t\tUnable to retrieve disassembly for IP %#"
			B_PRIx64 ": address does not point to a function.\n",
			instructionPointer);
		WRITE_AND_CHECK(_output, data);
		return B_OK;
	}

	Statement* statement = NULL;
	DisassembledCode* code = instance->GetSourceCode();
	BReference<DisassembledCode> codeReference;
	if (code == NULL) {
		status_t error = fTeam->DebugInfo()->DisassembleFunction(instance,
			code);
		if (error != B_OK) {
			data.SetToFormat("\t\t\tUnable to retrieve disassembly for IP %#"
				B_PRIx64 ": %s.\n", instructionPointer, strerror(error));
			WRITE_AND_CHECK(_output, data);
			return B_OK;
		}

		codeReference.SetTo(code, true);
	} else
		codeReference.SetTo(code);

	statement = code->StatementAtAddress(instructionPointer);
	if (statement == NULL) {
		data.SetToFormat("\t\t\tUnable to retrieve disassembly for IP %#"
			B_PRIx64 ": address does not map to a valid instruction.\n",
			instructionPointer);
		WRITE_AND_CHECK(_output, data);
		return B_OK;
	}

	SourceLocation location = statement->StartSourceLocation();

	data = "\t\t\tDisassembly:\n";
	WRITE_AND_CHECK(_output, data);
	for (int32 i = 0; i <= location.Line(); i++) {
		data = "\t\t\t\t";
		data << code->LineAt(i);
		if (i == location.Line())
			data << " <--";
		data << "\n";
		WRITE_AND_CHECK(_output, data);
	}
	data = "\n";
	WRITE_AND_CHECK(_output, data);

	return B_OK;
}


/**
 * @brief Dumps the bytes between the stack pointer and frame pointer in
 *        16-byte rows, fetching the underlying memory block on demand.
 *
 * @param _output         File to write to.
 * @param state           CPU state used to locate the stack pointer.
 * @param framePointer    Frame pointer used as the upper or lower bound
 *                        depending on the stack direction.
 * @param stackDirection  Architecture-defined stack direction; selects the
 *                        ordering of @a framePointer relative to the stack pointer.
 * @return B_OK on success or any error from a blocking memory-block wait.
 */
status_t
DebugReportGenerator::_DumpStackFrameMemory(BFile& _output,
	CpuState* state, target_addr_t framePointer, uint8 stackDirection)
{
	target_addr_t startAddress;
	target_addr_t endAddress;
	if (stackDirection == STACK_GROWTH_DIRECTION_POSITIVE) {
		startAddress = framePointer;
		endAddress = state->StackPointer();
	} else {
		startAddress = state->StackPointer();
		endAddress = framePointer;
	}

	if (endAddress <= startAddress)
		return B_OK;

	if (fCurrentBlock == NULL || !fCurrentBlock->Contains(startAddress)) {
		status_t error;
		fListener->InspectRequested(startAddress, this);
		do {
			error = acquire_sem(fTeamDataSem);
		} while (error == B_INTERRUPTED);

		if (error != B_OK)
			return error;
	}

	BString data("\t\t\tFrame memory:\n");
	WRITE_AND_CHECK(_output, data);
	if (fBlockRetrievalStatus == B_OK) {
		data.Truncate(0L);
		UiUtils::DumpMemory(data, 4, fCurrentBlock, startAddress, 1, 16,
			endAddress - startAddress);
		WRITE_AND_CHECK(_output, data);
	} else {
		data.SetToFormat("\t\t\t\tUnavailable (%s)\n", strerror(
				fBlockRetrievalStatus));
		WRITE_AND_CHECK(_output, data);
	}

	return B_OK;
}


/**
 * @brief Forces resolution of a value node (and optionally its descendants)
 *        before printing it into the report.
 *
 * Triggers ValueNodeManager work via the UI listener and blocks until the
 * value-changed callback fires. Recurses up to @a maxDepth levels through the
 * node's children so that aggregate values are pre-resolved before the dump.
 *
 * @param node      Value node whose value should be resolved.
 * @param frame     Stack frame providing the resolution context.
 * @param maxDepth  Maximum recursion depth into children.
 * @return B_OK on success or any error from semaphore waits or the underlying
 *         resolution path.
 */
status_t
DebugReportGenerator::_ResolveValueIfNeeded(ValueNode* node, StackFrame* frame,
	int32 maxDepth)
{
	status_t result = B_OK;
	if (node->LocationAndValueResolutionState() == VALUE_NODE_UNRESOLVED) {
		fWaitingNode = node;
		fListener->ValueNodeValueRequested(frame->GetCpuState(),
			fNodeManager->GetContainer(), node);
		do {
			result = acquire_sem(fTeamDataSem);
		} while (result == B_INTERRUPTED);
	}

	if (node->LocationAndValueResolutionState() == B_OK && maxDepth > 0) {
		AutoLocker<ValueNodeContainer> containerLocker(
			fNodeManager->GetContainer());
		for (int32 i = 0; i < node->CountChildren(); i++) {
			ValueNodeChild* child = node->ChildAt(i);
			containerLocker.Unlock();
			result = fNodeManager->AddChildNodes(child);
			if (result != B_OK)
				continue;

			// since in the case of a pointer to a compound we hide
			// the intervening compound, don't consider the hidden node
			// a level for the purposes of depth traversal
			if (node->GetType()->ResolveRawType(false)->Kind() == TYPE_ADDRESS
				&& child->GetType()->ResolveRawType(false)->Kind()
					== TYPE_COMPOUND) {
				_ResolveValueIfNeeded(child->Node(), frame, maxDepth);
			} else
				_ResolveValueIfNeeded(child->Node(), frame, maxDepth - 1);
			containerLocker.Lock();
		}
	}

	return result;
}


/**
 * @brief Stores the freshly-retrieved memory block (or failure status) and
 *        wakes the waiting generator thread.
 *
 * @param block   New current block; ownership of the reference passes to the
 *                generator and the previous block is released.
 * @param result  Status from the retrieval; cached for the dump path to read.
 */
void
DebugReportGenerator::_HandleMemoryBlockRetrieved(TeamMemoryBlock* block,
	status_t result)
{
	if (fCurrentBlock != NULL) {
		fCurrentBlock->ReleaseReference();
		fCurrentBlock = NULL;
	}

	fBlockRetrievalStatus = result;

	fCurrentBlock = block;
	release_sem(fTeamDataSem);
}



/**
 * @brief Comparator that orders AreaInfo objects by base address ascending.
 *
 * @param a  Left-hand area.
 * @param b  Right-hand area.
 * @return -1 if @a a precedes @a b, 1 otherwise.
 */
/*static*/ int
DebugReportGenerator::_CompareAreas(const AreaInfo* a, const AreaInfo* b)
{
	if (a->BaseAddress() < b->BaseAddress())
		return -1;

	return 1;
}


/**
 * @brief Comparator that orders Image objects by text-base ascending.
 *
 * @param a  Left-hand image.
 * @param b  Right-hand image.
 * @return -1 if @a a precedes @a b, 1 otherwise.
 */
/*static*/ int
DebugReportGenerator::_CompareImages(const Image* a, const Image* b)
{
	if (a->Info().TextBase() < b->Info().TextBase())
		return -1;

	return 1;
}


/**
 * @brief Comparator that orders semaphores by id ascending.
 *
 * @param a  Left-hand semaphore.
 * @param b  Right-hand semaphore.
 * @return -1 if @a a precedes @a b, 1 otherwise.
 */
/*static*/ int
DebugReportGenerator::_CompareSemaphores(const SemaphoreInfo* a,
	const SemaphoreInfo* b)
{
	if (a->SemID() < b->SemID())
		return -1;

	return 1;
}


/**
 * @brief Comparator that orders threads with running threads first and
 *        stopped threads last; ties break by thread id.
 *
 * @param a  Left-hand thread.
 * @param b  Right-hand thread.
 * @return -1 or 1 according to the rule above.
 */
/*static*/ int
DebugReportGenerator::_CompareThreads(const ::Thread* a,
	const ::Thread* b)
{
	// sort stopped threads last, otherwise sort by thread ID
	if (a->State() == b->State())
		return a->ID() < b->ID() ? -1 : 1;

	if (a->State() == THREAD_STATE_STOPPED && b->State()
			!= THREAD_STATE_STOPPED) {
		return 1;
	}

	return -1;
}
