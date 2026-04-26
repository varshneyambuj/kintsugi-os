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
 * MIT License. Copyright 2003-2014, Haiku.
 * Original authors: Sikosis, Jérôme Duval, yourpalal, Alex Wilson.
 */

/** @file MediaWindow.h
    @brief Top-level BWindow that drives the Media preflet master/detail layout. */

#ifndef MEDIA_WINDOW_H
#define MEDIA_WINDOW_H


#include <Alert.h>
#include <ListView.h>
#include <MediaAddOn.h>
#include <ParameterWeb.h>
#include <StringView.h>
#include <Window.h>

#include <ObjectList.h>

#include "MediaIcons.h"
#include "MediaListItem.h"
#include "MediaViews.h"

/** @brief Filename, relative to B_USER_SETTINGS_DIRECTORY, of the saved window frame. */
#define SETTINGS_FILE "MediaPrefs Settings"


class BCardLayout;
class BSeparatorView;
class MidiSettingsView;

/**
 * @brief Top-level BWindow for the Media preflet.
 *
 * The window is split into a left list (categories and discovered media
 * nodes) and a right card pane that swaps between the audio settings,
 * video settings, MIDI settings, and per-node parameter web. The window
 * watches the BMediaRoster for server lifecycle events and rebuilds its
 * lists when the media server is restarted.
 */
class MediaWindow : public BWindow {
public:
								MediaWindow(BRect frame);
								~MediaWindow();

			status_t			InitCheck();

	// methods to be called by MediaListItems...
			void				SelectNode(const dormant_node_info* node);
			void				SelectAudioSettings(const char* title);
			void				SelectVideoSettings(const char* title);
			void				SelectAudioMixer(const char* title);
			void				SelectMidiSettings(const char* title);

	// methods to be called by SettingsViews...
			void				UpdateInputListItem(
									MediaListItem::media_type type,
									const dormant_node_info* node);
			void				UpdateOutputListItem(
									MediaListItem::media_type type,
									const dormant_node_info* node);

	virtual	bool 				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);

private:
	typedef BObjectList<dormant_node_info, true> NodeList;

			void				_InitWindow();
			status_t			_InitMedia(bool first);

			void				_FindNodes();
			void				_FindNodes(media_type type, uint64 kind,
									NodeList& into);
			void				_AddNodeItems(NodeList &from,
									MediaListItem::media_type type);
			void				_EmptyNodeLists();
			void				_UpdateListViewMinWidth();

			NodeListItem*		_FindNodeListItem(dormant_node_info* info);

	static	status_t			_RestartMediaServices(void* data);

			void				_ClearParamView();
			void				_MakeParamView();
			void				_MakeEmptyParamView();

	/**
	 * @brief Owning wrapper around a media_node that registers
	 *        BMediaRoster watchers automatically.
	 *
	 * Switching the wrapped node tears down the previous watcher and
	 * registers a fresh one; destruction releases the underlying node.
	 */
	struct SmartNode {
								SmartNode(const BMessenger& notifyHandler);
								~SmartNode();

			void				SetTo(const dormant_node_info* node);
			void				SetTo(const media_node& node);
			bool				IsSet();
								operator media_node();

	private:
			void				_FreeNode();
			media_node*			fNode;
			BMessenger			fMessenger;
	};

			BListView*			fListView;
			BSeparatorView*		fTitleView;
			BCardLayout*		fContentLayout;
			AudioSettingsView*	fAudioView;
			VideoSettingsView*	fVideoView;
			MidiSettingsView*	fMidiView;

			SmartNode			fCurrentNode;
			BParameterWeb*		fParamWeb;


			NodeList			fAudioInputs;
			NodeList			fAudioOutputs;
			NodeList			fVideoInputs;
			NodeList			fVideoOutputs;

			status_t			fInitCheck;
			thread_id			fRestartThread;
			BAlert*				fRestartAlert;
};


#endif	// MEDIA_WINDOW_H
