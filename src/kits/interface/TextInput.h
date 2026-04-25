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
 * MIT License. Copyright 2001-2020, Haiku.
 * Original authors: Frans van Nispen (xlr8@tref.nl).
 */

/** @file TextInput.h
    @brief Private BTextView derivative used as the editable text body of a BTextControl. */

//! The BTextView derivative owned by an instance of BTextControl.

#ifndef	_TEXT_CONTROLI_H
#define	_TEXT_CONTROLI_H


#include <TextView.h>


class BTextControl;

namespace BPrivate {

/**
 * @brief Private BTextView subclass that backs a BTextControl.
 *
 * Adds undo-on-revert behaviour, single-line key handling, and tight
 * integration with the parent BTextControl's frame and focus state.
 */
class _BTextInput_ : public BTextView {
public:
						_BTextInput_(BRect frame, BRect textRect,
							uint32 resizeMask,
							uint32 flags = B_WILL_DRAW | B_PULSE_NEEDED);
						_BTextInput_(BMessage *data);
virtual					~_BTextInput_();

static	BArchivable*	Instantiate(BMessage *data);
virtual	status_t		Archive(BMessage *data, bool deep = true) const;

virtual	void			MouseDown(BPoint where);
virtual	void			FrameResized(float width, float height);
virtual	void			KeyDown(const char *bytes, int32 numBytes);
virtual	void			MakeFocus(bool focusState = true);

virtual	BSize			MinSize();

		/** @brief Snapshot the current text so that revert-on-escape can restore it. */
		void			SetInitialText();

virtual	void			Paste(BClipboard *clipboard);

protected:

virtual	void			InsertText(const char *inText, int32 inLength,
								   int32 inOffset, const text_run_array *inRuns);
virtual	void			DeleteText(int32 fromOffset, int32 toOffset);

private:

		BTextControl	*TextControl();

		char			*fPreviousText;
		bool			fInMouseDown;
};

}	// namespace BPrivate

using namespace BPrivate;


#endif	// _TEXT_CONTROLI_H

