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
 *   Copyright 2001-2006 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers, mflerackers@androme.be
 *       Stefano Ceccherini, burton666@libero.it
 */

/**	Style storage used by BTextView */


#include "InlineInput.h"
#include "StyleBuffer.h"

#include <View.h>

#include <stdio.h>


//	#pragma mark - _BStyleRunDescBuffer_


/**
 * @brief Constructs the run-descriptor buffer with an initial capacity of 20.
 */
_BStyleRunDescBuffer_::_BStyleRunDescBuffer_()
	:
	_BTextViewSupportBuffer_<STEStyleRunDesc>(20)
{
}


/**
 * @brief Inserts a style-run descriptor at the given buffer position.
 *
 * @param inDesc Pointer to the descriptor to insert.
 * @param index  Position at which to insert the new descriptor.
 */
void
_BStyleRunDescBuffer_::InsertDesc(STEStyleRunDesc* inDesc, int32 index)
{
	InsertItemsAt(1, index, inDesc);
}


/**
 * @brief Removes a contiguous range of style-run descriptors.
 *
 * @param index First descriptor index to remove.
 * @param count Number of descriptors to remove.
 */
void
_BStyleRunDescBuffer_::RemoveDescs(int32 index, int32 count)
{
	RemoveItemsAt(count, index);
}


/**
 * @brief Maps a byte offset to the index of the run that contains it.
 *
 * Uses binary search over the descriptor array ordered by STEStyleRunDesc::offset.
 *
 * @param offset Byte offset to look up.
 * @return Index of the run that covers @p offset.
 */
int32
_BStyleRunDescBuffer_::OffsetToRun(int32 offset) const
{
	if (fItemCount <= 1)
		return 0;

	int32 minIndex = 0;
	int32 maxIndex = fItemCount;
	int32 index = 0;

	while (minIndex < maxIndex) {
		index = (minIndex + maxIndex) >> 1;
		if (offset >= fBuffer[index].offset) {
			if (index >= fItemCount - 1
				|| offset < fBuffer[index + 1].offset) {
				break;
			} else
				minIndex = index + 1;
		} else
			maxIndex = index;
	}

	return index;
}


/**
 * @brief Adds @p delta to the byte offset of every run at or after @p index.
 *
 * @param delta Signed byte delta (negative for deletions).
 * @param index First run index to update.
 */
void
_BStyleRunDescBuffer_::BumpOffset(int32 delta, int32 index)
{
	for (int32 i = index; i < fItemCount; i++)
		fBuffer[i].offset += delta;
}


//	#pragma mark - _BStyleRecordBuffer_


/**
 * @brief Constructs an empty style-record buffer.
 */
_BStyleRecordBuffer_::_BStyleRecordBuffer_()
	:
	_BTextViewSupportBuffer_<STEStyleRecord>()
{
}


/**
 * @brief Finds or creates a style record for the given font and colour.
 *
 * Searches for an existing matching record first; if none is found it either
 * reuses an unreferenced slot or expands the buffer.
 *
 * @param inFont  Font to look up or add.
 * @param inColor Text colour to look up or add.
 * @return Index of the matching or newly created style record.
 */
int32
_BStyleRecordBuffer_::InsertRecord(const BFont* inFont,
	const rgb_color* inColor)
{
	int32 index = 0;

	// look for style in buffer
	if (MatchRecord(inFont, inColor, &index))
		return index;

	// style not found, add it
	font_height fh;
	inFont->GetHeight(&fh);

	// check if there's any unused space
	for (index = 0; index < fItemCount; index++) {
		if (fBuffer[index].refs < 1) {
			fBuffer[index].refs = 0;
			fBuffer[index].ascent = fh.ascent;
			fBuffer[index].descent = fh.descent + fh.leading;
			fBuffer[index].style.font = *inFont;
			fBuffer[index].style.color = *inColor;
			return index;
		}
	}

	// no unused space, expand the buffer
	const STEStyle style = { *inFont, *inColor };
	const STEStyleRecord newRecord = {
		0,
		fh.ascent,
		fh.descent + fh.leading,
		style
	};
	InsertItemsAt(1, fItemCount, &newRecord);

	return index;
}


/**
 * @brief Increments the reference count of a style record.
 *
 * @param index Index of the style record to retain.
 */
void
_BStyleRecordBuffer_::CommitRecord(int32 index)
{
	fBuffer[index].refs++;
}


/**
 * @brief Decrements the reference count of a style record.
 *
 * When the reference count reaches zero the slot may be reused by
 * a subsequent InsertRecord() call.
 *
 * @param index Index of the style record to release.
 */
void
_BStyleRecordBuffer_::RemoveRecord(int32 index)
{
	fBuffer[index].refs--;
}


/**
 * @brief Searches for a style record matching the given font and colour.
 *
 * @param inFont    Font to search for.
 * @param inColor   Colour to search for.
 * @param outIndex  Output: index of the matching record if found.
 * @return true if a match was found, false otherwise.
 */
bool
_BStyleRecordBuffer_::MatchRecord(const BFont* inFont, const rgb_color* inColor,
	int32* outIndex)
{
	for (int32 i = 0; i < fItemCount; i++) {
		if (*inFont == fBuffer[i].style.font
			&& *inColor == fBuffer[i].style.color) {
			*outIndex = i;
			return true;
		}
	}

	return false;
}


//	#pragma mark - SetStyleFromMode


/**
 * @brief Copies selected font and colour attributes from one style to another.
 *
 * Only attributes whose corresponding bit is set in @p mode are copied.
 * If @p mode is 0 or B_FONT_ALL, the entire colour is also copied.
 *
 * @param mode       Bitmask of B_FONT_* flags indicating which attributes to copy.
 * @param fromFont   Source font (may be NULL).
 * @param toFont     Destination font (may be NULL).
 * @param fromColor  Source colour (may be NULL).
 * @param toColor    Destination colour (may be NULL).
 */
static void
SetStyleFromMode(uint32 mode, const BFont* fromFont, BFont* toFont,
	const rgb_color* fromColor, rgb_color* toColor)
{
	if (fromFont != NULL && toFont != NULL) {
		if ((mode & B_FONT_FAMILY_AND_STYLE) != 0)
			toFont->SetFamilyAndStyle(fromFont->FamilyAndStyle());

		if ((mode & B_FONT_FACE) != 0)
			toFont->SetFace(fromFont->Face());

		if ((mode & B_FONT_SIZE) != 0)
			toFont->SetSize(fromFont->Size());

		if ((mode & B_FONT_SHEAR) != 0)
			toFont->SetShear(fromFont->Shear());

		if ((mode & B_FONT_FALSE_BOLD_WIDTH) != 0)
			toFont->SetFalseBoldWidth(fromFont->FalseBoldWidth());
	}

	if (fromColor != NULL && toColor != NULL
		&& (mode == 0 || mode == B_FONT_ALL)) {
		*toColor = *fromColor;
	}
}


//	#pragma mark - BTextView::StyleBuffer


/**
 * @brief Constructs the StyleBuffer with the given default font and colour.
 *
 * The null style is the style applied to newly inserted text when there is
 * no surrounding styled text.
 *
 * @param inFont  Default font for the null style.
 * @param inColor Default colour for the null style.
 */
BTextView::StyleBuffer::StyleBuffer(const BFont* inFont,
	const rgb_color* inColor)
	:
	fValidNullStyle(true)
{
	fNullStyle.font = *inFont;
	fNullStyle.color = *inColor;
}


/**
 * @brief Marks the null style as invalid so it will be refreshed on next use.
 */
void
BTextView::StyleBuffer::InvalidateNullStyle()
{
	fValidNullStyle = false;
}


/**
 * @brief Returns whether the null style is currently valid.
 *
 * @return true if the null style reflects the current insertion point style.
 */
bool
BTextView::StyleBuffer::IsValidNullStyle() const
{
	return fValidNullStyle;
}


/**
 * @brief Refreshes the null style from the run that covers @p offset.
 *
 * If the null style is already valid, or if the run buffer is empty, this
 * method does nothing.
 *
 * @param offset Byte offset used to locate the source run.
 */
void
BTextView::StyleBuffer::SyncNullStyle(int32 offset)
{
	if (fValidNullStyle || fStyleRunDesc.ItemCount() < 1)
		return;

	int32 index = OffsetToRun(offset);
	fNullStyle = fStyleRecord[fStyleRunDesc[index]->index]->style;

	fValidNullStyle = true;
}


/**
 * @brief Updates the null style by applying a mode-selected subset of attributes.
 *
 * If the null style is currently invalid, the style at @p offset - 1 is first
 * loaded as the base before the mode-filtered attributes are applied.
 *
 * @param inMode  Bitmask of B_FONT_* flags selecting which attributes to change.
 * @param inFont  Source font for the attributes to apply; may be NULL.
 * @param inColor Source colour to apply; may be NULL.
 * @param offset  Byte offset used to locate the base style if the null style is
 *                invalid.
 */
void
BTextView::StyleBuffer::SetNullStyle(uint32 inMode, const BFont* inFont,
	const rgb_color* inColor, int32 offset)
{
	if (fValidNullStyle || fStyleRunDesc.ItemCount() < 1) {
		SetStyleFromMode(inMode, inFont, &fNullStyle.font, inColor,
			&fNullStyle.color);
	} else {
		int32 index = OffsetToRun(offset - 1);
		fNullStyle = fStyleRecord[fStyleRunDesc[index]->index]->style;
		SetStyleFromMode(inMode, inFont, &fNullStyle.font, inColor,
			&fNullStyle.color);
	}

	fValidNullStyle = true;
}


/**
 * @brief Returns pointers to the current null-style font and colour.
 *
 * Either output pointer may be NULL if that value is not needed.
 *
 * @param font  Output pointer to the null-style BFont; may be NULL.
 * @param color Output pointer to the null-style rgb_color; may be NULL.
 */
void
BTextView::StyleBuffer::GetNullStyle(const BFont** font,
	const rgb_color** color) const
{
	if (font != NULL)
		*font = &fNullStyle.font;

	if (color != NULL)
		*color = &fNullStyle.color;
}


/**
 * @brief Allocates a STEStyleRange large enough to hold @p numStyles runs.
 *
 * @param numStyles Number of style runs to reserve space for.
 * @return Pointer to the allocated range, or NULL if allocation failed.
 */
STEStyleRange*
BTextView::StyleBuffer::AllocateStyleRange(const int32 numStyles) const
{
	STEStyleRange* range = (STEStyleRange*)malloc(sizeof(int32)
		+ sizeof(STEStyleRun) * numStyles);
	if (range != NULL)
		range->count = numStyles;

	return range;
}


/**
 * @brief Applies a style change to all runs within the byte range
 *        [@p fromOffset, @p toOffset).
 *
 * Handles splitting, merging, and inserting run descriptors as needed to
 * maintain a compact, non-redundant run list.
 *
 * @param fromOffset Start of the byte range to restyle.
 * @param toOffset   End of the byte range (exclusive).
 * @param textLen    Total text length (used to detect the last run).
 * @param inMode     B_FONT_* bitmask indicating which attributes to change.
 * @param inFont     New font to apply; falls back to null-style font if NULL.
 * @param inColor    New colour to apply; falls back to null-style colour if NULL.
 */
void
BTextView::StyleBuffer::SetStyleRange(int32 fromOffset, int32 toOffset,
	int32 textLen, uint32 inMode, const BFont* inFont,
	const rgb_color* inColor)
{
	if (inFont == NULL)
		inFont = &fNullStyle.font;

	if (inColor == NULL)
		inColor = &fNullStyle.color;

	if (fromOffset == toOffset) {
		SetNullStyle(inMode, inFont, inColor, fromOffset);
		return;
	}

	if (fStyleRunDesc.ItemCount() < 1) {
		STEStyleRunDesc newDesc;
		newDesc.offset = fromOffset;
		newDesc.index = fStyleRecord.InsertRecord(inFont, inColor);
		fStyleRunDesc.InsertDesc(&newDesc, 0);
		fStyleRecord.CommitRecord(newDesc.index);
		return;
	}

	int32 offset = fromOffset;
	int32 runIndex = OffsetToRun(offset);
	int32 styleIndex = 0;
	do {
		const STEStyleRunDesc runDesc = *fStyleRunDesc[runIndex];
		int32 runEnd = textLen;
		if (runIndex < fStyleRunDesc.ItemCount() - 1)
			runEnd = fStyleRunDesc[runIndex + 1]->offset;

		STEStyle style = fStyleRecord[runDesc.index]->style;
		SetStyleFromMode(inMode, inFont, &style.font, inColor, &style.color);

		styleIndex = fStyleRecord.InsertRecord(&style.font, &style.color);

		if (runDesc.offset == offset && runIndex > 0
			&& fStyleRunDesc[runIndex - 1]->index == styleIndex) {
			RemoveStyles(runIndex);
			runIndex--;
		}

		if (styleIndex != runDesc.index) {
			if (offset > runDesc.offset) {
				STEStyleRunDesc newDesc;
				newDesc.offset = offset;
				newDesc.index = styleIndex;
				fStyleRunDesc.InsertDesc(&newDesc, runIndex + 1);
				fStyleRecord.CommitRecord(newDesc.index);
				runIndex++;
			} else {
				fStyleRunDesc[runIndex]->index = styleIndex;
				fStyleRecord.CommitRecord(styleIndex);
			}

			if (toOffset < runEnd) {
				STEStyleRunDesc newDesc;
				newDesc.offset = toOffset;
				newDesc.index = runDesc.index;
				fStyleRunDesc.InsertDesc(&newDesc, runIndex + 1);
				fStyleRecord.CommitRecord(newDesc.index);
			}
		}

		runIndex++;
		offset = runEnd;
	} while (offset < toOffset);

	if (offset == toOffset && runIndex < fStyleRunDesc.ItemCount()
		&& fStyleRunDesc[runIndex]->index == styleIndex) {
		RemoveStyles(runIndex);
	}
}


/**
 * @brief Returns the font and colour at a given byte offset.
 *
 * Falls back to the null style when no runs have been recorded yet.
 *
 * @param inOffset  Byte offset to query.
 * @param outFont   Output font pointer; may be NULL.
 * @param outColor  Output colour pointer; may be NULL.
 */
void
BTextView::StyleBuffer::GetStyle(int32 inOffset, BFont* outFont,
	rgb_color* outColor) const
{
	if (fStyleRunDesc.ItemCount() < 1) {
		if (outFont != NULL)
			*outFont = fNullStyle.font;

		if (outColor != NULL)
			*outColor = fNullStyle.color;

		return;
	}

	int32 runIndex = OffsetToRun(inOffset);
	int32 styleIndex = fStyleRunDesc[runIndex]->index;

	if (outFont != NULL)
		*outFont = fStyleRecord[styleIndex]->style.font;

	if (outColor != NULL)
		*outColor = fStyleRecord[styleIndex]->style.color;
}


/**
 * @brief Returns a heap-allocated STEStyleRange covering the given byte range.
 *
 * The caller is responsible for freeing the returned pointer.
 *
 * @param startOffset First byte of the range.
 * @param endOffset   Last byte of the range.
 * @return Allocated STEStyleRange, or NULL if allocation failed.
 */
STEStyleRange*
BTextView::StyleBuffer::GetStyleRange(int32 startOffset, int32 endOffset) const
{
	int32 startIndex = OffsetToRun(startOffset);
	int32 endIndex = OffsetToRun(endOffset);

	int32 numStyles = endIndex - startIndex + 1;
	if (numStyles < 1)
		numStyles = 1;

	STEStyleRange* result = AllocateStyleRange(numStyles);
	if (result == NULL)
		return NULL;

	STEStyleRun* run = &result->runs[0];
	for (int32 index = 0; index < numStyles; index++) {
		*run = (*this)[startIndex + index];
		run->offset -= startOffset;
		if (run->offset < 0)
			run->offset = 0;

		run++;
	}

	return result;
}


/**
 * @brief Removes all style runs that overlap the deleted byte range.
 *
 * Adjusts offsets of surviving runs so that the run list remains consistent
 * after a text deletion.
 *
 * @param fromOffset Start of the deleted byte range.
 * @param toOffset   End of the deleted byte range.
 */
void
BTextView::StyleBuffer::RemoveStyleRange(int32 fromOffset, int32 toOffset)
{
	int32 fromIndex = fStyleRunDesc.OffsetToRun(fromOffset);
	int32 toIndex = fStyleRunDesc.OffsetToRun(toOffset) - 1;

	int32 count = toIndex - fromIndex;
	if (count > 0) {
		RemoveStyles(fromIndex + 1, count);
		toIndex = fromIndex;
	}

	fStyleRunDesc.BumpOffset(fromOffset - toOffset, fromIndex + 1);

	if (toIndex == fromIndex && toIndex < fStyleRunDesc.ItemCount() - 1) {
		STEStyleRunDesc* runDesc = fStyleRunDesc[toIndex + 1];
		runDesc->offset = fromOffset;
	}

	if (fromIndex < fStyleRunDesc.ItemCount() - 1) {
		STEStyleRunDesc* runDesc = fStyleRunDesc[fromIndex];
		if (runDesc->offset == (runDesc + 1)->offset) {
			RemoveStyles(fromIndex);
			fromIndex--;
		}
	}

	if (fromIndex >= 0 && fromIndex < fStyleRunDesc.ItemCount() - 1) {
		STEStyleRunDesc* runDesc = fStyleRunDesc[fromIndex];
		if (runDesc->index == (runDesc + 1)->index)
			RemoveStyles(fromIndex + 1);
	}
}


/**
 * @brief Removes @p count consecutive style runs starting at @p index.
 *
 * Decrements the reference count on each associated style record before
 * removing the descriptors.
 *
 * @param index First run index to remove.
 * @param count Number of runs to remove (default 1).
 */
void
BTextView::StyleBuffer::RemoveStyles(int32 index, int32 count)
{
	for (int32 i = index; i < index + count; i++)
		fStyleRecord.RemoveRecord(fStyleRunDesc[i]->index);

	fStyleRunDesc.RemoveDescs(index, count);
}


/**
 * @brief Returns the length of the uniform-style segment starting at
 *        @p fromOffset.
 *
 * Fills in the font, colour, and metric pointers for the style covering
 * @p fromOffset, then returns how many bytes remain until the next style
 * change (or until @p length bytes have been accounted for).
 *
 * @param fromOffset  First byte of the segment.
 * @param length      Maximum number of bytes to consider.
 * @param input       InlineInput state (currently unused; reserved for future
 *                    inline-input highlighting).
 * @param outFont     Output pointer to the segment's font; may be NULL.
 * @param outColor    Output pointer to the segment's colour; may be NULL.
 * @param outAscent   Output: ascent of the segment's font; may be NULL.
 * @param outDescent  Output: descent + leading; may be NULL.
 * @return Number of bytes in the uniform segment, or 0 if empty.
 */
int32
BTextView::StyleBuffer::Iterate(int32 fromOffset, int32 length,
	InlineInput* input,
	const BFont** outFont, const rgb_color** outColor,
	float* outAscent, float* outDescent, uint32*) const
{
	// TODO: Handle the InlineInput style here in some way
	int32 numRuns = fStyleRunDesc.ItemCount();
	if (length < 1 || numRuns < 1)
		return 0;

	int32 result = length;
	int32 runIndex = fStyleRunDesc.OffsetToRun(fromOffset);
	STEStyleRunDesc* run = fStyleRunDesc[runIndex];

	if (outFont != NULL)
		*outFont = &fStyleRecord[run->index]->style.font;

	if (outColor != NULL)
		*outColor = &fStyleRecord[run->index]->style.color;

	if (outAscent != NULL)
		*outAscent = fStyleRecord[run->index]->ascent;

	if (outDescent != NULL)
		*outDescent = fStyleRecord[run->index]->descent;

	if (runIndex < numRuns - 1) {
		int32 nextOffset = (run + 1)->offset - fromOffset;
		result = min_c(result, nextOffset);
	}

	return result;
}


/**
 * @brief Maps a byte offset to the index of the style run that contains it.
 *
 * @param offset Byte offset to look up.
 * @return Run index covering @p offset.
 */
int32
BTextView::StyleBuffer::OffsetToRun(int32 offset) const
{
	return fStyleRunDesc.OffsetToRun(offset);
}


/**
 * @brief Adjusts run-descriptor offsets after a text insertion or deletion.
 *
 * @param delta Signed byte delta to add to each affected offset.
 * @param index First run index to update.
 */
void
BTextView::StyleBuffer::BumpOffset(int32 delta, int32 index)
{
	fStyleRunDesc.BumpOffset(delta, index);
}


/**
 * @brief Returns a copy of the STEStyleRun at the given run-descriptor index.
 *
 * Falls back to the null style if the run-descriptor buffer is empty.
 *
 * @param index Run-descriptor index.
 * @return A value-copy of the requested STEStyleRun.
 */
STEStyleRun
BTextView::StyleBuffer::operator[](int32 index) const
{
	STEStyleRun run;

	if (fStyleRunDesc.ItemCount() < 1) {
		run.offset = 0;
		run.style = fNullStyle;
	} else {
		STEStyleRunDesc* runDesc = fStyleRunDesc[index];
		run.offset = runDesc->offset;
		run.style = fStyleRecord[runDesc->index]->style;
	}

	return run;
}


// TODO: Horrible name, but can't think of a better one
// ? CompareStyles ?
// ? FilterStyles ?
/**
 * @brief Clears bits in @p mode for font attributes that differ between two styles.
 *
 * Used by ContinuousGetStyle() to compute which attributes are uniform across
 * a selection spanning multiple style runs.
 *
 * @param firstStyle  Reference style (first run in the selection).
 * @param otherStyle  Another run's style to compare against.
 * @param mode        In/out bitmask; bits are cleared where styles differ.
 * @param sameColor   In/out flag; set to false if the colours differ.
 */
static void
FixupMode(const STEStyle &firstStyle, const STEStyle &otherStyle, uint32 &mode,
	bool &sameColor)
{
	if ((mode & B_FONT_FAMILY_AND_STYLE) != 0) {
		if (firstStyle.font != otherStyle.font)
			mode &= ~B_FONT_FAMILY_AND_STYLE;
	}
	if ((mode & B_FONT_SIZE) != 0) {
		if (firstStyle.font.Size() != otherStyle.font.Size())
			mode &= ~B_FONT_SIZE;
	}
	if ((mode & B_FONT_SHEAR) != 0) {
		if (firstStyle.font.Shear() != otherStyle.font.Shear())
			mode &= ~B_FONT_SHEAR;
	}
	if ((mode & B_FONT_FALSE_BOLD_WIDTH) != 0) {
		if (firstStyle.font.FalseBoldWidth()
				!= otherStyle.font.FalseBoldWidth()) {
			mode &= ~B_FONT_FALSE_BOLD_WIDTH;
		}
	}
	if (firstStyle.color != otherStyle.color)
		sameColor = false;

	// TODO: Finish this: handle B_FONT_FACE, B_FONT_FLAGS, etc.
	// if needed
}


/**
 * @brief Returns the common font attributes and colour across a text range.
 *
 * Computes the intersection of all style attributes within the range
 * [@p fromOffset, @p toOffset).  Attributes that differ across runs have
 * their corresponding bit cleared in @p *ioMode.
 *
 * @param outFont     Output: font from the last run in the range; may be NULL.
 * @param ioMode      In/out bitmask; bits are cleared for differing attributes.
 * @param outColor    Output: colour from the last run; may be NULL.
 * @param sameColor   Output: true if all runs share the same colour; may be NULL.
 * @param fromOffset  Start byte offset.
 * @param toOffset    End byte offset (exclusive).
 */
void
BTextView::StyleBuffer::ContinuousGetStyle(BFont *outFont, uint32* ioMode,
	rgb_color* outColor, bool* sameColor, int32 fromOffset,
	int32 toOffset) const
{
	uint32 mode = B_FONT_ALL;

	if (fStyleRunDesc.ItemCount() < 1) {
		if (ioMode)
			*ioMode = mode;

		if (outFont != NULL)
			*outFont = fNullStyle.font;

		if (outColor != NULL)
			*outColor = fNullStyle.color;

		if (sameColor != NULL)
			*sameColor = true;

		return;
	}

	int32 fromIndex = OffsetToRun(fromOffset);
	int32 toIndex = OffsetToRun(toOffset - 1);

	if (fromIndex == toIndex) {
		int32 styleIndex = fStyleRunDesc[fromIndex]->index;
		const STEStyle* style = &fStyleRecord[styleIndex]->style;

		if (ioMode != NULL)
			*ioMode = mode;

		if (outFont != NULL)
			*outFont = style->font;

		if (outColor != NULL)
			*outColor = style->color;

		if (sameColor != NULL)
			*sameColor = true;
	} else {
		bool oneColor = true;
		int32 styleIndex = fStyleRunDesc[toIndex]->index;
		STEStyle theStyle = fStyleRecord[styleIndex]->style;

		for (int32 i = fromIndex; i < toIndex; i++) {
			styleIndex = fStyleRunDesc[i]->index;
			FixupMode(fStyleRecord[styleIndex]->style, theStyle, mode,
				oneColor);
		}

		if (ioMode != NULL)
			*ioMode = mode;

		if (outFont != NULL)
			*outFont = theStyle.font;

		if (outColor != NULL)
			*outColor = theStyle.color;

		if (sameColor != NULL)
			*sameColor = oneColor;
	}
}
