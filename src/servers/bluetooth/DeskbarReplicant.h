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
 *   Copyright 2009, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Weirauch, dev@m-phasis.de
 */

/** @file DeskbarReplicant.h
 *  @brief Tray replicant view used to expose the Bluetooth server's status in the Deskbar. */

#ifndef DESKBAR_REPLICANT_H
#define DESKBAR_REPLICANT_H


#include <View.h>


/** @brief Deskbar item name used by the Bluetooth tray replicant. */
extern const char* kDeskbarItemName;


/** @brief BView subclass that lives in the Deskbar tray and represents the Bluetooth daemon.
 *
 * Draws a Bluetooth icon, opens preferences on left click, exposes a context
 * menu, and reports a friendly error alert if the daemon goes away. */
class DeskbarReplicant : public BView {
	public:
		/** @brief Constructs the replicant for in-process use. */
		DeskbarReplicant(BRect frame, int32 resizingMode);
		/** @brief Reconstructs the replicant from a Deskbar archive. */
		DeskbarReplicant(BMessage* archive);
		virtual ~DeskbarReplicant();

		/** @brief Archive-instantiation hook required by BArchivable. */
		static	DeskbarReplicant* Instantiate(BMessage* archive);
		/** @brief Serialises the replicant into @p archive. */
		virtual	status_t Archive(BMessage* archive, bool deep = true) const;

		/** @brief Loads the icon bitmap once the view is attached. */
		virtual	void	AttachedToWindow();

		/** @brief Draws the Bluetooth status icon. */
		virtual	void	Draw(BRect updateRect);

		/** @brief Handles messages from menu items and the daemon. */
		virtual	void	MessageReceived(BMessage* message);
		/** @brief Opens the context menu or launches preferences on click. */
		virtual	void	MouseDown(BPoint where);

	private:
		void			_Init();

		void			_QuitBluetoothServer();

		void			_ShowErrorAlert(BString msg, status_t status);

		BBitmap*		fIcon;  /**< Cached Bluetooth icon bitmap. */
};

#endif	// DESKBAR_REPLICANT_H
