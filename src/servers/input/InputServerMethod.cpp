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
 *   Copyright (c) 2002-2004 Haiku Project
 *   Distributed under the terms of the MIT License.
 *
 *   This is the InputServerMethod implementation.
 */

/** @file InputServerMethod.cpp
 *  @brief BInputServerMethod kit-side implementation used by input method add-ons. */

#include <InputServerMethod.h>
#include <Menu.h>
#include <Messenger.h>
#include "InputServer.h"
#include "InputServerTypes.h"
#include "remote_icon.h"
/**
 * @brief Constructs an input method add-on with the given name and icon.
 *
 * Allocates the internal _BMethodAddOn_ helper that manages the method's
 * state in the Deskbar replicant.
 *
 * @param name Display name of this input method.
 * @param icon Raw 16x16 CMAP8 icon data, or NULL for a default icon.
 */
BInputServerMethod::BInputServerMethod(const char *name,
                                       const uchar *icon)
{
	CALLED();
	fOwner = new _BMethodAddOn_(this, name, icon);
}


/**
 * @brief Destructor; deletes the internal _BMethodAddOn_ helper.
 */
BInputServerMethod::~BInputServerMethod()
{
	CALLED();
	delete fOwner;
}


/**
 * @brief Called when this input method is activated or deactivated.
 *
 * The default implementation does nothing. Subclasses may override to
 * perform setup or teardown when the method gains or loses focus.
 *
 * @param active @c true if the method is being activated, @c false if deactivated.
 * @return B_OK.
 */
status_t
BInputServerMethod::MethodActivated(bool active)
{
    return B_OK;
}


/**
 * @brief Enqueues a message into the input server's method event queue.
 *
 * @param message The message to enqueue. Ownership transfers on success.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BInputServerMethod::EnqueueMessage(BMessage *message)
{
	return ((InputServer*)be_app)->EnqueueMethodMessage(message);
}


/**
 * @brief Changes the display name of this input method in the Deskbar replicant.
 *
 * @param name The new display name.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BInputServerMethod::SetName(const char *name)
{
    return fOwner->SetName(name);
}


/**
 * @brief Changes the icon of this input method in the Deskbar replicant.
 *
 * @param icon Raw 16x16 CMAP8 icon data, or NULL for a default icon.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BInputServerMethod::SetIcon(const uchar *icon)
{
    return fOwner->SetIcon(icon);
}


/**
 * @brief Sets the popup submenu and its target messenger for this input method.
 *
 * Updates the Deskbar replicant so the user can access method-specific options.
 *
 * @param menu   The popup menu to display, or NULL to remove the current one.
 * @param target The messenger that should receive messages from menu items.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BInputServerMethod::SetMenu(const BMenu *menu,
                            const BMessenger target)
{
    return fOwner->SetMenu(menu, target);
}


/** @brief Reserved for future binary compatibility. */
void
BInputServerMethod::_ReservedInputServerMethod1()
{
}


/** @brief Reserved for future binary compatibility. */
void
BInputServerMethod::_ReservedInputServerMethod2()
{
}


/** @brief Reserved for future binary compatibility. */
void
BInputServerMethod::_ReservedInputServerMethod3()
{
}


/** @brief Reserved for future binary compatibility. */
void
BInputServerMethod::_ReservedInputServerMethod4()
{
}


/** @brief Monotonically increasing cookie counter used to assign unique IDs to methods. */
static int32 sNextMethodCookie = 1;


/**
 * @brief Constructs the internal add-on state for an input method.
 *
 * Duplicates the name, copies or zeroes the icon data, and assigns a unique
 * cookie for identifying this method in the Deskbar replicant.
 *
 * @param method Pointer to the owning BInputServerMethod.
 * @param name   Display name of the method (strdup'd).
 * @param icon   Raw 16x16 CMAP8 icon data, or NULL for a grey default.
 */
_BMethodAddOn_::_BMethodAddOn_(BInputServerMethod *method, const char *name,
	const uchar *icon)
	: fMethod(method),
	fMenu(NULL),
	fCookie(sNextMethodCookie++)
{
	fName = strdup(name);
	if (icon != NULL)
		memcpy(fIcon, icon, 16*16*1);
	else
		memset(fIcon, 0x1d, 16*16*1);
}


/**
 * @brief Destructor; frees the name and popup menu.
 */
_BMethodAddOn_::~_BMethodAddOn_()
{
	free(fName);
	delete fMenu;
}


/**
 * @brief Updates this method's display name and notifies the Deskbar replicant.
 *
 * @param name The new display name.
 * @return B_OK on success, or B_ERROR if the replicant messenger is unavailable.
 */
status_t
_BMethodAddOn_::SetName(const char* name)
{
	CALLED();
	if (fName)
		free(fName);
	if (name)
		fName = strdup(name);

	BMessage msg(IS_UPDATE_NAME);
	msg.AddInt32("cookie", fCookie);
	msg.AddString("name", name);
	if (((InputServer*)be_app)->MethodReplicant())
		return ((InputServer*)be_app)->MethodReplicant()->SendMessage(&msg);
	else
		return B_ERROR;
}


/**
 * @brief Updates this method's icon and notifies the Deskbar replicant.
 *
 * @param icon Raw 16x16 CMAP8 icon data, or NULL for a grey default.
 * @return B_OK on success, or B_ERROR if the replicant messenger is unavailable.
 */
status_t
_BMethodAddOn_::SetIcon(const uchar* icon)
{
	CALLED();

	if (icon != NULL)
		memcpy(fIcon, icon, 16*16*1);
	else
		memset(fIcon, 0x1d, 16*16*1);

	BMessage msg(IS_UPDATE_ICON);
	msg.AddInt32("cookie", fCookie);
	msg.AddData("icon", B_RAW_TYPE, icon, 16*16*1);
	if (((InputServer*)be_app)->MethodReplicant())
		return ((InputServer*)be_app)->MethodReplicant()->SendMessage(&msg);
	else
		return B_ERROR;
}


/**
 * @brief Updates the popup menu and its target, notifying the Deskbar replicant.
 *
 * @param menu      The popup menu to archive and send, or NULL.
 * @param messenger The messenger that should receive menu item messages.
 * @return B_OK on success, or B_ERROR if the replicant messenger is unavailable.
 */
status_t
_BMethodAddOn_::SetMenu(const BMenu *menu, const BMessenger &messenger)
{
	CALLED();
	fMenu = menu;
	fMessenger = messenger;

	BMessage msg(IS_UPDATE_MENU);
	msg.AddInt32("cookie", fCookie);
	BMessage menuMsg;
	if (menu)
		menu->Archive(&menuMsg);
	msg.AddMessage("menu", &menuMsg);
	msg.AddMessenger("target", messenger);
	if (((InputServer*)be_app)->MethodReplicant())
		return ((InputServer*)be_app)->MethodReplicant()->SendMessage(&msg);
	else
		return B_ERROR;
}


/**
 * @brief Activates or deactivates this method, updating the Deskbar replicant and
 *        calling through to the owning BInputServerMethod.
 *
 * @param activate @c true to activate, @c false to deactivate.
 * @return B_OK on success, or B_ERROR if no owning method is set.
 */
status_t
_BMethodAddOn_::MethodActivated(bool activate)
{
	CALLED();
	if (fMethod) {
		PRINT(("%s cookie %" B_PRId32 "\n", __PRETTY_FUNCTION__, fCookie));
		if (activate && ((InputServer*)be_app)->MethodReplicant()) {
			BMessage msg(IS_UPDATE_METHOD);
        		msg.AddInt32("cookie", fCookie);
                	((InputServer*)be_app)->MethodReplicant()->SendMessage(&msg);
		}
		return fMethod->MethodActivated(activate);
	}
	return B_ERROR;
}


/**
 * @brief Sends an IS_ADD_METHOD message to the Deskbar replicant to register this method.
 *
 * Packs the cookie, name, icon, menu, and target messenger into a BMessage
 * and sends it to the method replicant.
 *
 * @return B_OK on success, or B_ERROR if the replicant messenger is unavailable.
 */
status_t
_BMethodAddOn_::AddMethod()
{
	PRINT(("%s cookie %" B_PRId32 "\n", __PRETTY_FUNCTION__, fCookie));
	BMessage msg(IS_ADD_METHOD);
	msg.AddInt32("cookie", fCookie);
	msg.AddString("name", fName);
	msg.AddData("icon", B_RAW_TYPE, fIcon, 16*16*1);
	BMessage menuMsg;
	if (fMenu != NULL)
		fMenu->Archive(&menuMsg);
	msg.AddMessage("menu", &menuMsg);
	msg.AddMessenger("target", fMessenger);
	if (((InputServer*)be_app)->MethodReplicant())
		return ((InputServer*)be_app)->MethodReplicant()->SendMessage(&msg);
	else
		return B_ERROR;
}


/**
 * @brief Constructs the built-in Roman keymap input method.
 *
 * Uses the "Roman" display name and the default remote icon bitmap.
 */
KeymapMethod::KeymapMethod()
        : BInputServerMethod("Roman", kRemoteBits)
{

}


/** @brief Destructor. */
KeymapMethod::~KeymapMethod()
{

}

