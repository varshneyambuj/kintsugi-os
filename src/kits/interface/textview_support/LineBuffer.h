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
 * MIT License. Copyright 2001-2006, Haiku, Inc.
 * Original authors: Marc Flerackers (mflerackers@androme.be).
 */

/** @file LineBuffer.h
    @brief Line index for BTextView: caches per-line offsets, vertical positions,
           and ascent/width metrics for fast wrap and hit-testing. */

#ifndef __LINE_BUFFER_H
#define __LINE_BUFFER_H


#include <SupportDefs.h>
#include <TextView.h>

#include "TextViewSupportBuffer.h"

/** @brief One cached entry per line: text offset, top y, ascent, and pixel width. */
struct STELine {
	long		offset;		// offset of first character of line
	float		origin;		// pixel position of top of line
	float		ascent;		// maximum ascent for line
	float		width;		// cached width of line in pixels
};


/**
 * @brief Sorted line index used by BTextView.
 *
 * Stores one STELine per line of wrapped text plus a sentinel at the end.
 * Supports binary-search queries from text offset to line and from pixel
 * position to line, plus bulk shift operations when text or line heights
 * change above a given line.
 */
class BTextView::LineBuffer : public _BTextViewSupportBuffer_<STELine> {

public:
								LineBuffer();
	virtual						~LineBuffer();

			void				InsertLine(STELine* inLine, int32 index);
			void				RemoveLines(int32 index, int32 count = 1);
			void				RemoveLineRange(int32 fromOffset,
									int32 toOffset);

			int32				OffsetToLine(int32 offset) const;
			int32				PixelToLine(float pixel) const;

			void				BumpOrigin(float delta, int32 index);
			void				BumpOffset(int32 delta, int32 index);

			int32				NumLines() const;
			float				MaxWidth() const;
			STELine*			operator[](int32 index) const;
};


/**
 * @brief Returns the number of real lines (excluding the trailing sentinel).
 *
 * @return Line count.
 */
inline int32
BTextView::LineBuffer::NumLines() const
{
	return fItemCount - 1;
}


/**
 * @brief Returns the line entry at @a index.
 *
 * @param index Zero-based line index; no bounds checking is performed.
 * @return Pointer to the STELine record at that index.
 */
inline STELine *
BTextView::LineBuffer::operator[](int32 index) const
{
	return &fBuffer[index];
}


#endif	// __LINE_BUFFER_H
