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
 *   Copyright 2026, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file CleanUpAdminDirectoryRequest.cpp
 * @brief Request that removes stale package-manager state and transaction directories.
 *
 * Scans the packages admin directory for state_ and transaction- subdirectories
 * older than a caller-specified timestamp, then asks the user to confirm before
 * recursively removing them.  A minimum number of states to retain ensures the
 * system always has a usable rollback point.
 *
 * @see BInstallationLocationInfo, BRemoveEngine
 */


#include <package/CleanUpAdminDirectoryRequest.h>

#include <Directory.h>
#include <Entry.h>
#include <JobQueue.h>
#include <Path.h>
#include <RemoveEngine.h>
#include <StringList.h>
#include <StringFormat.h>

#include <StringForSize.h>

#include <package/PackagesDirectoryDefs.h>


namespace BPackageKit {


using namespace BPrivate;


/**
 * @brief Internal job that performs the actual admin-directory cleanup.
 *
 * This private job class is created and enqueued by
 * CleanUpAdminDirectoryRequest::CreateInitialJobs().  It collects candidate
 * directories, totals their disk usage, confirms with the user, and then
 * removes each directory via BRemoveEngine.
 */
class CleanUpAdminDirectoryJob : public BJob {
	typedef	BJob				inherited;

public:
	CleanUpAdminDirectoryJob(
		const BContext& context, const BString& title,
		const BInstallationLocationInfo& location,
		time_t cleanupBefore, int32 minStatesToKeep);
	virtual ~CleanUpAdminDirectoryJob();

protected:
	virtual	status_t Execute();

private:
	status_t _GetOldStateDirectories(BStringList& directories);

private:
	const BInstallationLocationInfo& fLocationInfo;
	time_t fCleanupBefore;
	int32 fMinimumStatesToKeep;
};


/**
 * @brief Construct the cleanup job.
 *
 * @param context           Package-kit context for decisions and temp files.
 * @param title             Human-readable job title shown in progress UI.
 * @param location          Installation location info providing the packages directory.
 * @param cleanupBefore     Only directories with mtime strictly before this
 *                          timestamp are candidates for removal.
 * @param minStatesToKeep   Minimum number of state directories to retain even
 *                          if they are older than \a cleanupBefore.
 */
CleanUpAdminDirectoryJob::CleanUpAdminDirectoryJob(const BContext& context,
	const BString& title, const BInstallationLocationInfo& location,
	time_t cleanupBefore, int32 minStatesToKeep)
	:
	inherited(context, title),
	fLocationInfo(location),
	fCleanupBefore(cleanupBefore),
	fMinimumStatesToKeep(minStatesToKeep)
{
}


/**
 * @brief Destructor.
 */
CleanUpAdminDirectoryJob::~CleanUpAdminDirectoryJob()
{
}


/**
 * @brief Collect old directories, prompt the user, and remove them.
 *
 * Calculates the total size of all candidate directories, formats a
 * localised confirmation question, and on approval iterates through the
 * list invoking BRemoveEngine for each entry.
 *
 * @return B_OK on success, B_CANCELED if the user declined, or an error code
 *         if any directory could not be removed.
 */
status_t
CleanUpAdminDirectoryJob::Execute()
{
	BStringList oldStatesAndTransactions;
	status_t status = _GetOldStateDirectories(oldStatesAndTransactions);
	if (status != B_OK)
		return status;
	if (oldStatesAndTransactions.IsEmpty())
		return B_OK;

	size_t totalSize = 0;
	for (int32 i = 0; i < oldStatesAndTransactions.CountStrings(); i++) {
		BDirectory dir(oldStatesAndTransactions.StringAt(i));
		BEntry entry;
		while (dir.GetNextEntry(&entry, true) == B_OK) {
			off_t entrySize;
			if (entry.GetSize(&entrySize) == B_OK)
				totalSize += entrySize;
		}
	}

	static BStringFormat format("{0, plural,"
		"one{Clean up # old state (%size)?} other{Clean up # old states (%size)?}}");

	BString question;
	format.Format(question, oldStatesAndTransactions.CountStrings());

	char buffer[128];
	question.ReplaceAll("%size", string_for_size(totalSize, buffer, sizeof(buffer)));

	bool yes = fContext.DecisionProvider().YesNoDecisionNeeded("", question,
		"yes", "no", "yes");
	if (!yes)
		return B_CANCELED;

	for (int32 i = 0; i < oldStatesAndTransactions.CountStrings(); i++) {
		BString directory = oldStatesAndTransactions.StringAt(i);
		status_t status = BRemoveEngine().RemoveEntry(BRemoveEngine::Entry(
			directory));

		if (status != B_OK) {
			SetErrorString(BString().SetToFormat("Failed to remove %s: %s\n",
				directory.String(), strerror(status)));
			return status;
		}
	}

	return B_OK;
}


/**
 * @brief Populate \a directories with the paths of old state and transaction directories.
 *
 * Iterates the admin directory and categorises entries as either "state_*" or
 * "transaction-*".  Entries newer than fCleanupBefore, newer/equal to the
 * active state, or that would reduce the retained state count below
 * fMinimumStatesToKeep are excluded.  Surviving state entries are sorted so
 * that the caller can trim from the newest end.
 *
 * @param directories  Output list populated with absolute paths of removable directories.
 * @return B_OK on success, or an error code if the admin directory cannot be opened.
 */
status_t
CleanUpAdminDirectoryJob::_GetOldStateDirectories(BStringList& directories)
{
	BDirectory packages(&fLocationInfo.PackagesDirectoryRef());
	BDirectory administrative(&packages, PACKAGES_DIRECTORY_ADMIN_DIRECTORY);
	if (administrative.InitCheck() != B_OK)
		return administrative.InitCheck();

	BStringList states, transactions;
	BEntry entry;
	int32 skippedStates = 0;
	while (administrative.GetNextEntry(&entry) == B_OK) {
		if (!entry.IsDirectory())
			continue;

		time_t mtime;
		if (entry.GetModificationTime(&mtime) != B_OK)
			continue;

		BStringList* list = NULL;
		BString name = entry.Name();
		if (name.StartsWith("transaction-")) {
			if (mtime >= fCleanupBefore)
				continue;

			list = &transactions;
		} else if (name.StartsWith("state_")) {
			if (mtime >= fCleanupBefore) {
				skippedStates++;
				continue;
			}
			if (!fLocationInfo.ActiveStateName().IsEmpty()
					&& name >= fLocationInfo.ActiveStateName()) {
				skippedStates++;
				continue;
			}

			list = &states;
		} else {
			continue;
		}

		list->Add(BPath(&administrative, name).Path());
	}

	states.Sort();
	while (!states.IsEmpty() && skippedStates < fMinimumStatesToKeep) {
		states.Remove(states.CountStrings() - 1);
		skippedStates++;
	}

	directories.MakeEmpty();
	directories.Add(states);
	directories.Add(transactions);
	return B_OK;
}


/**
 * @brief Construct the public request object.
 *
 * @param context           Package-kit context.
 * @param location          Installation location whose admin directory is cleaned.
 * @param cleanupBefore     Remove directories with mtime before this timestamp.
 * @param minStatesToKeep   Minimum number of state directories to preserve.
 */
CleanUpAdminDirectoryRequest::CleanUpAdminDirectoryRequest(const BContext& context,
	const BInstallationLocationInfo& location,
	time_t cleanupBefore, int32 minStatesToKeep)
	:
	inherited(context),
	fLocationInfo(location),
	fCleanupBefore(cleanupBefore),
	fMinimumStatesToKeep(minStatesToKeep)
{
}


/**
 * @brief Destructor.
 */
CleanUpAdminDirectoryRequest::~CleanUpAdminDirectoryRequest()
{
}


/**
 * @brief Create and queue the single cleanup job.
 *
 * @return B_OK if the job was queued successfully, B_NO_INIT if InitCheck()
 *         fails, or B_NO_MEMORY on allocation failure.
 */
status_t
CleanUpAdminDirectoryRequest::CreateInitialJobs()
{
	status_t result = InitCheck();
	if (result != B_OK)
		return B_NO_INIT;

	CleanUpAdminDirectoryJob* cleanUpJob
		= new (std::nothrow) CleanUpAdminDirectoryJob(fContext,
			"", fLocationInfo, fCleanupBefore, fMinimumStatesToKeep);
	if (cleanUpJob == NULL)
		return B_NO_MEMORY;
	if ((result = QueueJob(cleanUpJob)) != B_OK) {
		delete cleanUpJob;
		return result;
	}

	return B_OK;
}


}	// namespace BPackageKit
