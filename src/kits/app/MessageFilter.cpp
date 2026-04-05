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
 *     Ambuj Varshney, varshney@ambuj.se
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


/**
 * @file MessageFilter.cpp
 * @brief Implementation of BMessageFilter for intercepting and filtering messages before dispatch.
 *
 * BMessageFilter provides a mechanism to examine and optionally reject or redirect
 * BMessages before they are dispatched to their target BHandler. Filters can match
 * on message command, delivery method, and source, and may use a hook function or
 * virtual method for custom filtering logic.
 */


#include <MessageFilter.h>


/** @brief Construct a filter matching a specific message command.
 *  @param inWhat The message command code to filter on.
 *  @param func Optional filter hook function invoked when a message matches.
 *              Pass NULL to rely on the virtual Filter() method instead.
 */
BMessageFilter::BMessageFilter(uint32 inWhat, filter_hook func)
	:
	fFiltersAny(false),
	fWhat(inWhat),
	fDelivery(B_ANY_DELIVERY),
	fSource(B_ANY_SOURCE),
	fLooper(NULL),
	fFilterFunction(func)
{
}


/** @brief Construct a filter matching any command with specific delivery and source constraints.
 *  @param delivery The message delivery method to filter on (e.g., B_ANY_DELIVERY).
 *  @param source The message source type to filter on (e.g., B_ANY_SOURCE).
 *  @param func Optional filter hook function invoked when a message matches.
 */
BMessageFilter::BMessageFilter(message_delivery delivery,
	message_source source, filter_hook func)
	:
	fFiltersAny(true),
	fWhat(0),
	fDelivery(delivery),
	fSource(source),
	fLooper(NULL),
	fFilterFunction(func)
{
}


/** @brief Construct a filter matching a specific command with delivery and source constraints.
 *  @param delivery The message delivery method to filter on.
 *  @param source The message source type to filter on.
 *  @param inWhat The message command code to filter on.
 *  @param func Optional filter hook function invoked when a message matches.
 */
BMessageFilter::BMessageFilter(message_delivery delivery,
	message_source source, uint32 inWhat, filter_hook func)
	:
	fFiltersAny(false),
	fWhat(inWhat),
	fDelivery(delivery),
	fSource(source),
	fLooper(NULL),
	fFilterFunction(func)
{
}


/** @brief Copy constructor.
 *  @param filter The message filter to copy from.
 */
BMessageFilter::BMessageFilter(const BMessageFilter& filter)
{
	*this = filter;
}


/** @brief Construct a filter by copying from a pointer to another filter.
 *  @param filter Pointer to the message filter to copy from.
 */
BMessageFilter::BMessageFilter(const BMessageFilter* filter)
{
	*this = *filter;
}


/** @brief Destructor. */
BMessageFilter::~BMessageFilter()
{
}


/** @brief Assignment operator.
 *  @param from The message filter to assign from.
 *  @return Reference to this filter after assignment.
 */
BMessageFilter &
BMessageFilter::operator=(const BMessageFilter& from)
{
	fFiltersAny			= from.FiltersAnyCommand();
	fWhat				= from.Command();
	fDelivery			= from.MessageDelivery();
	fSource				= from.MessageSource();
	fFilterFunction		= from.FilterFunction();

	SetLooper(from.Looper());

	return *this;
}


/** @brief Filter a message before it is dispatched to its target handler.
 *
 *  Override this virtual method to implement custom filtering logic.
 *  The default implementation returns B_DISPATCH_MESSAGE, allowing the
 *  message through without modification.
 *
 *  @param message The message being filtered.
 *  @param target Pointer to the target handler pointer; may be modified to
 *                redirect the message to a different handler.
 *  @return B_DISPATCH_MESSAGE to allow the message through, or
 *          B_SKIP_MESSAGE to discard it.
 */
filter_result
BMessageFilter::Filter(BMessage* message, BHandler** target)
{
	return B_DISPATCH_MESSAGE;
}


/** @brief Return the message delivery constraint for this filter.
 *  @return The message_delivery type this filter matches against.
 */
message_delivery
BMessageFilter::MessageDelivery() const
{
	return fDelivery;
}


/** @brief Return the message source constraint for this filter.
 *  @return The message_source type this filter matches against.
 */
message_source
BMessageFilter::MessageSource() const
{
	return fSource;
}


/** @brief Return the message command code this filter matches.
 *  @return The command code (the 'what' field) this filter is set to match.
 */
uint32
BMessageFilter::Command() const
{
	return fWhat;
}


/** @brief Check whether this filter matches all message commands.
 *  @return true if the filter matches any command, false if it is restricted
 *          to a specific command code.
 */
bool
BMessageFilter::FiltersAnyCommand() const
{
	return fFiltersAny;
}


/** @brief Return the looper this filter is attached to.
 *  @return Pointer to the owning BLooper, or NULL if the filter is not
 *          attached to any looper.
 */
BLooper*
BMessageFilter::Looper() const
{
	return fLooper;
}


void BMessageFilter::_ReservedMessageFilter1() {}
void BMessageFilter::_ReservedMessageFilter2() {}


/** @brief Set the looper that owns this filter.
 *  @param owner Pointer to the BLooper to associate with this filter.
 */
void
BMessageFilter::SetLooper(BLooper* owner)
{
	fLooper = owner;
}


/** @brief Return the filter hook function associated with this filter.
 *  @return The filter_hook function pointer, or NULL if no hook was set.
 */
filter_hook
BMessageFilter::FilterFunction() const
{
	return fFilterFunction;
}
