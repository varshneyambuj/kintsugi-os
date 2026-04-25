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
 *   Copyright 2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file RemoteDebugRequest.cpp
 * @brief Marshalling implementation for the network debugger transport's
 *        request/response objects.
 *
 * Each concrete request and response subclass plus the two abstract bases
 * implement LoadFromMessage()/SaveToMessage() to round-trip themselves
 * through a BMessage. Type() returns the wire-protocol id used by the remote
 * dispatcher to route a request to its handler.
 */


#include "RemoteDebugRequest.h"

#include <stdlib.h>

#include <Message.h>

#include <debugger.h>

#include <AutoDeleter.h>

#include "Architecture.h"
#include "CpuState.h"


// #pragma mark - RemoteDebugRequest


/**
 * @brief Default-constructs a request with no associated architecture.
 */
RemoteDebugRequest::RemoteDebugRequest()
	:
	BReferenceable(),
	fArchitecture(NULL)
{
}


/**
 * @brief Releases the architecture reference if one was set.
 */
RemoteDebugRequest::~RemoteDebugRequest()
{
	if (fArchitecture != NULL)
		fArchitecture->ReleaseReference();
}


/**
 * @brief Validates the message type and forwards to the subclass loader.
 *
 * @param data  Message previously produced by SaveToMessage().
 * @return B_OK on success, B_BAD_VALUE if the type field doesn't match,
 *         or any error from LoadSpecificInfoFromMessage().
 */
status_t
RemoteDebugRequest::LoadFromMessage(const BMessage& data)
{
	if (data.FindInt32("type") != Type())
		return B_BAD_VALUE;

	return LoadSpecificInfoFromMessage(data);
}


/**
 * @brief Empties @a _output, writes the type tag, and forwards to the subclass writer.
 *
 * @param _output  Message to populate; cleared on entry.
 * @return B_OK on success, otherwise the first failing AddInt32() or
 *         SaveSpecificInfoToMessage() error.
 */
status_t
RemoteDebugRequest::SaveToMessage(BMessage& _output) const
{
	_output.MakeEmpty();

	status_t error = _output.AddInt32("type", Type());
	if (error != B_OK)
		return error;

	return SaveSpecificInfoToMessage(_output);
}


/**
 * @brief Sets the architecture used for CpuState marshalling and acquires a reference.
 *
 * @param architecture  Architecture to associate; must be non-NULL.
 */
void
RemoteDebugRequest::SetArchitecture(Architecture* architecture)
{
	fArchitecture = architecture;
	fArchitecture->AcquireReference();
}


// #pragma mark - RemoteDebugResponse


/**
 * @brief Default-constructs a response with no request and a B_OK result.
 */
RemoteDebugResponse::RemoteDebugResponse()
	:
	BReferenceable(),
	fRequest(NULL),
	fResult(B_OK)
{
}


/**
 * @brief Releases the request reference if one was set.
 */
RemoteDebugResponse::~RemoteDebugResponse()
{
	if (fRequest != NULL)
		fRequest->ReleaseReference();
}


/**
 * @brief Binds the response to its originating request and result code.
 *
 * @param request  Originating request; the response acquires a reference.
 * @param result   Outcome of the request.
 */
void
RemoteDebugResponse::SetRequestInfo(RemoteDebugRequest* request,
	status_t result)
{
	fRequest = request;
	fRequest->AcquireReference();
	fResult = result;
}


/**
 * @brief Validates the type tag, checks the result, and forwards to the
 *        subclass loader on success.
 *
 * @param data  Message previously produced by SaveToMessage().
 * @return B_OK if loaded (or if the call failed remotely with no payload),
 *         B_BAD_VALUE on type mismatch, or any error from the subclass loader.
 */
status_t
RemoteDebugResponse::LoadFromMessage(const BMessage& data)
{
	if (data.FindInt32("type") != Request()->Type())
		return B_BAD_VALUE;

	if (!Succeeded())
		return B_OK;

	return LoadSpecificInfoFromMessage(data);
}


/**
 * @brief Empties @a _output and writes the type, result, and (on success) payload.
 *
 * @param _output  Message to populate; cleared on entry.
 * @return B_OK on success, otherwise the first failing AddInt32() or
 *         SaveSpecificInfoToMessage() error.
 */
status_t
RemoteDebugResponse::SaveToMessage(BMessage& _output) const
{
	_output.MakeEmpty();

	status_t error = _output.AddInt32("type", Request()->Type());
	if (error != B_OK)
		return error;

	error = _output.AddInt32("result", Result());
	if (error != B_OK)
		return error;

	if (!Succeeded())
		return B_OK;

	return SaveSpecificInfoToMessage(_output);
}


/**
 * @brief Default no-op loader for responses that carry no payload beyond status.
 *
 * @param data  Ignored.
 * @return Always B_OK.
 */
status_t
RemoteDebugResponse::LoadSpecificInfoFromMessage(const BMessage& data)
{
	return B_OK;
}


/**
 * @brief Default no-op writer for responses that carry no payload beyond status.
 *
 * @param _output  Ignored.
 * @return Always B_OK.
 */
status_t
RemoteDebugResponse::SaveSpecificInfoToMessage(BMessage& _output) const
{
	return B_OK;
}


// #pragma mark - RemoteDebugReadMemoryRequest


/** @brief Default-constructs an empty read-memory request. */
RemoteDebugReadMemoryRequest::RemoteDebugReadMemoryRequest()
	:
	RemoteDebugRequest(),
	fAddress(0),
	fSize(0)
{
}


/** @brief Trivial destructor. */
RemoteDebugReadMemoryRequest::~RemoteDebugReadMemoryRequest()
{
}


/**
 * @brief Configures the request with target address and length to read.
 *
 * @param address  Target-side address.
 * @param size     Number of bytes to read.
 */
void
RemoteDebugReadMemoryRequest::SetTo(target_addr_t address, target_size_t size)
{
	fAddress = address;
	fSize = size;
}


/** @brief Returns the wire type code for read-memory requests. */
remote_request_type
RemoteDebugReadMemoryRequest::Type() const
{
	return REMOTE_REQUEST_TYPE_READ_MEMORY;
}


/**
 * @brief Reads address and size fields from @a data.
 *
 * @param data  Source message.
 * @return B_OK on success, B_BAD_VALUE if any field is missing.
 */
status_t
RemoteDebugReadMemoryRequest::LoadSpecificInfoFromMessage(const BMessage& data)
{
	if (data.FindUInt64("address", &fAddress) != B_OK)
		return B_BAD_VALUE;

	if (data.FindUInt64("size", &fSize) != B_OK)
		return B_BAD_VALUE;

	return B_OK;
}


/**
 * @brief Writes address and size fields into @a _output.
 *
 * @param _output  Destination message.
 * @return B_OK on success or any error from BMessage::AddUInt64().
 */
status_t
RemoteDebugReadMemoryRequest::SaveSpecificInfoToMessage(
	BMessage& _output) const
{
	status_t error = _output.AddUInt64("address", fAddress);
	if (error != B_OK)
		return error;

	return _output.AddUInt64("size", fSize);
}


// #pragma mark - RemoteDebugWriteMemoryRequest


/** @brief Default-constructs an empty write-memory request. */
RemoteDebugWriteMemoryRequest::RemoteDebugWriteMemoryRequest()
	:
	RemoteDebugRequest(),
	fAddress(0),
	fData(NULL),
	fSize(0)
{
}


/** @brief Frees the buffered payload, if any. */
RemoteDebugWriteMemoryRequest::~RemoteDebugWriteMemoryRequest()
{
	if (fData != NULL)
		free(fData);
}


/**
 * @brief Configures the request and copies the payload to be written.
 *
 * @param address  Target-side address to write to.
 * @param data     Source bytes; must be non-NULL when @a size > 0.
 * @param size     Number of bytes to copy.
 * @return B_OK on success, B_BAD_VALUE for invalid arguments, or B_NO_MEMORY
 *         on allocation failure.
 */
status_t
RemoteDebugWriteMemoryRequest::SetTo(target_addr_t address, const void* data,
	target_size_t size)
{
	if (size == 0 || data == NULL)
		return B_BAD_VALUE;

	fAddress = address;
	fSize = size;
	fData = malloc(fSize);
	if (fData == NULL)
		return B_NO_MEMORY;


	memcpy(fData, data, fSize);
	return B_OK;
}


/** @brief Returns the wire type code for write-memory requests. */
remote_request_type
RemoteDebugWriteMemoryRequest::Type() const
{
	return REMOTE_REQUEST_TYPE_WRITE_MEMORY;
}


/**
 * @brief Reads the address, size, and raw payload from @a data.
 *
 * @param data  Source message.
 * @return B_OK on success, B_BAD_VALUE if a field is missing, B_NO_MEMORY on
 *         allocation failure, or B_MISMATCHED_VALUES if the payload size
 *         doesn't match the declared size.
 */
status_t
RemoteDebugWriteMemoryRequest::LoadSpecificInfoFromMessage(
	const BMessage& data)
{
	if (data.FindUInt64("address", &fAddress) != B_OK)
		return B_BAD_VALUE;

	if (data.FindUInt64("size", &fSize) != B_OK)
		return B_BAD_VALUE;

	fData = malloc(fSize);
	if (fData == NULL)
		return B_NO_MEMORY;

	const void* messageData = NULL;
	ssize_t numBytes = -1;
	status_t error = data.FindData("data", B_RAW_TYPE, &messageData,
		&numBytes);
	if (error != B_OK)
		return error;

	if ((size_t)numBytes != fSize)
		return B_MISMATCHED_VALUES;

	memcpy(fData, messageData, numBytes);

	return B_OK;
}


/**
 * @brief Writes address, size, and raw payload into @a _output.
 *
 * @param _output  Destination message.
 * @return B_OK on success or any error from BMessage::AddUInt64()/AddData().
 */
status_t
RemoteDebugWriteMemoryRequest::SaveSpecificInfoToMessage(
	BMessage& _output) const
{
	status_t error = _output.AddUInt64("address", fAddress);
	if (error != B_OK)
		return error;

	error = _output.AddUInt64("size", fSize);
	if (error != B_OK)
		return error;

	return _output.AddData("data", B_RAW_TYPE, fData, (ssize_t)fSize);
}


// #pragma mark - RemoteDebugSetTeamFlagsRequest


/** @brief Default-constructs an empty set-team-flags request. */
RemoteDebugSetTeamFlagsRequest::RemoteDebugSetTeamFlagsRequest()
	:
	RemoteDebugRequest(),
	fFlags(0)
{
}


/** @brief Trivial destructor. */
RemoteDebugSetTeamFlagsRequest::~RemoteDebugSetTeamFlagsRequest()
{
}


/**
 * @brief Configures the request with the team-debug flag bitmask to apply.
 *
 * @param flags  Bitmask of B_TEAM_DEBUG_* flags.
 */
void
RemoteDebugSetTeamFlagsRequest::SetTo(int32 flags)
{
	fFlags = flags;
}


/** @brief Returns the wire type code for set-team-flags requests. */
remote_request_type
RemoteDebugSetTeamFlagsRequest::Type() const
{
	return REMOTE_REQUEST_TYPE_SET_TEAM_FLAGS;
}


/**
 * @brief Reads the flags field from @a data.
 *
 * @param data  Source message.
 * @return B_OK on success, B_BAD_VALUE if "flags" is missing.
 */
status_t
RemoteDebugSetTeamFlagsRequest::LoadSpecificInfoFromMessage(
	const BMessage& data)
{
	if (data.FindInt32("flags", &fFlags) != B_OK)
		return B_BAD_VALUE;

	return B_OK;
}


/**
 * @brief Writes the flags field into @a _output.
 *
 * @param _output  Destination message.
 * @return Result from BMessage::AddInt32().
 */
status_t
RemoteDebugSetTeamFlagsRequest::SaveSpecificInfoToMessage(
	BMessage& _output) const
{
	return _output.AddInt32("flags", fFlags);
}


// #pragma mark - RemoteDebugSetThreadFlagsRequest


/** @brief Default-constructs an empty set-thread-flags request. */
RemoteDebugSetThreadFlagsRequest::RemoteDebugSetThreadFlagsRequest()
	:
	RemoteDebugRequest(),
	fThread(-1),
	fFlags(0)
{
}


/** @brief Trivial destructor. */
RemoteDebugSetThreadFlagsRequest::~RemoteDebugSetThreadFlagsRequest()
{
}


/**
 * @brief Configures the request with the target thread and flag bitmask.
 *
 * @param thread  Thread id to update.
 * @param flags   Bitmask of B_THREAD_DEBUG_* flags.
 */
void
RemoteDebugSetThreadFlagsRequest::SetTo(thread_id thread, int32 flags)
{
	fThread = thread;
	fFlags = flags;
}


remote_request_type
RemoteDebugSetThreadFlagsRequest::Type() const
{
	return REMOTE_REQUEST_TYPE_SET_THREAD_FLAGS;
}


status_t
RemoteDebugSetThreadFlagsRequest::LoadSpecificInfoFromMessage(
	const BMessage& data)
{
	if (data.FindInt32("thread", &fThread) != B_OK)
		return B_BAD_VALUE;

	if (data.FindInt32("flags", &fFlags) != B_OK)
		return B_BAD_VALUE;

	return B_OK;
}


status_t
RemoteDebugSetThreadFlagsRequest::SaveSpecificInfoToMessage(
	BMessage& _output) const
{
	status_t error = _output.AddInt32("thread", fThread);
	if (error != B_OK)
		return error;

	return _output.AddInt32("flags", fFlags);
}


// #pragma mark - RemoteDebugThreadActionRequest


/** @brief Default-constructs an empty thread-action request. */
RemoteDebugThreadActionRequest::RemoteDebugThreadActionRequest()
	:
	RemoteDebugRequest(),
	fThread(-1)
{
}


/** @brief Trivial destructor. */
RemoteDebugThreadActionRequest::~RemoteDebugThreadActionRequest()
{
}


/**
 * @brief Sets the target thread id for the action.
 *
 * @param thread  Thread id this action targets.
 */
void
RemoteDebugThreadActionRequest::SetTo(thread_id thread)
{
	fThread = thread;
}


/**
 * @brief Reads the thread id from @a data.
 *
 * @param data  Source message.
 * @return B_OK on success, B_BAD_VALUE if "thread" is missing.
 */
status_t
RemoteDebugThreadActionRequest::LoadSpecificInfoFromMessage(
	const BMessage& data)
{
	if (data.FindInt32("thread", &fThread) != B_OK)
		return B_BAD_VALUE;

	return B_OK;
}


/**
 * @brief Writes the thread id into @a _output.
 *
 * @param _output  Destination message.
 * @return Result from BMessage::AddInt32().
 */
status_t
RemoteDebugThreadActionRequest::SaveSpecificInfoToMessage(
	BMessage& _output) const
{
	return _output.AddInt32("thread", fThread);
}


// #pragma mark - RemoteDebugContinueThreadRequest


/** @brief Default-constructs a continue-thread request. */
RemoteDebugContinueThreadRequest::RemoteDebugContinueThreadRequest()
	:
	RemoteDebugThreadActionRequest()
{
}


/** @brief Trivial destructor. */
RemoteDebugContinueThreadRequest::~RemoteDebugContinueThreadRequest()
{
}

/** @brief Returns the wire type code for continue-thread requests. */
remote_request_type
RemoteDebugContinueThreadRequest::Type() const
{
	return REMOTE_REQUEST_TYPE_CONTINUE_THREAD;
}


// #pragma mark - RemoteDebugStopThreadRequest


/** @brief Default-constructs a stop-thread request. */
RemoteDebugStopThreadRequest::RemoteDebugStopThreadRequest()
	:
	RemoteDebugThreadActionRequest()
{
}


/** @brief Trivial destructor. */
RemoteDebugStopThreadRequest::~RemoteDebugStopThreadRequest()
{
}

/** @brief Returns the wire type code for stop-thread requests. */
remote_request_type
RemoteDebugStopThreadRequest::Type() const
{
	return REMOTE_REQUEST_TYPE_STOP_THREAD;
}


// #pragma mark - RemoteDebugSingleStepThreadRequest


/** @brief Default-constructs a single-step-thread request. */
RemoteDebugSingleStepThreadRequest::RemoteDebugSingleStepThreadRequest()
	:
	RemoteDebugThreadActionRequest()
{
}


/** @brief Trivial destructor. */
RemoteDebugSingleStepThreadRequest::~RemoteDebugSingleStepThreadRequest()
{
}

/** @brief Returns the wire type code for single-step-thread requests. */
remote_request_type
RemoteDebugSingleStepThreadRequest::Type() const
{
	return REMOTE_REQUEST_TYPE_SINGLE_STEP_THREAD;
}


// #pragma mark - RemoteDebugGetCpuStateRequest


/** @brief Default-constructs a get-CPU-state request. */
RemoteDebugGetCpuStateRequest::RemoteDebugGetCpuStateRequest()
	:
	RemoteDebugThreadActionRequest()
{
}


/** @brief Trivial destructor. */
RemoteDebugGetCpuStateRequest::~RemoteDebugGetCpuStateRequest()
{
}


/** @brief Returns the wire type code for get-CPU-state requests. */
remote_request_type
RemoteDebugGetCpuStateRequest::Type() const
{
	return REMOTE_REQUEST_TYPE_GET_CPU_STATE;
}


// #pragma mark - RemoteDebugSetCpuStateRequest


/** @brief Default-constructs an empty set-CPU-state request. */
RemoteDebugSetCpuStateRequest::RemoteDebugSetCpuStateRequest()
	:
	RemoteDebugRequest(),
	fThread(-1),
	fCpuState(NULL)
{
}


/** @brief Releases the held CPU state reference, if any. */
RemoteDebugSetCpuStateRequest::~RemoteDebugSetCpuStateRequest()
{
	if (fCpuState != NULL)
		fCpuState->ReleaseReference();
}


/**
 * @brief Configures the request with the target thread and a CPU state.
 *
 * @param thread  Thread id whose state is being overwritten.
 * @param state   New CPU state; the request acquires a reference.
 */
void
RemoteDebugSetCpuStateRequest::SetTo(thread_id thread, CpuState* state)
{
	fThread = thread;
	fCpuState = state;
	if (fCpuState != NULL)
		fCpuState->AcquireReference();
}


/** @brief Returns the wire type code for set-CPU-state requests. */
remote_request_type
RemoteDebugSetCpuStateRequest::Type() const
{
	return REMOTE_REQUEST_TYPE_SET_CPU_STATE;
}


/**
 * @brief Reads thread id and architecture-sized CPU state blob from @a data.
 *
 * @param data  Source message.
 * @return B_OK on success, B_BAD_VALUE if any field is missing or malformed,
 *         or any error from Architecture::CreateCpuState().
 */
status_t
RemoteDebugSetCpuStateRequest::LoadSpecificInfoFromMessage(
	const BMessage& data)
{
	if (data.FindInt32("thread", &fThread) != B_OK)
		return B_BAD_VALUE;

	if (fCpuState != NULL) {
		fCpuState->ReleaseReference();
		fCpuState = NULL;
	}

	const uint8* buffer = NULL;
	ssize_t numBytes = 0;
	size_t stateSize = GetArchitecture()->DebugCpuStateSize();
	status_t error = data.FindData("state", B_RAW_TYPE, (const void**)&buffer,
		&numBytes);
	if (error != B_OK || (size_t)numBytes != stateSize)
		return B_BAD_VALUE;

	return GetArchitecture()->CreateCpuState(buffer, stateSize, fCpuState);
}


/**
 * @brief Writes thread id and architecture-sized CPU state blob into @a _output.
 *
 * @param _output  Destination message.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or any error
 *         from CpuState::UpdateDebugState() or BMessage::AddInt32()/AddData().
 */
status_t
RemoteDebugSetCpuStateRequest::SaveSpecificInfoToMessage(
	BMessage& _output) const
{
	status_t error = _output.AddInt32("thread", fThread);
	if (error != B_OK)
		return error;

	size_t stateSize = GetArchitecture()->DebugCpuStateSize();
	uint8* buffer = new(std::nothrow) uint8[stateSize];
	if (buffer == NULL)
		return B_NO_MEMORY;

	ArrayDeleter<uint8> deleter(buffer);
	error = fCpuState->UpdateDebugState(buffer, stateSize);
	if (error != B_OK)
		return error;

	return _output.AddData("state", B_RAW_TYPE, buffer, (ssize_t)stateSize);
}


// #pragma mark - RemoteDebugAddressActionRequest


/** @brief Default-constructs an empty address-action request. */
RemoteDebugAddressActionRequest::RemoteDebugAddressActionRequest()
	:
	RemoteDebugRequest(),
	fAddress(0)
{
}


/** @brief Trivial destructor. */
RemoteDebugAddressActionRequest::~RemoteDebugAddressActionRequest()
{
}


/**
 * @brief Sets the target address for the action.
 *
 * @param address  Target-side address this action operates on.
 */
void
RemoteDebugAddressActionRequest::SetTo(target_addr_t address)
{
	fAddress = address;
}


/**
 * @brief Reads the address field from @a data.
 *
 * @param data  Source message.
 * @return Result from BMessage::FindUInt64().
 */
status_t
RemoteDebugAddressActionRequest::LoadSpecificInfoFromMessage(
	const BMessage& data)
{
	return data.FindUInt64("address", &fAddress);
}


/**
 * @brief Writes the address field into @a _output.
 *
 * @param _output  Destination message.
 * @return Result from BMessage::AddUInt64().
 */
status_t
RemoteDebugAddressActionRequest::SaveSpecificInfoToMessage(
	BMessage& _output) const
{
	return _output.AddUInt64("address", fAddress);
}


// #pragma mark - RemoteDebugInstallBreakpointRequest


/** @brief Default-constructs an install-breakpoint request. */
RemoteDebugInstallBreakpointRequest::RemoteDebugInstallBreakpointRequest()
	:
	RemoteDebugAddressActionRequest()
{
}


/** @brief Trivial destructor. */
RemoteDebugInstallBreakpointRequest::~RemoteDebugInstallBreakpointRequest()
{
}


/** @brief Returns the wire type code for install-breakpoint requests. */
remote_request_type
RemoteDebugInstallBreakpointRequest::Type() const
{
	return REMOTE_REQUEST_TYPE_INSTALL_BREAKPOINT;
}


// #pragma mark - RemoteDebugUninstallBreakpointRequest


/** @brief Default-constructs an uninstall-breakpoint request. */
RemoteDebugUninstallBreakpointRequest::RemoteDebugUninstallBreakpointRequest()
	:
	RemoteDebugAddressActionRequest()
{
}


/** @brief Trivial destructor. */
RemoteDebugUninstallBreakpointRequest::~RemoteDebugUninstallBreakpointRequest()
{
}

/** @brief Returns the wire type code for uninstall-breakpoint requests. */
remote_request_type
RemoteDebugUninstallBreakpointRequest::Type() const
{
	return REMOTE_REQUEST_TYPE_UNINSTALL_BREAKPOINT;
}


// #pragma mark - RemoteDebugInstallWatchpointRequest


/** @brief Default-constructs an empty install-watchpoint request. */
RemoteDebugInstallWatchpointRequest::RemoteDebugInstallWatchpointRequest()
	:
	RemoteDebugRequest(),
	fAddress(0),
	fWatchType(B_DATA_READ_WATCHPOINT),
	fLength(0)
{
}


/** @brief Trivial destructor. */
RemoteDebugInstallWatchpointRequest::~RemoteDebugInstallWatchpointRequest()
{
}


/**
 * @brief Configures the request with watch parameters.
 *
 * @param address  Target address to watch.
 * @param type     B_DATA_*_WATCHPOINT trigger type.
 * @param length   Watch length in bytes.
 */
void
RemoteDebugInstallWatchpointRequest::SetTo(target_addr_t address, uint32 type,
	int32 length)
{
	fAddress = address;
	fWatchType = type;
	fLength = length;
}


/** @brief Returns the wire type code for install-watchpoint requests. */
remote_request_type
RemoteDebugInstallWatchpointRequest::Type() const
{
	return REMOTE_REQUEST_TYPE_INSTALL_WATCHPOINT;
}


/**
 * @brief Reads address, watch-type, and length fields from @a data.
 *
 * @param data  Source message.
 * @return B_OK on success or any error from BMessage::Find*().
 */
status_t
RemoteDebugInstallWatchpointRequest::LoadSpecificInfoFromMessage(
	const BMessage& data)
{
	status_t error = data.FindUInt64("address", &fAddress);
	if (error != B_OK)
		return error;

	error = data.FindUInt32("watchtype", &fWatchType);
	if (error != B_OK)
		return error;

	return data.FindInt32("length", &fLength);
}


/**
 * @brief Writes address, watch-type, and length fields into @a _output.
 *
 * @param _output  Destination message.
 * @return B_OK on success or any error from BMessage::Add*().
 */
status_t
RemoteDebugInstallWatchpointRequest::SaveSpecificInfoToMessage(
	BMessage& _output) const
{
	status_t error = _output.AddUInt64("address", fAddress);
	if (error != B_OK)
		return error;

	error = _output.AddUInt32("watchtype", fWatchType);
	if (error != B_OK)
		return error;

	return _output.AddInt32("length", fLength);
}


// #pragma mark - RemoteDebugUninstallWatchpointRequest


/** @brief Default-constructs an uninstall-watchpoint request. */
RemoteDebugUninstallWatchpointRequest::RemoteDebugUninstallWatchpointRequest()
	:
	RemoteDebugAddressActionRequest()
{
}


/** @brief Trivial destructor. */
RemoteDebugUninstallWatchpointRequest::~RemoteDebugUninstallWatchpointRequest()
{
}


/** @brief Returns the wire type code for uninstall-watchpoint requests. */
remote_request_type
RemoteDebugUninstallWatchpointRequest::Type() const
{
	return REMOTE_REQUEST_TYPE_UNINSTALL_WATCHPOINT;
}


// #pragma mark - RemoteDebugReadMemoryResponse


/** @brief Default-constructs an empty read-memory response. */
RemoteDebugReadMemoryResponse::RemoteDebugReadMemoryResponse()
	:
	RemoteDebugResponse(),
	fData(NULL),
	fSize(0)
{
}


/** @brief Frees the buffered payload, if any. */
RemoteDebugReadMemoryResponse::~RemoteDebugReadMemoryResponse()
{
	if (fData != NULL)
		free(fData);
}


/**
 * @brief Sets the buffer and size returned from a read-memory request.
 *
 * @param data  Pointer to the buffer; ownership transfers to the response.
 * @param size  Number of bytes in @a data.
 */
void
RemoteDebugReadMemoryResponse::SetTo(void* data, target_size_t size)
{
	fData = data;
	fSize = size;
}


/**
 * @brief Reads the size and raw payload from @a data.
 *
 * @param data  Source message.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or
 *         B_MISMATCHED_VALUES if the payload size doesn't match the size field.
 */
status_t
RemoteDebugReadMemoryResponse::LoadSpecificInfoFromMessage(
	const BMessage& data)
{
	status_t error = data.FindUInt64("size", &fSize);
	if (error != B_OK)
		return error;

	fData = malloc(fSize);
	if (fData == NULL)
		return B_NO_MEMORY;

	const void* messageData = NULL;
	ssize_t numBytes = -1;
	error = data.FindData("data", B_RAW_TYPE, &messageData, &numBytes);
	if (error != B_OK)
		return error;

	if ((size_t)numBytes != fSize)
		return B_MISMATCHED_VALUES;

	memcpy(fData, messageData, numBytes);
	return B_OK;
}


/**
 * @brief Writes size and raw payload into @a _output.
 *
 * @param _output  Destination message.
 * @return B_OK on success or any error from BMessage::AddUInt64()/AddData().
 *         If fData is NULL the call returns B_OK without writing anything.
 */
status_t
RemoteDebugReadMemoryResponse::SaveSpecificInfoToMessage(
	BMessage& _output) const
{
	if (fData == NULL)
		return B_OK;

	status_t error = _output.AddUInt64("size", fSize);
	if (error != B_OK)
		return error;

	return _output.AddData("data", B_RAW_TYPE, fData, (ssize_t)fSize);
}


// #pragma mark - RemoteDebugGetCpuStateResponse


/** @brief Default-constructs an empty get-CPU-state response. */
RemoteDebugGetCpuStateResponse::RemoteDebugGetCpuStateResponse()
	:
	RemoteDebugResponse(),
	fCpuState(NULL)
{
}


/** @brief Releases the held CPU state reference, if any. */
RemoteDebugGetCpuStateResponse::~RemoteDebugGetCpuStateResponse()
{
	if (fCpuState != NULL)
		fCpuState->ReleaseReference();
}


/**
 * @brief Sets the CPU state returned by the request.
 *
 * @param state  CPU state instance; the response acquires a reference.
 */
void
RemoteDebugGetCpuStateResponse::SetTo(CpuState* state)
{
	fCpuState = state;
	if (fCpuState != NULL)
		fCpuState->AcquireReference();
}


/**
 * @brief Reads an architecture-sized CPU state blob from @a data.
 *
 * @param data  Source message.
 * @return B_OK on success, B_BAD_VALUE if the payload is missing or wrong size,
 *         or any error from Architecture::CreateCpuState().
 */
status_t
RemoteDebugGetCpuStateResponse::LoadSpecificInfoFromMessage(
	const BMessage& data)
{
	if (fCpuState != NULL) {
		fCpuState->ReleaseReference();
		fCpuState = NULL;
	}

	const uint8* buffer = NULL;
	ssize_t numBytes = 0;
	size_t stateSize = GetArchitecture()->DebugCpuStateSize();
	status_t error = data.FindData("state", B_RAW_TYPE, (const void**)&buffer,
		&numBytes);
	if (error != B_OK || (size_t)numBytes != stateSize)
		return B_BAD_VALUE;

	return GetArchitecture()->CreateCpuState(buffer, stateSize, fCpuState);
}


/**
 * @brief Writes an architecture-sized CPU state blob into @a _output.
 *
 * @param _output  Destination message.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or any error
 *         from CpuState::UpdateDebugState() or BMessage::AddData().
 */
status_t
RemoteDebugGetCpuStateResponse::SaveSpecificInfoToMessage(
	BMessage& _output) const
{
	size_t stateSize = GetArchitecture()->DebugCpuStateSize();
	uint8* buffer = new(std::nothrow) uint8[stateSize];
	if (buffer == NULL)
		return B_NO_MEMORY;

	ArrayDeleter<uint8> deleter(buffer);
	status_t error = fCpuState->UpdateDebugState(buffer, stateSize);
	if (error != B_OK)
		return error;

	return _output.AddData("state", B_RAW_TYPE, buffer, (ssize_t)stateSize);
}
