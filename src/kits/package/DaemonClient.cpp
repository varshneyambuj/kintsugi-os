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
 *   Copyright 2013-2014, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 */


/**
 * @file DaemonClient.cpp
 * @brief IPC client for communicating with the package_daemon.
 *
 * BDaemonClient provides a synchronous API for querying installation-location
 * information, committing package activation transactions, and creating
 * transaction directories.  It lazily initialises a BMessenger to the
 * package_daemon application on first use.
 *
 * @see BActivationTransaction, BCommitTransactionResult, BInstallationLocationInfo
 */


#include <package/DaemonClient.h>

#include <time.h>

#include <Directory.h>
#include <Entry.h>
#include <package/CommitTransactionResult.h>
#include <package/InstallationLocationInfo.h>
#include <package/PackageInfo.h>

#include <package/ActivationTransaction.h>
#include <package/PackagesDirectoryDefs.h>


namespace BPackageKit {
namespace BPrivate {


/**
 * @brief Default constructor; leaves the messenger uninitialised until first use.
 */
BDaemonClient::BDaemonClient()
	:
	fDaemonMessenger()
{
}


/**
 * @brief Destructor.
 */
BDaemonClient::~BDaemonClient()
{
}


/**
 * @brief Query the package daemon for information about an installation location.
 *
 * Sends a B_MESSAGE_GET_INSTALLATION_LOCATION_INFO request to the daemon,
 * including the filesystem root node so that chroot environments are handled
 * correctly, and populates \a _info with the response.
 *
 * @param location  The BPackageInstallationLocation to query.
 * @param _info     Output object populated with location details on success.
 * @return B_OK on success, B_BAD_REPLY if the daemon sent an unexpected reply
 *         type, or an error code on messaging or extraction failure.
 */
status_t
BDaemonClient::GetInstallationLocationInfo(
	BPackageInstallationLocation location, BInstallationLocationInfo& _info)
{
	status_t error = _InitMessenger();
	if (error != B_OK)
		return error;

	BMessage request(B_MESSAGE_GET_INSTALLATION_LOCATION_INFO);
	error = request.AddInt32("location", location);
	if (error != B_OK)
		return error;

	// Get our filesystem root node. If we are in a chroot this is not the same
	// as the package_daemon root node, so we must provide it.
	struct stat st;
	if (stat("/boot", &st) == 0) {
		error = request.AddInt32("volume", st.st_dev);
		if (error != B_OK)
			return error;
		error = request.AddInt64("root", st.st_ino);
		if (error != B_OK)
			return error;
	}

	// send the request
	BMessage reply;
	error = fDaemonMessenger.SendMessage(&request, &reply);
	if (error != B_OK)
		return error;
	if (reply.what != B_MESSAGE_GET_INSTALLATION_LOCATION_INFO_REPLY)
		return B_BAD_REPLY;

	// extract the location info
	int32 baseDirectoryDevice;
	int64 baseDirectoryNode;
	int32 packagesDirectoryDevice;
	int64 packagesDirectoryNode;
	int64 changeCount;
	BPackageInfoSet latestActivePackages;
	BPackageInfoSet latestInactivePackages;
	if ((error = reply.FindInt32("base directory device", &baseDirectoryDevice))
			!= B_OK
		|| (error = reply.FindInt64("base directory node", &baseDirectoryNode))
			!= B_OK
		|| (error = reply.FindInt32("packages directory device",
			&packagesDirectoryDevice)) != B_OK
		|| (error = reply.FindInt64("packages directory node",
			&packagesDirectoryNode)) != B_OK
		|| (error = _ExtractPackageInfoSet(reply, "latest active packages",
			latestActivePackages)) != B_OK
		|| (error = _ExtractPackageInfoSet(reply, "latest inactive packages",
			latestInactivePackages)) != B_OK
		|| (error = reply.FindInt64("change count", &changeCount)) != B_OK) {
		return error;
	}

	BPackageInfoSet currentlyActivePackages;
	error = _ExtractPackageInfoSet(reply, "currently active packages",
		currentlyActivePackages);
	if (error != B_OK && error != B_NAME_NOT_FOUND)
		return error;

	BString activeStateName;
	error = reply.FindString("active state", &activeStateName);
	if (error != B_OK && error != B_NAME_NOT_FOUND)
		return error;

	_info.Unset();
	_info.SetLocation(location);
	_info.SetBaseDirectoryRef(node_ref(baseDirectoryDevice, baseDirectoryNode));
	_info.SetPackagesDirectoryRef(
		node_ref(packagesDirectoryDevice, packagesDirectoryNode));
	_info.SetLatestActivePackageInfos(latestActivePackages);
	_info.SetLatestInactivePackageInfos(latestInactivePackages);
	_info.SetCurrentlyActivePackageInfos(currentlyActivePackages);
	_info.SetActiveStateName(activeStateName);
	_info.SetChangeCount(changeCount);

	return B_OK;
}


/**
 * @brief Submit a package activation transaction to the daemon.
 *
 * Archives \a transaction into a BMessage, sends it to the package daemon,
 * and extracts the result from the reply into \a _result.
 *
 * @param transaction  The fully initialised BActivationTransaction to commit.
 * @param _result      Output object that receives the commit result.
 * @return B_OK if the transaction was accepted and the result extracted,
 *         B_BAD_VALUE if the transaction fails InitCheck(), or an error code
 *         on messaging or reply-extraction failure.
 */
status_t
BDaemonClient::CommitTransaction(const BActivationTransaction& transaction,
	BCommitTransactionResult& _result)
{
	if (transaction.InitCheck() != B_OK)
		return B_BAD_VALUE;

	status_t error = _InitMessenger();
	if (error != B_OK)
		return error;

	// send the request
	BMessage request(B_MESSAGE_COMMIT_TRANSACTION);
	error = transaction.Archive(&request);
	if (error != B_OK)
		return error;

	BMessage reply;
	fDaemonMessenger.SendMessage(&request, &reply);
	if (reply.what != B_MESSAGE_COMMIT_TRANSACTION_REPLY)
		return B_ERROR;

	// extract the result
	return _result.ExtractFromMessage(reply);
}


/**
 * @brief Create a transaction directory and initialise a transaction object.
 *
 * Fetches the current installation-location info, opens the admin directory,
 * creates a uniquely-named "transaction-N" subdirectory, and calls SetTo() on
 * \a _transaction so that it is ready for use.  If SetTo() fails the directory
 * is cleaned up before returning.
 *
 * @param location              Target installation location.
 * @param _transaction          Output transaction initialised with location,
 *                              change count, and directory name.
 * @param _transactionDirectory Output BDirectory set to the new directory.
 * @return B_OK on success, or an error code on any failure.
 */
status_t
BDaemonClient::CreateTransaction(BPackageInstallationLocation location,
	BActivationTransaction& _transaction, BDirectory& _transactionDirectory)
{
	// get an info for the location
	BInstallationLocationInfo info;
	status_t error = GetInstallationLocationInfo(location, info);
	if (error != B_OK)
		return error;

	// open admin directory
	entry_ref entryRef;
	entryRef.device = info.PackagesDirectoryRef().device;
	entryRef.directory = info.PackagesDirectoryRef().node;
	error = entryRef.set_name(PACKAGES_DIRECTORY_ADMIN_DIRECTORY);
	if (error != B_OK)
		return error;

	BDirectory adminDirectory;
	error = adminDirectory.SetTo(&entryRef);
	if (error != B_OK)
		return error;

	// create a transaction directory
	int uniqueId = 1;
	BString directoryName;
	for (;; uniqueId++) {
		directoryName.SetToFormat("transaction-%d", uniqueId);
		if (directoryName.IsEmpty())
			return B_NO_MEMORY;

		error = adminDirectory.CreateDirectory(directoryName,
			&_transactionDirectory);
		if (error == B_OK)
			break;
		if (error != B_FILE_EXISTS)
			return error;
	}

	// init the transaction
	error = _transaction.SetTo(location, info.ChangeCount(), directoryName);
	if (error != B_OK) {
		BEntry entry;
		_transactionDirectory.GetEntry(&entry);
		_transactionDirectory.Unset();
		if (entry.InitCheck() == B_OK)
			entry.Remove();
		return error;
	}

	return B_OK;
}


/**
 * @brief Lazily initialise the BMessenger to the package daemon.
 *
 * Returns immediately if the messenger is already valid; otherwise looks up
 * the daemon by its well-known application signature.
 *
 * @return B_OK if the messenger is valid after this call, or an error code
 *         if the daemon could not be located.
 */
status_t
BDaemonClient::_InitMessenger()
{
	if (fDaemonMessenger.IsValid())
		return B_OK;

		// get the package daemon's address
	status_t error;
	fDaemonMessenger = BMessenger(B_PACKAGE_DAEMON_APP_SIGNATURE, -1, &error);
	return error;
}


/**
 * @brief Extract a set of BPackageInfo objects from a named field in \a message.
 *
 * Iterates over all embedded BMessages stored under \a field, deserialises
 * each into a BPackageInfo, and adds it to \a _infos.  A missing field is
 * treated as an empty set rather than an error.
 *
 * @param message  Source BMessage.
 * @param field    Name of the repeated BMessage field to extract.
 * @param _infos   Output set populated with the deserialised package infos.
 * @return B_OK on success, B_BAD_DATA if the field type is wrong, or an error
 *         code on deserialisation failure.
 */
status_t
BDaemonClient::_ExtractPackageInfoSet(const BMessage& message,
	const char* field, BPackageInfoSet& _infos)
{
	// get the number of items
	type_code type;
	int32 count;
	if (message.GetInfo(field, &type, &count) != B_OK) {
		// the field is missing
		return B_OK;
	}
	if (type != B_MESSAGE_TYPE)
		return B_BAD_DATA;

	for (int32 i = 0; i < count; i++) {
		BMessage archive;
		status_t error = message.FindMessage(field, i, &archive);
		if (error != B_OK)
			return error;

		BPackageInfo info(&archive, &error);
		if (error != B_OK)
			return error;

		error = _infos.AddInfo(info);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


}	// namespace BPrivate
}	// namespace BPackageKit
