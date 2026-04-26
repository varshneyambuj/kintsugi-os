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
 *   Copyright 2003-2008 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Jérôme Duval
 *       Oliver Ruiz Dorantes
 *       Atsushi Takamatsu
 */


/**
 * @file HEventList.cpp
 * @brief Two-column list of system event names mapped to wav file paths.
 *
 * Each row pairs a BMediaFiles event name (such as "Beep" or "Startup") with
 * the path to the wav that should play when that event fires. Selection
 * changes are reported up to the parent window via M_EVENT_CHANGED.
 *
 * @see HWindow, BMediaFiles
 */


#include "HEventList.h"

#include <Alert.h>
#include <Catalog.h>
#include <ColumnTypes.h>
#include <Entry.h>
#include <Locale.h>
#include <MediaFiles.h>
#include <Path.h>
#include <stdio.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "HEventList"


/**
 * @brief Constructs a row for the given event with its current sound path.
 *
 * @param name  System event name as registered with BMediaFiles (e.g. "Beep").
 * @param path  Path to the wav file currently bound to the event, or NULL
 *              when no sound is bound; SetPath() picks a "<none>" label in
 *              that case.
 */
HEventRow::HEventRow(const char* name, const char* path)
	:
	BRow(),
	fName(name)
{
	SetField(new BStringField(name), kEventColumn);
	SetPath(path);
}


/**
 * @brief Destructor; the column list view owns the BStringField storage.
 */
HEventRow::~HEventRow()
{
}


/**
 * @brief Updates the path bound to this row and refreshes the visible label.
 *
 * @param _path  Filesystem path of the wav, or NULL to display "<none>".
 */
void
HEventRow::SetPath(const char* _path)
{
	fPath = _path;
	BPath path(_path);
	SetField(new BStringField(_path ? path.Leaf() : B_TRANSLATE("<none>")),
		kSoundColumn);
}


/**
 * @brief Removes this event from BMediaFiles under the given type.
 *
 * @param type  BMediaFiles type (typically BMediaFiles::B_SOUNDS).
 */
void
HEventRow::Remove(const char* type)
{
	BMediaFiles().RemoveItem(type, Name());
}


/**
 * @brief Constructs the list view and adds the Event and Sound columns.
 *
 * @param name  View name forwarded to BColumnListView.
 */
HEventList::HEventList(const char* name)
	:
	BColumnListView(name, B_NAVIGABLE, B_PLAIN_BORDER, true),
	fType(NULL)
{
	AddColumn(new BStringColumn(B_TRANSLATE("Event"), 180, 50, 500,
		B_TRUNCATE_MIDDLE), kEventColumn);
	AddColumn(new BStringColumn(B_TRANSLATE("Sound"), 130, 50, 500,
		B_TRUNCATE_END), kSoundColumn);
}


/**
 * @brief Destroys all rows and releases the cached BMediaFiles type string.
 */
HEventList::~HEventList()
{
	RemoveAll();
	free(fType);
}


/**
 * @brief Repopulates the list with every event of the given BMediaFiles type.
 *
 * @param type  BMediaFiles type whose events should be displayed; typically
 *              BMediaFiles::B_SOUNDS.
 * @note A non-existent or unnamed referenced file produces a "<none>" row
 *       so the user can rebind the slot rather than seeing a stale path.
 */
void
HEventList::SetType(const char* type)
{
	RemoveAll();
	BMediaFiles mfiles;
	mfiles.RewindRefs(type);
	free(fType);
	fType = strdup(type);

	BString name;
	entry_ref ref;
	while (mfiles.GetNextRef(&name,&ref) == B_OK) {
		BPath path(&ref);
		if (path.InitCheck() != B_OK || ref.name == NULL
			|| strcmp(ref.name, "") == 0)
			AddRow(new HEventRow(name.String(), NULL));
		else
			AddRow(new HEventRow(name.String(), path.Path()));
	}

	ResizeAllColumnsToPreferred();
}


/**
 * @brief Deletes every row from the list view.
 */
void
HEventList::RemoveAll()
{
	BRow* row;
	while ((row = RowAt((int32)0, NULL)) != NULL) {
		RemoveRow(row);
		delete row;
	}
}


/**
 * @brief Reacts to the user picking a different event row.
 *
 * Validates that the bound wav still exists; if it does, sends an
 * M_EVENT_CHANGED message to the parent window so it can update the
 * Sound-file menu and Play button. If the file vanished it is removed from
 * BMediaFiles and an alert is shown to the user.
 */
void
HEventList::SelectionChanged()
{
	BColumnListView::SelectionChanged();

	HEventRow* row = (HEventRow*)CurrentSelection();
	if (row != NULL) {
		entry_ref ref;
		BMediaFiles().GetRefFor(fType, row->Name(), &ref);

		BPath path(&ref);
		if (path.InitCheck() == B_OK || ref.name == NULL
			|| strcmp(ref.name, "") == 0) {
			row->SetPath(path.Path());
			UpdateRow(row);
		} else {
			printf("name %s\n", ref.name);
			BMediaFiles().RemoveRefFor(fType, row->Name(), ref);
			BAlert* alert = new BAlert("alert",
				B_TRANSLATE("No such file or directory"), B_TRANSLATE("OK"));
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
			return;
		}
		BMessage msg(M_EVENT_CHANGED);
		msg.AddString("name", row->Name());
		msg.AddString("path", row->Path());
		Window()->PostMessage(&msg);
	}
}


/**
 * @brief Binds @a path to the currently selected event row.
 *
 * Persists the binding via BMediaFiles and updates the visible Sound column.
 *
 * @param path  Filesystem path of the wav to associate with the selection;
 *              may not be NULL.
 */
void
HEventList::SetPath(const char* path)
{
	HEventRow* row = (HEventRow*)CurrentSelection();
	if (row != NULL) {
		entry_ref ref;
		BEntry entry(path);
		entry.GetRef(&ref);
		BMediaFiles().SetRefFor(fType, row->Name(), ref);

		row->SetPath(path);
		UpdateRow(row);
	}
}
