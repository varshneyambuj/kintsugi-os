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
 *   Copyright 2003-2015, Haiku, Inc.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Sikosis, Jérôme Duval
 *       yourpalal, Alex Wilson
 */


/**
 * @file MediaWindow.cpp
 * @brief Implements MediaWindow, the main window of the Media preferences
 *        application.
 *
 * Lists the registered audio and video nodes plus a few synthetic items
 * (audio settings, video settings, MIDI, audio mixer) and routes
 * selection changes either to a custom settings sub-view or to a
 * dynamically constructed BMediaTheme parameter web. Manages restart of
 * media_server in a background thread.
 */


#include "MediaWindow.h"

#include <stdio.h>

#include <Application.h>
#include <Autolock.h>
#include <Button.h>
#include <CardLayout.h>
#include <Catalog.h>
#include <Debug.h>
#include <Deskbar.h>
#include <IconUtils.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MediaRoster.h>
#include <MediaTheme.h>
#include <Resources.h>
#include <Roster.h>
#include <Screen.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <SpaceLayoutItem.h>
#include <StorageKit.h>
#include <String.h>
#include <TextView.h>

#include "Media.h"
#include "MediaIcons.h"
#include "MidiSettingsView.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Media Window"


/** @brief Listview selection-change notification. */
const uint32 ML_SELECTED_NODE = 'MlSN';
/** @brief Posted by the media-server-restart worker thread once it has
           finished shutting the server down. */
const uint32 ML_RESTART_THREAD_FINISHED = 'MlRF';


/**
 * @brief Visitor that walks the listview and toggles a default-input or
 *        default-output flag on the @c NodeListItem matching a target.
 *
 * Used after the user picks a new default audio/video input/output so the
 * matching row is highlighted while siblings have the flag cleared.
 */
class NodeListItemUpdater : public MediaListItem::Visitor {
public:
	/** @brief Member-pointer type for the per-item update method to
	           invoke. */
	typedef void (NodeListItem::*UpdateMethod)(bool);

	/**
	 * @brief Constructs the updater for one default-flag pass.
	 *
	 * @param target  Item that should receive @c true; all others get
	 *                @c false.
	 * @param action  NodeListItem method called with the comparison
	 *                result (e.g. SetDefaultInput).
	 */
	NodeListItemUpdater(NodeListItem* target, UpdateMethod action)
		:
		fComparator(target),
		fAction(action)
	{
	}


	/** @brief Mixer rows are not node rows; ignore. */
	virtual	void	Visit(AudioMixerListItem*){}
	/** @brief Device-header rows have no per-node flag; ignore. */
	virtual	void	Visit(DeviceListItem*){}
	/** @brief MIDI rows have no per-node flag; ignore. */
	virtual	void	Visit(MidiListItem*){}
	/**
	 * @brief Updates a NodeListItem's flag based on whether it matches the
	 *        cached comparator's target.
	 *
	 * @param item  Listview row to update.
	 */
	virtual void	Visit(NodeListItem* item)
	{
		item->Accept(fComparator);
		(item->*(fAction))(fComparator.result == 0);
	}

private:

			NodeListItem::Comparator		fComparator;
			UpdateMethod					fAction;
};


/**
 * @brief Constructs an unbound SmartNode that will deliver media events
 *        to @a notifyHandler once a node is set.
 *
 * @param notifyHandler  Recipient of BMediaRoster watch notifications for
 *                       the current node.
 */
MediaWindow::SmartNode::SmartNode(const BMessenger& notifyHandler)
	:
	fNode(NULL),
	fMessenger(notifyHandler)
{
}


/** @brief Releases the held media_node, unsubscribing from the watcher. */
MediaWindow::SmartNode::~SmartNode()
{
	_FreeNode();
}


/**
 * @brief Resolves @a info to a live media_node, instantiating a global
 *        node if one is not already running, and starts watching it.
 *
 * @param info  Dormant-node descriptor; @c NULL clears the binding.
 */
void
MediaWindow::SmartNode::SetTo(const dormant_node_info* info)
{
	_FreeNode();
	if (!info)
		return;

	fNode = new media_node();
	BMediaRoster* roster = BMediaRoster::Roster();

	status_t status = B_OK;
	media_node_id node_id;
	if (roster->GetInstancesFor(info->addon, info->flavor_id, &node_id) == B_OK)
		status = roster->GetNodeFor(node_id, fNode);
	else
		status = roster->InstantiateDormantNode(*info, fNode, B_FLAVOR_IS_GLOBAL);

	if (status != B_OK) {
		fprintf(stderr, "SmartNode::SetTo error with node %" B_PRId32
			": %s\n", fNode->node, strerror(status));
	}

	status = roster->StartWatching(fMessenger, *fNode, B_MEDIA_WILDCARD);
	if (status != B_OK) {
		fprintf(stderr, "SmartNode::SetTo can't start watching for"
			" node %" B_PRId32 "\n", fNode->node);
	}
}


/**
 * @brief Adopts an already-instantiated @a node and starts watching it.
 *
 * @param node  Live media_node to track.
 */
void
MediaWindow::SmartNode::SetTo(const media_node& node)
{
	_FreeNode();
	fNode = new media_node(node);
	BMediaRoster* roster = BMediaRoster::Roster();
	roster->StartWatching(fMessenger, *fNode, B_MEDIA_WILDCARD);
}


/**
 * @brief Reports whether a node is currently bound.
 *
 * @return @c true when SetTo() has been called and not yet cleared.
 */
bool
MediaWindow::SmartNode::IsSet()
{
	return fNode != NULL;
}


/**
 * @brief Converts the SmartNode to a media_node value, returning a default
 *        node when nothing is bound.
 *
 * @return The bound media_node, or a value-initialised one when unbound.
 */
MediaWindow::SmartNode::operator media_node()
{
	if (fNode)
		return *fNode;
	media_node node;
	return node;
}


/**
 * @brief Stops watching the held node, releases it via the media_roster,
 *        and clears the pointer.
 *
 * @note Releases unconditionally even when StopWatching() reports an
 *       error so the descriptor never leaks.
 */
void
MediaWindow::SmartNode::_FreeNode()
{
	if (!IsSet())
		return;

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster != NULL) {
		status_t status = roster->StopWatching(fMessenger,
			*fNode, B_MEDIA_WILDCARD);
		if (status != B_OK) {
			fprintf(stderr, "SmartNode::_FreeNode can't unwatch"
				" media services for node %" B_PRId32 "\n", fNode->node);
		}

		roster->ReleaseNode(*fNode);
		if (status != B_OK) {
			fprintf(stderr, "SmartNode::_FreeNode can't release"
				" node %" B_PRId32 "\n", fNode->node);
		}
	}
	delete fNode;
	fNode = NULL;
}


// #pragma mark -


/**
 * @brief Builds the window layout, queries the media_roster for the
 *        current node graph, and subscribes to media-server lifecycle
 *        events.
 *
 * @param frame  Initial window frame.
 */
MediaWindow::MediaWindow(BRect frame)
	:
	BWindow(frame, B_TRANSLATE_SYSTEM_NAME("Media"), B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fCurrentNode(BMessenger(this)),
	fParamWeb(NULL),
	fAudioInputs(5),
	fAudioOutputs(5),
	fVideoInputs(5),
	fVideoOutputs(5),
	fInitCheck(B_OK),
	fRestartThread(-1),
	fRestartAlert(NULL)
{
	_InitWindow();

	BMediaRoster* roster = BMediaRoster::Roster();
	roster->StartWatching(BMessenger(this, this),
		B_MEDIA_SERVER_STARTED);
	roster->StartWatching(BMessenger(this, this),
		B_MEDIA_SERVER_QUIT);
}


/**
 * @brief Persists the window frame and unwinds all media_roster
 *        subscriptions before destruction.
 *
 * Writes a tiny @c MediaPrefs config file under
 * @c B_USER_SETTINGS_DIRECTORY so the next launch can restore the frame.
 */
MediaWindow::~MediaWindow()
{
	_EmptyNodeLists();
	_ClearParamView();

	char buffer[512];
	BRect rect = Frame();
	PRINT_OBJECT(rect);
	snprintf(buffer, 512, "# MediaPrefs Settings\n rect = %i,%i,%i,%i\n",
		int(rect.left), int(rect.top), int(rect.right), int(rect.bottom));

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append(SETTINGS_FILE);
		BFile file(path.Path(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
		if (file.InitCheck() == B_OK)
			file.Write(buffer, strlen(buffer));
	}

	BMediaRoster* roster = BMediaRoster::CurrentRoster();
	roster->StopWatching(BMessenger(this, this),
		B_MEDIA_SERVER_STARTED);
	roster->StartWatching(BMessenger(this, this),
		B_MEDIA_SERVER_QUIT);
}


/**
 * @brief Reports whether the window successfully initialised the
 *        media_roster on construction.
 *
 * @return @c B_OK when the roster is available; the failure code from
 *         _InitMedia() otherwise.
 */
status_t
MediaWindow::InitCheck()
{
	return fInitCheck;
}


/**
 * @brief Switches the right-hand pane to a parameter web for the chosen
 *        dormant node.
 *
 * @param node  Selected dormant-node descriptor.
 */
void
MediaWindow::SelectNode(const dormant_node_info* node)
{
	fCurrentNode.SetTo(node);
	_MakeParamView();
	fTitleView->SetLabel(node->name);
}


/**
 * @brief Shows the AudioSettingsView in the right-hand card and updates
 *        the title bar.
 *
 * @param title  Localised title text.
 */
void
MediaWindow::SelectAudioSettings(const char* title)
{
	fContentLayout->SetVisibleItem(fContentLayout->IndexOfView(fAudioView));
	fTitleView->SetLabel(title);
}


/**
 * @brief Shows the VideoSettingsView in the right-hand card and updates
 *        the title bar.
 *
 * @param title  Localised title text.
 */
void
MediaWindow::SelectVideoSettings(const char* title)
{
	fContentLayout->SetVisibleItem(fContentLayout->IndexOfView(fVideoView));
	fTitleView->SetLabel(title);
}


/**
 * @brief Selects the system audio mixer node and renders its parameter
 *        web in the right-hand pane.
 *
 * @param title  Localised title text.
 */
void
MediaWindow::SelectAudioMixer(const char* title)
{
	media_node mixerNode;
	BMediaRoster* roster = BMediaRoster::Roster();
	roster->GetAudioMixer(&mixerNode);
	fCurrentNode.SetTo(mixerNode);
	_MakeParamView();
	fTitleView->SetLabel(title);
}


/**
 * @brief Shows the MidiSettingsView in the right-hand card and updates
 *        the title bar.
 *
 * @param title  Localised title text.
 */
void
MediaWindow::SelectMidiSettings(const char* title)
{
	fContentLayout->SetVisibleItem(fContentLayout->IndexOfView(fMidiView));
	fTitleView->SetLabel(title);
}


/**
 * @brief Marks @a node as the new default input for @a type and updates
 *        every NodeListItem flag accordingly.
 *
 * @param type  AUDIO_TYPE or VIDEO_TYPE.
 * @param node  Newly designated default input node.
 */
void
MediaWindow::UpdateInputListItem(MediaListItem::media_type type,
	const dormant_node_info* node)
{
	NodeListItem compareTo(node, type);
	NodeListItemUpdater updater(&compareTo, &NodeListItem::SetDefaultInput);
	for (int32 i = 0; i < fListView->CountItems(); i++) {
		MediaListItem* item = static_cast<MediaListItem*>(fListView->ItemAt(i));
		item->Accept(updater);
	}
	fListView->Invalidate();
}


/**
 * @brief Marks @a node as the new default output for @a type and updates
 *        every NodeListItem flag accordingly.
 *
 * @param type  AUDIO_TYPE or VIDEO_TYPE.
 * @param node  Newly designated default output node.
 */
void
MediaWindow::UpdateOutputListItem(MediaListItem::media_type type,
	const dormant_node_info* node)
{
	NodeListItem compareTo(node, type);
	NodeListItemUpdater updater(&compareTo, &NodeListItem::SetDefaultOutput);
	for (int32 i = 0; i < fListView->CountItems(); i++) {
		MediaListItem* item = static_cast<MediaListItem*>(fListView->ItemAt(i));
		item->Accept(updater);
	}
	fListView->Invalidate();
}


/**
 * @brief Allows the window to close, optionally warning the user if a
 *        media-server restart is in flight.
 *
 * Stops node watching and tells the BApplication to quit, then returns.
 *
 * @return Always @c true; the close is unconditional even when the
 *         restart warning fires.
 */
bool
MediaWindow::QuitRequested()
{
	if (fRestartThread > 0) {
		BString text(B_TRANSLATE("Quitting %prefname% now will stop the "
			"restarting of the media services. Flaky or unavailable media "
			"functionality is the likely result."));
		text.ReplaceFirst("%prefname%", B_TRANSLATE_SYSTEM_NAME("Media"));

		fRestartAlert = new BAlert(B_TRANSLATE("Warning!"), text,
			B_TRANSLATE("Quit anyway"), NULL, NULL,
			B_WIDTH_AS_USUAL, B_OFFSET_SPACING, B_WARNING_ALERT);

		fRestartAlert->Go();
	}
	// Stop watching the MediaRoster
	fCurrentNode.SetTo(NULL);
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


/**
 * @brief Routes media_roster events, listview selection changes, and the
 *        restart-thread completion message.
 *
 * Restart requests spawn a background worker; the @c
 * ML_RESTART_THREAD_FINISHED message arrives when the worker has shut
 * the server down so we can drive the UI back to a clean state.
 *
 * @param message  Incoming BMessage.
 */
void
MediaWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case ML_RESTART_THREAD_FINISHED:
			fRestartThread = -1;
			_InitMedia(false);
			break;

		case ML_RESTART_MEDIA_SERVER:
		{
			fRestartThread = spawn_thread(&MediaWindow::_RestartMediaServices,
				"restart_thread", B_NORMAL_PRIORITY, this);
			if (fRestartThread < 0)
				fprintf(stderr, "couldn't create restart thread\n");
			else
				resume_thread(fRestartThread);
			break;
		}

		case B_MEDIA_WEB_CHANGED:
		case ML_SELECTED_NODE:
		{
			PRINT_OBJECT(*message);

			MediaListItem* item = static_cast<MediaListItem*>(
					fListView->ItemAt(fListView->CurrentSelection()));
			if (item == NULL)
				break;

			fCurrentNode.SetTo(NULL);
			_ClearParamView();
			item->AlterWindow(this);
			break;
		}

		case B_MEDIA_SERVER_STARTED:
		case B_MEDIA_SERVER_QUIT:
		{
			PRINT_OBJECT(*message);
			_InitMedia(false);
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


// #pragma mark - private


/**
 * @brief Builds the window's static layout: listview on the left, title
 *        bar plus card layout on the right.
 *
 * Calls _InitMedia() at the end and posts a quit if the media stack is
 * unavailable.
 */
void
MediaWindow::_InitWindow()
{
	fListView = new BListView("media_list_view");
	fListView->SetSelectionMessage(new BMessage(ML_SELECTED_NODE));
	fListView->SetExplicitMinSize(BSize(140, B_SIZE_UNSET));

	// Add ScrollView to Media Menu for pretty border
	BScrollView* scrollView = new BScrollView("listscroller",
		fListView, 0, false, false, B_FANCY_BORDER);

	// Create the Views
	fTitleView = new BSeparatorView(B_HORIZONTAL, B_FANCY_BORDER);
	fTitleView->SetLabel(B_TRANSLATE("Audio settings"));
	fTitleView->SetFont(be_bold_font);

	fContentLayout = new BCardLayout();
	new BView("content view", 0, fContentLayout);
	fContentLayout->Owner()->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fContentLayout->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fAudioView = new AudioSettingsView();
	fContentLayout->AddView(fAudioView);

	fVideoView = new VideoSettingsView();
	fContentLayout->AddView(fVideoView);

	fMidiView = new MidiSettingsView();
	fContentLayout->AddView(fMidiView);

	// Layout all views
	BLayoutBuilder::Group<>(this, B_HORIZONTAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(scrollView, 0.0f)
		.AddGroup(B_VERTICAL)
			.SetInsets(0, 0, 0, 0)
			.Add(fTitleView)
			.Add(fContentLayout);

	// Start the window
	fInitCheck = _InitMedia(true);
	if (fInitCheck != B_OK)
		PostMessage(B_QUIT_REQUESTED);
	else if (IsHidden())
		Show();
}


/**
 * @brief Re-queries the media_roster for nodes and rebuilds the listview
 *        on the left.
 *
 * On first launch, prompts the user to start the media_server when one
 * is not already running. The previous selection (audio or video) is
 * preserved across reinitialisations.
 *
 * @param first  @c true on the very first call after construction;
 *               relaxes some preconditions and shows the start-server
 *               alert when needed.
 * @retval B_OK     Listview rebuilt and one row selected.
 * @retval B_ERROR  User declined to start the media_server.
 */
status_t
MediaWindow::_InitMedia(bool first)
{
	status_t err = B_OK;
	BMediaRoster* roster = BMediaRoster::Roster(&err);

	if (first && err != B_OK) {
		BAlert* alert = new BAlert("start_media_server",
			B_TRANSLATE("Could not connect to the media server.\n"
				"Would you like to start it ?"),
			B_TRANSLATE("Quit"),
			B_TRANSLATE("Start media server"), NULL,
			B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		if (alert->Go() == 0)
			return B_ERROR;

		Show();

		launch_media_server();
	}

	Lock();

	bool isVideoSelected = true;
	if (!first && fListView->ItemAt(0) != NULL
		&& fListView->ItemAt(0)->IsSelected())
		isVideoSelected = false;

	while (fListView->CountItems() > 0)
		delete fListView->RemoveItem((int32)0);
	_EmptyNodeLists();

	// Grab Media Info
	_FindNodes();

	// Add video nodes first. They might have an additional audio
	// output or input, but still should be listed as video node.
	_AddNodeItems(fVideoOutputs, MediaListItem::VIDEO_TYPE);
	_AddNodeItems(fVideoInputs, MediaListItem::VIDEO_TYPE);
	_AddNodeItems(fAudioOutputs, MediaListItem::AUDIO_TYPE);
	_AddNodeItems(fAudioInputs, MediaListItem::AUDIO_TYPE);

	fAudioView->AddOutputNodes(fAudioOutputs);
	fAudioView->AddInputNodes(fAudioInputs);
	fVideoView->AddOutputNodes(fVideoOutputs);
	fVideoView->AddInputNodes(fVideoInputs);

	// build our list view
	DeviceListItem* audio = new DeviceListItem(B_TRANSLATE("Audio settings"),
		MediaListItem::AUDIO_TYPE);
	fListView->AddItem(audio);

	MidiListItem* midi = new MidiListItem(B_TRANSLATE("MIDI Settings"));
	fListView->AddItem(midi);

	MediaListItem* video = new DeviceListItem(B_TRANSLATE("Video settings"),
		MediaListItem::VIDEO_TYPE);
	fListView->AddItem(video);

	MediaListItem* mixer = new AudioMixerListItem(B_TRANSLATE("Audio mixer"));
	fListView->AddItem(mixer);

	fListView->SortItems(&MediaListItem::Compare);
	_UpdateListViewMinWidth();

	// Set default nodes for our setting views
	media_node defaultNode;
	dormant_node_info nodeInfo;
	int32 outputID;
	BString outputName;

	if (roster->GetAudioInput(&defaultNode) == B_OK) {
		roster->GetDormantNodeFor(defaultNode, &nodeInfo);
		fAudioView->SetDefaultInput(&nodeInfo);
			// this causes our listview to be updated as well
	}

	if (roster->GetAudioOutput(&defaultNode, &outputID, &outputName) == B_OK) {
		roster->GetDormantNodeFor(defaultNode, &nodeInfo);
		fAudioView->SetDefaultOutput(&nodeInfo);
		fAudioView->SetDefaultChannel(outputID);
			// this causes our listview to be updated as well
	}

	if (roster->GetVideoInput(&defaultNode) == B_OK) {
		roster->GetDormantNodeFor(defaultNode, &nodeInfo);
		fVideoView->SetDefaultInput(&nodeInfo);
			// this causes our listview to be updated as well
	}

	if (roster->GetVideoOutput(&defaultNode) == B_OK) {
		roster->GetDormantNodeFor(defaultNode, &nodeInfo);
		fVideoView->SetDefaultOutput(&nodeInfo);
			// this causes our listview to be updated as well
	}

	if (first)
		fListView->Select(fListView->IndexOf(mixer));
	else if (isVideoSelected)
		fListView->Select(fListView->IndexOf(video));
	else
		fListView->Select(fListView->IndexOf(audio));

	Unlock();

	return B_OK;
}


/**
 * @brief Discovers the eight standard physical input/output node lists
 *        across the four media-format categories.
 */
void
MediaWindow::_FindNodes()
{
	_FindNodes(B_MEDIA_RAW_AUDIO, B_PHYSICAL_OUTPUT, fAudioOutputs);
	_FindNodes(B_MEDIA_RAW_AUDIO, B_PHYSICAL_INPUT, fAudioInputs);
	_FindNodes(B_MEDIA_ENCODED_AUDIO, B_PHYSICAL_OUTPUT, fAudioOutputs);
	_FindNodes(B_MEDIA_ENCODED_AUDIO, B_PHYSICAL_INPUT, fAudioInputs);
	_FindNodes(B_MEDIA_RAW_VIDEO, B_PHYSICAL_OUTPUT, fVideoOutputs);
	_FindNodes(B_MEDIA_RAW_VIDEO, B_PHYSICAL_INPUT, fVideoInputs);
	_FindNodes(B_MEDIA_ENCODED_VIDEO, B_PHYSICAL_OUTPUT, fVideoOutputs);
	_FindNodes(B_MEDIA_ENCODED_VIDEO, B_PHYSICAL_INPUT, fVideoInputs);
}


/**
 * @brief Queries the media_roster for dormant nodes that match @a type
 *        and @a kind and appends them to @a into.
 *
 * The format constraint is attached to the input or output side based on
 * the kind flag so the call resolves either consumers or producers.
 *
 * @param type  Required media type (e.g. B_MEDIA_RAW_AUDIO).
 * @param kind  Bitmap with one of @c B_PHYSICAL_INPUT or
 *              @c B_PHYSICAL_OUTPUT set.
 * @param into  Owning list to append matching dormant_node_info copies
 *              to.
 * @todo Improve error reporting once the media_roster surfaces richer
 *       failure codes.
 */
void
MediaWindow::_FindNodes(media_type type, uint64 kind, NodeList& into)
{
	dormant_node_info nodeInfo[64];
	int32 nodeInfoCount = 64;

	media_format format;
	media_format* nodeInputFormat = NULL;
	media_format* nodeOutputFormat = NULL;
	format.type = type;

	// output nodes must be BBufferConsumers => they have an input format
	// input nodes must be BBufferProducers => they have an output format
	if ((kind & B_PHYSICAL_OUTPUT) != 0)
		nodeInputFormat = &format;
	else if ((kind & B_PHYSICAL_INPUT) != 0)
		nodeOutputFormat = &format;
	else
		return;

	BMediaRoster* roster = BMediaRoster::Roster();

	if (roster->GetDormantNodes(nodeInfo, &nodeInfoCount, nodeInputFormat,
			nodeOutputFormat, NULL, kind) != B_OK) {
		// TODO: better error reporting!
		fprintf(stderr, "error\n");
		return;
	}

	for (int32 i = 0; i < nodeInfoCount; i++) {
		PRINT(("node : %s, media_addon %i, flavor_id %i\n",
			nodeInfo[i].name, (int)nodeInfo[i].addon,
			(int)nodeInfo[i].flavor_id));

		dormant_node_info* info = new dormant_node_info();
		strlcpy(info->name, nodeInfo[i].name, B_MEDIA_NAME_LENGTH);
		info->flavor_id = nodeInfo[i].flavor_id;
		info->addon = nodeInfo[i].addon;
		into.AddItem(info);
	}
}


/**
 * @brief Adds a NodeListItem to the listview for every node in @a list
 *        that is not already represented.
 *
 * @param list  Source list of dormant_node_info.
 * @param type  Discriminator passed to NodeListItem so it knows whether
 *              the node is audio or video.
 */
void
MediaWindow::_AddNodeItems(NodeList& list, MediaListItem::media_type type)
{
	int32 count = list.CountItems();
	for (int32 i = 0; i < count; i++) {
		dormant_node_info* info = list.ItemAt(i);
		if (_FindNodeListItem(info) == NULL)
			fListView->AddItem(new NodeListItem(info, type));
	}
}


/**
 * @brief Clears every per-direction node cache before a re-enumeration.
 */
void
MediaWindow::_EmptyNodeLists()
{
	fAudioOutputs.MakeEmpty();
	fAudioInputs.MakeEmpty();
	fVideoOutputs.MakeEmpty();
	fVideoInputs.MakeEmpty();
}


/**
 * @brief Finds the existing NodeListItem that already represents @a info,
 *        if any.
 *
 * @param info  Dormant-node descriptor to match.
 * @return Borrowed pointer to the matching listview item, or @c NULL when
 *         no row currently represents @a info.
 */
NodeListItem*
MediaWindow::_FindNodeListItem(dormant_node_info* info)
{
	NodeListItem audioItem(info, MediaListItem::AUDIO_TYPE);
	NodeListItem videoItem(info, MediaListItem::VIDEO_TYPE);

	NodeListItem::Comparator audioComparator(&audioItem);
	NodeListItem::Comparator videoComparator(&videoItem);

	for (int32 i = 0; i < fListView->CountItems(); i++) {
		MediaListItem* item = static_cast<MediaListItem*>(fListView->ItemAt(i));
		item->Accept(audioComparator);
		if (audioComparator.result == 0)
			return static_cast<NodeListItem*>(item);

		item->Accept(videoComparator);
		if (videoComparator.result == 0)
			return static_cast<NodeListItem*>(item);
	}
	return NULL;
}


/**
 * @brief Resizes the listview so its minimum width fits the widest row.
 *
 * Avoids cropping localised row labels with descenders or wide
 * characters.
 */
void
MediaWindow::_UpdateListViewMinWidth()
{
	float width = 0;
	for (int32 i = 0; i < fListView->CountItems(); i++) {
		BListItem* item = fListView->ItemAt(i);
		width = max_c(width, item->Width());
	}
	fListView->SetExplicitMinSize(BSize(width, B_SIZE_UNSET));
	fListView->InvalidateLayout();
}


/**
 * @brief Worker-thread entry point that asks the media_server to shut
 *        down and then notifies the window.
 *
 * Closes the modal "Restarting" alert once the shutdown completes so the
 * window can re-enumerate nodes.
 *
 * @param data  Pointer to the MediaWindow that spawned the worker.
 * @return Whatever PostMessage() returned for
 *         @c ML_RESTART_THREAD_FINISHED.
 */
status_t
MediaWindow::_RestartMediaServices(void* data)
{
	MediaWindow* window = (MediaWindow*)data;

	shutdown_media_server();

	if (window->fRestartAlert != NULL
			&& window->fRestartAlert->Lock()) {
		window->fRestartAlert->Quit();
	}

	return window->PostMessage(ML_RESTART_THREAD_FINISHED);
}


/**
 * @brief Removes the dynamically added parameter-web view, if one is
 *        currently shown, and frees the BParameterWeb backing it.
 *
 * The three permanent settings views (audio, video, MIDI) are left in
 * place.
 */
void
MediaWindow::_ClearParamView()
{
	BLayoutItem* item = fContentLayout->VisibleItem();
	if (!item)
		return;

	BView* view = item->View();
	if (view != fVideoView && view != fAudioView && view != fMidiView) {
		fContentLayout->RemoveItem(item);
		delete view;
		delete fParamWeb;
		fParamWeb = NULL;
	}
}


/**
 * @brief Builds the BMediaTheme parameter view for the current node and
 *        installs it in the right-hand card.
 *
 * Falls back to _MakeEmptyParamView() when the node exposes no controls
 * or the theme cannot render a view.
 */
void
MediaWindow::_MakeParamView()
{
	if (!fCurrentNode.IsSet())
		return;

	fParamWeb = NULL;
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster->GetParameterWebFor(fCurrentNode, &fParamWeb) == B_OK) {
		BRect hint(fContentLayout->Frame());
		BView* paramView = BMediaTheme::ViewFor(fParamWeb, &hint);
		if (paramView) {
			fContentLayout->AddView(paramView);
			fContentLayout->SetVisibleItem(fContentLayout->CountItems() - 1);
			return;
		}
	}

	_MakeEmptyParamView();
}


/**
 * @brief Installs a centred placeholder string in the right-hand card
 *        when no parameter web is available.
 */
void
MediaWindow::_MakeEmptyParamView()
{
	fParamWeb = NULL;

	BStringView* stringView = new BStringView("noControls",
		B_TRANSLATE("This hardware has no controls."));

	BSize unlimited(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
	stringView->SetExplicitMaxSize(unlimited);

	BAlignment centered(B_ALIGN_HORIZONTAL_CENTER,
		B_ALIGN_VERTICAL_CENTER);
	stringView->SetExplicitAlignment(centered);
	stringView->SetAlignment(B_ALIGN_CENTER);

	fContentLayout->AddView(stringView);
	fContentLayout->SetVisibleItem(fContentLayout->CountItems() - 1);

	rgb_color panel = stringView->LowColor();
	stringView->SetHighColor(tint_color(panel,
		B_DISABLED_LABEL_TINT));
}

