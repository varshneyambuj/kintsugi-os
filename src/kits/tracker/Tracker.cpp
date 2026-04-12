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
 *   Open Tracker License
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the Be Sample Code License.
 */

/**
 * @file Tracker.cpp
 * @brief TTracker is the main Tracker BApplication subclass.
 *
 * @see BApplication, TTracker, BWindow
 */

#include "Tracker.h"

#include <errno.h>
#include <fs_attr.h>
#include <fs_info.h>
#include <image.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/resource.h>
#include <unistd.h>

#include <Alert.h>
#include <Autolock.h>
#include <Catalog.h>
#include <Debug.h>
#include <FindDirectory.h>
#include <Locale.h>
#include <MenuItem.h>
#include <NodeInfo.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <PathMonitor.h>
#include <Roster.h>
#include <StopWatch.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <tracker_private.h>

#include "Attributes.h"
#include "AutoLock.h"
#include "BackgroundImage.h"
#include "Bitmaps.h"
#include "Commands.h"
#include "ContainerWindow.h"
#include "DeskWindow.h"
#include "FindPanel.h"
#include "FunctionObject.h"
#include "FSClipboard.h"
#include "FSUtils.h"
#include "InfoWindow.h"
#include "MimeTypes.h"
#include "MimeTypeList.h"
#include "NodePreloader.h"
#include "OpenWithWindow.h"
#include "PoseView.h"
#include "QueryContainerWindow.h"
#include "StatusWindow.h"
#include "TaskLoop.h"
#include "Thread.h"
#include "TrackerSettings.h"
#include "TrackerSettingsWindow.h"
#include "TrackerString.h"
#include "TrashWatcher.h"
#include "VirtualDirectoryWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Tracker"


// prototypes for some private kernel calls that will some day be public
#ifndef _IMPEXP_ROOT
#	define _IMPEXP_ROOT
#endif


const int32 DEFAULT_MON_NUM = 4096;
	// copied from fsil.c

const int8 kOpenWindowNoFlags = 0;
const int8 kOpenWindowMinimized = 1;
const int8 kOpenWindowHasState = 2;

const uint32 PSV_MAKE_PRINTER_ACTIVE_QUIETLY = 'pmaq';
	// from pr_server.h

const int32 kNodeMonitorBumpValue = 512;


namespace BPrivate {

NodePreloader* gPreloader = NULL;


class LaunchLooper : public BLooper {
public:
	LaunchLooper()
		:
		BLooper("launch looper")
	{
	}

	virtual void
	MessageReceived(BMessage* message)
	{
		void (*function)(const entry_ref*, const BMessage*, bool);
		BMessage refs;
		bool openWithOK;
		entry_ref appRef;

		if (message->FindPointer("function", (void**)&function) != B_OK
			|| message->FindMessage("refs", &refs) != B_OK
			|| message->FindBool("openWithOK", &openWithOK) != B_OK) {
			printf("incomplete launch message\n");
			return;
		}

		if (message->FindRef("appRef", &appRef) == B_OK)
			function(&appRef, &refs, openWithOK);
		else
			function(NULL, &refs, openWithOK);
	}
};

BLooper* gLaunchLooper = NULL;


// #pragma mark - functions


void
InitIconPreloader()
{
	static int32 lock = 0;

	if (atomic_add(&lock, 1) != 0) {
		// Just wait for the icon cache to be instantiated
		int32 tries = 20;
		while (IconCache::sIconCache == NULL && tries-- > 0)
			snooze(10000);
		return;
	}

	if (IconCache::sIconCache != NULL)
		return;

	// only start the node preloader if its Tracker or the Deskbar itself,
	// don't start it for file panels

	bool preload = dynamic_cast<TTracker*>(be_app) != NULL;
	if (!preload) {
		// check for deskbar
		app_info info;
		if (be_app->GetAppInfo(&info) == B_OK
			&& !strcmp(info.signature, kDeskbarSignature))
			preload = true;
	}

	if (preload) {
		gPreloader = NodePreloader::InstallNodePreloader("NodePreloader",
			be_app);
	}

	IconCache::sIconCache = new IconCache();

	atomic_add(&lock, -1);
}

}	// namespace BPrivate


uint32
GetVolumeFlags(Model* model)
{
	fs_info info;
	if (model->IsVolume()) {
		// search for the correct volume
		int32 cookie = 0;
		dev_t device;
		while ((device = next_dev(&cookie)) >= B_OK) {
			if (fs_stat_dev(device,&info))
				continue;

			if (!strcmp(info.volume_name,model->Name()))
				return info.flags;
		}
		return B_FS_HAS_ATTR;
	}
	if (!fs_stat_dev(model->NodeRef()->device,&info))
		return info.flags;

	return B_FS_HAS_ATTR;
}


//	#pragma mark - WatchingInterface


class TTracker::WatchingInterface : public BPathMonitor::BWatchingInterface {
public:
	virtual status_t WatchNode(const node_ref* node, uint32 flags,
		const BMessenger& target)
	{
		return TTracker::WatchNode(node, flags, target);
	}

	virtual status_t WatchNode(const node_ref* node, uint32 flags,
		const BHandler* handler, const BLooper* looper = NULL)
	{
		return TTracker::WatchNode(node, flags, BMessenger(handler, looper));
	}
};


//	#pragma mark - TTracker


/**
 * @brief Construct the TTracker application object.
 *
 * Sets the working directory to the user's home directory, requests extra
 * file descriptors for nested copy operations, creates the Desktop window,
 * initialises the icon preloader and trash watcher, and starts the pulse
 * timer for volume space-bar updates.
 */
TTracker::TTracker()
	:
	BApplication(kTrackerSignature),
	fMimeTypeList(NULL),
	fClipboardRefsWatcher(NULL),
	fTrashWatcher(NULL),
	fTaskLoop(NULL),
	fNodeMonitorCount(-1),
	fWatchingInterface(new WatchingInterface),
	fSettingsWindow(NULL)
{
	BPathMonitor::SetWatchingInterface(fWatchingInterface);

	// set the cwd to /boot/home, anything that's launched
	// from Tracker will automatically inherit this
	BPath homePath;

	if (find_directory(B_USER_DIRECTORY, &homePath) == B_OK)
		chdir(homePath.Path());

	// ask for a bunch more file descriptors so that nested copying works well
	struct rlimit rl;
	rl.rlim_cur = 512;
	rl.rlim_max = RLIM_SAVED_MAX;
	setrlimit(RLIMIT_NOFILE, &rl);

	fNodeMonitorCount = DEFAULT_MON_NUM;

	gLocalizedNamePreferred
		= BLocaleRoster::Default()->IsFilesystemTranslationPreferred();

#ifdef CHECK_OPEN_MODEL_LEAKS
	InitOpenModelDumping();
#endif

	InitIconPreloader();

#ifdef LEAK_CHECKING
	SetNewLeakChecking(true);
	SetMallocLeakChecking(true);
#endif

	// This is how often it should update the free space bar on the
	// volume icons
	SetPulseRate(1000000);

	gLaunchLooper = new LaunchLooper();
	gLaunchLooper->Run();

	// create Desktop window and lock it
	AutoLock<WindowList> lock(&fWindowList);
	BContainerWindow* deskWindow = new BDeskWindow(&fWindowList);
	AutoLock<BWindow> windowLock(deskWindow);

	// init Desktop now that pose view is created and window is locked
	deskWindow->Init();

	// create this before ReadyToRun() so that the Trash icon gets set
	fTrashWatcher = new BTrashWatcher();
}


/**
 * @brief Destructor; stops the launch looper and releases owned objects.
 */
TTracker::~TTracker()
{
	gLaunchLooper->Lock();
	gLaunchLooper->Quit();

	BPathMonitor::SetWatchingInterface(NULL);
	delete fWatchingInterface;

	delete fMimeTypeList;
}


/**
 * @brief Handle a quit request, saving window state before allowing exit.
 *
 * Prevents quit via keyboard shortcut, attempts to cancel in-progress file
 * operations, and saves all pose locations before granting the quit.
 *
 * @return true if quitting is allowed; false to defer.
 */
bool
TTracker::QuitRequested()
{
	// don't allow user quitting
	if (CurrentMessage() != NULL && CurrentMessage()->FindBool("shortcut")) {
		// but allow quitting to hide fSettingsWindow
		int32 index = 0;
		BWindow* window = NULL;
		while ((window = WindowAt(index++)) != NULL) {
			if (window == fSettingsWindow) {
				if (fSettingsWindow->Lock()) {
					if (!fSettingsWindow->IsHidden()
						&& fSettingsWindow->IsActive()) {
						fSettingsWindow->Hide();
					}
					fSettingsWindow->Unlock();
				}
				break;
			}
		}

		return false;
	}

	gStatusWindow->AttemptToQuit();
		// try quitting the copy/move/empty trash threads

	BMessage message;
	AutoLock<WindowList> lock(&fWindowList);
	// save open windows in a message inside an attribute of the desktop
	int32 count = fWindowList.CountItems();
	for (int32 i = 0; i < count; i++) {
		BContainerWindow* window
			= dynamic_cast<BContainerWindow*>(fWindowList.ItemAt(i));

		if (window != NULL && window->Lock()) {
			if (window->TargetModel() != NULL && window->PoseView() != NULL
				&& !window->PoseView()->IsDesktopView()) {
				if (window->TargetModel()->IsRoot()) {
					message.AddBool("open_disks_window", true);
				} else {
					BEntry entry;
					BPath path;
					const entry_ref* ref = window->TargetModel()->EntryRef();
					if (entry.SetTo(ref) == B_OK
						&& entry.GetPath(&path) == B_OK) {
						int8 flags = window->IsMinimized()
							? kOpenWindowMinimized : kOpenWindowNoFlags;
						uint32 deviceFlags
							= GetVolumeFlags(window->TargetModel());

						// save state for every window which is
						//	a) already open on another workspace
						//	b) on a volume not capable of writing attributes
						if (window != FindContainerWindow(ref)
							|| (deviceFlags
								& (B_FS_HAS_ATTR | B_FS_IS_READONLY))
									!= B_FS_HAS_ATTR) {
							BMessage stateMessage;
							window->SaveState(stateMessage);
							window->SetSaveStateEnabled(false);
								// This is to prevent its state to be saved
								// to the node when closed.
							message.AddMessage("window state", &stateMessage);
							flags |= kOpenWindowHasState;
						}
						const char* target;
						bool pathAlreadyExists = false;
						for (int32 index = 0;
								message.FindString("paths", index, &target)
									== B_OK; index++) {
							if (!strcmp(target,path.Path())) {
								pathAlreadyExists = true;
								break;
							}
						}
						if (!pathAlreadyExists)
							message.AddString("paths", path.Path());

						message.AddInt8(path.Path(), flags);
					}
				}
			}
			window->Unlock();
		}
	}
	lock.Unlock();

	// write windows to open on disk
	BDirectory deskDir;
	if (!BootedInSafeMode() && FSGetDeskDir(&deskDir) == B_OK) {
		// if message is empty, delete the corresponding attribute
		if (message.CountNames(B_ANY_TYPE)) {
			ssize_t size = message.FlattenedSize();
			if (size > 0) {
				char* buffer = new char[size];
				message.Flatten(buffer, size);
				deskDir.WriteAttr(kAttrOpenWindows, B_MESSAGE_TYPE, 0, buffer,
					size);
				delete[] buffer;
			}
		} else
			deskDir.RemoveAttr(kAttrOpenWindows);
	}

	for (int32 count = 0; count < 50; count++) {
		// wait 5 seconds for the copiing/moving to quit
		if (gStatusWindow->AttemptToQuit())
			break;

		snooze(100000);
	}

	return _inherited::QuitRequested();
}


/**
 * @brief Persist settings and perform final cleanup before the application exits.
 */
void
TTracker::Quit()
{
	TrackerSettings().SaveSettings(false);

	fClipboardRefsWatcher->Lock();
	fClipboardRefsWatcher->Quit();

	fTrashWatcher->Lock();
	fTrashWatcher->Quit();

	WellKnowEntryList::Quit();

	delete gPreloader;
	delete fTaskLoop;
	delete IconCache::sIconCache;

	_inherited::Quit();
}


/**
 * @brief Central message dispatcher for all Tracker application messages.
 *
 * Routes scripting, file-operation, node-monitor, volume-mount, settings, and
 * window-management messages to the appropriate handlers.
 *
 * @param message  The incoming BMessage.
 */
void
TTracker::MessageReceived(BMessage* message)
{
	if (HandleScriptingMessage(message))
		return;

	switch (message->what) {
		case kGetInfo:
			OpenInfoWindows(message);
			break;

		case kMoveSelectionToTrash:
			MoveRefsToTrash(message);
			break;

		case kSelect:
			SelectRefs(message);
			break;

		case kCloseWindowAndChildren:
		{
			const node_ref* itemNode;
			ssize_t bytes;
			if (message->FindData("node_ref", B_RAW_TYPE,
					(const void**)&itemNode, &bytes) == B_OK) {
				CloseWindowAndChildren(itemNode);
			}
			break;
		}

		case kCloseAllWindows:
			CloseAllWindows();
			break;

		case kCloseAllInWorkspace:
			CloseAllInWorkspace();
			break;

		case kFindButton:
			(new FindWindow())->Show();
			break;

		case kEditQuery:
			EditQueries(message);
			break;

		case kShowSplash:
			run_be_about();
			break;

		case kAddPrinter:
			// show the addprinter window
			run_add_printer_panel();
			break;

		case kMakeActivePrinter:
			// get the current selection
			SetDefaultPrinter(message);
			break;

#ifdef MOUNT_MENU_IN_DESKBAR
		case 'gmtv':
		{
			// Someone (probably the deskbar) has requested a list of
			// mountable volumes.
			BMessage reply;
			AutoMounterLoop()->EachMountableItemAndFloppy(
				&AddMountableItemToMessage, &reply);
			message->SendReply(&reply);
			break;
		}
#endif

		case kUnmountVolume:
			// When the user attempts to unmount a volume from the mount
			// context menu, this is where the message gets received.
			// Save pose locations and forward this to the automounter
			SaveAllPoseLocations();
			// Fall through...
		case kMountVolume:
		case kMountAllNow:
			MountServer().SendMessage(message);
			break;

		case kRestoreBackgroundImage:
		{
			BDeskWindow* desktop = GetDeskWindow();
			AutoLock<BWindow> lock(desktop);
			desktop->PostMessage(message, desktop);
			break;
		}

		case kRunAutomounterSettings:
			ShowSettingsWindow();
			fSettingsWindow->ShowPage(
				TrackerSettingsWindow::kAutomountSettings);
			break;

		case kShowSettingsWindow:
			ShowSettingsWindow();
			break;

		case kFavoriteCountChangedExternally:
			SendNotices(kFavoriteCountChangedExternally, message);
			break;

		case kStartWatchClipboardRefs:
		{
			BMessenger messenger;
			message->FindMessenger("target", &messenger);
			if (messenger.IsValid())
				fClipboardRefsWatcher->AddToNotifyList(messenger);
			break;
		}

		case kStopWatchClipboardRefs:
		{
			BMessenger messenger;
			if (message->FindMessenger("target", &messenger) == B_OK
				&& messenger.IsValid()) {
				fClipboardRefsWatcher->RemoveFromNotifyList(messenger);
			}
			break;
		}

		case kFSClipboardChanges:
			fClipboardRefsWatcher->UpdatePoseViews(message);
			break;

		case kShowVolumeSpaceBar:
		case kSpaceBarColorChanged:
			gPeriodicUpdatePoses.DoPeriodicUpdate(true);
			break;

		case B_LOCALE_CHANGED:
		{
			BLocaleRoster::Default()->Refresh();
			bool localize;
			if (message->FindBool("filesys", &localize) == B_OK)
				gLocalizedNamePreferred = localize;
			break;
		}

		case kUpdateThumbnail:
		{
			// message passed from generator thread
			// update icon on passed-in node_ref
			node_ref noderef;
			if (message->FindNodeRef("noderef", &noderef) == B_OK) {
				// cycle through open windows to find the node's pose
				// TODO find a faster way
				AutoLock<WindowList> lock(&fWindowList);
				int32 count = fWindowList.CountItems();
				for (int32 index = 0; index < count; index++) {
					BContainerWindow* window = dynamic_cast<BContainerWindow*>(
						fWindowList.ItemAt(index));
					if (window == NULL)
						continue;

					AutoLock<BWindow> windowLock(window);
					if (!windowLock.IsLocked())
						continue;

					BPoseView* poseView = window->PoseView();
					if (poseView == NULL)
						continue;

					BPose* pose = poseView->FindPose(&noderef);
					if (pose != NULL) {
						poseView->UpdateIcon(pose);
						break; // updated pose icon, exit loop
					}
				}
			}
			break;
		}

		default:
			_inherited::MessageReceived(message);
			break;
	}
}


/**
 * @brief Periodic pulse to refresh volume space-bar icons if enabled.
 *
 * Called by the BApplication framework at the rate set by SetPulseRate().
 */
void
TTracker::Pulse()
{
	if (!TrackerSettings().ShowVolumeSpaceBar())
		return;

	// update the volume icon's free space bars
	gPeriodicUpdatePoses.DoPeriodicUpdate(false);
}


/**
 * @brief Set the system default printer from the node_refs in \a message.
 *
 * @param message  Message containing node_refs of the printer to make default.
 */
void
TTracker::SetDefaultPrinter(const BMessage* message)
{
	// get the first item selected
	int32 count = 0;
	uint32 type = 0;
	message->GetInfo("refs", &type, &count);

	if (count <= 0)
		return;

	// will make the first item the default printer, disregards any
	// other files
	entry_ref ref;
	ASSERT(message->FindRef("refs", 0, &ref) == B_OK);
	if (message->FindRef("refs", 0, &ref) != B_OK)
		return;

#if B_BEOS_VERSION_DANO
	set_default_printer(ref.name);
#else
	// 	create a message for the print server
	BMessenger messenger("application/x-vnd.Be-PSRV", -1);
	if (!messenger.IsValid())
		return;

	//	send the selection to the print server
	BMessage makeActiveMessage(PSV_MAKE_PRINTER_ACTIVE_QUIETLY);
	makeActiveMessage.AddString("printer", ref.name);

	BMessage reply;
	messenger.SendMessage(&makeActiveMessage, &reply);
#endif
}


/**
 * @brief Move the refs in \a message to the Trash.
 *
 * @param message  Message containing entry_refs to move.
 */
void
TTracker::MoveRefsToTrash(const BMessage* message)
{
	int32 count;
	uint32 type;
	message->GetInfo("refs", &type, &count);

	if (count <= 0)
		return;

	BObjectList<entry_ref, true>* srcList = new BObjectList<entry_ref, true>(count);

	for (int32 index = 0; index < count; index++) {
		entry_ref ref;
		ASSERT(message->FindRef("refs", index, &ref) == B_OK);
		if (message->FindRef("refs", index, &ref) != B_OK)
			continue;

		AutoLock<WindowList> lock(&fWindowList);
		BContainerWindow* window = FindParentContainerWindow(&ref);
		if (window != NULL) {
			// if we have a window open for this entry, ask the pose to
			// delete it, this will select the next entry
			window->PoseView()->MoveEntryToTrash(&ref);
		} else {
			// add all others to a list that gets deleted separately
			srcList->AddItem(new entry_ref(ref));
		}
	}

	// async move to trash
	FSMoveToTrash(srcList);
}


/**
 * @brief Select entries in their parent Tracker windows.
 *
 * Opens the parent window for each ref if necessary, then selects the
 * corresponding pose.
 *
 * @param message  Message containing entry_refs to select.
 */
void
TTracker::SelectRefs(const BMessage* message)
{
	uint32 type = 0;
	int32 count = 0;
	message->GetInfo("refs", &type, &count);

	for (int32 index = 0; index < count; index++) {
		entry_ref ref;
		message->FindRef("refs", index, &ref);
		BEntry entry(&ref, true);
		if (entry.InitCheck() != B_OK || !entry.Exists())
			continue;

		AutoLock<WindowList> lock(&fWindowList);
		BContainerWindow* window = FindParentContainerWindow(&ref);
		if (window == NULL)
			continue;

		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) != B_OK)
			continue;

		BString expression;
		expression << "^";
		expression << name;
		expression << "$";

		BMessage* selectMessage = new BMessage(kSelectMatchingEntries);
		selectMessage->AddInt32("ExpressionType", kRegexpMatch);
		selectMessage->AddString("Expression", expression);
		selectMessage->AddBool("InvertSelection", false);
		selectMessage->AddBool("IgnoreCase", false);

		window->Activate();
			// must be activated to populate the pose list

		snooze(100000);
			// wait a bit for the pose list to be populated
			// ToDo: figure out why this is necessary

		window->PostMessage(selectMessage);
	}
}


template <class T, class FT>
class EntryAndNodeDoSoonWithMessageFunctor : public
	FunctionObjectWithResult<bool> {
public:
	EntryAndNodeDoSoonWithMessageFunctor(FT func, T* target,
		const entry_ref* child, const node_ref* parent,
		const BMessage* message)
		:
		fFunc(func),
		fTarget(target),
		fNode(*parent),
		fEntry(*child)
	{
		fSendMessage = message != NULL;
		if (message != NULL)
			fMessage = *message;
	}

	virtual ~EntryAndNodeDoSoonWithMessageFunctor() {}
	virtual void operator()()
	{
		result = (fTarget->*fFunc)(&fEntry, &fNode,
			fSendMessage ? &fMessage : NULL);
	}

protected:
	FT fFunc;
	T* fTarget;
	node_ref fNode;
	entry_ref fEntry;
	BMessage fMessage;
	bool fSendMessage;
};


/**
 * @brief Launch a file and, if successful, close its parent container window.
 *
 * @param launchThis      The entry_ref to launch.
 * @param closeThis       The node_ref of the parent window to close on success.
 * @param messageToBundle An optional message to bundle into the B_REFS_RECEIVED send.
 * @return true if the launch succeeded.
 */
bool
TTracker::LaunchAndCloseParentIfOK(const entry_ref* launchThis,
	const node_ref* closeThis, const BMessage* messageToBundle)
{
	BMessage refsReceived(B_REFS_RECEIVED);
	if (messageToBundle != NULL) {
		refsReceived = *messageToBundle;
		refsReceived.what = B_REFS_RECEIVED;
	}
	refsReceived.AddRef("refs", launchThis);
	// synchronous launch, we are already in our own thread
	if (TrackerLaunch(&refsReceived, false) == B_OK) {
		// if launched fine, close parent window in a bit
		fTaskLoop->RunLater(NewMemberFunctionObject(&TTracker::CloseParent,
			this, *closeThis), 1000000);
	}

	return false;
}


/**
 * @brief Open a filesystem entry, launching applications or opening windows as needed.
 *
 * Handles directories, applications, query files, and documents.  If
 * \a nodeToClose is non-NULL, schedules the parent window to close after
 * navigation.
 *
 * @param ref           The entry_ref to open.
 * @param nodeToClose   Parent node to close when navigating (may be NULL).
 * @param nodeToSelect  Child node to select in the opened window (may be NULL).
 * @param selector      How to open the entry (open, open with, get info, etc.).
 * @param messageToBundle Optional message bundled with a B_REFS_RECEIVED send.
 * @return B_OK on success, or an error code on failure.
 */
status_t
TTracker::OpenRef(const entry_ref* ref, const node_ref* nodeToClose,
	const node_ref* nodeToSelect, OpenSelector selector,
	const BMessage* messageToBundle)
{
	Model* model = NULL;
	BEntry entry(ref, true);
	status_t result = entry.InitCheck();

	if (result != B_OK) {
		BAlert* alert = new BAlert("",
			B_TRANSLATE("There was an error resolving the link."),
			B_TRANSLATE_COMMENT("Get info", "Tracker's 'Get info' panel [ALT+I]"),
			B_TRANSLATE("Cancel"), 0, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		int32 choice = alert->Go();

		if (choice == 0) {
			BMessenger tracker(kTrackerSignature);
			BMessage message(kGetInfo);
			message.AddRef("refs", ref);
			tracker.SendMessage(&message);
		}
		return result;
	} else
		model = new Model(&entry);

	result = model->InitCheck();
	if (result != B_OK) {
		delete model;
		return result;
	}

	bool openAsContainer = model->IsContainer();

	if (openAsContainer && selector != kOpenWith) {
		// if folder or query has a preferred handler and it's not the
		// Tracker, open it by sending refs to the handling app

		// if we are responding to the final open of OpenWith, just
		// skip this and proceed to opening the container with Tracker
		model->OpenNode();
		BNodeInfo nodeInfo(model->Node());
		char preferredApp[B_MIME_TYPE_LENGTH];
		if (nodeInfo.GetPreferredApp(preferredApp) == B_OK
			&& strcasecmp(preferredApp, kTrackerSignature) != 0) {
			openAsContainer = false;
		}
		model->CloseNode();
	}

	if (openAsContainer || selector == kRunOpenWithWindow) {
		// special case opening plain folders, queries or using open with
		OpenContainerWindow(model, NULL, selector, kRestoreDecor);
			// window adopts model
		if (nodeToClose)
			CloseParentWaitingForChildSoon(ref, nodeToClose);
	} else if (model->IsQueryTemplate()) {
		// query template - open new find window
		(new FindWindow(model->EntryRef()))->Show();

		delete model;
		if (nodeToClose)
			CloseParentWaitingForChildSoon(ref, nodeToClose);
	} else {
		delete model;
		// run Launch in a separate thread and close parent if successful
		if (nodeToClose) {
			Thread::Launch(new EntryAndNodeDoSoonWithMessageFunctor<TTracker,
				bool (TTracker::*)(const entry_ref*, const node_ref*,
				const BMessage*)>(&TTracker::LaunchAndCloseParentIfOK, this,
				ref, nodeToClose, messageToBundle));
		} else {
			BMessage refsReceived(B_REFS_RECEIVED);
			if (messageToBundle) {
				refsReceived = *messageToBundle;
				refsReceived.what = B_REFS_RECEIVED;
			}
			refsReceived.AddRef("refs", ref);
			TrackerLaunch(&refsReceived, true);
		}
	}

	if (nodeToSelect)
		SelectChildInParentSoon(ref, nodeToSelect);

	return B_OK;
}


/**
 * @brief Handle a B_REFS_RECEIVED message by opening each referenced entry.
 *
 * @param message  Message containing entry_refs and an optional open selector.
 */
void
TTracker::RefsReceived(BMessage* message)
{
	OpenSelector selector = kOpen;
	if (message->HasInt32("launchUsingSelector"))
		selector = kRunOpenWithWindow;

	entry_ref handlingApp;
	if (message->FindRef("handler", &handlingApp) == B_OK)
		selector = kOpenWith;

	int32 count;
	uint32 type;
	message->GetInfo("refs", &type, &count);

	switch (selector) {
		case kRunOpenWithWindow:
			OpenContainerWindow(NULL, message, selector);
				// window adopts model
			break;

		case kOpenWith:
		{
			// Open With resulted in passing refs and a handler,
			// open the files with the handling app
			message->RemoveName("handler");

			// have to find out if handling app is the Tracker
			// if it is, just pass it to the active Tracker,
			// no matter which Tracker was chosen to handle the refs
			char signature[B_MIME_TYPE_LENGTH];
			signature[0] = '\0';
			{
				BFile handlingNode(&handlingApp, O_RDONLY);
				BAppFileInfo appInfo(&handlingNode);
				appInfo.GetSignature(signature);
			}

			if (strcasecmp(signature, kTrackerSignature) != 0) {
				// handling app not Tracker, pass entries to the apps
				// RefsReceived
				TrackerLaunch(&handlingApp, message, true);
				break;
			}
		}
		// fall thru, opening refs by the Tracker as if they were
		// double-clicked
		case kOpen:
		{
			// copy over "Poses" messenger so that refs received
			// recipients know where the open came from
			BMessage* bundleThis = NULL;
			BMessage stackBundleThis;
			BMessenger messenger;
			if (message->FindMessenger("TrackerViewToken", &messenger)
					== B_OK) {
				bundleThis = &stackBundleThis;
				bundleThis->AddMessenger("TrackerViewToken", messenger);
			} else {
				// copy over any "be:*" fields -- e.g. /bin/open may include
				// "be:line" and "be:column"
				for (int32 i = 0;; i++) {
					char* name;
					type_code type;
					int32 count;
					status_t error = message->GetInfo(B_ANY_TYPE, i, &name,
						&type, &count);
					if (error != B_OK)
						break;

					if (strncmp(name, "be:", 3) != 0)
						continue;

					for (int32 k = 0; k < count; k++) {
						const void* data;
						ssize_t size;
						if (message->FindData(name, type, k, &data, &size)
								!= B_OK) {
							break;
						}
						if (stackBundleThis.AddData(name, type, data, size)
								!= B_OK) {
							break;
						}
						bundleThis = &stackBundleThis;
					}
				}
			}

			for (int32 index = 0; index < count; index++) {
				entry_ref ref;
				message->FindRef("refs", index, &ref);

				const node_ref* nodeToClose = NULL;
				const node_ref* nodeToSelect = NULL;
				ssize_t numBytes;

				message->FindData("nodeRefsToClose", B_RAW_TYPE, index,
					(const void**)&nodeToClose, &numBytes);
				message->FindData("nodeRefToSelect", B_RAW_TYPE, index,
					(const void**)&nodeToSelect, &numBytes);

				OpenRef(&ref, nodeToClose, nodeToSelect, selector,
					bundleThis);
			}

			break;
		}
	}
}


/**
 * @brief Open paths passed on the command line as if they were double-clicked.
 *
 * @param argc  Number of argument strings.
 * @param argv  Array of null-terminated path strings.
 */
void
TTracker::ArgvReceived(int32 argc, char** argv)
{
	BMessage* message = CurrentMessage();
	const char* currentWorkingDirectoryPath = NULL;
	entry_ref ref;

	if (message->FindString("cwd", &currentWorkingDirectoryPath) == B_OK) {
		BDirectory workingDirectory(currentWorkingDirectoryPath);
		for (int32 index = 1; index < argc; index++) {
			BEntry entry;
			if (entry.SetTo(&workingDirectory, argv[index]) == B_OK
				&& entry.GetRef(&ref) == B_OK) {
				OpenRef(&ref);
			} else if (get_ref_for_path(argv[index], &ref) == B_OK)
				OpenRef(&ref);
		}
	}
}


/**
 * @brief Open a Tracker container window for the given model.
 *
 * Reuses an existing window if one is already open for the same node (when
 * \a checkAlreadyOpen is true), otherwise creates a new BContainerWindow.
 *
 * @param model              The model to display; ownership is transferred.
 * @param originalRefsList   Message with original refs for query windows.
 * @param openSelector       How to open (normal, with app, as query, etc.).
 * @param openFlags          Flags controlling window behaviour.
 * @param checkAlreadyOpen   If true, bring existing window to front instead.
 * @param stateMessage       Saved window-state message to restore (may be NULL).
 */
void
TTracker::OpenContainerWindow(Model* model, BMessage* originalRefsList,
	OpenSelector openSelector, uint32 openFlags, bool checkAlreadyOpen,
	const BMessage* stateMessage)
{
	AutoLock<WindowList> lock(&fWindowList);
	BContainerWindow* window = NULL;
	const node_ref* modelNodeRef = model->NodeRef();
	if (checkAlreadyOpen && openSelector != kRunOpenWithWindow) {
		// find out if window already open
		window = FindContainerWindow(modelNodeRef);
	}

	bool someWindowActivated = false;

	uint32 workspace = (uint32)(1 << current_workspace());
	int32 windowCount = 0;
	while (window != NULL) {
		if ((window->Workspaces() & workspace) != 0
			&& (!model->IsDesktop() || !TrackerSettings().SingleWindowBrowse())) {
			// We found at least one window that is open and is not Desktop
			// or we're in spatial mode, activate it and make sure we don't
			// jerk the workspaces around.
			window->Activate();
			someWindowActivated = true;
		}
		window = FindContainerWindow(model->NodeRef(), ++windowCount);
	}

	if (someWindowActivated) {
		delete model;
		return;
	}

	// If no window was activated (none in the current workspace),
	// we open a new one.

	if (openSelector == kRunOpenWithWindow) {
		BMessage* refList = NULL;
		if (originalRefsList == NULL) {
			// when passing just a single model, stuff it's entry in a single
			// element list anyway
			ASSERT(model != NULL);
			refList = new BMessage;
			refList->AddRef("refs", model->EntryRef());
			delete model;
			model = NULL;
		} else {
			// clone the message, window adopts it for it's own use
			refList = new BMessage(*originalRefsList);
		}
		window = new OpenWithContainerWindow(refList, &fWindowList);
	} else if (model->IsQuery()) {
		// window will adopt the model
		window = new BQueryContainerWindow(&fWindowList, openFlags);
	} else if (model->IsVirtualDirectory()) {
		// window will adopt the model
		window = new VirtualDirectoryWindow(&fWindowList, openFlags);
	} else {
		// window will adopt the model
		window = new BContainerWindow(&fWindowList, openFlags);
	}

	if (model != NULL && window != NULL && window->LockLooper()) {
		window->CreatePoseView(model);
		if (window->PoseView() == NULL) {
			// Failed initialization.
			window->PostMessage(B_QUIT_REQUESTED);
			window->UnlockLooper();
			return;
		}
		window->UnlockLooper();
	}

	BMessage restoreStateMessage(kRestoreState);

	if (stateMessage != NULL)
		restoreStateMessage.AddMessage("state", stateMessage);

	window->PostMessage(&restoreStateMessage);
}


/**
 * @brief Open query editor windows for the refs in \a message.
 *
 * @param message  Message containing entry_refs of query or template files.
 */
void
TTracker::EditQueries(const BMessage* message)
{
	bool editOnlyIfTemplate;
	if (message->FindBool("editQueryOnPose", &editOnlyIfTemplate) != B_OK)
		editOnlyIfTemplate = false;

	type_code type;
	int32 count;
	message->GetInfo("refs", &type, &count);
	for (int32 index = 0; index < count; index++) {
		entry_ref ref;
		message->FindRef("refs", index, &ref);
		BEntry entry(&ref, true);
		if (entry.InitCheck() == B_OK && entry.Exists())
			(new FindWindow(&ref, editOnlyIfTemplate))->Show();
	}
}


/**
 * @brief Open Get Info windows for the refs in \a message.
 *
 * @param message  Message containing entry_refs to inspect.
 */
void
TTracker::OpenInfoWindows(BMessage* message)
{
	type_code type;
	int32 count;
	message->GetInfo("refs", &type, &count);

	for (int32 index = 0; index < count; index++) {
		entry_ref ref;
		message->FindRef("refs", index, &ref);
		BEntry entry;
		if (entry.SetTo(&ref) == B_OK) {
			Model* model = new Model(&entry);
			if (model->InitCheck() != B_OK) {
				delete model;
				continue;
			}

			AutoLock<WindowList> lock(&fWindowList);
			BInfoWindow* wind = FindInfoWindow(model->NodeRef());

			if (wind) {
				wind->Activate();
				delete model;
			} else {
				model->SniffMimeIfNeeded();
				wind = new BInfoWindow(model, index, &fWindowList);
				wind->PostMessage(kRestoreState);
			}
		}
	}
}


/**
 * @brief Return the Desktop window.
 *
 * @return The BDeskWindow, or NULL if not yet created.
 */
BDeskWindow*
TTracker::GetDeskWindow() const
{
	int32 count = fWindowList.CountItems();
	for (int32 index = 0; index < count; index++) {
		BDeskWindow* window = dynamic_cast<BDeskWindow*>(
			fWindowList.ItemAt(index));
		if (window != NULL)
			return window;
	}
	TRESPASS();

	return NULL;
}


/**
 * @brief Post \a message to every open BContainerWindow.
 *
 * Must be called with fWindowList locked.
 *
 * @param message  The message to broadcast.
 */
void
TTracker::PostMessageToAllContainerWindows(BMessage& message) const
{
	ASSERT(fWindowList.IsLocked());

	int32 count = fWindowList.CountItems();
	for (int32 index = 0; index < count; index++) {
		BContainerWindow* window = dynamic_cast<BContainerWindow*>(
			fWindowList.ItemAt(index));

		if (window != NULL)
			window->PostMessage(&message);
	}
}


/**
 * @brief Find a container window displaying the node identified by \a node.
 *
 * Must be called with fWindowList locked.
 *
 * @param node    The node_ref to look up.
 * @param number  0-based index when multiple windows show the same node.
 * @return The matching BContainerWindow, or NULL if not found.
 */
BContainerWindow*
TTracker::FindContainerWindow(const node_ref* node, int32 number) const
{
	ASSERT(fWindowList.IsLocked());

	int32 count = fWindowList.CountItems();
	int32 windowsFound = 0;
	for (int32 index = 0; index < count; index++) {
		BContainerWindow* window = dynamic_cast<BContainerWindow*>(
			fWindowList.ItemAt(index));

		if (window != NULL && window->IsShowing(node)
			&& number == windowsFound++) {
			return window;
		}
	}

	return NULL;
}


/**
 * @brief Find a container window displaying the entry identified by \a entry.
 *
 * Must be called with fWindowList locked.
 *
 * @param entry   The entry_ref to look up.
 * @param number  0-based index when multiple windows show the same entry.
 * @return The matching BContainerWindow, or NULL if not found.
 */
BContainerWindow*
TTracker::FindContainerWindow(const entry_ref* entry, int32 number) const
{
	ASSERT(fWindowList.IsLocked());

	int32 count = fWindowList.CountItems();

	int32 windowsFound = 0;

	for (int32 index = 0; index < count; index++) {
		BContainerWindow* window = dynamic_cast<BContainerWindow*>
			(fWindowList.ItemAt(index));

		if (window && window->IsShowing(entry) && number == windowsFound++)
			return window;
	}

	return NULL;
}


/**
 * @brief Return true if a container window is currently open for \a entry.
 *
 * @param entry  The entry_ref to check.
 * @return true if a window exists for the entry.
 */
bool
TTracker::EntryHasWindowOpen(const entry_ref* entry)
{
	AutoLock<WindowList> lock(&fWindowList);
	return FindContainerWindow(entry) != NULL;
}


/**
 * @brief Find the container window for the parent directory of \a ref.
 *
 * @param ref  The entry_ref whose parent window to locate.
 * @return The parent BContainerWindow, or NULL if not open.
 */
BContainerWindow*
TTracker::FindParentContainerWindow(const entry_ref* ref) const
{
	BEntry entry(ref);
	BEntry parent;

	if (entry.GetParent(&parent) != B_OK)
		return NULL;

	entry_ref parentRef;
	parent.GetRef(&parentRef);

	ASSERT(fWindowList.IsLocked());

	int32 count = fWindowList.CountItems();
	for (int32 index = 0; index < count; index++) {
		BContainerWindow* window = dynamic_cast<BContainerWindow*>(
			fWindowList.ItemAt(index));
		if (window != NULL && window->IsShowing(&parentRef))
			return window;
	}

	return NULL;
}


/**
 * @brief Find an open Info window for the given node_ref.
 *
 * @param node  The node_ref to look up.
 * @return The matching BInfoWindow, or NULL if not found.
 */
BInfoWindow*
TTracker::FindInfoWindow(const node_ref* node) const
{
	ASSERT(fWindowList.IsLocked());

	int32 count = fWindowList.CountItems();
	for (int32 index = 0; index < count; index++) {
		BInfoWindow* window = dynamic_cast<BInfoWindow*>(
			fWindowList.ItemAt(index));
		if (window != NULL && window->IsShowing(node))
			return window;
	}

	return NULL;
}


/**
 * @brief Return true if any query window is active for the given device.
 *
 * @param device  The device ID to check.
 * @return true if at least one query container window watches \a device.
 */
bool
TTracker::QueryActiveForDevice(dev_t device)
{
	AutoLock<WindowList> lock(&fWindowList);
	int32 count = fWindowList.CountItems();
	for (int32 index = 0; index < count; index++) {
		BQueryContainerWindow* window = dynamic_cast<BQueryContainerWindow*>(
			fWindowList.ItemAt(index));
		if (window != NULL) {
			AutoLock<BWindow> lock(window);
			if (window->ActiveOnDevice(device))
				return true;
		}
	}

	return false;
}


/**
 * @brief Close all query windows watching \a device to allow unmounting.
 *
 * Called before unmounting a volume to ensure no query holds the device open.
 *
 * @param device  The device ID whose query windows should be closed.
 */
void
TTracker::CloseActiveQueryWindows(dev_t device)
{
	// used when trying to unmount a volume - an active query would prevent
	// that from happening
	bool closed = false;
	AutoLock<WindowList> lock(fWindowList);
	for (int32 index = fWindowList.CountItems(); index >= 0; index--) {
		BQueryContainerWindow* window
			= dynamic_cast<BQueryContainerWindow*>(fWindowList.ItemAt(index));
		if (window != NULL) {
			AutoLock<BWindow> lock(window);
			if (window->ActiveOnDevice(device)) {
				window->PostMessage(B_QUIT_REQUESTED);
				closed = true;
			}
		}
	}

	lock.Unlock();

	if (closed) {
		for (int32 timeout = 30; timeout; timeout--) {
			// wait a bit for windows to fully close
			if (!QueryActiveForDevice(device))
				return;

			snooze(100000);
		}
	}
}


/**
 * @brief Persist icon positions for all open container windows.
 *
 * Called during quit to ensure that icon layout changes are not lost.
 */
void
TTracker::SaveAllPoseLocations()
{
	int32 numWindows = fWindowList.CountItems();
	for (int32 windowIndex = 0; windowIndex < numWindows; windowIndex++) {
		BContainerWindow* window = dynamic_cast<BContainerWindow*>(
			fWindowList.ItemAt(windowIndex));
		if (window != NULL) {
			AutoLock<BWindow> lock(window);
			BDeskWindow* deskWindow = dynamic_cast<BDeskWindow*>(window);
			if (deskWindow != NULL)
				deskWindow->SaveDesktopPoseLocations();
			else
				window->PoseView()->SavePoseLocations();
		}
	}
}


/**
 * @brief Close the container window for \a node and all windows for its children.
 *
 * Used when a directory is deleted or unmounted to clean up stale windows.
 *
 * @param node  The node_ref of the parent directory being closed.
 */
void
TTracker::CloseWindowAndChildren(const node_ref* node)
{
	BDirectory dir(node);
	if (dir.InitCheck() != B_OK)
		return;

	AutoLock<WindowList> lock(&fWindowList);
	BObjectList<BContainerWindow> closeList;

	// make a list of all windows to be closed
	// count from end to beginning so we can remove items safely
	for (int32 index = fWindowList.CountItems() - 1; index >= 0; index--) {
		BContainerWindow* window = dynamic_cast<BContainerWindow*>(
			fWindowList.ItemAt(index));
		if (window && window->TargetModel()) {
			BEntry wind_entry;
			wind_entry.SetTo(window->TargetModel()->EntryRef());

			if ((*window->TargetModel()->NodeRef() == *node)
				|| dir.Contains(&wind_entry)) {

				// ToDo:
				// get rid of the Remove here, BContainerWindow::Quit does it
				fWindowList.RemoveItemAt(index);
				closeList.AddItem(window);
			}
		}
	}

	// now really close the windows
	int32 numItems = closeList.CountItems();
	for (int32 index = 0; index < numItems; index++) {
		BContainerWindow* window = closeList.ItemAt(index);
		window->PostMessage(B_QUIT_REQUESTED);
	}
}


/**
 * @brief Close all non-Desktop container windows in the current workspace.
 */
void
TTracker::CloseAllInWorkspace()
{
	AutoLock<WindowList> lock(&fWindowList);

	int32 currentWorkspace = 1 << current_workspace();
	// count from end to beginning so we can remove items safely
	for (int32 index = fWindowList.CountItems() - 1; index >= 0; index--) {
		BWindow* window = fWindowList.ItemAt(index);
		if (window != NULL && (window->Workspaces() & currentWorkspace) != 0) {
			// avoid the desktop
			if (dynamic_cast<BDeskWindow*>(window) == NULL
				&& dynamic_cast<BStatusWindow*>(window) == NULL) {
				window->PostMessage(B_QUIT_REQUESTED);
			}
		}
	}
}


/**
 * @brief Close all Tracker windows without quitting the application.
 *
 * Called in response to a B_QUIT message from DeskBar to clean up while
 * keeping Tracker running.
 */
void
TTracker::CloseAllWindows()
{
	// this is a response to the DeskBar sending us a B_QUIT, when it really
	// means to say close all your windows. It might be better to have it
	// send a kCloseAllWindows message and have windowless apps stay running,
	// which is what we will do for the Tracker
	AutoLock<WindowList> lock(&fWindowList);

	int32 count = CountWindows();
	for (int32 index = 0; index < count; index++) {
		BWindow* window = WindowAt(index);
		// avoid the desktop
		if (dynamic_cast<BDeskWindow*>(window) == NULL
			&& dynamic_cast<BStatusWindow*>(window) == NULL) {
			window->PostMessage(B_QUIT_REQUESTED);
		}
	}

	// count from end to beginning so we can remove items safely
	for (int32 index = fWindowList.CountItems() - 1; index >= 0; index--) {
		BWindow* window = fWindowList.ItemAt(index);
		if (dynamic_cast<BDeskWindow*>(window) == NULL
			&& dynamic_cast<BStatusWindow*>(window) == NULL) {
			// ToDo: get rid of the Remove here, BContainerWindow::Quit()
			// does it
			fWindowList.RemoveItemAt(index);
		}
	}
}


/**
 * @brief Reopen all windows that were open in the previous Tracker session.
 *
 * Reads the saved window list from the Tracker state attribute and opens
 * each one, optionally filtering to paths starting with \a pathFilter.
 *
 * @param pathFilter  If non-NULL, only windows whose path starts with this
 *                    string are reopened (used after a volume is mounted).
 */
void
TTracker::_OpenPreviouslyOpenedWindows(const char* pathFilter)
{
	size_t filterLength = 0;
	if (pathFilter != NULL)
		filterLength = strlen(pathFilter);

	BDirectory deskDir;
	attr_info attrInfo;
	if (FSGetDeskDir(&deskDir) != B_OK
		|| deskDir.GetAttrInfo(kAttrOpenWindows, &attrInfo) != B_OK) {
		return;
	}

	char* buffer = (char*)malloc((size_t)attrInfo.size);
	BMessage message;
	if (deskDir.ReadAttr(kAttrOpenWindows, B_MESSAGE_TYPE, 0, buffer,
			(size_t)attrInfo.size) != attrInfo.size
		|| message.Unflatten(buffer) != B_OK) {
		free(buffer);
		return;
	}

	free(buffer);

	node_ref nodeRef;
	deskDir.GetNodeRef(&nodeRef);

	int32 stateMessageCounter = 0;
	const char* path;
	for (int32 i = 0; message.FindString("paths", i, &path) == B_OK; i++) {
		if (strncmp(path, pathFilter, filterLength) != 0)
			continue;

		BEntry entry(path, true);
		if (entry.InitCheck() != B_OK)
			continue;

		int8 flags = 0;
		for (int32 j = 0; message.FindInt8(path, j, &flags) == B_OK; j++) {
			Model* model = new Model(&entry);
			if (model->InitCheck() == B_OK && model->IsContainer()) {
				BMessage state;
				bool restoreStateFromMessage = false;
				if ((flags & kOpenWindowHasState) != 0
					&& message.FindMessage("window state",
						stateMessageCounter++, &state) == B_OK) {
					restoreStateFromMessage = true;
				}

				if (restoreStateFromMessage) {
					OpenContainerWindow(model, 0, kOpen, kRestoreWorkspace
						| (flags & kOpenWindowMinimized ? kIsHidden : 0U)
						| kRestoreDecor, false, &state);
				} else {
					OpenContainerWindow(model, 0, kOpen, kRestoreWorkspace
						| (flags & kOpenWindowMinimized ? kIsHidden : 0U)
						| kRestoreDecor);
				}
			} else
				delete model;
		}
	}

	// open disks window if needed

	if (pathFilter == NULL && TrackerSettings().ShowDisksIcon()
		&& message.HasBool("open_disks_window")) {
		BEntry entry("/");
		Model* model = new Model(&entry);
		if (model->InitCheck() == B_OK)
			OpenContainerWindow(model, 0, kOpen, kRestoreWorkspace);
		else
			delete model;
	}
}


/**
 * @brief Complete application startup: MIME types, templates, and previous windows.
 *
 * Called by the BApplication framework after the message loop is running.
 * Initialises MIME types, installs query templates and background images, and
 * reopens all windows that were open in the previous session.
 */
void
TTracker::ReadyToRun()
{
	gStatusWindow = new BStatusWindow();
	InitMimeTypes();
	InstallDefaultTemplates();
	InstallIndices();
	InstallTemporaryBackgroundImages();

	fTrashWatcher->Run();

	fClipboardRefsWatcher = new BClipboardRefsWatcher();
	fClipboardRefsWatcher->Run();

	fTaskLoop = new StandAloneTaskLoop(true);

	// kick off building the mime type list for find panels, etc.
	fMimeTypeList = new MimeTypeList();

	if (!BootedInSafeMode()) {
		// kick of transient query killer
		DeleteTransientQueriesTask::StartUpTransientQueryCleaner();
		// the mount_server will have mounted the previous volumes already.
		_OpenPreviouslyOpenedWindows();
	}
}


/**
 * @brief Return the global MIME type list used by Open With dialogs.
 *
 * @return Pointer to the MimeTypeList, or NULL if not yet initialised.
 */
MimeTypeList*
TTracker::MimeTypes() const
{
	return fMimeTypeList;
}


/**
 * @brief Schedule a deferred selection of \a child in the parent window.
 *
 * @param parent  The entry_ref of the parent directory window.
 * @param child   The node_ref of the child entry to select.
 */
void
TTracker::SelectChildInParentSoon(const entry_ref* parent,
	const node_ref* child)
{
	fTaskLoop->RunLater(NewMemberFunctionObjectWithResult
		(&TTracker::SelectChildInParent, this, parent, child),
		100000, 200000, 5000000);
}


/**
 * @brief Schedule closing the parent window once its child window has opened.
 *
 * @param child   The entry_ref of the child entry being opened.
 * @param parent  The node_ref of the parent window to close.
 */
void
TTracker::CloseParentWaitingForChildSoon(const entry_ref* child,
	const node_ref* parent)
{
	fTaskLoop->RunLater(NewMemberFunctionObjectWithResult
		(&TTracker::CloseParentWaitingForChild, this, child, parent),
		200000, 100000, 5000000);
}


/**
 * @brief Schedule a deferred pose selection by location in a container window.
 *
 * @param parent       node_ref of the parent directory window.
 * @param pointInPose  The point (in pose-view coordinates) to hit-test.
 */
void
TTracker::SelectPoseAtLocationSoon(node_ref parent, BPoint pointInPose)
{
	fTaskLoop->RunLater(NewMemberFunctionObject
		(&TTracker::SelectPoseAtLocationInParent, this, parent, pointInPose),
		100000);
}


/**
 * @brief Immediately select the pose at \a pointInPose in the parent window.
 *
 * @param parent       node_ref of the parent directory window.
 * @param pointInPose  The point to hit-test in pose-view coordinates.
 */
void
TTracker::SelectPoseAtLocationInParent(node_ref parent, BPoint pointInPose)
{
	AutoLock<WindowList> lock(&fWindowList);
	BContainerWindow* parentWindow = FindContainerWindow(&parent);
	if (parentWindow != NULL) {
		AutoLock<BWindow> lock(parentWindow);
		parentWindow->PoseView()->SelectPoseAtLocation(pointInPose);
	}
}


/**
 * @brief Close the parent window once the child window is ready.
 *
 * Periodically called by the task loop; returns false until the child window
 * exists, then closes the parent and returns true.
 *
 * @param child   entry_ref of the child being opened.
 * @param parent  node_ref of the parent window to close.
 * @return true when the parent has been successfully closed.
 */
bool
TTracker::CloseParentWaitingForChild(const entry_ref* child,
	const node_ref* parent)
{
	AutoLock<WindowList> lock(&fWindowList);

	BContainerWindow* parentWindow = FindContainerWindow(parent);
	if (parentWindow == NULL) {
		// parent window already closed, give up
		return true;
	}

	// If child is a symbolic link, dereference it, so that
	// FindContainerWindow will succeed.
	BEntry entry(child, true);
	entry_ref resolvedChild;
	if (entry.GetRef(&resolvedChild) != B_OK)
		resolvedChild = *child;

	BContainerWindow* window = FindContainerWindow(&resolvedChild);
	if (window != NULL) {
		AutoLock<BWindow> lock(window);
		if (!window->IsHidden())
			return CloseParentWindowCommon(parentWindow);
	}

	return false;
}


/**
 * @brief Immediately close the parent container window identified by \a parent.
 *
 * @param parent  node_ref of the parent window to close.
 */
void
TTracker::CloseParent(node_ref parent)
{
	AutoLock<WindowList> lock(&fWindowList);
	if (!lock)
		return;

	CloseParentWindowCommon(FindContainerWindow(&parent));
}


/**
 * @brief Show or bring to front the Tracker preferences window.
 *
 * Creates the window if it does not yet exist.
 */
void
TTracker::ShowSettingsWindow()
{
	if (fSettingsWindow == NULL) {
		fSettingsWindow = new TrackerSettingsWindow();
		fSettingsWindow->Show();
	} else {
		if (fSettingsWindow->Lock()) {
			if (fSettingsWindow->IsHidden())
				fSettingsWindow->Show();
			else
				fSettingsWindow->Activate();

			fSettingsWindow->Unlock();
		}
	}
}


/**
 * @brief Close a container window unless it is the Desktop.
 *
 * @param window  The window to close; must not be NULL.
 * @return true if the window was closed; false if it is the Desktop.
 */
bool
TTracker::CloseParentWindowCommon(BContainerWindow* window)
{
	ASSERT(fWindowList.IsLocked());

	if (dynamic_cast<BDeskWindow*>(window) != NULL) {
		// don't close the desktop
		return false;
	}

	window->PostMessage(B_QUIT_REQUESTED);
	return true;
}


bool
TTracker::SelectChildInParent(const entry_ref* parent, const node_ref* child)
{
	AutoLock<WindowList> lock(&fWindowList);

	BContainerWindow* window = FindContainerWindow(parent);
	if (window == NULL) {
		// parent window already closed, give up
		return false;
	}

	AutoLock<BWindow> windowLock(window);
	if (windowLock.IsLocked()) {
		BPoseView* view = window->PoseView();
		int32 index;
		BPose* pose = view->FindPose(child, &index);
		if (pose != NULL) {
			view->SelectPose(pose, index);
			return true;
		}
	}

	return false;
}


status_t
TTracker::NeedMoreNodeMonitors()
{
	fNodeMonitorCount += kNodeMonitorBumpValue;
	PRINT(("bumping nodeMonitorCount to %" B_PRId32 "\n", fNodeMonitorCount));

	struct rlimit rl;
	rl.rlim_cur = fNodeMonitorCount;
	rl.rlim_max = RLIM_SAVED_MAX;
	if (setrlimit(RLIMIT_NOVMON, &rl) < 0) {
		fNodeMonitorCount -= kNodeMonitorBumpValue;
		return errno;
	}

	return B_OK;
}


status_t
TTracker::WatchNode(const node_ref* node, uint32 flags, BMessenger target)
{
	status_t result = watch_node(node, flags, target);
	if (result == B_OK || result != B_NO_MEMORY) {
		// need to make sure this uses the same error value as
		// the node monitor code
		return result;
	}

	PRINT(("failed to start monitoring, trying to allocate more "
		"node monitors\n"));

	TTracker* tracker = dynamic_cast<TTracker*>(be_app);
	if (tracker == NULL) {
		// we are the file panel only, just fail
		return result;
	}

	result = tracker->NeedMoreNodeMonitors();

	if (result != B_OK) {
		PRINT(("failed to allocate more node monitors, %s\n",
			strerror(result)));
		return result;
	}

	// try again, this time with more node monitors
	return watch_node(node, flags, target);
}


BMessenger
TTracker::MountServer() const
{
	return BMessenger(kMountServerSignature);
}


bool
TTracker::TrashFull() const
{
	return fTrashWatcher->CheckTrashDirs();
}


bool
TTracker::IsTrashNode(const node_ref* node) const
{
	return fTrashWatcher->IsTrashNode(node);
}

bool
TTracker::InTrashNode(const entry_ref* ref) const
{
	return FSInTrashDir(ref);
}
