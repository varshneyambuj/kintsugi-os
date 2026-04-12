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
 *   Copyright 2011-2013, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file MailFilter.cpp
 * @brief Abstract base class for mail protocol filter add-ons.
 *
 * BMailFilter provides the default no-op implementations of the filter
 * callbacks invoked by BMailProtocol during inbound and outbound message
 * processing. Add-on authors subclass BMailFilter and override the methods
 * they care about (HeaderFetched, BodyFetched, MessageSent, etc.) to inspect
 * or transform messages at each stage of the pipeline.
 *
 * @see BMailProtocol, HaikuMailFormatFilter
 */


#include <MailFilter.h>


/**
 * @brief Constructs a BMailFilter attached to the given protocol and settings.
 *
 * @param protocol  The owning BMailProtocol that will invoke filter callbacks.
 * @param settings  Add-on settings for this filter instance, or NULL if the
 *                  filter has no configurable settings.
 */
BMailFilter::BMailFilter(BMailProtocol& protocol,
	const BMailAddOnSettings* settings)
	:
	fMailProtocol(protocol),
	fSettings(settings)
{
}


/**
 * @brief Destroys the BMailFilter.
 */
BMailFilter::~BMailFilter()
{
}


/**
 * @brief Called when a message header has been fetched from the server.
 *
 * Default implementation takes no action. Override to inspect or modify
 * the message attributes before they are written as BFS attributes.
 *
 * @param ref         File reference of the partially fetched message.
 * @param file        BFile handle for reading or modifying the message file.
 * @param attributes  BMessage accumulating attribute key/value pairs.
 * @return B_NO_MAIL_ACTION to leave the message in place.
 */
BMailFilterAction
BMailFilter::HeaderFetched(entry_ref& ref, BFile& file, BMessage& attributes)
{
	return B_NO_MAIL_ACTION;
}


/**
 * @brief Called when a complete message body has been fetched from the server.
 *
 * Default implementation takes no action. Override to post-process fully
 * downloaded messages, e.g. to update attributes based on body content.
 *
 * @param ref         File reference of the fully fetched message.
 * @param file        BFile handle for the message file.
 * @param attributes  BMessage of current attribute values.
 */
void
BMailFilter::BodyFetched(const entry_ref& ref, BFile& file,
	BMessage& attributes)
{
}


/**
 * @brief Called when a mailbox synchronisation has completed.
 *
 * Default implementation takes no action. Override to react to the end of
 * a fetch cycle (e.g. update a UI badge or trigger dependent queries).
 *
 * @param status  B_OK if the sync was successful, or an error code.
 */
void
BMailFilter::MailboxSynchronized(status_t status)
{
}


/**
 * @brief Called just before a message is submitted to the outbound protocol.
 *
 * Default implementation takes no action. Override to modify or annotate the
 * message file before it is handed off to the SMTP or other outbound handler.
 *
 * @param ref   File reference of the outbound message.
 * @param file  BFile handle for the message file.
 */
void
BMailFilter::MessageReadyToSend(const entry_ref& ref, BFile& file)
{
}


/**
 * @brief Called after a message has been successfully sent.
 *
 * Default implementation takes no action. Override to perform cleanup or
 * bookkeeping after delivery is confirmed.
 *
 * @param ref   File reference of the sent message.
 * @param file  BFile handle for the message file.
 */
void
BMailFilter::MessageSent(const entry_ref& ref, BFile& file)
{
}
