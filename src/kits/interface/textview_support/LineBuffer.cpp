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
 *   Copyright 2001-2006, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers (mflerackers@androme.be)
 */


/**
 * @file LineBuffer.cpp
 * @brief Dynamic array of STELine records used by BTextView to track line
 *        layout.
 *
 * LineBuffer extends the generic _BTextViewSupportBuffer_ template to store
 * one STELine per wrapped text line. Each STELine holds a byte offset, a
 * pixel origin (vertical position), and a pixel width. The buffer provides
 * binary-search lookups by both byte offset and pixel position, as well as
 * bulk operations that shift stored values when text is inserted or removed.
 *
 * @see BTextView, StyleBuffer, TextGapBuffer
 */


#include "LineBuffer.h"


/**
 * @brief Constructs an empty LineBuffer with an initial capacity of 20 lines.
 */
BTextView::LineBuffer::LineBuffer()
	:	_BTextViewSupportBuffer_<STELine>(20, 2)
{
}


/**
 * @brief Destroys the LineBuffer.
 */
BTextView::LineBuffer::~LineBuffer()
{
}


/**
 * @brief Inserts a new STELine record at the given position.
 *
 * @param inLine Pointer to the STELine data to insert.
 * @param index  Buffer position at which to insert the new line.
 */
void
BTextView::LineBuffer::InsertLine(STELine* inLine, int32 index)
{
	InsertItemsAt(1, index, inLine);
}


/**
 * @brief Removes a contiguous range of line records from the buffer.
 *
 * @param index First line index to remove.
 * @param count Number of lines to remove.
 */
void
BTextView::LineBuffer::RemoveLines(int32 index, int32 count)
{
	RemoveItemsAt(count, index);
}


/**
 * @brief Removes all line records that fall entirely within a byte range.
 *
 * Finds the line numbers corresponding to @p fromOffset and @p toOffset, then
 * removes the intermediate lines and adjusts the byte offsets of remaining
 * lines so they remain consistent with the deletion.
 *
 * @param fromOffset Start byte offset of the range to clear.
 * @param toOffset   End byte offset of the range to clear.
 */
void
BTextView::LineBuffer::RemoveLineRange(int32 fromOffset, int32 toOffset)
{
	int32 fromLine = OffsetToLine(fromOffset);
	int32 toLine = OffsetToLine(toOffset);

	int32 count = toLine - fromLine;
	if (count > 0)
		RemoveLines(fromLine + 1, count);

	BumpOffset(fromOffset - toOffset, fromLine + 1);
}


/**
 * @brief Maps a byte offset to the index of the line that contains it.
 *
 * Uses binary search over the fBuffer array.
 *
 * @param offset Byte offset to look up.
 * @return Index of the line whose range includes @p offset.
 */
int32
BTextView::LineBuffer::OffsetToLine(int32 offset) const
{
	int32 minIndex = 0;
	int32 maxIndex = fItemCount - 1;
	int32 index = 0;

	while (minIndex < maxIndex) {
		index = (minIndex + maxIndex) >> 1;
		if (offset >= fBuffer[index].offset) {
			if (offset < fBuffer[index + 1].offset)
				break;
			else
				minIndex = index + 1;
		} else
			maxIndex = index;
	}

	return index;
}


/**
 * @brief Maps a pixel vertical position to the index of the line at that
 *        coordinate.
 *
 * Uses binary search over the fBuffer array ordered by STELine::origin.
 *
 * @param pixel Vertical pixel position to look up.
 * @return Index of the line whose origin range includes @p pixel.
 */
int32
BTextView::LineBuffer::PixelToLine(float pixel) const
{
	int32 minIndex = 0;
	int32 maxIndex = fItemCount - 1;
	int32 index = 0;

	while (minIndex < maxIndex) {
		index = (minIndex + maxIndex) >> 1;
		if (pixel >= fBuffer[index].origin) {
			if (pixel < fBuffer[index + 1].origin)
				break;
			else
				minIndex = index + 1;
		} else
			maxIndex = index;
	}

	return index;
}


/**
 * @brief Adds @p delta to the pixel origin of every line at or after @p index.
 *
 * Called when a line's height changes and all subsequent lines must be shifted.
 *
 * @param delta Amount (in pixels) to add to each origin.
 * @param index First line index to update.
 */
void
BTextView::LineBuffer::BumpOrigin(float delta, int32 index)
{
	for (long i = index; i < fItemCount; i++)
		fBuffer[i].origin += delta;
}


/**
 * @brief Adds @p delta to the byte offset of every line at or after @p index.
 *
 * Called after text is inserted or deleted to keep offsets consistent.
 *
 * @param delta Signed byte count to add (negative for deletions).
 * @param index First line index to update.
 */
void
BTextView::LineBuffer::BumpOffset(int32 delta, int32 index)
{
	for (long i = index; i < fItemCount; i++)
		fBuffer[i].offset += delta;
}


/**
 * @brief Returns the maximum line width stored in the buffer.
 *
 * Iterates all stored STELine records and returns the largest width value.
 *
 * @return Maximum width in pixels, or 0 if the buffer is empty.
 */
float
BTextView::LineBuffer::MaxWidth() const
{
	if (fItemCount == 0)
		return 0;

	float maxWidth = 0;
	STELine* line = &fBuffer[0];
	for (int32 i = 0; i < fItemCount; i++) {
		if (maxWidth < line->width)
			maxWidth = line->width;
		line++;
	}
	return maxWidth;
}
