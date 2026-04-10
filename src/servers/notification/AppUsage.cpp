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
 *
 *   Authors:
 *       Michael Davidson, slaad@bong.com.au
 *       Mikael Eiman, mikael@eiman.tv
 *       Pier Luigi Fiorini, pierluigi.fiorini@gmail.com
 *       Brian Hill, supernova@tycho.email
 */

/** @file AppUsage.cpp
 *  @brief Persisted per-application notification preferences (notify on/off, last seen, etc.). */

#include <Message.h>

#include <AppUsage.h>
#include <NotificationReceived.h>

/** @brief BFlattenable type code identifying serialised AppUsage objects. */
const type_code kTypeCode = 'ipau';


/**
 * @brief Constructs a default AppUsage with empty name/signature and notifications allowed.
 */
AppUsage::AppUsage()
	:
	fAppName(""),
	fSignature(""),
	fAllow(true)
{
}


/**
 * @brief Constructs an AppUsage with the given application name, signature, and permission.
 *
 * @param name      Human-readable application name.
 * @param signature MIME application signature (e.g. "application/x-vnd.MyApp").
 * @param allow     Whether notifications from this application are allowed.
 */
AppUsage::AppUsage(const char* name, const char* signature, bool allow)
	:
	fAppName(name),
	fSignature(signature),
	fAllow(allow)
{
}


/**
 * @brief Returns whether the given type code matches this flattenable's type.
 *
 * @param code The type code to test.
 * @return @c true if @a code equals the AppUsage type code.
 */
bool
AppUsage::AllowsTypeCode(type_code code) const
{
	return code == kTypeCode;
}


/**
 * @brief Serialises this AppUsage into a flat buffer.
 *
 * Packs the application name, signature, and allow flag into a BMessage and
 * flattens it into @a buffer.
 *
 * @param buffer   Destination buffer; must be at least FlattenedSize() bytes.
 * @param numBytes Size of the destination buffer.
 * @return B_OK on success, or B_ERROR if the buffer is too small.
 */
status_t
AppUsage::Flatten(void* buffer, ssize_t numBytes) const
{
	BMessage msg;
	msg.AddString("name", fAppName);
	msg.AddString("signature", fSignature);
	msg.AddBool("allow", fAllow);

	if (numBytes < msg.FlattenedSize())
		return B_ERROR;

	return msg.Flatten((char*)buffer, numBytes);
}


/**
 * @brief Returns the number of bytes required to flatten this AppUsage.
 *
 * @return The flattened size in bytes.
 */
ssize_t
AppUsage::FlattenedSize() const
{
	BMessage msg;
	msg.AddString("name", fAppName);
	msg.AddString("signature", fSignature);
	msg.AddBool("allow", fAllow);

	return msg.FlattenedSize();
}


/**
 * @brief Returns whether the flattened representation has a fixed size.
 *
 * @return Always @c false because the name and signature strings vary in length.
 */
bool
AppUsage::IsFixedSize() const
{
	return false;
}


/**
 * @brief Returns the type code that identifies flattened AppUsage objects.
 *
 * @return The AppUsage type code ('ipau').
 */
type_code
AppUsage::TypeCode() const
{
	return kTypeCode;
}


/**
 * @brief Restores this AppUsage from a flat buffer.
 *
 * Unflattens a BMessage from the buffer and reads the name, signature, and
 * allow flag.
 *
 * @param code     The type code of the data; must equal the AppUsage type code.
 * @param buffer   Source buffer containing the flattened BMessage.
 * @param numBytes Size of the source data.
 * @return B_OK on success, or B_ERROR if the type code is wrong or unflattening fails.
 */
status_t
AppUsage::Unflatten(type_code code, const void* buffer,
	ssize_t numBytes)
{
	if (code != kTypeCode)
		return B_ERROR;

	BMessage msg;
	status_t status = B_ERROR;

	status = msg.Unflatten((const char*)buffer);

	if (status == B_OK) {
		msg.FindString("name", &fAppName);
		msg.FindString("signature", &fSignature);
		msg.FindBool("allow", &fAllow);
	}
	
	return status;
}

						
/**
 * @brief Returns the human-readable application name.
 *
 * @return The application name as a C string.
 */
const char*
AppUsage::AppName()
{
	return fAppName.String();
}


/**
 * @brief Returns the MIME application signature.
 *
 * @return The application signature as a C string.
 */
const char*
AppUsage::Signature()
{
	return fSignature.String();
}


/**
 * @brief Returns whether notifications from this application are allowed.
 *
 * @return @c true if notifications are permitted, @c false otherwise.
 */
bool
AppUsage::Allowed()
{
	return fAllow;
}


/**
 * @brief Sets whether notifications from this application are allowed.
 *
 * @param allow @c true to permit notifications, @c false to suppress them.
 */
void
AppUsage::SetAllowed(bool allow)
{
	fAllow = allow;
}
