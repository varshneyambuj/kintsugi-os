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
 *   Copyright 2011-2016, Haiku, Inc. All rights reserved.
 *   Copyright 2001-2003 Dr. Zoidberg Enterprises. All rights reserved.
 */


/**
 * @file MailProtocol.cpp
 * @brief Base classes for inbound and outbound mail protocol add-ons.
 *
 * BMailProtocol is a BLooper subclass that owns a pipeline of BMailFilter
 * objects and provides notification helpers for progress reporting. Protocol
 * add-ons subclass BInboundMailProtocol or BOutboundMailProtocol and
 * implement SyncMessages() or HandleSendMessages() respectively.
 * HaikuMailFormatFilter is always the first filter installed in every pipeline
 * to handle the Haiku-specific BFS attribute mapping.
 *
 * @see BMailFilter, HaikuMailFormatFilter, BMailSettings
 */


#include <stdio.h>
#include <stdlib.h>

#include <fs_attr.h>

#include <Alert.h>
#include <Autolock.h>
#include <Directory.h>
#include <E-mail.h>
#include <FindDirectory.h>
#include <Node.h>
#include <NodeInfo.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <Query.h>
#include <Roster.h>
#include <String.h>
#include <StringList.h>
#include <VolumeRoster.h>

#include <MailFilter.h>
#include <MailDaemon.h>
#include <MailProtocol.h>
#include <MailSettings.h>

#include <mail_util.h>
#include <MailPrivate.h>
#include <NodeMessage.h>

#include "HaikuMailFormatFilter.h"


using namespace BPrivate;


/**
 * @brief Constructs a BMailProtocol looper with the HaikuMailFormatFilter pre-installed.
 *
 * @param name      Human-readable add-on name used to build the looper name.
 * @param settings  Account settings providing account ID and filter configuration.
 */
BMailProtocol::BMailProtocol(const char* name,
	const BMailAccountSettings& settings)
	:
	BLooper(_LooperName(name, settings)),
	fAccountSettings(settings),
	fMailNotifier(NULL)
{
	AddFilter(new HaikuMailFormatFilter(*this, settings));
}


/**
 * @brief Destroys the protocol, freeing the notifier and all loaded filters.
 */
BMailProtocol::~BMailProtocol()
{
	delete fMailNotifier;

	for (int i = 0; i < fFilterList.CountItems(); i++)
		delete fFilterList.ItemAt(i);

	std::map<entry_ref, image_id>::iterator it = fFilterImages.begin();
	for (; it != fFilterImages.end(); it++)
		unload_add_on(it->second);
}


/**
 * @brief Returns the account settings associated with this protocol instance.
 *
 * @return Const reference to the BMailAccountSettings stored at construction.
 */
const BMailAccountSettings&
BMailProtocol::AccountSettings() const
{
	return fAccountSettings;
}


/**
 * @brief Replaces the mail notifier, taking ownership of the new one.
 *
 * The previous notifier (if any) is deleted before the new one is installed.
 *
 * @param mailNotifier  New notifier instance, or NULL to remove notifications.
 */
void
BMailProtocol::SetMailNotifier(BMailNotifier* mailNotifier)
{
	delete fMailNotifier;
	fMailNotifier = mailNotifier;
}


/**
 * @brief Returns the current mail notifier, or NULL if none is set.
 *
 * @return Pointer to the BMailNotifier, or NULL.
 */
BMailNotifier*
BMailProtocol::MailNotifier() const
{
	return fMailNotifier;
}


/**
 * @brief Appends a filter to the end of the protocol pipeline.
 *
 * Thread-safe: acquires the looper lock before modifying the filter list.
 *
 * @param filter  Filter to add; ownership is NOT transferred by this call.
 * @return true if the filter was added successfully, false on allocation failure.
 */
bool
BMailProtocol::AddFilter(BMailFilter* filter)
{
	BAutolock locker(const_cast< BMailProtocol * >(this));
	return fFilterList.AddItem(filter);
}


/**
 * @brief Returns the number of filters in the pipeline.
 *
 * @return Filter count.
 */
int32
BMailProtocol::CountFilter() const
{
	BAutolock locker(const_cast< BMailProtocol * >(this));
	return fFilterList.CountItems();
}


/**
 * @brief Returns the filter at the given index.
 *
 * @param index  Zero-based filter index.
 * @return Pointer to the BMailFilter, or NULL if out of range.
 */
BMailFilter*
BMailProtocol::FilterAt(int32 index) const
{
	BAutolock locker(const_cast< BMailProtocol * >(this));
	return fFilterList.ItemAt(index);
}


/**
 * @brief Removes and returns the filter at the given index.
 *
 * The caller takes ownership of the returned filter.
 *
 * @param index  Zero-based filter index to remove.
 * @return Pointer to the removed filter, or NULL if out of range.
 */
BMailFilter*
BMailProtocol::RemoveFilter(int32 index)
{
	BAutolock locker(const_cast< BMailProtocol * >(this));
	return fFilterList.RemoveItemAt(index);
}


/**
 * @brief Removes a specific filter from the pipeline.
 *
 * @param filter  Filter to remove (compared by pointer identity).
 * @return true if the filter was found and removed.
 */
bool
BMailProtocol::RemoveFilter(BMailFilter* filter)
{
	BAutolock locker(const_cast< BMailProtocol * >(this));
	return fFilterList.RemoveItem(filter);
}


/**
 * @brief Dispatches incoming BMessages to the base looper.
 *
 * @param message  Incoming message from the looper's queue.
 */
void
BMailProtocol::MessageReceived(BMessage* message)
{
	BLooper::MessageReceived(message);
}


/**
 * @brief Displays an error message via the notifier, if one is set.
 *
 * @param error  Null-terminated error string to display.
 */
void
BMailProtocol::ShowError(const char* error)
{
	if (MailNotifier() != NULL)
		MailNotifier()->ShowError(error);
}


/**
 * @brief Displays an informational message via the notifier, if one is set.
 *
 * @param message  Null-terminated message string to display.
 */
void
BMailProtocol::ShowMessage(const char* message)
{
	if (MailNotifier() != NULL)
		MailNotifier()->ShowMessage(message);
}


/**
 * @brief Sets the total number of items expected for progress reporting.
 *
 * @param items  Total item count.
 */
void
BMailProtocol::SetTotalItems(uint32 items)
{
	if (MailNotifier() != NULL)
		MailNotifier()->SetTotalItems(items);
}


/**
 * @brief Sets the total expected byte count for progress reporting.
 *
 * @param size  Total byte count.
 */
void
BMailProtocol::SetTotalItemsSize(uint64 size)
{
	if (MailNotifier() != NULL)
		MailNotifier()->SetTotalItemsSize(size);
}


/**
 * @brief Reports incremental progress to the notifier.
 *
 * @param messages  Number of messages processed since last report.
 * @param bytes     Number of bytes transferred since last report.
 * @param message   Optional status string to display, or NULL.
 */
void
BMailProtocol::ReportProgress(uint32 messages, uint64 bytes,
	const char* message)
{
	if (MailNotifier() != NULL)
		MailNotifier()->ReportProgress(messages, bytes, message);
}


/**
 * @brief Resets the progress indicator to the start of a new operation.
 *
 * @param message  Optional message to display when resetting, or NULL.
 */
void
BMailProtocol::ResetProgress(const char* message)
{
	if (MailNotifier() != NULL)
		MailNotifier()->ResetProgress(message);
}


/**
 * @brief Notifies that a new fetch cycle is beginning with the given message count.
 *
 * Resets progress and sets the total item count so the UI can show a meaningful
 * progress bar.
 *
 * @param count  Number of messages to be fetched.
 */
void
BMailProtocol::NotifyNewMessagesToFetch(int32 count)
{
	ResetProgress();
	SetTotalItems(count);
}


/**
 * @brief Runs header-fetch filters and, on success, writes attributes to the file.
 *
 * Calls _ProcessHeaderFetched() then, if the result is not a delete action,
 * flushes the accumulated attributes into the file using the NodeMessage
 * operator.
 *
 * @param ref         File reference; may be updated with a canonical name.
 * @param file        BFile handle for writing attributes.
 * @param attributes  BMessage accumulating the header attributes.
 * @return The BMailFilterAction returned by the filter pipeline.
 */
BMailFilterAction
BMailProtocol::ProcessHeaderFetched(entry_ref& ref, BFile& file,
	BMessage& attributes)
{
	BMailFilterAction action = _ProcessHeaderFetched(ref, file, attributes);
	if (action >= B_OK && action != B_DELETE_MAIL_ACTION)
		file << attributes;

	return action;
}


/**
 * @brief Runs body-fetch filters and writes updated attributes to the file.
 *
 * @param ref         File reference.
 * @param file        BFile handle for writing attributes.
 * @param attributes  BMessage of attributes to update and flush.
 */
void
BMailProtocol::NotifyBodyFetched(const entry_ref& ref, BFile& file,
	BMessage& attributes)
{
	_NotifyBodyFetched(ref, file, attributes);
	file << attributes;
}


/**
 * @brief Runs the complete header+body fetch filter pipeline in one call.
 *
 * Combines ProcessHeaderFetched() and NotifyBodyFetched() for protocols that
 * always fetch the complete message in one operation.
 *
 * @param ref         File reference; may be updated with a canonical name.
 * @param file        BFile handle.
 * @param attributes  BMessage accumulating attributes.
 * @return The BMailFilterAction from the header stage.
 */
BMailFilterAction
BMailProtocol::ProcessMessageFetched(entry_ref& ref, BFile& file,
	BMessage& attributes)
{
	BMailFilterAction action = _ProcessHeaderFetched(ref, file, attributes);
	if (action >= B_OK && action != B_DELETE_MAIL_ACTION) {
		_NotifyBodyFetched(ref, file, attributes);
		file << attributes;
	}

	return action;
}


/**
 * @brief Invokes MessageReadyToSend() on all installed filters.
 *
 * @param ref   File reference of the outbound message.
 * @param file  BFile handle for the message file.
 */
void
BMailProtocol::NotifyMessageReadyToSend(const entry_ref& ref, BFile& file)
{
	for (int i = 0; i < fFilterList.CountItems(); i++)
		fFilterList.ItemAt(i)->MessageReadyToSend(ref, file);
}


/**
 * @brief Invokes MessageSent() on all installed filters.
 *
 * @param ref   File reference of the sent message.
 * @param file  BFile handle for the message file.
 */
void
BMailProtocol::NotifyMessageSent(const entry_ref& ref, BFile& file)
{
	for (int i = 0; i < fFilterList.CountItems(); i++)
		fFilterList.ItemAt(i)->MessageSent(ref, file);
}


/**
 * @brief Loads and installs all filter add-ons listed in \a settings.
 *
 * @param settings  Protocol settings whose filter list to iterate.
 */
void
BMailProtocol::LoadFilters(const BMailProtocolSettings& settings)
{
	for (int i = 0; i < settings.CountFilterSettings(); i++) {
		BMailAddOnSettings* filterSettings = settings.FilterSettingsAt(i);
		BMailFilter* filter = _LoadFilter(*filterSettings);
		if (filter != NULL)
			AddFilter(filter);
	}
}


/**
 * @brief Constructs the looper name from the add-on name and account name.
 *
 * @param addOnName  Add-on name string.
 * @param settings   Account settings providing the account name suffix.
 * @return Combined looper name string.
 */
/*static*/ BString
BMailProtocol::_LooperName(const char* addOnName,
	const BMailAccountSettings& settings)
{
	BString name = addOnName;

	const char* accountName = settings.Name();
	if (accountName != NULL && accountName[0] != '\0')
		name << " " << accountName;

	return name;
}


/**
 * @brief Loads a filter add-on image and instantiates the filter object.
 *
 * Resolves the add-on's image (loading it if not already cached), looks up
 * the instantiate_filter symbol, and calls it to create the filter instance.
 * The image is retained in fFilterImages so that the symbol remains valid.
 *
 * @param settings  Add-on settings providing the entry_ref of the add-on file.
 * @return Newly allocated BMailFilter, or NULL if loading or instantiation fails.
 */
BMailFilter*
BMailProtocol::_LoadFilter(const BMailAddOnSettings& settings)
{
	const entry_ref& ref = settings.AddOnRef();
	std::map<entry_ref, image_id>::iterator it = fFilterImages.find(ref);
	image_id image;
	if (it != fFilterImages.end())
		image = it->second;
	else {
		BEntry entry(&ref);
		BPath path(&entry);
		image = load_add_on(path.Path());
	}
	if (image < 0)
		return NULL;

	BMailFilter* (*instantiateFilter)(BMailProtocol& protocol,
		const BMailAddOnSettings& settings);
	if (get_image_symbol(image, "instantiate_filter", B_SYMBOL_TYPE_TEXT,
			(void**)&instantiateFilter) != B_OK) {
		unload_add_on(image);
		return NULL;
	}

	fFilterImages[ref] = image;
	return instantiateFilter(*this, settings);
}


/**
 * @brief Passes the header-fetched event through all filters and handles renaming.
 *
 * Calls HeaderFetched() on each filter. If a filter returns B_DELETE_MAIL_ACTION
 * the file is removed. If the ref is changed by a filter, the file is renamed
 * with a unique name to avoid collisions.
 *
 * @param ref         File reference; updated with the final canonical name.
 * @param file        BFile handle.
 * @param attributes  Accumulated header attributes.
 * @return B_DELETE_MAIL_ACTION, B_MOVE_MAIL_ACTION, or B_NO_MAIL_ACTION.
 */
BMailFilterAction
BMailProtocol::_ProcessHeaderFetched(entry_ref& ref, BFile& file,
	BMessage& attributes)
{
	entry_ref outRef = ref;

	for (int i = 0; i < fFilterList.CountItems(); i++) {
		BMailFilterAction action = fFilterList.ItemAt(i)->HeaderFetched(outRef,
			file, attributes);
		if (action == B_DELETE_MAIL_ACTION) {
			// We have to delete the message
			BEntry entry(&ref);
			status_t status = entry.Remove();
			if (status != B_OK) {
				fprintf(stderr, "BMailProtocol::NotifyHeaderFetched(): could "
					"not delete mail: %s\n", strerror(status));
			}
			return B_DELETE_MAIL_ACTION;
		}
	}

	if (ref == outRef)
		return B_NO_MAIL_ACTION;

	// We have to rename the file
	node_ref newParentRef;
	newParentRef.device = outRef.device;
	newParentRef.node = outRef.directory;

	BDirectory newParent(&newParentRef);
	status_t status = newParent.InitCheck();
	BString workerName;
	if (status == B_OK) {
		int32 uniqueNumber = 1;
		do {
			workerName = outRef.name;
			if (uniqueNumber > 1)
				workerName << "_" << uniqueNumber;

			// TODO: support copying to another device!
			BEntry entry(&ref);
			status = entry.Rename(workerName);

			uniqueNumber++;
		} while (status == B_FILE_EXISTS);
	}

	if (status != B_OK) {
		fprintf(stderr, "BMailProtocol::NotifyHeaderFetched(): could not "
			"rename mail (%s)! (should be: %s)\n", strerror(status),
			workerName.String());
	}

	ref = outRef;
	ref.set_name(workerName.String());

	return B_MOVE_MAIL_ACTION;
}


/**
 * @brief Passes the body-fetched event through all filters.
 *
 * @param ref         File reference.
 * @param file        BFile handle.
 * @param attributes  Accumulated message attributes.
 */
void
BMailProtocol::_NotifyBodyFetched(const entry_ref& ref, BFile& file,
	BMessage& attributes)
{
	for (int i = 0; i < fFilterList.CountItems(); i++)
		fFilterList.ItemAt(i)->BodyFetched(ref, file, attributes);
}


// #pragma mark -


/**
 * @brief Constructs a BInboundMailProtocol and loads inbound filters.
 *
 * @param name      Add-on name for the looper name.
 * @param settings  Account settings providing the inbound filter list.
 */
BInboundMailProtocol::BInboundMailProtocol(const char* name,
	const BMailAccountSettings& settings)
	:
	BMailProtocol(name, settings)
{
	LoadFilters(fAccountSettings.InboundSettings());
}


/**
 * @brief Destroys the inbound protocol.
 */
BInboundMailProtocol::~BInboundMailProtocol()
{
}


/**
 * @brief Dispatches kMsgSyncMessages, kMsgFetchBody, and kMsgMarkMessageAsRead.
 *
 * Calls SyncMessages(), HandleFetchBody(), or MarkMessageAsRead() based on the
 * message what code. Unknown messages are forwarded to the base class.
 *
 * @param message  Incoming BMessage from the looper queue.
 */
void
BInboundMailProtocol::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSyncMessages:
		{
			NotiyMailboxSynchronized(SyncMessages());
			break;
		}

		case kMsgFetchBody:
		{
			entry_ref ref;
			if (message->FindRef("ref", &ref) != B_OK)
				break;

			BMessenger target;
			message->FindMessenger("target", &target);

			status_t status = HandleFetchBody(ref, target);
			if (status != B_OK)
				ReplyBodyFetched(target, ref, status);
			break;
		}

		case kMsgMarkMessageAsRead:
		{
			entry_ref ref;
			message->FindRef("ref", &ref);
			read_flags read = (read_flags)message->FindInt32("read");
			MarkMessageAsRead(ref, read);
			break;
		}

		default:
			BMailProtocol::MessageReceived(message);
			break;
	}
}


/**
 * @brief Asynchronously requests a body fetch for the given message ref.
 *
 * Posts kMsgFetchBody to this looper's own message queue so the fetch runs
 * on the protocol thread.
 *
 * @param ref      entry_ref of the partially fetched message.
 * @param replyTo  Optional messenger for B_MAIL_BODY_FETCHED notification.
 * @return B_OK on successful message delivery to the looper.
 */
status_t
BInboundMailProtocol::FetchBody(const entry_ref& ref, BMessenger* replyTo)
{
	BMessage message(kMsgFetchBody);
	message.AddRef("ref", &ref);
	if (replyTo != NULL)
		message.AddMessenger("target", *replyTo);

	return BMessenger(this).SendMessage(&message);
}


/**
 * @brief Updates the read status attribute on the given message file.
 *
 * @param ref   entry_ref of the message to mark.
 * @param flag  B_READ, B_SEEN, or B_UNREAD.
 * @return B_OK on success, or an error code from write_read_attr().
 */
status_t
BInboundMailProtocol::MarkMessageAsRead(const entry_ref& ref, read_flags flag)
{
	BNode node(&ref);
	return write_read_attr(node, flag);
}


/**
 * @brief Sends a B_MAIL_BODY_FETCHED reply to the given messenger.
 *
 * @param replyTo  Destination messenger for the completion notification.
 * @param ref      entry_ref of the message whose body was fetched.
 * @param status   Fetch result status code.
 */
/*static*/ void
BInboundMailProtocol::ReplyBodyFetched(const BMessenger& replyTo,
	const entry_ref& ref, status_t status)
{
	BMessage message(B_MAIL_BODY_FETCHED);
	message.AddInt32("status", status);
	message.AddRef("ref", &ref);
	replyTo.SendMessage(&message);
}


/**
 * @brief Notifies all filters that a mailbox synchronisation has completed.
 *
 * @param status  B_OK if the sync succeeded, or an error code.
 */
void
BInboundMailProtocol::NotiyMailboxSynchronized(status_t status)
{
	for (int32 i = 0; i < CountFilter(); i++)
		FilterAt(i)->MailboxSynchronized(status);
}


// #pragma mark -


/**
 * @brief Constructs a BOutboundMailProtocol and loads outbound filters.
 *
 * @param name      Add-on name for the looper name.
 * @param settings  Account settings providing the outbound filter list.
 */
BOutboundMailProtocol::BOutboundMailProtocol(const char* name,
	const BMailAccountSettings& settings)
	:
	BMailProtocol(name, settings)
{
	LoadFilters(fAccountSettings.OutboundSettings());
}


/**
 * @brief Destroys the outbound protocol.
 */
BOutboundMailProtocol::~BOutboundMailProtocol()
{
}


/**
 * @brief Queues a batch of messages for sending.
 *
 * Posts kMsgSendMessages with the file list and total byte count to the
 * protocol's own looper thread.
 *
 * @param files       BMessage containing refs to the files to send.
 * @param totalBytes  Total byte count of all messages for progress reporting.
 * @return B_OK on successful delivery to the looper.
 */
status_t
BOutboundMailProtocol::SendMessages(const BMessage& files, off_t totalBytes)
{
	BMessage message(kMsgSendMessages);
	message.Append(files);
	message.AddInt64("bytes", totalBytes);

	return BMessenger(this).SendMessage(&message);
}


/**
 * @brief Dispatches kMsgSendMessages to HandleSendMessages().
 *
 * Unknown messages are forwarded to the base class.
 *
 * @param message  Incoming BMessage from the looper queue.
 */
void
BOutboundMailProtocol::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSendMessages:
			HandleSendMessages(*message, message->FindInt64("bytes"));
			break;

		default:
			BMailProtocol::MessageReceived(message);
	}
}
