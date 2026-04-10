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
 *   Copyright 2015-2018, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file Events.cpp
 *  @brief Implements the event system for the launch daemon including demand, external, file, volume, and network events. */


#include "Events.h"

#include <stdio.h>

#include <Entry.h>
#include <LaunchRoster.h>
#include <Message.h>
#include <ObjectList.h>
#include <Path.h>
#include <StringList.h>

#include "BaseJob.h"
#include "FileWatcher.h"
#include "LaunchDaemon.h"
#include "NetworkWatcher.h"
#include "Utility.h"
#include "VolumeWatcher.h"


class EventContainer : public Event {
protected:
								EventContainer(Event* parent,
									const BMessenger* target,
									const BMessage& args);
								EventContainer(BaseJob* owner,
									const BMessenger& target);

public:
			void				AddEvent(Event* event);
			BObjectList<Event, true>& Events();

			const BMessenger&	Target() const;

	virtual	status_t			Register(EventRegistrator& registrator);
	virtual	void				Unregister(EventRegistrator& registrator);

	virtual	void				Trigger(Event* origin);

	virtual	BaseJob*			Owner() const;
	virtual	void				SetOwner(BaseJob* owner);

protected:
			void				AddEventsToString(BString& string) const;

protected:
			BaseJob*			fOwner;
			BMessenger			fTarget;
			BObjectList<Event, true> fEvents;
			bool				fRegistered;
};


class OrEvent : public EventContainer {
public:
								OrEvent(Event* parent, const BMessenger* target,
									const BMessage& args);
								OrEvent(BaseJob* owner,
									const BMessenger& target);

	virtual	void				ResetTrigger();

	virtual	BString				ToString() const;
};


class StickyEvent : public Event {
public:
								StickyEvent(Event* parent);
	virtual						~StickyEvent();

	virtual	void				ResetSticky();
	virtual	void				ResetTrigger();
};


class DemandEvent : public Event {
public:
								DemandEvent(Event* parent);

	virtual	status_t			Register(EventRegistrator& registrator);
	virtual	void				Unregister(EventRegistrator& registrator);

	virtual	BString				ToString() const;
};


class ExternalEvent : public Event {
public:
								ExternalEvent(Event* parent, const char* name,
									const BMessage& args);

			const BString&		Name() const;
			bool				Resolve(uint32 flags);

			void				ResetSticky();
	virtual	void				ResetTrigger();

	virtual	status_t			Register(EventRegistrator& registrator);
	virtual	void				Unregister(EventRegistrator& registrator);

	virtual	BString				ToString() const;

private:
			BString				fName;
			BStringList			fArguments;
			uint32				fFlags;
			bool				fResolved;
};


class FileCreatedEvent : public Event, FileListener {
public:
								FileCreatedEvent(Event* parent,
									const BMessage& args);

	virtual	status_t			Register(EventRegistrator& registrator);
	virtual	void				Unregister(EventRegistrator& registrator);

	virtual	BString				ToString() const;

	virtual void				FileCreated(const char* path);

private:
			BPath				fPath;
};


class VolumeMountedEvent : public Event, public VolumeListener {
public:
								VolumeMountedEvent(Event* parent,
									const BMessage& args);

	virtual	status_t			Register(EventRegistrator& registrator);
	virtual	void				Unregister(EventRegistrator& registrator);

	virtual	BString				ToString() const;

	virtual	void				VolumeMounted(dev_t device);
	virtual	void				VolumeUnmounted(dev_t device);
};


class NetworkAvailableEvent : public StickyEvent, public NetworkListener {
public:
								NetworkAvailableEvent(Event* parent,
									const BMessage& args);

	virtual	status_t			Register(EventRegistrator& registrator);
	virtual	void				Unregister(EventRegistrator& registrator);

	virtual	BString				ToString() const;

	virtual	void				NetworkAvailabilityChanged(bool available);
};


/**
 * @brief Factory function that instantiates the appropriate Event subclass by name.
 *
 * Recognizes "or", "demand", "file_created", "volume_mounted", and
 * "network_available". Unrecognized names produce an ExternalEvent.
 *
 * @param parent The parent event in the event tree (may be NULL for root).
 * @param name   The event type name.
 * @param target Optional BMessenger to send trigger notifications to.
 * @param args   The BMessage containing event arguments.
 * @return A newly allocated Event, or NULL if the name is "or" with empty args.
 */
static Event*
create_event(Event* parent, const char* name, const BMessenger* target,
	const BMessage& args)
{
	if (strcmp(name, "or") == 0) {
		if (args.IsEmpty())
			return NULL;

		return new OrEvent(parent, target, args);
	}

	if (strcmp(name, "demand") == 0)
		return new DemandEvent(parent);
	if (strcmp(name, "file_created") == 0)
		return new FileCreatedEvent(parent, args);
	if (strcmp(name, "volume_mounted") == 0)
		return new VolumeMountedEvent(parent, args);
	if (strcmp(name, "network_available") == 0)
		return new NetworkAvailableEvent(parent, args);

	return new ExternalEvent(parent, name, args);
}


// #pragma mark -


/**
 * @brief Constructs a base event with the given parent.
 *
 * @param parent The parent event, or NULL if this is a root event.
 */
Event::Event(Event* parent)
	:
	fParent(parent),
	fTriggered(false)
{
}


/** @brief Destroys the event. */
Event::~Event()
{
}


/** @brief Returns whether this event has been triggered. */
bool
Event::Triggered() const
{
	return fTriggered;
}


/**
 * @brief Marks this event as triggered and propagates upward to the parent.
 *
 * @param origin The original event that initiated the trigger.
 */
void
Event::Trigger(Event* origin)
{
	fTriggered = true;
	if (fParent != NULL)
		fParent->Trigger(origin);
}


/** @brief Resets this event to its non-triggered state. */
void
Event::ResetTrigger()
{
	fTriggered = false;
}


/**
 * @brief Returns the job that owns this event by walking up to the root.
 *
 * @return The owning BaseJob, or NULL if no owner is set.
 */
BaseJob*
Event::Owner() const
{
	if (fParent != NULL)
		return fParent->Owner();

	return NULL;
}


/**
 * @brief Sets the owning job by propagating upward to the root event.
 *
 * @param owner The BaseJob that owns this event tree.
 */
void
Event::SetOwner(BaseJob* owner)
{
	if (fParent != NULL)
		fParent->SetOwner(owner);
}


/** @brief Returns the parent event, or NULL for a root event. */
Event*
Event::Parent() const
{
	return fParent;
}


// #pragma mark -


/**
 * @brief Constructs an event container by parsing child events from a BMessage.
 *
 * Iterates over all B_MESSAGE_TYPE fields in @a args, creating child
 * events via create_event() and adding them.
 *
 * @param parent The parent event (may be NULL).
 * @param target Optional BMessenger for trigger notifications.
 * @param args   The BMessage containing child event definitions.
 */
EventContainer::EventContainer(Event* parent, const BMessenger* target,
	const BMessage& args)
	:
	Event(parent),
	fEvents(5),
	fRegistered(false)
{
	if (target != NULL)
		fTarget = *target;

	char* name;
	type_code type;
	int32 count;
	for (int32 index = 0; args.GetInfo(B_MESSAGE_TYPE, index, &name, &type,
			&count) == B_OK; index++) {
		BMessage message;
		for (int32 messageIndex = 0; args.FindMessage(name, messageIndex,
				&message) == B_OK; messageIndex++) {
			AddEvent(create_event(this, name, target, message));
		}
	}
}


/**
 * @brief Constructs an empty root event container with the given owner and target.
 *
 * @param owner  The job that owns this event tree.
 * @param target The BMessenger to send trigger notifications to.
 */
EventContainer::EventContainer(BaseJob* owner, const BMessenger& target)
	:
	Event(NULL),
	fOwner(owner),
	fTarget(target),
	fEvents(5),
	fRegistered(false)
{
}


/**
 * @brief Adds a child event to this container.
 *
 * NULL events are silently ignored.
 *
 * @param event The event to add (takes ownership).
 */
void
EventContainer::AddEvent(Event* event)
{
	if (event != NULL)
		fEvents.AddItem(event);
}


/** @brief Returns the list of child events. */
BObjectList<Event, true>&
EventContainer::Events()
{
	return fEvents;
}


/** @brief Returns the BMessenger target for trigger notifications. */
const BMessenger&
EventContainer::Target() const
{
	return fTarget;
}


/**
 * @brief Registers all child events with the given registrator.
 *
 * Skips registration if already registered. Stops and returns on first error.
 *
 * @param registrator The event registrator to register with.
 * @return B_OK on success, or the first child registration error.
 */
status_t
EventContainer::Register(EventRegistrator& registrator)
{
	if (fRegistered)
		return B_OK;

	int32 count = fEvents.CountItems();
	for (int32 index = 0; index < count; index++) {
		Event* event = fEvents.ItemAt(index);
		status_t status = event->Register(registrator);
		if (status != B_OK)
			return status;
	}

	fRegistered = true;
	return B_OK;
}


/**
 * @brief Unregisters all child events from the given registrator.
 *
 * @param registrator The event registrator to unregister from.
 */
void
EventContainer::Unregister(EventRegistrator& registrator)
{
	int32 count = fEvents.CountItems();
	for (int32 index = 0; index < count; index++) {
		Event* event = fEvents.ItemAt(index);
		event->Unregister(registrator);
	}
}


/**
 * @brief Triggers this container and, for root containers, notifies the owner via BMessenger.
 *
 * @param origin The event that initiated the trigger.
 */
void
EventContainer::Trigger(Event* origin)
{
	Event::Trigger(origin);

	if (Parent() == NULL && Owner() != NULL) {
		BMessage message(kMsgEventTriggered);
		message.AddPointer("event", origin);
		message.AddString("owner", Owner()->Name());
		fTarget.SendMessage(&message);
	}
}


/** @brief Returns the owning job of this event container. */
BaseJob*
EventContainer::Owner() const
{
	return fOwner;
}


/**
 * @brief Sets the owning job for this container and propagates to the parent.
 *
 * @param owner The BaseJob that owns this event tree.
 */
void
EventContainer::SetOwner(BaseJob* owner)
{
	Event::SetOwner(owner);
	fOwner = owner;
}


/**
 * @brief Appends a bracketed, comma-separated list of child event strings to @a string.
 *
 * @param string The output string to append to.
 */
void
EventContainer::AddEventsToString(BString& string) const
{
	string += "[";

	for (int32 index = 0; index < fEvents.CountItems(); index++) {
		if (index != 0)
			string += ", ";
		string += fEvents.ItemAt(index)->ToString();
	}
	string += "]";
}


// #pragma mark - or


/**
 * @brief Constructs an OR event from arguments, creating child events.
 *
 * @param parent The parent event.
 * @param target Optional BMessenger for trigger notifications.
 * @param args   The BMessage containing child event definitions.
 */
OrEvent::OrEvent(Event* parent, const BMessenger* target, const BMessage& args)
	:
	EventContainer(parent, target, args)
{
}


/**
 * @brief Constructs an empty OR event for the given owner and target.
 *
 * @param owner  The job owning this event.
 * @param target The BMessenger for trigger notifications.
 */
OrEvent::OrEvent(BaseJob* owner, const BMessenger& target)
	:
	EventContainer(owner, target)
{
}


/**
 * @brief Resets the trigger state for this OR event and all children.
 *
 * After resetting all children, re-evaluates whether any child is still
 * triggered (e.g. sticky events) and updates the OR state accordingly.
 */
void
OrEvent::ResetTrigger()
{
	fTriggered = false;

	int32 count = fEvents.CountItems();
	for (int32 index = 0; index < count; index++) {
		Event* event = fEvents.ItemAt(index);
		event->ResetTrigger();
		fTriggered |= event->Triggered();
	}
}


/**
 * @brief Returns a human-readable string representation of this OR event.
 *
 * @return A string of the form "or [child1, child2, ...]".
 */
BString
OrEvent::ToString() const
{
	BString string = "or ";
	EventContainer::AddEventsToString(string);
	return string;
}


// #pragma mark - StickyEvent


/**
 * @brief Constructs a sticky event with the given parent.
 *
 * @param parent The parent event.
 */
StickyEvent::StickyEvent(Event* parent)
	:
	Event(parent)
{
}


/** @brief Destroys the sticky event. */
StickyEvent::~StickyEvent()
{
}


/** @brief Explicitly resets the sticky trigger state (unlike ResetTrigger). */
void
StickyEvent::ResetSticky()
{
	Event::ResetTrigger();
}


/** @brief No-op for sticky events; the trigger persists until ResetSticky() is called. */
void
StickyEvent::ResetTrigger()
{
	// This is a sticky event; we don't reset the trigger here
}


// #pragma mark - demand


/**
 * @brief Constructs a demand event with the given parent.
 *
 * @param parent The parent event.
 */
DemandEvent::DemandEvent(Event* parent)
	:
	Event(parent)
{
}


/**
 * @brief Registration is a no-op for demand events (they are triggered programmatically).
 *
 * @param registrator The event registrator (unused).
 * @return B_OK always.
 */
status_t
DemandEvent::Register(EventRegistrator& registrator)
{
	return B_OK;
}


/**
 * @brief Unregistration is a no-op for demand events.
 *
 * @param registrator The event registrator (unused).
 */
void
DemandEvent::Unregister(EventRegistrator& registrator)
{
}


/**
 * @brief Returns the string "demand".
 *
 * @return A BString containing "demand".
 */
BString
DemandEvent::ToString() const
{
	return "demand";
}


// #pragma mark - External event


/**
 * @brief Constructs an external event with the given name and arguments.
 *
 * @param parent The parent event.
 * @param name   The external event's unique name.
 * @param args   BMessage containing "args" strings for this event.
 */
ExternalEvent::ExternalEvent(Event* parent, const char* name,
	const BMessage& args)
	:
	Event(parent),
	fName(name),
	fFlags(0),
	fResolved(false)
{
	const char* argument;
	for (int32 index = 0; args.FindString("args", index, &argument) == B_OK;
			index++) {
		fArguments.Add(argument);
	}
}


/** @brief Returns the name of this external event. */
const BString&
ExternalEvent::Name() const
{
	return fName;
}


/**
 * @brief Resolves (registers) this external event with the given flags.
 *
 * Can only be resolved once; subsequent calls return @c false.
 *
 * @param flags The event flags (e.g. B_STICKY_EVENT).
 * @return @c true if the event was newly resolved, @c false if already resolved.
 */
bool
ExternalEvent::Resolve(uint32 flags)
{
	if (fResolved)
		return false;

	fResolved = true;
	fFlags = flags;
	return true;
}


/** @brief Resets the trigger state only if this event has the B_STICKY_EVENT flag. */
void
ExternalEvent::ResetSticky()
{
	if ((fFlags & B_STICKY_EVENT) != 0)
		Event::ResetTrigger();
}


/** @brief Resets the trigger state only if this event does NOT have the B_STICKY_EVENT flag. */
void
ExternalEvent::ResetTrigger()
{
	if ((fFlags & B_STICKY_EVENT) == 0)
		Event::ResetTrigger();
}


/**
 * @brief Registers this external event with the event registrator.
 *
 * @param registrator The registrator to register with.
 * @return B_OK on success, or an error code on failure.
 */
status_t
ExternalEvent::Register(EventRegistrator& registrator)
{
	return registrator.RegisterExternalEvent(this, Name().String(), fArguments);
}


/**
 * @brief Unregisters this external event from the event registrator.
 *
 * @param registrator The registrator to unregister from.
 */
void
ExternalEvent::Unregister(EventRegistrator& registrator)
{
	registrator.UnregisterExternalEvent(this, Name().String());
}


/**
 * @brief Returns the external event's name as a string.
 *
 * @return The event name.
 */
BString
ExternalEvent::ToString() const
{
	return fName;
}


// #pragma mark - file_created


/**
 * @brief Constructs a file-created event that triggers when a file appears at the given path.
 *
 * @param parent The parent event.
 * @param args   BMessage whose "args" string is the filesystem path to watch.
 */
FileCreatedEvent::FileCreatedEvent(Event* parent, const BMessage& args)
	:
	Event(parent)
{
	fPath.SetTo(args.GetString("args", NULL));
}


/**
 * @brief Registers this event with the FileWatcher to monitor the configured path.
 *
 * @param registrator The event registrator (unused; registration is via FileWatcher).
 * @return B_OK on success, or an error code if path monitoring fails.
 */
status_t
FileCreatedEvent::Register(EventRegistrator& registrator)
{
	return FileWatcher::Register(this, fPath);
}


/**
 * @brief Unregisters this event from the FileWatcher.
 *
 * @param registrator The event registrator (unused).
 */
void
FileCreatedEvent::Unregister(EventRegistrator& registrator)
{
	FileWatcher::Unregister(this, fPath);
}


/**
 * @brief Returns a human-readable string for this file-created event.
 *
 * @return A string of the form "file_created <path>".
 */
BString
FileCreatedEvent::ToString() const
{
	BString string = "file_created ";
	string << fPath.Path();
	return string;
}


/**
 * @brief Callback invoked by FileWatcher when a file is created at a watched path.
 *
 * Triggers the event if @a path matches the configured watch path.
 *
 * @param path The path where a file was created.
 */
void
FileCreatedEvent::FileCreated(const char* path)
{
	if (strcmp(fPath.Path(), path) == 0)
		Trigger(this);
}


// #pragma mark -


/**
 * @brief Constructs a volume-mounted event.
 *
 * @param parent The parent event.
 * @param args   Event arguments (currently unused).
 */
VolumeMountedEvent::VolumeMountedEvent(Event* parent, const BMessage& args)
	:
	Event(parent)
{
}


/**
 * @brief Registers this event with the VolumeWatcher for mount notifications.
 *
 * @param registrator The event registrator (unused; registration is via VolumeWatcher).
 * @return B_OK always.
 */
status_t
VolumeMountedEvent::Register(EventRegistrator& registrator)
{
	VolumeWatcher::Register(this);
	return B_OK;
}


/**
 * @brief Unregisters this event from the VolumeWatcher.
 *
 * @param registrator The event registrator (unused).
 */
void
VolumeMountedEvent::Unregister(EventRegistrator& registrator)
{
	VolumeWatcher::Unregister(this);
}


/**
 * @brief Returns the string "volume_mounted".
 *
 * @return A BString containing "volume_mounted".
 */
BString
VolumeMountedEvent::ToString() const
{
	return "volume_mounted";
}


/**
 * @brief Triggers this event when a volume is mounted.
 *
 * @param device The device ID of the newly mounted volume.
 */
void
VolumeMountedEvent::VolumeMounted(dev_t device)
{
	Trigger(this);
}


/**
 * @brief No-op handler for volume unmount notifications.
 *
 * @param device The device ID of the unmounted volume.
 */
void
VolumeMountedEvent::VolumeUnmounted(dev_t device)
{
}


// #pragma mark -


/**
 * @brief Constructs a network-available sticky event.
 *
 * @param parent The parent event.
 * @param args   Event arguments (currently unused).
 */
NetworkAvailableEvent::NetworkAvailableEvent(Event* parent,
	const BMessage& args)
	:
	StickyEvent(parent)
{
}


/**
 * @brief Registers this event with the NetworkWatcher for availability notifications.
 *
 * @param registrator The event registrator (unused; registration is via NetworkWatcher).
 * @return B_OK always.
 */
status_t
NetworkAvailableEvent::Register(EventRegistrator& registrator)
{
	NetworkWatcher::Register(this);
	return B_OK;
}


/**
 * @brief Unregisters this event from the NetworkWatcher.
 *
 * @param registrator The event registrator (unused).
 */
void
NetworkAvailableEvent::Unregister(EventRegistrator& registrator)
{
	NetworkWatcher::Unregister(this);
}


/**
 * @brief Returns the string "network_available".
 *
 * @return A BString containing "network_available".
 */
BString
NetworkAvailableEvent::ToString() const
{
	return "network_available";
}


/**
 * @brief Handles network availability changes; triggers when available, resets when not.
 *
 * @param available @c true if the network became available, @c false if it went down.
 */
void
NetworkAvailableEvent::NetworkAvailabilityChanged(bool available)
{
	if (available)
		Trigger(this);
	else
		ResetSticky();
}


// #pragma mark -


/**
 * @brief Creates an event tree from a BMessage, wrapped in an implicit OR.
 *
 * @param target  The BMessenger to send trigger notifications to.
 * @param message The BMessage containing event definitions.
 * @return A newly allocated Event tree, or NULL on failure.
 */
/*static*/ Event*
Events::FromMessage(const BMessenger& target, const BMessage& message)
{
	return create_event(NULL, "or", &target, message);
}


/**
 * @brief Adds a DemandEvent to an existing event tree, wrapping in an OR if needed.
 *
 * If @a event is not already an OrEvent, creates a new OrEvent containing
 * the original event plus a new DemandEvent.
 *
 * @param target The BMessenger for trigger notifications.
 * @param event  The existing event tree (may be NULL).
 * @return The augmented event tree containing a DemandEvent.
 */
/*static*/ Event*
Events::AddOnDemand(const BMessenger& target, Event* event)
{
	OrEvent* orEvent = dynamic_cast<OrEvent*>(event);
	if (orEvent == NULL) {
		EventContainer* container = dynamic_cast<EventContainer*>(event);
		if (container != NULL)
			orEvent = new OrEvent(container->Owner(), container->Target());
		else
			orEvent = new OrEvent(NULL, target);
	}
	if (orEvent != event && event != NULL)
		orEvent->AddEvent(event);

	orEvent->AddEvent(new DemandEvent(orEvent));
	return orEvent;
}


/**
 * @brief Recursively searches the event tree for an unresolved ExternalEvent with the given name and resolves it.
 *
 * @param event The root of the event tree to search.
 * @param name  The external event name to resolve.
 * @param flags The event flags to apply upon resolution.
 * @return The resolved ExternalEvent, or NULL if not found or already resolved.
 */
/*static*/ Event*
Events::ResolveExternalEvent(Event* event, const char* name, uint32 flags)
{
	if (event == NULL)
		return NULL;

	if (EventContainer* container = dynamic_cast<EventContainer*>(event)) {
		for (int32 index = 0; index < container->Events().CountItems();
				index++) {
			Event* event = ResolveExternalEvent(container->Events().ItemAt(index), name, flags);
			if (event != NULL)
				return event;
		}
	} else if (ExternalEvent* external = dynamic_cast<ExternalEvent*>(event)) {
		if (external->Name() == name && external->Resolve(flags))
			return external;
	}

	return NULL;
}


/**
 * @brief Triggers an event if it is an ExternalEvent; no-op for other types or NULL.
 *
 * @param event The event to trigger (may be NULL).
 */
/*static*/ void
Events::TriggerExternalEvent(Event* event)
{
	if (event == NULL)
		return;

	ExternalEvent* external = dynamic_cast<ExternalEvent*>(event);
	if (external == NULL)
		return;

	external->Trigger(external);
}


/**
 * @brief Resets the sticky state of an ExternalEvent; no-op for other types or NULL.
 *
 * @param event The event to reset (may be NULL).
 */
/*static*/ void
Events::ResetStickyExternalEvent(Event* event)
{
	if (event == NULL)
		return;

	ExternalEvent* external = dynamic_cast<ExternalEvent*>(event);
	if (external == NULL)
		return;

	external->ResetSticky();
}


/*!	This will trigger a demand event, if it exists.

	\param testOnly If \c true, the deman will not actually be triggered,
			it will only be checked if it could.
	\return \c true, if there is a demand event, and it has been
			triggered by this call. \c false if not.
*/
/*static*/ bool
Events::TriggerDemand(Event* event, bool testOnly)
{
	if (event == NULL || event->Triggered())
		return false;

	if (EventContainer* container = dynamic_cast<EventContainer*>(event)) {
		for (int32 index = 0; index < container->Events().CountItems();
				index++) {
			Event* childEvent = container->Events().ItemAt(index);
			if (dynamic_cast<DemandEvent*>(childEvent) != NULL) {
				if (testOnly)
					return true;

				childEvent->Trigger(childEvent);
				break;
			}
			if (dynamic_cast<EventContainer*>(childEvent) != NULL) {
				if (TriggerDemand(childEvent, testOnly))
					break;
			}
		}
	}

	return event->Triggered();
}
