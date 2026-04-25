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
 * Original authors: Marc Flerackers (mflerackers@androme.be),
 *                   Stefano Ceccherini (burton666@libero.it).
 */

/** @file StyleBuffer.h
    @brief Style storage for BTextView: keeps font/color runs over a range of
           text and lets the view query or mutate styles for any offset range. */


#include <Font.h>
#include <InterfaceDefs.h>
#include <SupportDefs.h>
#include <TextView.h>

#include "TextViewSupportBuffer.h"


/** @brief Style descriptor for a single run: font and pen color. */
struct STEStyle {
	BFont		font;		// font
	rgb_color	color;		// pen color
};


/** @brief A style run paired with the text offset where it begins. */
struct STEStyleRun {
	long		offset;		// byte offset of first character of run
	STEStyle	style;		// style info
};


/** @brief A counted array of STEStyleRun used for bulk style queries and applications. */
struct STEStyleRange {
	long		count;		// number of style runs
	STEStyleRun	runs[1];	// array of count number of runs
};


/** @brief Reference-counted style record cached with ascent/descent metrics. */
struct STEStyleRecord {
	long		refs;		// reference count for this style
	float		ascent;		// ascent for this style
	float		descent;	// descent for this style
	STEStyle	style;		// style info
};


/** @brief Run descriptor: text offset paired with the index of a shared style record. */
struct STEStyleRunDesc {
	long		offset;		// byte offset of first character of run
	long		index;		// index of corresponding style record
};


// _BStyleRunDescBuffer_ class -------------------------------------------------
/**
 * @brief Sorted array of style-run descriptors keyed by text offset.
 *
 * Maintains the per-run mapping from text offset to the index of a style
 * record in the companion record buffer.
 */
class _BStyleRunDescBuffer_ : public _BTextViewSupportBuffer_<STEStyleRunDesc> {
public:
								_BStyleRunDescBuffer_();

			void				InsertDesc(STEStyleRunDesc* inDesc,
								int32 index);
			void				RemoveDescs(int32 index, int32 count = 1);

			int32				OffsetToRun(int32 offset) const;
			void				BumpOffset(int32 delta, int32 index);

			STEStyleRunDesc*	operator[](int32 index) const;
};


/**
 * @brief Returns a pointer to the descriptor at @a index.
 *
 * @param index Zero-based descriptor index; no bounds checking is performed.
 * @return Pointer to the run descriptor stored at that index.
 */
inline STEStyleRunDesc*
_BStyleRunDescBuffer_::operator[](int32 index) const
{
	return &fBuffer[index];
}


// _BStyleRecordBuffer_ class --------------------------------------------------
/**
 * @brief Reference-counted pool of unique font/color style records.
 *
 * Style runs share STEStyleRecord entries via reference counting; new
 * combinations are added on demand and removed once their reference count
 * reaches zero.
 */
class _BStyleRecordBuffer_ : public _BTextViewSupportBuffer_<STEStyleRecord> {
public:
								_BStyleRecordBuffer_();

			int32				InsertRecord(const BFont* inFont,
									const rgb_color* inColor);
			void				CommitRecord(int32 index);
			void				RemoveRecord(int32 index);

			bool				MatchRecord(const BFont* inFont,
									const rgb_color* inColor,
									int32* outIndex);

			STEStyleRecord*		operator[](int32 index) const;
};


/**
 * @brief Returns a pointer to the style record at @a index.
 *
 * @param index Zero-based record index; no bounds checking is performed.
 * @return Pointer to the style record stored at that index.
 */
inline STEStyleRecord*
_BStyleRecordBuffer_::operator[](int32 index) const
{
	return &fBuffer[index];
}


// StyleBuffer class --------------------------------------------------------
/**
 * @brief Combined run-descriptor and record buffer that tracks style runs
 *        across a BTextView's text.
 *
 * StyleBuffer answers point and range style queries, supports applying a new
 * style to a range, and maintains a "null style" used by typing operations
 * when there is no selection to inherit from.
 */
class BTextView::StyleBuffer {
public:
								StyleBuffer(const BFont* inFont,
									const rgb_color* inColor);

			void				InvalidateNullStyle();
			bool				IsValidNullStyle() const;

			void				SyncNullStyle(int32 offset);
			void				SetNullStyle(uint32 inMode,
									const BFont* inFont,
									const rgb_color* inColor,
									int32 offset = 0);
			void				GetNullStyle(const BFont** font,
									const rgb_color** color) const;

			void				GetStyle(int32 inOffset, BFont* outFont,
									rgb_color* outColor) const;
			void				ContinuousGetStyle(BFont*, uint32*,
									rgb_color*, bool*, int32, int32) const;

			STEStyleRange*		AllocateStyleRange(
									const int32 numStyles) const;
			void				SetStyleRange(int32 fromOffset,
									int32 toOffset, int32 textLen,
									uint32 inMode, const BFont* inFont,
									const rgb_color* inColor);
			STEStyleRange*		GetStyleRange(int32 startOffset,
									int32 endOffset) const;

			void				RemoveStyleRange(int32 fromOffset,
									int32 toOffset);
			void				RemoveStyles(int32 index, int32 count = 1);

			int32				Iterate(int32 fromOffset, int32 length,
									InlineInput* input,
									const BFont** outFont = NULL,
									const rgb_color** outColor = NULL,
									float* outAscent = NULL,
									float* outDescen = NULL,
									uint32* = NULL) const;

			int32				OffsetToRun(int32 offset) const;
			void				BumpOffset(int32 delta, int32 index);

			STEStyleRun			operator[](int32 index) const;
			int32				NumRuns() const;

	const	_BStyleRunDescBuffer_&	RunBuffer() const;
	const	_BStyleRecordBuffer_&	RecordBuffer() const;

private:
			_BStyleRunDescBuffer_	fStyleRunDesc;
			_BStyleRecordBuffer_	fStyleRecord;
			bool				fValidNullStyle;
			STEStyle			fNullStyle;
};


/**
 * @brief Returns the number of style runs currently tracked.
 *
 * @return Count of run descriptors in the buffer.
 */
inline int32
BTextView::StyleBuffer::NumRuns() const
{
	return fStyleRunDesc.ItemCount();
}


/**
 * @brief Returns the underlying run-descriptor buffer.
 *
 * @return Const reference to the descriptor buffer.
 */
inline const _BStyleRunDescBuffer_&
BTextView::StyleBuffer::RunBuffer() const
{
	return fStyleRunDesc;
}


/**
 * @brief Returns the underlying style-record pool.
 *
 * @return Const reference to the record buffer.
 */
inline const _BStyleRecordBuffer_&
BTextView::StyleBuffer::RecordBuffer() const
{
	return fStyleRecord;
}
