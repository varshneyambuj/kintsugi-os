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
 * MIT License. Copyright 2001-2010, Haiku.
 * Original authors: Michael Pfeiffer.
 */

/** @file JobListView.h
    @brief List view and row class for displaying spooled print jobs. */

#ifndef _JOB_LISTVIEW_H
#define _JOB_LISTVIEW_H


#include <Bitmap.h>
#include <ListItem.h>
#include <ListView.h>
#include <String.h>


class Job;
class JobItem;
class SpoolFolder;


/**
 * @brief Single-selection list of pending and recent print jobs.
 *
 * Mirrors the contents of a SpoolFolder and exposes Cancel / Restart
 * helpers that operate on the currently selected JobItem.
 */
class JobListView : public BListView {
	typedef BListView Inherited;
public:
								JobListView(BRect frame);
								~JobListView();

			void				AttachedToWindow();
			void				SetSpoolFolder(SpoolFolder* folder);

			void				AddJob(Job* job);
			void				RemoveJob(Job* job);
			void				UpdateJob(Job* job);

			JobItem* 			SelectedItem() const;

			void				RestartJob();
			void				CancelJob();

private:
			JobItem* 			FindJob(Job* job) const;
};


/**
 * @brief Single row in JobListView, wrapping one Job.
 *
 * Caches the job's display strings and an optional MIME-type icon.
 */
class JobItem : public BListItem {
public:
								JobItem(Job* job);
								~JobItem();

			void				Update();

			void				Update(BView *owner, const BFont *font);
			void				DrawItem(BView *owner, BRect bounds,
									bool complete);

			/** @brief Returns the wrapped Job; reference is owned by the
			    item. */
			Job* 				GetJob() const { return fJob; }

private:
			Job*				fJob;
			BBitmap*			fIcon;
			BString				fName;
			BString				fPages;
			BString				fStatus;
			BString				fSize;
};

#endif // _JOB_LISTVIEW_H
