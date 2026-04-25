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
 *   Copyright 2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2012-2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file UiUtils.cpp
 * @brief Display-formatting helpers used across the debugger's UI layers.
 *
 * Collects string-formatting routines for thread states, BVariant values,
 * stack-frame names, area protection bitmasks, signal names/dispositions,
 * type codes, value-node graphs, and memory dumps. The helpers are static
 * so any UI front end (Tracker-based, ncurses, headless) can share them.
 */


#include "UiUtils.h"

#include <ctype.h>
#include <stdio.h>

#include <DateTime.h>
#include <KernelExport.h>
#include <Path.h>
#include <String.h>
#include <Variant.h>

#include <vm_defs.h>

#include "FunctionInstance.h"
#include "Image.h"
#include "RangeList.h"
#include "SignalDispositionTypes.h"
#include "StackFrame.h"
#include "Team.h"
#include "TeamMemoryBlock.h"
#include "Thread.h"
#include "Type.h"
#include "Value.h"
#include "ValueNode.h"


/**
 * @brief Convert a thread state and stop reason into a short label.
 *
 * @param state          One of THREAD_STATE_*.
 * @param stoppedReason  One of THREAD_STOPPED_* (only consulted when stopped).
 * @return Pointer to a static string describing the thread.
 */
/*static*/ const char*
UiUtils::ThreadStateToString(int state, int stoppedReason)
{
	switch (state) {
		case THREAD_STATE_RUNNING:
			return "Running";
		case THREAD_STATE_STOPPED:
			break;
		case THREAD_STATE_UNKNOWN:
		default:
			return "?";
	}

	// thread is stopped -- get the reason
	switch (stoppedReason) {
		case THREAD_STOPPED_DEBUGGER_CALL:
			return "Call";
		case THREAD_STOPPED_EXCEPTION:
			return "Exception";
		case THREAD_STOPPED_BREAKPOINT:
		case THREAD_STOPPED_WATCHPOINT:
		case THREAD_STOPPED_SINGLE_STEP:
		case THREAD_STOPPED_DEBUGGED:
		case THREAD_STOPPED_UNKNOWN:
		default:
			return "Debugged";
	}
}


/**
 * @brief Format a numeric BVariant in a width-appropriate hex/float style.
 *
 * Floats use "%.3g"; integers fall back to width-specific hex
 * (e.g. "0x%02x" for 8-bit values). Non-numeric values are returned via
 * BVariant::ToString() unchanged.
 *
 * @param value       Value to format.
 * @param buffer      Working buffer for numeric formats.
 * @param bufferSize  Capacity of @a buffer.
 * @return Pointer to either @a buffer or BVariant::ToString().
 */
/*static*/ const char*
UiUtils::VariantToString(const BVariant& value, char* buffer,
	size_t bufferSize)
{
	if (!value.IsNumber())
		return value.ToString();

	switch (value.Type()) {
		case B_FLOAT_TYPE:
		case B_DOUBLE_TYPE:
			snprintf(buffer, bufferSize, "%.3g", value.ToDouble());
			break;
		case B_INT8_TYPE:
		case B_UINT8_TYPE:
			snprintf(buffer, bufferSize, "0x%02x", value.ToUInt8());
			break;
		case B_INT16_TYPE:
		case B_UINT16_TYPE:
			snprintf(buffer, bufferSize, "0x%04x", value.ToUInt16());
			break;
		case B_INT32_TYPE:
		case B_UINT32_TYPE:
			snprintf(buffer, bufferSize, "0x%08" B_PRIx32,
				value.ToUInt32());
			break;
		case B_INT64_TYPE:
		case B_UINT64_TYPE:
		default:
			snprintf(buffer, bufferSize, "0x%016" B_PRIx64,
				value.ToUInt64());
			break;
	}

	return buffer;
}


/**
 * @brief Render the function/image name plus offset for a stack frame.
 *
 * Falls back to the image name when no FunctionInstance is available, and
 * to "?" when neither is known. The offset is the IP minus the function
 * (or image text) base, formatted as hex.
 *
 * @param frame       Frame whose label is being rendered.
 * @param buffer      Output buffer.
 * @param bufferSize  Capacity of @a buffer.
 * @return Pointer to @a buffer.
 */
/*static*/ const char*
UiUtils::FunctionNameForFrame(StackFrame* frame, char* buffer,
	size_t bufferSize)
{
	Image* image = frame->GetImage();
	FunctionInstance* function = frame->Function();
	if (image == NULL && function == NULL) {
		snprintf(buffer, bufferSize, "?");
		return buffer;
	}

	BString name;
	target_addr_t baseAddress;
	if (function != NULL) {
		name = function->PrettyName();
		baseAddress = function->Address();
	} else {
		name = image->Name();
		baseAddress = image->Info().TextBase();
	}

	snprintf(buffer, bufferSize, "%s + %#" B_PRIx64,
		name.String(), frame->InstructionPointer() - baseAddress);

	return buffer;
}


/**
 * @brief Convert an image_type enum into a short label.
 *
 * @param type        Image type.
 * @param buffer      Output buffer.
 * @param bufferSize  Capacity of @a buffer.
 * @return Pointer to @a buffer.
 */
/*static*/ const char*
UiUtils::ImageTypeToString(image_type type, char* buffer, size_t bufferSize)
{
	switch (type) {
		case B_APP_IMAGE:
			snprintf(buffer, bufferSize, "app");
			break;
		case B_LIBRARY_IMAGE:
			snprintf(buffer, bufferSize, "lib");
			break;
		case B_ADD_ON_IMAGE:
			snprintf(buffer, bufferSize, "add-on");
			break;
		case B_SYSTEM_IMAGE:
			snprintf(buffer, bufferSize, "system");
			break;
		default:
			snprintf(buffer, bufferSize, "unknown");
			break;
	}

	return buffer;
}


/**
 * @brief Convert an area locking-flag value into a short label.
 *
 * @param flags       Locking flag (B_NO_LOCK, B_FULL_LOCK, ...).
 * @param buffer      Output buffer.
 * @param bufferSize  Capacity of @a buffer.
 * @return Pointer to @a buffer.
 */
/*static*/ const char*
UiUtils::AreaLockingFlagsToString(uint32 flags, char* buffer,
	size_t bufferSize)
{
	switch (flags) {
		case B_NO_LOCK:
			snprintf(buffer, bufferSize, "none");
			break;
		case B_LAZY_LOCK:
			snprintf(buffer, bufferSize, "lazy");
			break;
		case B_FULL_LOCK:
			snprintf(buffer, bufferSize, "full");
			break;
		case B_CONTIGUOUS:
			snprintf(buffer, bufferSize, "contiguous");
			break;
		case B_LOMEM:
			snprintf(buffer, bufferSize, "lo-mem");
			break;
		case B_32_BIT_FULL_LOCK:
			snprintf(buffer, bufferSize, "32-bit full");
			break;
		case B_32_BIT_CONTIGUOUS:
			snprintf(buffer, bufferSize, "32-bit contig.");
			break;
		default:
			snprintf(buffer, bufferSize, "unknown");
			break;
	}

	return buffer;
}


/**
 * @brief Render an area-protection bitmask as a permission string (e.g. "rwxs").
 *
 * Shows user vs kernel protection bits separately, suppressing kernel
 * variants when an equivalent user variant is already present. Trailing
 * "s/o/c/S/k" letters denote stack/overcommit/cloneable/shared/kernel
 * areas. Unknown leftover bits are dumped in hex.
 *
 * @param protection  Protection bitmask.
 * @param _output     Output BString. Truncated and rewritten.
 * @return Reference to @a _output for chaining.
 */
/*static*/ const BString&
UiUtils::AreaProtectionFlagsToString(uint32 protection, BString& _output)
{
	#undef ADD_AREA_FLAG_IF_PRESENT
	#define ADD_AREA_FLAG_IF_PRESENT(flag, protection, name, output, missing)\
		if ((protection & flag) != 0) { \
			_output += name; \
			protection &= ~flag; \
		} else \
			_output += missing; \

	_output.Truncate(0);
	uint32 userFlags = protection & B_USER_PROTECTION;
	bool userProtectionPresent = userFlags != 0;
	ADD_AREA_FLAG_IF_PRESENT(B_READ_AREA, protection, "r", _output,
		userProtectionPresent ? "-" : " ");
	ADD_AREA_FLAG_IF_PRESENT(B_WRITE_AREA, protection, "w", _output,
		userProtectionPresent ? "-" : " ");
	ADD_AREA_FLAG_IF_PRESENT(B_EXECUTE_AREA, protection, "x", _output,
		userProtectionPresent ? "-" : " ");

	// if the user versions of these flags are present,
	// filter out their kernel equivalents since they're implied.
	if ((userFlags & B_READ_AREA) != 0)
		protection &= ~B_KERNEL_READ_AREA;
	if ((userFlags & B_WRITE_AREA) != 0)
		protection &= ~B_KERNEL_WRITE_AREA;
	if ((userFlags & B_EXECUTE_AREA) != 0)
		protection &= ~B_KERNEL_EXECUTE_AREA;

	if ((protection & B_KERNEL_PROTECTION) != 0) {
		ADD_AREA_FLAG_IF_PRESENT(B_KERNEL_READ_AREA, protection, "r",
			_output, "-");
		ADD_AREA_FLAG_IF_PRESENT(B_KERNEL_WRITE_AREA, protection, "w",
			_output, "-");
		ADD_AREA_FLAG_IF_PRESENT(B_KERNEL_EXECUTE_AREA, protection, "x",
			_output, "-");
	}

	ADD_AREA_FLAG_IF_PRESENT(B_STACK_AREA, protection, "s", _output, "");
	ADD_AREA_FLAG_IF_PRESENT(B_KERNEL_STACK_AREA, protection, "s", _output, "");
	ADD_AREA_FLAG_IF_PRESENT(B_OVERCOMMITTING_AREA, protection, _output, "o",
		"");
	ADD_AREA_FLAG_IF_PRESENT(B_CLONEABLE_AREA, protection, "c", _output, "");
	ADD_AREA_FLAG_IF_PRESENT(B_SHARED_AREA, protection, "S", _output, "");
	ADD_AREA_FLAG_IF_PRESENT(B_KERNEL_AREA, protection, "k", _output, "");

	if (protection != 0) {
		char buffer[32];
		snprintf(buffer, sizeof(buffer), ", u:(%#04" B_PRIx32 ")",
			protection);
		_output += buffer;
	}

	return _output;
}


/**
 * @brief Build a default report-file name for @a team using the current date/time.
 *
 * Produces "<leaf>-<id>-debug-DD-MM-YYYY-HH-MM-SS.report".
 *
 * @param team        Team being reported on.
 * @param buffer      Output buffer.
 * @param bufferSize  Capacity of @a buffer.
 * @return Pointer to @a buffer.
 */
/*static*/ const char*
UiUtils::ReportNameForTeam(::Team* team, char* buffer, size_t bufferSize)
{
	BPath teamPath(team->Name());
	BDateTime currentTime;
	currentTime.SetTime_t(time(NULL));
	snprintf(buffer, bufferSize, "%s-%" B_PRId32 "-debug-%02" B_PRId32 "-%02"
		B_PRId32 "-%02" B_PRId32 "-%02" B_PRId32 "-%02" B_PRId32 "-%02"
		B_PRId32 ".report", teamPath.Leaf(), team->ID(),
		currentTime.Date().Day(), currentTime.Date().Month(),
		currentTime.Date().Year(), currentTime.Time().Hour(),
		currentTime.Time().Minute(), currentTime.Time().Second());

	return buffer;
}


/**
 * @brief Build a default core-file name for @a team using the current date/time.
 *
 * Produces "<leaf>-<id>-debug-DD-MM-YYYY-HH-MM-SS.core".
 *
 * @param team        Team whose core is being saved.
 * @param buffer      Output buffer.
 * @param bufferSize  Capacity of @a buffer.
 * @return Pointer to @a buffer.
 */
/*static*/ const char*
UiUtils::CoreFileNameForTeam(::Team* team, char* buffer, size_t bufferSize)
{
	BPath teamPath(team->Name());
	BDateTime currentTime;
	currentTime.SetTime_t(time(NULL));
	snprintf(buffer, bufferSize, "%s-%" B_PRId32 "-debug-%02" B_PRId32 "-%02"
		B_PRId32 "-%02" B_PRId32 "-%02" B_PRId32 "-%02" B_PRId32 "-%02"
		B_PRId32 ".core", teamPath.Leaf(), team->ID(),
		currentTime.Date().Day(), currentTime.Date().Month(),
		currentTime.Date().Year(), currentTime.Time().Hour(),
		currentTime.Time().Minute(), currentTime.Time().Second());

	return buffer;

}


/**
 * @brief Recursively render a ValueNode and its children into a textual tree.
 *
 * Honors @a maxDepth so very deep object graphs do not flood the output.
 * For pointer-to-compound nodes, the intermediate compound layer is
 * collapsed and the children are printed directly. Unresolved nodes are
 * marked as such.
 *
 * @param _output      BString that the rendered tree is appended to.
 * @param child        Child node to render.
 * @param indentLevel  Current indent depth in tabs.
 * @param maxDepth     Maximum recursion depth; 0 stops traversal.
 */
/*static*/ void
UiUtils::PrintValueNodeGraph(BString& _output, ValueNodeChild* child,
	int32 indentLevel, int32 maxDepth)
{
	_output.Append('\t', indentLevel);
	_output << child->Name();

	ValueNode* node = child->Node();
	if (node == NULL) {
		_output << ": Unavailable\n";
		return;
	}

	if (node->GetType()->Kind() != TYPE_COMPOUND) {
		_output << ": ";
		status_t resolutionState = node->LocationAndValueResolutionState();
		if (resolutionState == VALUE_NODE_UNRESOLVED)
			_output << "Unresolved";
		else if (resolutionState == B_OK) {
			Value* value = node->GetValue();
			if (value != NULL) {
				BString valueData;
				value->ToString(valueData);
				_output << valueData;
			} else
				_output << "Unavailable";
		} else
			_output << strerror(resolutionState);
	}

	if (maxDepth == 0 || node->CountChildren() == 0) {
		_output << "\n";
		return;
	}

	if (node->CountChildren() == 1
		&& node->GetType()->ResolveRawType(false)->Kind() == TYPE_ADDRESS
		&& node->ChildAt(0)->GetType()->ResolveRawType(false)->Kind()
			== TYPE_COMPOUND) {
		// for the case of a pointer to a compound type,
		// we want to hide the intervening compound node and print
		// the children directly.
		node = node->ChildAt(0)->Node();
	}

	if (node != NULL) {
		_output << " {\n";

		for (int32 i = 0; i < node->CountChildren(); i++) {
			// don't dump compound nodes if our depth limit won't allow
			// us to traverse into their children anyways, and the top
			// level node contains no data of intereest.
			if (node->ChildAt(i)->GetType()->Kind() != TYPE_COMPOUND
				|| maxDepth > 1) {
				PrintValueNodeGraph(_output, node->ChildAt(i),
					indentLevel + 1, maxDepth - 1);
			}
		}
		_output.Append('\t', indentLevel);
		_output << "}\n";
	} else
		_output << "\n";

	return;
}


/**
 * @brief Render a hexdump-style memory listing into @a _output.
 *
 * Each row begins with the address and the printable-ASCII rendition of
 * the row, then the hex bytes/words. Rows are wrapped at @a displayWidth
 * items.
 *
 * @param _output       BString that the dump is appended to.
 * @param indentLevel   Indent depth in tabs at the start of each line.
 * @param block         Memory block being dumped.
 * @param address       Starting address.
 * @param itemSize      Size of each item in bytes (1, 2, 4, or 8).
 * @param displayWidth  Number of items per row.
 * @param count         Number of items to dump.
 */
/*static*/ void
UiUtils::DumpMemory(BString& _output, int32 indentLevel,
	TeamMemoryBlock* block, target_addr_t address, int32 itemSize,
	int32 displayWidth, int32 count)
{
	BString data;

	int32 j;
	_output.Append('\t', indentLevel);
	for (int32 i = 0; i < count; i++) {
		if (!block->Contains(address + i * itemSize))
			break;

		uint8* value;

		if ((i % displayWidth) == 0) {
			int32 displayed = min_c(displayWidth, (count-i)) * itemSize;
			if (i != 0) {
				_output.Append("\n");
				_output.Append('\t', indentLevel);
			}

			data.SetToFormat("[%#" B_PRIx64 "]  ", address + i * itemSize);
			_output += data;
			char c;
			for (j = 0; j < displayed; j++) {
				c = *(block->Data() + address - block->BaseAddress()
					+ (i * itemSize) + j);
				if (!isprint(c))
					c = '.';

				_output += c;
			}
			if (count > displayWidth) {
				// make sure the spacing in the last line is correct
				for (j = displayed; j < displayWidth * itemSize; j++)
					_output += ' ';
			}
			_output.Append("  ");
		}

		value = block->Data() + address - block->BaseAddress()
			+ i * itemSize;

		switch (itemSize) {
			case 1:
				data.SetToFormat(" %02" B_PRIx8, *(uint8*)value);
				break;
			case 2:
				data.SetToFormat(" %04" B_PRIx16, *(uint16*)value);
				break;
			case 4:
				data.SetToFormat(" %08" B_PRIx32, *(uint32*)value);
				break;
			case 8:
				data.SetToFormat(" %016" B_PRIx64, *(uint64*)value);
				break;
		}

		_output += data;
	}

	_output.Append("\n");
}


/**
 * @brief Parse a single "low" or "low-high" token into two integers.
 *
 * @param rangeString  Token to parse. Trimmed of whitespace by the caller.
 * @param lowerBound   Output lower bound.
 * @param upperBound   Output upper bound (equals @a lowerBound for "low").
 * @retval B_OK         Parsed successfully.
 * @retval B_BAD_VALUE  @a lowerBound exceeds @a upperBound.
 */
static status_t ParseRangeString(BString& rangeString, int32& lowerBound,
	int32& upperBound)
{
	lowerBound = atoi(rangeString.String());
	int32 index = rangeString.FindFirst('-');
	if (index >= 0) {
		rangeString.Remove(0, index + 1);
		upperBound = atoi(rangeString.String());
	} else
		upperBound = lowerBound;

	if (lowerBound > upperBound)
		return B_BAD_VALUE;

	return B_OK;
}


/**
 * @brief Parse a comma-separated list of ranges into a RangeList.
 *
 * Tokens are of the form "N" or "N-M". When @a fixedRange is true any value
 * outside [lowerBound, upperBound] makes the whole expression fail.
 *
 * @param rangeExpression  Input expression (e.g. "1,3-5,7").
 * @param lowerBound       Inclusive lower bound for validation.
 * @param upperBound       Inclusive upper bound for validation.
 * @param fixedRange       Whether to enforce the validation bounds.
 * @param _output          RangeList that receives the parsed ranges.
 * @retval B_OK         Parsed successfully.
 * @retval B_BAD_DATA   Input was empty.
 * @retval B_BAD_VALUE  Token was malformed or out of bounds.
 * @return Other status codes propagated from RangeList::AddRange().
 */
/*static*/ status_t
UiUtils::ParseRangeExpression(const BString& rangeExpression, int32 lowerBound,
	int32 upperBound, bool fixedRange, RangeList& _output)
{
	if (rangeExpression.IsEmpty())
		return B_BAD_DATA;

	BString dataString = rangeExpression;
	dataString.RemoveAll(" ");

	// first, tokenize the range list to its constituent child ranges.
	int32 index;
	int32 lowValue;
	int32 highValue;
	BString tempRange;
	while (!dataString.IsEmpty()) {
		index = dataString.FindFirst(',');
		if (index == 0)
			return B_BAD_VALUE;
		else if (index > 0) {
			dataString.MoveInto(tempRange, 0, index);
			dataString.Remove(0, 1);
		} else {
			tempRange = dataString;
			dataString.Truncate(0);
		}

		status_t result = ParseRangeString(tempRange, lowValue, highValue);
		if (result != B_OK)
			return result;


		if (fixedRange && (lowValue < lowerBound || highValue > upperBound))
			return B_BAD_VALUE;

		result = _output.AddRange(lowValue, highValue);
		if (result != B_OK)
			return result;

		tempRange.Truncate(0);
	}

	return B_OK;
}


/**
 * @brief Map a BVariant type_code into its short C type-name string.
 *
 * @param type  Type code (e.g. B_INT32_TYPE).
 * @return Pointer to a static string ("int32", "double", ...) or
 *         "unknown" for unrecognized values.
 */
/*static*/ const char*
UiUtils::TypeCodeToString(type_code type)
{
	switch (type) {
		case B_INT8_TYPE:
			return "int8";
		case B_UINT8_TYPE:
			return "uint8";
		case B_INT16_TYPE:
			return "int16";
		case B_UINT16_TYPE:
			return "uint16";
		case B_INT32_TYPE:
			return "int32";
		case B_UINT32_TYPE:
			return "uint32";
		case B_INT64_TYPE:
			return "int64";
		case B_UINT64_TYPE:
			return "uint64";
		case B_FLOAT_TYPE:
			return "float";
		case B_DOUBLE_TYPE:
			return "double";
		case B_STRING_TYPE:
			return "string";
		default:
			return "unknown";
	}
}


/**
 * @brief Read the @a index-th element of @a data interpreted as type @a T.
 *
 * @tparam T     Lane type.
 * @param data   Raw SIMD register buffer.
 * @param index  Lane index.
 * @return The lane value.
 */
template<typename T>
T GetSIMDValueAtOffset(char* data, int32 index)
{
	return ((T*)data)[index];
}


/**
 * @brief Return the byte size corresponding to a SIMD render format.
 *
 * @param format  One of SIMD_RENDER_FORMAT_*.
 * @return Element size in bytes, or 0 for unknown formats.
 */
static int32 GetSIMDFormatByteSize(uint32 format)
{
	switch (format) {
		case SIMD_RENDER_FORMAT_INT8:
			return sizeof(char);
		case SIMD_RENDER_FORMAT_INT16:
			return sizeof(int16);
		case SIMD_RENDER_FORMAT_INT32:
			return sizeof(int32);
		case SIMD_RENDER_FORMAT_INT64:
			return sizeof(int64);
		case SIMD_RENDER_FORMAT_FLOAT:
			return sizeof(float);
		case SIMD_RENDER_FORMAT_DOUBLE:
			return sizeof(double);
	}

	return 0;
}


/**
 * @brief Render a SIMD register value as a comma-separated brace-enclosed list.
 *
 * The element format (int8/int16/.../float/double) controls both the lane
 * width and the per-lane formatting.
 *
 * @param value    Raw SIMD register value (BVariant of pointer type).
 * @param bitSize  Total width of the register in bits.
 * @param format   Lane interpretation (one of SIMD_RENDER_FORMAT_*).
 * @param _output  BString that receives the formatted string.
 * @return Reference to @a _output.
 */
/*static*/
const BString&
UiUtils::FormatSIMDValue(const BVariant& value, uint32 bitSize,
	uint32 format, BString& _output)
{
	_output.SetTo("{");
	char* data = (char*)value.ToPointer();
	uint32 count = bitSize / (GetSIMDFormatByteSize(format) * 8);
	for (uint32 i = 0; i < count; i ++) {
		BString temp;
		switch (format) {
			case SIMD_RENDER_FORMAT_INT8:
				temp.SetToFormat("%#" B_PRIx8,
					GetSIMDValueAtOffset<uint8>(data, i));
				break;
			case SIMD_RENDER_FORMAT_INT16:
				temp.SetToFormat("%#" B_PRIx16,
					GetSIMDValueAtOffset<uint16>(data, i));
				break;
			case SIMD_RENDER_FORMAT_INT32:
				temp.SetToFormat("%#" B_PRIx32,
					GetSIMDValueAtOffset<uint32>(data, i));
				break;
			case SIMD_RENDER_FORMAT_INT64:
				temp.SetToFormat("%#" B_PRIx64,
					GetSIMDValueAtOffset<uint64>(data, i));
				break;
			case SIMD_RENDER_FORMAT_FLOAT:
				temp.SetToFormat("%.3g",
					(double)GetSIMDValueAtOffset<float>(data, i));
				break;
			case SIMD_RENDER_FORMAT_DOUBLE:
				temp.SetToFormat("%.3g",
					GetSIMDValueAtOffset<double>(data, i));
				break;
		}
		_output += temp;
		if (i < count - 1)
			_output += ", ";
	}
	_output += "}";

	return _output;
}


/**
 * @brief Convert a signal number into its conventional symbolic name.
 *
 * Recognizes the standard POSIX signals plus Haiku-specific extensions
 * (SIGKILLTHR, SIGRTMIN+N).
 *
 * @param signal   Signal number.
 * @param _output  BString that receives the name.
 * @return Pointer to @c _output.String() for convenience.
 */
const char*
UiUtils::SignalNameToString(int32 signal, BString& _output)
{
	#undef DEFINE_SIGNAL_STRING
	#define DEFINE_SIGNAL_STRING(x)										\
		case x:															\
			_output = #x;												\
			return _output.String();

	switch (signal) {
		DEFINE_SIGNAL_STRING(SIGHUP)
		DEFINE_SIGNAL_STRING(SIGINT)
		DEFINE_SIGNAL_STRING(SIGQUIT)
		DEFINE_SIGNAL_STRING(SIGILL)
		DEFINE_SIGNAL_STRING(SIGCHLD)
		DEFINE_SIGNAL_STRING(SIGABRT)
		DEFINE_SIGNAL_STRING(SIGPIPE)
		DEFINE_SIGNAL_STRING(SIGFPE)
		DEFINE_SIGNAL_STRING(SIGKILL)
		DEFINE_SIGNAL_STRING(SIGSTOP)
		DEFINE_SIGNAL_STRING(SIGSEGV)
		DEFINE_SIGNAL_STRING(SIGCONT)
		DEFINE_SIGNAL_STRING(SIGTSTP)
		DEFINE_SIGNAL_STRING(SIGALRM)
		DEFINE_SIGNAL_STRING(SIGTERM)
		DEFINE_SIGNAL_STRING(SIGTTIN)
		DEFINE_SIGNAL_STRING(SIGTTOU)
		DEFINE_SIGNAL_STRING(SIGUSR1)
		DEFINE_SIGNAL_STRING(SIGUSR2)
		DEFINE_SIGNAL_STRING(SIGWINCH)
		DEFINE_SIGNAL_STRING(SIGKILLTHR)
		DEFINE_SIGNAL_STRING(SIGTRAP)
		DEFINE_SIGNAL_STRING(SIGPOLL)
		DEFINE_SIGNAL_STRING(SIGPROF)
		DEFINE_SIGNAL_STRING(SIGSYS)
		DEFINE_SIGNAL_STRING(SIGURG)
		DEFINE_SIGNAL_STRING(SIGVTALRM)
		DEFINE_SIGNAL_STRING(SIGXCPU)
		DEFINE_SIGNAL_STRING(SIGXFSZ)
		DEFINE_SIGNAL_STRING(SIGBUS)
		default:
			break;
	}

	if (signal == SIGRTMIN)
		_output = "SIGRTMIN";
	else if (signal == SIGRTMAX)
		_output = "SIGRTMAX";
	else
		_output.SetToFormat("SIGRTMIN+%" B_PRId32, signal - SIGRTMIN);

	return _output.String();
}


/**
 * @brief Convert a signal disposition value into a short label.
 *
 * @param disposition  One of SIGNAL_DISPOSITION_*.
 * @return Pointer to a static string describing the disposition.
 */
const char*
UiUtils::SignalDispositionToString(int disposition)
{
	switch (disposition) {
		case SIGNAL_DISPOSITION_IGNORE:
			return "Ignore";
		case SIGNAL_DISPOSITION_STOP_AT_RECEIPT:
			return "Stop at receipt";
		case SIGNAL_DISPOSITION_STOP_AT_SIGNAL_HANDLER:
			return "Stop at signal handler";
		default:
			break;
	}

	return "Unknown";
}
