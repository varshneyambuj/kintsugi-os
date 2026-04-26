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

/** @file RepositoriesView.h
    @brief Main pane for the Repositories preflet, hosting the repo list. */

#ifndef REPOSITORIES_VIEW_H
#define REPOSITORIES_VIEW_H


#include <ColumnListView.h>
#include <GroupView.h>
#include <String.h>
#include <StringView.h>
#include <View.h>

#include "RepositoriesSettings.h"
#include "RepoRow.h"
#include "TaskLooper.h"


/**
 * @brief BColumnListView specialisation that maps the Delete key to a
 *        DELETE_KEY_PRESSED window message.
 */
class RepositoriesListView : public BColumnListView {
public:
							RepositoriesListView(const char* name);
	virtual void			KeyDown(const char* bytes, int32 numBytes);
};


/**
 * @brief Top-level view of the Repositories preflet.
 *
 * Hosts the repository list, the Add/Remove/Enable/Disable buttons, and the
 * inline status indicator. Owns a TaskLooper that runs pkgman enable and
 * disable jobs asynchronously, and persists the user's repository list via
 * RepositoriesSettings.
 */
class RepositoriesView : public BGroupView {
public:
							RepositoriesView();
							~RepositoriesView();
	virtual void			AllAttached();
	virtual void			AttachedToWindow();
	virtual void			MessageReceived(BMessage*);
	void					AddManualRepository(BString url);
	/** @brief True while at least one pkgman task is in flight. */
	bool					IsTaskRunning() { return fRunningTaskCount > 0; }

private:
	// Message helpers
	void					_AddSelectedRowsToQueue();
	void					_TaskStarted(RepoRow* rowItem, int16 count);
	void					_TaskCompleted(RepoRow* rowItem, int16 count,
								BString& newName);
	void					_TaskCanceled(RepoRow* rowItem, int16 count);
	void					_ShowCompletedStatusIfDone();
	void					_UpdateFromRepoConfig(RepoRow* rowItem);

	// GUI functions
	status_t				_EmptyList();
	void					_InitList();
	void					_RefreshList();
	void					_UpdateListFromRoster();
	void					_SaveList();
	RepoRow*				_AddRepo(BString name, BString url, bool enabled);
	void					_FindSiblings();
	void					_UpdateButtons();
	void					_UpdateStatusView();
	
	RepositoriesSettings	fSettings;
	RepositoriesListView*	fListView;
	BView*					fStatusContainerView;
	BStringView*			fListStatusView;
	TaskLooper*				fTaskLooper;
	bool					fShowCompletedStatus;
	int						fRunningTaskCount, fLastCompletedTimerId;
	BButton*				fAddButton;
	BButton*				fRemoveButton;
	BButton*				fEnableButton;
	BButton*				fDisableButton;
};


#endif
