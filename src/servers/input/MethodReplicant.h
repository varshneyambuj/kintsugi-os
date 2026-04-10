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
 *   Copyright (c) 2004, Haiku
 *   This software is part of the Haiku distribution and is covered
 *   by the Haiku license.
 *
 *   Authors:
 *       Jérôme Duval
 */

/** @file MethodReplicant.h
 *  @brief Deskbar tray replicant that lets the user pick the active input method. */

#ifndef METHOD_REPLICANT_H_
#define METHOD_REPLICANT_H_

#include <PopUpMenu.h>
#include <View.h>
#include "MethodMenuItem.h"

/** @brief Symbol name used to identify the replicant in the Deskbar tray. */
#define REPLICANT_CTL_NAME "MethodReplicant"

class _EXPORT MethodReplicant;

/** @brief Deskbar tray view that exposes the input method selection menu.
 *
 * Holds the current method's icon, a pop-up menu of available input
 * methods, and updates itself in response to add/remove/icon-change
 * messages broadcast by the input server. */
class MethodReplicant : public BView {
	public:
		/** @brief Constructs a fresh replicant pointing at the input server signature. */
		MethodReplicant(const char* signature);

		/** @brief Reconstructs the replicant from a Deskbar archive. */
		MethodReplicant(BMessage *);
		// BMessage * based constructor needed to support archiving
		virtual ~MethodReplicant();

		// archiving overrides
		/** @brief Archive-instantiation hook required by BArchivable. */
		static MethodReplicant *Instantiate(BMessage *data);
		/** @brief Serialises the replicant into @p data. */
		virtual	status_t Archive(BMessage *data, bool deep = true) const;

		/** @brief Subscribes to method-change broadcasts on attach. */
		virtual void AttachedToWindow();

		// misc BView overrides
		/** @brief Pops up the method selection menu on click. */
		virtual void MouseDown(BPoint);
		virtual void MouseUp(BPoint);

		/** @brief Draws the current input method's icon. */
		virtual void Draw(BRect);

		/** @brief Handles add/remove/update broadcasts from the input server. */
		virtual void MessageReceived(BMessage *);
	private:
		BBitmap *fSegments;   /**< Cached icon bitmap of the active input method. */
		char *fSignature;     /**< MIME signature of the input server we talk to. */
		BPopUpMenu fMenu;     /**< Pop-up menu of available input methods. */

		void UpdateMethod(BMessage *);
		void UpdateMethodIcon(BMessage *);
		void UpdateMethodMenu(BMessage *);
		void UpdateMethodName(BMessage *);
		void AddMethod(BMessage *message);
		void RemoveMethod(BMessage *message);
		MethodMenuItem *FindItemByCookie(int32 cookie);
};

#endif
