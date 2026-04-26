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

/** @file MediaListItem.h
    @brief Class hierarchy of BListItems shown in the Media preflet's master list. */

#ifndef __MEDIALISTITEM_H__
#define __MEDIALISTITEM_H__


#include <ListItem.h>
#include <MediaAddOn.h>


struct dormant_node_info;

struct MediaIcons;
class MediaWindow;

class AudioMixerListItem;
class DeviceListItem;
class MidiListItem;
class NodeListItem;


/**
 * @brief Common base class for all entries shown in the Media preflet
 *        list view.
 *
 * Provides shared icon/label rendering and a visitor-based comparison
 * mechanism so the concrete subclasses can interleave reliably even
 * though their orderings depend on their type. Subclasses must implement
 * @c AlterWindow() to describe what to show in the right pane when they
 * are selected.
 */
class MediaListItem : public BListItem {
public:
								MediaListItem();

	virtual	void				AlterWindow(MediaWindow* window) = 0;

	/** @brief Discriminates between audio and video media items. */
	enum media_type {
		AUDIO_TYPE,
		VIDEO_TYPE
	};

	virtual	void				Update(BView* owner, const BFont* font);
	virtual	void				DrawItem(BView* owner, BRect frame,
									bool complete = false);

	virtual	const char*			Label() = 0;


	/** @brief Returns the global MediaIcons resource cache. */
	static	MediaIcons*			Icons() {return sIcons;}
	/** @brief Sets the global MediaIcons resource cache. */
	static	void				SetIcons(MediaIcons* icons) {sIcons = icons;}

	/**
	 * @brief Visitor base used by both rendering and comparison logic.
	 *
	 * Concrete subclasses double-dispatch through @c Accept() so that
	 * comparison results can depend on the dynamic type of both operands.
	 */
	struct Visitor {
		virtual	void			Visit(AudioMixerListItem* item) = 0;
		virtual	void			Visit(DeviceListItem* item) = 0;
		virtual	void			Visit(NodeListItem* item) = 0;
		virtual void			Visit(MidiListItem* item) = 0;
	};

	virtual	void				Accept(Visitor& visitor) = 0;

	// use the visitor pattern for comparison,
	// -1 : < item; 0 : == item; 1 : > item
	virtual	int					CompareWith(MediaListItem* item) = 0;

	static	int					Compare(const void* itemOne,
									const void* itemTwo);

protected:
			struct Renderer;

	virtual void				SetRenderParameters(Renderer& renderer) = 0;

private:

	static	MediaIcons*			sIcons;
};


/**
 * @brief List item that represents an individual media node (input or
 *        output add-on).
 *
 * Knows whether it is currently the default input or default output for
 * its type so it can paint the matching badge.
 */
class NodeListItem : public MediaListItem {
public:
								NodeListItem(const dormant_node_info* node,
									media_type type);

			void				SetMediaType(MediaListItem::media_type type);
			void				SetDefaultOutput(bool isDefault);
			/** @brief Returns whether this node is currently the default output. */
			bool				IsDefaultOutput() {return fIsDefaultOutput;}
			void				SetDefaultInput(bool isDefault);
			/** @brief Returns whether this node is currently the default input. */
			bool				IsDefaultInput() {return fIsDefaultInput;}

	virtual	void				AlterWindow(MediaWindow* window);

	virtual	const char*			Label();
			/** @brief Returns whether this node represents audio or video media. */
			media_type			Type() {return fMediaType;}

	virtual	void				Accept(MediaListItem::Visitor& visitor);

	/**
	 * @brief Visitor that orders other MediaListItems relative to a given
	 *        NodeListItem target.
	 */
	struct Comparator : public MediaListItem::Visitor {
								Comparator(NodeListItem* compareOthersTo);
		virtual	void			Visit(NodeListItem* item);
		virtual	void			Visit(DeviceListItem* item);
		virtual	void			Visit(AudioMixerListItem* item);
		virtual void			Visit(MidiListItem* item);

				int				result;
					// -1 : < item; 0 : == item; 1 : > item
	private:
				NodeListItem*	fTarget;
	};

	// -1 : < item; 0 : == item; 1 : > item
	virtual	int					CompareWith(MediaListItem* item);

private:

	virtual void				SetRenderParameters(Renderer& renderer);

			const dormant_node_info* fNodeInfo;

			media_type			fMediaType;
			bool				fIsDefaultInput;
			bool				fIsDefaultOutput;
};


/**
 * @brief List item used as a category header for "Audio settings" and
 *        "Video settings".
 *
 * Selecting this item shows the matching SettingsView in the right
 * pane and does not trigger any media-server side effect.
 */
class DeviceListItem : public MediaListItem {
public:
								DeviceListItem(const char* title,
									MediaListItem::media_type type);

			/** @brief Returns whether this header represents audio or video. */
			MediaListItem::media_type Type() {return fMediaType;}
	virtual	const char*			Label() {return fTitle;}
	virtual	void				AlterWindow(MediaWindow* window);
	virtual	void				Accept(MediaListItem::Visitor& visitor);

	/**
	 * @brief Visitor that orders other MediaListItems relative to a given
	 *        DeviceListItem target.
	 */
	struct Comparator : public MediaListItem::Visitor {
								Comparator(DeviceListItem* compareOthersTo);
		virtual	void			Visit(NodeListItem* item);
		virtual	void			Visit(DeviceListItem* item);
		virtual	void			Visit(AudioMixerListItem* item);
		virtual void			Visit(MidiListItem* item);

				int				result;
					// -1 : < item; 0 : == item; 1 : > item
	private:
				DeviceListItem*	fTarget;
	};

	// -1 : < item; 0 : == item; 1 : > item
	virtual	int					CompareWith(MediaListItem* item);

private:
	virtual void				SetRenderParameters(Renderer& renderer);

			const char*			fTitle;
			media_type			fMediaType;
};


/**
 * @brief List item that selects the audio mixer's parameter web in the
 *        right pane.
 */
class AudioMixerListItem : public MediaListItem {
public:
								AudioMixerListItem(const char* title);


	virtual	const char*			Label() {return fTitle;}
	virtual	void				AlterWindow(MediaWindow* window);
	virtual	void				Accept(MediaListItem::Visitor& visitor);

	/**
	 * @brief Visitor that orders other MediaListItems relative to an
	 *        AudioMixerListItem target.
	 */
	struct Comparator : public MediaListItem::Visitor {
								Comparator(AudioMixerListItem* compareOthersTo);
		virtual	void			Visit(NodeListItem* item);
		virtual	void			Visit(DeviceListItem* item);
		virtual	void			Visit(AudioMixerListItem* item);
		virtual void			Visit(MidiListItem* item);

				int				result;
					// -1 : < item; 0 : == item; 1 : > item
	private:
				AudioMixerListItem* fTarget;
	};

	// -1 : < item; 0 : == item; 1 : > item
	virtual	int					CompareWith(MediaListItem* item);

private:
	virtual void				SetRenderParameters(Renderer& renderer);

			const char*			fTitle;
};


/**
 * @brief List item that selects the MIDI SoundFont settings view.
 */
class MidiListItem : public MediaListItem {
public:
								MidiListItem(const char* title);

	virtual	void				AlterWindow(MediaWindow* window);

	virtual	const char*			Label();

	virtual	void				Accept(MediaListItem::Visitor& visitor);

	/**
	 * @brief Visitor that orders other MediaListItems relative to a
	 *        MidiListItem target.
	 */
	struct Comparator : public MediaListItem::Visitor {
								Comparator(MidiListItem* compareOthersTo);
		virtual	void			Visit(NodeListItem* item);
		virtual	void			Visit(DeviceListItem* item);
		virtual	void			Visit(AudioMixerListItem* item);
		virtual void			Visit(MidiListItem* item);

				int				result;
					// -1 : < item; 0 : == item; 1 : > item
	private:
				MidiListItem*	fTarget;
	};

	// -1 : < item; 0 : == item; 1 : > item
	virtual	int					CompareWith(MediaListItem* item);

private:

	virtual void				SetRenderParameters(Renderer& renderer);

			const char*			fTitle;
};
#endif	/* __MEDIALISTITEM_H__ */
