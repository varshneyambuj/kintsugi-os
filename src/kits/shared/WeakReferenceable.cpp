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
 *   Copyright 2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file WeakReferenceable.cpp
 *  @brief Implements the \c WeakPointer and \c BWeakReferenceable classes
 *         that together provide thread-safe weak references to heap objects.
 *
 *  \c BWeakReferenceable is the base class for objects that can be weakly
 *  referenced. A \c WeakPointer acts as an indirection layer: it holds a
 *  strong use-count and a raw pointer to the object. Weak reference holders
 *  keep a reference-counted \c WeakPointer; they can promote it to a strong
 *  reference via \c WeakPointer::Get(), which atomically increments the
 *  use-count only if the object is still alive.
 */

#include <WeakReferenceable.h>

#include <stdio.h>
#include <OS.h>


namespace BPrivate {


/**
 * @brief Constructs a \c WeakPointer for \a object with an initial use-count
 *        of one.
 *
 * @param object The \c BWeakReferenceable instance this pointer tracks. Must
 *               not be \c NULL.
 */
WeakPointer::WeakPointer(BWeakReferenceable* object)
	:
	fUseCount(1),
	fObject(object)
{
}


/**
 * @brief Destructor. No resources are managed directly by \c WeakPointer.
 */
WeakPointer::~WeakPointer()
{
}


/**
 * @brief Atomically acquires a strong reference to the tracked object.
 *
 * Uses a compare-and-swap loop to increment \c fUseCount only when it is
 * currently positive, guaranteeing that no new strong references are issued
 * after the object has been destroyed (use-count reached zero). Calls
 * \c debugger() if a negative use-count is detected, which indicates a
 * programming error.
 *
 * @return A pointer to the tracked \c BWeakReferenceable, or \c NULL if the
 *         object has already been deleted.
 */
BWeakReferenceable*
WeakPointer::Get()
{
	int32 count;
	do {
		count = atomic_get(&fUseCount);
		if (count == 0)
			return NULL;
		if (count < 0)
			debugger("reference (use) count is negative");
	} while (atomic_test_and_set(&fUseCount, count + 1, count) != count);

	return fObject;
}


/**
 * @brief Releases a strong reference and deletes the object if the count
 *        reaches zero.
 *
 * Atomically decrements \c fUseCount. When the count transitions from 1 to 0
 * the tracked object is deleted and \c true is returned. A negative resulting
 * count indicates a bug and causes a \c debugger() call.
 *
 * @return \c true if the object was deleted as a result of this call,
 *         \c false otherwise.
 */
bool
WeakPointer::Put()
{
	const int32 count = atomic_add(&fUseCount, -1);
	if (count == 1) {
		delete fObject;
		return true;
	}
	if (count <= 0)
		debugger("reference (use) count is negative");

	return false;
}


/**
 * @brief Returns the current use-count of the tracked object.
 *
 * @return The value of \c fUseCount at the moment of the call.
 */
int32
WeakPointer::UseCount() const
{
	return fUseCount;
}


/**
 * @brief Increments the use-count without performing any validity check.
 *
 * Used internally when a new strong reference is created by the owning
 * \c BWeakReferenceable itself (e.g. during construction). Unlike \c Get(),
 * this method does not guard against a zero count.
 */
void
WeakPointer::GetUnchecked()
{
	atomic_add(&fUseCount, 1);
}


//	#pragma -


/**
 * @brief Constructs a \c BWeakReferenceable and allocates its \c WeakPointer.
 *
 * Allocates a \c WeakPointer on the heap using \c std::nothrow. Callers
 * should check \c InitCheck() after construction to verify the allocation
 * succeeded.
 */
BWeakReferenceable::BWeakReferenceable()
	:
	fPointer(new(std::nothrow) WeakPointer(this))
{
}


/**
 * @brief Destructor. Verifies the object is not still referenced and releases
 *        the \c WeakPointer.
 *
 * If the \c WeakPointer's use-count is exactly one (only the object itself
 * holds it) the count is atomically set to zero to mark the object as dead.
 * If the count is non-zero after this step a \c debugger() message is issued,
 * indicating that outstanding strong references remain — a sign of a reference
 * counting bug.
 */
BWeakReferenceable::~BWeakReferenceable()
{
	if (fPointer->UseCount() == 1)
		atomic_test_and_set(&fPointer->fUseCount, 0, 1);

	if (fPointer->UseCount() != 0) {
		char message[256];
		snprintf(message, sizeof(message), "deleting referenceable object %p with "
			"reference count (%" B_PRId32 ")", this, fPointer->UseCount());
		debugger(message);
	}

	fPointer->ReleaseReference();
}


/**
 * @brief Checks whether construction succeeded.
 *
 * @return \c B_OK if the internal \c WeakPointer was allocated successfully,
 *         or \c B_NO_MEMORY if the allocation failed.
 */
status_t
BWeakReferenceable::InitCheck()
{
	if (fPointer == NULL)
		return B_NO_MEMORY;
	return B_OK;
}


/**
 * @brief Returns a reference-counted \c WeakPointer for this object.
 *
 * Acquires an additional reference on the internal \c WeakPointer before
 * returning it, so the caller must eventually call \c ReleaseReference() on
 * the returned pointer.
 *
 * @return A \c WeakPointer with its reference count incremented.
 */
WeakPointer*
BWeakReferenceable::GetWeakPointer()
{
	fPointer->AcquireReference();
	return fPointer;
}


}	// namespace BPrivate
