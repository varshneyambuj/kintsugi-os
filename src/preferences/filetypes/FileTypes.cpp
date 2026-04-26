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
 *   Copyright 2006-2007, Axel Dörfler, axeld@pinc-software.de.
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/**
 * @file FileTypes.cpp
 * @brief Entry point and BApplication implementation for the FileTypes
 *        preference app.
 *
 * Hosts the main window factory, the persistent settings store, file panel
 * dispatch, refs/argv handling, cascade placement of per-application type
 * editors, and small free-function helpers (is_application, is_resource,
 * error_alert) shared across the app.
 */


#include "ApplicationTypesWindow.h"
#include "ApplicationTypeWindow.h"
#include "FileTypes.h"
#include "FileTypesWindow.h"
#include "FileTypeWindow.h"

#include <AppFileInfo.h>
#include <Application.h>
#include <Alert.h>
#include <Catalog.h>
#include <Locale.h>
#include <TextView.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>
#include <Resources.h>
#include <Screen.h>

#include <stdio.h>
#include <strings.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "FileTypes"


/** @brief MIME signature used to register and launch the FileTypes app. */
const char* kSignature = "application/x-vnd.Haiku-FileTypes";

/** @brief BMessage what code identifying the on-disk settings blob. */
static const uint32 kMsgFileTypesSettings = 'FTst';
/** @brief Pixel offset between cascaded ApplicationTypeWindow positions. */
static const uint32 kCascadeOffset = 20;


/**
 * @brief Persistent settings store: window frames and view-state flags
 *        kept in a flattened BMessage under the user settings directory.
 */
class Settings {
public:
								Settings();
								~Settings();

			const BMessage&		Message() const { return fMessage; }
			void				UpdateFrom(BMessage* message);

private:
			void				_SetDefaults();
			status_t			_Open(BFile* file, int32 mode);

			BMessage			fMessage;
			bool				fUpdated;
};

/**
 * @brief BApplication subclass that hosts every FileTypes window and the
 *        shared open-file panel.
 */
class FileTypes : public BApplication {
public:
								FileTypes();
	virtual						~FileTypes();

	virtual	void				ReadyToRun();

	virtual	void				RefsReceived(BMessage* message);
	virtual	void				ArgvReceived(int32 argc, char** argv);
	virtual	void				MessageReceived(BMessage* message);

	virtual	bool				QuitRequested();

private:
			void				_WindowClosed();

			// Add one degree of offset to the starting position of the next ApplicationTypeWindow
			void				_AppTypeCascade(BRect lastFrame);

private:
			Settings			fSettings;
			BFilePanel*			fFilePanel;
			BMessenger			fFilePanelTarget;
			FileTypesWindow*	fTypesWindow;
			BWindow*			fApplicationTypesWindow;
			uint32				fWindowCount;
			uint32				fTypeWindowCount;
			BString				fArgvType;
};


/**
 * @brief Loads the persisted settings BMessage from disk, falling back to
 *        defaults when the file is missing or unreadable.
 *
 * On failure to open or unflatten, the default values laid down by
 * _SetDefaults() are kept and @a fUpdated remains false (so no pointless
 * write occurs on destruction).
 */
Settings::Settings()
	:
	fMessage(kMsgFileTypesSettings),
	fUpdated(false)
{
	_SetDefaults();

	BFile file;
	if (_Open(&file, B_READ_ONLY) != B_OK)
		return;

	BMessage settings;
	if (settings.Unflatten(&file) == B_OK) {
		// We don't unflatten into our default message to make sure
		// nothing is lost (because of old or corrupted on disk settings)
		UpdateFrom(&settings);
		fUpdated = false;
	}
}


/**
 * @brief Flushes the settings to disk if anything has been updated since
 *        construction.
 */
Settings::~Settings()
{
	// only save the settings if something has changed
	if (!fUpdated)
		return;

	BFile file;
	if (_Open(&file, B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY) != B_OK)
		return;

	fMessage.Flatten(&file);
}


/**
 * @brief Merges fields found in @a message into the in-memory settings
 *        BMessage and flags it dirty.
 *
 * Only the known keys (window frames, icon/rule visibility flags, split
 * weights) are copied; unknown keys are ignored. The
 * "app_type_initial_frame" key is intentionally not merged because it
 * represents the static origin point used on first launch.
 *
 * @param message  Message containing one or more known settings fields.
 */
void
Settings::UpdateFrom(BMessage* message)
{
	BRect frame;
	if (message->FindRect("file_types_frame", &frame) == B_OK)
		fMessage.ReplaceRect("file_types_frame", frame);

	if (message->FindRect("app_types_frame", &frame) == B_OK)
		fMessage.ReplaceRect("app_types_frame", frame);

	// "app_type_initial_frame" is omitted because it is not meant to be updated

	if (message->FindRect("app_type_next_frame", &frame) == B_OK)
		fMessage.ReplaceRect("app_type_next_frame", frame);

	bool showIcons;
	if (message->FindBool("show_icons", &showIcons) == B_OK)
		fMessage.ReplaceBool("show_icons", showIcons);

	bool showRule;
	if (message->FindBool("show_rule", &showRule) == B_OK)
		fMessage.ReplaceBool("show_rule", showRule);

	float splitWeight;
	if (message->FindFloat("left_split_weight", &splitWeight) == B_OK)
		fMessage.ReplaceFloat("left_split_weight", splitWeight);
	if (message->FindFloat("right_split_weight", &splitWeight) == B_OK)
		fMessage.ReplaceFloat("right_split_weight", splitWeight);

	fUpdated = true;
}


/**
 * @brief Populates the settings BMessage with the factory defaults used
 *        on the first launch or when the on-disk file cannot be read.
 */
void
Settings::_SetDefaults()
{
	fMessage.AddRect("file_types_frame", BRect(80.0f, 80.0f, 600.0f, 480.0f));
	fMessage.AddRect("app_types_frame", BRect(100.0f, 100.0f, 540.0f, 480.0f));
	fMessage.AddRect("app_type_initial_frame", BRect(100.0f, 110.0f, 250.0f, 340.0f));
	fMessage.AddRect("app_type_next_frame", BRect(100.0f, 110.0f, 250.0f, 340.0f));
	fMessage.AddBool("show_icons", true);
	fMessage.AddBool("show_rule", false);
	fMessage.AddFloat("left_split_weight", 0.2);
	fMessage.AddFloat("right_split_weight", 0.8);
}


/**
 * @brief Opens the user settings file backing this Settings instance.
 *
 * @param file  Output BFile that will be initialised on success.
 * @param mode  BFile open flags (e.g. B_READ_ONLY, B_WRITE_ONLY |
 *              B_CREATE_FILE | B_ERASE_FILE).
 * @return      B_OK on success, B_ERROR if the user settings directory
 *              cannot be located, or any error returned by BFile::SetTo.
 */
status_t
Settings::_Open(BFile* file, int32 mode)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return B_ERROR;

	path.Append("FileTypes settings");

	return file->SetTo(path.Path(), mode);
}


//	#pragma mark -


/**
 * @brief Constructs the BApplication and the shared file-open panel.
 */
FileTypes::FileTypes()
	:
	BApplication(kSignature),
	fTypesWindow(NULL),
	fApplicationTypesWindow(NULL),
	fWindowCount(0),
	fTypeWindowCount(0)
{
	fFilePanel = new BFilePanel(B_OPEN_PANEL, NULL, NULL,
		B_FILE_NODE, false);
}


/**
 * @brief Releases the shared open file panel.
 */
FileTypes::~FileTypes()
{
	delete fFilePanel;
}


/**
 * @brief Opens the default FileTypes window when no other window has been
 *        created in response to refs or argv arguments.
 */
void
FileTypes::ReadyToRun()
{
	// are there already windows open?
	if (CountWindows() != 1)
		return;

	// if not, open the FileTypes window
	PostMessage(kMsgOpenTypesWindow);
}


/**
 * @brief Dispatches incoming entry refs to the right editor window.
 *
 * Applications and resource files spawn an ApplicationTypeWindow per
 * entry; remaining refs are aggregated and handed to a single
 * FileTypeWindow. Errors opening any ref produce a translated alert and
 * the offending ref is removed from the message before further processing.
 *
 * Holding the Shift key on launch keeps symlinks unresolved.
 *
 * @param message  Message containing zero or more "refs" entries.
 */
void
FileTypes::RefsReceived(BMessage* message)
{
	bool traverseLinks = (modifiers() & B_SHIFT_KEY) == 0;

	// filter out applications and entries we can't open
	int32 index = 0;
	entry_ref ref;
	while (message->FindRef("refs", index++, &ref) == B_OK) {
		BEntry entry;
		BFile file;

		status_t status = entry.SetTo(&ref, traverseLinks);
		if (status == B_OK)
			status = file.SetTo(&entry, B_READ_ONLY);

		if (status != B_OK) {
			// file cannot be opened

			char buffer[1024];
			snprintf(buffer, sizeof(buffer),
				B_TRANSLATE("Could not open \"%s\":\n"
				"%s"),
				ref.name, strerror(status));

			BAlert* alert = new BAlert(B_TRANSLATE("FileTypes request"),
				buffer, B_TRANSLATE("OK"), NULL, NULL,
				B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();

			message->RemoveData("refs", --index);
			continue;
		}

		if (!is_application(file) && !is_resource(file)) {
			entry_ref target;
			if (entry.GetRef(&target) == B_OK && target != ref)
				message->ReplaceRef("refs", index - 1, &ref);
			continue;
		}

		// remove application from list
		message->RemoveData("refs", --index);

		// There are some refs left that want to be handled by the type window

		BWindow* window = new ApplicationTypeWindow(fSettings.Message(), entry);
		_AppTypeCascade(window->Frame());
			// For accurate height and width, get the frame that results after layouting,
			// instead of the initial frame that's stored in fSettings.
		window->Show();

		fTypeWindowCount++;
		fWindowCount++;
	}

	if (message->FindRef("refs", &ref) != B_OK)
		return;

	// There are some refs left that want to be handled by the type window
	BPoint point(100.0f + kCascadeOffset * fTypeWindowCount,
		110.0f + kCascadeOffset * fTypeWindowCount);

	BWindow* window = new FileTypeWindow(point, *message);
	window->Show();

	fTypeWindowCount++;
	fWindowCount++;
}


/**
 * @brief Translates command-line arguments into a refs message and
 *        forwards them to RefsReceived().
 *
 * Recognises the special form "FileTypes -type <mime>" to preselect a
 * MIME type the next time the main browser is opened. Any other arguments
 * are treated as paths (resolved against the launching CWD when relative)
 * and bundled into a synthetic refs message.
 *
 * @param argc  Argument count.
 * @param argv  Argument vector; argv[0] is the executable path.
 */
void
FileTypes::ArgvReceived(int32 argc, char** argv)
{
	if (argc == 3 && strcmp(argv[1], "-type") == 0) {
		fArgvType = argv[2];
		return;
	}

	BMessage* message = CurrentMessage();

	BDirectory currentDirectory;
	if (message != NULL)
		currentDirectory.SetTo(message->FindString("cwd"));

	BMessage refs;

	for (int i = 1 ; i < argc ; i++) {
		BPath path;
		if (argv[i][0] == '/')
			path.SetTo(argv[i]);
		else
			path.SetTo(&currentDirectory, argv[i]);

		status_t status;
		entry_ref ref;
		BEntry entry;

		if ((status = entry.SetTo(path.Path(), false)) != B_OK
			|| (status = entry.GetRef(&ref)) != B_OK) {
			fprintf(stderr, "Could not open file \"%s\": %s\n",
				path.Path(), strerror(status));
			continue;
		}

		refs.AddRef("refs", &ref);
	}

	RefsReceived(&refs);
}


/**
 * @brief Top-level message dispatch for the FileTypes BApplication.
 *
 * Handles the open-window/window-closed bookkeeping for the main browser
 * and the application-types browser, the shared file panel request, and
 * the silent-relaunch and B_CANCEL hooks needed for clean exit.
 *
 * @param message  Incoming BMessage.
 */
void
FileTypes::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSettingsChanged:
			fSettings.UpdateFrom(message);
			break;

		case kMsgOpenTypesWindow:
			if (fTypesWindow == NULL) {
				fTypesWindow = new FileTypesWindow(fSettings.Message());
				if (fArgvType.Length() > 0) {
					// Set the window to the type that was requested on the
					// command line (-type), we do this only once, if we
					// ever opened more than one FileTypesWindow.
					fTypesWindow->SelectType(fArgvType.String());
					fArgvType = "";
				}
				fTypesWindow->Show();
				fWindowCount++;
			} else
				fTypesWindow->Activate(true);
			break;
		case kMsgTypesWindowClosed:
			fTypesWindow = NULL;
			_WindowClosed();
			break;

		case kMsgOpenApplicationTypesWindow:
			if (fApplicationTypesWindow == NULL) {
				fApplicationTypesWindow = new ApplicationTypesWindow(
					fSettings.Message());
				fApplicationTypesWindow->Show();
				fWindowCount++;
			} else
				fApplicationTypesWindow->Activate(true);
			break;
		case kMsgApplicationTypesWindowClosed:
			fApplicationTypesWindow = NULL;
			_WindowClosed();
			break;

		case kMsgTypeWindowClosed:
			fTypeWindowCount--;
			// supposed to fall through

		case kMsgWindowClosed:
			_WindowClosed();
			break;


		case kMsgOpenFilePanel:
		{
			// the open file panel sends us a message when it's done
			const char* subTitle;
			if (message->FindString("title", &subTitle) != B_OK)
				subTitle = B_TRANSLATE("Open file");

			int32 what;
			if (message->FindInt32("message", &what) != B_OK)
				what = B_REFS_RECEIVED;

			BMessenger target;
			if (message->FindMessenger("target", &target) != B_OK)
				target = be_app_messenger;

			BString title = B_TRANSLATE_SYSTEM_NAME("FileTypes");
			if (subTitle != NULL && subTitle[0]) {
				title.Append(": ");
				title.Append(subTitle);
			}

			uint32 flavors = B_FILE_NODE;
			if (message->FindBool("allowDirs"))
				flavors |= B_DIRECTORY_NODE;
			fFilePanel->SetNodeFlavors(flavors);


			fFilePanel->SetMessage(new BMessage(what));
			fFilePanel->Window()->SetTitle(title.String());
			fFilePanel->SetTarget(target);

			if (!fFilePanel->IsShowing())
				fFilePanel->Show();
			break;
		}

		case B_SILENT_RELAUNCH:
			// In case we were launched via the add-on, there is no types
			// window yet.
			if (fTypesWindow == NULL)
				PostMessage(kMsgOpenTypesWindow);
			break;

		case B_CANCEL:
			if (fWindowCount == 0)
				PostMessage(B_QUIT_REQUESTED);
			break;

		case B_SIMPLE_DATA:
			RefsReceived(message);
			break;

		default:
			BApplication::MessageReceived(message);
			break;
	}
}


/**
 * @brief Always permits termination; called by Be when the app is asked
 *        to quit.
 *
 * @return Always true.
 */
bool
FileTypes::QuitRequested()
{
	return true;
}


/**
 * @brief Decrements the live window counter and posts B_QUIT_REQUESTED
 *        when no windows remain and the file panel is closed.
 */
void
FileTypes::_WindowClosed()
{
	if (--fWindowCount == 0 && !fFilePanel->IsShowing())
		PostMessage(B_QUIT_REQUESTED);
}


/**
 * @brief Computes the next cascaded ApplicationTypeWindow position from
 *        @a lastFrame and writes it back to the settings store.
 *
 * Wraps the cascade back to the initial frame when an offset would push
 * the window off the right or bottom edge of the screen.
 *
 * @param lastFrame  Frame of the most recently opened ApplicationTypeWindow
 *                   in screen coordinates.
 */
void
FileTypes::_AppTypeCascade(BRect lastFrame)
{
	BScreen screen;
	BRect screenBorder = screen.Frame();
	BRect initFrame;

	float left = lastFrame.left + kCascadeOffset;
	if (left + lastFrame.Width() > screenBorder.right) {
		// If about to cascade off the right edge of the screen, revert the horizontal
		// position to that of the first window.
		if (fSettings.Message().FindRect("app_type_initial_frame", &initFrame) == B_OK)
			left = initFrame.LeftTop().x;
	}

	float top = lastFrame.top + kCascadeOffset;
	if (top + lastFrame.Height() > screenBorder.bottom) {
		if (fSettings.Message().FindRect("app_type_initial_frame", &initFrame) == B_OK)
			top = initFrame.LeftTop().y;
	}

	lastFrame.OffsetTo(BPoint(left, top));
	BMessage update(kMsgSettingsChanged);
	update.AddRect("app_type_next_frame", lastFrame);
	fSettings.UpdateFrom(&update);
}


//	#pragma mark -


/**
 * @brief Tests whether @a file carries the application MIME type.
 *
 * @param file  Open BFile to probe.
 * @return      True when BAppFileInfo reports B_APP_MIME_TYPE,
 *              false otherwise (including when the info is invalid).
 */
bool
is_application(BFile& file)
{
	BAppFileInfo appInfo(&file);
	if (appInfo.InitCheck() != B_OK)
		return false;

	char type[B_MIME_TYPE_LENGTH];
	if (appInfo.GetType(type) != B_OK
		|| strcasecmp(type, B_APP_MIME_TYPE))
		return false;

	return true;
}


/**
 * @brief Tests whether @a file carries the BResources MIME type.
 *
 * @param file  Open BFile to probe.
 * @return      True when the file's node info reports
 *              B_RESOURCE_MIME_TYPE and BResources can read it.
 */
bool
is_resource(BFile& file)
{
	BResources resources(&file);
	if (resources.InitCheck() != B_OK)
		return false;
	
	BNodeInfo nodeInfo(&file);
	char type[B_MIME_TYPE_LENGTH];
	if (nodeInfo.GetType(type) != B_OK
		|| strcasecmp(type, B_RESOURCE_MIME_TYPE))
		return false;

	return true;
}


/**
 * @brief Shows a translated, blocking BAlert reporting an error to the user.
 *
 * Appends a human-readable description of @a status when it is not B_OK.
 * The alert closes on Escape and uses the translated FileTypes title.
 *
 * @param message  User-facing message; may include a short context.
 * @param status   Status code; B_OK suppresses the appended strerror text.
 * @param type     BAlert visual style (info, warning, stop, etc.).
 */
void
error_alert(const char* message, status_t status, alert_type type)
{
	char warning[512];
	if (status != B_OK) {
		snprintf(warning, sizeof(warning), "%s:\n\t%s\n", message,
			strerror(status));
	}

	BAlert* alert = new BAlert(B_TRANSLATE("FileTypes request"),
		status == B_OK ? message : warning,
		B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, type);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
}


/**
 * @brief Process entry point: instantiates the FileTypes BApplication
 *        and runs its message loop until quit.
 *
 * @param argc  Argument count (forwarded to BApplication via ArgvReceived).
 * @param argv  Argument vector.
 * @return      Always 0.
 */
int
main(int argc, char** argv)
{
	FileTypes probe;
	probe.Run();
	return 0;
}
