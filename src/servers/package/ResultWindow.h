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

/** @file ResultWindow.h
 *  @brief Displays the list of additional package changes for user confirmation */

#ifndef RESULT_WINDOW_H
#define RESULT_WINDOW_H


#include <map>
#include <set>

#include <ObjectList.h>
#include <Window.h>


namespace BPackageKit {
	class BSolverPackage;
}

using BPackageKit::BSolverPackage;

class BButton;
class BGroupLayout;
class BGroupView;


/** @brief Confirmation dialog that shows additional package changes the solver requires */
class ResultWindow : public BWindow {
public:
			typedef std::set<BSolverPackage*> PackageSet;
			typedef BObjectList<BSolverPackage> PackageList;

public:
	/** @brief Construct the result window and its scrollable layout */
								ResultWindow();
	/** @brief Destructor */
	virtual						~ResultWindow();

	/** @brief Add install/uninstall entries for a given location; returns true if any were added */
			bool				AddLocationChanges(const char* location,
									const PackageList& packagesToInstall,
									const PackageSet& packagesAlreadyAdded,
									const PackageList& packagesToUninstall,
									const PackageSet& packagesAlreadyRemoved);
	/** @brief Show the window and block until the user accepts or cancels */
			bool				Go();

	/** @brief Handle window close by releasing the blocked caller */
	virtual	bool				QuitRequested();
	/** @brief Dispatch Cancel and Apply messages */
	virtual	void				MessageReceived(BMessage* message);

private:
			bool				_AddPackages(BGroupLayout* packagesGroup,
									const PackageList& packages,
									const PackageSet& ignorePackages,
									bool install);

private:
			sem_id				fDoneSemaphore;  /**< Semaphore to block the caller */
			bool				fClientWaiting;  /**< True while the caller is blocked */
			bool				fAccepted;       /**< True if the user clicked Apply */
			BGroupView*			fContainerView;  /**< Scrollable container for location groups */
			BButton*			fCancelButton;   /**< Cancel button */
			BButton*			fApplyButton;    /**< Apply changes button */
};


#endif	// RESULT_WINDOW_H
