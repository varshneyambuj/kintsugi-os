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
 *   Copyright 2001-2010 Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers (mflerackers@androme.be)
 */


/**
 * @file ZombieReplicantView.cpp
 * @brief Implementation of ZombieReplicantView, a placeholder for failed replicants
 *
 * ZombieReplicantView is displayed in a BShelf when a replicant cannot be
 * instantiated (e.g., because its add-on is missing). It shows an error
 * message and provides a context menu to remove the broken replicant.
 *
 * @see BShelf, BView
 */


#include <Alert.h>
#include <Message.h>
#include <MimeType.h>
#include <String.h>
#include <SystemCatalog.h>

#include "ZombieReplicantView.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>

using BPrivate::gSystemCatalog;

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ZombieReplicantView"

#undef B_TRANSLATE
#define B_TRANSLATE(str) \
	gSystemCatalog.GetString(B_TRANSLATE_MARK(str), "ZombieReplicantView")


/**
 * @brief Constructs a ZombieReplicantView for a replicant that failed to load.
 *
 * Creates a BBox named "<Zombie>" with B_FOLLOW_NONE anchoring and B_WILL_DRAW
 * set.  The view is painted with @c kZombieColor and a small bold font so it is
 * visually distinct from normal views.  The @a error code is stored for display
 * in the B_ABOUT_REQUESTED handler.
 *
 * @param frame  The frame rectangle that the zombie should occupy in its parent view.
 * @param error  The status_t code that describes why instantiation failed; shown
 *               to the user via strerror() when they activate the error alert.
 *
 * @see MessageReceived(), SetArchive()
 */
_BZombieReplicantView_::_BZombieReplicantView_(BRect frame, status_t error)
	:
	BBox(frame, "<Zombie>", B_FOLLOW_NONE, B_WILL_DRAW),
	fError(error)
{
	BFont font(be_bold_font);
	font.SetSize(9.0f); // TODO
	SetFont(&font);
	SetViewColor(kZombieColor);
}


/**
 * @brief Destroys the ZombieReplicantView.
 *
 * The archived BMessage (fArchive) is owned by the BShelf and is not freed here.
 */
_BZombieReplicantView_::~_BZombieReplicantView_()
{
}


/**
 * @brief Handles messages directed at the zombie placeholder view.
 *
 * B_ABOUT_REQUESTED is intercepted to display an alert that explains why the
 * replicant could not be instantiated.  The alert shows either the short
 * description of the missing add-on (looked up via BMimeType) or a generic
 * "no signature" message, together with the human-readable error string from
 * strerror().  All other messages are forwarded to BView::MessageReceived().
 *
 * @param msg The incoming BMessage to handle.
 *
 * @see BView::MessageReceived(), fError, fArchive
 */
void
_BZombieReplicantView_::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case B_ABOUT_REQUESTED:
		{
			const char* addOn = NULL;
			BString error;
			if (fArchive->FindString("add_on", &addOn) == B_OK) {
				char description[B_MIME_TYPE_LENGTH] = "";
				BMimeType type(addOn);
				type.GetShortDescription(description);
				error = B_TRANSLATE("Cannot create the replicant for "
						"\"%description\".\n%error");
				error.ReplaceFirst("%description", description);
			} else
				error = B_TRANSLATE("Cannot locate the application for the "
					"replicant. No application signature supplied.\n%error");

			error.ReplaceFirst("%error", strerror(fError));

			BAlert* alert = new (std::nothrow) BAlert(B_TRANSLATE("Error"),
				error.String(), B_TRANSLATE("OK"), NULL, NULL,
				B_WIDTH_AS_USUAL, B_STOP_ALERT);
			if (alert != NULL) {
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go();
			}

			break;
		}
		default:
			BView::MessageReceived(msg);
	}
}


/**
 * @brief Draws the zombie placeholder content within @a updateRect.
 *
 * Paints a centred "?" character in the bold font so the user can identify
 * the broken slot at a glance, then delegates to BBox::Draw() to render the
 * surrounding box frame and label.
 *
 * @param updateRect The dirty region that needs to be redrawn.
 */
void
_BZombieReplicantView_::Draw(BRect updateRect)
{
	BRect bounds(Bounds());
	font_height fh;

	GetFontHeight(&fh);

	DrawChar('?', BPoint(bounds.Width() / 2.0f - StringWidth("?") / 2.0f,
		bounds.Height() / 2.0f - fh.ascent / 2.0f));

	BBox::Draw(updateRect);
}


/**
 * @brief Absorbs mouse-down events without any action.
 *
 * Mouse clicks on the zombie view are swallowed here to prevent the default
 * BView drag-replicant behaviour from triggering on a broken replicant.
 */
void
_BZombieReplicantView_::MouseDown(BPoint)
{
}


/**
 * @brief Archives the zombie by copying the original failed-replicant archive.
 *
 * Rather than re-archiving the zombie's own state, this method copies the
 * original BMessage that the shelf was trying to instantiate.  This preserves
 * the replicant data so that the shelf can re-try instantiation after a
 * restart or add-on installation.
 *
 * @param archive The BMessage to write into; must be non-NULL.
 * @param[in]    (unnamed bool) Deep flag; ignored because the archive is a
 *               verbatim copy.
 *
 * @return B_OK on success.
 *
 * @see SetArchive()
 */
status_t
_BZombieReplicantView_::Archive(BMessage* archive, bool) const
{
	*archive = *fArchive;

	return B_OK;
}


/**
 * @brief Stores the original replicant archive so it can be re-used by Archive().
 *
 * Called by BShelf after construction to hand the zombie its archived message.
 * The shelf retains ownership of @a archive.
 *
 * @param archive Pointer to the BMessage that BShelf failed to instantiate.
 *
 * @see Archive()
 */
void
_BZombieReplicantView_::SetArchive(BMessage* archive)
{
	fArchive = archive;
}
