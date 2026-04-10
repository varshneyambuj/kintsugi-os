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

/** @file AppGroupView.h
 *  @brief BGroupView that bundles all notifications coming from a single application. */

#ifndef _APP_GROUP_VIEW_H
#define _APP_GROUP_VIEW_H

#include <vector>

#include <GroupView.h>
#include <Messenger.h>
#include <String.h>

class BGroupView;

class NotificationWindow;
class NotificationView;

/** @brief Vector of notification views currently grouped under an application. */
typedef std::vector<NotificationView*> infoview_t;

/** @brief Vertical group of NotificationView instances all belonging to one application.
 *
 * Renders the application label, a collapse toggle, and a close button at
 * the top, then stacks the contained NotificationView children below. The
 * group can collapse to hide its notifications and signals the parent
 * NotificationWindow when it becomes empty so it can be removed. */
class AppGroupView : public BGroupView {
public:
	/** @brief Constructs the group with a parent messenger and an application label. */
								AppGroupView(const BMessenger& messenger, const char* label);

	/** @brief Draws the header (label, collapse toggle, close button). */
	virtual	void				Draw(BRect updateRect);
	/** @brief Handles clicks on the header controls. */
	virtual	void				MouseDown(BPoint point);
	/** @brief Handles child-view removal and dismissal messages. */
	virtual	void				MessageReceived(BMessage* msg);

	/** @brief Returns true if the group currently contains any notifications. */
			bool				HasChildren();
	/** @brief Returns the number of notification children in this group. */
			int32				ChildrenCount();

	/** @brief Inserts a new NotificationView into the group, taking ownership. */
			void				AddInfo(NotificationView* view);
	/** @brief Toggles preview mode on this group and all its child views. */
			void				SetPreviewModeOn(bool enabled);

	/** @brief Returns the application signature this group represents. */
			const BString&		Group() const;
	/** @brief Sets the application signature this group represents. */
			void				SetGroup(const char* group);

	/** @brief Draws the per-group close button. */
			void				DrawCloseButton(const BRect& updateRect);
	/** @brief Returns the size of the close button in pixels. */
			float				CloseButtonSize() const;

private:
			BString				fLabel;          /**< Application label shown in the group header. */
			BMessenger			fMessenger;      /**< Messenger pointing back to the parent NotificationWindow. */
			infoview_t			fInfo;           /**< Notification views currently in the group. */
			bool				fCollapsed;      /**< True if the group is showing only its header. */
			BRect				fCloseRect;      /**< Hit rectangle for the close button. */
			BRect				fCollapseRect;   /**< Hit rectangle for the collapse toggle. */
			float				fHeaderSize;     /**< Height of the header row in pixels. */
			bool				fCloseClicked;   /**< True between close-button press and release. */
			bool				fPreviewModeOn;  /**< True while displayed inside the preferences preview. */
};

#endif	// _APP_GROUP_VIEW_H
