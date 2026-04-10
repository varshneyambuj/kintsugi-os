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

/** @file ProblemWindow.h
 *  @brief Displays dependency-resolution problems and lets the user pick solutions */

#ifndef PROBLEM_WINDOW_H
#define PROBLEM_WINDOW_H


#include <map>
#include <set>

#include <Window.h>


namespace BPackageKit {
	class BSolver;
	class BSolverPackage;
	class BSolverProblem;
	class BSolverProblemSolution;
	class BSolverProblemSolutionElement;
}

using BPackageKit::BSolver;
using BPackageKit::BSolverPackage;
using BPackageKit::BSolverProblem;
using BPackageKit::BSolverProblemSolution;
using BPackageKit::BSolverProblemSolutionElement;

class BButton;
class BGroupView;
class BRadioButton;


/** @brief Modal dialog that presents solver dependency problems and lets the user select solutions */
class ProblemWindow : public BWindow {
public:
			typedef std::set<BSolverPackage*> SolverPackageSet;

public:
	/** @brief Construct the problem window and its UI layout */
								ProblemWindow();
	/** @brief Destructor */
	virtual						~ProblemWindow();

	/** @brief Show the window, block until the user responds, and apply chosen solutions */
			bool				Go(BSolver* solver,
									const SolverPackageSet& packagesAddedByUser,
									const SolverPackageSet&
										packagesRemovedByUser);

	/** @brief Handle window close request */
	virtual	bool				QuitRequested();
	/** @brief Dispatch Cancel, Retry, and solution-update messages */
	virtual	void				MessageReceived(BMessage* message);

private:
			struct Solution;

			typedef std::map<BRadioButton*, Solution> SolutionMap;

private:
			void				_ClearProblemsGui();
			void				_AddProblemsGui(BSolver* solver);
			void				_AddProblem(BSolverProblem* problem,
									const float backgroundTint);
			BString				_SolutionElementText(
									const BSolverProblemSolutionElement*
										element) const;
			bool				_AnySolutionSelected() const;

private:
			sem_id				fDoneSemaphore;     /**< Semaphore to block the caller until the dialog closes */
			bool				fClientWaiting;     /**< True while the caller is blocked on fDoneSemaphore */
			bool				fAccepted;          /**< True if the user clicked Retry */
			BGroupView*			fContainerView;     /**< Scrollable container for problem groups */
			BButton*			fCancelButton;      /**< Cancel button */
			BButton*			fRetryButton;       /**< Retry button (enabled when a solution is selected) */
			SolutionMap			fSolutions;         /**< Maps radio buttons to their solution objects */
			const SolverPackageSet* fPackagesAddedByUser;   /**< User-added packages for label rewriting */
			const SolverPackageSet* fPackagesRemovedByUser; /**< User-removed packages for label rewriting */
};


#endif	// PROBLEM_WINDOW_H
