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
 *   Open Tracker License
 *
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of
 *   this software and associated documentation files (the "Software"), to deal in
 *   the Software without restriction, including without limitation the rights to
 *   use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *   of the Software, and to permit persons to whom the Software is furnished to do
 *   so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice applies to all licensees
 *   and shall be included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *   BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 *   AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
 *   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *   Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
 *   trademarks of Be Incorporated in the United States and other countries.
 *   All rights reserved.
 */


/**
 * @file FSUndoRedo.cpp
 * @brief Undo/redo infrastructure for Tracker file-system operations.
 *
 * Provides the Undo class hierarchy (UndoCopyMove, UndoDelete, UndoRename)
 * and the top-level Undo/Redo functions that pop and replay actions from the
 * per-application undo stack maintained by TTracker.
 *
 * @see FSUtils, TTracker
 */


#include "Commands.h"
#include "FSUndoRedo.h"
#include "FSUtils.h"

#include <Autolock.h>
#include <Volume.h>
#include <Node.h>
#include <Path.h>


static const int32 kUndoRedoListMaxCount = 20;


namespace BPrivate {

class UndoItem {
	public:
		virtual ~UndoItem() {}

		virtual status_t Undo() = 0;
		virtual status_t Redo() = 0;

		virtual void UpdateEntry(BEntry* /*entry*/, const char* /*name*/) {}
			// updates the name of the target from the source entry "entry"
};

static BObjectList<UndoItem> sUndoList, sRedoList;
static BLocker sLock("undo");

class UndoItemCopy : public UndoItem {
	public:
		UndoItemCopy(BObjectList<entry_ref, true>* sourceList, BDirectory &target,
			BList* pointList, uint32 moveMode);
		virtual ~UndoItemCopy();

		virtual status_t Undo();
		virtual status_t Redo();
		virtual void UpdateEntry(BEntry* entry, const char* name);

	private:
		BObjectList<entry_ref, true> fSourceList;
		BObjectList<entry_ref, true> fTargetList;
		entry_ref	fSourceRef, fTargetRef;
		uint32		fMoveMode;
};


class UndoItemMove : public UndoItem {
	public:
		/** source - list of file(s) that were moved.  Assumes ownership.
		 *	origfolder - location it was moved from
		 */
		UndoItemMove(BObjectList<entry_ref, true>* sourceList, BDirectory &target,
			BList* pointList);
		virtual ~UndoItemMove();

		virtual status_t Undo();
		virtual status_t Redo();

	private:
		BObjectList<entry_ref, true> fSourceList;
		entry_ref	fSourceRef, fTargetRef;
};


class UndoItemFolder : public UndoItem {
	public:
		UndoItemFolder(const entry_ref &ref);
			// ref - entry_ref indicating the folder created
		virtual ~UndoItemFolder();

		virtual status_t Undo();
		virtual status_t Redo();

	private:
		// this ref has two different meanings in the different states of
		// this object:
		//  - Undo() - fRef indicates the folder that was created via
		//    FSCreateNewFolderIn(...)
		//  - Redo() - fRef indicates the folder in which
		//    FSCreateNewFolderIn() should be performed
		entry_ref	fRef;
};


class UndoItemRename : public UndoItem {
	public:
		UndoItemRename(const entry_ref &origRef, const entry_ref &ref);
		UndoItemRename(const BEntry &entry, const char* newName);
		virtual ~UndoItemRename();

		virtual status_t Undo();
		virtual status_t Redo();

	private:
		entry_ref	fRef, fOrigRef;
};


class UndoItemRenameVolume : public UndoItem {
	public:
		UndoItemRenameVolume(BVolume &volume, const char* newName);
		virtual ~UndoItemRenameVolume();

		virtual status_t Undo();
		virtual status_t Redo();

	private:
		BVolume	fVolume;
		BString	fOldName, fNewName;
};


//--------------------------


/**
 * @brief Update all entry refs in @a list to point into the directory of @a entry.
 *
 * Used when rebuilding a redo target list after an undo.
 *
 * @param list   List of entry refs whose device and directory fields are updated.
 * @param entry  The BEntry whose containing directory is used as the new source.
 * @return B_OK on success, B_ERROR if the node ref cannot be retrieved.
 */
static status_t
ChangeListSource(BObjectList<entry_ref, true> &list, BEntry &entry)
{
	node_ref source;
	if (entry.GetNodeRef(&source) != B_OK)
		return B_ERROR;

	for (int32 index = 0; index < list.CountItems(); index++) {
		entry_ref* ref = list.ItemAt(index);

		ref->device = source.device;
		ref->directory = source.node;
	}

	return B_OK;
}


/**
 * @brief Push @a item onto the undo stack, trimming if the maximum depth is reached.
 *
 * Also clears the redo list because a new action invalidates the redo history.
 *
 * @param item  Heap-allocated UndoItem to push; ownership is transferred to sUndoList.
 */
static void
AddUndoItem(UndoItem* item)
{
	BAutolock locker(sLock);

	// we have a restricted number of possible undos
	if (sUndoList.CountItems() == kUndoRedoListMaxCount)
		sUndoList.RemoveItem(sUndoList.LastItem());

	sUndoList.AddItem(item, 0);
	sRedoList.MakeEmpty();
}


//	#pragma mark - Undo


/**
 * @brief Destructor; commits the pending undo item to the undo stack.
 *
 * If fUndo is non-NULL, pushes it onto sUndoList via AddUndoItem().
 */
Undo::~Undo()
{
	if (fUndo != NULL)
		AddUndoItem(fUndo);
}


/**
 * @brief Forward an entry-rename update to the pending undo item.
 *
 * @param entry     The renamed BEntry.
 * @param destName  The new name assigned to the entry.
 */
void
Undo::UpdateEntry(BEntry* entry, const char* destName)
{
	if (fUndo != NULL)
		fUndo->UpdateEntry(entry, destName);
}


/**
 * @brief Cancel the pending undo item without adding it to the stack.
 */
void
Undo::Remove()
{
	delete fUndo;
	fUndo = NULL;
}


/**
 * @brief Create an undo record for a move or copy operation.
 *
 * @param sourceList  List of entry refs that were moved or copied.
 * @param dest        The destination directory.
 * @param pointList   Optional list of drop positions (may be NULL).
 * @param moveMode    kMoveSelectionTo → move undo; otherwise copy undo.
 */
MoveCopyUndo::MoveCopyUndo(BObjectList<entry_ref, true>* sourceList,
	BDirectory &dest, BList* pointList, uint32 moveMode)
{
	if (moveMode == kMoveSelectionTo)
		fUndo = new UndoItemMove(sourceList, dest, pointList);
	else
		fUndo = new UndoItemCopy(sourceList, dest, pointList, moveMode);
}


/**
 * @brief Create an undo record for a newly created folder.
 *
 * @param ref  Entry ref of the newly created folder.
 */
NewFolderUndo::NewFolderUndo(const entry_ref &ref)
{
	fUndo = new UndoItemFolder(ref);
}


/**
 * @brief Create an undo record for a single file rename.
 *
 * @param entry    The BEntry that was renamed.
 * @param newName  The name that was assigned.
 */
RenameUndo::RenameUndo(BEntry &entry, const char* newName)
{
	fUndo = new UndoItemRename(entry, newName);
}


RenameVolumeUndo::RenameVolumeUndo(BVolume &volume, const char* newName)
{
	fUndo = new UndoItemRenameVolume(volume, newName);
}


//	#pragma mark - UndoItemCopy


UndoItemCopy::UndoItemCopy(BObjectList<entry_ref, true>* sourceList,
	BDirectory &target, BList* /*pointList*/, uint32 moveMode)
	:
	fSourceList(*sourceList),
	fTargetList(*sourceList),
	fMoveMode(moveMode)
{
	BEntry entry(sourceList->ItemAt(0));

	BEntry sourceEntry;
	entry.GetParent(&sourceEntry);
	sourceEntry.GetRef(&fSourceRef);

	BEntry targetEntry;
	target.GetEntry(&targetEntry);
	targetEntry.GetRef(&fTargetRef);
	ChangeListSource(fTargetList, targetEntry);
}


UndoItemCopy::~UndoItemCopy()
{
}


status_t
UndoItemCopy::Undo()
{
	FSDeleteRefList(new BObjectList<entry_ref, true>(fTargetList), true, false);
	return B_OK;
}


status_t
UndoItemCopy::Redo()
{
	FSMoveToFolder(new BObjectList<entry_ref, true>(fSourceList),
		new BEntry(&fTargetRef), FSUndoMoveMode(fMoveMode), NULL);

	return B_OK;
}


void
UndoItemCopy::UpdateEntry(BEntry* entry, const char* name)
{
	entry_ref changedRef;
	if (entry->GetRef(&changedRef) != B_OK)
		return;

	for (int32 index = 0; index < fSourceList.CountItems(); index++) {
		entry_ref* ref = fSourceList.ItemAt(index);
		if (changedRef != *ref)
			continue;

		ref = fTargetList.ItemAt(index);
		ref->set_name(name);
	}
}


//	#pragma mark - UndoItemMove


UndoItemMove::UndoItemMove(BObjectList<entry_ref, true>* sourceList,
	BDirectory &target, BList* /*pointList*/)
	:
	fSourceList(*sourceList)
{
	BEntry entry(sourceList->ItemAt(0));
	BEntry source;
	entry.GetParent(&source);
	source.GetRef(&fSourceRef);

	BEntry targetEntry;
	target.GetEntry(&targetEntry);
	targetEntry.GetRef(&fTargetRef);
}


UndoItemMove::~UndoItemMove()
{
}


status_t
UndoItemMove::Undo()
{
	BObjectList<entry_ref, true>* list = new BObjectList<entry_ref, true>(fSourceList);
	BEntry entry(&fTargetRef);
	ChangeListSource(*list, entry);

	// FSMoveToFolder() owns its arguments
	FSMoveToFolder(list, new BEntry(&fSourceRef),
		FSUndoMoveMode(kMoveSelectionTo), NULL);

	return B_OK;
}


status_t
UndoItemMove::Redo()
{
	// FSMoveToFolder() owns its arguments
	FSMoveToFolder(new BObjectList<entry_ref, true>(fSourceList),
		new BEntry(&fTargetRef), FSUndoMoveMode(kMoveSelectionTo), NULL);

	return B_OK;
}


//	#pragma mark -


UndoItemFolder::UndoItemFolder(const entry_ref &ref)
	:
	fRef(ref)
{
}


UndoItemFolder::~UndoItemFolder()
{
}


status_t
UndoItemFolder::Undo()
{
	FSDelete(new entry_ref(fRef), false, false);
	return B_OK;
}


status_t
UndoItemFolder::Redo()
{
	return FSCreateNewFolder(&fRef);
}


//	#pragma mark -


UndoItemRename::UndoItemRename(const entry_ref &origRef, const entry_ref &ref)
	:
	fRef(ref),
	fOrigRef(origRef)
{
}


UndoItemRename::UndoItemRename(const BEntry &entry, const char* newName)
{
	entry.GetRef(&fOrigRef);

	fRef = fOrigRef;
	fRef.set_name(newName);
}


UndoItemRename::~UndoItemRename()
{
}


status_t
UndoItemRename::Undo()
{
	BEntry entry(&fRef, false);
	return entry.Rename(fOrigRef.name);
}


status_t
UndoItemRename::Redo()
{
	BEntry entry(&fOrigRef, false);
	return entry.Rename(fRef.name);
}


//	#pragma mark - UndoItemRenameVolume


UndoItemRenameVolume::UndoItemRenameVolume(BVolume &volume,
	const char* newName)
	:
	fVolume(volume),
	fNewName(newName)
{
	char* buffer = fOldName.LockBuffer(B_FILE_NAME_LENGTH);
	if (buffer != NULL) {
		fVolume.GetName(buffer);
		fOldName.UnlockBuffer();
	}
}


UndoItemRenameVolume::~UndoItemRenameVolume()
{
}


status_t
UndoItemRenameVolume::Undo()
{
	return fVolume.SetName(fOldName.String());
}


status_t
UndoItemRenameVolume::Redo()
{
	return fVolume.SetName(fNewName.String());
}


//	#pragma mark - FSUndo() and FSRedo() functions


/**
 * @brief Pop the most recent undo item and execute its Undo() operation.
 *
 * Moves the item from sUndoList to sRedoList. Has no effect if the undo
 * list is empty.
 */
void
FSUndo()
{
	BAutolock locker(sLock);

	UndoItem* undoItem = sUndoList.FirstItem();
	if (undoItem == NULL)
		return;

	undoItem->Undo();
		// ToDo: evaluate return code

	sUndoList.RemoveItem(undoItem);

	if (sRedoList.CountItems() == kUndoRedoListMaxCount)
		sRedoList.RemoveItem(sRedoList.LastItem());

	sRedoList.AddItem(undoItem, 0);
}


/**
 * @brief Pop the most recent redo item and execute its Redo() operation.
 *
 * Moves the item from sRedoList back to sUndoList. Has no effect if the redo
 * list is empty.
 */
void
FSRedo()
{
	BAutolock locker(sLock);

	UndoItem* undoItem = sRedoList.FirstItem();
	if (undoItem == NULL)
		return;

	undoItem->Redo();
		// ToDo: evaluate return code

	sRedoList.RemoveItem(undoItem);

	if (sUndoList.CountItems() == kUndoRedoListMaxCount)
		sUndoList.RemoveItem(sUndoList.LastItem());

	sUndoList.AddItem(undoItem, 0);
}

}	// namespace BPrivate
