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
 *   Open Tracker License
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the Be Sample Code License.
 */


/**
 * @file ViewState.cpp
 * @brief BColumn and BViewState serialisation and deserialisation.
 *
 * BColumn describes a single attribute column in list view (title, width,
 * alignment, attribute key).  BViewState captures the complete display
 * configuration of a Tracker window (view mode, sort order, scroll origin,
 * icon size).  Both classes support streaming to/from BMallocIO and BMessage
 * with optional big-endian byte swapping for cross-platform portability.
 *
 * @see BPoseView, PoseView
 */


#include <Debug.h>
#include <AppDefs.h>
#include <InterfaceDefs.h>

#include "Attributes.h"
#include "Commands.h"
#include "PoseView.h"
#include "Utilities.h"
#include "ViewState.h"

#include <new>
#include <string.h>
#include <stdlib.h>


const char* kColumnVersionName = "BColumn:version";
const char* kColumnTitleName = "BColumn:fTitle";
const char* kColumnOffsetName = "BColumn:fOffset";
const char* kColumnWidthName = "BColumn:fWidth";
const char* kColumnAlignmentName = "BColumn:fAlignment";
const char* kColumnAttrName = "BColumn:fAttrName";
const char* kColumnAttrHashName = "BColumn:fAttrHash";
const char* kColumnAttrTypeName = "BColumn:fAttrType";
const char* kColumnDisplayAsName = "BColumn:fDisplayAs";
const char* kColumnStatFieldName = "BColumn:fStatField";
const char* kColumnEditableName = "BColumn:fEditable";

const char* kViewStateVersionName = "ViewState:version";
const char* kViewStateViewModeName = "ViewState:fViewMode";
const char* kViewStateLastIconModeName = "ViewState:fLastIconMode";
const char* kViewStateListOriginName = "ViewState:fListOrigin";
const char* kViewStateIconOriginName = "ViewState:fIconOrigin";
const char* kViewStatePrimarySortAttrName = "ViewState:fPrimarySortAttr";
const char* kViewStatePrimarySortTypeName = "ViewState:fPrimarySortType";
const char* kViewStateSecondarySortAttrName = "ViewState:fSecondarySortAttr";
const char* kViewStateSecondarySortTypeName = "ViewState:fSecondarySortType";
const char* kViewStateReverseSortName = "ViewState:fReverseSort";
const char* kViewStateIconSizeName = "ViewState:fIconSize";
const char* kViewStateLastIconSizeName = "ViewState:fLastIconSize";


static const int32 kColumnStateMinArchiveVersion = 21;
	// bump version when layout changes


//	#pragma mark - BColumn


/**
 * @brief Construct a BColumn with all fields including a display-as override.
 *
 * @param title          Column header text.
 * @param width          Initial column width in pixels (scaled to font size).
 * @param align          Text alignment within the column.
 * @param attributeName  Attribute key (e.g. "BEOS:name").
 * @param attrType       BeOS attribute type constant.
 * @param displayAs      Display-as format string, or NULL for the default.
 * @param statField      true if this is a stat (kernel) attribute.
 * @param editable       true if the user can edit this attribute inline.
 */
BColumn::BColumn(const char* title, float width,
	alignment align, const char* attributeName, uint32 attrType,
	const char* displayAs, bool statField, bool editable)
{
	_Init(title, width, align, attributeName, attrType, displayAs,
		statField, editable);
}


/**
 * @brief Construct a BColumn without a display-as override.
 *
 * @param title          Column header text.
 * @param width          Initial column width in pixels (scaled to font size).
 * @param align          Text alignment within the column.
 * @param attributeName  Attribute key.
 * @param attrType       BeOS attribute type constant.
 * @param statField      true if this is a stat (kernel) attribute.
 * @param editable       true if the user can edit this attribute inline.
 */
BColumn::BColumn(const char* title, float width,
	alignment align, const char* attributeName, uint32 attrType,
	bool statField, bool editable)
{
	_Init(title, width, align, attributeName, attrType, NULL,
		statField, editable);
}


/**
 * @brief Destructor.
 */
BColumn::~BColumn()
{
}


/**
 * @brief Deserialise a BColumn from a raw byte stream.
 *
 * @param stream      The stream positioned immediately after the header.
 * @param version     Archive version read from the stream header.
 * @param endianSwap  If true, byte-swap all multi-byte fields.
 */
BColumn::BColumn(BMallocIO* stream, int32 version, bool endianSwap)
{
	StringFromStream(&fTitle, stream, endianSwap);
	stream->Read(&fOffset, sizeof(float));
	stream->Read(&fWidth, sizeof(float));
	stream->Read(&fAlignment, sizeof(alignment));
	StringFromStream(&fAttrName, stream, endianSwap);
	stream->Read(&fAttrHash, sizeof(uint32));
	stream->Read(&fAttrType, sizeof(uint32));
	stream->Read(&fStatField, sizeof(bool));
	stream->Read(&fEditable, sizeof(bool));
	if (version == kColumnStateArchiveVersion)
		StringFromStream(&fDisplayAs, stream, endianSwap);

	if (endianSwap) {
		PRINT(("endian swapping column\n"));
		fOffset = B_SWAP_FLOAT(fOffset);
		fWidth = B_SWAP_FLOAT(fWidth);
		STATIC_ASSERT(sizeof(alignment) == sizeof(int32));
		fAlignment = (alignment)B_SWAP_INT32(fAlignment);
		fAttrHash = B_SWAP_INT32(fAttrHash);
		fAttrType = B_SWAP_INT32(fAttrType);
	}

	fOffset = ceilf(fOffset * _Scale());
	fWidth = ceilf(fWidth * _Scale());
}


/**
 * @brief Deserialise a BColumn from a BMessage at the given index.
 *
 * @param message  The message containing column fields at \a index.
 * @param index    The array index to read within each message field.
 */
BColumn::BColumn(const BMessage &message, int32 index)
{
	if (message.FindString(kColumnTitleName, index, &fTitle) != B_OK)
		fTitle.SetTo(B_EMPTY_STRING);

	if (message.FindFloat(kColumnOffsetName, index, &fOffset) != B_OK)
		fOffset = -1.0f;
	else
		fOffset = ceilf(fOffset * _Scale());

	if (message.FindFloat(kColumnWidthName, index, &fWidth) != B_OK)
		fWidth = -1.0f;
	else
		fWidth = ceilf(fWidth * _Scale());

	if (message.FindInt32(kColumnAlignmentName, index, (int32*)&fAlignment)
			!= B_OK) {
		fAlignment = B_ALIGN_LEFT;
	}

	if (message.FindString(kColumnAttrName, index, &fAttrName) != B_OK)
		fAttrName = BString(B_EMPTY_STRING);

	if (message.FindInt32(kColumnAttrHashName, index, (int32*)&fAttrHash)
			!= B_OK) {
		fAttrHash = 0;
	}

	if (message.FindInt32(kColumnAttrTypeName, index, (int32*)&fAttrType)
			!= B_OK) {
		fAttrType = 0;
	}

	if (message.FindString(kColumnDisplayAsName, index, &fDisplayAs) != B_OK)
		fDisplayAs.SetTo(B_EMPTY_STRING);

	if (message.FindBool(kColumnStatFieldName, index, &fStatField) != B_OK)
		fStatField = false;

	if (message.FindBool(kColumnEditableName, index, &fEditable) != B_OK)
		fEditable = false;
}


/**
 * @brief Common initialisation helper called by all constructors.
 *
 * @param title          Column header text.
 * @param width          Width in pixels (unscaled; will be multiplied by _Scale()).
 * @param align          Text alignment.
 * @param attributeName  Attribute key string.
 * @param attrType       BeOS attribute type.
 * @param displayAs      Optional display-as override.
 * @param statField      true for stat (kernel) attributes.
 * @param editable       true if the attribute can be edited inline.
 */
void
BColumn::_Init(const char* title, float width,
	alignment align, const char* attributeName, uint32 attrType,
	const char* displayAs, bool statField, bool editable)
{
	fTitle = title;
	fAttrName = attributeName;
	fDisplayAs = displayAs;
	fOffset = -1.0f;
	fWidth = width * _Scale();
	fAlignment = align;
	fAttrHash = AttrHashString(attributeName, attrType);
	fAttrType = attrType;
	fStatField = statField;
	fEditable = editable;
}


/**
 * @brief Return the DPI-based scale factor for column widths.
 *
 * Computes the ratio of the current plain-font size to 12 points so that
 * column widths scale proportionally with the system font.
 *
 * @return Scale factor (1.0 at 12-point plain font).
 */
/* static */ float
BColumn::_Scale()
{
	return (be_plain_font->Size() / 12.0f);
}


/**
 * @brief Deserialise a BColumn from a raw byte stream, validating the header.
 *
 * @param stream      Stream positioned at the column header.
 * @param endianSwap  If true, byte-swap all multi-byte fields.
 * @return A new BColumn on success, or NULL if validation fails.
 */
BColumn*
BColumn::InstantiateFromStream(BMallocIO* stream, bool endianSwap)
{
	// compare stream header in canonical form

	// we can't use ValidateStream(), as we preserve backwards compatibility
	int32 version;
	uint32 key;
	if (stream->Read(&key, sizeof(uint32)) <= 0
		|| stream->Read(&version, sizeof(int32)) <=0)
		return 0;

	if (endianSwap) {
		key = SwapUInt32(key);
		version = SwapInt32(version);
	}

	if (key != AttrHashString("BColumn", B_OBJECT_TYPE)
		|| version < kColumnStateMinArchiveVersion)
		return 0;

//	PRINT(("instantiating column, %s\n", endianSwap ? "endian swapping," : ""));
	return _Sanitize(new (std::nothrow) BColumn(stream, version, endianSwap));
}


/**
 * @brief Deserialise a BColumn from a BMessage at the given index.
 *
 * @param message  Message containing serialised column data.
 * @param index    Array index within the message fields.
 * @return A new BColumn on success, or NULL on version mismatch.
 */
BColumn*
BColumn::InstantiateFromMessage(const BMessage &message, int32 index)
{
	int32 version = kColumnStateArchiveVersion;
	int32 messageVersion;

	if (message.FindInt32(kColumnVersionName, index, &messageVersion) != B_OK)
		return NULL;

	if (version != messageVersion)
		return NULL;

	return _Sanitize(new (std::nothrow) BColumn(message, index));
}


/**
 * @brief Serialise this column to a raw byte stream.
 *
 * Writes the class identifier, version number, and all field values.
 * Floating-point widths are unscaled before writing.
 *
 * @param stream  Output stream to write the serialised column data.
 */
void
BColumn::ArchiveToStream(BMallocIO* stream) const
{
	// write class identifier and version info
	uint32 key = AttrHashString("BColumn", B_OBJECT_TYPE);
	stream->Write(&key, sizeof(uint32));
	int32 version = kColumnStateArchiveVersion;
	stream->Write(&version, sizeof(int32));

//	PRINT(("ArchiveToStream column, key %x, version %d\n", key, version));

	const float offset = floorf(fOffset / _Scale()),
		width = floorf(fWidth / _Scale());

	StringToStream(&fTitle, stream);
	stream->Write(&offset, sizeof(float));
	stream->Write(&width, sizeof(float));
	stream->Write(&fAlignment, sizeof(alignment));
	StringToStream(&fAttrName, stream);
	stream->Write(&fAttrHash, sizeof(uint32));
	stream->Write(&fAttrType, sizeof(uint32));
	stream->Write(&fStatField, sizeof(bool));
	stream->Write(&fEditable, sizeof(bool));
	StringToStream(&fDisplayAs, stream);
}


/**
 * @brief Serialise this column into a BMessage by appending field values.
 *
 * @param message  Output message to which column fields are appended.
 */
void
BColumn::ArchiveToMessage(BMessage &message) const
{
	const float offset = floorf(fOffset / _Scale()),
		width = floorf(fWidth / _Scale());

	message.AddInt32(kColumnVersionName, kColumnStateArchiveVersion);

	message.AddString(kColumnTitleName, fTitle);
	message.AddFloat(kColumnOffsetName, offset);
	message.AddFloat(kColumnWidthName, width);
	message.AddInt32(kColumnAlignmentName, fAlignment);
	message.AddString(kColumnAttrName, fAttrName);
	message.AddInt32(kColumnAttrHashName, static_cast<int32>(fAttrHash));
	message.AddInt32(kColumnAttrTypeName, static_cast<int32>(fAttrType));
	message.AddString(kColumnDisplayAsName, fDisplayAs.String());
	message.AddBool(kColumnStatFieldName, fStatField);
	message.AddBool(kColumnEditableName, fEditable);
}


/**
 * @brief Validate a newly deserialised BColumn and delete it if corrupt.
 *
 * @param column  Column to validate; may be NULL.
 * @return \a column if valid, or NULL if sanity checks fail (object is deleted).
 */
BColumn*
BColumn::_Sanitize(BColumn* column)
{
	if (column == NULL)
		return NULL;

	// sanity-check the resulting column
	if (column->fTitle.Length() > 500
		|| column->fOffset < 0
		|| column->fOffset > 10000
		|| column->fWidth < 0
		|| column->fWidth > 10000
		|| (int32)column->fAlignment < B_ALIGN_LEFT
		|| (int32)column->fAlignment > B_ALIGN_CENTER
		|| column->fAttrName.Length() > 500) {
		PRINT(("column data not valid\n"));
		delete column;
		return NULL;
	}
#if DEBUG
// TODO: Whatever this is supposed to mean, fix it.
//	else if (endianSwap)
//		PRINT(("Instantiated foreign column ok\n"));
#endif

	return column;
}


//	#pragma mark - BViewState


/**
 * @brief Construct a default BViewState (list mode, name-sorted, standard icon size).
 */
BViewState::BViewState()
{
	_Init();
	_StorePreviousState();
}


/**
 * @brief Deserialise a BViewState from a raw byte stream.
 *
 * @param stream      Stream positioned after the validated header.
 * @param endianSwap  If true, byte-swap all multi-byte fields.
 */
BViewState::BViewState(BMallocIO* stream, bool endianSwap)
{
	_Init();
	stream->Read(&fViewMode, sizeof(uint32));
	stream->Read(&fLastIconMode, sizeof(uint32));
	stream->Read(&fListOrigin, sizeof(BPoint));
	stream->Read(&fIconOrigin, sizeof(BPoint));
	stream->Read(&fPrimarySortAttr, sizeof(uint32));
	stream->Read(&fPrimarySortType, sizeof(uint32));
	stream->Read(&fSecondarySortAttr, sizeof(uint32));
	stream->Read(&fSecondarySortType, sizeof(uint32));
	stream->Read(&fReverseSort, sizeof(bool));
	stream->Read(&fIconSize, sizeof(uint32));
	stream->Read(&fLastIconSize, sizeof(uint32));

	if (endianSwap) {
		PRINT(("endian swapping view state\n"));
		fViewMode = B_SWAP_INT32(fViewMode);
		fLastIconMode = B_SWAP_INT32(fLastIconMode);
		fIconSize = B_SWAP_INT32(fIconSize);
		fLastIconSize = B_SWAP_INT32(fLastIconSize);
		swap_data(B_POINT_TYPE, &fListOrigin,
			sizeof(fListOrigin), B_SWAP_ALWAYS);
		swap_data(B_POINT_TYPE, &fIconOrigin,
			sizeof(fIconOrigin), B_SWAP_ALWAYS);
		fPrimarySortAttr = B_SWAP_INT32(fPrimarySortAttr);
		fSecondarySortAttr = B_SWAP_INT32(fSecondarySortAttr);
		fPrimarySortType = B_SWAP_INT32(fPrimarySortType);
		fSecondarySortType = B_SWAP_INT32(fSecondarySortType);
	}

	_StorePreviousState();
	_Sanitize(this, true);
}


/**
 * @brief Deserialise a BViewState from a BMessage.
 *
 * @param message  Message containing the serialised view-state fields.
 */
BViewState::BViewState(const BMessage &message)
{
	_Init();
	message.FindInt32(kViewStateViewModeName, (int32*)&fViewMode);
	message.FindInt32(kViewStateLastIconModeName, (int32*)&fLastIconMode);
	message.FindInt32(kViewStateLastIconSizeName,(int32*)&fLastIconSize);
	message.FindInt32(kViewStateIconSizeName, (int32*)&fIconSize);
	message.FindPoint(kViewStateListOriginName, &fListOrigin);
	message.FindPoint(kViewStateIconOriginName, &fIconOrigin);
	message.FindInt32(kViewStatePrimarySortAttrName,
		(int32*)&fPrimarySortAttr);
	message.FindInt32(kViewStatePrimarySortTypeName,
		(int32*)&fPrimarySortType);
	message.FindInt32(kViewStateSecondarySortAttrName,
		(int32*)&fSecondarySortAttr);
	message.FindInt32(kViewStateSecondarySortTypeName,
		(int32*)&fSecondarySortType);
	message.FindBool(kViewStateReverseSortName, &fReverseSort);

	_StorePreviousState();
	_Sanitize(this, true);
}


/**
 * @brief Serialise this view state to a raw byte stream and snapshot the current state.
 *
 * @param stream  Output stream to write the serialised data.
 */
void
BViewState::ArchiveToStream(BMallocIO* stream)
{
	_ArchiveToStream(stream);
	_StorePreviousState();
}


/**
 * @brief Serialise this view state into a BMessage and snapshot the current state.
 *
 * @param message  Output message to append serialised fields to.
 */
void
BViewState::ArchiveToMessage(BMessage &message)
{
	_ArchiveToMessage(message);
	_StorePreviousState();
}


/**
 * @brief Deserialise a BViewState from a raw byte stream, validating the header.
 *
 * @param stream      Stream positioned at the view-state header.
 * @param endianSwap  If true, byte-swap all multi-byte fields.
 * @return A new BViewState on success, or NULL if validation fails.
 */
BViewState*
BViewState::InstantiateFromStream(BMallocIO* stream, bool endianSwap)
{
	// compare stream header in canonical form
	uint32 key = AttrHashString("BViewState", B_OBJECT_TYPE);
	int32 version = kViewStateArchiveVersion;

	if (endianSwap) {
		key = SwapUInt32(key);
		version = SwapInt32(version);
	}

	if (!ValidateStream(stream, key, version))
		return NULL;

	return _Sanitize(new (std::nothrow) BViewState(stream, endianSwap));
}


/**
 * @brief Deserialise a BViewState from a BMessage, checking the version field.
 *
 * @param message  Message containing the serialised view-state fields.
 * @return A new BViewState on success, or NULL on version mismatch.
 */
BViewState*
BViewState::InstantiateFromMessage(const BMessage &message)
{
	int32 version = kViewStateArchiveVersion;

	int32 messageVersion;
	if (message.FindInt32(kViewStateVersionName, &messageVersion) != B_OK)
		return NULL;

	if (version != messageVersion)
		return NULL;

	return _Sanitize(new (std::nothrow) BViewState(message));
}


/**
 * @brief Set all fields to their default values.
 */
void
BViewState::_Init()
{
	fViewMode = kListMode;
	fLastIconMode = 0;
	fIconSize = B_LARGE_ICON;
	fLastIconSize = B_LARGE_ICON;
	fListOrigin.Set(0, 0);
	fIconOrigin.Set(0, 0);
	fPrimarySortAttr = AttrHashString(kAttrStatName, B_STRING_TYPE);
	fPrimarySortType = B_STRING_TYPE;
	fSecondarySortAttr = 0;
	fSecondarySortType = 0;
	fReverseSort = false;
}


/**
 * @brief Snapshot the current field values into the "previous" copies.
 *
 * Used by ArchiveToStream/ArchiveToMessage to detect changes since the last save.
 */
void
BViewState::_StorePreviousState()
{
	fPreviousViewMode = fViewMode;
	fPreviousLastIconMode = fLastIconMode;
	fPreviousIconSize = fIconSize;
	fPreviousLastIconSize = fLastIconSize;
	fPreviousListOrigin = fListOrigin;
	fPreviousIconOrigin = fIconOrigin;
	fPreviousPrimarySortAttr = fPrimarySortAttr;
	fPreviousSecondarySortAttr = fSecondarySortAttr;
	fPreviousPrimarySortType = fPrimarySortType;
	fPreviousSecondarySortType = fSecondarySortType;
	fPreviousReverseSort = fReverseSort;
}


/**
 * @brief Validate and optionally correct a deserialised BViewState.
 *
 * When \a fixOnly is true only out-of-range values are clamped; when false
 * an unrecognised view mode causes the object to be deleted and NULL returned.
 *
 * @param state    The state to validate; may be NULL.
 * @param fixOnly  If true, only clamp values; if false, also check enum validity.
 * @return \a state after corrections, or NULL if sanity checks fail.
 */
BViewState*
BViewState::_Sanitize(BViewState* state, bool fixOnly)
{
	if (state == NULL)
		return NULL;

	if (state->fViewMode == kListMode) {
		if (state->fListOrigin.x < 0)
			state->fListOrigin.x = 0;

		if (state->fListOrigin.y < 0)
			state->fListOrigin.y = 0;
	}
	if (state->fIconSize < 16)
		state->fIconSize = 16;

	if (state->fIconSize > 128)
		state->fIconSize = 128;

	if (state->fLastIconSize < 16)
		state->fLastIconSize = 16;

	if (state->fLastIconSize > 128)
		state->fLastIconSize = 128;

	if (fixOnly)
		return state;

	// do a sanity check here
	if ((state->fViewMode != kListMode
			&& state->fViewMode != kIconMode
			&& state->fViewMode != kMiniIconMode
			&& state->fViewMode != 0)
		|| (state->fLastIconMode != kListMode
			&& state->fLastIconMode != kIconMode
			&& state->fLastIconMode != kMiniIconMode
			&& state->fLastIconMode != 0)) {
		PRINT(("Bad data instantiating ViewState, view mode %" B_PRIx32
			", lastIconMode %" B_PRIx32 "\n", state->fViewMode,
			state->fLastIconMode));

		delete state;
		return NULL;
	}
#if DEBUG
// TODO: Whatever this is supposed to mean, fix it.
//	else if (endianSwap)
//		PRINT(("Instantiated foreign view state ok\n"));
#endif

	return state;
}


/**
 * @brief Internal helper to write all view-state fields to a byte stream.
 *
 * @param stream  Output stream to write to.
 */
void
BViewState::_ArchiveToStream(BMallocIO* stream) const
{
	// write class identifier and verison info
	uint32 key = AttrHashString("BViewState", B_OBJECT_TYPE);
	stream->Write(&key, sizeof(key));
	int32 version = kViewStateArchiveVersion;
	stream->Write(&version, sizeof(version));

	stream->Write(&fViewMode, sizeof(uint32));
	stream->Write(&fLastIconMode, sizeof(uint32));
	stream->Write(&fListOrigin, sizeof(BPoint));
	stream->Write(&fIconOrigin, sizeof(BPoint));
	stream->Write(&fPrimarySortAttr, sizeof(uint32));
	stream->Write(&fPrimarySortType, sizeof(uint32));
	stream->Write(&fSecondarySortAttr, sizeof(uint32));
	stream->Write(&fSecondarySortType, sizeof(uint32));
	stream->Write(&fReverseSort, sizeof(bool));
	stream->Write(&fIconSize, sizeof(uint32));
	stream->Write(&fLastIconSize, sizeof(uint32));
}


/**
 * @brief Internal helper to append all view-state fields to a BMessage.
 *
 * @param message  Output message to append fields to.
 */
void
BViewState::_ArchiveToMessage(BMessage &message) const
{
	message.AddInt32(kViewStateVersionName, kViewStateArchiveVersion);

	message.AddInt32(kViewStateViewModeName, static_cast<int32>(fViewMode));
	message.AddInt32(kViewStateLastIconModeName,
		static_cast<int32>(fLastIconMode));
	message.AddPoint(kViewStateListOriginName, fListOrigin);
	message.AddPoint(kViewStateIconOriginName, fIconOrigin);
	message.AddInt32(kViewStatePrimarySortAttrName,
		static_cast<int32>(fPrimarySortAttr));
	message.AddInt32(kViewStatePrimarySortTypeName,
		static_cast<int32>(fPrimarySortType));
	message.AddInt32(kViewStateSecondarySortAttrName,
		static_cast<int32>(fSecondarySortAttr));
	message.AddInt32(kViewStateSecondarySortTypeName,
		static_cast<int32>(fSecondarySortType));
	message.AddBool(kViewStateReverseSortName, fReverseSort);
	message.AddInt32(kViewStateIconSizeName, static_cast<int32>(fIconSize));
	message.AddInt32(kViewStateLastIconSizeName,
		static_cast<int32>(fLastIconSize));
}
