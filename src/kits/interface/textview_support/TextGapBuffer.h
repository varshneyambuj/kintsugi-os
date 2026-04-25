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

/** @file TextGapBuffer.h
    @brief Gap-buffer text storage backing BTextView; supports cheap insertion
           near the cursor and an optional password-mode mask. */

#ifndef __TEXTGAPBUFFER_H
#define __TEXTGAPBUFFER_H


#include <SupportDefs.h>
#include <TextView.h>


class BFile;


namespace BPrivate {


/**
 * @brief Gap-buffer character store used by BTextView.
 *
 * Insertions and deletions near the current gap position are O(amount), so the
 * buffer is well suited to interactive editing where most edits happen close
 * together. RealText() returns the underlying characters, while Text() applies
 * the password mask when password mode is enabled.
 */
class TextGapBuffer {
public:
								TextGapBuffer();
								~TextGapBuffer();

			void				InsertText(const char* inText, int32 inNumItems,
									int32 inAtIndex);
			bool				InsertText(BFile* file, int32 fileOffset,
									int32 amount, int32 atIndex);
			void				RemoveRange(int32 start, int32 end);

			bool				FindChar(char inChar, int32 fromIndex,
									int32* ioDelta);

			const char*			Text();
			const char*			RealText();
			int32				Length() const;

			const char*			GetString(int32 fromOffset, int32* numBytes);
			void				GetString(int32 offset, int32 length,
									char* buffer);

			char				RealCharAt(int32 offset) const;

			bool				PasswordMode() const;
			void				SetPasswordMode(bool);

private:
			void				_MoveGapTo(int32 toIndex);
			void				_EnlargeGapTo(int32 inCount);
			void				_ShrinkGapTo(int32 inCount);

			int32				fItemCount;			// logical count
			char*				fBuffer;			// allocated memory
			int32				fBufferCount;		// physical count
			int32				fGapIndex;			// gap position
			int32				fGapCount;			// gap count
			char*				fScratchBuffer;		// for GetString
			int32				fScratchSize;		// scratch size
			bool				fPasswordMode;
};


/**
 * @brief Returns the logical (gap-excluded) character count of the buffer.
 *
 * @return Number of stored characters, not counting the gap.
 */
inline int32
TextGapBuffer::Length() const
{
	return fItemCount;
}


/**
 * @brief Returns the raw character at @a index, ignoring password masking.
 *
 * Skips over the gap so callers see a contiguous logical sequence. Out-of-range
 * indices return 0; passing an index strictly past the end fires the debugger.
 *
 * @param index Zero-based logical character index.
 * @return The character at @a index, or 0 when @a index equals the buffer length.
 */
inline char
TextGapBuffer::RealCharAt(int32 index) const
{
	if (index < 0 || index >= fItemCount) {
		if (index != fItemCount)
			debugger("RealCharAt: invalid index supplied");
		return 0;
	}

	return index < fGapIndex ? fBuffer[index] : fBuffer[index + fGapCount];
}


} // namespace BPrivate


#endif //__TEXTGAPBUFFER_H
