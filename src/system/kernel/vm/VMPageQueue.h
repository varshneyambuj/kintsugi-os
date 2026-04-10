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
 *   Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/** @file VMPageQueue.h
 *  @brief Spinlock-protected doubly-linked queue of vm_page objects. */

#ifndef VM_PAGE_QUEUE_H
#define VM_PAGE_QUEUE_H


#include <util/DoublyLinkedList.h>

#include <lock.h>
#include <interrupts.h>
#include <util/AutoLock.h>
#include <vm/vm_types.h>


/** @brief Doubly-linked list of vm_pages with an embedded spinlock.
 *
 * The kernel's page allocator and pager keep their working sets (free,
 * cached, modified, active, …) in VMPageQueue instances. The plain methods
 * assume the caller already holds the queue's spinlock; the @c *Unlocked
 * variants take it themselves with interrupts disabled. */
struct VMPageQueue {
public:
	typedef DoublyLinkedList<vm_page,
		DoublyLinkedListMemberGetLink<vm_page, &vm_page::queue_link> > PageList;
	typedef PageList::ConstIterator Iterator;

public:
	/** @brief Initialises an empty queue with the given debug name. */
			void				Init(const char* name);

	/** @brief Returns the queue's debug name. */
			const char*			Name() const	{ return fName; }

	/** @brief Appends @p page to the tail of the queue. Caller must hold the lock. */
	inline	void				Append(vm_page* page);
	/** @brief Prepends @p page to the head of the queue. Caller must hold the lock. */
	inline	void				Prepend(vm_page* page);
	/** @brief Inserts @p page directly after @p insertAfter. Caller must hold the lock. */
	inline	void				InsertAfter(vm_page* insertAfter,
									vm_page* page);
	/** @brief Removes @p page from the queue. Caller must hold the lock. */
	inline	void				Remove(vm_page* page);
	/** @brief Removes and returns the head page, or NULL if empty. Caller must hold the lock. */
	inline	vm_page*			RemoveHead();
	/** @brief Re-inserts @p page at the head (tail==false) or tail (tail==true). */
	inline	void				Requeue(vm_page* page, bool tail);

	/** @brief Locked Append(): takes the queue spinlock with interrupts disabled. */
	inline	void				AppendUnlocked(vm_page* page);
	/** @brief Locked Append() that splices in an entire pre-built page list. */
	inline	void				AppendUnlocked(PageList& pages, uint32 count);
	/** @brief Locked Prepend(): takes the queue spinlock with interrupts disabled. */
	inline	void				PrependUnlocked(vm_page* page);
	/** @brief Locked Remove(): takes the queue spinlock with interrupts disabled. */
	inline	void				RemoveUnlocked(vm_page* page);
	/** @brief Locked RemoveHead(): takes the queue spinlock with interrupts disabled. */
	inline	vm_page*			RemoveHeadUnlocked();
	/** @brief Locked Requeue(): takes the queue spinlock with interrupts disabled. */
	inline	void				RequeueUnlocked(vm_page* page, bool tail);

	/** @brief Returns the head page, or NULL if empty. */
	inline	vm_page*			Head() const;
	/** @brief Returns the tail page, or NULL if empty. */
	inline	vm_page*			Tail() const;
	/** @brief Returns the page before @p page in the queue. */
	inline	vm_page*			Previous(vm_page* page) const;
	/** @brief Returns the page after @p page in the queue. */
	inline	vm_page*			Next(vm_page* page) const;

	/** @brief Returns the number of pages currently in the queue. */
	inline	page_num_t			Count() const	{ return fCount; }

	/** @brief Returns a const forward iterator over the queue. */
	inline	Iterator			GetIterator() const;

	/** @brief Returns a reference to the queue's spinlock. */
	inline	spinlock&			GetLock()	{ return fLock; }

protected:
			const char*			fName;   /**< Debug name shown in tracing/dumps. */
			spinlock			fLock;   /**< Spinlock protecting @c fPages and @c fCount. */
			page_num_t			fCount;  /**< Number of pages currently linked in. */
			PageList			fPages;  /**< Underlying intrusive doubly-linked list. */
};


// #pragma mark - VMPageQueue


void
VMPageQueue::Append(vm_page* page)
{
#if DEBUG_PAGE_QUEUE
	if (page->queue != NULL) {
		panic("%p->VMPageQueue::Append(page: %p): page thinks it is "
			"already in queue %p", this, page, page->queue);
	}
#endif	// DEBUG_PAGE_QUEUE

	fPages.Add(page);
	fCount++;

#if DEBUG_PAGE_QUEUE
	page->queue = this;
#endif
}


void
VMPageQueue::Prepend(vm_page* page)
{
#if DEBUG_PAGE_QUEUE
	if (page->queue != NULL) {
		panic("%p->VMPageQueue::Prepend(page: %p): page thinks it is "
			"already in queue %p", this, page, page->queue);
	}
#endif	// DEBUG_PAGE_QUEUE

	fPages.Add(page, false);
	fCount++;

#if DEBUG_PAGE_QUEUE
	page->queue = this;
#endif
}


void
VMPageQueue::InsertAfter(vm_page* insertAfter, vm_page* page)
{
#if DEBUG_PAGE_QUEUE
	if (page->queue != NULL) {
		panic("%p->VMPageQueue::InsertAfter(page: %p): page thinks it is "
			"already in queue %p", this, page, page->queue);
	}
#endif	// DEBUG_PAGE_QUEUE

	fPages.InsertAfter(insertAfter, page);
	fCount++;

#if DEBUG_PAGE_QUEUE
	page->queue = this;
#endif
}


void
VMPageQueue::Remove(vm_page* page)
{
#if DEBUG_PAGE_QUEUE
	if (page->queue != this) {
		panic("%p->VMPageQueue::Remove(page: %p): page thinks it "
			"is in queue %p", this, page, page->queue);
	}
#endif	// DEBUG_PAGE_QUEUE

	fPages.Remove(page);
	fCount--;

#if DEBUG_PAGE_QUEUE
	page->queue = NULL;
#endif
}


vm_page*
VMPageQueue::RemoveHead()
{
	vm_page* page = fPages.RemoveHead();
	if (page != NULL) {
		fCount--;

#if DEBUG_PAGE_QUEUE
		if (page->queue != this) {
			panic("%p->VMPageQueue::RemoveHead(): page %p thinks it is in "
				"queue %p", this, page, page->queue);
		}

		page->queue = NULL;
#endif	// DEBUG_PAGE_QUEUE
	}

	return page;
}


void
VMPageQueue::Requeue(vm_page* page, bool tail)
{
#if DEBUG_PAGE_QUEUE
	if (page->queue != this) {
		panic("%p->VMPageQueue::Requeue(): page %p thinks it is in "
			"queue %p", this, page, page->queue);
	}
#endif

	fPages.Remove(page);
	fPages.Add(page, tail);
}


void
VMPageQueue::AppendUnlocked(vm_page* page)
{
	InterruptsSpinLocker locker(fLock);
	Append(page);
}


void
VMPageQueue::AppendUnlocked(PageList& pages, uint32 count)
{
#if DEBUG_PAGE_QUEUE
	for (PageList::Iterator it = pages.GetIterator();
			vm_page* page = it.Next();) {
		if (page->queue != NULL) {
			panic("%p->VMPageQueue::AppendUnlocked(): page %p thinks it is "
				"already in queue %p", this, page, page->queue);
		}

		page->queue = this;
	}

#endif	// DEBUG_PAGE_QUEUE

	InterruptsSpinLocker locker(fLock);

	fPages.TakeFrom(&pages);
	fCount += count;
}


void
VMPageQueue::PrependUnlocked(vm_page* page)
{
	InterruptsSpinLocker locker(fLock);
	Prepend(page);
}


void
VMPageQueue::RemoveUnlocked(vm_page* page)
{
	InterruptsSpinLocker locker(fLock);
	return Remove(page);
}


vm_page*
VMPageQueue::RemoveHeadUnlocked()
{
	InterruptsSpinLocker locker(fLock);
	return RemoveHead();
}


void
VMPageQueue::RequeueUnlocked(vm_page* page, bool tail)
{
	InterruptsSpinLocker locker(fLock);
	Requeue(page, tail);
}


vm_page*
VMPageQueue::Head() const
{
	return fPages.Head();
}


vm_page*
VMPageQueue::Tail() const
{
	return fPages.Tail();
}


vm_page*
VMPageQueue::Previous(vm_page* page) const
{
	return fPages.GetPrevious(page);
}


vm_page*
VMPageQueue::Next(vm_page* page) const
{
	return fPages.GetNext(page);
}


VMPageQueue::Iterator
VMPageQueue::GetIterator() const
{
	return fPages.GetIterator();
}


#endif	// VM_PAGE_QUEUE_H
