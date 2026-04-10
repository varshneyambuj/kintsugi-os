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
   DeskbarView - mail_daemon's deskbar menu and view
   
   Copyright 2001 Dr. Zoidberg Enterprises. All rights reserved.
 */
/** @file DeskbarView.h
 *  @brief Deskbar replicant view for the mail daemon status icon. */
#ifndef DESKBAR_VIEW_H
#define DESKBAR_VIEW_H


#include <View.h>

#include "NavMenu.h"


enum {
	kStatusNoMail = 0,
	kStatusNewMail,
	kStatusCount
};

enum MDDeskbarMessages {
	MD_CHECK_SEND_NOW = 'MDra',
	MD_CHECK_FOR_MAILS,
	MD_SEND_MAILS,
	MD_OPEN_NEW,
	MD_OPEN_PREFS,
	MD_REFRESH_QUERY
};

class BPopUpMenu;
class BQuery;
class BDirectory;
class BEntry;
class BPath;

/** @brief Deskbar replicant showing mail status and providing a menu. */
class _EXPORT DeskbarView : public BView {
public:
						/** @brief Construct with a frame rectangle. */
						DeskbarView(BRect frame);
						/** @brief Unarchive constructor. */
						DeskbarView(BMessage* data);
	/** @brief Destructor; frees bitmaps and queries. */
	virtual				~DeskbarView();

	/** @brief Draw the current status icon. */
	virtual void		Draw(BRect updateRect);
	/** @brief Set up colors and refresh the mail query. */
	virtual void		AttachedToWindow();
	/** @brief BArchivable instantiation hook. */
	static DeskbarView*	Instantiate(BMessage* data);
	/** @brief Archive the view for the deskbar. */
	virtual	status_t	Archive(BMessage* data, bool deep = true) const;
	/** @brief Handle right-click context menu. */
	virtual void	 	MouseDown(BPoint);
	/** @brief Handle left-click open and middle-click check. */
	virtual void	 	MouseUp(BPoint);
	/** @brief Handle mail commands and query updates. */
	virtual void		MessageReceived(BMessage* message);
	/** @brief Periodic check for daemon status. */
	virtual void		Pulse();

private:
	bool				_EntryInTrash(const entry_ref*);
	void				_RefreshMailQuery();
	bool				_CreateMenuLinks(BDirectory&, BPath&);
	void				_CreateNewMailQuery(BEntry&);
	BPopUpMenu*			_BuildMenu();
	void				_InitBitmaps();
	status_t			_GetNewQueryRef(entry_ref& ref);

	BBitmap*			fBitmaps[kStatusCount]; /**< Status icon bitmaps indexed by kStatus enum */
	int32				fStatus; /**< Current mail status (no mail or new mail) */

	BList				fNewMailQueries; /**< Live queries tracking new messages */
	int32				fNewMessages; /**< Count of unread messages */

	int32				fLastButtons; /**< Mouse buttons from the last MouseDown */
};

#endif	/* DESKBAR_VIEW_H */
