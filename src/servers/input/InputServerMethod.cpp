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
 *  Method: BInputServerMethod::BInputServerMethod()
 *   Descr: 
 */
BInputServerMethod::BInputServerMethod(const char *name,
                                       const uchar *icon)
{
	CALLED();
	fOwner = new _BMethodAddOn_(this, name, icon);
}


/**
 *  Method: BInputServerMethod::~BInputServerMethod()
 *   Descr: 
 */
BInputServerMethod::~BInputServerMethod()
{
	CALLED();
	delete fOwner;
}


/**
 *  Method: BInputServerMethod::MethodActivated()
 *   Descr: 
 */
status_t
BInputServerMethod::MethodActivated(bool active)
{
    return B_OK;
}


/**
 *  Method: BInputServerMethod::EnqueueMessage()
 *   Descr: 
 */
status_t
BInputServerMethod::EnqueueMessage(BMessage *message)
{
	return ((InputServer*)be_app)->EnqueueMethodMessage(message);
}


/**
 *  Method: BInputServerMethod::SetName()
 *   Descr: 
 */
status_t
BInputServerMethod::SetName(const char *name)
{
    return fOwner->SetName(name);
}


/**
 *  Method: BInputServerMethod::SetIcon()
 *   Descr: 
 */
status_t
BInputServerMethod::SetIcon(const uchar *icon)
{
    return fOwner->SetIcon(icon);
}


/**
 *  Method: BInputServerMethod::SetMenu()
 *   Descr: 
 */
status_t
BInputServerMethod::SetMenu(const BMenu *menu,
                            const BMessenger target)
{
    return fOwner->SetMenu(menu, target);
}


/**
 *  Method: BInputServerMethod::_ReservedInputServerMethod1()
 *   Descr: 
 */
void
BInputServerMethod::_ReservedInputServerMethod1()
{
}


/**
 *  Method: BInputServerMethod::_ReservedInputServerMethod2()
 *   Descr: 
 */
void
BInputServerMethod::_ReservedInputServerMethod2()
{
}


/**
 *  Method: BInputServerMethod::_ReservedInputServerMethod3()
 *   Descr: 
 */
void
BInputServerMethod::_ReservedInputServerMethod3()
{
}


/**
 *  Method: BInputServerMethod::_ReservedInputServerMethod4()
 *   Descr: 
 */
void
BInputServerMethod::_ReservedInputServerMethod4()
{
}


static int32 sNextMethodCookie = 1;


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


_BMethodAddOn_::~_BMethodAddOn_()
{
	free(fName);
	delete fMenu;
}


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


KeymapMethod::KeymapMethod()
        : BInputServerMethod("Roman", kRemoteBits)
{

}


KeymapMethod::~KeymapMethod()
{

}

