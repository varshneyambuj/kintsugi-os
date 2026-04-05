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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2005 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Pahtz <pahtz@yahoo.com.au>
 *       Axel Dörfler, axeld@pinc-software.de
 */

/**
 * @file LinkSender.cpp
 * @brief Implementation of BPrivate::LinkSender for low-level port-based message sending.
 *
 * Provides the sender side of the lightweight inter-process communication
 * mechanism used between client applications and the app_server. Messages are
 * assembled in an internal buffer and flushed to a kernel port. Supports
 * attaching typed data including primitives, strings, regions, shapes, and
 * gradients. Large payloads exceeding kMaxBufferSize are transferred via
 * shared memory areas.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>

#include <Gradient.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <GradientRadialFocus.h>
#include <GradientDiamond.h>
#include <GradientConic.h>
#include <Region.h>
#include <Shape.h>
#include <ShapePrivate.h>

#include <ServerProtocol.h>
#include <LinkSender.h>

#include "link_message.h"
#include "syscalls.h"

//#define DEBUG_BPORTLINK
#ifdef DEBUG_BPORTLINK
#	include <stdio.h>
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif

//#define TRACE_SERVER_LINK_GRADIENTS
#ifdef TRACE_SERVER_LINK_GRADIENTS
#	include <OS.h>
#	define GTRACE(x) debug_printf x
#else
#	define GTRACE(x) ;
#endif

static const size_t kMaxStringSize = 4096;
static const size_t kWatermark = kInitialBufferSize - 24;
	// if a message is started after this mark, the buffer is flushed automatically

namespace BPrivate {

/** @brief Constructs a LinkSender attached to the specified port.
 *  @param port The kernel port ID to which messages will be sent.
 */
LinkSender::LinkSender(port_id port)
	:
	fPort(port),
	fTargetTeam(-1),
	fBuffer(NULL),
	fBufferSize(0),

	fCurrentEnd(0),
	fCurrentStart(0),
	fCurrentStatus(B_OK)
{
}


/** @brief Destroys the LinkSender and frees the internal send buffer. */
LinkSender::~LinkSender()
{
	free(fBuffer);
}


/** @brief Sets the kernel port to which messages will be sent.
 *  @param port The new destination port ID.
 */
void
LinkSender::SetPort(port_id port)
{
	fPort = port;
}


/** @brief Begins composing a new message with the given code.
 *
 *  Finalizes (or cancels) any previously started message, then writes a
 *  new message_header into the buffer. If necessary, the buffer is flushed
 *  to make room for the new message.
 *
 *  @param code The message code identifying this message type.
 *  @param minSize Minimum payload size hint (excluding the header) used to
 *                 pre-allocate buffer space. Defaults to 0.
 *  @return B_OK on success, B_NO_MEMORY if buffer allocation fails, or an
 *          error from Flush().
 */
status_t
LinkSender::StartMessage(int32 code, size_t minSize)
{
	// end previous message
	if (EndMessage() < B_OK)
		CancelMessage();

	if (minSize > kMaxBufferSize - sizeof(message_header)) {
		// we will handle this case in Attach, using an area
		minSize = sizeof(area_id);
	}

	minSize += sizeof(message_header);

	// Eventually flush buffer to make space for the new message.
	// Note, we do not take the actual buffer size into account to not
	// delay the time between buffer flushes too much.
	if (fBufferSize > 0 && (minSize > SpaceLeft() || fCurrentStart >= kWatermark)) {
		status_t status = Flush();
		if (status < B_OK)
			return status;
	}

	if (minSize > fBufferSize) {
		if (AdjustBuffer(minSize) != B_OK)
			return fCurrentStatus = B_NO_MEMORY;
	}

	message_header *header = (message_header *)(fBuffer + fCurrentStart);
	header->size = 0;
		// will be set later
	header->code = code;
	header->flags = 0;

	STRACE(("info: LinkSender buffered header %ld (%lx) [%lu %lu %lu].\n",
		code, code, header->size, header->code, header->flags));

	fCurrentEnd += sizeof(message_header);
	return B_OK;
}


/** @brief Finalizes the current message being composed.
 *
 *  Records the total message size in the header and optionally sets the
 *  needsReply flag. After this call, the buffer write position advances
 *  to the start of the next message slot.
 *
 *  @param needsReply If true, the kNeedsReply flag is set in the message
 *                    header so the receiver knows a reply is expected.
 *  @return B_OK on success, or the current error status if the message
 *          was not properly started.
 */
status_t
LinkSender::EndMessage(bool needsReply)
{
	if (fCurrentEnd == fCurrentStart || fCurrentStatus < B_OK)
		return fCurrentStatus;

	// record the size of the message
	message_header *header = (message_header *)(fBuffer + fCurrentStart);
	header->size = CurrentMessageSize();
	if (needsReply)
		header->flags |= needsReply;

	STRACE(("info: LinkSender EndMessage() of size %ld.\n", header->size));

	// bump to start of next message
	fCurrentStart = fCurrentEnd;
	return B_OK;
}


/** @brief Discards the current in-progress message.
 *
 *  Rewinds the buffer write position back to the start of the current
 *  message and resets the error status, effectively canceling the message
 *  without sending it.
 */
void
LinkSender::CancelMessage()
{
	fCurrentEnd = fCurrentStart;
	fCurrentStatus = B_OK;
}


/** @brief Appends raw data to the current message.
 *
 *  Copies the supplied data into the send buffer as part of the current
 *  message payload. For very large payloads (>= kMaxBufferSize), data is
 *  transferred via a shared memory area instead of being buffered inline.
 *  If insufficient buffer space remains, completed messages are flushed
 *  first.
 *
 *  @param passedData Pointer to the data to attach.
 *  @param passedSize Number of bytes to attach.
 *  @return B_OK on success, B_BAD_VALUE if size is zero, B_NO_INIT if
 *          StartMessage() has not been called, or another error code on
 *          failure.
 */
status_t
LinkSender::Attach(const void *passedData, size_t passedSize)
{
	size_t size = passedSize;
	const void* data = passedData;

	if (fCurrentStatus < B_OK)
		return fCurrentStatus;

	if (size == 0)
		return fCurrentStatus = B_BAD_VALUE;

	if (fCurrentEnd == fCurrentStart)
		return B_NO_INIT;	// need to call StartMessage() first

	bool useArea = false;
	if (size >= kMaxBufferSize) {
		useArea = true;
		size = sizeof(area_id);
	}

	if (SpaceLeft() < size) {
		// we have to make space for the data

		status_t status = FlushCompleted(size + CurrentMessageSize());
		if (status < B_OK)
			return fCurrentStatus = status;
	}

	area_id senderArea = -1;
	if (useArea) {
		if (fTargetTeam < 0) {
			port_info info;
			status_t result = get_port_info(fPort, &info);
			if (result != B_OK)
				return result;
			fTargetTeam = info.team;
		}
		void* address = NULL;
		off_t alignedSize = (passedSize + B_PAGE_SIZE) & ~(B_PAGE_SIZE - 1);
		senderArea = create_area("LinkSenderArea", &address, B_ANY_ADDRESS,
			alignedSize, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);

		if (senderArea < B_OK)
			return senderArea;

		data = &senderArea;
		memcpy(address, passedData, passedSize);

		area_id areaID = senderArea;
		senderArea = _kern_transfer_area(senderArea, &address,
			B_ANY_ADDRESS, fTargetTeam);

		if (senderArea < B_OK) {
			delete_area(areaID);
			return senderArea;
		}
	}

	memcpy(fBuffer + fCurrentEnd, data, size);
	fCurrentEnd += size;

	return B_OK;
}


/** @brief Appends a length-prefixed string to the current message.
 *
 *  Writes a 32-bit length prefix followed by the string data. If the
 *  string is NULL, an empty string is attached. Strings exceeding
 *  kMaxStringSize are truncated to zero length.
 *
 *  @param string The null-terminated string to attach, or NULL for empty.
 *  @param length The number of characters to attach, or -1 to use the
 *                full string length.
 *  @return B_OK on success, or an error code from Attach().
 */
status_t
LinkSender::AttachString(const char *string, int32 length)
{
	if (string == NULL)
		string = "";

	size_t maxLength = strlen(string);
	if (length == -1) {
		length = (int32)maxLength;

		// we should report an error here
		if (maxLength > kMaxStringSize)
			length = 0;
	} else if (length > (int32)maxLength)
		length = maxLength;

	status_t status = Attach<int32>(length);
	if (status < B_OK)
		return status;

	if (length > 0) {
		status = Attach(string, length);
		if (status < B_OK)
			fCurrentEnd -= sizeof(int32);	// rewind the transaction
	}

	return status;
}


/** @brief Serializes and appends a BRegion to the current message.
 *
 *  Writes the region's rectangle count, bounding rectangle, and
 *  individual clipping rectangles into the message buffer.
 *
 *  @param region The BRegion to serialize and attach.
 *  @return B_OK on success, or an error code from Attach().
 */
status_t
LinkSender::AttachRegion(const BRegion& region)
{
	Attach(&region.fCount, sizeof(int32));
	if (region.fCount > 0) {
		Attach(&region.fBounds, sizeof(clipping_rect));
		return Attach(region.fData,
			region.fCount * sizeof(clipping_rect));
	}

	return Attach(&region.fBounds, sizeof(clipping_rect));
}


/** @brief Serializes and appends a BShape to the current message.
 *
 *  Writes the shape's operation count, point count, operation list, and
 *  point list into the message buffer.
 *
 *  @param shape The BShape to serialize and attach.
 *  @return B_OK unconditionally (individual Attach errors are deferred).
 */
status_t
LinkSender::AttachShape(BShape& shape)
{
	int32 opCount, ptCount;
	uint32* opList;
	BPoint* ptList;

	BShape::Private(shape).GetData(&opCount, &ptCount, &opList, &ptList);

	Attach(&opCount, sizeof(int32));
	Attach(&ptCount, sizeof(int32));
	if (opCount > 0)
		Attach(opList, opCount * sizeof(uint32));
	if (ptCount > 0)
		Attach(ptList, ptCount * sizeof(BPoint));
	return B_OK;
}


/** @brief Serializes and appends a BGradient to the current message.
 *
 *  Writes the gradient type, color stop count, individual color stops,
 *  and type-specific parameters (e.g., start/end for linear, center/radius
 *  for radial) into the message buffer.
 *
 *  @param gradient The BGradient to serialize and attach.
 *  @return B_OK on success, or an error code from Attach().
 */
status_t
LinkSender::AttachGradient(const BGradient& gradient)
{
	GTRACE(("ServerLink::AttachGradient\n"));
	BGradient::Type gradientType = gradient.GetType();
	int32 stopCount = gradient.CountColorStops();
	GTRACE(("ServerLink::AttachGradient> color stop count == %d\n",
		(int)stopCount));
	Attach(&gradientType, sizeof(BGradient::Type));
	Attach(&stopCount, sizeof(int32));
	if (stopCount > 0) {
		for (int i = 0; i < stopCount; i++) {
			Attach(gradient.ColorStopAtFast(i),
				sizeof(BGradient::ColorStop));
		}
	}

	switch (gradientType) {
		case BGradient::TYPE_LINEAR:
		{
			GTRACE(("ServerLink::AttachGradient> type == TYPE_LINEAR\n"));
			const BGradientLinear* linear = (BGradientLinear*)&gradient;
			Attach(linear->Start());
			Attach(linear->End());
			break;
		}
		case BGradient::TYPE_RADIAL:
		{
			GTRACE(("ServerLink::AttachGradient> type == TYPE_RADIAL\n"));
			const BGradientRadial* radial = (BGradientRadial*)&gradient;
			BPoint center = radial->Center();
			float radius = radial->Radius();
			Attach(&center, sizeof(BPoint));
			Attach(&radius, sizeof(float));
			break;
		}
		case BGradient::TYPE_RADIAL_FOCUS:
		{
			GTRACE(("ServerLink::AttachGradient> type == TYPE_RADIAL_FOCUS\n"));
			const BGradientRadialFocus* radialFocus
				= (BGradientRadialFocus*)&gradient;
			BPoint center = radialFocus->Center();
			BPoint focal = radialFocus->Focal();
			float radius = radialFocus->Radius();
			Attach(&center, sizeof(BPoint));
			Attach(&focal, sizeof(BPoint));
			Attach(&radius, sizeof(float));
			break;
		}
		case BGradient::TYPE_DIAMOND:
		{
			GTRACE(("ServerLink::AttachGradient> type == TYPE_DIAMOND\n"));
			const BGradientDiamond* diamond = (BGradientDiamond*)&gradient;
			BPoint center = diamond->Center();
			Attach(&center, sizeof(BPoint));
			break;
		}
		case BGradient::TYPE_CONIC:
		{
			GTRACE(("ServerLink::AttachGradient> type == TYPE_CONIC\n"));
			const BGradientConic* conic = (BGradientConic*)&gradient;
			BPoint center = conic->Center();
			float angle = conic->Angle();
			Attach(&center, sizeof(BPoint));
			Attach(&angle, sizeof(float));
			break;
		}
		case BGradient::TYPE_NONE:
		{
			GTRACE(("ServerLink::AttachGradient> type == TYPE_NONE\n"));
			break;
		}
	}
	return B_OK;
}


/** @brief Resizes the internal send buffer to accommodate the given size.
 *
 *  Allocates a new buffer if the current one is too small. The size is
 *  clamped to at least kInitialBufferSize and at most kMaxBufferSize,
 *  rounded up to a page boundary for larger sizes.
 *
 *  @param newSize The minimum required buffer size in bytes.
 *  @param _oldBuffer If non-NULL, receives the pointer to the previous
 *                    buffer (caller must free it). If NULL, the old buffer
 *                    is freed automatically.
 *  @return B_OK on success, B_NO_MEMORY if allocation fails, or
 *          B_BUFFER_OVERFLOW if newSize exceeds kMaxBufferSize.
 */
status_t
LinkSender::AdjustBuffer(size_t newSize, char **_oldBuffer)
{
	// make sure the new size is within bounds
	if (newSize <= kInitialBufferSize)
		newSize = kInitialBufferSize;
	else if (newSize > kMaxBufferSize)
		return B_BUFFER_OVERFLOW;
	else if (newSize > kInitialBufferSize)
		newSize = (newSize + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);

	if (newSize == fBufferSize) {
		// keep existing buffer
		if (_oldBuffer)
			*_oldBuffer = fBuffer;
		return B_OK;
	}

	// create new larger buffer
	char *buffer = (char *)malloc(newSize);
	if (buffer == NULL)
		return B_NO_MEMORY;

	if (_oldBuffer)
		*_oldBuffer = fBuffer;
	else
		free(fBuffer);

	fBuffer = buffer;
	fBufferSize = newSize;
	return B_OK;
}


/** @brief Flushes all completed messages and preserves the current incomplete one.
 *
 *  Temporarily hides the in-progress message, flushes all completed
 *  messages to the port, then moves the incomplete message to the start
 *  of a (potentially resized) buffer so composition can continue.
 *
 *  @param newBufferSize The minimum buffer size needed after the flush,
 *                       accounting for the incomplete message and new data.
 *  @return B_OK on success, or an error code from Flush() or AdjustBuffer().
 */
status_t
LinkSender::FlushCompleted(size_t newBufferSize)
{
	// we need to hide the incomplete message so that it's not flushed
	int32 end = fCurrentEnd;
	int32 start = fCurrentStart;
	fCurrentEnd = fCurrentStart;

	status_t status = Flush();
	if (status < B_OK) {
		fCurrentEnd = end;
		return status;
	}

	char *oldBuffer = NULL;
	status = AdjustBuffer(newBufferSize, &oldBuffer);
	if (status != B_OK)
		return status;

	// move the incomplete message to the start of the buffer
	fCurrentEnd = end - start;
	if (oldBuffer != fBuffer) {
		memcpy(fBuffer, oldBuffer + start, fCurrentEnd);
		free(oldBuffer);
	} else
		memmove(fBuffer, fBuffer + start, fCurrentEnd);

	return B_OK;
}


/** @brief Writes all buffered messages to the destination port.
 *
 *  Finalizes the current message (if any), then writes the entire buffer
 *  contents to the kernel port as a single port message. After a
 *  successful flush the buffer is reset.
 *
 *  @param timeout Maximum time to wait for the port write. Use
 *                 B_INFINITE_TIMEOUT to block indefinitely.
 *  @param needsReply If true, sets the needsReply flag on the last
 *                    message before flushing.
 *  @return B_OK on success, or an error code from write_port().
 */
status_t
LinkSender::Flush(bigtime_t timeout, bool needsReply)
{
	if (fCurrentStatus < B_OK)
		return fCurrentStatus;

	EndMessage(needsReply);
	if (fCurrentStart == 0)
		return B_OK;

	STRACE(("info: LinkSender Flush() waiting to send messages of %ld bytes on port %ld.\n",
		fCurrentEnd, fPort));

	status_t err;
	if (timeout != B_INFINITE_TIMEOUT) {
		do {
			err = write_port_etc(fPort, kLinkCode, fBuffer,
				fCurrentEnd, B_RELATIVE_TIMEOUT, timeout);
		} while (err == B_INTERRUPTED);
	} else {
		do {
			err = write_port(fPort, kLinkCode, fBuffer, fCurrentEnd);
		} while (err == B_INTERRUPTED);
	}

	if (err < B_OK) {
		STRACE(("error info: LinkSender Flush() failed for %ld bytes (%s) on port %ld.\n",
			fCurrentEnd, strerror(err), fPort));
		return err;
	}

	STRACE(("info: LinkSender Flush() messages total of %ld bytes on port %ld.\n",
		fCurrentEnd, fPort));

	fCurrentEnd = 0;
	fCurrentStart = 0;

	return B_OK;
}

}	// namespace BPrivate
