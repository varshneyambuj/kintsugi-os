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
 * MIT License. Copyright 2016, Rene Gollent, rene@gollent.com.
 */

/** @file RemoteDebugRequest.h
    @brief Marshalled request/response protocol used by the network debugger transport. */

#ifndef REMOTE_DEBUG_REQUEST_H
#define REMOTE_DEBUG_REQUEST_H

#include <OS.h>

#include <Referenceable.h>

#include "Types.h"


enum remote_request_type {
	// debug requests
	REMOTE_REQUEST_TYPE_READ_MEMORY = 0,
	REMOTE_REQUEST_TYPE_WRITE_MEMORY,
	REMOTE_REQUEST_TYPE_SET_TEAM_FLAGS,
	REMOTE_REQUEST_TYPE_SET_THREAD_FLAGS,
	REMOTE_REQUEST_TYPE_CONTINUE_THREAD,
	REMOTE_REQUEST_TYPE_STOP_THREAD,
	REMOTE_REQUEST_TYPE_SINGLE_STEP_THREAD,
	REMOTE_REQUEST_TYPE_GET_CPU_STATE,
	REMOTE_REQUEST_TYPE_SET_CPU_STATE,
	REMOTE_REQUEST_TYPE_INSTALL_BREAKPOINT,
	REMOTE_REQUEST_TYPE_UNINSTALL_BREAKPOINT,
	REMOTE_REQUEST_TYPE_INSTALL_WATCHPOINT,
	REMOTE_REQUEST_TYPE_UNINSTALL_WATCHPOINT,
	REMOTE_REQUEST_TYPE_PREPARE_HANDOVER,
	REMOTE_REQUEST_TYPE_WRITE_CORE_FILE,

	// team information requests
	REMOTE_REQUEST_TYPE_GET_TEAM_INFO,
	REMOTE_REQUEST_TYPE_GET_THREAD_INFOS,
	REMOTE_REQUEST_TYPE_GET_IMAGE_INFOS,
	REMOTE_REQUEST_TYPE_GET_AREA_INFOS,
	REMOTE_REQUEST_TYPE_GET_SEM_INFOS,
	REMOTE_REQUEST_TYPE_GET_SYMBOL_INFOS,
	REMOTE_REQUEST_TYPE_GET_SYMBOL_INFO,
	REMOTE_REQUEST_TYPE_GET_THREAD_INFO,
	REMOTE_REQUEST_TYPE_GET_MEMORY_PROPERTIES
};


class Architecture;
class BMessage;
class CpuState;


/** @brief Abstract base for marshalled remote debug requests; encodes the
           request type and per-subclass payload to/from a BMessage. */
class RemoteDebugRequest : public BReferenceable {
public:
								RemoteDebugRequest();
	virtual						~RemoteDebugRequest();

	virtual	remote_request_type	Type() const = 0;

			status_t			LoadFromMessage(const BMessage& data);
			status_t			SaveToMessage(BMessage& _output) const;

			Architecture*		GetArchitecture() const
									{ return fArchitecture; }
			void				SetArchitecture(Architecture* architecture);

protected:
	virtual	status_t			LoadSpecificInfoFromMessage(
									const BMessage& data) = 0;
	virtual	status_t			SaveSpecificInfoToMessage(
									BMessage& _output) const = 0;

private:
			Architecture*		fArchitecture;
};


/** @brief Base class for marshalled responses paired with a RemoteDebugRequest;
           always carries a status code and may carry subclass-specific data. */
class RemoteDebugResponse : public BReferenceable {
public:
								RemoteDebugResponse();
	virtual						~RemoteDebugResponse();

			void				SetRequestInfo(RemoteDebugRequest* request,
									status_t result);

			RemoteDebugRequest*	Request() const 	{ return fRequest; }
			Architecture*		GetArchitecture() const
									{ return fRequest->GetArchitecture(); }

			status_t			LoadFromMessage(const BMessage& data);
			status_t			SaveToMessage(BMessage& _output) const;

			status_t			Result() const		{ return fResult; }
			bool				Succeeded() const	{ return fResult == B_OK; }

protected:
								// for requests that respond with additional
								// information beyond a simple success/failure,
								// a subclass must be implemented that provides
								// versions of the functions below to save/
								// and restore the corresponding additional
								// data. Requests that merely return a status
								// code can simply instantiate the basic
								// response class as is.
	virtual	status_t			LoadSpecificInfoFromMessage(const BMessage& data);
	virtual	status_t			SaveSpecificInfoToMessage(BMessage& _output) const;

private:
			RemoteDebugRequest*	fRequest;
			status_t			fResult;
};


// #pragma mark - Requests


/** @brief Request to read a contiguous range of debugged-team memory. */
class RemoteDebugReadMemoryRequest : public RemoteDebugRequest {
public:
								RemoteDebugReadMemoryRequest();
	virtual						~RemoteDebugReadMemoryRequest();

			void				SetTo(target_addr_t address,
									target_size_t size);

			target_addr_t		Address() const	{ return fAddress; }
			target_size_t 		Size() const { return fSize; }

	virtual	remote_request_type	Type() const;

protected:
	virtual	status_t			LoadSpecificInfoFromMessage(
									const BMessage& data);
	virtual	status_t			SaveSpecificInfoToMessage(
									BMessage& _output) const;

private:
			target_addr_t		fAddress;
			target_size_t		fSize;
};


/** @brief Request to write a contiguous buffer into debugged-team memory. */
class RemoteDebugWriteMemoryRequest : public RemoteDebugRequest {
public:
								RemoteDebugWriteMemoryRequest();
	virtual						~RemoteDebugWriteMemoryRequest();

			status_t			SetTo(target_addr_t address,
									const void* data, target_size_t size);

			target_addr_t		Address() const	{ return fAddress; }
			const void*			Data() const	{ return fData; }
			target_size_t 		Size() const	{ return fSize; }

	virtual	remote_request_type	Type() const;

protected:
	virtual	status_t			LoadSpecificInfoFromMessage(
									const BMessage& data);
	virtual	status_t			SaveSpecificInfoToMessage(
									BMessage& _output) const;

private:
			target_addr_t		fAddress;
			void*				fData;
			target_size_t		fSize;
};


/** @brief Request to update team-wide debug flags on the remote target. */
class RemoteDebugSetTeamFlagsRequest : public RemoteDebugRequest {
public:
								RemoteDebugSetTeamFlagsRequest();
	virtual						~RemoteDebugSetTeamFlagsRequest();

			void				SetTo(int32 flags);

			int32				Flags() const	{ return fFlags; }

	virtual	remote_request_type	Type() const;

protected:
	virtual	status_t			LoadSpecificInfoFromMessage(
									const BMessage& data);
	virtual	status_t			SaveSpecificInfoToMessage(
									BMessage& _output) const;

private:
			int32				fFlags;
};


/** @brief Request to update per-thread debug flags on the remote target. */
class RemoteDebugSetThreadFlagsRequest : public RemoteDebugRequest {
public:
								RemoteDebugSetThreadFlagsRequest();
	virtual						~RemoteDebugSetThreadFlagsRequest();

			void				SetTo(thread_id thread, int32 flags);

			thread_id			Thread() const	{ return fThread; }
			int32				Flags() const	{ return fFlags; }

	virtual	remote_request_type	Type() const;

protected:
	virtual	status_t			LoadSpecificInfoFromMessage(
									const BMessage& data);
	virtual	status_t			SaveSpecificInfoToMessage(
									BMessage& _output) const;

private:
			thread_id			fThread;
			int32				fFlags;
};


// abstract base for the various thread actions, as those all
// take a thread ID as a parameter and have no special response
// requirements, with only the action to be taken differing.
/** @brief Abstract base for thread-targeted remote actions that need only a
           thread id and a status reply (continue, stop, single-step, ...). */
class RemoteDebugThreadActionRequest : public RemoteDebugRequest {
public:
								RemoteDebugThreadActionRequest();
	virtual						~RemoteDebugThreadActionRequest();

			void				SetTo(thread_id thread);

			thread_id			Thread() const	{ return fThread; }

protected:
	virtual	status_t			LoadSpecificInfoFromMessage(
									const BMessage& data);
	virtual	status_t			SaveSpecificInfoToMessage(
									BMessage& _output) const;

private:
			thread_id			fThread;
};


/** @brief Request to resume a stopped thread on the remote target. */
class RemoteDebugContinueThreadRequest
	: public RemoteDebugThreadActionRequest {
public:
								RemoteDebugContinueThreadRequest();
	virtual						~RemoteDebugContinueThreadRequest();

	virtual	remote_request_type	Type() const;
};


/** @brief Request to stop a running thread on the remote target. */
class RemoteDebugStopThreadRequest
	: public RemoteDebugThreadActionRequest {
public:
								RemoteDebugStopThreadRequest();
	virtual						~RemoteDebugStopThreadRequest();

	virtual	remote_request_type	Type() const;
};


/** @brief Request to single-step a thread on the remote target. */
class RemoteDebugSingleStepThreadRequest
	: public RemoteDebugThreadActionRequest {
public:
								RemoteDebugSingleStepThreadRequest();
	virtual						~RemoteDebugSingleStepThreadRequest();

	virtual	remote_request_type	Type() const;
};


/** @brief Request to retrieve the CPU state of a thread on the remote target. */
class RemoteDebugGetCpuStateRequest
	: public RemoteDebugThreadActionRequest {
public:
								RemoteDebugGetCpuStateRequest();
	virtual						~RemoteDebugGetCpuStateRequest();

	virtual	remote_request_type	Type() const;
};


/** @brief Request to overwrite the CPU state of a thread on the remote target. */
class RemoteDebugSetCpuStateRequest : public RemoteDebugRequest {
public:
								RemoteDebugSetCpuStateRequest();
	virtual						~RemoteDebugSetCpuStateRequest();

			void				SetTo(thread_id thread, CpuState* state);

			thread_id			Thread() const	{ return fThread; }

	virtual	remote_request_type	Type() const;

protected:
	virtual	status_t			LoadSpecificInfoFromMessage(
									const BMessage& data);
	virtual	status_t			SaveSpecificInfoToMessage(
									BMessage& _output) const;

private:
			thread_id			fThread;
			CpuState*			fCpuState;
};


// abstract base for the various actions that influence how the CPU
// reacts to a particular memory address, ergo break/watchpoints.
/** @brief Abstract base for address-targeted requests (install/uninstall
           breakpoints and address-only watchpoint operations). */
class RemoteDebugAddressActionRequest : public RemoteDebugRequest {
public:
								RemoteDebugAddressActionRequest();
	virtual						~RemoteDebugAddressActionRequest();

			void				SetTo(target_addr_t address);

			target_addr_t		Address() const	{ return fAddress; }

protected:
	virtual	status_t			LoadSpecificInfoFromMessage(
									const BMessage& data);
	virtual	status_t			SaveSpecificInfoToMessage(
									BMessage& _output) const;

private:
			target_addr_t		fAddress;
};


/** @brief Request to install a breakpoint at a target address. */
class RemoteDebugInstallBreakpointRequest
	: public RemoteDebugAddressActionRequest {
public:
								RemoteDebugInstallBreakpointRequest();
	virtual						~RemoteDebugInstallBreakpointRequest();

	virtual	remote_request_type	Type() const;
};


/** @brief Request to remove a previously-installed breakpoint. */
class RemoteDebugUninstallBreakpointRequest
	: public RemoteDebugAddressActionRequest {
public:
								RemoteDebugUninstallBreakpointRequest();
	virtual						~RemoteDebugUninstallBreakpointRequest();

	virtual	remote_request_type	Type() const;
};


/** @brief Request to install a hardware watchpoint with type and length. */
class RemoteDebugInstallWatchpointRequest : public RemoteDebugRequest {
public:
								RemoteDebugInstallWatchpointRequest();
	virtual						~RemoteDebugInstallWatchpointRequest();

			void				SetTo(target_addr_t address, uint32 type,
									int32 length);

			target_addr_t		Address() const		{ return fAddress; }
			uint32				WatchType() const	{ return fWatchType; }
			int32				Length() const		{ return fLength; }

	virtual	remote_request_type	Type() const;

protected:
	virtual	status_t			LoadSpecificInfoFromMessage(
									const BMessage& data);
	virtual	status_t			SaveSpecificInfoToMessage(
									BMessage& _output) const;

private:
			target_addr_t		fAddress;
			uint32				fWatchType;
			int32				fLength;
};


/** @brief Request to remove a previously-installed watchpoint. */
class RemoteDebugUninstallWatchpointRequest
	: public RemoteDebugAddressActionRequest {
public:
								RemoteDebugUninstallWatchpointRequest();
	virtual						~RemoteDebugUninstallWatchpointRequest();

	virtual	remote_request_type	Type() const;
};


// #pragma mark - Responses


/** @brief Response carrying the data buffer returned by a read-memory request. */
class RemoteDebugReadMemoryResponse : public RemoteDebugResponse {
public:
								RemoteDebugReadMemoryResponse();
	virtual						~RemoteDebugReadMemoryResponse();

			void				SetTo(void* data, target_size_t size);

			const void*			Data() const	{ return fData; }
			target_size_t		Size() const 	{ return fSize; }

protected:
	virtual	status_t			LoadSpecificInfoFromMessage(const BMessage& data);
	virtual	status_t			SaveSpecificInfoToMessage(BMessage& _output) const;

private:
			void*				fData;
			target_size_t		fSize;
};


/** @brief Response carrying the CPU state returned by a get-CPU-state request. */
class RemoteDebugGetCpuStateResponse : public RemoteDebugResponse {
public:
								RemoteDebugGetCpuStateResponse();
	virtual						~RemoteDebugGetCpuStateResponse();

			void				SetTo(CpuState* state);

			CpuState*			GetCpuState() const { return fCpuState; }

protected:
	virtual	status_t			LoadSpecificInfoFromMessage(const BMessage& data);
	virtual	status_t			SaveSpecificInfoToMessage(BMessage& _output) const;

private:
			CpuState*			fCpuState;
};


#endif	// REMOTE_DEBUG_REQUEST_H
