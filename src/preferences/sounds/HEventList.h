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
 * MIT License. Copyright 2003-2008, Haiku Inc.
 * Original authors: Jérôme Duval, Oliver Ruiz Dorantes, Atsushi Takamatsu.
 */

/** @file HEventList.h
    @brief Two-column list of system events and their bound wav paths. */

#ifndef __HEVENTLIST_H__
#define __HEVENTLIST_H__


#include <ColumnListView.h>
#include <String.h>


/** @brief Column index of the system event name. */
/** @brief Column index of the bound sound file's leaf name. */
enum {
	kEventColumn,
	kSoundColumn,
};


/**
 * @brief Single-row binding of an event name to the wav file that plays it.
 */
class HEventRow : public BRow {
public:
								HEventRow(const char* event_name,
									const char* path);
	virtual						~HEventRow();

			/** @brief Returns the system event name (e.g. "Beep"). */
			const char*			Name() const { return fName.String(); }
			/** @brief Returns the bound wav path; empty when unbound. */
			const char*			Path() const { return fPath.String(); }
			void				Remove(const char* type);
			void				SetPath(const char* path);

private:
			BString				fName;
			BString				fPath;
};


/** @brief Notification sent to the window when the selected event changes. */
enum {
	M_EVENT_CHANGED = 'SCAG'
};


/**
 * @brief BColumnListView that lists the events of a BMediaFiles type and
 *        their bound wav files.
 */
class HEventList : public BColumnListView {
public:
								HEventList(const char* name = "EventList");
	virtual						~HEventList();
			void				RemoveAll();
			void				SetType(const char* type);
			void				SetPath(const char* path);

protected:
	virtual	void				SelectionChanged();

private:
			char*				fType;	
};


#endif	// __HEVENTLIST_H__
