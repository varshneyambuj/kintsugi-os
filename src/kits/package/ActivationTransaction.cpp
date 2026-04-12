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
 *   Copyright 2013, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 */


/**
 * @file ActivationTransaction.cpp
 * @brief Serialisable description of a package activation/deactivation request.
 *
 * BActivationTransaction carries the set of packages to activate and deactivate,
 * the target installation location, the current change count (for optimistic
 * concurrency), and the transaction directory name used by the package daemon.
 * It can be archived into and restored from a BMessage for IPC with the daemon.
 *
 * @see BDaemonClient, BCommitTransactionResult
 */


#include <package/ActivationTransaction.h>

#include <new>

#include <Message.h>


namespace BPackageKit {
namespace BPrivate {


/**
 * @brief Default constructor; creates an empty, uninitialised transaction.
 */
BActivationTransaction::BActivationTransaction()
	:
	fLocation(B_PACKAGE_INSTALLATION_LOCATION_ENUM_COUNT),
	fChangeCount(0),
	fTransactionDirectoryName(),
	fPackagesToActivate(),
	fPackagesToDeactivate(),
	fFirstBootProcessing(false)
{
}


/**
 * @brief Reconstruct a transaction from an archived BMessage.
 *
 * Reads all transaction fields from \a archive.  The "first boot processing"
 * field is optional for backwards compatibility.  If any mandatory field is
 * missing or invalid, the error is reported through \a _error.
 *
 * @param archive  BMessage previously produced by Archive().
 * @param _error   Optional out-pointer that receives B_OK on success or an
 *                 error code if deserialisation fails.
 */
BActivationTransaction::BActivationTransaction(BMessage* archive,
	status_t* _error)
	:
	fLocation(B_PACKAGE_INSTALLATION_LOCATION_ENUM_COUNT),
	fChangeCount(0),
	fTransactionDirectoryName(),
	fPackagesToActivate(),
	fPackagesToDeactivate(),
	fFirstBootProcessing(false)
{
	status_t error;
	int32 location;

	if (archive->FindBool("first boot processing", &fFirstBootProcessing)
			!= B_OK)
		fFirstBootProcessing = false; // Field is optional for compatibility.

	if ((error = archive->FindInt32("location", &location)) == B_OK
		&& (error = archive->FindInt64("change count", &fChangeCount)) == B_OK
		&& (error = archive->FindString("transaction",
			&fTransactionDirectoryName)) == B_OK
		&& (error = _ExtractStringList(archive, "activate",
			fPackagesToActivate)) == B_OK
		&& (error = _ExtractStringList(archive, "deactivate",
			fPackagesToDeactivate)) == B_OK) {
		if (location >= 0
			&& location <= B_PACKAGE_INSTALLATION_LOCATION_ENUM_COUNT) {
			fLocation = (BPackageInstallationLocation)location;
		} else
			error = B_BAD_VALUE;
	}

	if (_error != NULL)
		*_error = error;
}


/**
 * @brief Destructor.
 */
BActivationTransaction::~BActivationTransaction()
{
}


/**
 * @brief Check whether the transaction is fully and consistently initialised.
 *
 * @return B_OK if the transaction is valid and ready to submit, or B_BAD_VALUE
 *         if any required field is missing or out of range.
 */
status_t
BActivationTransaction::InitCheck() const
{
	if (fLocation < 0 || fLocation >= B_PACKAGE_INSTALLATION_LOCATION_ENUM_COUNT
		|| fTransactionDirectoryName.IsEmpty()
		|| (fPackagesToActivate.IsEmpty() && fPackagesToDeactivate.IsEmpty())) {
		return B_BAD_VALUE;
	}
	return B_OK;
}


/**
 * @brief Initialise the transaction for a specific location and change count.
 *
 * Sets the installation location, the change count for optimistic concurrency,
 * the transaction directory name, and clears both package lists and the first-
 * boot flag.
 *
 * @param location       Target package installation location.
 * @param changeCount    Change count obtained from the daemon; must match.
 * @param directoryName  Name of the transaction directory in the admin folder.
 * @return B_OK on success, or B_BAD_VALUE if \a location is out of range or
 *         \a directoryName is empty.
 */
status_t
BActivationTransaction::SetTo(BPackageInstallationLocation location,
	int64 changeCount, const BString& directoryName)
{
	if (location < 0 || location >= B_PACKAGE_INSTALLATION_LOCATION_ENUM_COUNT
		|| directoryName.IsEmpty()) {
		return B_BAD_VALUE;
	}

	fLocation = location;
	fChangeCount = changeCount;
	fTransactionDirectoryName = directoryName;
	fPackagesToActivate.MakeEmpty();
	fPackagesToDeactivate.MakeEmpty();
	fFirstBootProcessing = false;

	return B_OK;
}


/**
 * @brief Return the target package installation location.
 *
 * @return The BPackageInstallationLocation set on this transaction.
 */
BPackageInstallationLocation
BActivationTransaction::Location() const
{
	return fLocation;
}


/**
 * @brief Override the installation location directly.
 *
 * @param location  New package installation location value.
 */
void
BActivationTransaction::SetLocation(BPackageInstallationLocation location)
{
	fLocation = location;
}


/**
 * @brief Return the change count stored in this transaction.
 *
 * @return The 64-bit change count used for optimistic concurrency with the daemon.
 */
int64
BActivationTransaction::ChangeCount() const
{
	return fChangeCount;
}


/**
 * @brief Set the change count for optimistic concurrency.
 *
 * @param changeCount  New change count value.
 */
void
BActivationTransaction::SetChangeCount(int64 changeCount)
{
	fChangeCount = changeCount;
}


/**
 * @brief Return the name of the transaction directory.
 *
 * @return Const reference to the transaction directory name string.
 */
const BString&
BActivationTransaction::TransactionDirectoryName() const
{
	return fTransactionDirectoryName;
}


/**
 * @brief Set the name of the transaction directory in the admin folder.
 *
 * @param directoryName  New transaction directory name.
 */
void
BActivationTransaction::SetTransactionDirectoryName(
	const BString& directoryName)
{
	fTransactionDirectoryName = directoryName;
}


/**
 * @brief Return the list of packages to activate.
 *
 * @return Const reference to the BStringList of package names to activate.
 */
const BStringList&
BActivationTransaction::PackagesToActivate() const
{
	return fPackagesToActivate;
}


/**
 * @brief Replace the activate list with \a packages.
 *
 * @param packages  List of package names to activate.
 * @return true if all strings were copied successfully, false on allocation failure.
 */
bool
BActivationTransaction::SetPackagesToActivate(const BStringList& packages)
{
	fPackagesToActivate = packages;
	return fPackagesToActivate.CountStrings() == packages.CountStrings();
}


/**
 * @brief Append a single package name to the activate list.
 *
 * @param package  Package name to add.
 * @return true on success, false if memory allocation failed.
 */
bool
BActivationTransaction::AddPackageToActivate(const BString& package)
{
	return fPackagesToActivate.Add(package);
}


/**
 * @brief Return the list of packages to deactivate.
 *
 * @return Const reference to the BStringList of package names to deactivate.
 */
const BStringList&
BActivationTransaction::PackagesToDeactivate() const
{
	return fPackagesToDeactivate;
}


/**
 * @brief Replace the deactivate list with \a packages.
 *
 * @param packages  List of package names to deactivate.
 * @return true if all strings were copied successfully, false on allocation failure.
 */
bool
BActivationTransaction::SetPackagesToDeactivate(const BStringList& packages)
{
	fPackagesToDeactivate = packages;
	return fPackagesToDeactivate.CountStrings() == packages.CountStrings();
}


/**
 * @brief Append a single package name to the deactivate list.
 *
 * @param package  Package name to add.
 * @return true on success, false if memory allocation failed.
 */
bool
BActivationTransaction::AddPackageToDeactivate(const BString& package)
{
	return fPackagesToDeactivate.Add(package);
}


/**
 * @brief Return whether this transaction is part of first-boot processing.
 *
 * @return true if first-boot processing mode is enabled.
 */
bool
BActivationTransaction::FirstBootProcessing() const
{
	return fFirstBootProcessing;
}


/**
 * @brief Enable or disable first-boot processing mode.
 *
 * @param processingIsOn  true to enable first-boot processing.
 */
void
BActivationTransaction::SetFirstBootProcessing(bool processingIsOn)
{
	fFirstBootProcessing = processingIsOn;
}


/**
 * @brief Serialise the transaction into a BMessage for IPC.
 *
 * Stores all transaction fields, including the package lists and the
 * first-boot flag, into \a archive so that it can be sent to the daemon.
 *
 * @param archive  BMessage to populate.
 * @param deep     Passed through to BArchivable::Archive().
 * @return B_OK on success, or an error code if any field could not be stored.
 */
status_t
BActivationTransaction::Archive(BMessage* archive, bool deep) const
{
	status_t error = BArchivable::Archive(archive, deep);
	if (error != B_OK)
		return error;

	if ((error = archive->AddInt32("location", fLocation)) != B_OK
		|| (error = archive->AddInt64("change count", fChangeCount)) != B_OK
		|| (error = archive->AddString("transaction",
			fTransactionDirectoryName)) != B_OK
		|| (error = archive->AddStrings("activate", fPackagesToActivate))
			!= B_OK
		|| (error = archive->AddStrings("deactivate", fPackagesToDeactivate))
			!= B_OK
		|| (error = archive->AddBool("first boot processing",
			fFirstBootProcessing)) != B_OK) {
		return error;
	}

	return B_OK;
}


/**
 * @brief BArchivable instantiation hook.
 *
 * Called by the archiving framework to reconstruct a BActivationTransaction
 * from a BMessage archive.
 *
 * @param archive  The source BMessage.
 * @return A newly allocated BActivationTransaction, or NULL on validation failure.
 */
/*static*/ BArchivable*
BActivationTransaction::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BActivationTransaction"))
		return new(std::nothrow) BActivationTransaction(archive);
	return NULL;
}


/**
 * @brief Extract a BStringList from a named field in \a archive.
 *
 * A missing field is treated as an empty list rather than an error, which
 * allows older archive formats that lack certain optional lists to load cleanly.
 *
 * @param archive  Source BMessage.
 * @param field    Field name to read.
 * @param _list    Output list populated from the archive field.
 * @return B_OK on success or if the field is absent, otherwise an error code.
 */
/*static*/ status_t
BActivationTransaction::_ExtractStringList(BMessage* archive, const char* field,
	BStringList& _list)
{
	status_t error = archive->FindStrings(field, &_list);
	return error == B_NAME_NOT_FOUND ? B_OK : error;
		// If the field doesn't exist, that's OK.
}


}	// namespace BPrivate
}	// namespace BPackageKit
