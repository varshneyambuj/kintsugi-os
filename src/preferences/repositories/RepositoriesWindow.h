/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2017, Haiku Inc.
 * Original authors: Brian Hill.
 */

/** @file RepositoriesWindow.h
    @brief Top-level window frame for the Repositories preflet. */

#ifndef REPOSITORIES_WINDOW_H
#define REPOSITORIES_WINDOW_H


#include <Node.h>
#include <Window.h>

#include "AddRepoWindow.h"
#include "RepositoriesSettings.h"
#include "RepositoriesView.h"


/**
 * @brief Main BWindow for the Repositories preflet.
 *
 * Hosts a single RepositoriesView, restores the saved frame, watches the
 * package-repositories directory for outside modifications, and forwards
 * Add-by-URL dialog messages.
 */
class RepositoriesWindow : public BWindow {
public:
							RepositoriesWindow();
							~RepositoriesWindow();
	virtual	bool			QuitRequested();
	virtual void			MessageReceived(BMessage*);

private:
	void					_StartWatching();
	void					_StopWatching();

	RepositoriesSettings	fSettings;
	RepositoriesView*		fView;
	AddRepoWindow*			fAddWindow;
	BMessenger				fMessenger;
	node_ref				fPackageNodeRef;
		// node_ref to watch for changes to package-repositories directory
	status_t				fPackageNodeStatus;
	bool					fWatchingPackageNode;
		// true when package-repositories directory is being watched
};


#endif
