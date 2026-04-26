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
 *   Copyright 2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, <axeld@pinc-software.de>
 */


/**
 * @file IPAddressControl.cpp
 * @brief Implementation of IPAddressControl, a BTextControl that validates
 *        IPv4/IPv6 addresses on every keystroke.
 */


#include "IPAddressControl.h"

#include <NetworkAddress.h>


/** @brief Internal modification-notification message used to trigger
           validation. */
static const uint32 kMsgModified = 'txmd';


/**
 * @brief Constructs the control bound to the given address family.
 *
 * Wires a modification message so each keystroke triggers _UpdateMark().
 *
 * @param family  Address family (AF_INET or AF_INET6) used for parsing.
 * @param label   Control label shown to the user.
 * @param name    BHandler name; may be NULL.
 */
IPAddressControl::IPAddressControl(int family, const char* label,
	const char* name)
	:
	BTextControl(name, label, "", NULL),
	fFamily(family),
	fAllowEmpty(true)
{
	SetModificationMessage(new BMessage(kMsgModified));
}


/**
 * @brief Destructor.
 */
IPAddressControl::~IPAddressControl()
{
}


/**
 * @brief Reports whether empty input is treated as valid.
 *
 * @return true if an empty field passes validation.
 */
bool
IPAddressControl::AllowEmpty() const
{
	return fAllowEmpty;
}


/**
 * @brief Configures whether empty input is valid.
 *
 * @param empty  When true, an empty string is accepted; when false, it is
 *               flagged as invalid.
 */
void
IPAddressControl::SetAllowEmpty(bool empty)
{
	fAllowEmpty = empty;
}


/**
 * @brief Retargets modification messages to this view and runs an initial
 *        validation pass.
 */
void
IPAddressControl::AttachedToWindow()
{
	BTextControl::AttachedToWindow();
	SetTarget(this);
	_UpdateMark();
}


/**
 * @brief Handles modification notifications by re-running validation.
 *
 * @param message  Incoming BMessage.
 */
void
IPAddressControl::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgModified:
			_UpdateMark();
			break;

		default:
			BTextControl::MessageReceived(message);
			break;
	}
}


/**
 * @brief Re-parses the current text and updates the invalid-mark state.
 *
 * Empty input defers to fAllowEmpty; non-empty input is parsed as a
 * numeric address with DNS resolution disabled.
 */
void
IPAddressControl::_UpdateMark()
{
	if (TextLength() == 0) {
		MarkAsInvalid(!fAllowEmpty);
		return;
	}

	BNetworkAddress address;
	bool success = address.SetTo(fFamily, Text(), (char*)NULL,
		B_NO_ADDRESS_RESOLUTION) == B_OK;

	MarkAsInvalid(!success);
}
