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
 *   Copyright 2001-2010 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers (mflerackers@androme.be)
 *       Axel Dörfler, axeld@pinc-software.de
 *       Jérôme Duval
 *       René Gollent
 *       Alexandre Deckner, alex@zappotek.com
 */


/**
 * @file Shelf.cpp
 * @brief Implementation of BShelf, a replicant container view
 *
 * BShelf accepts BView replicants dropped onto it via BDragger. It archives
 * and restores replicant state, manages the replicant lifecycle, and provides
 * scripting access to hosted replicants.
 *
 * @see BDragger, BView, BArchivable
 */

#include <Shelf.h>

#include <pthread.h>

#include <AutoDeleter.h>
#include <AutoLock.h>
#include <Beep.h>
#include <Dragger.h>
#include <Entry.h>
#include <File.h>
#include <Looper.h>
#include <Message.h>
#include <MessageFilter.h>
#include <Messenger.h>
#include <Point.h>
#include <PropertyInfo.h>
#include <Rect.h>
#include <String.h>
#include <View.h>

#include <ViewPrivate.h>

#include "ZombieReplicantView.h"

#include <stdio.h>
#include <string.h>

#include <map>
#include <utility>


namespace {

/** @brief Map from add-on signature to (image_id, reference_count) pairs. */
typedef std::map<BString, std::pair<image_id, int32> > LoadedImageMap;

/**
 * @brief Process-wide singleton tracking loaded replicant add-on images.
 *
 * Maintains a reference-counted map of add-on images that have been loaded to
 * satisfy replicant instantiation. When the reference count for an image
 * reaches zero the image is unloaded via unload_add_on(). Access to the map
 * is protected by an internal BLocker.
 */
struct LoadedImages {
	/** @brief Map of loaded images keyed by add-on signature. */
	LoadedImageMap			images;

	/**
	 * @brief Constructs the LoadedImages singleton with a named lock.
	 */
	LoadedImages()
		:
		fLock("BShelf loaded image map")
	{
	}

	/**
	 * @brief Acquires the internal lock.
	 *
	 * @return true if the lock was acquired, false otherwise.
	 */
	bool Lock()
	{
		return fLock.Lock();
	}

	/**
	 * @brief Releases the internal lock.
	 */
	void Unlock()
	{
		fLock.Unlock();
	}

	/**
	 * @brief Returns the process-wide LoadedImages singleton, creating it if necessary.
	 *
	 * Uses pthread_once to guarantee thread-safe one-time initialization.
	 *
	 * @return Pointer to the singleton LoadedImages instance.
	 */
	static LoadedImages* Default()
	{
		if (sDefaultInstance == NULL)
			pthread_once(&sDefaultInitOnce, &_InitSingleton);

		return sDefaultInstance;
	}

private:
	/**
	 * @brief pthread_once callback that allocates the singleton instance.
	 */
	static void _InitSingleton()
	{
		sDefaultInstance = new LoadedImages;
	}

private:
	/** @brief Mutex protecting the images map. */
	BLocker					fLock;

	/** @brief pthread_once control variable for one-time singleton initialization. */
	static pthread_once_t	sDefaultInitOnce;

	/** @brief The singleton instance pointer, set by _InitSingleton(). */
	static LoadedImages*	sDefaultInstance;
};

/** @brief pthread_once initializer for LoadedImages::sDefaultInstance. */
pthread_once_t LoadedImages::sDefaultInitOnce = PTHREAD_ONCE_INIT;

/** @brief The process-wide LoadedImages singleton, lazily created by Default(). */
LoadedImages* LoadedImages::sDefaultInstance = NULL;

}	// unnamed namespace


/**
 * @brief Scripting property table for BShelf.
 *
 * Defines the "Replicant" property with count/create (direct specifier) and
 * delete/get (index, reverse-index, name, or ID specifier) verbs. Used by
 * ResolveSpecifier() and GetSupportedSuites().
 *
 * @see BShelf::ResolveSpecifier(), BShelf::GetSupportedSuites()
 */
static property_info sShelfPropertyList[] = {
	{
		"Replicant",
		{ B_COUNT_PROPERTIES, B_CREATE_PROPERTY },
		{ B_DIRECT_SPECIFIER },
		NULL, 0,
	},

	{
		"Replicant",
		{ B_DELETE_PROPERTY, B_GET_PROPERTY },
		{ B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, B_NAME_SPECIFIER, B_ID_SPECIFIER },
		NULL, 0,
	},

	{
		"Replicant",
		{},
		{ B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER, B_NAME_SPECIFIER, B_ID_SPECIFIER },
		"... of Replicant {index | name | id} of ...", 0,
	},

	{ 0 }
};

/**
 * @brief Scripting property table for individual replicants.
 *
 * Exposes the "ID" (int32), "Name" (string), "Signature" (string),
 * "Suites" (property info), and "View" (direct specifier) properties
 * accessible through the replicant scripting suite "suite/vnd.Be-replicant".
 *
 * @see BShelf::MessageReceived(), BShelf::ResolveSpecifier()
 */
static property_info sReplicantPropertyList[] = {
	{
		"ID",
		{ B_GET_PROPERTY },
		{ B_DIRECT_SPECIFIER },
		NULL, 0, { B_INT32_TYPE }
	},

	{
		"Name",
		{ B_GET_PROPERTY },
		{ B_DIRECT_SPECIFIER },
		NULL, 0, { B_STRING_TYPE }
	},

	{
		"Signature",
		{ B_GET_PROPERTY },
		{ B_DIRECT_SPECIFIER },
		NULL, 0, { B_STRING_TYPE }
	},

	{
		"Suites",
		{ B_GET_PROPERTY },
		{ B_DIRECT_SPECIFIER },
		NULL, 0, { B_PROPERTY_INFO_TYPE }
	},

	{
		"View",
		{ },
		{ B_DIRECT_SPECIFIER },
		NULL, 0,
	},

	{ 0 }
};


namespace BPrivate {

/**
 * @brief Internal record tracking a single replicant hosted by a BShelf.
 *
 * Each replicant_data entry owns the archived BMessage for the replicant and
 * holds (non-owning) pointers to the live BView, its BDragger, and the
 * optional zombie placeholder view. Ownership of the views is managed by
 * BShelf::_DeleteReplicant() according to the dragger relation.
 */
struct replicant_data {
	/**
	 * @brief Constructs a fully-initialised replicant record.
	 *
	 * @param message  The archived replicant BMessage (ownership transferred).
	 * @param view     The instantiated replicant view.
	 * @param dragger  The associated BDragger, or NULL.
	 * @param relation The spatial relationship between dragger and target view.
	 * @param id       The unique replicant ID assigned by the shelf.
	 */
	replicant_data(BMessage *message, BView *view, BDragger *dragger,
		BDragger::relation relation, unsigned long id);

	/**
	 * @brief Constructs a default (empty/error) replicant record.
	 */
	replicant_data();

	/**
	 * @brief Destroys the replicant record and frees the owned BMessage.
	 */
	~replicant_data();

	/**
	 * @brief Searches @a list for the record whose message pointer equals @a msg.
	 *
	 * @param list The BList of replicant_data pointers to search.
	 * @param msg  The BMessage pointer to match.
	 * @return Pointer to the matching replicant_data, or NULL if not found.
	 */
	static replicant_data* Find(BList const *list, BMessage const *msg);

	/**
	 * @brief Searches @a list for the record whose live view equals @a view.
	 *
	 * @param list        The BList of replicant_data pointers to search.
	 * @param view        The BView pointer to match.
	 * @param allowZombie If true, also match against zombie_view pointers.
	 * @return Pointer to the matching replicant_data, or NULL if not found.
	 */
	static replicant_data* Find(BList const *list, BView const *view, bool allowZombie);

	/**
	 * @brief Searches @a list for the record with the given unique @a id.
	 *
	 * @param list The BList of replicant_data pointers to search.
	 * @param id   The unique replicant ID to match.
	 * @return Pointer to the matching replicant_data, or NULL if not found.
	 */
	static replicant_data* Find(BList const *list, unsigned long id);

	/**
	 * @brief Returns the zero-based index of the record matching @a msg.
	 *
	 * @param list The BList of replicant_data pointers to search.
	 * @param msg  The BMessage pointer to match.
	 * @return The index of the matching entry, or -1 if not found.
	 */
	static int32 IndexOf(BList const *list, BMessage const *msg);

	/**
	 * @brief Returns the zero-based index of the record matching @a view.
	 *
	 * @param list        The BList of replicant_data pointers to search.
	 * @param view        The BView pointer to match.
	 * @param allowZombie If true, also match against zombie_view pointers.
	 * @return The index of the matching entry, or -1 if not found.
	 */
	static int32 IndexOf(BList const *list, BView const *view, bool allowZombie);

	/**
	 * @brief Returns the zero-based index of the record with the given @a id.
	 *
	 * @param list The BList of replicant_data pointers to search.
	 * @param id   The unique replicant ID to match.
	 * @return The index of the matching entry, or -1 if not found.
	 */
	static int32 IndexOf(BList const *list, unsigned long id);

	/**
	 * @brief Archives this replicant record into @a msg.
	 *
	 * Stores the replicant's unique ID, its position, and its archived view
	 * (or zombie view) message under the keys "uniqueid", "position", and
	 * "message" respectively.
	 *
	 * @param msg The BMessage to archive into.
	 * @return B_OK on success, or an error code if archiving the view fails.
	 */
	status_t Archive(BMessage *msg);

	/** @brief The archived BMessage for this replicant (owned). */
	BMessage*			message;

	/** @brief The live replicant view (not owned; ownership varies by relation). */
	BView*				view;

	/** @brief The associated BDragger, or NULL (not owned; varies by relation). */
	BDragger*			dragger;

	/** @brief Spatial relationship between the dragger and the target view. */
	BDragger::relation	relation;

	/** @brief Unique ID assigned by the shelf at insertion time. */
	unsigned long		id;

	/** @brief Status of the last operation on this replicant (B_OK or error). */
	status_t			error;

	/** @brief Zombie placeholder view shown when instantiation fails (not owned). */
	BView*				zombie_view;
};

/**
 * @brief Message filter installed on the container view to handle object drops.
 *
 * Intercepts B_ARCHIVED_OBJECT and B_ABOUT_REQUESTED messages delivered to
 * the shelf's container view. Dragged replicants are repositioned when the
 * source is in the same looper; external drops are forwarded to BShelf via
 * _AddReplicant().
 *
 * @see BShelf::_InitData(), ShelfContainerViewFilter::_ObjectDropFilter()
 */
class ShelfContainerViewFilter : public BMessageFilter {
	public:
		/**
		 * @brief Constructs the filter for the given shelf and container view.
		 *
		 * @param shelf The owning BShelf instance.
		 * @param view  The container view on which the filter is installed.
		 */
		ShelfContainerViewFilter(BShelf *shelf, BView *view);

		/**
		 * @brief Routes drop and about-requested messages to _ObjectDropFilter().
		 *
		 * All other messages are passed through unchanged.
		 *
		 * @param msg     The incoming BMessage.
		 * @param handler In/out pointer to the target BHandler.
		 * @return B_DISPATCH_MESSAGE or B_SKIP_MESSAGE.
		 */
		filter_result	Filter(BMessage *msg, BHandler **handler);

	private:
		/**
		 * @brief Handles a dropped or about-requested archived object.
		 *
		 * If dragging is disallowed and the message was dropped, it is skipped.
		 * For internal moves the replicant view is repositioned; for external
		 * drops _AddReplicant() is called and the message is detached from the
		 * looper.
		 *
		 * @param msg      The B_ARCHIVED_OBJECT or B_ABOUT_REQUESTED message.
		 * @param _handler In/out pointer to the target BHandler.
		 * @return B_SKIP_MESSAGE always (the message is fully consumed here).
		 */
		filter_result	_ObjectDropFilter(BMessage *msg, BHandler **_handler);

		/** @brief The owning BShelf. */
		BShelf	*fShelf;

		/** @brief The container view this filter is attached to. */
		BView	*fView;
};

/**
 * @brief Message filter installed on each replicant view to intercept delete requests.
 *
 * When a kDeleteReplicant message arrives it redirects the handler to the
 * shelf and attaches a "_target" pointer so BShelf::MessageReceived() can
 * identify which view to remove.
 *
 * @see BShelf::_GetReplicant(), BShelf::MessageReceived()
 */
class ReplicantViewFilter : public BMessageFilter {
	public:
		/**
		 * @brief Constructs the filter for the given shelf and replicant view.
		 *
		 * @param shelf The owning BShelf instance.
		 * @param view  The replicant view this filter is attached to.
		 */
		ReplicantViewFilter(BShelf *shelf, BView *view);

		/**
		 * @brief Redirects kDeleteReplicant messages to the shelf handler.
		 *
		 * Attaches the replicant's BView pointer as "_target" and redirects
		 * the handler to the shelf, leaving all other messages unchanged.
		 *
		 * @param message The incoming BMessage.
		 * @param handler In/out pointer to the target BHandler.
		 * @return B_DISPATCH_MESSAGE always.
		 */
		filter_result Filter(BMessage *message, BHandler **handler);

	private:
		/** @brief The owning BShelf. */
		BShelf	*fShelf;

		/** @brief The replicant view this filter guards. */
		BView	*fView;
};

}	// namespace BPrivate


using BPrivate::replicant_data;
using BPrivate::ReplicantViewFilter;
using BPrivate::ShelfContainerViewFilter;


//	#pragma mark -


/**
 * @brief Sends a B_REPLY to the source of a replicant add request if it is waiting.
 *
 * Constructs a reply containing the assigned unique ID and the operation status
 * code, then sends it back via BMessage::SendReply() if the source is blocking.
 *
 * @param message  The original request BMessage whose source may be waiting.
 * @param status   The result code to embed in the reply ("error" field).
 * @param uniqueID The unique ID assigned to the new replicant ("id" field).
 * @return The @a status value passed in, for use in a return statement.
 *
 * @see BShelf::_AddReplicant()
 */
static status_t
send_reply(BMessage* message, status_t status, uint32 uniqueID)
{
	if (message->IsSourceWaiting()) {
		BMessage reply(B_REPLY);
		reply.AddInt32("id", uniqueID);
		reply.AddInt32("error", status);
		message->SendReply(&reply);
	}

	return status;
}


/**
 * @brief Returns whether a replicant of the given class and add-on is already hosted.
 *
 * Iterates the shelf's replicant list and checks each entry's archived "class"
 * and "add_on" fields against the supplied strings. Used to enforce the
 * "be:load_each_time" uniqueness constraint.
 *
 * @param list      The BList of replicant_data entries to search.
 * @param className The replicant class name to match.
 * @param addOn     The add-on signature to match.
 * @return true if a matching replicant already exists, false otherwise.
 *
 * @see BShelf::_AddReplicant()
 */
static bool
find_replicant(BList &list, const char *className, const char *addOn)
{
	int32 i = 0;
	replicant_data *item;
	while ((item = (replicant_data *)list.ItemAt(i++)) != NULL) {
		const char *replicantClassName;
		const char *replicantAddOn;
		if (item->message->FindString("class", &replicantClassName) == B_OK
			&& item->message->FindString("add_on", &replicantAddOn) == B_OK
			&& !strcmp(className, replicantClassName)
			&& addOn != NULL && replicantAddOn != NULL
			&& !strcmp(addOn, replicantAddOn))
		return true;
	}
	return false;
}


//	#pragma mark -


/**
 * @brief Constructs a fully-initialised replicant record.
 *
 * The dragger field is intentionally left NULL even though a dragger pointer
 * is accepted; BShelf::_GetReplicant() sets it after determining the
 * dragger-target relationship.
 *
 * @param _message  The archived replicant BMessage (ownership transferred).
 * @param _view     The instantiated replicant view.
 * @param _dragger  Unused at construction time; stored as NULL.
 * @param _relation The spatial relationship between dragger and target view.
 * @param _id       The unique replicant ID assigned by the shelf.
 */
replicant_data::replicant_data(BMessage *_message, BView *_view, BDragger *_dragger,
	BDragger::relation _relation, unsigned long _id)
	:
	message(_message),
	view(_view),
	dragger(NULL),
	relation(_relation),
	id(_id),
	error(B_OK),
	zombie_view(NULL)
{
}


/**
 * @brief Constructs a default (empty/error) replicant record.
 *
 * All pointers are NULL and the error field is initialised to B_ERROR.
 * Used as a sentinel or placeholder before a valid record is populated.
 */
replicant_data::replicant_data()
	:
	message(NULL),
	view(NULL),
	dragger(NULL),
	relation(BDragger::TARGET_UNKNOWN),
	id(0),
	error(B_ERROR),
	zombie_view(NULL)
{
}

/**
 * @brief Destroys the replicant record, freeing the owned archived BMessage.
 *
 * @note The live view and dragger are NOT deleted here; their lifetimes are
 *       managed by BShelf::_DeleteReplicant() based on the dragger relation.
 */
replicant_data::~replicant_data()
{
	delete message;
}

/**
 * @brief Archives this replicant record into @a msg.
 *
 * Archives the live view (or zombie view if no live view exists) and stores
 * the result under the "message" key. Also stores the replicant's unique ID
 * under "uniqueid" and its top-left position under "position".
 *
 * @param msg The BMessage to archive into.
 * @return B_OK on success, or an error code if archiving the view fails.
 */
status_t
replicant_data::Archive(BMessage* msg)
{
	status_t result = B_OK;
	BMessage archive;
	if (view)
		result = view->Archive(&archive);
	else if (zombie_view)
		result = zombie_view->Archive(&archive);

	if (result != B_OK)
		return result;

	msg->AddInt32("uniqueid", id);
	BPoint pos (0,0);
	msg->AddMessage("message", &archive);
	if (view)
		pos = view->Frame().LeftTop();
	else if (zombie_view)
		pos = zombie_view->Frame().LeftTop();
	msg->AddPoint("position", pos);

	return result;
}

/**
 * @brief Searches @a list for the record whose message pointer equals @a msg.
 *
 * @param list The BList of replicant_data pointers to search.
 * @param msg  The BMessage pointer to match.
 * @return Pointer to the matching replicant_data, or NULL if not found.
 */
//static
replicant_data *
replicant_data::Find(BList const *list, BMessage const *msg)
{
	int32 i = 0;
	replicant_data *item;
	while ((item = (replicant_data*)list->ItemAt(i++)) != NULL) {
		if (item->message == msg)
			return item;
	}

	return NULL;
}


/**
 * @brief Searches @a list for the record whose live or zombie view equals @a view.
 *
 * @param list        The BList of replicant_data pointers to search.
 * @param view        The BView pointer to match.
 * @param allowZombie If true, also test each entry's zombie_view pointer.
 * @return Pointer to the matching replicant_data, or NULL if not found.
 */
//static
replicant_data *
replicant_data::Find(BList const *list, BView const *view, bool allowZombie)
{
	int32 i = 0;
	replicant_data *item;
	while ((item = (replicant_data*)list->ItemAt(i++)) != NULL) {
		if (item->view == view)
			return item;

		if (allowZombie && item->zombie_view == view)
			return item;
	}

	return NULL;
}


/**
 * @brief Searches @a list for the record whose unique @a id matches.
 *
 * @param list The BList of replicant_data pointers to search.
 * @param id   The unique replicant ID to match.
 * @return Pointer to the matching replicant_data, or NULL if not found.
 */
//static
replicant_data *
replicant_data::Find(BList const *list, unsigned long id)
{
	int32 i = 0;
	replicant_data *item;
	while ((item = (replicant_data*)list->ItemAt(i++)) != NULL) {
		if (item->id == id)
			return item;
	}

	return NULL;
}


/**
 * @brief Returns the zero-based index of the record whose message equals @a msg.
 *
 * @param list The BList of replicant_data pointers to search.
 * @param msg  The BMessage pointer to match.
 * @return The index of the matching entry, or -1 if not found.
 */
//static
int32
replicant_data::IndexOf(BList const *list, BMessage const *msg)
{
	int32 i = 0;
	replicant_data *item;
	while ((item = (replicant_data*)list->ItemAt(i)) != NULL) {
		if (item->message == msg)
			return i;
		i++;
	}

	return -1;
}


/**
 * @brief Returns the zero-based index of the record whose view matches @a view.
 *
 * @param list        The BList of replicant_data pointers to search.
 * @param view        The BView pointer to match.
 * @param allowZombie If true, also test each entry's zombie_view pointer.
 * @return The index of the matching entry, or -1 if not found.
 */
//static
int32
replicant_data::IndexOf(BList const *list, BView const *view, bool allowZombie)
{
	int32 i = 0;
	replicant_data *item;
	while ((item = (replicant_data*)list->ItemAt(i)) != NULL) {
		if (item->view == view)
			return i;

		if (allowZombie && item->zombie_view == view)
			return i;
		i++;
	}

	return -1;
}


/**
 * @brief Returns the zero-based index of the record with the given unique @a id.
 *
 * @param list The BList of replicant_data pointers to search.
 * @param id   The unique replicant ID to match.
 * @return The index of the matching entry, or -1 if not found.
 */
//static
int32
replicant_data::IndexOf(BList const *list, unsigned long id)
{
	int32 i = 0;
	replicant_data *item;
	while ((item = (replicant_data*)list->ItemAt(i)) != NULL) {
		if (item->id == id)
			return i;
		i++;
	}

	return -1;
}


//	#pragma mark -


/**
 * @brief Constructs the ShelfContainerViewFilter for the given shelf and container view.
 *
 * Registers the filter to intercept messages from any delivery source.
 *
 * @param shelf The owning BShelf instance.
 * @param view  The container view on which this filter will be installed.
 */
ShelfContainerViewFilter::ShelfContainerViewFilter(BShelf *shelf, BView *view)
	: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
	fShelf(shelf),
	fView(view)
{
}


/**
 * @brief Routes B_ARCHIVED_OBJECT and B_ABOUT_REQUESTED messages to _ObjectDropFilter().
 *
 * All other messages are allowed to pass through with B_DISPATCH_MESSAGE.
 *
 * @param msg     The incoming BMessage.
 * @param handler In/out pointer to the target BHandler.
 * @return B_DISPATCH_MESSAGE for unhandled messages; the return value of
 *         _ObjectDropFilter() for object-drop and about-requested messages.
 */
filter_result
ShelfContainerViewFilter::Filter(BMessage *msg, BHandler **handler)
{
	filter_result filter = B_DISPATCH_MESSAGE;

	if (msg->what == B_ARCHIVED_OBJECT
		|| msg->what == B_ABOUT_REQUESTED)
		return _ObjectDropFilter(msg, handler);

	return filter;
}


/**
 * @brief Handles a dropped or about-requested archived object message.
 *
 * If the shelf does not allow dragging and the message was dropped, the
 * message is skipped. For internal moves (same looper), the replicant's
 * position is updated. For external drops, _AddReplicant() is called and
 * the message is detached from the looper.
 *
 * @param msg      The B_ARCHIVED_OBJECT or B_ABOUT_REQUESTED message.
 * @param _handler In/out pointer to the target BHandler; used to obtain the
 *                 mouse view for coordinate conversion.
 * @return B_SKIP_MESSAGE always; the message is fully consumed by this handler.
 */
filter_result
ShelfContainerViewFilter::_ObjectDropFilter(BMessage *msg, BHandler **_handler)
{
	BView *mouseView = NULL;
	if (*_handler)
		mouseView = dynamic_cast<BView*>(*_handler);

	if (msg->WasDropped()) {
		if (!fShelf->fAllowDragging)
			return B_SKIP_MESSAGE;
	}

	BPoint point;
	BPoint offset;

	if (msg->WasDropped()) {
		point = msg->DropPoint(&offset);
		point = mouseView->ConvertFromScreen(point - offset);
	}

	BLooper *looper = NULL;
	BHandler *handler = msg->ReturnAddress().Target(&looper);

	if (Looper() == looper) {
		BDragger *dragger = NULL;
		if (handler)
			dragger = dynamic_cast<BDragger*>(handler);
		else
			return B_SKIP_MESSAGE;

		BRect rect;
		if (dragger->fRelation == BDragger::TARGET_IS_CHILD)
			rect = dragger->Frame();
		else
			rect = dragger->fTarget->Frame();
		rect.OffsetTo(point);
		point = rect.LeftTop() + fShelf->AdjustReplicantBy(rect, msg);

		if (dragger->fRelation == BDragger::TARGET_IS_PARENT)
			dragger->fTarget->MoveTo(point);
		else if (dragger->fRelation == BDragger::TARGET_IS_CHILD)
			dragger->MoveTo(point);
		else {
			//TODO: TARGET_UNKNOWN/TARGET_SIBLING
		}

	} else {
		if (fShelf->_AddReplicant(msg, &point, fShelf->fGenCount++) == B_OK)
			Looper()->DetachCurrentMessage();
	}

	return B_SKIP_MESSAGE;
}


//	#pragma mark -


/**
 * @brief Constructs the ReplicantViewFilter for the given shelf and replicant view.
 *
 * Registers the filter to intercept messages from any delivery source so that
 * kDeleteReplicant requests can be redirected to the owning shelf.
 *
 * @param shelf The owning BShelf instance.
 * @param view  The replicant (or zombie) view this filter is attached to.
 */
ReplicantViewFilter::ReplicantViewFilter(BShelf *shelf, BView *view)
	: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
	fShelf(shelf),
	fView(view)
{
}


/**
 * @brief Redirects kDeleteReplicant messages to the owning shelf handler.
 *
 * When a kDeleteReplicant message arrives the handler is redirected to the
 * shelf and the view's pointer is attached as "_target" so that
 * BShelf::MessageReceived() can look up the correct replicant_data entry.
 * All other messages are dispatched normally.
 *
 * @param message The incoming BMessage.
 * @param handler In/out pointer to the target BHandler; set to fShelf for
 *                kDeleteReplicant messages.
 * @return B_DISPATCH_MESSAGE always.
 */
filter_result
ReplicantViewFilter::Filter(BMessage *message, BHandler **handler)
{
	if (message->what == kDeleteReplicant) {
		if (handler != NULL)
			*handler = fShelf;
		message->AddPointer("_target", fView);
	}
	return B_DISPATCH_MESSAGE;
}


//	#pragma mark -


/**
 * @brief Constructs a BShelf attached to the given container view.
 *
 * No persistent state is loaded or saved. Use SetSaveLocation() afterward
 * if persistence is required.
 *
 * @param view       The BView that will act as the replicant container.
 * @param allowDrags If true, replicants may be re-dragged within the shelf.
 * @param shelfType  An optional type string used to filter incoming replicants
 *                   when type enforcement is enabled. NULL for no filtering.
 *
 * @see SetSaveLocation(), SetTypeEnforced()
 */
BShelf::BShelf(BView *view, bool allowDrags, const char *shelfType)
	: BHandler(shelfType)
{
	_InitData(NULL, NULL, view, allowDrags);
}


/**
 * @brief Constructs a BShelf that saves and restores state from an entry_ref file.
 *
 * The existing replicant state is read from the file identified by @a ref at
 * construction time. When Save() is called (or the shelf is destroyed) the
 * state is written back to the same file.
 *
 * @param ref        An entry_ref identifying the file used for persistent storage.
 * @param view       The BView that will act as the replicant container.
 * @param allowDrags If true, replicants may be re-dragged within the shelf.
 * @param shelfType  Optional type string for replicant filtering.
 *
 * @see Save(), SetSaveLocation(const entry_ref*)
 */
BShelf::BShelf(const entry_ref *ref, BView *view, bool allowDrags,
	const char *shelfType)
	: BHandler(shelfType)
{
	_InitData(new BEntry(ref), NULL, view, allowDrags);
}


/**
 * @brief Constructs a BShelf that reads initial state from a BDataIO stream.
 *
 * The replicant state is read from @a stream at construction time. The stream
 * is not written back automatically; call Save() or SetSaveLocation() to
 * configure a save destination.
 *
 * @param stream     The BDataIO stream to read archived replicant state from.
 * @param view       The BView that will act as the replicant container.
 * @param allowDrags If true, replicants may be re-dragged within the shelf.
 * @param shelfType  Optional type string for replicant filtering.
 *
 * @see Save(), SetSaveLocation(BDataIO*)
 */
BShelf::BShelf(BDataIO *stream, BView *view, bool allowDrags,
	const char *shelfType)
	: BHandler(shelfType)
{
	_InitData(NULL, stream, view, allowDrags);
}


/**
 * @brief Constructs a BShelf from an archived BMessage.
 *
 * @param data The BMessage produced by a prior Archive() call.
 *
 * @note This constructor is currently unimplemented.
 * @see Archive(), Instantiate()
 */
BShelf::BShelf(BMessage *data)
	: BHandler(data)
{
	// TODO: Implement ?
}


/**
 * @brief Destroys the BShelf, saving state and cleaning up all replicants.
 *
 * Calls Save() to persist the current replicant state if a save location was
 * configured. Deletes the BEntry and BFile objects if they are owned by the
 * shelf (i.e. the entry_ref constructor was used). Destroys all replicant_data
 * records and detaches the shelf from the container view.
 *
 * @note The container view's filter and shelf back-pointer are both cleared
 *       during destruction. The view itself is not destroyed.
 * @see Save(), _InitData()
 */
BShelf::~BShelf()
{
	Save();

	// we own fStream only when fEntry is set
	if (fEntry != NULL) {
		delete fEntry;
		delete fStream;
	}

	while (fReplicants.CountItems() > 0) {
		replicant_data *data = (replicant_data *)fReplicants.ItemAt(0);
		fReplicants.RemoveItem((int32)0);
		delete data;
	}

	fContainerView->_SetShelf(NULL);
}


/**
 * @brief Archives the BShelf into a BMessage (stub — not yet implemented).
 *
 * @param data Unused.
 * @param deep Unused.
 * @return B_ERROR always; archiving via the BArchivable interface is not
 *         currently supported. Use Save() / SetSaveLocation() instead.
 *
 * @see Save(), _Archive()
 */
status_t
BShelf::Archive(BMessage *data, bool deep) const
{
	return B_ERROR;
}


/**
 * @brief Instantiates a BShelf from an archived BMessage (stub — not yet implemented).
 *
 * @param data Unused.
 * @return NULL always.
 *
 * @see Archive()
 */
BArchivable *
BShelf::Instantiate(BMessage *data)
{
	return NULL;
}


/**
 * @brief Handles incoming scripting and lifecycle messages for the shelf.
 *
 * Processes the following message types:
 * - kDeleteReplicant: looks up the target view by "_target" pointer and calls
 *   DeleteReplicant().
 * - B_DELETE_PROPERTY / B_GET_PROPERTY / B_GET_SUPPORTED_SUITES on "Replicant":
 *   resolves the replicant specifier, then deletes, retrieves, or returns suite
 *   information as appropriate.
 * - B_COUNT_PROPERTIES on "Replicant": replies with CountReplicants().
 * - B_CREATE_PROPERTY: reads "data" (BMessage) and "location" (BPoint) from
 *   the message and calls AddReplicant().
 *
 * Unrecognised messages are forwarded to BHandler::MessageReceived().
 *
 * @param msg The BMessage to process.
 *
 * @see DeleteReplicant(), AddReplicant(), CountReplicants(), ReplicantAt()
 */
void
BShelf::MessageReceived(BMessage *msg)
{
	if (msg->what == kDeleteReplicant) {
		BHandler *replicant = NULL;
		if (msg->FindPointer("_target", (void **)&replicant) == B_OK) {
			BView *view = dynamic_cast<BView *>(replicant);
			if (view != NULL)
				DeleteReplicant(view);
		}
		return;
	}

	BMessage replyMsg(B_REPLY);
	status_t err = B_BAD_SCRIPT_SYNTAX;

	BMessage specifier;
	int32 what;
	const char *prop;
	int32 index;
	if (msg->GetCurrentSpecifier(&index, &specifier, &what, &prop) != B_OK)
		return BHandler::MessageReceived(msg);

	switch (msg->what) {
		case B_DELETE_PROPERTY:
		case B_GET_PROPERTY:
		case B_GET_SUPPORTED_SUITES:
			if (strcmp(prop, "Replicant") == 0) {
				BMessage reply;
				int32 i;
				uint32 ID;
				BView *replicant = NULL;
				BMessage *repMessage = NULL;
				err = _GetProperty(&specifier, &reply);
				if (err == B_OK)
					err = reply.FindInt32("index", &i);

				if (err == B_OK && msg->what == B_DELETE_PROPERTY) { // Delete Replicant
					err = DeleteReplicant(i);
					break;
				}
				if (err == B_OK && msg->what == B_GET_SUPPORTED_SUITES) {
					err = replyMsg.AddString("suites", "suite/vnd.Be-replicant");
					if (err == B_OK) {
						BPropertyInfo propInfo(sReplicantPropertyList);
						err = replyMsg.AddFlat("messages", &propInfo);
					}
					break;
				}
				if (err == B_OK )
					repMessage = ReplicantAt(i, &replicant, &ID, &err);
				if (err == B_OK && replicant) {
					msg->PopSpecifier();
					BMessage archive;
					err = replicant->Archive(&archive);
					if (err == B_OK && msg->GetCurrentSpecifier(&index, &specifier, &what, &prop) != B_OK) {
						err = replyMsg.AddMessage("result", &archive);
						break;
					}
					// now handles the replicant suite
					err = B_BAD_SCRIPT_SYNTAX;
					if (msg->what != B_GET_PROPERTY)
						break;
					if (strcmp(prop, "ID") == 0) {
						err = replyMsg.AddInt32("result", ID);
					} else if (strcmp(prop, "Name") == 0) {
						err = replyMsg.AddString("result", replicant->Name());
					} else if (strcmp(prop, "Signature") == 0) {
						const char *add_on = NULL;
						err = repMessage->FindString("add_on", &add_on);
						if (err == B_OK)
							err = replyMsg.AddString("result", add_on);
					} else if (strcmp(prop, "Suites") == 0) {
						err = replyMsg.AddString("suites", "suite/vnd.Be-replicant");
						if (err == B_OK) {
							BPropertyInfo propInfo(sReplicantPropertyList);
							err = replyMsg.AddFlat("messages", &propInfo);
						}
					}
				}
				break;
			}
			return BHandler::MessageReceived(msg);

		case B_COUNT_PROPERTIES:
			if (strcmp(prop, "Replicant") == 0) {
				err = replyMsg.AddInt32("result", CountReplicants());
				break;
			}
			return BHandler::MessageReceived(msg);

		case B_CREATE_PROPERTY:
		{
			BMessage replicantMsg;
			BPoint pos;
			if (msg->FindMessage("data", &replicantMsg) == B_OK
				&& msg->FindPoint("location", &pos) == B_OK) {
					err = AddReplicant(&replicantMsg, pos);
			}
		}
		break;
	}

	if (err < B_OK) {
		replyMsg.what = B_MESSAGE_NOT_UNDERSTOOD;

		if (err == B_BAD_SCRIPT_SYNTAX)
			replyMsg.AddString("message", "Didn't understand the specifier(s)");
		else
			replyMsg.AddString("message", strerror(err));
	}

	replyMsg.AddInt32("error", err);
	msg->SendReply(&replyMsg);
}


/**
 * @brief Persists the current replicant state to the configured save location.
 *
 * If a save location was set via SetSaveLocation(const entry_ref*), a new
 * BFile is opened (erasing any previous content) and the shelf state is
 * flattened into it. If a stream was provided via SetSaveLocation(BDataIO*)
 * that stream is used directly. If no save location is configured, B_ERROR
 * is returned without writing anything.
 *
 * @return B_OK on success; B_ERROR if no save location is set; or a file
 *         system / message error code on failure.
 *
 * @see SetSaveLocation(), _Archive()
 */
status_t
BShelf::Save()
{
	status_t status = B_ERROR;
	if (fEntry != NULL) {
		BFile *file = new BFile(fEntry, B_READ_WRITE | B_ERASE_FILE);
		status = file->InitCheck();
		if (status != B_OK) {
			delete file;
			return status;
		}
		delete fStream;
		fStream = file;
	}

	if (fStream != NULL) {
		BMessage message;
		status = _Archive(&message);
		if (status == B_OK)
			status = message.Flatten(fStream);
	}

	return status;
}


/**
 * @brief Sets the dirty flag, indicating whether unsaved changes exist.
 *
 * @param state true to mark the shelf as having unsaved changes, false to
 *              clear the dirty state after a successful save.
 *
 * @see IsDirty(), Save()
 */
void
BShelf::SetDirty(bool state)
{
	fDirty = state;
}


/**
 * @brief Returns whether the shelf has unsaved changes.
 *
 * @return true if the shelf state has been modified since the last Save(),
 *         false otherwise.
 *
 * @see SetDirty(), Save()
 */
bool
BShelf::IsDirty() const
{
	return fDirty;
}


/**
 * @brief Resolves a scripting specifier for the shelf or one of its replicants.
 *
 * Matches "Replicant" property specifiers against the shelf property table and
 * the replicant property table. For replicant sub-properties (ID, Name,
 * Signature, Suites) the shelf itself is returned as the handler; for the
 * "View" sub-property the replicant view is returned directly.
 *
 * @param msg      The scripting BMessage containing the specifier chain.
 * @param index    Index of the current specifier within the chain.
 * @param specifier The current specifier BMessage.
 * @param form     The specifier form (B_INDEX_SPECIFIER, B_NAME_SPECIFIER, etc.).
 * @param property The property name string.
 * @return The BHandler that should handle the message, or NULL on error.
 *
 * @see MessageReceived(), GetSupportedSuites()
 */
BHandler *
BShelf::ResolveSpecifier(BMessage *msg, int32 index, BMessage *specifier,
						int32 form, const char *property)
{
	BPropertyInfo shelfPropInfo(sShelfPropertyList);
	BHandler *target = NULL;
	BView *replicant = NULL;

	switch (shelfPropInfo.FindMatch(msg, 0, specifier, form, property)) {
		case 0:
			target = this;
			break;
		case 1:
			if (msg->PopSpecifier() != B_OK) {
				target = this;
				break;
			}
			msg->SetCurrentSpecifier(index);
			// fall through
		case 2: {
			BMessage reply;
			status_t err = _GetProperty(specifier, &reply);
			int32 i;
			uint32 ID;
			if (err == B_OK)
				err = reply.FindInt32("index", &i);
			if (err == B_OK)
				ReplicantAt(i, &replicant, &ID, &err);

			if (err == B_OK && replicant != NULL) {
				if (index == 0)
					return this;
			} else {
				BMessage replyMsg(B_MESSAGE_NOT_UNDERSTOOD);
				replyMsg.AddInt32("error", B_BAD_INDEX);
				replyMsg.AddString("message", "Cannot find replicant at/with specified index/name.");
				msg->SendReply(&replyMsg);
			}
			}
			msg->PopSpecifier();
			break;
	}

	if (!replicant) {
		if (target)
			return target;
		return BHandler::ResolveSpecifier(msg, index, specifier, form,
			property);
	}

	int32 repIndex;
	status_t err = msg->GetCurrentSpecifier(&repIndex, specifier, &form, &property);
	if (err) {
		BMessage reply(B_MESSAGE_NOT_UNDERSTOOD);
		reply.AddInt32("error", err);
		msg->SendReply(&reply);
		return NULL;
	}

	BPropertyInfo replicantPropInfo(sReplicantPropertyList);
	switch (replicantPropInfo.FindMatch(msg, 0, specifier, form, property)) {
		case 0:
		case 1:
		case 2:
		case 3:
			msg->SetCurrentSpecifier(index);
			target = this;
			break;
		case 4:
			target = replicant;
			msg->PopSpecifier();
			break;
		default:
			break;
	}
	if (!target) {
		BMessage replyMsg(B_MESSAGE_NOT_UNDERSTOOD);
		replyMsg.AddInt32("error", B_BAD_SCRIPT_SYNTAX);
		replyMsg.AddString("message", "Didn't understand the specifier(s)");
		msg->SendReply(&replyMsg);
	}
	return target;
}


/**
 * @brief Advertises the scripting suites supported by BShelf.
 *
 * Adds "suite/vnd.Be-shelf" and the associated property info to @a message,
 * then chains to BHandler::GetSupportedSuites().
 *
 * @param message The BMessage to populate with suite and property info.
 * @return B_OK on success, or an error code if adding data to the message fails.
 *
 * @see ResolveSpecifier(), MessageReceived()
 */
status_t
BShelf::GetSupportedSuites(BMessage *message)
{
	status_t err;
	err = message->AddString("suites", "suite/vnd.Be-shelf");
	if (err == B_OK) {
		BPropertyInfo propInfo(sShelfPropertyList);
		err = message->AddFlat("messages", &propInfo);
	}
	if (err == B_OK)
		return BHandler::GetSupportedSuites(message);
	return err;
}


/**
 * @brief Dispatches a private perform request to the base BHandler.
 *
 * This hook exists for binary compatibility. Application code should not
 * call this method directly.
 *
 * @param d   The perform code identifying the requested operation.
 * @param arg Opaque argument whose meaning depends on @a d.
 * @return The result of BHandler::Perform().
 */
status_t
BShelf::Perform(perform_code d, void *arg)
{
	return BHandler::Perform(d, arg);
}


/**
 * @brief Returns whether replicants may be re-dragged within this shelf.
 *
 * @return true if drag-and-drop repositioning is allowed, false if it is
 *         suppressed.
 *
 * @see SetAllowsDragging()
 */
bool
BShelf::AllowsDragging() const
{
	return fAllowDragging;
}


/**
 * @brief Enables or disables drag-and-drop repositioning of replicants.
 *
 * When disabled, any dropped message whose source is outside the current
 * looper will be accepted, but internal moves via dragging are blocked by
 * ShelfContainerViewFilter.
 *
 * @param state true to allow dragging, false to suppress it.
 *
 * @see AllowsDragging()
 */
void
BShelf::SetAllowsDragging(bool state)
{
	fAllowDragging = state;
}


/**
 * @brief Returns whether zombie (failed) replicants are tolerated.
 *
 * When false, any replicant that fails to instantiate is silently rejected
 * rather than stored as a zombie entry.
 *
 * @return true if zombie replicants are permitted, false otherwise.
 *
 * @see SetAllowsZombies(), DisplaysZombies()
 */
bool
BShelf::AllowsZombies() const
{
	return fAllowZombies;
}


/**
 * @brief Controls whether failed replicant instantiations are kept as zombies.
 *
 * @param state true to allow zombie entries, false to silently reject
 *              replicants that fail to instantiate.
 *
 * @see AllowsZombies(), SetDisplaysZombies()
 */
void
BShelf::SetAllowsZombies(bool state)
{
	fAllowZombies = state;
}


/**
 * @brief Returns whether zombie replicants are shown as placeholder views.
 *
 * When true and a replicant fails to instantiate, a _BZombieReplicantView_
 * is created and displayed in place of the real replicant view.
 *
 * @return true if zombie placeholder views are shown, false otherwise.
 *
 * @see SetDisplaysZombies(), AllowsZombies()
 */
bool
BShelf::DisplaysZombies() const
{
	return fDisplayZombies;
}


/**
 * @brief Controls whether failed replicants are shown as zombie placeholder views.
 *
 * Has no effect unless AllowsZombies() is also true.
 *
 * @param state true to display zombie placeholder views, false to hide them.
 *
 * @see DisplaysZombies(), SetAllowsZombies()
 */
void
BShelf::SetDisplaysZombies(bool state)
{
	fDisplayZombies = state;
}


/**
 * @brief Returns whether type enforcement is active for this shelf.
 *
 * When active, only replicants whose "shelf_type" field matches the shelf's
 * name are accepted.
 *
 * @return true if type enforcement is enabled, false otherwise.
 *
 * @see SetTypeEnforced(), BShelf::_AddReplicant()
 */
bool
BShelf::IsTypeEnforced() const
{
	return fTypeEnforced;
}


/**
 * @brief Enables or disables type-based filtering of incoming replicants.
 *
 * When enabled, BShelf::_AddReplicant() rejects any replicant whose
 * "shelf_type" field does not match the shelf's name string.
 *
 * @param state true to enforce type matching, false to accept all replicants.
 *
 * @see IsTypeEnforced()
 */
void
BShelf::SetTypeEnforced(bool state)
{
	fTypeEnforced = state;
}


/**
 * @brief Sets a BDataIO stream as the save destination for shelf state.
 *
 * Clears any previously set entry_ref save location and marks the shelf dirty.
 * The shelf does not take ownership of @a data_io.
 *
 * @param data_io The stream to write shelf state into when Save() is called.
 * @return B_OK always.
 *
 * @see Save(), SaveLocation(), SetSaveLocation(const entry_ref*)
 */
status_t
BShelf::SetSaveLocation(BDataIO *data_io)
{
	fDirty = true;

	if (fEntry != NULL) {
		delete fEntry;
		fEntry = NULL;
	}

	fStream = data_io;

	return B_OK;
}


/**
 * @brief Sets a file identified by an entry_ref as the save destination.
 *
 * Creates a new BEntry from @a ref and marks the shelf dirty. If a plain
 * BDataIO stream was previously set (without an entry_ref), it is cleared.
 *
 * @param ref The entry_ref identifying the file to use for persistent storage.
 * @return B_OK always.
 *
 * @see Save(), SaveLocation(), SetSaveLocation(BDataIO*)
 */
status_t
BShelf::SetSaveLocation(const entry_ref *ref)
{
	fDirty = true;

	if (fEntry)
		delete fEntry;
	else
		fStream = NULL;

	fEntry = new BEntry(ref);

	return B_OK;
}


/**
 * @brief Returns the current save destination and optionally its entry_ref.
 *
 * If a BDataIO stream was set, that stream is returned and @a ref (if
 * non-NULL) is set to a default-constructed entry_ref. If an entry_ref file
 * was configured, the ref is populated via BEntry::GetRef() and NULL is
 * returned for the stream.
 *
 * @param ref If non-NULL, receives the entry_ref of the save file, or a
 *            default entry_ref if a plain stream is configured.
 * @return The BDataIO stream if one is set, NULL otherwise.
 *
 * @see SetSaveLocation(), Save()
 */
BDataIO *
BShelf::SaveLocation(entry_ref *ref) const
{
	if (fStream) {
		if (ref)
			*ref = entry_ref();
		return fStream;
	} else if (fEntry && ref)
		fEntry->GetRef(ref);

	return NULL;
}


/**
 * @brief Adds a replicant from an archived BMessage at the given location.
 *
 * Delegates to _AddReplicant() with an auto-incremented unique ID. On success
 * the shelf takes ownership of @a data.
 *
 * @param data     The archived replicant BMessage. Ownership is transferred on
 *                 success only.
 * @param location The desired top-left position for the replicant in container
 *                 view coordinates.
 * @return B_OK on success, or an error code if the replicant was rejected.
 *
 * @see _AddReplicant(), DeleteReplicant(), CanAcceptReplicantMessage()
 */
status_t
BShelf::AddReplicant(BMessage *data, BPoint location)
{
	return _AddReplicant(data, &location, fGenCount++);
}


/**
 * @brief Removes the replicant whose live or zombie view is @a replicant.
 *
 * Looks up the replicant_data entry by view pointer (including zombie views)
 * and calls _DeleteReplicant() to tear it down and fire ReplicantDeleted().
 *
 * @param replicant The BView of the replicant to remove.
 * @return B_OK on success, B_BAD_VALUE if the view is not hosted by this shelf.
 *
 * @see _DeleteReplicant(), ReplicantDeleted()
 */
status_t
BShelf::DeleteReplicant(BView *replicant)
{
	int32 index = replicant_data::IndexOf(&fReplicants, replicant, true);

	replicant_data *item = (replicant_data*)fReplicants.ItemAt(index);
	if (item == NULL)
		return B_BAD_VALUE;

	return _DeleteReplicant(item);
}


/**
 * @brief Removes the replicant identified by its archived BMessage.
 *
 * @param data The BMessage pointer previously returned by ReplicantAt().
 * @return B_OK on success, B_BAD_VALUE if the message is not found.
 *
 * @see _DeleteReplicant(), ReplicantAt()
 */
status_t
BShelf::DeleteReplicant(BMessage *data)
{
	int32 index = replicant_data::IndexOf(&fReplicants, data);

	replicant_data *item = (replicant_data*)fReplicants.ItemAt(index);
	if (!item)
		return B_BAD_VALUE;

	return _DeleteReplicant(item);
}


/**
 * @brief Removes the replicant at the given zero-based index.
 *
 * @param index Zero-based position of the replicant in the hosted list.
 * @return B_OK on success, B_BAD_INDEX if the index is out of range.
 *
 * @see _DeleteReplicant(), CountReplicants()
 */
status_t
BShelf::DeleteReplicant(int32 index)
{
	replicant_data *item = (replicant_data*)fReplicants.ItemAt(index);
	if (!item)
		return B_BAD_INDEX;

	return _DeleteReplicant(item);
}


/**
 * @brief Returns the number of replicants currently hosted by this shelf.
 *
 * Includes both live replicants and zombie placeholder entries.
 *
 * @return The total count of hosted replicant entries.
 *
 * @see ReplicantAt(), IndexOf()
 */
int32
BShelf::CountReplicants() const
{
	return fReplicants.CountItems();
}


/**
 * @brief Returns the archived BMessage for the replicant at @a index.
 *
 * Optionally provides the live BView, unique ID, and instantiation status for
 * the replicant. If no replicant exists at @a index the out-parameters are
 * set to sentinel values and NULL is returned.
 *
 * @param index     Zero-based index of the replicant.
 * @param _view     If non-NULL, receives the live BView (may be NULL for zombies).
 * @param _uniqueID If non-NULL, receives the unique ID (set to ~0u on failure).
 * @param _error    If non-NULL, receives the replicant's instantiation status.
 * @return The archived BMessage for the replicant, or NULL if out of range.
 *
 * @see CountReplicants(), IndexOf(), AddReplicant()
 */
BMessage *
BShelf::ReplicantAt(int32 index, BView **_view, uint32 *_uniqueID,
	status_t *_error) const
{
	replicant_data *item = (replicant_data*)fReplicants.ItemAt(index);
	if (item == NULL) {
		// no replicant found
		if (_view)
			*_view = NULL;
		if (_uniqueID)
			*_uniqueID = ~(uint32)0;
		if (_error)
			*_error = B_BAD_INDEX;

		return NULL;
	}

	if (_view)
		*_view = item->view;
	if (_uniqueID)
		*_uniqueID = item->id;
	if (_error)
		*_error = item->error;

	return item->message;
}


/**
 * @brief Returns the index of the replicant whose live view is @a replicantView.
 *
 * Zombie views are not searched; use DeleteReplicant(BView*) for zombie lookup.
 *
 * @param replicantView The BView to look up.
 * @return Zero-based index of the matching entry, or -1 if not found.
 *
 * @see ReplicantAt(), CountReplicants()
 */
int32
BShelf::IndexOf(const BView* replicantView) const
{
	return replicant_data::IndexOf(&fReplicants, replicantView, false);
}


/**
 * @brief Returns the index of the replicant whose archived BMessage is @a archive.
 *
 * @param archive The BMessage pointer previously returned by ReplicantAt().
 * @return Zero-based index of the matching entry, or -1 if not found.
 *
 * @see ReplicantAt(), CountReplicants()
 */
int32
BShelf::IndexOf(const BMessage *archive) const
{
	return replicant_data::IndexOf(&fReplicants, archive);
}


/**
 * @brief Returns the index of the replicant with the given unique @a id.
 *
 * @param id The unique replicant ID as returned by ReplicantAt().
 * @return Zero-based index of the matching entry, or -1 if not found.
 *
 * @see ReplicantAt(), CountReplicants()
 */
int32
BShelf::IndexOf(uint32 id) const
{
	return replicant_data::IndexOf(&fReplicants, id);
}


/**
 * @brief Hook called to determine whether a replicant archive message is acceptable.
 *
 * Subclasses may override this to inspect the incoming BMessage and reject
 * replicants based on custom criteria before instantiation is attempted.
 *
 * @param archive The incoming replicant archive message.
 * @return true if the message should be accepted (default), false to reject it.
 *
 * @see CanAcceptReplicantView(), _AddReplicant()
 */
bool
BShelf::CanAcceptReplicantMessage(BMessage*) const
{
	return true;
}


/**
 * @brief Hook called to determine whether an instantiated replicant view is acceptable.
 *
 * Called after the replicant has been instantiated but before it is added to
 * the container view. Subclasses may inspect the frame, view, and archive to
 * implement custom placement or rejection logic.
 *
 * @param frame   The proposed frame rectangle in container view coordinates.
 * @param view    The newly instantiated BView.
 * @param archive The archive message associated with the view.
 * @return true if the view should be accepted (default), false to reject it.
 *
 * @see CanAcceptReplicantMessage(), AdjustReplicantBy()
 */
bool
BShelf::CanAcceptReplicantView(BRect, BView*, BMessage*) const
{
	return true;
}


/**
 * @brief Hook called to obtain a fine-grained position adjustment for a replicant.
 *
 * Called after CanAcceptReplicantView() has accepted the view. The returned
 * BPoint is added to the drop or requested position before the view is
 * moved into place.
 *
 * @param frame   The proposed frame rectangle in container view coordinates.
 * @param archive The archive message associated with the view.
 * @return The position delta to apply; default implementation returns B_ORIGIN
 *         (no adjustment).
 *
 * @see CanAcceptReplicantView(), _GetReplicant()
 */
BPoint
BShelf::AdjustReplicantBy(BRect, BMessage*) const
{
	return B_ORIGIN;
}


/**
 * @brief Hook called immediately before a replicant is removed from the shelf.
 *
 * Subclasses may override this to perform cleanup or notification when a
 * replicant is deleted. The default implementation is empty.
 *
 * @param index     The zero-based index of the replicant being removed.
 * @param archive   The archived BMessage associated with the replicant.
 * @param replicant The live (or zombie) BView that is about to be removed,
 *                  or NULL if no view was instantiated.
 *
 * @see DeleteReplicant(), _DeleteReplicant()
 */
void
BShelf::ReplicantDeleted(int32 index, const BMessage *archive,
	const BView *replicant)
{
}


/**
 * @brief Binary-compatibility stub for the first reserved BShelf virtual slot.
 *
 * This C-linkage function satisfies the vtable slot reserved for future use.
 * It is intentionally empty because the corresponding slot was not present in
 * BeOS R5's libbe.
 */
extern "C" void
_ReservedShelf1__6BShelfFv(BShelf *const, int32, const BMessage*, const BView*)
{
	// is not contained in BeOS R5's libbe, so we leave it empty
}


/** @brief Reserved virtual slot 2 — intentionally empty. */
void BShelf::_ReservedShelf2() {}
/** @brief Reserved virtual slot 3 — intentionally empty. */
void BShelf::_ReservedShelf3() {}
/** @brief Reserved virtual slot 4 — intentionally empty. */
void BShelf::_ReservedShelf4() {}
/** @brief Reserved virtual slot 5 — intentionally empty. */
void BShelf::_ReservedShelf5() {}
/** @brief Reserved virtual slot 6 — intentionally empty. */
void BShelf::_ReservedShelf6() {}
/** @brief Reserved virtual slot 7 — intentionally empty. */
void BShelf::_ReservedShelf7() {}
/** @brief Reserved virtual slot 8 — intentionally empty. */
void BShelf::_ReservedShelf8() {}


/**
 * @brief Disabled copy constructor — BShelf objects are not copyable.
 */
BShelf::BShelf(const BShelf&)
{
}


/**
 * @brief Disabled copy-assignment operator — BShelf objects are not copyable.
 *
 * @return Reference to this (never actually used).
 */
BShelf &
BShelf::operator=(const BShelf &)
{
	return *this;
}


/**
 * @brief Serialises the shelf state and all hosted replicants into @a data.
 *
 * Stores the following fields in @a data:
 * - "_zom_dsp" (bool): whether zombie views are displayed.
 * - "_zom_alw" (bool): whether zombie replicants are permitted.
 * - "_sg_cnt"  (int32): the current generation counter.
 * - "replicant" (BMessage, repeated): one archived replicant_data per entry.
 *
 * Also calls BHandler::Archive() to capture the handler name.
 *
 * @param data The BMessage to archive into.
 * @return B_OK on success, or an error code if any field could not be added.
 *
 * @see Save(), _InitData()
 */
status_t
BShelf::_Archive(BMessage *data) const
{
	status_t status = BHandler::Archive(data);
	if (status != B_OK)
		return status;

	status = data->AddBool("_zom_dsp", DisplaysZombies());
	if (status != B_OK)
		return status;

	status = data->AddBool("_zom_alw", AllowsZombies());
	if (status != B_OK)
		return status;

	status = data->AddInt32("_sg_cnt", fGenCount);
	if (status != B_OK)
		return status;

	BMessage archive('ARCV');
	for (int32 i = 0; i < fReplicants.CountItems(); i++) {
		if (((replicant_data *)fReplicants.ItemAt(i))->Archive(&archive) == B_OK)
			status = data->AddMessage("replicant", &archive);
		if (status != B_OK)
			break;
		archive.MakeEmpty();
	}

	return status;
}


/**
 * @brief Core initialisation routine called by all BShelf constructors.
 *
 * Performs the following steps in order:
 * 1. Initialises all shelf fields to their defaults.
 * 2. Opens a read-only BFile from @a entry if provided, otherwise stores
 *    @a stream directly as the input stream.
 * 3. Creates and installs the ShelfContainerViewFilter on @a view.
 * 4. Registers the shelf with the container view via BView::_SetShelf().
 * 5. If a stream is available, unflattens the archived shelf state and
 *    restores all replicants by calling AddReplicant() for each entry.
 *
 * @param entry      A BEntry pointing to the persistent storage file, or NULL.
 *                   Ownership is transferred; _InitData() stores the pointer.
 * @param stream     A BDataIO to read from if no entry is given, or NULL.
 *                   Not owned by the shelf when passed this way.
 * @param view       The container BView that will host replicants.
 * @param allowDrags If true, replicants may be repositioned by dragging.
 *
 * @see _Archive(), AddReplicant()
 */
void
BShelf::_InitData(BEntry *entry, BDataIO *stream, BView *view,
	bool allowDrags)
{
	fContainerView = view;
	fStream = NULL;
	fEntry = entry;
	fFilter = NULL;
	fGenCount = 1;
	fAllowDragging = allowDrags;
	fDirty = true;
	fDisplayZombies = false;
	fAllowZombies = true;
	fTypeEnforced = false;

	if (fEntry != NULL)
		fStream = new BFile(entry, B_READ_ONLY);
	else
		fStream = stream;

	fFilter = new ShelfContainerViewFilter(this, fContainerView);

	fContainerView->AddFilter(fFilter);
	fContainerView->_SetShelf(this);

	if (fStream != NULL) {
		BMessage archive;

		if (archive.Unflatten(fStream) == B_OK) {
			bool allowZombies;
			if (archive.FindBool("_zom_dsp", &allowZombies) != B_OK)
				allowZombies = false;

			SetDisplaysZombies(allowZombies);

			if (archive.FindBool("_zom_alw", &allowZombies) != B_OK)
				allowZombies = true;

			SetAllowsZombies(allowZombies);

			int32 genCount;
			if (!archive.FindInt32("_sg_cnt", &genCount))
				genCount = 1;

			BMessage replicant;
			for (int32 i = 0; archive.FindMessage("replicant", i, &replicant)
				== B_OK; i++) {
				BPoint point;
				BMessage *replMsg = new BMessage();
				ObjectDeleter<BMessage> deleter(replMsg);
				replicant.FindPoint("position", &point);
				if (replicant.FindMessage("message", replMsg) == B_OK)
					if (AddReplicant(replMsg, point) == B_OK) {
						// Detach the deleter since AddReplicant is taking
						// ownership on success. In R2 API this should be
						// changed to take always ownership on the message.
						deleter.Detach();
					}
			}
		}
	}
}


/**
 * @brief Removes and destroys a single replicant entry.
 *
 * Detaches the replicant view and its dragger from the container view, calls
 * the ReplicantDeleted() hook, removes the item from the internal list, and
 * then frees the views and dragger according to the dragger relation:
 * - TARGET_IS_PARENT or TARGET_IS_SIBLING: the replicant view is deleted.
 * - TARGET_IS_CHILD or TARGET_IS_SIBLING: the dragger is deleted.
 *
 * Also decrements the reference count for the replicant's add-on image in the
 * LoadedImages singleton and unloads it when the count reaches zero.
 *
 * @param item The replicant_data entry to remove. The pointer is invalid after
 *             this call returns.
 * @return B_OK always.
 *
 * @see ReplicantDeleted(), DeleteReplicant()
 */
status_t
BShelf::_DeleteReplicant(replicant_data* item)
{
	BView *view = item->view;
	if (view == NULL)
		view = item->zombie_view;

	if (view != NULL)
		view->RemoveSelf();

	if (item->dragger != NULL)
		item->dragger->RemoveSelf();

	int32 index = replicant_data::IndexOf(&fReplicants, item->message);

	ReplicantDeleted(index, item->message, view);

	fReplicants.RemoveItem(item);

	if (item->relation == BDragger::TARGET_IS_PARENT
		|| item->relation == BDragger::TARGET_IS_SIBLING) {
		delete view;
	}
	if (item->relation == BDragger::TARGET_IS_CHILD
		|| item->relation == BDragger::TARGET_IS_SIBLING) {
		delete item->dragger;
	}

	// Update use count for image and unload if necessary
	const char* signature = NULL;
	if (item->message->FindString("add_on", &signature) == B_OK
		&& signature != NULL) {
		LoadedImages* loadedImages = LoadedImages::Default();
		AutoLock<LoadedImages> lock(loadedImages);
		if (lock.IsLocked()) {
			LoadedImageMap::iterator it = loadedImages->images.find(
				BString(signature));

			if (it != loadedImages->images.end()) {
				(*it).second.second--;
				if ((*it).second.second <= 0) {
					unload_add_on((*it).second.first);
					loadedImages->images.erase(it);
				}
			}
		}
	}

	delete item;

	return B_OK;
}


/**
 * @brief Validates, instantiates, and hosts a new replicant.
 *
 * Takes over ownership of @a data on success only. The following checks are
 * performed in order before the replicant view is created:
 * 1. Type enforcement: if enabled, the "shelf_type" field must match the
 *    shelf's name.
 * 2. CanAcceptReplicantMessage(): the message-level acceptance hook.
 * 3. Uniqueness: if "be:load_each_time" is false, duplicate class/add-on
 *    combinations are rejected.
 *
 * After passing validation, the archivable is instantiated via
 * _InstantiateObject(). If instantiation fails and zombies are allowed and
 * displayed, a _BZombieReplicantView_ is created instead. The add-on image
 * reference count is updated in the LoadedImages singleton.
 *
 * @param data      The archived replicant BMessage. Ownership is transferred
 *                  on success only.
 * @param location  Pointer to the desired top-left BPoint, or NULL to use the
 *                  view's own archived frame.
 * @param uniqueID  The unique ID to assign to this replicant entry.
 * @return B_OK on success; B_ERROR if the replicant was rejected; or another
 *         error code forwarded via send_reply().
 *
 * @see _GetReplicant(), _CreateZombie(), send_reply(), CanAcceptReplicantMessage()
 */
status_t
BShelf::_AddReplicant(BMessage *data, BPoint *location, uint32 uniqueID)
{
	// Check shelf types if needed
	if (fTypeEnforced) {
		const char *shelfType = NULL;
		if (data->FindString("shelf_type", &shelfType) == B_OK
			&& shelfType != NULL) {
			if (Name() && strcmp(shelfType, Name()) != 0) {
				printf("Replicant was rejected by BShelf: The BShelf's type and the Replicant's type don't match.");
				return send_reply(data, B_ERROR, uniqueID);
			} else {
				printf("Replicant was rejected by BShelf: Replicant indicated a <type> (%s), but the shelf does not.", shelfType);
				return send_reply(data, B_ERROR, uniqueID);
			}
		} else {
			printf("Replicant was rejected by BShelf: Replicant did not have a <type>");
			return send_reply(data, B_ERROR, uniqueID);
		}
	}

	// Check if we can accept this message
	if (!CanAcceptReplicantMessage(data)) {
		printf("Replicant was rejected by BShelf::CanAcceptReplicantMessage()");
		return send_reply(data, B_ERROR, uniqueID);
	}

	// Check if we can create multiple instances
	if (data->FindBool("be:load_each_time")) {
		const char *className = NULL;
		const char *addOn = NULL;

		if (data->FindString("class", &className) == B_OK
			&& data->FindString("add_on", &addOn) == B_OK) {
			if (find_replicant(fReplicants, className, addOn)) {
				printf("Replicant was rejected. Unique replicant already exists. class=%s, signature=%s",
					className, addOn);
				return send_reply(data, B_ERROR, uniqueID);
			}
		}
	}

	// Instantiate the object, if this fails we have a zombie
	image_id image = -1;
	BArchivable *archivable = _InstantiateObject(data, &image);

	BView *view = NULL;

	if (archivable != NULL) {
		view = dynamic_cast<BView*>(archivable);

		if (view == NULL)
			return send_reply(data, B_ERROR, uniqueID);
	}

	BDragger* dragger = NULL;
	BView* replicant = NULL;
	BDragger::relation relation = BDragger::TARGET_UNKNOWN;
	_BZombieReplicantView_* zombie = NULL;
	if (view != NULL) {
		const BPoint point = location ? *location : view->Frame().LeftTop();
		replicant = _GetReplicant(data, view, point, dragger, relation);
		if (replicant == NULL)
			return send_reply(data, B_ERROR, uniqueID);
	} else if (fDisplayZombies && fAllowZombies) {
		zombie = _CreateZombie(data, dragger);
	} else if (!fAllowZombies) {
		// There was no view, and we're not allowed to have any zombies
		// in the house
		return send_reply(data, B_ERROR, uniqueID);
	}

	// Update use count for image
	const char* signature = NULL;
	if (data->FindString("add_on", &signature) == B_OK && signature != NULL) {
		LoadedImages* loadedImages = LoadedImages::Default();
		AutoLock<LoadedImages> lock(loadedImages);
		if (lock.IsLocked()) {
			LoadedImageMap::iterator it = loadedImages->images.find(
				BString(signature));

			if (it == loadedImages->images.end())
				loadedImages->images.insert(LoadedImageMap::value_type(
					BString(signature), std::pair<image_id, int>(image, 1)));
			else
				(*it).second.second++;
		}
	}

	if (zombie == NULL) {
		data->RemoveName("_drop_point_");
		data->RemoveName("_drop_offset_");
	}

	replicant_data *item = new replicant_data(data, replicant, dragger,
		relation, uniqueID);

	item->error = B_OK;
	item->zombie_view = zombie;

	fReplicants.AddItem(item);

	return send_reply(data, B_OK, uniqueID);
}


/**
 * @brief Resolves the dragger relationship, validates, positions, and adds a replicant view.
 *
 * Calls _GetReplicantData() to determine the dragger/target relationship and
 * locate the actual replicant BView. Then:
 * 1. Calls CanAcceptReplicantView() — if rejected, the view and dragger are
 *    freed according to the relation and NULL is returned.
 * 2. Calls AdjustReplicantBy() to obtain a fine-grained position delta.
 * 3. Moves the view to @a point plus the delta.
 * 4. Adds the dragger and/or replicant to the container view as appropriate.
 * 5. Installs a ReplicantViewFilter on the replicant view.
 *
 * @param data     The replicant archive message.
 * @param view     The top-level BView produced by instantiating the archive.
 * @param point    Desired top-left position in container view coordinates.
 * @param dragger  Out-parameter: set to the BDragger if one was found.
 * @param relation Out-parameter: set to the dragger/target relationship.
 * @return The replicant BView on success, or NULL if rejected or on error.
 *
 * @see _GetReplicantData(), CanAcceptReplicantView(), AdjustReplicantBy()
 */
BView *
BShelf::_GetReplicant(BMessage *data, BView *view, const BPoint &point,
	BDragger *&dragger, BDragger::relation &relation)
{
	// TODO: test me -- there seems to be lots of bugs parked here!
	BView *replicant = NULL;
	_GetReplicantData(data, view, replicant, dragger, relation);

	if (dragger != NULL)
		dragger->_SetViewToDrag(replicant);

	BRect frame = view->Frame().OffsetToCopy(point);
	if (!CanAcceptReplicantView(frame, replicant, data)) {
		// the view has not been accepted
		if (relation == BDragger::TARGET_IS_PARENT
			|| relation == BDragger::TARGET_IS_SIBLING) {
			delete replicant;
		}
		if (relation == BDragger::TARGET_IS_CHILD
			|| relation == BDragger::TARGET_IS_SIBLING) {
			delete dragger;
		}
		return NULL;
	}

	BPoint adjust = AdjustReplicantBy(frame, data);

	if (dragger != NULL)
		dragger->_SetShelf(this);

	// TODO: could be not correct for some relations
	view->MoveTo(point + adjust);

	// if it's a sibling or a child, we need to add the dragger
	if (relation == BDragger::TARGET_IS_SIBLING
		|| relation == BDragger::TARGET_IS_CHILD)
		fContainerView->AddChild(dragger);

	if (relation != BDragger::TARGET_IS_CHILD)
		fContainerView->AddChild(replicant);

	replicant->AddFilter(new ReplicantViewFilter(this, replicant));

	return replicant;
}


/**
 * @brief Determines the dragger/target relationship for an instantiated replicant.
 *
 * Inspects @a data and @a view to establish whether the replicant uses the
 * TARGET_IS_SIBLING, TARGET_IS_CHILD, or TARGET_IS_PARENT relationship:
 * - If a "__widget" sub-message is present, the dragger is instantiated from
 *   it and the relation is TARGET_IS_SIBLING.
 * - If @a view is itself a BDragger, the relation is TARGET_IS_CHILD and the
 *   replicant is the dragger's first child.
 * - Otherwise the relation is TARGET_IS_PARENT and the dragger is looked up
 *   by name ("_dragger_") inside the view.
 *
 * @param data      The replicant archive message, checked for "__widget".
 * @param view      The top-level BView produced by archive instantiation.
 * @param replicant Out: receives the actual replicant BView.
 * @param dragger   Out: receives the associated BDragger, or NULL.
 * @param relation  Out: receives the established dragger/target relationship.
 *
 * @see _GetReplicant(), BDragger::relation
 */
/* static */
void
BShelf::_GetReplicantData(BMessage *data, BView *view, BView *&replicant,
	BDragger *&dragger, BDragger::relation &relation)
{
	// Check if we have a dragger archived as "__widget" inside the message
	BMessage widget;
	if (data->FindMessage("__widget", &widget) == B_OK) {
		image_id draggerImage = B_ERROR;
		replicant = view;
		dragger = dynamic_cast<BDragger*>(_InstantiateObject(&widget, &draggerImage));
		// Replicant is a sibling, or unknown, if there isn't a dragger
		if (dragger != NULL)
			relation = BDragger::TARGET_IS_SIBLING;

	} else if ((dragger = dynamic_cast<BDragger*>(view)) != NULL) {
		// Replicant is child of the dragger
		relation = BDragger::TARGET_IS_CHILD;
		replicant = dragger->ChildAt(0);

	} else {
		// Replicant is parent of the dragger
		relation = BDragger::TARGET_IS_PARENT;
		replicant = view;
		dragger = dynamic_cast<BDragger *>(replicant->FindView("_dragger_"));
			// can be NULL, the replicant could not have a dragger at all
	}
}


/**
 * @brief Creates and installs a zombie placeholder view for a failed replicant.
 *
 * If @a data was dropped, positions the zombie at the drop point offset from
 * the container view's screen coordinates. Attaches a child BDragger to the
 * zombie view, marks it as zombied, sets the archived message on it, installs
 * a ReplicantViewFilter, and adds it to the container view.
 *
 * If the message was not dropped (e.g. loaded from a stream at startup), no
 * zombie view is created and NULL is returned.
 *
 * @param data    The failed replicant archive message. The zombie view stores
 *                a pointer to this message but does not take ownership.
 * @param dragger Out-parameter: set to the BDragger child added to the zombie,
 *                or left unchanged if no zombie is created.
 * @return The new _BZombieReplicantView_ on success, or NULL if the message
 *         was not a drop operation.
 *
 * @see _AddReplicant(), _BZombieReplicantView_
 */
_BZombieReplicantView_ *
BShelf::_CreateZombie(BMessage *data, BDragger *&dragger)
{
	// TODO: the zombies must be adjusted and moved as well!
	BRect frame;
	if (data->FindRect("_frame", &frame) != B_OK)
		frame = BRect();

	_BZombieReplicantView_ *zombie = NULL;
	if (data->WasDropped()) {
		BPoint offset;
		BPoint dropPoint = data->DropPoint(&offset);

		frame.OffsetTo(fContainerView->ConvertFromScreen(dropPoint) - offset);

		zombie = new _BZombieReplicantView_(frame, B_ERROR);

		frame.OffsetTo(B_ORIGIN);

		dragger = new BDragger(frame, zombie);
		dragger->_SetShelf(this);
		dragger->_SetZombied(true);

		zombie->AddChild(dragger);
		zombie->SetArchive(data);
		zombie->AddFilter(new ReplicantViewFilter(this, zombie));

		fContainerView->AddChild(zombie);
	}

	return zombie;
}


/**
 * @brief Resolves a scripting specifier to a replicant index and ID.
 *
 * Inspects the @a msg what-code (B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER,
 * B_NAME_SPECIFIER, or B_ID_SPECIFIER) and calls ReplicantAt() to locate the
 * matching replicant. On success, populates @a reply with "index" (int32) and
 * "ID" (int32) fields.
 *
 * @param msg   The specifier BMessage extracted from the scripting chain.
 * @param reply The BMessage to fill with "index" and "ID" on success.
 * @return B_OK if a matching replicant was found; B_ERROR or B_NAME_NOT_FOUND
 *         otherwise.
 *
 * @see MessageReceived(), ResolveSpecifier()
 */
status_t
BShelf::_GetProperty(BMessage *msg, BMessage *reply)
{
	uint32 ID;
	status_t err = B_ERROR;
	BView *replicant = NULL;
	switch (msg->what) {
		case B_INDEX_SPECIFIER:	{
			int32 index = -1;
			if (msg->FindInt32("index", &index)!=B_OK)
				break;
			ReplicantAt(index, &replicant, &ID, &err);
			break;
		}
		case B_REVERSE_INDEX_SPECIFIER:	{
			int32 rindex;
			if (msg->FindInt32("index", &rindex) != B_OK)
				break;
			ReplicantAt(CountReplicants() - rindex, &replicant, &ID, &err);
			break;
		}
		case B_NAME_SPECIFIER: {
			const char *name;
			if (msg->FindString("name", &name) != B_OK)
				break;
			for (int32 i = 0; i < CountReplicants(); i++) {
				BView *view = NULL;
				ReplicantAt(i, &view, &ID, &err);
				if (err != B_OK || view == NULL)
					continue;
				if (view->Name() != NULL && strcmp(view->Name(), name) == 0) {
					replicant = view;
					break;
				}
				err = B_NAME_NOT_FOUND;
			}
			break;
		}
		case B_ID_SPECIFIER: {
			uint32 id;
			if (msg->FindInt32("id", (int32 *)&id) != B_OK)
				break;
			for (int32 i = 0; i < CountReplicants(); i++) {
				BView *view = NULL;
				ReplicantAt(i, &view, &ID, &err);
				if (err != B_OK || view == NULL)
					continue;
				if (ID == id) {
					replicant = view;
					break;
				}
				err = B_NAME_NOT_FOUND;
			}
			break;
		}
		default:
			break;
	}

	if (replicant) {
		reply->AddInt32("index", IndexOf(replicant));
		reply->AddInt32("ID", ID);
	}

	return err;
}


/**
 * @brief Safely calls instantiate_object(), catching any exceptions.
 *
 * Wraps the global instantiate_object() call in a try/catch block so that
 * a poorly-written replicant constructor that throws an exception does not
 * crash the host application.
 *
 * @param archive The archived BMessage to instantiate from.
 * @param image   Out-parameter: receives the image_id of the loaded add-on,
 *                or B_ERROR if the class was found in the current image.
 * @return The instantiated BArchivable on success, or NULL if instantiation
 *         failed or threw an exception.
 *
 * @see _AddReplicant(), instantiate_object()
 */
/* static */
BArchivable *
BShelf::_InstantiateObject(BMessage *archive, image_id *image)
{
	// Stay on the safe side. The constructor called by instantiate_object
	// could throw an exception, which we catch here. Otherwise our calling app
	// could die without notice.
	try {
		return instantiate_object(archive, image);
	} catch (...) {
		return NULL;
	}
}

