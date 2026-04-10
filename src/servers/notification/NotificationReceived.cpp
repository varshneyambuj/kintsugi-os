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
 *   Copyright 2010, Haiku, Inc. All Rights Reserved.
 *   Copyright 2008-2009, Pier Luigi Fiorini. All Rights Reserved.
 *   Copyright 2004-2008, Michael Davidson. All Rights Reserved.
 *   Copyright 2004-2007, Mikael Eiman. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Davidson, slaad@bong.com.au
 *       Mikael Eiman, mikael@eiman.tv
 *       Pier Luigi Fiorini, pierluigi.fiorini@gmail.com
 */

/** @file NotificationReceived.cpp
 *  @brief Tracking record for a single notification's last-displayed state. */

#include <Message.h>
#include <Notification.h>
#include <NotificationReceived.h>

/** @brief BFlattenable type code identifying serialised NotificationReceived objects. */
const type_code kTypeCode = 'ipnt';


/**
 * @brief Constructs a default NotificationReceived with an empty title, information type,
 *        disabled state, and the current time as the last-received timestamp.
 */
NotificationReceived::NotificationReceived()
	:
	fTitle(""),
	fType(B_INFORMATION_NOTIFICATION),
	fEnabled(false),
	fLastReceived(time(NULL))
{
}


/**
 * @brief Constructs a NotificationReceived with the given title, type, and enabled state.
 *
 * The last-received timestamp is set to the current time.
 *
 * @param title   Human-readable notification title.
 * @param type    The notification_type (information, important, error, or progress).
 * @param enabled Whether this notification is currently enabled.
 */
NotificationReceived::NotificationReceived(const char* title,
	notification_type type, bool enabled)
	:
	fTitle(title),
	fType(type),
	fEnabled(enabled),
	fLastReceived(time(NULL))
{
}


/** @brief Destroys the NotificationReceived record. */
NotificationReceived::~NotificationReceived()
{
}


/**
 * @brief Returns whether the given type code matches this flattenable's type.
 *
 * @param code The type code to test.
 * @return @c true if @a code equals the NotificationReceived type code.
 */
bool
NotificationReceived::AllowsTypeCode(type_code code) const
{
	return code == kTypeCode;
}


/**
 * @brief Serialises this NotificationReceived into a flat buffer.
 *
 * Packs the title, type, last-received timestamp, and enabled flag into a
 * BMessage and flattens it into @a buffer.
 *
 * @param buffer   Destination buffer; must be at least FlattenedSize() bytes.
 * @param numBytes Size of the destination buffer.
 * @return B_OK on success, or B_ERROR if the buffer is too small.
 */
status_t
NotificationReceived::Flatten(void* buffer, ssize_t numBytes) const
{
	BMessage msg;
	msg.AddString("notify_title", fTitle);
	msg.AddInt32("notify_type", (int32)fType);
	msg.AddInt32("notify_lastreceived", (int32)fLastReceived);
	msg.AddBool("notify_enabled", fEnabled);

	if (numBytes < msg.FlattenedSize())
		return B_ERROR;
		
	return msg.Flatten((char*)buffer, numBytes);
}


/**
 * @brief Returns the number of bytes required to flatten this NotificationReceived.
 *
 * @return The flattened size in bytes.
 */
ssize_t
NotificationReceived::FlattenedSize() const
{
	BMessage msg;
	msg.AddString("notify_title", fTitle);
	msg.AddInt32("notify_type", (int32)fType);
	msg.AddInt32("notify_lastreceived", (int32)fLastReceived);
	msg.AddBool("notify_enabled", fEnabled);
	
	return msg.FlattenedSize();
}


/**
 * @brief Returns whether the flattened representation has a fixed size.
 *
 * @return Always @c false because the title string varies in length.
 */
bool
NotificationReceived::IsFixedSize() const
{
	return false;
}


/**
 * @brief Returns the type code that identifies flattened NotificationReceived objects.
 *
 * @return The NotificationReceived type code ('ipnt').
 */
type_code
NotificationReceived::TypeCode() const
{
	return kTypeCode;
}


/**
 * @brief Restores this NotificationReceived from a flat buffer.
 *
 * Unflattens a BMessage from the buffer and reads the title, type,
 * last-received timestamp, and enabled flag.
 *
 * @param code     The type code of the data; must equal the NotificationReceived type code.
 * @param buffer   Source buffer containing the flattened BMessage.
 * @param numBytes Size of the source data.
 * @return B_OK on success, or B_ERROR if the type code is wrong or unflattening fails.
 */
status_t
NotificationReceived::Unflatten(type_code code, const void* buffer,
	ssize_t numBytes)
{
	if (code != kTypeCode)
		return B_ERROR;

	BMessage msg;
	status_t error = msg.Unflatten((const char*)buffer);

	if (error == B_OK) {
		msg.FindString("notify_title", &fTitle);
		msg.FindInt32("notify_type", (int32 *)&fType);
		msg.FindInt32("notify_lastreceived", (int32 *)&fLastReceived);
		msg.FindBool("notify_enabled", &fEnabled);
	}

	return error;
}


/**
 * @brief Returns the notification title string.
 *
 * @return The title as a C string.
 */
const char*
NotificationReceived::Title()
{
	return fTitle.String();
}


/**
 * @brief Returns the notification type (information, important, error, or progress).
 *
 * @return The notification_type value.
 */
notification_type
NotificationReceived::Type()
{
	return fType;
}


/**
 * @brief Sets the notification type.
 *
 * @param type The new notification_type value.
 */
void
NotificationReceived::SetType(notification_type type)
{
	fType = type;
}


/**
 * @brief Returns the timestamp of when this notification was last received.
 *
 * @return The last-received time as a time_t value.
 */
time_t
NotificationReceived::LastReceived()
{
	return fLastReceived;
}


/**
 * @brief Returns whether this notification type is currently enabled.
 *
 * @return @c true if the notification is enabled, @c false otherwise.
 */
bool
NotificationReceived::Allowed()
{
	return fEnabled;
}


/**
 * @brief Updates the last-received timestamp to the current time.
 */
void
NotificationReceived::UpdateTimeStamp()
{
	fLastReceived = time(NULL);
}


/**
 * @brief Sets the last-received timestamp to the given value.
 *
 * @param time The new timestamp.
 */
void
NotificationReceived::SetTimeStamp(time_t time)
{
	fLastReceived = time;
}
