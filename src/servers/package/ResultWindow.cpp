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
   /*
    * Copyright 2013, Ingo Weinhold, ingo_weinhold@gmx.de.
    * Distributed under the terms of the MIT License.
    */
 */

/** @file ResultWindow.cpp
 *  @brief Implements the package-change confirmation dialog with install and uninstall lists */



#include "ResultWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <ScrollView.h>
#include <StringView.h>
#include <package/solver/SolverPackage.h>
#include <package/solver/SolverRepository.h>

#include <AutoDeleter.h>
#include <AutoLocker.h>
#include <ViewPort.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PackageResult"

using namespace BPackageKit;


/** @brief Message command code sent when the user clicks "Apply changes". */
static const uint32 kApplyMessage = 'rtry';


/**
 * @brief Constructs the package-change confirmation dialog.
 *
 * Builds the UI layout with a scrollable area for location change groups,
 * Cancel and Apply buttons, and creates the done-semaphore used to
 * synchronize the client thread with the window's event loop.
 *
 * @throws std::bad_alloc If the semaphore cannot be created.
 */
ResultWindow::ResultWindow()
	:
	BWindow(BRect(0, 0, 400, 300), B_TRANSLATE_COMMENT("Package changes",
			"Window title"), B_TITLED_WINDOW_LOOK,
		B_NORMAL_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_NOT_MINIMIZABLE | B_AUTO_UPDATE_SIZE_LIMITS,
		B_ALL_WORKSPACES),
	fDoneSemaphore(-1),
	fClientWaiting(false),
	fAccepted(false),
	fContainerView(NULL),
	fCancelButton(NULL),
	fApplyButton(NULL)

{
	fDoneSemaphore = create_sem(0, "package changes");
	if (fDoneSemaphore < 0)
		throw std::bad_alloc();

	BStringView* topTextView = NULL;
	BViewPort* viewPort = NULL;

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_SMALL_INSETS)
		.Add(topTextView = new BStringView(NULL, B_TRANSLATE(
				"The following additional package changes have to be made:")))
		.Add(new BScrollView(NULL, viewPort = new BViewPort(), 0, false, true))
		.AddGroup(B_HORIZONTAL)
			.Add(fCancelButton = new BButton(B_TRANSLATE("Cancel"),
				new BMessage(B_CANCEL)))
			.AddGlue()
			.Add(fApplyButton = new BButton(B_TRANSLATE("Apply changes"),
				new BMessage(kApplyMessage)))
		.End();

	topTextView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	viewPort->SetChildView(fContainerView = new BGroupView(B_VERTICAL, 0));

	// set small scroll step (large step will be set by the view port)
	font_height fontHeight;
	topTextView->GetFontHeight(&fontHeight);
	float smallStep = ceilf(fontHeight.ascent + fontHeight.descent);
	viewPort->ScrollBar(B_VERTICAL)->SetSteps(smallStep, smallStep);
}


/**
 * @brief Destroys the ResultWindow and deletes the done-semaphore.
 */
ResultWindow::~ResultWindow()
{
	if (fDoneSemaphore >= 0)
		delete_sem(fDoneSemaphore);
}


/**
 * @brief Adds a group of package changes for a single installation location.
 *
 * Creates a visually grouped section showing which packages will be
 * installed and uninstalled at the given location, filtering out packages
 * already handled by the user.
 *
 * @param location               Human-readable location name (e.g. "system").
 * @param packagesToInstall      Packages that will be installed.
 * @param packagesAlreadyAdded   Packages to exclude from the install list.
 * @param packagesToUninstall    Packages that will be uninstalled.
 * @param packagesAlreadyRemoved Packages to exclude from the uninstall list.
 * @return @c true if any package entries were added, @c false if all were filtered.
 */
bool
ResultWindow::AddLocationChanges(const char* location,
	const PackageList& packagesToInstall,
	const PackageSet& packagesAlreadyAdded,
	const PackageList& packagesToUninstall,
	const PackageSet& packagesAlreadyRemoved)
{
	BGroupView* locationGroup = new BGroupView(B_VERTICAL);
	ObjectDeleter<BGroupView> locationGroupDeleter(locationGroup);

	locationGroup->GroupLayout()->SetInsets(B_USE_SMALL_INSETS);

	float backgroundTint = B_NO_TINT;
	if ((fContainerView->CountChildren() & 1) != 0)
		backgroundTint = 1.04;

	locationGroup->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR, backgroundTint);
	locationGroup->SetHighUIColor(B_DOCUMENT_TEXT_COLOR);

	BStringView* locationView = new BStringView(NULL, BString().SetToFormat("in %s:", location));
	locationGroup->AddChild(locationView);
	locationView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	locationView->AdoptParentColors();

	BFont locationFont;
	locationView->GetFont(&locationFont);
	locationFont.SetFace(B_BOLD_FACE);
	locationView->SetFont(&locationFont);

	BGroupLayout* packagesGroup = new BGroupLayout(B_VERTICAL);
	locationGroup->GroupLayout()->AddItem(packagesGroup);
	packagesGroup->SetInsets(20, 0, 0, 0);

	bool packagesAdded = _AddPackages(packagesGroup, packagesToInstall,
		packagesAlreadyAdded, true);
	packagesAdded |= _AddPackages(packagesGroup, packagesToUninstall,
		packagesAlreadyRemoved, false);

	if (!packagesAdded)
		return false;

	fContainerView->AddChild(locationGroup);
	locationGroupDeleter.Detach();

	return true;
}


/**
 * @brief Shows the window and blocks until the user accepts or cancels.
 *
 * Centers the window, displays it, then blocks on a semaphore until the
 * user clicks Apply or Cancel (or closes the window). On acceptance the
 * window is quit and @c true is returned.
 *
 * @return @c true if the user accepted the changes, @c false otherwise.
 */
bool
ResultWindow::Go()
{
	AutoLocker<ResultWindow> locker(this);

	CenterOnScreen();
	Show();

	fAccepted = false;
	fClientWaiting = true;

	locker.Unlock();

	while (acquire_sem(fDoneSemaphore) == B_INTERRUPTED) {
	}

	locker.Lock();
	bool result = false;
	if (locker.IsLocked()) {
		result = fAccepted;
		Quit();
		locker.Detach();
	} else
		PostMessage(B_QUIT_REQUESTED);

	return result;
}


/**
 * @brief Handles the window close request.
 *
 * If a client thread is waiting, hides the window and releases the
 * semaphore (treating close as cancellation) instead of quitting.
 *
 * @return @c true if the window may be destroyed, @c false if it was only hidden.
 */
bool
ResultWindow::QuitRequested()
{
	if (fClientWaiting) {
		Hide();
		fClientWaiting = false;
		release_sem(fDoneSemaphore);
		return false;
	}

	return true;
}


/**
 * @brief Handles UI messages for Cancel and Apply button clicks.
 *
 * @param message The incoming BMessage.
 */
void
ResultWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_CANCEL:
		case kApplyMessage:
			Hide();
			fAccepted = message->what == kApplyMessage;
			fClientWaiting = false;
			release_sem(fDoneSemaphore);
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


/**
 * @brief Populates a layout group with install or uninstall package entries.
 *
 * Iterates the package list, skipping those in the ignore set, and adds
 * a BStringView for each remaining package describing the action.
 *
 * @param packagesGroup  The layout group to add string views to.
 * @param packages       The list of packages to display.
 * @param ignorePackages Set of packages to skip.
 * @param install        @c true for install entries, @c false for uninstall.
 * @return @c true if at least one package was added, @c false otherwise.
 */
bool
ResultWindow::_AddPackages(BGroupLayout* packagesGroup,
	const PackageList& packages, const PackageSet& ignorePackages, bool install)
{
	bool packagesAdded = false;

	for (int32 i = 0; BSolverPackage* package = packages.ItemAt(i);
		i++) {
		if (ignorePackages.find(package) != ignorePackages.end())
			continue;

		BString text;
		if (install) {
			text.SetToFormat(B_TRANSLATE_COMMENT("install package %s from "
					"repository %s\n", "Don't change '%s' variables"),
				package->Info().FileName().String(),
				package->Repository()->Name().String());
		} else {
			text.SetToFormat(B_TRANSLATE_COMMENT("uninstall package %s\n",
					"Don't change '%s' variable"),
				package->VersionedName().String());
		}

		BStringView* packageView = new BStringView(NULL, text);
		packagesGroup->AddView(packageView);
		packageView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
		packageView->AdoptParentColors();

		packagesAdded = true;
	}

	return packagesAdded;
}
