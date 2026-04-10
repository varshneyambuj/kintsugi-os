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
   Copyright 2011-2012, Haiku, Inc. All rights reserved.
   Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
   Distributed under the terms of the MIT License.
 */
/** @file DefaultNotifier.h
 *  @brief BMailNotifier implementation using BNotification for progress. */
#ifndef DEFAULT_NOTIFIER_H
#define DEFAULT_NOTIFIER_H


#include <Notification.h>
#include <String.h>

#include "MailProtocol.h"

#include "ErrorLogWindow.h"
#include "StatusWindow.h"


/** @brief Mail progress notifier using BNotification. */
class DefaultNotifier : public BMailNotifier {
public:
								/** @brief Construct for an account with a display mode. */
								DefaultNotifier(const char* accountName,
									bool inbound, ErrorLogWindow* errorWindow,
									uint32 showMode);
								/** @brief Destructor. */
								~DefaultNotifier();

			/** @brief Create a copy of this notifier. */
			BMailNotifier*		Clone();

			/** @brief Display an error in the log window. */
			void				ShowError(const char* error);
			/** @brief Display an informational message in the log window. */
			void				ShowMessage(const char* message);

			/** @brief Set the total number of messages to transfer. */
			void				SetTotalItems(uint32 items);
			/** @brief Set the total byte size of the transfer. */
			void				SetTotalItemsSize(uint64 size);
			/** @brief Update progress by messages and bytes transferred. */
			void				ReportProgress(uint32 messages, uint64 bytes,
									const char* message = NULL);
			/** @brief Reset counters and optionally update the title. */
			void				ResetProgress(const char* message = NULL);

private:
			void				_NotifyIfAllowed(int timeout = 0);

private:
			BString				fAccountName; /**< Mail account display name */
			bool				fIsInbound; /**< True for fetching, false for sending */
			ErrorLogWindow*		fErrorWindow; /**< Error log window reference */
			BNotification		fNotification; /**< System notification object */
			uint32				fShowMode; /**< Bit flags controlling when notifications appear */

			uint32				fTotalItems; /**< Total messages in the current transfer */
			uint32				fItemsDone; /**< Messages processed so far */
			uint64				fTotalSize; /**< Total bytes to transfer */
			uint64				fSizeDone; /**< Bytes transferred so far */
};


#endif // DEFAULT_NOTIFIER_H
