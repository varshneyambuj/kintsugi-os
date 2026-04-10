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

/** @file NotificationWindow.h
 *  @brief On-screen window that stacks per-app notification groups and applies user filters. */

#ifndef _NOTIFICATION_WINDOW_H
#define _NOTIFICATION_WINDOW_H

#include <cmath>
#include <vector>
#include <map>

#include <AppFileInfo.h>
#include <Path.h>
#include <String.h>
#include <Window.h>

#include "NotificationView.h"


class AppGroupView;
class AppUsage;

struct property_info;

/** @brief Map from application signature to its visible notification group view. */
typedef std::map<BString, AppGroupView*> appview_t;
/** @brief Map from application signature to its persisted notification preferences. */
typedef std::map<BString, AppUsage*> appfilter_t;

/** @brief Internal message asking the window to remove an empty AppGroupView. */
const uint32 kRemoveGroupView = 'RGVi';


/** @brief BWindow subclass that draws stacked notification groups on the desktop.
 *
 * Owns the per-application AppGroupView instances, the persisted AppUsage
 * filter table, and the user-configurable display settings (position,
 * width, icon size, timeout). Reacts to workspace and screen changes by
 * repositioning itself, and shows/hides automatically based on whether
 * any notifications are currently visible. */
class NotificationWindow : public BWindow {
public:
									NotificationWindow();
	virtual							~NotificationWindow();

	/** @brief Returns true to allow the window to quit. */
	virtual	bool					QuitRequested();
	/** @brief Handles incoming notification, settings, and group-removal messages. */
	virtual	void					MessageReceived(BMessage*);
	/** @brief Repositions the window when the active workspace changes. */
	virtual	void					WorkspaceActivated(int32, bool);
	/** @brief Repositions the window after a frame resize. */
	virtual	void					FrameResized(float width, float height);
	/** @brief Repositions the window when the screen geometry changes. */
	virtual	void					ScreenChanged(BRect frame, color_space mode);

	/** @brief Returns the configured per-notification timeout in microseconds. */
			int32					Timeout();
	/** @brief Returns the configured notification window width. */
			float					Width();

	/** @brief Shows the window if any notifications are visible, hides it otherwise. */
			void					_ShowHide();

private:
	friend class AppGroupView;

			void					SetPosition();
			void					_LoadSettings(bool startMonitor = false);
			void					_LoadAppFilters(BMessage& settings);
			void					_LoadGeneralSettings(BMessage& settings);
			void					_LoadDisplaySettings(BMessage& settings);

			appview_t				fAppViews;     /**< Live per-application notification group views. */
			appfilter_t				fAppFilters;   /**< Persisted per-application notification preferences. */

			float					fWidth;        /**< Configured notification window width. */
			int32					fIconSize;     /**< Icon size used inside notification views. */
			int32					fTimeout;      /**< Per-notification timeout in microseconds. */
			uint32					fPosition;     /**< Screen corner the window docks to. */
			bool					fShouldRun;    /**< False if the user has disabled notifications. */
			BPath					fCachePath;    /**< On-disk cache directory for notification state. */
};

/** @brief Scripting suite property descriptor table for the notification window. */
extern property_info main_prop_list[];

#endif	// _NOTIFICATION_WINDOW_H
