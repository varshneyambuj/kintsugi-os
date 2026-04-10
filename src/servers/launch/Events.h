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

/** @file Events.h
 *  @brief Event tree that schedules launch_daemon jobs (file_created, network, demand, …). */

#ifndef EVENTS_H
#define EVENTS_H


#include <Messenger.h>
#include <String.h>


class BaseJob;
class Event;


/** @brief Interface used by Events to register themselves with named external sources. */
class EventRegistrator {
public:
	/** @brief Subscribes @p event to the named external trigger. */
	virtual	status_t			RegisterExternalEvent(Event* event,
									const char* name,
									const BStringList& arguments) = 0;
	/** @brief Unsubscribes @p event from the named external trigger. */
	virtual	void				UnregisterExternalEvent(Event* event,
									const char* name) = 0;
};


/** @brief Abstract trigger that fires a launch job when a condition becomes true.
 *
 * Events form a tree (and/or composites with leaf triggers like file_created
 * or volume_mounted). The launch daemon registers each leaf with the
 * appropriate watcher; when a leaf fires, it propagates the trigger up the
 * tree, and if the root becomes "triggered" the owning BaseJob is launched. */
class Event {
public:
								Event(Event* parent);
	virtual						~Event();

	/** @brief Subclass hook: register the leaf event with @p registrator. */
	virtual	status_t			Register(
									EventRegistrator& registrator) = 0;
	/** @brief Subclass hook: unregister the leaf event from @p registrator. */
	virtual	void				Unregister(
									EventRegistrator& registrator) = 0;

	/** @brief Returns true if this event has currently fired. */
			bool				Triggered() const;
	/** @brief Marks this event as fired and propagates upward. */
	virtual	void				Trigger(Event* origin);
	/** @brief Clears the fired state for re-arming. */
	virtual	void				ResetTrigger();

	/** @brief Returns the owning launch job, walking up the tree if needed. */
	virtual	BaseJob*			Owner() const;
	/** @brief Sets the owning launch job. */
	virtual	void				SetOwner(BaseJob* owner);

	/** @brief Returns the parent event in the tree, or NULL at the root. */
			Event*				Parent() const;

	/** @brief Returns a human-readable description of the event. */
	virtual	BString				ToString() const = 0;

protected:
			Event*				fParent;     /**< Parent event in the tree (not owned). */
			bool				fTriggered;  /**< True once Trigger() has been called. */
};


/** @brief Helper factory and utilities for parsing and managing event trees. */
class Events {
public:
	/** @brief Builds an Event tree from a parsed configuration message. */
	static	Event*			FromMessage(const BMessenger& target,
								const BMessage& message);
	/** @brief Wraps @p event with an on-demand trigger. */
	static	Event*			AddOnDemand(const BMessenger& target, Event* event);
	/** @brief Resolves a leaf into an external event registered by name. */
	static	Event*			ResolveExternalEvent(Event* event,
								const char* name, uint32 flags);
	/** @brief Triggers a previously registered external event. */
	static	void			TriggerExternalEvent(Event* event);
	/** @brief Clears a sticky external event so it may fire again. */
	static	void			ResetStickyExternalEvent(Event* event);
	/** @brief Asks an on-demand event to fire, optionally without launching its job. */
	static	bool			TriggerDemand(Event* event, bool testOnly = false);
};


#endif // EVENTS_H
