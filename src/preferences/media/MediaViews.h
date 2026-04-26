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
 * MIT License. Copyright 2003, Haiku.
 * Original authors: Sikosis, Jérôme Duval.
 */

/** @file MediaViews.h
    @brief Settings views and helper menu items for audio and video defaults. */

#ifndef __MEDIAVIEWS_H__
#define __MEDIAVIEWS_H__
#include <GroupView.h>
#include <MediaAddOn.h>
#include <MenuItem.h>

#include <ObjectList.h>


class BBox;
class BButton;
class BCheckBox;
class BMenu;
class BMenuField;
class BString;
class BStringView;

class MediaWindow;


/** @brief Message asking MediaWindow to restart the media services. */
const uint32 ML_RESTART_MEDIA_SERVER = 'resr';
/** @brief Message dispatched by AudioSettingsView when the default channel changes. */
const uint32 ML_DEFAULT_CHANNEL_CHANGED = 'chch';


/**
 * @brief Menu item bound to a dormant_node_info, used in the input/output pickers.
 */
class NodeMenuItem : public BMenuItem
{
public:
								NodeMenuItem(const dormant_node_info* info,
									BMessage* message, char shortcut = 0,
									uint32 modifiers = 0);
	virtual	status_t			Invoke(BMessage* message = NULL);

			/** @brief Returns the dormant_node_info this item represents. */
			const dormant_node_info* NodeInfo() const {return fInfo;}
private:

			const dormant_node_info* fInfo;
};


/**
 * @brief Menu item bound to a media_input, used in the audio channel picker.
 *
 * Owns its @c media_input pointer; deletion is performed by the destructor.
 */
class ChannelMenuItem : public BMenuItem
{
public:
								ChannelMenuItem(media_input* input,
									BMessage* message, char shortcut = 0,
									uint32 modifiers = 0);
	virtual						~ChannelMenuItem();

			int32				DestinationID();
			media_input*		Input();

	virtual	status_t			Invoke(BMessage* message = NULL);

private:
			media_input*		fInput;
};


/**
 * @brief Base BGroupView for the audio and video default-picker panes.
 *
 * Builds and owns the input/output BPopUpMenus, populates them from
 * dormant-node lists, and routes selections back to the concrete
 * SetDefaultInput()/SetDefaultOutput() overrides.
 */
class SettingsView : public BGroupView
{
public:
	typedef BObjectList<dormant_node_info, true> NodeList;

								SettingsView();
			void				AddInputNodes(NodeList& nodes);
			void				AddOutputNodes(NodeList& nodes);

	virtual	void				SetDefaultInput(const dormant_node_info* info);
	virtual	void				SetDefaultOutput(const dormant_node_info* info);

	virtual	void				MessageReceived(BMessage* message);
	virtual	void				AttachedToWindow();


protected:

			/** @brief Returns the input picker menu owned by this view. */
			BMenu*				InputMenu() {return fInputMenu;}
			/** @brief Returns the output picker menu owned by this view. */
			BMenu*				OutputMenu() {return fOutputMenu;}

			BButton*			MakeRestartButton();

			void				_EmptyMenu(BMenu* menu);
			MediaWindow*		_MediaWindow() const;

private:
			void				_PopulateMenu(BMenu* menu, NodeList& nodes,
									const BMessage& message);
			NodeMenuItem*		_FindNodeItem(BMenu* menu,
									const dormant_node_info* nodeInfo);
			void				_ClearMenuSelection(BMenu* menu);

			BMenu* 				fInputMenu;
			BMenu* 				fOutputMenu;
};


/**
 * @brief Settings view for audio defaults: input/output nodes, output
 *        channel, and the Deskbar volume control toggle.
 */
class AudioSettingsView : public SettingsView
{
public:
								AudioSettingsView();

			void				SetDefaultChannel(int32 channelID);

	virtual	void				SetDefaultInput(const dormant_node_info* info);
	virtual	void				SetDefaultOutput(const dormant_node_info* info);

	virtual	void				MessageReceived(BMessage* message);
	virtual	void				AttachedToWindow();

private:
			BMenuField*			_MakeChannelMenu();
			BCheckBox*			_MakeVolumeCheckBox();
			void				_FillChannelMenu(const dormant_node_info* info);

			void				_ShowDeskbarVolumeControl();
			void				_HideDeskbarVolumeControl();

			ChannelMenuItem*	_ChannelMenuItemAt(int32 index);

			BMenu*				fChannelMenu;
			BCheckBox* 			fVolumeCheckBox;
};


/**
 * @brief Settings view for video defaults: just input and output node pickers.
 */
class VideoSettingsView : public SettingsView
{
public:
								VideoSettingsView();

	virtual	void				SetDefaultInput(const dormant_node_info* info);
	virtual	void				SetDefaultOutput(const dormant_node_info* info);
};

#endif
