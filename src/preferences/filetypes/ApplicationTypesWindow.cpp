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
 *   Copyright 2006-2010, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file ApplicationTypesWindow.cpp
 * @brief Implementation of the application MIME-types browser window and
 *        of the modal progress dialog used while pruning application
 *        signatures whose executables are no longer installed on disk.
 *
 * @todo  Think about adopting Tracker's info window style here
 *        (pressable path).
 */


#include "ApplicationTypesWindow.h"
#include "FileTypes.h"
#include "FileTypesWindow.h"
#include "MimeTypeListView.h"
#include "StringView.h"

#include <AppFileInfo.h>
#include <Application.h>
#include <Bitmap.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Query.h>
#include <Roster.h>
#include <Screen.h>
#include <ScrollView.h>
#include <StatusBar.h>
#include <StringFormat.h>
#include <StringView.h>
#include <TextView.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <stdio.h>
#include <strings.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Application Types Window"


/**
 * @brief Modal progress window with abort button used while iterating
 *        the application list looking for stale signatures.
 */
class ProgressWindow : public BWindow {
	public:
		ProgressWindow(const char* message, int32 max,
			volatile bool* signalQuit);
		virtual ~ProgressWindow();

		virtual void MessageReceived(BMessage* message);

	private:
		BStatusBar*		fStatusBar;
		BButton*		fAbortButton;
		volatile bool*	fQuitListener;
};

/** @brief Sent by the list view when the selected row changes. */
const uint32 kMsgTypeSelected = 'typs';
/** @brief Sent when an item is double-clicked or otherwise invoked. */
const uint32 kMsgTypeInvoked = 'typi';
/** @brief Triggers the "Remove uninstalled" sweep. */
const uint32 kMsgRemoveUninstalled = 'runs';
/** @brief Triggers the per-application editor (Edit button). */
const uint32 kMsgEdit = 'edit';


/**
 * @brief Maps a B_*_VERSION variety constant to its translated label.
 *
 * @param variety  Variety code as found in version_info.
 * @return         Translated, user-facing string. "-" when @a variety
 *                 is unknown.
 */
const char*
variety_to_text(uint32 variety)
{
	switch (variety) {
		case B_DEVELOPMENT_VERSION:
			return B_TRANSLATE("Development");
		case B_ALPHA_VERSION:
			return B_TRANSLATE("Alpha");
		case B_BETA_VERSION:
			return B_TRANSLATE("Beta");
		case B_GAMMA_VERSION:
			return B_TRANSLATE("Gamma");
		case B_GOLDEN_MASTER_VERSION:
			return B_TRANSLATE("Golden master");
		case B_FINAL_VERSION:
			return B_TRANSLATE("Final");
	}

	return "-";
}


//	#pragma mark -


/**
 * @brief Constructs the centred progress window.
 *
 * @param message     Localised text shown above the progress bar.
 * @param max         Total number of steps.
 * @param signalQuit  Pointer to a flag the worker polls to honour the
 *                    Abort button. May be NULL.
 */
ProgressWindow::ProgressWindow(const char* message,
	int32 max, volatile bool* signalQuit)
	:
	BWindow(BRect(0, 0, 300, 200), B_TRANSLATE("Progress"), B_MODAL_WINDOW_LOOK,
		B_MODAL_SUBSET_WINDOW_FEEL, B_ASYNCHRONOUS_CONTROLS |
			B_NOT_V_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fQuitListener(signalQuit)
{
	char count[100];
	snprintf(count, sizeof(count), "/%" B_PRId32, max);

	fStatusBar = new BStatusBar("status", message, count);
	fStatusBar->SetMaxValue(max);
	fAbortButton = new BButton("abort", B_TRANSLATE("Abort"),
		new BMessage(B_CANCEL));

	float padding = be_control_look->DefaultItemSpacing();
	BLayoutBuilder::Group<>(this, B_VERTICAL, padding)
		.SetInsets(padding, padding, padding, padding)
		.Add(fStatusBar)
		.Add(fAbortButton);

	// center on screen
	BScreen screen(this);
	MoveTo(screen.Frame().left + (screen.Frame().Width()
			- Bounds().Width()) / 2.0f,
		screen.Frame().top + (screen.Frame().Height()
			- Bounds().Height()) / 2.0f);
}


/**
 * @brief Destructor; layout-managed children are released by BWindow.
 */
ProgressWindow::~ProgressWindow()
{
}


/**
 * @brief Routes B_UPDATE_STATUS_BAR ticks and the B_CANCEL action; on
 *        cancel the abort button is greyed out and the externally
 *        owned quit flag is raised.
 *
 * @param message  Incoming BMessage.
 */
void
ProgressWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_UPDATE_STATUS_BAR:
			char count[100];
			snprintf(count, sizeof(count), "%" B_PRId32,
				(int32)fStatusBar->CurrentValue() + 1);

			fStatusBar->Update(1, NULL, count);
			break;

		case B_CANCEL:
			fAbortButton->SetEnabled(false);
			if (fQuitListener != NULL)
				*fQuitListener = true;
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


//	#pragma mark -


/**
 * @brief Builds the application types browser at the frame stored in
 *        @a settings.
 *
 * Lays out the application list on the left and the Information,
 * Version, and action buttons groups on the right, then registers for
 * MIME database notifications and resets to the empty selection state.
 *
 * @param settings  Persistent settings BMessage carrying frame and view
 *                  state fields.
 */
ApplicationTypesWindow::ApplicationTypesWindow(const BMessage& settings)
	: BWindow(_Frame(settings), B_TRANSLATE("Application types"),
		B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
{
	float padding = be_control_look->DefaultItemSpacing();
	BAlignment labelAlignment = be_control_look->DefaultLabelAlignment();
	BAlignment fullWidthTopAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_TOP);

	// Application list

	fTypeListView = new MimeTypeListView("listview", "application", true, true);
	fTypeListView->SetSelectionMessage(new BMessage(kMsgTypeSelected));
	fTypeListView->SetInvocationMessage(new BMessage(kMsgTypeInvoked));

	BScrollView* scrollView = new BScrollView("scrollview", fTypeListView,
		B_FRAME_EVENTS | B_WILL_DRAW, false, true);

	BButton* button = new BButton("remove", B_TRANSLATE("Remove uninstalled"),
		new BMessage(kMsgRemoveUninstalled));

	// "Information" group

	BBox* infoBox = new BBox((char*)NULL);
	infoBox->SetLabel(B_TRANSLATE("Information"));
	infoBox->SetExplicitAlignment(fullWidthTopAlignment);

	fNameView = new StringView(B_TRANSLATE("Name:"), NULL);
	fNameView->TextView()->SetExplicitAlignment(labelAlignment);
	fNameView->LabelView()->SetExplicitAlignment(labelAlignment);
	fSignatureView = new StringView(B_TRANSLATE("Signature:"), NULL);
	fSignatureView->TextView()->SetExplicitAlignment(labelAlignment);
	fSignatureView->LabelView()->SetExplicitAlignment(labelAlignment);
	fSignatureView->TextView()->SetExplicitMinSize(BSize(
		fSignatureView->TextView()->StringWidth("M") * 42, B_SIZE_UNSET));
	fPathView = new StringView(B_TRANSLATE("Path:"), NULL);
	fPathView->TextView()->SetExplicitAlignment(labelAlignment);
	fPathView->LabelView()->SetExplicitAlignment(labelAlignment);

	BLayoutBuilder::Grid<>(infoBox, padding, padding)
		.SetInsets(padding, padding * 2, padding, padding)
		.Add(fNameView->LabelView(), 0, 0)
		.Add(fNameView->TextView(), 1, 0, 2)
		.Add(fSignatureView->LabelView(), 0, 1)
		.Add(fSignatureView->TextView(), 1, 1, 2)
		.Add(fPathView->LabelView(), 0, 2)
		.Add(fPathView->TextView(), 1, 2, 2);

	// "Version" group

	BBox* versionBox = new BBox("");
	versionBox->SetLabel(B_TRANSLATE("Version"));
	versionBox->SetExplicitAlignment(fullWidthTopAlignment);

	fVersionView = new StringView(B_TRANSLATE("Version:"), NULL);
	fVersionView->TextView()->SetExplicitAlignment(labelAlignment);
	fVersionView->LabelView()->SetExplicitAlignment(labelAlignment);
	fDescriptionLabel = new StringView(B_TRANSLATE("Description:"), NULL);
	fDescriptionLabel->LabelView()->SetExplicitAlignment(labelAlignment);
	fDescriptionView = new BTextView("description");
	fDescriptionView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fDescriptionView->SetLowColor(fDescriptionView->ViewColor());
	fDescriptionView->MakeEditable(false);

	BLayoutBuilder::Grid<>(versionBox, padding, padding)
		.SetInsets(padding, padding * 2, padding, padding)
		.Add(fVersionView->LabelView(), 0, 0)
		.Add(fVersionView->TextView(), 1, 0)
		.Add(fDescriptionLabel->LabelView(), 0, 1)
		.Add(fDescriptionView, 1, 1, 2, 2);

	// Launch and Tracker buttons

	fEditButton = new BButton(B_TRANSLATE("Edit" B_UTF8_ELLIPSIS),
		new BMessage(kMsgEdit));
	// launch and tracker buttons get messages in _SetType()
	fLaunchButton = new BButton(B_TRANSLATE("Launch"));
	fTrackerButton = new BButton(
		B_TRANSLATE("Show in Tracker" B_UTF8_ELLIPSIS));


	BLayoutBuilder::Group<>(this, B_HORIZONTAL, padding)
		.AddGroup(B_VERTICAL, padding, 3)
			.Add(scrollView)
			.AddGroup(B_HORIZONTAL)
				.Add(button)
				.AddGlue()
				.End()
			.End()
		.AddGroup(B_VERTICAL, padding)
			.Add(infoBox)
			.Add(versionBox)
			.AddGroup(B_HORIZONTAL, padding)
				.Add(fEditButton)
				.Add(fLaunchButton)
				.Add(fTrackerButton)
				.AddGlue()
				.End()
			.AddGlue(10.0)
			.End()

		.SetInsets(B_USE_WINDOW_SPACING);

	BMimeType::StartWatching(this);
	_SetType(NULL);
}


/**
 * @brief Stops MIME database watching and tears the window down.
 */
ApplicationTypesWindow::~ApplicationTypesWindow()
{
	BMimeType::StopWatching(this);
}


/**
 * @brief Returns the saved window frame from @a settings or a default
 *        rectangle if none is recorded.
 *
 * @param settings  Persistent settings message.
 * @return          Frame rectangle in screen coordinates.
 */
BRect
ApplicationTypesWindow::_Frame(const BMessage& settings) const
{
	BRect rect;
	if (settings.FindRect("app_types_frame", &rect) == B_OK)
		return rect;

	return BRect(100.0f, 100.0f, 540.0f, 480.0f);
}


/**
 * @brief Walks the full list of application signatures and removes
 *        every entry whose executable is not found by a BEOS:APP_SIG
 *        query on any mounted, query-capable volume.
 *
 * Runs on the looper thread; pumps the looper periodically via
 * UpdateIfNeeded() so MIME change events can be processed during the
 * sweep. Reports the number of removed entries via an info alert at
 * the end.
 *
 * @note This is invoked from the "Remove uninstalled" button.
 */
void
ApplicationTypesWindow::_RemoveUninstalled()
{
	// Note: this runs in the looper's thread, which isn't that nice

	int32 removed = 0;
	volatile bool quit = false;

	BWindow* progressWindow =
		new ProgressWindow(
			B_TRANSLATE("Removing uninstalled application types"),
			fTypeListView->FullListCountItems(), &quit);
	progressWindow->AddToSubset(this);
	progressWindow->Show();

	for (int32 i = fTypeListView->FullListCountItems(); i-- > 0 && !quit;) {
		MimeTypeItem* item = dynamic_cast<MimeTypeItem*>
			(fTypeListView->FullListItemAt(i));
		progressWindow->PostMessage(B_UPDATE_STATUS_BAR);

		if (item == NULL)
			continue;

		// search for application on all volumes

		bool found = false;

		BVolumeRoster volumeRoster;
		BVolume volume;
		while (volumeRoster.GetNextVolume(&volume) == B_OK) {
			if (!volume.KnowsQuery())
				continue;

			BQuery query;
			query.PushAttr("BEOS:APP_SIG");
			query.PushString(item->Type());
			query.PushOp(B_EQ);

			query.SetVolume(&volume);
			query.Fetch();

			entry_ref ref;
			if (query.GetNextRef(&ref) == B_OK) {
				found = true;
				break;
			}
		}

		if (!found) {
			BMimeType mimeType(item->Type());
			mimeType.Delete();

			removed++;

			// We're blocking the message loop that received the MIME changes,
			// so we dequeue all waiting messages from time to time
			if (removed % 10 == 0)
				UpdateIfNeeded();
		}
	}

	progressWindow->PostMessage(B_QUIT_REQUESTED);

	static BStringFormat format(B_TRANSLATE("{0, plural, "
		"one{# Application type could be removed} "
		"other{# Application types could be removed}}"));
	BString message;
	format.Format(message, removed);

	error_alert(message, B_OK, B_INFO_ALERT);
}


/**
 * @brief Updates the right-hand pane to reflect the application
 *        signature @a type.
 *
 * When @a type is NULL the pane is cleared and all action buttons are
 * disabled. Otherwise the application's name, signature, path,
 * version, and long description are read via BAppFileInfo and rendered.
 * The Launch and Show-In-Tracker buttons are wired to send refs to the
 * Tracker application.
 *
 * @param type         Signature to display, or NULL to clear.
 * @param forceUpdate  Bitmask of B_*_CHANGED flags hinting which
 *                     subsections need re-reading. B_EVERYTHING_CHANGED
 *                     is implied when @a type differs from the previous
 *                     selection.
 */
void
ApplicationTypesWindow::_SetType(BMimeType* type, int32 forceUpdate)
{
	if (type == NULL) {
		fCurrentType.Unset();

		// Information group
		fNameView->SetText(NULL);
		fNameView->SetEnabled(false);
		fSignatureView->SetText(NULL);
		fSignatureView->SetEnabled(false);
		fPathView->SetText(NULL);
		fPathView->SetEnabled(false);

		// Version group
		fVersionView->SetText(NULL);
		fVersionView->SetEnabled(false);
		fDescriptionView->SetText(NULL);
		fDescriptionLabel->SetEnabled(true);

		// Buttons
		fEditButton->SetEnabled(false);
		fLaunchButton->SetMessage(NULL);
		fLaunchButton->SetEnabled(false);
		fTrackerButton->SetMessage(NULL);
		fTrackerButton->SetEnabled(false);

		return;
	}

	if (fCurrentType == *type) {
		if (!forceUpdate)
			return;
	} else
		forceUpdate = B_EVERYTHING_CHANGED;

	if (&fCurrentType != type)
		fCurrentType.SetTo(type->Type());

	fSignatureView->SetText(type->Type());
	fSignatureView->SetEnabled(true);

	if ((forceUpdate & B_SHORT_DESCRIPTION_CHANGED) != 0) {
		char description[B_MIME_TYPE_LENGTH];

		if (type->GetShortDescription(description) != B_OK) {
			fNameView->SetText("");
			fNameView->SetEnabled(false);
		} else {
			fNameView->SetText(description);
			fNameView->SetEnabled(true);
		}
	}

	if ((forceUpdate & B_APP_HINT_CHANGED) != 0) {
		bool appInfoFound = false;
		entry_ref ref;

		if (be_roster->FindApp(fCurrentType.Type(), &ref) == B_OK) {
			// Set launch message
			BMessenger tracker("application/x-vnd.Be-TRAK");
			BMessage* message = new BMessage(B_REFS_RECEIVED);
			message->AddRef("refs", &ref);

			fLaunchButton->SetMessage(message);
			fLaunchButton->SetTarget(tracker);
			fLaunchButton->SetEnabled(true);

			// update version information

			BFile file(&ref, B_READ_ONLY);
			if (file.InitCheck() == B_OK) {
				fEditButton->SetEnabled(true);

				BAppFileInfo appInfo(&file);
				version_info versionInfo;
				if (appInfo.InitCheck() == B_OK
					&& appInfo.GetVersionInfo(&versionInfo, B_APP_VERSION_KIND)
						== B_OK) {
					char version[256];
					snprintf(version, sizeof(version),
						"%" B_PRIu32 ".%" B_PRIu32 ".%" B_PRIu32 ", %s/%" B_PRIu32,
						versionInfo.major, versionInfo.middle,
						versionInfo.minor,
						variety_to_text(versionInfo.variety),
						versionInfo.internal);

					fVersionView->SetText(version);
					fVersionView->SetEnabled(true);
					fDescriptionView->SetText(versionInfo.long_info);
					fDescriptionLabel->SetEnabled(true);

					appInfoFound = true;
				}
			} else {
				fEditButton->SetEnabled(false);
			}
		} else {
			fEditButton->SetEnabled(false);
			fLaunchButton->SetMessage(NULL);
			fLaunchButton->SetEnabled(false);
		}

		if (!appInfoFound) {
			fVersionView->SetText(NULL);
			fVersionView->SetEnabled(false);
			fDescriptionView->SetText(NULL);
			fDescriptionLabel->SetEnabled(false);
		}

		BPath path(&ref);
		if (path.InitCheck() == B_OK) {
			// Set path
			path.GetParent(&path);
			fPathView->SetText(path.Path());
			fPathView->SetEnabled(true);

			// Set "Show In Tracker" message
			BEntry entry(path.Path());
			entry_ref directoryRef;
			if (entry.GetRef(&directoryRef) == B_OK) {
				BMessenger tracker("application/x-vnd.Be-TRAK");
				BMessage* message = new BMessage(B_REFS_RECEIVED);
				message->AddRef("refs", &directoryRef);

				fTrackerButton->SetMessage(message);
				fTrackerButton->SetTarget(tracker);
				fTrackerButton->SetEnabled(true);
			} else {
				fTrackerButton->SetMessage(NULL);
				fTrackerButton->SetEnabled(false);
			}
		} else {
			fPathView->SetText(NULL);
			fPathView->SetEnabled(false);
			fTrackerButton->SetMessage(NULL);
			fTrackerButton->SetEnabled(false);
		}
	}
}


/**
 * @brief Routes list selection, edit/launch invocation, the prune
 *        action, and incremental MIME database notifications.
 *
 * @param message  Incoming BMessage.
 */
void
ApplicationTypesWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgTypeSelected:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				MimeTypeItem* item = (MimeTypeItem*)fTypeListView->ItemAt(index);
				if (item != NULL) {
					BMimeType type(item->Type());
					_SetType(&type);
				} else
					_SetType(NULL);
			}
			break;
		}

		case kMsgTypeInvoked:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				MimeTypeItem* item = (MimeTypeItem*)fTypeListView->ItemAt(index);
				if (item != NULL) {
					BMimeType type(item->Type());
					entry_ref ref;
					if (type.GetAppHint(&ref) == B_OK) {
						BMessage refs(B_REFS_RECEIVED);
						refs.AddRef("refs", &ref);

						be_app->PostMessage(&refs);
					}
				}
			}
			break;
		}

		case kMsgEdit:
			fTypeListView->Invoke();
			break;

		case kMsgRemoveUninstalled:
			_RemoveUninstalled();
			break;

		case B_META_MIME_CHANGED:
		{
			const char* type;
			int32 which;
			if (message->FindString("be:type", &type) != B_OK
				|| message->FindInt32("be:which", &which) != B_OK) {
				break;
			}

			if (fCurrentType.Type() == NULL)
				break;

			if (!strcasecmp(fCurrentType.Type(), type)) {
				if (which != B_MIME_TYPE_DELETED)
					_SetType(&fCurrentType, which);
				else
					_SetType(NULL);
			}
			break;
		}

		default:
			BWindow::MessageReceived(message);
	}
}


/**
 * @brief Persists the current window frame and notifies the BApplication
 *        that this window is closing.
 *
 * @return Always true.
 */
bool
ApplicationTypesWindow::QuitRequested()
{
	BMessage update(kMsgSettingsChanged);
	update.AddRect("app_types_frame", Frame());
	be_app_messenger.SendMessage(&update);

	be_app->PostMessage(kMsgApplicationTypesWindowClosed);
	return true;
}


