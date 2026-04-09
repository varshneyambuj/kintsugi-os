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
 *   Copyright 2010, Axel Dörfler, axeld@pinc-software.de.
 *   This file may be used under the terms of the MIT License.
 */

/** @file QueryFile.cpp
 *  @brief Implementation of BQueryFile, a BEntryList adapter that executes
 *         a saved Tracker query across one or more filesystem volumes.
 *
 *  BQueryFile reads a query definition from a Tracker query file (or from
 *  a live BQuery object) and iterates over matching filesystem entries.
 *  It supports multi-volume queries by sequentially executing the same
 *  predicate on each target volume and stitching results together through
 *  the standard BEntryList interface.
 *
 *  @note Write support is not yet implemented. Live query support is also
 *        not yet available.
 */

#include <QueryFile.h>

#include <fs_attr.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include "tracker/MimeTypes.h"
#include "tracker/Utilities.h"


// TODO: add write support
// TODO: let Tracker use it?
// TODO: live query support?


/** @brief Attribute name used to store the query predicate string in a
 *         Tracker query file. */
const char*	kAttrQueryString = "_trk/qrystr";

/** @brief Attribute name used to store the target volume information in a
 *         Tracker query file. */
const char* kAttrQueryVolume = "_trk/qryvol1";


/** @brief Constructs a BQueryFile from an entry_ref and initializes it
 *         via SetTo(const entry_ref&).
 *
 *  @param ref  Reference to the Tracker query file to open.
 */
BQueryFile::BQueryFile(const entry_ref& ref)
{
	SetTo(ref);
}


/** @brief Constructs a BQueryFile from a BEntry and initializes it via
 *         SetTo(const BEntry&).
 *
 *  @param entry  The BEntry pointing to the Tracker query file.
 */
BQueryFile::BQueryFile(const BEntry& entry)
{
	SetTo(entry);
}


/** @brief Constructs a BQueryFile from a filesystem path and initializes it
 *         via SetTo(const char*).
 *
 *  @param path  Absolute or relative path to the Tracker query file.
 */
BQueryFile::BQueryFile(const char* path)
{
	SetTo(path);
}


/** @brief Constructs a BQueryFile from a live BQuery object and initializes
 *         it via SetTo(BQuery&).
 *
 *  @param query  A configured BQuery whose predicate and target volume are
 *                adopted.
 */
BQueryFile::BQueryFile(BQuery& query)
{
	SetTo(query);
}


/** @brief Destructor. */
BQueryFile::~BQueryFile()
{
}


/** @brief Returns the initialization status of the object.
 *
 *  @return B_OK if the object is fully initialized, or B_NO_INIT if
 *          SetTo() has not been called successfully.
 */
status_t
BQueryFile::InitCheck() const
{
	return fStatus;
}


/** @brief Loads a query definition from an entry_ref pointing to a Tracker
 *         query file.
 *
 *  Reads the query predicate from the "_trk/qrystr" attribute and the target
 *  volume list from the "_trk/qryvol1" attribute.  If the volume attribute is
 *  absent or all stored volumes are unavailable, all persistent, query-capable
 *  volumes are added instead.
 *
 *  @param ref  The entry_ref of the Tracker query file.
 *  @return B_OK on success, B_NO_MEMORY on allocation failure, or an error
 *          code from BNode / attribute access on failure.
 */
status_t
BQueryFile::SetTo(const entry_ref& ref)
{
	Unset();

	BNode node(&ref);
	fStatus = node.InitCheck();
	if (fStatus != B_OK)
		return fStatus;

	ssize_t bytesRead = node.ReadAttrString(kAttrQueryString, &fPredicate);
	if (bytesRead < 0)
		return fStatus = bytesRead;

	bool searchAllVolumes = true;
	attr_info info;
	if (node.GetAttrInfo(kAttrQueryVolume, &info) == B_OK) {
		void* buffer = malloc(info.size);
		if (buffer == NULL)
			return fStatus = B_NO_MEMORY;

		BMessage message;
		fStatus = message.Unflatten((const char*)buffer);
		if (fStatus == B_OK) {
			for (int32 index = 0; index < 100; index++) {
				BVolume volume;
				status_t status = BPrivate::MatchArchivedVolume(&volume,
					&message, index);
				if (status == B_OK) {
					fStatus = AddVolume(volume);
					if (fStatus != B_OK)
						break;

					searchAllVolumes = false;
				} else if (status != B_DEV_BAD_DRIVE_NUM) {
					// Volume doesn't seem to be mounted
					fStatus = status;
					break;
				}
			}
		}

		free(buffer);
	}

	if (searchAllVolumes) {
		// add all volumes to query
		BVolumeRoster roster;
		BVolume volume;
		while (roster.GetNextVolume(&volume) == B_OK) {
			if (volume.IsPersistent() && volume.KnowsQuery())
				AddVolume(volume);
		}
	}

	return fStatus;
}


/** @brief Loads a query definition from a BEntry.
 *
 *  Resolves the BEntry to an entry_ref and delegates to
 *  SetTo(const entry_ref&).
 *
 *  @param entry  BEntry pointing to the Tracker query file.
 *  @return B_OK on success or an error code on failure.
 */
status_t
BQueryFile::SetTo(const BEntry& entry)
{
	entry_ref ref;
	fStatus = entry.GetRef(&ref);
	if (fStatus != B_OK)
		return fStatus;

	return SetTo(ref);
}


/** @brief Loads a query definition from a filesystem path.
 *
 *  Resolves the path to an entry_ref and delegates to
 *  SetTo(const entry_ref&).
 *
 *  @param path  Path to the Tracker query file.
 *  @return B_OK on success or an error code on failure.
 */
status_t
BQueryFile::SetTo(const char* path)
{
	entry_ref ref;
	fStatus = get_ref_for_path(path, &ref);
	if (fStatus != B_OK)
		return fStatus;

	return SetTo(ref);
}


/** @brief Initializes this BQueryFile from a live BQuery object.
 *
 *  Adopts the predicate string and the single target volume from @p query.
 *
 *  @param query  A configured BQuery to copy predicate and volume from.
 *  @return B_OK on success or an error code on failure.
 */
status_t
BQueryFile::SetTo(BQuery& query)
{
	Unset();

	BString predicate;
	query.GetPredicate(&predicate);

	fStatus = SetPredicate(predicate.String());
	if (fStatus != B_OK)
		return fStatus;

	return fStatus = AddVolume(query.TargetDevice());
}


/** @brief Resets the object to an uninitialized state.
 *
 *  Clears the volume list, query object, predicate string, and sets the
 *  status to B_NO_INIT.
 */
void
BQueryFile::Unset()
{
	fStatus = B_NO_INIT;
	fCurrentVolumeIndex = -1;
	fVolumes.MakeEmpty();
	fQuery.Clear();
	fPredicate = "";
}


/** @brief Sets the query predicate string.
 *
 *  @param predicate  A Tracker query predicate string (e.g.
 *                    "name==\"*.txt\"").
 *  @return B_OK always.
 */
status_t
BQueryFile::SetPredicate(const char* predicate)
{
	fPredicate = predicate;
	return B_OK;
}


/** @brief Adds a BVolume to the list of target volumes for this query.
 *
 *  @param volume  The volume to add.
 *  @return B_OK on success or B_NO_MEMORY if the list cannot be extended.
 */
status_t
BQueryFile::AddVolume(const BVolume& volume)
{
	return fVolumes.AddItem((void*)(addr_t)volume.Device()) ? B_OK : B_NO_MEMORY;
}


/** @brief Adds a volume device identifier to the target volume list.
 *
 *  @param device  The dev_t identifier of the volume to add.
 *  @return B_OK on success or B_NO_MEMORY if the list cannot be extended.
 */
status_t
BQueryFile::AddVolume(dev_t device)
{
	return fVolumes.AddItem((void*)(addr_t)device) ? B_OK : B_NO_MEMORY;
}


/** @brief Returns the query predicate string.
 *
 *  @return A C-string containing the current predicate, or an empty string
 *          if none has been set.
 */
const char*
BQueryFile::Predicate() const
{
	return fPredicate.String();
}


/** @brief Returns the number of target volumes registered with this query.
 *
 *  @return The count of volumes in the target volume list.
 */
int32
BQueryFile::CountVolumes() const
{
	return fVolumes.CountItems();
}


/** @brief Returns the dev_t device identifier for the volume at @p index.
 *
 *  @param index  Zero-based index into the volume list.
 *  @return The dev_t value for the volume, or -1 if @p index is out of range.
 */
dev_t
BQueryFile::VolumeAt(int32 index) const
{
	if (index < 0 || index >= fVolumes.CountItems())
		return -1;

	return (dev_t)(addr_t)fVolumes.ItemAt(index);
}


/** @brief Writes the query definition to the file at @p ref.
 *
 *  @note This method is not yet implemented.
 *
 *  @param ref  Destination entry_ref for the query file.
 *  @return B_NOT_SUPPORTED always.
 */
status_t
BQueryFile::WriteTo(const entry_ref& ref)
{
	// TODO: implement
	return B_NOT_SUPPORTED;
}


/** @brief Writes the query definition to the file at the given path.
 *
 *  @note This method is not yet implemented.
 *
 *  @param path  Destination path for the query file.
 *  @return B_NOT_SUPPORTED always.
 */
status_t
BQueryFile::WriteTo(const char* path)
{
	entry_ref ref;
	status_t status = get_ref_for_path(path, &ref);
	if (status != B_OK)
		return status;

	return WriteTo(ref);
}


// #pragma mark - BEntryList implementation


/** @brief Retrieves the next matching entry across all target volumes.
 *
 *  Iterates volumes in order; when the current volume is exhausted advances
 *  to the next one and re-fetches the query.  On the first call the first
 *  volume's query is started automatically.
 *
 *  @param entry     Output BEntry set to the next matching entry.
 *  @param traverse  If true, symbolic links are traversed.
 *  @return B_OK on success, B_ENTRY_NOT_FOUND when all volumes are exhausted,
 *          or another error code on failure.
 */
status_t
BQueryFile::GetNextEntry(BEntry* entry, bool traverse)
{
	if (fCurrentVolumeIndex == -1) {
		// Start with first volume
		fCurrentVolumeIndex = 0;

		status_t status = _SetQuery(0);
		if (status != B_OK)
			return status;
	}

	status_t status = B_ENTRY_NOT_FOUND;

	while (fCurrentVolumeIndex < CountVolumes()) {
		status = fQuery.GetNextEntry(entry, traverse);
		if (status != B_ENTRY_NOT_FOUND)
			break;

		// Continue with next volume, if any
		status = _SetQuery(++fCurrentVolumeIndex);
	}

	return status;
}


/** @brief Retrieves the next matching entry reference across all target
 *         volumes.
 *
 *  Same volume-iteration logic as GetNextEntry().
 *
 *  @param ref  Output entry_ref set to the next matching entry.
 *  @return B_OK on success, B_ENTRY_NOT_FOUND when exhausted, or an error
 *          code on failure.
 */
status_t
BQueryFile::GetNextRef(entry_ref* ref)
{
	if (fCurrentVolumeIndex == -1) {
		// Start with first volume
		fCurrentVolumeIndex = 0;

		status_t status = _SetQuery(0);
		if (status != B_OK)
			return status;
	}

	status_t status = B_ENTRY_NOT_FOUND;

	while (fCurrentVolumeIndex < CountVolumes()) {
		status = fQuery.GetNextRef(ref);
		if (status != B_ENTRY_NOT_FOUND)
			break;

		// Continue with next volume, if any
		status = _SetQuery(++fCurrentVolumeIndex);
	}

	return status;
}


/** @brief Retrieves the next batch of matching directory entries across all
 *         target volumes.
 *
 *  Same volume-iteration logic as GetNextEntry().
 *
 *  @param buffer  Buffer to receive dirent structures.
 *  @param length  Size of @p buffer in bytes.
 *  @param count   Maximum number of dirents to return.
 *  @return The number of dirents written, B_ENTRY_NOT_FOUND when exhausted,
 *          or an error code on failure.
 */
int32
BQueryFile::GetNextDirents(struct dirent* buffer, size_t length, int32 count)
{
	if (fCurrentVolumeIndex == -1) {
		// Start with first volume
		fCurrentVolumeIndex = 0;

		status_t status = _SetQuery(0);
		if (status != B_OK)
			return status;
	}

	status_t status = B_ENTRY_NOT_FOUND;

	while (fCurrentVolumeIndex < CountVolumes()) {
		status = fQuery.GetNextDirents(buffer, length, count);
		if (status != B_ENTRY_NOT_FOUND)
			break;

		// Continue with next volume, if any
		status = _SetQuery(++fCurrentVolumeIndex);
	}

	return status;
}


/** @brief Rewinds the iteration so the next Get* call starts from the first
 *         volume again.
 *
 *  @return B_OK always.
 */
status_t
BQueryFile::Rewind()
{
	fCurrentVolumeIndex = -1;
	return B_OK;
}


/** @brief Returns the total number of matching entries.
 *
 *  @note Not supported; always returns -1.
 *
 *  @return -1.
 */
int32
BQueryFile::CountEntries()
{
	// not supported
	return -1;
}


/** @brief Returns the MIME type string that identifies Tracker query files.
 *
 *  @return B_QUERY_MIMETYPE.
 */
/*static*/ const char*
BQueryFile::MimeType()
{
	return B_QUERY_MIMETYPE;
}


/** @brief Configures the internal BQuery for the volume at @p index and
 *         fetches results.
 *
 *  Clears the previous query, sets the predicate and target volume for
 *  volume @p index, then calls Fetch().
 *
 *  @param index  Zero-based index into the volume list.
 *  @return B_OK on success, B_ENTRY_NOT_FOUND if @p index >= CountVolumes(),
 *          or an error code from BQuery::Fetch() on failure.
 */
status_t
BQueryFile::_SetQuery(int32 index)
{
	if (fCurrentVolumeIndex >= CountVolumes())
		return B_ENTRY_NOT_FOUND;

	BVolume volume(VolumeAt(fCurrentVolumeIndex));
	fQuery.Clear();
	fQuery.SetPredicate(fPredicate.String());
	fQuery.SetVolume(&volume);

	return fQuery.Fetch();
}
