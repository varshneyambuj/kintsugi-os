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
 *   Copyright 2014, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DwarfLoadingStateHandler.cpp
 * @brief Implementation of the loading-state handler that resolves missing
 *        external DWARF debug-info files via Package Kit or a manual file
 *        chooser.
 *
 * When the DWARF backend reports that an image's external debug-info
 * companion file is unavailable, the orchestrator hands the loading state
 * to this handler. It tries to identify a matching debuginfo package and
 * presents the user with an Install / Locate / Skip choice (or just
 * Locate / Skip when no package is known).
 *
 * @see DwarfImageDebugInfoLoadingState, ImageDebugLoadingStateHandler
 */


#include "DwarfLoadingStateHandler.h"

#include <sys/wait.h>

#include <Entry.h>
#include <InterfaceDefs.h>
#include <Path.h>
#include <package/solver/Solver.h>
#include <package/solver/SolverPackage.h>

#include "AutoDeleter.h"
#include "DwarfFile.h"
#include "DwarfImageDebugInfoLoadingState.h"
#include "package/manager/PackageManager.h"
#include "Tracing.h"
#include "UserInterface.h"


using namespace BPackageKit;
using BPackageKit::BManager::BPrivate::BPackageManager;


/** @brief User-visible action codes returned by the dialog. */
enum {
	/** @brief User chose to install the matching debuginfo package. */
	USER_CHOICE_INSTALL_PACKAGE = 0,
	/** @brief User chose to locate the debug-info file manually. */
	USER_CHOICE_LOCATE_FILE ,
	/** @brief User chose to skip and continue without the file. */
	USER_CHOICE_SKIP
};


/**
 * @brief Default-constructs the handler.
 */
DwarfLoadingStateHandler::DwarfLoadingStateHandler()
	:
	ImageDebugLoadingStateHandler()
{
}


/**
 * @brief Destructor; nothing to release.
 */
DwarfLoadingStateHandler::~DwarfLoadingStateHandler()
{
}


/**
 * @brief Reports whether @a state is a DwarfImageDebugInfoLoadingState.
 *
 * @param state  Backend-specific loading state to test.
 * @return @c true if this handler should run for @a state.
 */
bool
DwarfLoadingStateHandler::SupportsState(
	SpecificImageDebugInfoLoadingState* state)
{
	return dynamic_cast<DwarfImageDebugInfoLoadingState*>(state) != NULL;
}


/**
 * @brief Drives the user dialog for a DWARF loading state.
 *
 * In non-interactive mode, marks the state as "user input provided" without
 * asking. Otherwise, looks up a candidate debuginfo package via Package Kit
 * and shows the Install / Locate / Skip dialog. If the user chooses
 * install, runs @c pkgman; if the install fails the dialog is reshown so
 * the user can retry, locate, or skip. If the user chooses Locate, opens
 * a file chooser and stores the located path. The state's status field is
 * always set to @c DWARF_FILE_LOADING_STATE_USER_INPUT_PROVIDED on exit.
 *
 * @param state      Loading state to act upon; must be a
 *                   DwarfImageDebugInfoLoadingState.
 * @param interface  UI used for prompts and notifications.
 */
void
DwarfLoadingStateHandler::HandleState(
	SpecificImageDebugInfoLoadingState* state, UserInterface* interface)
{
	DwarfImageDebugInfoLoadingState* dwarfState
		= dynamic_cast<DwarfImageDebugInfoLoadingState*>(state);

	if (dwarfState == NULL) {
		ERROR("DwarfLoadingStateHandler::HandleState() passed "
			"non-dwarf state object %p.", state);
		return;
	}

	DwarfFileLoadingState& fileState = dwarfState->GetFileState();

	if (!interface->IsInteractive()) {
		fileState.state = DWARF_FILE_LOADING_STATE_USER_INPUT_PROVIDED;
		return;
	}

	BString requiredPackage;
	try {
		// Package Kit may throw exceptions.
		_GetMatchingDebugInfoPackage(fileState.externalInfoFileName,
			requiredPackage);
	} catch (...) {
		requiredPackage = BString();
	}

	// loop so that the user has a chance to retry or locate the file manually
	// in case package installation fails, e.g. due to transient download
	// issues.
	for (;;) {
		int32 choice;
		BString message;
		if (requiredPackage.IsEmpty()) {
			message.SetToFormat("The debug information file '%s' for "
				"image '%s' is missing. Would you like to locate the file "
				"manually?", fileState.externalInfoFileName.String(),
				fileState.dwarfFile->Name());
			choice = interface->SynchronouslyAskUser("Debug info missing",
				message.String(), "Locate", "Skip", NULL);
			if (choice == 0)
				choice = USER_CHOICE_LOCATE_FILE;
			else if (choice == 1)
				choice = USER_CHOICE_SKIP;
		} else {
			message.SetToFormat("The debug information file '%s' for "
				"image '%s' is missing, but can be found in the package "
				"'%s'. Would you like to install it, or locate the file "
				"manually?", fileState.externalInfoFileName.String(),
				fileState.dwarfFile->Name(), requiredPackage.String());
			choice = interface->SynchronouslyAskUser("Debug info missing",
				message.String(), "Install", "Locate", "Skip");
		}

		if (choice == USER_CHOICE_INSTALL_PACKAGE) {
			// TODO: integrate the package installation functionality directly.
			BString command;
			command.SetToFormat("/bin/pkgman install -y %s",
				requiredPackage.String());
			BString notification;
			notification.SetToFormat("Installing package %s" B_UTF8_ELLIPSIS,
				requiredPackage.String());
			interface->NotifyBackgroundWorkStatus(notification);
			int error = system(command.String());
			if (interface->IsInteractive()) {
				if (WIFEXITED(error)) {
					error = WEXITSTATUS(error);
					if (error == B_OK)
						break;
					message.SetToFormat("Package installation failed: %s.",
						strerror(error));
					interface->NotifyUser("Error", message.String(),
						USER_NOTIFICATION_ERROR);
					continue;
				}
			}
			break;
		} else if (choice == USER_CHOICE_LOCATE_FILE) {
			entry_ref ref;
			interface->SynchronouslyAskUserForFile(&ref);
			BPath path(&ref);
			if (path.InitCheck() == B_OK)
				fileState.locatedExternalInfoPath = path.Path();
			break;
		} else
			break;
	}

	fileState.state = DWARF_FILE_LOADING_STATE_USER_INPUT_PROVIDED;
}


/**
 * @brief Searches the package repositories for a debuginfo package that
 *        provides the missing file.
 *
 * Decomposes @a debugFileName into resolvable name and required version
 * (see _GetResolvableName()), queries Package Kit, and returns the
 * package whose version matches exactly.
 *
 * @param debugFileName  Name of the missing debug-info file as recorded in
 *                       DWARF (typically @c filename(package-version)).
 * @param _packageName   Out parameter receiving the matching package name.
 * @retval B_OK              A package was found.
 * @retval B_BAD_VALUE       @a debugFileName is malformed.
 * @retval B_ENTRY_NOT_FOUND No package matches.
 * @retval other             Errors propagated from Package Kit.
 */
status_t
DwarfLoadingStateHandler::_GetMatchingDebugInfoPackage(
	const BString& debugFileName, BString& _packageName)
{
	BString resolvableName;
	BPackageVersion requiredVersion;
	BPackageManager::ClientInstallationInterface clientInterface;
	BPackageManager::UserInteractionHandler handler;

	BPackageManager packageManager(B_PACKAGE_INSTALLATION_LOCATION_SYSTEM,
		&clientInterface, &handler);
	packageManager.Init(BPackageManager::B_ADD_INSTALLED_REPOSITORIES
		| BPackageManager::B_ADD_REMOTE_REPOSITORIES);
	BObjectList<BSolverPackage> packages;
	status_t error = _GetResolvableName(debugFileName, resolvableName,
		requiredVersion);
	if (error != B_OK)
		return error;

	error = packageManager.Solver()->FindPackages(resolvableName,
		BSolver::B_FIND_IN_PROVIDES, packages);
	if (error != B_OK)
		return error;
	else if (packages.CountItems() == 0)
		return B_ENTRY_NOT_FOUND;

	for (int32 i = 0; i < packages.CountItems(); i++) {
		BSolverPackage* package = packages.ItemAt(i);
		if (requiredVersion.Compare(package->Version()) == 0) {
			_packageName = package->Name();
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Parses a debug-info file name of the form
 *        @c filename(packageName-packageVersion) into components.
 *
 * Builds a resolvable name of the form @c debuginfo:filename(packageName)
 * suitable for @c BSolver::FindPackages() and stores the version in
 * @a _resolvableVersion.
 *
 * @param debugFileName        File name string to parse.
 * @param _resolvableName      Out parameter receiving the resolvable
 *                             expression.
 * @param _resolvableVersion   Out parameter receiving the parsed version.
 * @retval B_OK         Parsing succeeded.
 * @retval B_BAD_VALUE  Required delimiters were missing.
 * @retval other        Errors from BPackageVersion::SetTo().
 */
status_t
DwarfLoadingStateHandler::_GetResolvableName(const BString& debugFileName,
	BString& _resolvableName, BPackageVersion& _resolvableVersion)
{
	BString fileName;
	BString packageName;
	BString packageVersion;

	int32 startIndex = 0;
	int32 endIndex = debugFileName.FindFirst('(');
	if (endIndex < 0)
		return B_BAD_VALUE;

	debugFileName.CopyInto(fileName, 0, endIndex);
	startIndex = endIndex + 1;
	endIndex = debugFileName.FindFirst('-', startIndex);
	if (endIndex < 0)
		return B_BAD_VALUE;

	debugFileName.CopyInto(packageName, startIndex, endIndex - startIndex);
	startIndex = endIndex + 1;
	endIndex = debugFileName.FindFirst(')', startIndex);
	if (endIndex < 0)
		return B_BAD_VALUE;

	debugFileName.CopyInto(packageVersion, startIndex,
		endIndex - startIndex);

	_resolvableName.SetToFormat("debuginfo:%s(%s)", fileName.String(), packageName.String());

	return _resolvableVersion.SetTo(packageVersion);
}
