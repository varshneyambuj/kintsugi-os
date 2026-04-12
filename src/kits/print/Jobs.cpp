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
 *   Copyright (c) 2002 Haiku Project
 *   Author: Michael Pfeiffer
 *   Licensed under the MIT License.
 */


/**
 * @file Jobs.cpp
 * @brief Print job and spool-folder management for the print server.
 *
 * Implements the Job class, which represents a single print job backed by a
 * spool file in the filesystem, and the Folder class, which owns an ordered
 * list of Job objects and keeps it synchronised with the spool directory via
 * the FolderWatcher node-monitor infrastructure.
 *
 * @see FolderWatcher, Printer
 */


#include "pr_server.h"
#include "Jobs.h"
// #include "PrintServerApp.h"

// posix
#include <stdlib.h>
#include <string.h>

// BeOS
#include <kernel/fs_attr.h>
#include <Application.h>
#include <Node.h>
#include <NodeInfo.h>
#include <NodeMonitor.h>


// Implementation of Job

/**
 * @brief Constructs a Job from a spool-directory entry.
 *
 * Reads the entry's node reference and validates that the file meets the
 * spool-file contract (correct MIME type and mandatory attributes). The job
 * status is read from the PSRV_SPOOL_ATTR_STATUS node attribute and the
 * creation time is parsed from the trailing "@<timestamp>" in the file name.
 *
 * @param job     BEntry pointing to the spool file.
 * @param folder  The Folder that owns this job; stored as a back-pointer.
 */
Job::Job(const BEntry& job, Folder* folder)
	: fFolder(folder)
	, fTime(-1)
	, fStatus(kUnknown)
	, fValid(false)
	, fPrinter(NULL)
{
	// store light weight versions of BEntry and BNode
	job.GetRef(&fEntry);
	job.GetNodeRef(&fNode);

	fValid = IsValidJobFile();

	BNode node(&job);
	if (node.InitCheck() != B_OK) return;

	BString status;
		// read status attribute
	if (node.ReadAttrString(PSRV_SPOOL_ATTR_STATUS, &status) != B_OK) {
		status = "";
	}
    UpdateStatus(status.String());

    	// Now get file name and creation time from file name
    fTime = 0;
    BEntry entry(job);
    char name[B_FILE_NAME_LENGTH];
    if (entry.InitCheck() == B_OK && entry.GetName(name) == B_OK) {
    	fName = name;
    		// search for last '@' in file name
		char* p = NULL, *c = name;
		while ((c = strchr(c, '@')) != NULL) {
			p = c; c ++;
		}
			// and get time from file name
		if (p) fTime = atoi(p+1);
    }
}

/**
 * @brief Updates the cached JobStatus from a string representation.
 *
 * Converts the human-readable status string stored in the spool-file attribute
 * to the corresponding JobStatus enum value and stores it in fStatus.
 *
 * @param status  Null-terminated status string such as PSRV_JOB_STATUS_WAITING.
 */
void Job::UpdateStatus(const char* status) {
	if (strcmp(status, PSRV_JOB_STATUS_WAITING) == 0) fStatus = kWaiting;
	else if (strcmp(status, PSRV_JOB_STATUS_PROCESSING) == 0) fStatus = kProcessing;
	else if (strcmp(status, PSRV_JOB_STATUS_FAILED) == 0) fStatus = kFailed;
	else if (strcmp(status, PSRV_JOB_STATUS_COMPLETED) == 0) fStatus = kCompleted;
	else fStatus = kUnknown;
}

/**
 * @brief Writes a raw status string to the spool file's node attribute.
 *
 * Opens a BNode for the job's entry ref and writes \a status as a
 * B_STRING_TYPE attribute under PSRV_SPOOL_ATTR_STATUS.
 *
 * @param status  Null-terminated status string to persist.
 */
void Job::UpdateStatusAttribute(const char* status) {
	BNode node(&fEntry);
	if (node.InitCheck() == B_OK) {
		node.WriteAttr(PSRV_SPOOL_ATTR_STATUS, B_STRING_TYPE, 0, status, strlen(status)+1);
	}
}


/**
 * @brief Checks whether a named attribute exists on a node.
 *
 * @param n     The BNode to query.
 * @param name  Name of the attribute to look for.
 * @return true if the attribute exists, false otherwise.
 */
bool Job::HasAttribute(BNode* n, const char* name) {
	attr_info info;
	return n->GetAttrInfo(name, &info) == B_OK;
}


/**
 * @brief Validates that the backing file is a well-formed spool job.
 *
 * Checks that the file has the correct MIME type (PSRV_SPOOL_FILETYPE) and
 * that all mandatory spool attributes are present.
 *
 * @return true if the file is a valid spool job, false otherwise.
 */
bool Job::IsValidJobFile() {
	BNode node(&fEntry);
	if (node.InitCheck() != B_OK) return false;

	BNodeInfo info(&node);
	char mimeType[256];

		// Is job a spool file?
	return (info.InitCheck() == B_OK &&
	    info.GetType(mimeType) == B_OK &&
	    strcmp(mimeType, PSRV_SPOOL_FILETYPE) == 0) &&
	    HasAttribute(&node, PSRV_SPOOL_ATTR_MIMETYPE) &&
	    HasAttribute(&node, PSRV_SPOOL_ATTR_PAGECOUNT) &&
	    HasAttribute(&node, PSRV_SPOOL_ATTR_DESCRIPTION) &&
	    HasAttribute(&node, PSRV_SPOOL_ATTR_PRINTER) &&
	    HasAttribute(&node, PSRV_SPOOL_ATTR_STATUS);
}


/**
 * @brief Sets the job status and optionally persists it to the spool file.
 *
 * Updates the in-memory fStatus field. If \a writeToNode is true, the
 * corresponding string constant is also written to the node attribute so that
 * the print server can observe the change.
 *
 * @param s            The new JobStatus value.
 * @param writeToNode  If true, the status is also written to the spool file attribute.
 */
void Job::SetStatus(JobStatus s, bool writeToNode) {
	fStatus = s;
	if (!writeToNode) return;
	switch (s) {
		case kWaiting: UpdateStatusAttribute(PSRV_JOB_STATUS_WAITING); break;
		case kProcessing: UpdateStatusAttribute(PSRV_JOB_STATUS_PROCESSING); break;
		case kFailed: UpdateStatusAttribute(PSRV_JOB_STATUS_FAILED); break;
		case kCompleted: UpdateStatusAttribute(PSRV_JOB_STATUS_COMPLETED); break;
		default: break;
	}
}

/**
 * @brief Re-reads the spool file's status attribute and updates internal state.
 *
 * Re-validates fValid (in case the file has since gained its required attributes)
 * and re-reads the PSRV_SPOOL_ATTR_STATUS node attribute to synchronise fStatus
 * with any external change.
 */
void Job::UpdateAttribute() {
	fValid = fValid || IsValidJobFile();
	BNode node(&fEntry);
	BString status;
	if (node.InitCheck() == B_OK &&
		node.ReadAttrString(PSRV_SPOOL_ATTR_STATUS, &status) == B_OK) {
		UpdateStatus(status.String());
	}
}

/**
 * @brief Removes the backing spool file from the filesystem.
 */
void Job::Remove() {
	BEntry entry(&fEntry);
	if (entry.InitCheck() == B_OK) entry.Remove();
}

// Implementation of Folder

/**
 * @brief BObjectList comparator that sorts jobs by ascending creation time.
 *
 * Used by BObjectList::SortItems() to keep the job queue in FIFO order based
 * on the timestamp encoded in each spool file name.
 *
 * @param a  First job to compare.
 * @param b  Second job to compare.
 * @return Negative if \a a precedes \a b, zero if equal, positive otherwise.
 */
int Folder::AscendingByTime(const Job* a, const Job* b) {
	return a->Time() - b->Time();
}

/**
 * @brief Creates a Job from \a entry and appends it to the internal list.
 *
 * If the resulting Job passes InitCheck(), it is added to fJobs and,
 * when \a notify is true, the listener is informed via Notify().
 *
 * @param entry   BEntry of the spool file to add.
 * @param notify  If true, the FolderListener is notified of the addition.
 * @return true if the job was valid and added, false otherwise.
 */
bool Folder::AddJob(BEntry& entry, bool notify) {
	Job* job = new Job(entry, this);
	if (job->InitCheck() == B_OK) {
		fJobs.AddItem(job);
		if (notify) Notify(job, kJobAdded);
		return true;
	} else {
		job->Release();
		return false;
	}
}

/**
 * @brief Finds the Job whose node_ref matches the given reference.
 *
 * Performs a linear scan of fJobs. The assumption that ino_t uniquely
 * identifies a job file may break for hard links across volumes.
 *
 * @param node  The node_ref to search for.
 * @return Pointer to the matching Job, or NULL if not found.
 */
Job* Folder::Find(node_ref* node) {
	for (int i = 0; i < fJobs.CountItems(); i ++) {
		Job* job = fJobs.ItemAt(i);
		if (job->NodeRef() == *node) return job;
	}
	return NULL;
}

/**
 * @brief FolderListener callback: a new entry has been created in the spool directory.
 *
 * Constructs a BEntry from \a entry, adds a corresponding Job to the list, and
 * re-sorts it by ascending creation time.
 *
 * @param node   node_ref of the newly created file (unused directly here).
 * @param entry  entry_ref of the newly created spool file.
 */
void Folder::EntryCreated(node_ref* node, entry_ref* entry) {
	BEntry job(entry);
	if (job.InitCheck() == B_OK && Lock()) {
		if (AddJob(job)) {
			fJobs.SortItems(AscendingByTime);
		}
		Unlock();
	}
}

/**
 * @brief FolderListener callback: an entry has been removed from the spool directory.
 *
 * Finds the Job corresponding to \a node, removes it from fJobs, notifies the
 * listener, and releases the job's reference count.
 *
 * @param node  node_ref of the removed spool file.
 */
void Folder::EntryRemoved(node_ref* node) {
	Job* job = Find(node);
	if (job && Lock()) {
		fJobs.RemoveItem(job);
		Notify(job, kJobRemoved);
		job->Release();
		Unlock();
	}
}

/**
 * @brief FolderListener callback: an attribute has changed on a spool file.
 *
 * Finds the Job corresponding to \a node, asks it to re-read its status
 * attribute, and notifies the listener of the change.
 *
 * @param node  node_ref of the spool file whose attribute changed.
 */
void Folder::AttributeChanged(node_ref* node) {
	Job* job = Find(node);
	if (job && Lock()) {
		job->UpdateAttribute();
		Notify(job, kJobAttrChanged);
		Unlock();
	}
}

/**
 * @brief Populates the initial job list by scanning the spool directory.
 *
 * Iterates all entries already present in the directory, creates Job objects
 * for each without notification, and sorts the resulting list by creation time.
 */
void Folder::SetupJobList() {
	if (inherited::Folder()->InitCheck() == B_OK) {
		inherited::Folder()->Rewind();

		BEntry entry;
		while (inherited::Folder()->GetNextEntry(&entry) == B_OK) {
			AddJob(entry, false);
		}
		fJobs.SortItems(AscendingByTime);
	}
}

/**
 * @brief Constructs a Folder that watches \a spoolDir for print jobs.
 *
 * Installs itself as the FolderListener, acquires the lock, and performs an
 * initial scan of the directory to populate the job queue.
 *
 * @param locker    The BLocker used to serialise access to the job list.
 * @param looper    The BLooper that receives node-monitor messages.
 * @param spoolDir  The spool directory to watch.
 */
Folder::Folder(BLocker* locker, BLooper* looper, const BDirectory& spoolDir)
	: FolderWatcher(looper, spoolDir, true)
	, fLocker(locker)
	, fJobs()
{
	SetListener(this);
	if (Lock()) {
		SetupJobList();
		Unlock();
	}
}


/**
 * @brief Destroys the Folder and releases all owned Job objects.
 */
Folder::~Folder() {
	if (!Lock()) return;
		// release jobs
	for (int i = 0; i < fJobs.CountItems(); i ++) {
		Job* job = fJobs.ItemAt(i);
		job->Release();
	}
	Unlock();
}

/**
 * @brief Returns the next waiting job from the queue, acquiring it.
 *
 * Scans fJobs in order and returns the first valid job whose status is
 * kWaiting. The job's reference count is incremented before returning so
 * the caller must call Release() when done.
 *
 * @return A pointer to the next waiting Job, or NULL if none is available.
 */
Job* Folder::GetNextJob() {
	for (int i = 0; i < fJobs.CountItems(); i ++) {
		Job* job = fJobs.ItemAt(i);
		if (job->IsValid() && job->IsWaiting()) {
			job->Acquire(); return job;
		}
	}
	return NULL;
}
