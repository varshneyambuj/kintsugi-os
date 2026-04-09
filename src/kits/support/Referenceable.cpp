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
 *   Copyright 2005-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Referenceable.cpp
 * @brief Atomic reference-counting base class for heap-allocated objects.
 *
 * BReferenceable implements a thread-safe reference count using atomic
 * operations. Objects start with a reference count of 1. When the count
 * falls to zero, LastReferenceReleased() is called, which deletes the
 * object by default. Subclasses may override FirstReferenceAcquired() and
 * LastReferenceReleased() for custom lifecycle hooks.
 *
 * @see BReferenceable, BReference
 */


#include <Referenceable.h>

#include <stdio.h>
#include <OS.h>

//#define TRACE_REFERENCEABLE
#if defined(TRACE_REFERENCEABLE) && defined(_KERNEL_MODE)
#	include <tracing.h>
#	define TRACE(x, ...) ktrace_printf(x, __VA_ARGS__);
#else
#	define TRACE(x, ...)
#endif


/**
 * @brief Construct a BReferenceable with an initial reference count of one.
 *
 * The object is considered "owned" by its creator from construction; the
 * creator must call ReleaseReference() when it no longer needs the object.
 */
BReferenceable::BReferenceable()
	:
	fReferenceCount(1)
{
}


/**
 * @brief Destructor. Validates the reference count in debug builds.
 *
 * In non-boot builds, calls debugger() if the reference count at
 * destruction time is neither 0 nor 1, which indicates a reference
 * counting imbalance (e.g. a double-delete or a leaked reference).
 */
BReferenceable::~BReferenceable()
{
#if !defined(_BOOT_MODE)
	if (fReferenceCount != 0 && fReferenceCount != 1) {
		char message[256];
		snprintf(message, sizeof(message), "deleting referenceable object %p with "
			"reference count (%" B_PRId32 ")", this, fReferenceCount);
		debugger(message);
	}
#endif
}


/**
 * @brief Atomically increment the reference count.
 *
 * If the previous reference count was 0, FirstReferenceAcquired() is
 * invoked to notify the subclass that the object has been resurrected.
 * In non-boot builds, a negative previous count triggers debugger().
 *
 * @return The reference count value immediately before incrementing.
 * @see ReleaseReference(), FirstReferenceAcquired()
 */
int32
BReferenceable::AcquireReference()
{
	const int32 previousReferenceCount = atomic_add(&fReferenceCount, 1);
	if (previousReferenceCount == 0)
		FirstReferenceAcquired();
#if !defined(_BOOT_MODE)
	if (previousReferenceCount < 0)
		debugger("reference count is/was negative");
#endif

	TRACE("%p: acquire %ld\n", this, fReferenceCount);

	return previousReferenceCount;
}


/**
 * @brief Atomically decrement the reference count.
 *
 * If the previous reference count was 1 (i.e. it just reached 0),
 * LastReferenceReleased() is invoked. The default implementation of
 * LastReferenceReleased() deletes the object, so callers must not
 * access the object after this call returns in that case.
 * In non-boot builds, a non-positive previous count triggers debugger().
 *
 * @return The reference count value immediately before decrementing.
 * @see AcquireReference(), LastReferenceReleased()
 */
int32
BReferenceable::ReleaseReference()
{
	const int32 previousReferenceCount = atomic_add(&fReferenceCount, -1);
	TRACE("%p: release %ld\n", this, fReferenceCount);
	if (previousReferenceCount == 1)
		LastReferenceReleased();
#if !defined(_BOOT_MODE)
	if (previousReferenceCount <= 0)
		debugger("reference count is negative");
#endif
	return previousReferenceCount;
}


/**
 * @brief Called when the reference count transitions from 0 to 1.
 *
 * The default implementation does nothing. Subclasses may override this
 * to perform re-initialisation when a previously-unreferenced object is
 * reacquired (e.g. from a cache).
 *
 * @see AcquireReference()
 */
void
BReferenceable::FirstReferenceAcquired()
{
}


/**
 * @brief Called when the reference count transitions from 1 to 0.
 *
 * The default implementation deletes the object ("delete this"). Subclasses
 * that manage object lifetime externally (e.g. object pools) should override
 * this to return the object to the pool instead.
 *
 * @see ReleaseReference()
 */
void
BReferenceable::LastReferenceReleased()
{
	delete this;
}
