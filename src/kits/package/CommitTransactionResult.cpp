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
 * @file CommitTransactionResult.cpp
 * @brief Result types returned by the package daemon after a commit transaction.
 *
 * BTransactionIssue encodes a single non-fatal warning produced during a
 * transaction (e.g. a manually modified writable file), while
 * BCommitTransactionResult aggregates the overall error code, diagnostic path
 * strings, and the full list of issues.  Both types can be serialised into and
 * deserialised from BMessage for IPC with the package daemon.
 *
 * @see BDaemonClient, BActivationTransaction
 */


#include <package/CommitTransactionResult.h>

#include <Message.h>

//#include <package/DaemonDefs.h>


namespace BPackageKit {


// #pragma mark - BTransactionIssue


/**
 * @brief Default constructor; creates a transaction issue with placeholder values.
 */
BTransactionIssue::BTransactionIssue()
	:
	fType(B_WRITABLE_FILE_TYPE_MISMATCH),
	fPackageName(),
	fPath1(),
	fPath2(),
 	fSystemError(B_OK),
 	fExitCode(0)
{
}


/**
 * @brief Construct a fully populated transaction issue.
 *
 * @param type         Category of the issue (see BType enum).
 * @param packageName  Name of the package that triggered the issue.
 * @param path1        Primary path relevant to the issue.
 * @param path2        Secondary path relevant to the issue, if any.
 * @param systemError  System error code associated with the issue.
 * @param exitCode     Script exit code, if the issue arose from a script.
 */
BTransactionIssue::BTransactionIssue(BType type, const BString& packageName,
	const BString& path1, const BString& path2, status_t systemError,
	int exitCode)
	:
	fType(type),
	fPackageName(packageName),
	fPath1(path1),
	fPath2(path2),
 	fSystemError(systemError),
 	fExitCode(exitCode)
{
}


/**
 * @brief Copy constructor.
 *
 * @param other  Source issue to copy.
 */
BTransactionIssue::BTransactionIssue(const BTransactionIssue& other)
{
	*this = other;
}


/**
 * @brief Destructor.
 */
BTransactionIssue::~BTransactionIssue()
{
}


/**
 * @brief Return the type category of this issue.
 *
 * @return The BType enum value describing the kind of issue.
 */
BTransactionIssue::BType
BTransactionIssue::Type() const
{
	return fType;
}


/**
 * @brief Return the name of the package that caused this issue.
 *
 * @return Const reference to the package name string.
 */
const BString&
BTransactionIssue::PackageName() const
{
	return fPackageName;
}


/**
 * @brief Return the primary path associated with this issue.
 *
 * @return Const reference to the first path string.
 */
const BString&
BTransactionIssue::Path1() const
{
	return fPath1;
}


/**
 * @brief Return the secondary path associated with this issue.
 *
 * @return Const reference to the second path string.
 */
const BString&
BTransactionIssue::Path2() const
{
	return fPath2;
}


/**
 * @brief Return the system error code stored in this issue.
 *
 * @return The status_t error code.
 */
status_t
BTransactionIssue::SystemError() const
{
	return fSystemError;
}


/**
 * @brief Return the script exit code stored in this issue.
 *
 * @return The integer exit code (meaningful only for script-related issue types).
 */
int
BTransactionIssue::ExitCode() const
{
	return fExitCode;
}


/**
 * @brief Format a human-readable description of this issue.
 *
 * Selects a message template based on the issue type and substitutes
 * the stored path, error, and exit-code values.
 *
 * @return A BString containing the localised issue description.
 */
BString
BTransactionIssue::ToString() const
{
	const char* messageTemplate = "";
	switch (fType) {
		case B_WRITABLE_FILE_TYPE_MISMATCH:
			messageTemplate = "\"%path1%\" cannot be updated automatically,"
				" since its type doesn't match the type of \"%path2%\" which it"
				" is supposed to be updated with."
				" Please perform the update manually if needed.";
			break;
		case B_WRITABLE_FILE_NO_PACKAGE_ATTRIBUTE:
			messageTemplate = "\"%path1%\" cannot be updated automatically,"
				" since it doesn't have a SYS:PACKAGE attribute."
				" Please perform the update manually if needed.";
			break;
		case B_WRITABLE_FILE_OLD_ORIGINAL_FILE_MISSING:
			messageTemplate = "\"%path1%\" cannot be updated automatically,"
				" since \"%path2%\" which we need to compare it with is"
				" missing."
				" Please perform the update manually if needed.";
			break;
		case B_WRITABLE_FILE_OLD_ORIGINAL_FILE_TYPE_MISMATCH:
			messageTemplate = "\"%path1%\" cannot be updated automatically,"
				" since its type doesn't match the type of \"%path2%\" which we"
				" need to compare it with."
				" Please perform the update manually if needed.";
			break;
		case B_WRITABLE_FILE_COMPARISON_FAILED:
			messageTemplate = "\"%path1%\" cannot be updated automatically,"
				" since the comparison with \"%path2%\" failed: %error%."
				" Please perform the update manually if needed.";
			break;
		case B_WRITABLE_FILE_NOT_EQUAL:			// !keep old
			messageTemplate = "\"%path1%\" cannot be updated automatically,"
				" since it was changed manually from previous version"
				" \"%path2%\"."
				" Please perform the update manually if needed.";
			break;
		case B_WRITABLE_SYMLINK_COMPARISON_FAILED:	// !keep old
			messageTemplate = "Symbolic link \"%path1%\" cannot be updated"
				" automatically, since the comparison with \"%path2%\" failed:"
				" %error%."
				" Please perform the update manually if needed.";
			break;
		case B_WRITABLE_SYMLINK_NOT_EQUAL:			// !keep old
			messageTemplate = "Symbolic link \"%path1%\" cannot be updated"
				" automatically, since it was changed manually from previous"
				" version \"%path2%\"."
				" Please perform the update manually if needed.";
			break;
		case B_POST_INSTALL_SCRIPT_NOT_FOUND:
			messageTemplate = "Failed to find post-installation script "
				" \"%path1%\": %error%.";
			break;
		case B_STARTING_POST_INSTALL_SCRIPT_FAILED:
			messageTemplate = "Failed to run post-installation script "
				" \"%path1%\": %error%.";
			break;
		case B_POST_INSTALL_SCRIPT_FAILED:
			messageTemplate = "The post-installation script "
				" \"%path1%\" failed with exit code %exitCode%.";
			break;
		case B_PRE_UNINSTALL_SCRIPT_NOT_FOUND:
			messageTemplate = "Failed to find pre-uninstall script "
				" \"%path1%\": %error%.";
			break;
		case B_STARTING_PRE_UNINSTALL_SCRIPT_FAILED:
			messageTemplate = "Failed to run pre-uninstall script "
				" \"%path1%\": %error%.";
			break;
		case B_PRE_UNINSTALL_SCRIPT_FAILED:
			messageTemplate = "The pre-uninstall script "
				" \"%path1%\" failed with exit code %exitCode%.";
			break;
	}

	BString message(messageTemplate);
	message.ReplaceAll("%path1%", fPath1)
		.ReplaceAll("%path2%", fPath2)
		.ReplaceAll("%error%", strerror(fSystemError))
		.ReplaceAll("%exitCode%", BString() << fExitCode);
	return message;
}


/**
 * @brief Serialise this issue into a BMessage for IPC.
 *
 * @param message  Target BMessage to append fields to.
 * @return B_OK on success, or an error code if any field could not be stored.
 */
status_t
BTransactionIssue::AddToMessage(BMessage& message) const
{
	status_t error;
	if ((error = message.AddInt32("type", (int32)fType)) != B_OK
		|| (error = message.AddString("package", fPackageName)) != B_OK
		|| (error = message.AddString("path1", fPath1)) != B_OK
		|| (error = message.AddString("path2", fPath2)) != B_OK
		|| (error = message.AddInt32("system error", (int32)fSystemError))
				!= B_OK
		|| (error = message.AddInt32("exit code", (int32)fExitCode)) != B_OK) {
			return error;
	}

	return B_OK;
}


/**
 * @brief Populate this issue from a previously serialised BMessage.
 *
 * @param message  Source BMessage containing the serialised issue fields.
 * @return B_OK on success, or an error code if any required field is absent.
 */
status_t
BTransactionIssue::ExtractFromMessage(const BMessage& message)
{
	status_t error;
	int32 type;
	int32 systemError;
	int32 exitCode;
	if ((error = message.FindInt32("type", &type)) != B_OK
		|| (error = message.FindString("package", &fPackageName)) != B_OK
		|| (error = message.FindString("path1", &fPath1)) != B_OK
		|| (error = message.FindString("path2", &fPath2)) != B_OK
		|| (error = message.FindInt32("system error", &systemError)) != B_OK
		|| (error = message.FindInt32("exit code", &exitCode)) != B_OK) {
			return error;
	}

	fType = (BType)type;
	fSystemError = (status_t)systemError;
	fExitCode = (int)exitCode;

	return B_OK;
}


/**
 * @brief Copy-assignment operator.
 *
 * @param other  Source issue to copy from.
 * @return Reference to this issue.
 */
BTransactionIssue&
BTransactionIssue::operator=(const BTransactionIssue& other)
{
	fType = other.fType;
	fPackageName = other.fPackageName;
	fPath1 = other.fPath1;
	fPath2 = other.fPath2;
 	fSystemError = other.fSystemError;
	fExitCode = other.fExitCode;

	return *this;
}


// #pragma mark - BCommitTransactionResult


/**
 * @brief Default constructor; initialises to B_TRANSACTION_INTERNAL_ERROR.
 */
BCommitTransactionResult::BCommitTransactionResult()
	:
	fError(B_TRANSACTION_INTERNAL_ERROR),
	fSystemError(B_ERROR),
	fErrorPackage(),
	fPath1(),
	fPath2(),
	fString1(),
	fString2(),
	fOldStateDirectory(),
	fIssues(10)
{
}


/**
 * @brief Construct a result with an explicit transaction error code.
 *
 * @param error  The BTransactionError to store.
 */
BCommitTransactionResult::BCommitTransactionResult(BTransactionError error)
	:
	fError(error),
	fSystemError(B_ERROR),
	fErrorPackage(),
	fPath1(),
	fPath2(),
	fString1(),
	fString2(),
	fOldStateDirectory(),
	fIssues(10)
{
}


/**
 * @brief Copy constructor.
 *
 * @param other  Source result to copy.
 */
BCommitTransactionResult::BCommitTransactionResult(
	const BCommitTransactionResult& other)
	:
	fError(B_TRANSACTION_INTERNAL_ERROR),
	fSystemError(B_ERROR),
	fErrorPackage(),
	fPath1(),
	fPath2(),
	fString1(),
	fString2(),
	fOldStateDirectory(),
	fIssues(10)
{
	*this = other;
}


/**
 * @brief Destructor.
 */
BCommitTransactionResult::~BCommitTransactionResult()
{
}


/**
 * @brief Reset all fields to their default error state.
 */
void
BCommitTransactionResult::Unset()
{
	fError = B_TRANSACTION_INTERNAL_ERROR;
	fSystemError = B_ERROR;
	fErrorPackage.Truncate(0);
	fPath1.Truncate(0);
	fPath2.Truncate(0);
	fString1.Truncate(0);
	fString2.Truncate(0);
	fOldStateDirectory.Truncate(0);
	fIssues.MakeEmpty();
}


/**
 * @brief Return the number of non-fatal issues stored in this result.
 *
 * @return Count of BTransactionIssue objects.
 */
int32
BCommitTransactionResult::CountIssues() const
{
	return fIssues.CountItems();
}


/**
 * @brief Return the issue at the given index.
 *
 * @param index  Zero-based index into the issues list.
 * @return Const pointer to the BTransactionIssue, or NULL if \a index is out of range.
 */
const BTransactionIssue*
BCommitTransactionResult::IssueAt(int32 index) const
{
	if (index < 0 || index >= CountIssues())
		return NULL;
	return fIssues.ItemAt(index);
}


/**
 * @brief Append a copy of \a issue to the issues list.
 *
 * @param issue  The issue to add.
 * @return true on success, false if memory allocation failed.
 */
bool
BCommitTransactionResult::AddIssue(const BTransactionIssue& issue)
{
	BTransactionIssue* newIssue = new(std::nothrow) BTransactionIssue(issue);
	if (newIssue == NULL || !fIssues.AddItem(newIssue)) {
		delete newIssue;
		return false;
	}
	return true;
}


/**
 * @brief Return the high-level transaction error code.
 *
 * @return B_TRANSACTION_OK if no error occurred, otherwise the BTransactionError value.
 */
BTransactionError
BCommitTransactionResult::Error() const
{
	return fError > 0 ? (BTransactionError)fError : B_TRANSACTION_OK;
}


/**
 * @brief Set the high-level transaction error code.
 *
 * @param error  New BTransactionError value.
 */
void
BCommitTransactionResult::SetError(BTransactionError error)
{
	fError = error;
}


/**
 * @brief Return the underlying system error code.
 *
 * @return The status_t system error associated with the transaction failure.
 */
status_t
BCommitTransactionResult::SystemError() const
{
	return fSystemError;
}


/**
 * @brief Set the underlying system error code.
 *
 * @param error  The status_t system error to store.
 */
void
BCommitTransactionResult::SetSystemError(status_t error)
{
	fSystemError = error;
}


/**
 * @brief Return the name of the package involved in the error, if any.
 *
 * @return Const reference to the error package name string.
 */
const BString&
BCommitTransactionResult::ErrorPackage() const
{
	return fErrorPackage;
}


/**
 * @brief Store the name of the package that caused the transaction error.
 *
 * @param packageName  Package name to store.
 */
void
BCommitTransactionResult::SetErrorPackage(const BString& packageName)
{
	fErrorPackage = packageName;
}


/**
 * @brief Build a complete human-readable error message.
 *
 * Selects a template keyed to the stored BTransactionError, substitutes
 * package name, path, and system-error placeholder tokens, and returns the
 * resulting string.
 *
 * @return A BString containing the formatted error description.
 */
BString
BCommitTransactionResult::FullErrorMessage() const
{
	if (fError == 0)
		return "no error";

	const char* messageTemplate = "";
	switch ((BTransactionError)fError) {
		case B_TRANSACTION_OK:
			messageTemplate = "Everything went fine.";
			break;
		case B_TRANSACTION_NO_MEMORY:
			messageTemplate = "Out of memory.";
			break;
		case B_TRANSACTION_INTERNAL_ERROR:
			messageTemplate = "An internal error occurred. Specifics can be"
				" found in the syslog.";
			break;
		case B_TRANSACTION_INSTALLATION_LOCATION_BUSY:
			messageTemplate = "Another package operation is already in"
				" progress.";
			break;
		case B_TRANSACTION_CHANGE_COUNT_MISMATCH:
			messageTemplate = "The transaction is out of date.";
			break;
		case B_TRANSACTION_BAD_REQUEST:
			messageTemplate = "The requested transaction is invalid.";
			break;
		case B_TRANSACTION_NO_SUCH_PACKAGE:
			messageTemplate = "No such package \"%package%\".";
			break;
		case B_TRANSACTION_PACKAGE_ALREADY_EXISTS:
			messageTemplate = "The to be activated package \"%package%\" does"
				" already exist.";
			break;
		case B_TRANSACTION_FAILED_TO_GET_ENTRY_PATH:
			if (fPath1.IsEmpty()) {
				if (fErrorPackage.IsEmpty()) {
					messageTemplate = "A file path could not be determined:"
						"%error%";
				} else {
					messageTemplate = "While processing package \"%package%\""
						" a file path could not be determined: %error%";
				}
			} else {
				if (fErrorPackage.IsEmpty()) {
					messageTemplate = "The file path for \"%path1%\" could not"
						" be determined: %error%";
				} else {
					messageTemplate = "While processing package \"%package%\""
						" the file path for \"%path1%\" could not be"
						" determined: %error%";
				}
			}
			break;
		case B_TRANSACTION_FAILED_TO_OPEN_DIRECTORY:
			messageTemplate = "Failed to open directory \"%path1%\": %error%";
			break;
		case B_TRANSACTION_FAILED_TO_CREATE_DIRECTORY:
			messageTemplate = "Failed to create directory \"%path1%\": %error%";
			break;
		case B_TRANSACTION_FAILED_TO_REMOVE_DIRECTORY:
			messageTemplate = "Failed to remove directory \"%path1%\": %error%";
			break;
		case B_TRANSACTION_FAILED_TO_MOVE_DIRECTORY:
			messageTemplate = "Failed to move directory \"%path1%\" to"
				" \"%path2%\": %error%";
			break;
		case B_TRANSACTION_FAILED_TO_WRITE_ACTIVATION_FILE:
			messageTemplate = "Failed to write new package activation file"
				" \"%path1%\": %error%";
			break;
		case B_TRANSACTION_FAILED_TO_READ_PACKAGE_FILE:
			messageTemplate = "Failed to read package file \"%path1%\":"
				" %error%";
			break;
		case B_TRANSACTION_FAILED_TO_EXTRACT_PACKAGE_FILE:
			messageTemplate = "Failed to extract \"%path1%\" from package"
				" \"%package%\": %error%";
			break;
		case B_TRANSACTION_FAILED_TO_OPEN_FILE:
			messageTemplate = "Failed to open file \"%path1%\": %error%";
			break;
		case B_TRANSACTION_FAILED_TO_MOVE_FILE:
			messageTemplate = "Failed to move file \"%path1%\" to \"%path2%\":"
				" %error%";
			break;
		case B_TRANSACTION_FAILED_TO_COPY_FILE:
			messageTemplate = "Failed to copy file \"%path1%\" to \"%path2%\":"
				" %error%";
			break;
		case B_TRANSACTION_FAILED_TO_WRITE_FILE_ATTRIBUTE:
			messageTemplate = "Failed to write attribute \"%string1%\" of file"
				" \"%path1%\": %error%";
			break;
		case B_TRANSACTION_FAILED_TO_ACCESS_ENTRY:
			messageTemplate = "Failed to access entry \"%path1%\": %error%";
			break;
		case B_TRANSACTION_FAILED_TO_ADD_GROUP:
			messageTemplate = "Failed to add user group \"%string1%\" required"
				" by package \"%package%\".";
			break;
		case B_TRANSACTION_FAILED_TO_ADD_USER:
			messageTemplate = "Failed to add user \"%string1%\" required"
				" by package \"%package%\".";
			break;
		case B_TRANSACTION_FAILED_TO_ADD_USER_TO_GROUP:
			messageTemplate = "Failed to add user \"%string1%\" to group"
				" \"%string2%\" as required by package \"%package%\".";
			break;
		case B_TRANSACTION_FAILED_TO_CHANGE_PACKAGE_ACTIVATION:
			messageTemplate = "Failed to change the package activation in"
				" packagefs: %error%";
			break;
	}

	BString message(messageTemplate);
	message.ReplaceAll("%package%", fErrorPackage)
		.ReplaceAll("%path1%", fPath1)
		.ReplaceAll("%path2%", fPath2)
		.ReplaceAll("%string1%", fString1)
		.ReplaceAll("%string2%", fString2)
		.ReplaceAll("%error%", strerror(fSystemError));
	return message;
}


/**
 * @brief Return the first diagnostic path stored in this result.
 *
 * @return Const reference to the path1 string.
 */
const BString&
BCommitTransactionResult::Path1() const
{
	return fPath1;
}


/**
 * @brief Store the first diagnostic path.
 *
 * @param path  Path string to store.
 */
void
BCommitTransactionResult::SetPath1(const BString& path)
{
	fPath1 = path;
}


/**
 * @brief Return the second diagnostic path stored in this result.
 *
 * @return Const reference to the path2 string.
 */
const BString&
BCommitTransactionResult::Path2() const
{
	return fPath2;
}


/**
 * @brief Store the second diagnostic path.
 *
 * @param path  Path string to store.
 */
void
BCommitTransactionResult::SetPath2(const BString& path)
{
	fPath2 = path;
}


/**
 * @brief Return the first auxiliary diagnostic string.
 *
 * @return Const reference to the string1 field.
 */
const BString&
BCommitTransactionResult::String1() const
{
	return fString1;
}


/**
 * @brief Store the first auxiliary diagnostic string.
 *
 * @param string  Value to store.
 */
void
BCommitTransactionResult::SetString1(const BString& string)
{
	fString1 = string;
}


/**
 * @brief Return the second auxiliary diagnostic string.
 *
 * @return Const reference to the string2 field.
 */
const BString&
BCommitTransactionResult::String2() const
{
	return fString2;
}


/**
 * @brief Store the second auxiliary diagnostic string.
 *
 * @param string  Value to store.
 */
void
BCommitTransactionResult::SetString2(const BString& string)
{
	fString2 = string;
}


/**
 * @brief Return the path of the old state directory created before the transaction.
 *
 * @return Const reference to the old-state directory path string.
 */
const BString&
BCommitTransactionResult::OldStateDirectory() const
{
	return fOldStateDirectory;
}


/**
 * @brief Store the path of the old state directory.
 *
 * @param directory  Path of the old-state directory to store.
 */
void
BCommitTransactionResult::SetOldStateDirectory(const BString& directory)
{
	fOldStateDirectory = directory;
}


/**
 * @brief Serialise the full result (including all issues) into a BMessage.
 *
 * @param message  Target BMessage to populate.
 * @return B_OK on success, or an error code if any field could not be stored.
 */
status_t
BCommitTransactionResult::AddToMessage(BMessage& message) const
{
	status_t error;
	if ((error = message.AddInt32("error", (int32)fError)) != B_OK
		|| (error = message.AddInt32("system error", (int32)fSystemError))
			!= B_OK
		|| (error = message.AddString("error package", fErrorPackage)) != B_OK
		|| (error = message.AddString("path1", fPath1)) != B_OK
		|| (error = message.AddString("path2", fPath2)) != B_OK
		|| (error = message.AddString("string1", fString1)) != B_OK
		|| (error = message.AddString("string2", fString2)) != B_OK
		|| (error = message.AddString("old state", fOldStateDirectory))
				!= B_OK) {
		return error;
	}

	int32 count = fIssues.CountItems();
	for (int32 i = 0; i < count; i++) {
		const BTransactionIssue* issue = fIssues.ItemAt(i);
		BMessage issueMessage;
		if ((error = issue->AddToMessage(issueMessage)) != B_OK
			|| (error = message.AddMessage("issues", &issueMessage)) != B_OK) {
			return error;
		}
	}

	return B_OK;
}


/**
 * @brief Populate this result from a serialised BMessage received from the daemon.
 *
 * Resets the object via Unset(), then reads all scalar fields and iterates over
 * the embedded issue messages to reconstruct the issues list.
 *
 * @param message  Source BMessage produced by AddToMessage().
 * @return B_OK on success, or an error code if any required field is absent.
 */
status_t
BCommitTransactionResult::ExtractFromMessage(const BMessage& message)
{
	Unset();

	int32 resultError;
	int32 systemError;
	status_t error;
	if ((error = message.FindInt32("error", &resultError)) != B_OK
		|| (error = message.FindInt32("system error", &systemError)) != B_OK
		|| (error = message.FindString("error package", &fErrorPackage)) != B_OK
		|| (error = message.FindString("path1", &fPath1)) != B_OK
		|| (error = message.FindString("path2", &fPath2)) != B_OK
		|| (error = message.FindString("string1", &fString1)) != B_OK
		|| (error = message.FindString("string2", &fString2)) != B_OK
		|| (error = message.FindString("old state", &fOldStateDirectory))
				!= B_OK) {
		return error;
	}

	fError = (BTransactionError)resultError;
	fSystemError = (status_t)systemError;

	BMessage issueMessage;
	for (int32 i = 0; message.FindMessage("issues", i, &issueMessage) == B_OK;
			i++) {
		BTransactionIssue issue;
		error = issue.ExtractFromMessage(issueMessage);
		if (error != B_OK)
			return error;

		if (!AddIssue(issue))
			return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Copy-assignment operator; performs a deep copy of all fields and issues.
 *
 * @param other  Source result to copy from.
 * @return Reference to this result.
 */
BCommitTransactionResult&
BCommitTransactionResult::operator=(const BCommitTransactionResult& other)
{
	Unset();

	fError = other.fError;
	fSystemError = other.fSystemError;
	fErrorPackage = other.fErrorPackage;
	fPath1 = other.fPath1;
	fPath2 = other.fPath2;
	fString1 = other.fString1;
	fString2 = other.fString2;
	fOldStateDirectory = other.fOldStateDirectory;

	for (int32 i = 0; const BTransactionIssue* issue = other.fIssues.ItemAt(i);
			i++) {
		AddIssue(*issue);
	}

	return *this;
}


} // namespace BPackageKit
