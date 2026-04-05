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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Erik Jaesler (erik@cgsoftware.com)
 */
#ifndef _MESSAGE_FILTER_H
#define _MESSAGE_FILTER_H

/**
 * @file MessageFilter.h
 * @brief Defines the BMessageFilter class and related enums for message filtering.
 */

#include <Handler.h>


class BMessage;


/**
 * @enum filter_result
 * @brief Return codes for message filter hooks and the Filter() method.
 *
 * These values indicate whether a filtered message should be dispatched
 * to its target or skipped entirely.
 */
enum filter_result {
	B_SKIP_MESSAGE,		/**< The message should be skipped (not dispatched). */
	B_DISPATCH_MESSAGE	/**< The message should be dispatched to the target. */
};


/**
 * @brief Function pointer type for message filter hook functions.
 *
 * A filter_hook is a C-style callback that can be used instead of (or in
 * addition to) overriding BMessageFilter::Filter(). When a non-NULL
 * filter_hook is installed, it takes precedence over the virtual Filter() method.
 *
 * @param message  The message being filtered.
 * @param target   Pointer to the target handler pointer; the hook may change it.
 * @param filter   The BMessageFilter that invoked this hook.
 * @return B_DISPATCH_MESSAGE to continue delivery, or B_SKIP_MESSAGE to drop it.
 */
typedef filter_result (*filter_hook)
	(BMessage* message, BHandler** target, BMessageFilter* filter);


/**
 * @enum message_delivery
 * @brief Specifies which delivery method a filter should match.
 *
 * Used to restrict a BMessageFilter to messages that arrived via a
 * particular delivery mechanism.
 */
enum message_delivery {
	B_ANY_DELIVERY,			/**< Match messages regardless of delivery method. */
	B_DROPPED_DELIVERY,		/**< Match only messages delivered via drag-and-drop. */
	B_PROGRAMMED_DELIVERY	/**< Match only messages sent programmatically. */
};

/**
 * @enum message_source
 * @brief Specifies which source a filter should match.
 *
 * Used to restrict a BMessageFilter to messages originating from a
 * particular source type.
 */
enum message_source {
	B_ANY_SOURCE,		/**< Match messages from any source. */
	B_REMOTE_SOURCE,	/**< Match only messages from remote (other team) sources. */
	B_LOCAL_SOURCE		/**< Match only messages from local (same team) sources. */
};


/**
 * @brief Filters messages before they are dispatched to a handler.
 *
 * BMessageFilter can be attached to a BLooper or BHandler to intercept
 * messages before they reach their target. Filters can examine, modify, or
 * reject messages based on delivery method, source, and command ('what' field).
 *
 * Filtering can be done either via a filter_hook function pointer or by
 * overriding the virtual Filter() method. If a filter_hook is provided,
 * it takes precedence over the virtual method.
 *
 * @see BLooper::AddCommonFilter()
 * @see BHandler::AddFilter()
 * @see filter_result
 * @see filter_hook
 */
class BMessageFilter {
public:
	/**
	 * @brief Constructs a filter that matches a specific command.
	 *
	 * The filter matches all delivery methods and sources, but only messages
	 * with the specified 'what' field.
	 *
	 * @param what  The message command to filter.
	 * @param func  Optional filter hook function. If non-NULL, it is called
	 *              instead of the virtual Filter() method.
	 */
								BMessageFilter(uint32 what,
									filter_hook func = NULL);

	/**
	 * @brief Constructs a filter that matches by delivery method and source.
	 *
	 * The filter matches any command ('what' field).
	 *
	 * @param delivery  The delivery method to match.
	 * @param source    The message source to match.
	 * @param func      Optional filter hook function.
	 */
								BMessageFilter(message_delivery delivery,
									message_source source, filter_hook func = NULL);

	/**
	 * @brief Constructs a filter that matches by delivery, source, and command.
	 *
	 * @param delivery  The delivery method to match.
	 * @param source    The message source to match.
	 * @param what      The message command to filter.
	 * @param func      Optional filter hook function.
	 */
								BMessageFilter(message_delivery delivery,
									message_source source, uint32 what,
									filter_hook func = NULL);

	/**
	 * @brief Copy constructor (from reference).
	 *
	 * @param filter  The BMessageFilter to copy.
	 */
								BMessageFilter(const BMessageFilter& filter);

	/**
	 * @brief Copy constructor (from pointer).
	 *
	 * @param filter  Pointer to the BMessageFilter to copy.
	 */
								BMessageFilter(const BMessageFilter* filter);

	/**
	 * @brief Destructor.
	 */
	virtual						~BMessageFilter();

	/**
	 * @brief Assignment operator.
	 *
	 * @param from  The BMessageFilter to assign from.
	 * @return A reference to this BMessageFilter.
	 */
			BMessageFilter&		operator=(const BMessageFilter& from);

	/**
	 * @brief Hook method called to filter a message.
	 *
	 * Override this method to implement custom filtering logic. This method
	 * is only called if no filter_hook function was provided.
	 *
	 * @param message  The message being filtered.
	 * @param _target  Pointer to the target handler pointer; may be changed
	 *                 to redirect the message.
	 * @return B_DISPATCH_MESSAGE to allow delivery, or B_SKIP_MESSAGE to drop it.
	 */
	virtual	filter_result		Filter(BMessage* message, BHandler** _target);

	/**
	 * @brief Returns the delivery method this filter matches.
	 *
	 * @return The message_delivery criterion.
	 */
			message_delivery	MessageDelivery() const;

	/**
	 * @brief Returns the source type this filter matches.
	 *
	 * @return The message_source criterion.
	 */
			message_source		MessageSource() const;

	/**
	 * @brief Returns the command ('what' field) this filter matches.
	 *
	 * @return The command value, or 0 if the filter matches any command.
	 */
			uint32				Command() const;

	/**
	 * @brief Checks whether this filter matches any command.
	 *
	 * @return true if the filter does not restrict by command, false if it
	 *         filters a specific command.
	 */
			bool				FiltersAnyCommand() const;

	/**
	 * @brief Returns the looper this filter is attached to.
	 *
	 * @return A pointer to the owning BLooper, or NULL if not attached.
	 */
			BLooper*			Looper() const;

private:
	friend class BLooper;
	friend class BHandler;

	virtual	void				_ReservedMessageFilter1();
	virtual	void				_ReservedMessageFilter2();

			void				SetLooper(BLooper* owner);
			filter_hook			FilterFunction() const;

			bool				fFiltersAny;
			uint32				fWhat;
			message_delivery	fDelivery;
			message_source		fSource;
			BLooper*			fLooper;
			filter_hook			fFilterFunction;

			uint32				_reserved[3];
};


#endif	// _MESSAGE_FILTER_H
