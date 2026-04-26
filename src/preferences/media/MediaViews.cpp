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
 *   Copyright 2003-2011, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Sikosis
 *       Jérôme Duval
 */


/**
 * @file MediaViews.cpp
 * @brief Implementation of the audio and video default-picker settings views.
 *
 * Includes the menu-item subclasses (NodeMenuItem, ChannelMenuItem) used
 * inside the pickers and the AudioSettingsView extension that exposes
 * the output channel chooser and the Deskbar volume control toggle.
 *
 * @see MediaWindow
 */


#include "MediaViews.h"

#include <AutoDeleter.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <Deskbar.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MediaAddOn.h>
#include <MediaRoster.h>
#include <MenuField.h>
#include <PopUpMenu.h>
#include <String.h>
#include <StringView.h>
#include <TextView.h>

#include <assert.h>
#include <stdio.h>

#include "MediaWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Media views"

/** @brief Internal message: the default input node was changed via the picker. */
#define MEDIA_DEFAULT_INPUT_CHANGE 'dich'
/** @brief Internal message: the default output node was changed via the picker. */
#define MEDIA_DEFAULT_OUTPUT_CHANGE 'doch'
/** @brief Internal message: the Deskbar volume control checkbox was toggled. */
#define MEDIA_SHOW_HIDE_VOLUME_CONTROL 'shvc'


/**
 * @brief Constructs the base settings view with the input and output
 *        BPopUpMenus pre-populated with a "<none>" placeholder.
 */
SettingsView::SettingsView()
	:
	BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING),
	fInputMenu(NULL),
	fOutputMenu(NULL)
{
	// input menu
	fInputMenu = new BPopUpMenu(B_TRANSLATE_ALL("<none>",
		"VideoInputMenu", "Used when no video input is available"));
	fInputMenu->SetLabelFromMarked(true);

	// output menu
	fOutputMenu = new BPopUpMenu(B_TRANSLATE_ALL("<none>",
		"VideoOutputMenu", "Used when no video output is available"));
	fOutputMenu->SetLabelFromMarked(true);
}


/**
 * @brief Returns a fresh BButton wired to send @c ML_RESTART_MEDIA_SERVER.
 *
 * The button is unparented; the caller adds it to the layout.
 *
 * @return Newly allocated BButton; ownership transfers to the caller.
 */
BButton*
SettingsView::MakeRestartButton()
{
	return new BButton("restartButton",
		B_TRANSLATE("Restart media services"),
		new BMessage(ML_RESTART_MEDIA_SERVER));
}



/**
 * @brief Replaces the input picker contents with @a list.
 *
 * @param list Dormant input nodes to expose; not modified.
 */
void
SettingsView::AddInputNodes(NodeList& list)
{
	_EmptyMenu(fInputMenu);

	BMessage message(MEDIA_DEFAULT_INPUT_CHANGE);
	_PopulateMenu(fInputMenu, list, message);
}


/**
 * @brief Replaces the output picker contents with @a list.
 *
 * @param list Dormant output nodes to expose; not modified.
 */
void
SettingsView::AddOutputNodes(NodeList& list)
{
	_EmptyMenu(fOutputMenu);

	BMessage message(MEDIA_DEFAULT_OUTPUT_CHANGE);
	_PopulateMenu(fOutputMenu, list, message);
}


/**
 * @brief Marks the input picker entry corresponding to @a info as selected.
 *
 * @param info Dormant node to highlight; passing one not present in the
 *             menu just clears the previous selection.
 */
void
SettingsView::SetDefaultInput(const dormant_node_info* info)
{
	_ClearMenuSelection(fInputMenu);
	NodeMenuItem* item = _FindNodeItem(fInputMenu, info);
	if (item)
		item->SetMarked(true);
}


/**
 * @brief Marks the output picker entry corresponding to @a info as selected.
 *
 * @param info Dormant node to highlight; passing one not present in the
 *             menu just clears the previous selection.
 */
void
SettingsView::SetDefaultOutput(const dormant_node_info* info)
{
	_ClearMenuSelection(fOutputMenu);
	NodeMenuItem* item = _FindNodeItem(fOutputMenu, info);
	if (item)
		item->SetMarked(true);
}


/**
 * @brief BView message hook: forwards picker selections to the matching
 *        SetDefaultInput / SetDefaultOutput overrides.
 *
 * @param message Incoming message; unhandled messages fall through to
 *                BGroupView::MessageReceived().
 */
void
SettingsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MEDIA_DEFAULT_INPUT_CHANGE:
		{
			int32 index;
			if (message->FindInt32("index", &index)!=B_OK)
				break;
			NodeMenuItem* item
				= static_cast<NodeMenuItem*>(fInputMenu->ItemAt(index));
			SetDefaultInput(item->NodeInfo());
			break;
		}
		case MEDIA_DEFAULT_OUTPUT_CHANGE:
		{
			int32 index;
			if (message->FindInt32("index", &index)!=B_OK)
				break;
			NodeMenuItem* item
				= static_cast<NodeMenuItem*>(fOutputMenu->ItemAt(index));
			SetDefaultOutput(item->NodeInfo());
			break;
		}
		default:
			BGroupView::MessageReceived(message);
	}
}


/**
 * @brief BView AttachedToWindow hook: routes picker messages back here.
 */
void
SettingsView::AttachedToWindow()
{
	BMessenger thisMessenger(this);
	fInputMenu->SetTargetForItems(thisMessenger);
	fOutputMenu->SetTargetForItems(thisMessenger);
}


/**
 * @brief Returns the containing MediaWindow.
 *
 * @return The cast pointer; safe because this view is only ever inserted
 *         into a MediaWindow.
 */
MediaWindow*
SettingsView::_MediaWindow() const
{
	return static_cast<MediaWindow*>(Window());
}


/**
 * @brief Removes and deletes every item in @a menu.
 *
 * @param menu Menu to drain.
 */
void
SettingsView::_EmptyMenu(BMenu* menu)
{
	while (menu->CountItems() > 0)
		delete menu->RemoveItem((int32)0);
}


/**
 * @brief Adds a NodeMenuItem to @a menu for each dormant node in @a nodes.
 *
 * @param menu    Destination menu (typically @c fInputMenu or @c fOutputMenu).
 * @param nodes   Dormant nodes to expose.
 * @param message Template message; copied per item.
 */
void
SettingsView::_PopulateMenu(BMenu* menu, NodeList& nodes,
	const BMessage& message)
{
	for (int32 i = 0; i < nodes.CountItems(); i++) {
		dormant_node_info* info = nodes.ItemAt(i);
		menu->AddItem(new NodeMenuItem(info, new BMessage(message)));
	}

	if (Window() != NULL)
		menu->SetTargetForItems(BMessenger(this));
}


/**
 * @brief Locates the NodeMenuItem in @a menu that wraps @a nodeInfo.
 *
 * Comparison is by add-on id and flavor id.
 *
 * @param menu     Menu to scan.
 * @param nodeInfo Node identity to match.
 * @return Pointer to the matching menu item, or @c NULL when none matches.
 */
NodeMenuItem*
SettingsView::_FindNodeItem(BMenu* menu, const dormant_node_info* nodeInfo)
{
	for (int32 i = 0; i < menu->CountItems(); i++) {
		NodeMenuItem* item = static_cast<NodeMenuItem*>(menu->ItemAt(i));
		const dormant_node_info* itemInfo = item->NodeInfo();
		if (itemInfo && itemInfo->addon == nodeInfo->addon
			&& itemInfo->flavor_id == nodeInfo->flavor_id) {
			return item;
		}
	}
	return NULL;
}


/**
 * @brief Clears the marked flag on every item in @a menu.
 *
 * @param menu Menu to scrub.
 */
void
SettingsView::_ClearMenuSelection(BMenu* menu)
{
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		item->SetMarked(false);
	}
}


/**
 * @brief Constructs a NodeMenuItem bound to dormant node @a info.
 *
 * @param info      Node identity carried by this item.
 * @param message   Message dispatched on selection.
 * @param shortcut  Optional keyboard shortcut.
 * @param modifiers Optional modifier mask.
 */
NodeMenuItem::NodeMenuItem(const dormant_node_info* info, BMessage* message,
	char shortcut, uint32 modifiers)
	:
	BMenuItem(info->name, message, shortcut, modifiers),
	fInfo(info)
{

}


/**
 * @brief Suppresses re-invocation when the item is already marked.
 *
 * @param message Message override.
 * @return @c B_OK without firing when already marked; otherwise the
 *         result of BMenuItem::Invoke().
 */
status_t
NodeMenuItem::Invoke(BMessage* message)
{
	if (IsMarked())
		return B_OK;
	return BMenuItem::Invoke(message);
}


/**
 * @brief Constructs a ChannelMenuItem bound to media_input @a input.
 *
 * @param input     Channel identity; ownership transfers to this item.
 * @param message   Message dispatched on selection.
 * @param shortcut  Optional keyboard shortcut.
 * @param modifiers Optional modifier mask.
 */
ChannelMenuItem::ChannelMenuItem(media_input* input, BMessage* message,
	char shortcut, uint32 modifiers)
	:
	BMenuItem(input->name, message, shortcut, modifiers),
	fInput(input)
{
}


/**
 * @brief Releases the owned media_input.
 */
ChannelMenuItem::~ChannelMenuItem()
{
	delete fInput;
}


/**
 * @brief Returns the destination id of the wrapped media input.
 */
int32
ChannelMenuItem::DestinationID()
{
	return fInput->destination.id;
}


/**
 * @brief Returns the wrapped media_input pointer.
 */
media_input*
ChannelMenuItem::Input()
{
	return fInput;
}


/**
 * @brief Suppresses re-invocation when the item is already marked.
 *
 * @param message Message override.
 * @return @c B_OK without firing when already marked; otherwise the
 *         result of BMenuItem::Invoke().
 */
status_t
ChannelMenuItem::Invoke(BMessage* message)
{
	if (IsMarked())
		return B_OK;
	return BMenuItem::Invoke(message);
}


/**
 * @brief Builds the audio settings layout: defaults box, channel picker,
 *        Deskbar volume toggle, and Restart button.
 */
AudioSettingsView::AudioSettingsView()
{
	BBox* defaultsBox = new BBox("defaults");
	defaultsBox->SetLabel(B_TRANSLATE("Defaults"));
	BGridView* defaultsGridView = new BGridView();

	BMenuField* inputMenuField = new BMenuField("inputMenuField",
		B_TRANSLATE("Audio input:"), InputMenu());

	BMenuField* outputMenuField = new BMenuField("outputMenuField",
		B_TRANSLATE("Audio output:"), OutputMenu());

	BLayoutBuilder::Grid<>(defaultsGridView)
		.SetInsets(B_USE_DEFAULT_SPACING, 0, B_USE_DEFAULT_SPACING,
			B_USE_DEFAULT_SPACING)
		.AddMenuField(inputMenuField, 0, 0, B_ALIGN_HORIZONTAL_UNSET, 1, 3, 1)
		.AddMenuField(outputMenuField, 0, 1)
		.AddMenuField(_MakeChannelMenu(), 2, 1);

	defaultsBox->AddChild(defaultsGridView);

	BLayoutBuilder::Group<>(this)
		.SetInsets(0, 0, 0, 0)
		.Add(defaultsBox)
		.AddGroup(B_HORIZONTAL)
			.Add(_MakeVolumeCheckBox())
			.AddGlue()
			.Add(MakeRestartButton())
			.End()
		.AddGlue();
}


/**
 * @brief Marks the channel picker entry whose destination id matches
 *        @a channelID.
 *
 * @param channelID Destination id reported by the media roster.
 */
void
AudioSettingsView::SetDefaultChannel(int32 channelID)
{
	for (int32 i = 0; i < fChannelMenu->CountItems(); i++) {
		ChannelMenuItem* item = _ChannelMenuItemAt(i);
		item->SetMarked(item->DestinationID() == channelID);
	}
}


/**
 * @brief BView AttachedToWindow hook: routes channel and checkbox messages.
 */
void
AudioSettingsView::AttachedToWindow()
{
	SettingsView::AttachedToWindow();

	BMessenger thisMessenger(this);
	fChannelMenu->SetTargetForItems(thisMessenger);
	fVolumeCheckBox->SetTarget(thisMessenger);
}


/**
 * @brief BView message hook: handles channel selection and Deskbar toggle.
 *
 * @param message Incoming message; unhandled messages defer to the base
 *                SettingsView::MessageReceived().
 */
void
AudioSettingsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case ML_DEFAULT_CHANNEL_CHANGED:
			{
				int32 index;
				if (message->FindInt32("index", &index) != B_OK)
					break;
				ChannelMenuItem* item = _ChannelMenuItemAt(index);

				if (item) {
					BMediaRoster* roster = BMediaRoster::Roster();
					roster->SetAudioOutput(*item->Input());
				} else
					fprintf(stderr, "ChannelMenuItem not found\n");
			}
			break;
		case MEDIA_SHOW_HIDE_VOLUME_CONTROL:
		{
			if (fVolumeCheckBox->Value() == B_CONTROL_ON)
				_ShowDeskbarVolumeControl();
			else
				_HideDeskbarVolumeControl();
			break;
		}

		default:
			SettingsView::MessageReceived(message);
	}
}


/**
 * @brief Sets the audio default-input node, refreshes the list view badge,
 *        and tells the BMediaRoster to switch.
 *
 * @param info Dormant input node to make default.
 */
void
AudioSettingsView::SetDefaultInput(const dormant_node_info* info)
{
	SettingsView::SetDefaultInput(info);
	_MediaWindow()->UpdateInputListItem(MediaListItem::AUDIO_TYPE, info);
	BMediaRoster::Roster()->SetAudioInput(*info);
}


/**
 * @brief Sets the audio default-output node, refreshes badges, repopulates
 *        the channel picker, and tells the BMediaRoster to switch.
 *
 * @param info Dormant output node to make default.
 */
void
AudioSettingsView::SetDefaultOutput(const dormant_node_info* info)
{
	SettingsView::SetDefaultOutput(info);
	_MediaWindow()->UpdateOutputListItem(MediaListItem::AUDIO_TYPE, info);
	_FillChannelMenu(info);
	BMediaRoster::Roster()->SetAudioOutput(*info);
}


/**
 * @brief Creates the channel picker BMenuField with an empty popup.
 *
 * @return Newly allocated BMenuField wrapping @c fChannelMenu.
 */
BMenuField*
AudioSettingsView::_MakeChannelMenu()
{
	fChannelMenu = new BPopUpMenu(B_TRANSLATE("<none>"));
	fChannelMenu->SetLabelFromMarked(true);
	BMenuField* channelMenuField = new BMenuField("channelMenuField",
		B_TRANSLATE("Channel:"), fChannelMenu);
	return channelMenuField;
}


/**
 * @brief Creates the Deskbar volume control toggle, mirroring its current state.
 *
 * @return Newly allocated BCheckBox; pre-checked when the Deskbar already
 *         hosts the @c MediaReplicant.
 */
BCheckBox*
AudioSettingsView::_MakeVolumeCheckBox()
{
	fVolumeCheckBox = new BCheckBox("volumeCheckBox",
		B_TRANSLATE("Show volume control on Deskbar"),
		new BMessage(MEDIA_SHOW_HIDE_VOLUME_CONTROL));

	if (BDeskbar().HasItem("MediaReplicant"))
		fVolumeCheckBox->SetValue(B_CONTROL_ON);

	return fVolumeCheckBox;
}


/**
 * @brief Repopulates the channel picker from the inputs of @a nodeInfo.
 *
 * Acquires the dormant node (instantiating it as a global flavor when no
 * existing instance is found), enumerates its inputs into a growable
 * buffer, and creates one ChannelMenuItem per input. The first input
 * with destination id zero is marked as default.
 *
 * @param nodeInfo Dormant output node whose inputs become channels.
 */
void
AudioSettingsView::_FillChannelMenu(const dormant_node_info* nodeInfo)
{
	_EmptyMenu(fChannelMenu);

	BMediaRoster* roster = BMediaRoster::Roster();
	media_node node;
	media_node_id node_id;

	status_t err = roster->GetInstancesFor(nodeInfo->addon,
		nodeInfo->flavor_id, &node_id);
	if (err != B_OK) {
		err = roster->InstantiateDormantNode(*nodeInfo, &node,
			B_FLAVOR_IS_GLOBAL);
	} else {
		err = roster->GetNodeFor(node_id, &node);
	}

	if (err == B_OK) {
		int32 inputCount = 4;
		media_input* inputs = new media_input[inputCount];
		BPrivate::ArrayDeleter<media_input> inputDeleter(inputs);

		while (true) {
			int32 realInputCount = 0;
			err = roster->GetAllInputsFor(node, inputs,
				inputCount, &realInputCount);
			if (realInputCount > inputCount) {
				inputCount *= 2;
				inputs = new media_input[inputCount];
				inputDeleter.SetTo(inputs);
			} else {
				inputCount = realInputCount;
				break;
			}
		}

		if (err == B_OK) {
			BMessage message(ML_DEFAULT_CHANNEL_CHANGED);

			for (int32 i = 0; i < inputCount; i++) {
				media_input* input = new media_input();
				*input = inputs[i];
				ChannelMenuItem* channelItem = new ChannelMenuItem(input,
					new BMessage(message));
				fChannelMenu->AddItem(channelItem);

				if (channelItem->DestinationID() == 0)
					channelItem->SetMarked(true);
			}
		}
	}

	if (Window())
		fChannelMenu->SetTargetForItems(BMessenger(this));
}


/**
 * @brief Adds the @c desklink-driven volume replicant to the Deskbar.
 *
 * Logs to stderr when the addition fails for any reason.
 */
void
AudioSettingsView::_ShowDeskbarVolumeControl()
{
	BDeskbar deskbar;
	BEntry entry("/bin/desklink", true);
	int32 id;
	entry_ref ref;
	status_t status = entry.GetRef(&ref);
	if (status == B_OK)
		status = deskbar.AddItem(&ref, &id);

	if (status != B_OK) {
		fprintf(stderr, B_TRANSLATE(
			"Couldn't add volume control in Deskbar: %s\n"),
			strerror(status));
	}
}


/**
 * @brief Removes the volume replicant from the Deskbar.
 *
 * Logs to stderr when removal fails.
 */
void
AudioSettingsView::_HideDeskbarVolumeControl()
{
	BDeskbar deskbar;
	status_t status = deskbar.RemoveItem("MediaReplicant");
	if (status != B_OK) {
		fprintf(stderr, B_TRANSLATE(
			"Couldn't remove volume control in Deskbar: %s\n"),
			strerror(status));
	}
}


/**
 * @brief Returns the channel menu item at @a index, statically cast.
 *
 * @param index Zero-based index into @c fChannelMenu.
 * @return The item; behaviour is undefined if @a index is out of range.
 */
ChannelMenuItem*
AudioSettingsView::_ChannelMenuItemAt(int32 index)
{
	return static_cast<ChannelMenuItem*>(fChannelMenu->ItemAt(index));
}


/**
 * @brief Builds the video settings layout: defaults box and Restart button.
 */
VideoSettingsView::VideoSettingsView()
{
	BBox* defaultsBox = new BBox("defaults");
	defaultsBox->SetLabel(B_TRANSLATE("Defaults"));
	BGridView* defaultsGridView = new BGridView();

	BMenuField* inputMenuField = new BMenuField("inputMenuField",
		B_TRANSLATE("Video input:"), InputMenu());

	BMenuField* outputMenuField = new BMenuField("outputMenuField",
		B_TRANSLATE("Video output:"), OutputMenu());

	BLayoutBuilder::Grid<>(defaultsGridView)
		.SetInsets(B_USE_DEFAULT_SPACING, 0, B_USE_DEFAULT_SPACING,
			B_USE_DEFAULT_SPACING)
		.AddMenuField(inputMenuField, 0, 0)
		.AddMenuField(outputMenuField, 0, 1);

	defaultsBox->AddChild(defaultsGridView);

	BLayoutBuilder::Group<>(this)
		.SetInsets(0, 0, 0, 0)
		.Add(defaultsBox)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(MakeRestartButton())
			.End()
		.AddGlue();
}


/**
 * @brief Sets the video default-input node and asks the BMediaRoster
 *        to switch.
 *
 * @param info Dormant input node to make default.
 */
void
VideoSettingsView::SetDefaultInput(const dormant_node_info* info)
{
	SettingsView::SetDefaultInput(info);
	_MediaWindow()->UpdateInputListItem(MediaListItem::VIDEO_TYPE, info);
	BMediaRoster::Roster()->SetVideoInput(*info);
}


/**
 * @brief Sets the video default-output node and asks the BMediaRoster
 *        to switch.
 *
 * @param info Dormant output node to make default.
 */
void
VideoSettingsView::SetDefaultOutput(const dormant_node_info* info)
{
	SettingsView::SetDefaultOutput(info);
	_MediaWindow()->UpdateOutputListItem(MediaListItem::VIDEO_TYPE, info);
	BMediaRoster::Roster()->SetVideoOutput(*info);
}
