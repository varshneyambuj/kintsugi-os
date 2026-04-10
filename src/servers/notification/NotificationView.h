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
 *   Copyright 2010-2017, Haiku, Inc. All Rights Reserved.
 *   Copyright 2008-2009, Pier Luigi Fiorini. All Rights Reserved.
 *   Copyright 2004-2008, Michael Davidson. All Rights Reserved.
 *   Copyright 2004-2007, Mikael Eiman. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file NotificationView.h
 *  @brief BView that draws a single BNotification, including its icon, text, and close button. */

#ifndef _NOTIFICATION_VIEW_H
#define _NOTIFICATION_VIEW_H

#include <list>

#include <String.h>
#include <View.h>


class AppGroupView;
class BBitmap;
class BMessageRunner;
class BNotification;

/** @brief Internal message asking the parent group to remove an expired view. */
const uint32 kRemoveView = 'ReVi';


/** @brief BView that renders a single notification's icon, title, body, and close affordance.
 *
 * Owns the BNotification it displays, lays out its text into per-line
 * descriptors, and starts a BMessageRunner so that the notification
 * disappears after its timeout (unless the user has disabled timeouts
 * for preview mode). */
class NotificationView : public BView {
public:
	/** @brief Constructs a view for the given notification.
	 *  @param notification    Notification to display; ownership is taken by the view.
	 *  @param timeout         Auto-dismiss timeout in microseconds.
	 *  @param disableTimeout  If true, suppress the auto-dismiss timer (preview mode). */
								NotificationView(BNotification* notification, bigtime_t timeout,
									bool disableTimeout = false);
	virtual						~NotificationView();

	/** @brief Starts the dismissal timer once the view is attached. */
	virtual	void 				AttachedToWindow();
	/** @brief Handles dismissal and progress-update messages. */
	virtual	void 				MessageReceived(BMessage* message);
	/** @brief Draws the icon, text lines, and close button. */
	virtual	void				Draw(BRect updateRect);
	/** @brief Handles mouse clicks (close button, click-through). */
	virtual	void				MouseDown(BPoint point);

/*
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize 				PreferredSize();
*/

	/** @brief Resolves scripting specifiers targeting this view. */
	virtual	BHandler*			ResolveSpecifier(BMessage* msg, int32 index,
									BMessage* specifier, int32 form,
									const char* property);
	/** @brief Reports the scripting suites this view supports. */
	virtual	status_t			GetSupportedSuites(BMessage* msg);

	/** @brief Recomputes the wrapped text layout for the given maximum width. */
			void 				SetText(float newMaxWidth = -1);
	/** @brief Enables preview mode (no auto-dismiss, used by the preferences app). */
			void				SetPreviewModeOn(bool enabled);

	/** @brief Returns the unique message id used to deduplicate notifications. */
			const char*			MessageID() const;
	/** @brief Returns the current progress percentage [0-100], or -1 for non-progress. */
			int					ProgressPercent();

private:
			void				_CalculateSize();
			void				_DrawCloseButton(const BRect& updateRect);

	/** @brief One laid-out line of wrapped notification text. */
			struct LineInfo {
				BFont	font;      /**< Font used for this line. */
				BString	text;      /**< Wrapped text content of this line. */
				BPoint	location;  /**< Top-left position inside the view. */
			};

			typedef std::list<LineInfo*> LineInfoList;

			BNotification*		fNotification;     /**< Notification displayed by this view (owned). */
			bigtime_t			fTimeout;          /**< Auto-dismiss timeout in microseconds. */
			int32				fIconSize;         /**< Icon size in pixels. */
			bool				fDisableTimeout;   /**< True if the auto-dismiss timer is suppressed. */

			AppGroupView*		fGroupView;        /**< Parent group view this notification belongs to. */
			BMessageRunner*		fRunner;           /**< Periodic message runner driving the dismissal timer. */

			BBitmap*			fBitmap;           /**< Cached icon bitmap. */
			LineInfoList		fLines;            /**< Wrapped text lines (owned). */
			float				fHeight;           /**< Computed view height after layout. */
			rgb_color			fStripeColor;      /**< Side stripe colour indicating notification type. */
			bool				fCloseClicked;     /**< True between close-button press and release. */
			bool				fPreviewModeOn;    /**< True while displayed inside the preferences preview. */
};

#endif	// _NOTIFICATION_VIEW_H
