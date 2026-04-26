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
 * MIT License. Copyright 2023, Haiku, Inc.
 * Original authors: John Scipione (jscipione@gmail.com).
 */

/** @file StatusMenuField.h
    @brief BMenuField with overlaid status icons (stop/warn) and a tooltip. */

#ifndef STATUS_MENU_FIELD_H
#define STATUS_MENU_FIELD_H


#include <MenuField.h>
#include <MenuItem.h>
#include <String.h>


class BBitmap;


/**
 * @brief BMenuItem variant that draws a status icon to the right of the label.
 *
 * Used inside StatusMenuField's pop-up to render either a stop or warning
 * icon. The label is reserved enough horizontal space to avoid overlap.
 */
class StatusMenuItem : public BMenuItem {
public:
								StatusMenuItem(const char* name, BMessage* message = NULL);
								StatusMenuItem(BMessage* archive);

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	virtual	void				DrawContent();
	virtual	void				GetContentSize(float* _width, float* _height);

			BBitmap*			Icon();
	virtual	void				SetIcon(BBitmap* icon);

			BRect				IconRect();
			BSize				IconSize();
			float				Spacing();

private:
			BBitmap*			fIcon;
};


/**
 * @brief BMenuField that displays a status icon for duplicate or unmatched keys.
 *
 * Used by ModifierKeysWindow to flag rows whose modifier role conflicts
 * with another row (stop icon) or whose left/right roles do not agree
 * (warning icon). The status string drives a localized tooltip.
 */
class StatusMenuField : public BMenuField {
public:
								StatusMenuField(const char*, BMenu*);
								~StatusMenuField();

	virtual	void				SetDuplicate(bool on);
	virtual	void				SetUnmatched(bool on);

			/** @brief Current status token (@c "duplicate", @c "unmatched", or empty). */
			BString				Status() { return fStatus; };
	virtual	void				ClearStatus();
	virtual	void				SetStatus(BString status);

protected:
	virtual	void				ShowStopIcon(bool show);
	virtual	void				ShowWarnIcon(bool show);

private:
			void				_FillIcons();
			BRect				_IconRect();

private:
			BString				fStatus;

			BBitmap*			fStopIcon;
			BBitmap*			fWarnIcon;
};


#endif	// STATUS_MENU_FIELD_H
