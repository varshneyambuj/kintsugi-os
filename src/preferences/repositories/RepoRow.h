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

/** @file RepoRow.h
    @brief Single row entry for the repositories ColumnListView. */

#ifndef REPO_ROW_H
#define REPO_ROW_H


#include <ColumnListView.h>
#include <String.h>


/** @brief Column indices in the repositories list view. */
enum {
	kEnabledColumn,
	kNameColumn,
	kUrlColumn
};


/**
 * @brief One row in the repositories list, representing a single configured
 *        package repository.
 *
 * Holds the repository name, URL, enabled flag, the current task state
 * (idle/queued/running) and whether other rows share the same repository
 * name (siblings). Inline accessors return cached values; mutators update
 * both the field state and the displayed status column.
 */
class RepoRow : public BRow {
public:
								RepoRow(const char* repo_name,
									const char* repo_url, bool enabled);

			/** @brief Returns the repository display name. */
			const char*			Name() const { return fName.String(); }
			void				SetName(const char* name);
			/** @brief Returns the repository base URL. */
			const char*			Url() const { return fUrl.String(); }
			void				SetEnabled(bool enabled);
			void				RefreshEnabledField();
			/** @brief True if the repository is currently enabled. */
			bool				IsEnabled() { return fEnabled; }
			void				SetTaskState(uint32 state);
			/** @brief Returns the current task-queue state for this row. */
			uint32				TaskState() { return fTaskState; }
			/** @brief Records whether another row shares this repository name. */
			void				SetHasSiblings(bool hasSiblings)
									{ fHasSiblings = hasSiblings; }
			/** @brief True when at least one other row shares the name. */
			bool				HasSiblings() { return fHasSiblings; }

private:
			BString				fName;
			BString				fUrl;
			bool				fEnabled;
			uint32				fTaskState;
			bool				fHasSiblings;
};


#endif
