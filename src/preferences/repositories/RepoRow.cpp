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
 *   Copyright 2017 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Brian Hill
 */


/**
 * @file RepoRow.cpp
 * @brief Implementation of RepoRow, a single row entry in the repositories
 *        ColumnListView.
 *
 * Each row tracks the repository name, URL, enabled flag, and the current
 * task state (queued, running, or not in the queue). The Status column
 * mirrors that state and is refreshed whenever any of those values change.
 */


#include "RepoRow.h"

#include <Catalog.h>
#include <ColumnTypes.h>

#include "constants.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "RepoRow"


/**
 * @brief Constructs a RepoRow with the given name, URL, and enabled state.
 *
 * Populates the three string columns (status, name, URL) and synchronises the
 * status column with the initial enabled state.
 *
 * @param repo_name Repository display name shown in the Name column.
 * @param repo_url  Base URL for the repository shown in the URL column.
 * @param enabled   True if the repository is currently enabled in pkgman.
 */
RepoRow::RepoRow(const char* repo_name, const char* repo_url, bool enabled)
	:
	BRow(),
	fName(repo_name),
	fUrl(repo_url),
	fEnabled(enabled),
	fTaskState(STATE_NOT_IN_QUEUE)
{
	SetField(new BStringField(""), kEnabledColumn);
	SetField(new BStringField(fName.String()), kNameColumn);
	SetField(new BStringField(fUrl.String()), kUrlColumn);
	if (enabled)
		SetEnabled(enabled);
}


/**
 * @brief Updates the row's repository name and refreshes the Name column.
 *
 * @param name New display name for the repository.
 */
void
RepoRow::SetName(const char* name)
{
	BStringField* field = (BStringField*)GetField(kNameColumn);
	field->SetString(name);
	fName.SetTo(name);
	Invalidate();
}


/**
 * @brief Sets the enabled flag and refreshes the status column.
 *
 * @param enabled True if the repository is enabled in pkgman.
 */
void
RepoRow::SetEnabled(bool enabled)
{
	fEnabled = enabled;
	RefreshEnabledField();
}


/**
 * @brief Recomputes the Status column text from the current enabled and
 *        task-state values.
 *
 * Shows the localized "Enabled" tag when the row is idle and enabled, an
 * ellipsis while a task is queued or running, or an empty string otherwise.
 */
void
RepoRow::RefreshEnabledField()
{
	BStringField* field = (BStringField*)GetField(kEnabledColumn);
	if (fTaskState == STATE_NOT_IN_QUEUE)
		field->SetString(fEnabled ? B_TRANSLATE_COMMENT("Enabled",
			"Tag in the Status column") : "");
	else
		field->SetString(B_UTF8_ELLIPSIS);
	Invalidate();
}


/**
 * @brief Sets the row's task-queue state and refreshes the status column.
 *
 * @param state One of the STATE_* constants describing whether this row is
 *              idle, waiting in the queue, or actively running.
 */
void
RepoRow::SetTaskState(uint32 state)
{
	fTaskState = state;
	RefreshEnabledField();
}
