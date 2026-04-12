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
 *   Copyright 2011-2015, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Tappe <zooey@hirschkaefer.de>
 */


/**
 * @file Context.cpp
 * @brief Package-kit execution context providing shared services to jobs and requests.
 *
 * BContext owns a per-session temporary-file directory and provides access to a
 * BDecisionProvider (for interactive yes/no prompts) and a BJobStateListener
 * (for progress reporting).  BDecisionProvider's default implementation
 * automatically accepts the default choice so that non-interactive callers
 * work without subclassing.
 *
 * @see BJob, AddRepositoryRequest
 */


#include <new>

#include <package/Context.h>

#include <Directory.h>
#include <FindDirectory.h>
#include <OS.h>
#include <Path.h>

#include "TempfileManager.h"


namespace BPackageKit {


using BPrivate::TempfileManager;


/**
 * @brief Virtual destructor for the BDecisionProvider interface.
 */
BDecisionProvider::~BDecisionProvider()
{
}


/**
 * @brief Default yes/no decision handler; returns the default choice.
 *
 * Subclasses may override this to present an interactive dialogue.  The
 * base-class implementation simply compares \a defaultChoice to \a yes and
 * returns true if they match, accepting the default non-interactively.
 *
 * @param description   Context string describing the situation.
 * @param question      The specific question being asked.
 * @param yes           The string value that represents an affirmative answer.
 * @param no            The string value that represents a negative answer.
 * @param defaultChoice The pre-selected choice to use when running non-interactively.
 * @return true if \a defaultChoice equals \a yes, false otherwise.
 */
bool
BDecisionProvider::YesNoDecisionNeeded(const BString& description,
	const BString& question, const BString& yes, const BString& no,
	const BString& defaultChoice)
{
	return defaultChoice == yes;
}


/**
 * @brief Construct a context, creating the per-session temp directory.
 *
 * Calls _Initialize() to create a uniquely-named subdirectory inside the
 * system temp directory.  Check InitCheck() after construction to verify
 * that initialisation succeeded.
 *
 * @param decisionProvider   Reference to the decision provider implementation.
 * @param jobStateListener   Reference to the job-state listener for progress callbacks.
 */
BContext::BContext(BDecisionProvider& decisionProvider,
	BSupportKit::BJobStateListener& jobStateListener)
	:
	fDecisionProvider(decisionProvider),
	fJobStateListener(jobStateListener),
	fTempfileManager(NULL)
{
	fInitStatus = _Initialize();
}


/**
 * @brief Destructor; deletes the temporary-file manager (and its directory).
 */
BContext::~BContext()
{
	delete fTempfileManager;
}


/**
 * @brief Return the initialisation status of this context.
 *
 * @return B_OK if the context is ready to use, or an error code if
 *         the temp directory could not be created.
 */
status_t
BContext::InitCheck() const
{
	return fInitStatus;
}


/**
 * @brief Create a new temporary file inside the context's temp directory.
 *
 * The file is named with \a baseName as a prefix followed by a unique suffix.
 * The caller owns the resulting BEntry and is responsible for deleting the file
 * when finished.
 *
 * @param baseName  Prefix for the temporary file name.
 * @param entry     Output BEntry set to the newly created temporary file.
 * @return B_OK on success, B_BAD_VALUE if \a entry is NULL, B_NO_INIT if the
 *         manager was not initialised, or another error code on failure.
 */
status_t
BContext::GetNewTempfile(const BString& baseName, BEntry* entry) const
{
	if (entry == NULL)
		return B_BAD_VALUE;
	if (fTempfileManager == NULL)
		return B_NO_INIT;
	*entry = fTempfileManager->Create(baseName);
	return entry->InitCheck();
}


/**
 * @brief Return the job-state listener associated with this context.
 *
 * @return Reference to the BSupportKit::BJobStateListener.
 */
BSupportKit::BJobStateListener&
BContext::JobStateListener() const
{
	return fJobStateListener;
}


/**
 * @brief Return the decision provider associated with this context.
 *
 * @return Reference to the BDecisionProvider.
 */
BDecisionProvider&
BContext::DecisionProvider() const
{
	return fDecisionProvider;
}


/**
 * @brief Create the per-session temporary directory and configure the manager.
 *
 * Locates B_SYSTEM_TEMP_DIRECTORY, creates a uniquely-named subdirectory
 * (named "pkgkit-context-<thread>-<time>"), and sets it as the base directory
 * for the TempfileManager.
 *
 * @return B_OK on success, B_NO_MEMORY if the manager could not be allocated,
 *         or an error code if the temp directory could not be opened or created.
 */
status_t
BContext::_Initialize()
{
	fTempfileManager = new (std::nothrow) TempfileManager();
	if (fTempfileManager == NULL)
		return B_NO_MEMORY;

	BPath tempPath;
	status_t result = find_directory(B_SYSTEM_TEMP_DIRECTORY, &tempPath, true);
	if (result != B_OK)
		return result;
	BDirectory tempDirectory(tempPath.Path());
	if ((result = tempDirectory.InitCheck()) != B_OK)
		return result;

	BString contextName = BString("pkgkit-context-") << find_thread(NULL)
		<< "-" << system_time();
	BDirectory baseDirectory;
	result = tempDirectory.CreateDirectory(contextName.String(),
		&baseDirectory);
	if (result != B_OK)
		return result;

	fTempfileManager->SetBaseDirectory(baseDirectory);

	return B_OK;
}


}	// namespace BPackageKit
