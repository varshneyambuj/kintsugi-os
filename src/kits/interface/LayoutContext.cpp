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
 *   Copyright 2006 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold, bonefish@cs.tu-berlin.de
 */


/**
 * @file LayoutContext.cpp
 * @brief Implementation of BLayoutContext and BLayoutContextListener
 *
 * BLayoutContext acts as a shared context object passed to layout operations,
 * allowing layout items to coordinate with each other during a layout pass.
 * BLayoutContextListener receives notifications when the context is activated
 * or deactivated.
 *
 * @see BLayout, BLayoutItem
 */


#include <LayoutContext.h>


/**
 * @brief Construct a BLayoutContextListener.
 *
 * The base constructor performs no work. Subclasses should register
 * themselves with the relevant BLayoutContext via
 * BLayoutContext::AddListener() after construction.
 *
 * @see BLayoutContext::AddListener(), LayoutContextLeft()
 */
BLayoutContextListener::BLayoutContextListener()
{
}

/**
 * @brief Destroy the BLayoutContextListener.
 *
 * Subclasses should call BLayoutContext::RemoveListener() before their
 * destructor completes to avoid dangling listener pointers inside any
 * active BLayoutContext.
 *
 * @see BLayoutContext::RemoveListener()
 */
BLayoutContextListener::~BLayoutContextListener()
{
}


void BLayoutContextListener::_ReservedLayoutContextListener1() {}
void BLayoutContextListener::_ReservedLayoutContextListener2() {}
void BLayoutContextListener::_ReservedLayoutContextListener3() {}
void BLayoutContextListener::_ReservedLayoutContextListener4() {}
void BLayoutContextListener::_ReservedLayoutContextListener5() {}


// #pragma mark -


/**
 * @brief Construct a BLayoutContext.
 *
 * Initialises an empty listener list. The context becomes active
 * immediately upon construction; pass it to layout items via their
 * layout negotiation methods to coordinate a layout pass.
 *
 * @see AddListener(), BLayout
 */
BLayoutContext::BLayoutContext()
{
}

/**
 * @brief Destroy the BLayoutContext, notifying all registered listeners.
 *
 * Iterates over every registered BLayoutContextListener and calls its
 * LayoutContextLeft() method before the context is freed, giving listeners
 * a chance to clean up any references they hold to this context.
 *
 * @see AddListener(), RemoveListener(), BLayoutContextListener::LayoutContextLeft()
 */
BLayoutContext::~BLayoutContext()
{
	// notify the listeners
	for (int32 i = 0;
		 BLayoutContextListener* listener
		 	= (BLayoutContextListener*)fListeners.ItemAt(i);
		 i++) {
		listener->LayoutContextLeft(this);
	}
}

/**
 * @brief Register a listener to be notified when this context is destroyed.
 *
 * The listener's BLayoutContextListener::LayoutContextLeft() method will be
 * called when this BLayoutContext is destroyed. NULL pointers are silently
 * ignored.
 *
 * @param listener The listener to register; ignored if NULL.
 * @see RemoveListener(), BLayoutContextListener::LayoutContextLeft()
 */
void
BLayoutContext::AddListener(BLayoutContextListener* listener)
{
	if (listener)
		fListeners.AddItem(listener);
}

/**
 * @brief Unregister a previously added listener.
 *
 * After this call the listener will no longer receive
 * LayoutContextLeft() notifications from this context. NULL pointers are
 * silently ignored. It is safe to call this from within a LayoutContextLeft()
 * callback.
 *
 * @param listener The listener to remove; ignored if NULL.
 * @see AddListener()
 */
void
BLayoutContext::RemoveListener(BLayoutContextListener* listener)
{
	if (listener)
		fListeners.RemoveItem(listener);
}
