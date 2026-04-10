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
 */
/** @file ErrorLogWindow.h
 *  @brief Window that displays mail daemon error and status messages. */
#ifndef ZOIDBERG_MAIL_ERRORLOGWINDOW_H
#define ZOIDBERG_MAIL_ERRORLOGWINDOW_H

#include <Window.h>
#include <Alert.h>

/** @brief Scrollable window displaying mail daemon log entries. */
class ErrorLogWindow : public BWindow {
public:
		/** @brief Construct the error log window at a given rect. */
		ErrorLogWindow(BRect rect, const char *name, window_type type);

		/** @brief Append an error or info entry to the log. */
		void AddError(alert_type type, const char *message, const char *tag = NULL, 
			bool timestamp = true);

		/** @brief Hide the window instead of quitting. */
		bool QuitRequested();
		/** @brief Adjust scroll bars on resize. */
		void FrameResized(float new_width, float new_height);

		/** @brief Handle color-update messages. */
		void MessageReceived(BMessage* message);

private:
		void SetColumnColors(rgb_color base);

private:
		BView *view; /**< Scrollable error panel */
		bool	fIsRunning; /**< True once the window has been shown */

		rgb_color		fTextColor; /**< Current text color for entries */
		rgb_color		fColumnColor; /**< Background color for even rows */
		rgb_color		fColumnAlternateColor; /**< Background color for odd rows */
};

#endif // ZOIDBERG_MAIL_ERRORLOGWINDOW_H
