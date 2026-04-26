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
 *   Copyright (c) 2003, Haiku
 *   This software is part of the Haiku distribution and is covered
 *   by the MIT License.
 *
 *   Authors:
 *       Sikosis, Jérôme Duval
 */


/**
 * @file MediaListItem.cpp
 * @brief Implementation of the MediaListItem class hierarchy.
 *
 * Implements the shared icon/text Renderer used by all subclasses, and
 * the visitor-based comparators that order the heterogeneous list
 * entries (categories first, then audio nodes, MIDI, mixer, video
 * nodes).
 *
 * @see MediaWindow
 */


#include "MediaListItem.h"

#include <string.h>

#include <MediaAddOn.h>
#include <View.h>

#include "MediaIcons.h"
#include "MediaWindow.h"
#include "MidiSettingsView.h"


/** @brief Vertical padding in pixels above and below each list item. */
#define kITEM_MARGIN	1
/** @brief Comparator result indicating "this > other". */
#define GREATER_THAN	-1
/** @brief Comparator result indicating "this < other". */
#define LESS_THAN		1


/** @brief Storage for the global MediaIcons cache pointer; set by Media::Media(). */
MediaIcons* MediaListItem::sIcons = NULL;


/**
 * @brief Helper that knows how to draw a MediaListItem given a small
 *        parameter set: title, one or two icons, selection state, and
 *        single-vs-double inset preference.
 */
struct MediaListItem::Renderer {
	/**
	 * @brief Constructs an empty renderer with sane defaults
	 *        (double inset, not selected).
	 */
	Renderer()
		:
		fTitle(NULL),
		fPrimaryIcon(NULL),
		fSecondaryIcon(NULL),
		fDoubleInsets(true),
		fSelected(false)
	{
	}

	/**
	 * @brief Adds an icon to the renderer.
	 *
	 * The first icon added is drawn next to the label; the second is
	 * drawn to the right of the label. When a third or later icon is
	 * pushed, it replaces the primary slot and the previous primary
	 * slides to secondary.
	 *
	 * @param icon Bitmap to draw; ownership is not transferred.
	 */
	void AddIcon(BBitmap* icon)
	{
		if (!fPrimaryIcon)
			fPrimaryIcon = icon;
		else {
			fSecondaryIcon = fPrimaryIcon;
			fPrimaryIcon = icon;
		}
	}

	/** @brief Sets the title text. */
	void SetTitle(const char* title)
	{
		fTitle = title;
	}

	/** @brief Marks the item as selected, enabling the highlight fill. */
	void SetSelected(bool selected)
	{
		fSelected = selected;
	}

	/**
	 * @brief Sets whether to leave room for two icons; defaults to true.
	 *
	 * @param doubleInset @c true to reserve two icon slots, @c false for one.
	 */
	void UseDoubleInset(bool doubleInset)
	{
		fDoubleInsets = doubleInset;
	}

	/**
	 * @brief Renders the item into the given BView at @a frame.
	 *
	 * Saves and restores the view's high/low colors so the caller's
	 * drawing state is not perturbed.
	 *
	 * @param onto     Target BView (the list view).
	 * @param frame    Item frame in target coordinates.
	 * @param complete Whether to fill the entire frame regardless of
	 *                 selection state (set on full redraws).
	 */
	void Render(BView* onto, BRect frame, bool complete = false)
	{
		const rgb_color lowColor = onto->LowColor();
		const rgb_color highColor = onto->HighColor();

		if (fSelected || complete) {
			if (fSelected)
				onto->SetLowColor(ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
			onto->FillRect(frame, B_SOLID_LOW);
		}

		BPoint point(frame.left + 4.0f,
			frame.top + (frame.Height() - MediaIcons::sBounds.Height()) / 2.0f);

		BRect iconFrame(MediaIcons::IconRectAt(point + BPoint(1, 0)));

		onto->SetDrawingMode(B_OP_OVER);
		if (fPrimaryIcon && !fDoubleInsets) {
			onto->DrawBitmap(fPrimaryIcon, iconFrame);
			point.x = iconFrame.right + 1;
		} else if (fSecondaryIcon) {
			onto->DrawBitmap(fSecondaryIcon, iconFrame);
		}

		iconFrame = MediaIcons::IconRectAt(iconFrame.RightTop() + BPoint(1, 0));

		if (fDoubleInsets) {
			if (fPrimaryIcon != NULL)
				onto->DrawBitmap(fPrimaryIcon, iconFrame);
			point.x = iconFrame.right + 1;
		}

		onto->SetDrawingMode(B_OP_COPY);

		BFont font = be_plain_font;
		font_height	fontInfo;
		font.GetHeight(&fontInfo);

		onto->SetFont(&font);
		onto->MovePenTo(point.x + 8, frame.top
			+ fontInfo.ascent + (frame.Height()
			- ceilf(fontInfo.ascent + fontInfo.descent)) / 2.0f);
		onto->DrawString(fTitle);

		onto->SetHighColor(highColor);
		onto->SetLowColor(lowColor);
	}

	/**
	 * @brief Returns the natural width of the rendered item, including
	 *        margins, icon slots, and string width.
	 *
	 * @return Width in pixels suitable for SetWidth() on the BListItem.
	 */
	float ItemWidth()
	{
		float width = 4.0f;
			// left margin

		float iconSpace = MediaIcons::sBounds.Width() + 1.0f;
		if (fDoubleInsets)
			iconSpace *= 2.0f;
		width += iconSpace;
		width += 8.0f;
			// space between icons and text

		width += be_plain_font->StringWidth(fTitle) + 16.0f;
		return width;
	}

private:

	const char*	fTitle;
	BBitmap*	fPrimaryIcon;
	BBitmap*	fSecondaryIcon;
	bool		fDoubleInsets;
	bool		fSelected;
};


/**
 * @brief Constructs an empty list item with no toplevel flags set.
 */
MediaListItem::MediaListItem()
	:
	BListItem((uint32)0)
{
}


/**
 * @brief BListItem hook used to recompute item size after a font change.
 *
 * Ensures the item is at least tall enough for the icon, then asks each
 * subclass to populate a Renderer so the natural width can be set.
 *
 * @param owner Owning list view.
 * @param font  Font in effect for the list view.
 */
void
MediaListItem::Update(BView* owner, const BFont* font)
{
	// we need to override the update method so we can make sure our
	// list item size doesn't change
	BListItem::Update(owner, font);

	float iconHeight = MediaIcons::sBounds.Height() + 1;
	if ((Height() < iconHeight + kITEM_MARGIN * 2)) {
		SetHeight(iconHeight + kITEM_MARGIN * 2);
	}

	Renderer renderer;
	renderer.SetTitle(Label());
	SetRenderParameters(renderer);
	SetWidth(renderer.ItemWidth());
}


/**
 * @brief BListItem hook that paints the item via the shared Renderer.
 *
 * @param owner    Owning list view.
 * @param frame    Item frame in target coordinates.
 * @param complete Whether to repaint the entire item background.
 */
void
MediaListItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	Renderer renderer;
	renderer.SetSelected(IsSelected());
	renderer.SetTitle(Label());
	SetRenderParameters(renderer);
	renderer.Render(owner, frame, complete);
}


/**
 * @brief Static comparator suitable for BListView::SortItems().
 *
 * Defers to the visitor-based CompareWith() so that ordering can depend
 * on the dynamic types of both items.
 *
 * @param itemOne Pointer to a MediaListItem*.
 * @param itemTwo Pointer to a MediaListItem*.
 * @return Sort result in @c -1, @c 0, @c 1 form.
 */
int
MediaListItem::Compare(const void* itemOne, const void* itemTwo)
{
	MediaListItem* firstItem = *(MediaListItem**)itemOne;
	MediaListItem* secondItem = *(MediaListItem**)itemTwo;

	return firstItem->CompareWith(secondItem);
}


// #pragma mark - NodeListItem


/**
 * @brief Constructs a NodeListItem bound to the given dormant node.
 *
 * @param node Pointer to the dormant node info; the item does not own it.
 * @param type Whether the node is audio or video.
 */
NodeListItem::NodeListItem(const dormant_node_info* node, media_type type)
	:
	MediaListItem(),
	fNodeInfo(node),
	fMediaType(type),
	fIsDefaultInput(false),
	fIsDefaultOutput(false)
{
}


/**
 * @brief Adds the matching default-input or default-output icons for this
 *        node to the renderer.
 *
 * @param renderer Renderer to populate.
 */
void
NodeListItem::SetRenderParameters(MediaListItem::Renderer& renderer)
{
	MediaIcons::IconSet* iconSet = &Icons()->videoIcons;
	if (fMediaType == MediaListItem::AUDIO_TYPE)
		iconSet = &Icons()->audioIcons;

	if (fIsDefaultInput)
		renderer.AddIcon(&iconSet->inputIcon);
	if (fIsDefaultOutput)
		renderer.AddIcon(&iconSet->outputIcon);
}


/**
 * @brief Returns the underlying dormant node's display name.
 */
const char*
NodeListItem::Label()
{
	return fNodeInfo->name;
}


/** @brief Sets the media type of this node entry. */
void
NodeListItem::SetMediaType(media_type type)
{
	fMediaType = type;
}


/** @brief Toggles the default-output badge for this node entry. */
void
NodeListItem::SetDefaultOutput(bool isDefault)
{
	fIsDefaultOutput = isDefault;
}


/** @brief Toggles the default-input badge for this node entry. */
void
NodeListItem::SetDefaultInput(bool isDefault)
{
	fIsDefaultInput = isDefault;
}


/**
 * @brief Asks the MediaWindow to load this node's parameter web.
 *
 * @param window Containing MediaWindow.
 */
void
NodeListItem::AlterWindow(MediaWindow* window)
{
	window->SelectNode(fNodeInfo);
}


/**
 * @brief Visitor double-dispatch entry point for NodeListItem.
 */
void
NodeListItem::Accept(MediaListItem::Visitor& visitor)
{
	visitor.Visit(this);
}


/**
 * @brief Compares this node entry with @a item via the visitor pattern.
 *
 * @param item Other entry to compare against.
 * @return Comparator result in @c -1, @c 0, @c 1 form.
 */
int
NodeListItem::CompareWith(MediaListItem* item)
{
	Comparator comparator(this);
	item->Accept(comparator);
	return comparator.result;
}


/**
 * @brief Constructs a comparator targeting @a compareOthersTo.
 */
NodeListItem::Comparator::Comparator(NodeListItem* compareOthersTo)
	:
	result(GREATER_THAN),
	fTarget(compareOthersTo)
{
}


/**
 * @brief Orders this node entry against another node entry.
 *
 * Audio nodes precede video nodes; within the same media type the order
 * is the lexical order of the node names.
 */
void
NodeListItem::Comparator::Visit(NodeListItem* item)
{
	result = GREATER_THAN;

	if (fTarget->Type() != item->Type() && fTarget->Type() == VIDEO_TYPE)
		result = LESS_THAN;
	else
		result = strcmp(fTarget->Label(), item->Label());
}


/**
 * @brief Orders this node entry against a category header.
 *
 * Nodes always come after their matching header except when the target
 * is an audio node and the header is the video category.
 */
void
NodeListItem::Comparator::Visit(DeviceListItem* item)
{
	result = LESS_THAN;
	if (fTarget->Type() != item->Type() && fTarget->Type() == AUDIO_TYPE)
		result = GREATER_THAN;
}


/**
 * @brief Orders this node entry against the audio mixer entry.
 *
 * Nodes always sort before the mixer.
 */
void
NodeListItem::Comparator::Visit(AudioMixerListItem* item)
{
	result = LESS_THAN;
}


/**
 * @brief Orders this node entry against the MIDI entry.
 *
 * Audio and video nodes always sort after the MIDI entry.
 */
void
NodeListItem::Comparator::Visit(MidiListItem* item)
{
	result = GREATER_THAN;
}


// #pragma mark - DeviceListItem


/**
 * @brief Constructs a category header with the given title and media type.
 *
 * @param title Header label (already translated).
 * @param type  Whether this header introduces audio or video nodes.
 */
DeviceListItem::DeviceListItem(const char* title,
	MediaListItem::media_type type)
	:
	MediaListItem(),
	fTitle(title),
	fMediaType(type)
{
}


/**
 * @brief Visitor double-dispatch entry point for DeviceListItem.
 */
void
DeviceListItem::Accept(MediaListItem::Visitor& visitor)
{
	visitor.Visit(this);
}


/**
 * @brief Compares this header with @a item via the visitor pattern.
 *
 * @param item Other entry to compare against.
 * @return Comparator result in @c -1, @c 0, @c 1 form.
 */
int
DeviceListItem::CompareWith(MediaListItem* item)
{
	Comparator comparator(this);
	item->Accept(comparator);
	return comparator.result;
}


/**
 * @brief Constructs a comparator targeting @a compareOthersTo.
 */
DeviceListItem::Comparator::Comparator(DeviceListItem* compareOthersTo)
	:
	result(GREATER_THAN),
	fTarget(compareOthersTo)
{
}


/**
 * @brief Orders this header against a node entry.
 *
 * Headers precede nodes of the same type, and audio precedes video.
 */
void
DeviceListItem::Comparator::Visit(NodeListItem* item)
{
	result = GREATER_THAN;
	if (fTarget->Type() != item->Type() && fTarget->Type() == AUDIO_TYPE)
		result = LESS_THAN;
}


/**
 * @brief Orders two category headers; audio comes before video.
 */
void
DeviceListItem::Comparator::Visit(DeviceListItem* item)
{
	result = LESS_THAN;
	if (fTarget->Type() == AUDIO_TYPE)
		result = GREATER_THAN;
}


/**
 * @brief Orders this header against the audio mixer entry.
 */
void
DeviceListItem::Comparator::Visit(AudioMixerListItem* item)
{
	result = LESS_THAN;
	if (fTarget->Type() == AUDIO_TYPE)
		result = GREATER_THAN;
}


/**
 * @brief Orders this header against the MIDI entry.
 */
void
DeviceListItem::Comparator::Visit(MidiListItem* item)
{
	result = LESS_THAN;
}


/**
 * @brief Adds the generic devices badge with single-icon insets.
 *
 * @param renderer Renderer to populate.
 */
void
DeviceListItem::SetRenderParameters(Renderer& renderer)
{
	renderer.AddIcon(&Icons()->devicesIcon);
	renderer.UseDoubleInset(false);
}


/**
 * @brief Asks the MediaWindow to show the audio or video settings pane.
 *
 * @param window Containing MediaWindow.
 */
void
DeviceListItem::AlterWindow(MediaWindow* window)
{
	if (fMediaType == MediaListItem::AUDIO_TYPE)
		window->SelectAudioSettings(fTitle);
	else
		window->SelectVideoSettings(fTitle);
}


// #pragma mark - AudioMixerListItem


/**
 * @brief Constructs the audio mixer list entry with the given title.
 */
AudioMixerListItem::AudioMixerListItem(const char* title)
	:
	MediaListItem(),
	fTitle(title)
{
}


/**
 * @brief Asks the MediaWindow to show the audio mixer parameter web.
 *
 * @param window Containing MediaWindow.
 */
void
AudioMixerListItem::AlterWindow(MediaWindow* window)
{
	window->SelectAudioMixer(fTitle);
}


/**
 * @brief Visitor double-dispatch entry point for AudioMixerListItem.
 */
void
AudioMixerListItem::Accept(MediaListItem::Visitor& visitor)
{
	visitor.Visit(this);
}


/**
 * @brief Compares this audio-mixer entry with @a item via the visitor.
 *
 * @param item Other entry to compare against.
 * @return Comparator result in @c -1, @c 0, @c 1 form.
 */
int
AudioMixerListItem::CompareWith(MediaListItem* item)
{
	Comparator comparator(this);
	item->Accept(comparator);
	return comparator.result;
}


/**
 * @brief Constructs a comparator targeting @a compareOthersTo.
 */
AudioMixerListItem::Comparator::Comparator(AudioMixerListItem* compareOthersTo)
	:
	result(0),
	fTarget(compareOthersTo)
{
}


/**
 * @brief Orders the audio mixer against a node entry.
 *
 * The mixer always sorts after media nodes.
 */
void
AudioMixerListItem::Comparator::Visit(NodeListItem* item)
{
	result = GREATER_THAN;
}


/**
 * @brief Orders the audio mixer against a category header.
 */
void
AudioMixerListItem::Comparator::Visit(DeviceListItem* item)
{
	result = GREATER_THAN;
	if (item->Type() == AUDIO_TYPE)
		result = LESS_THAN;
}


/**
 * @brief Two audio mixer entries are always equal (only one exists).
 */
void
AudioMixerListItem::Comparator::Visit(AudioMixerListItem* item)
{
	result = 0;
}


/**
 * @brief Orders the audio mixer against the MIDI entry.
 */
void
AudioMixerListItem::Comparator::Visit(MidiListItem* item)
{
	result = GREATER_THAN;
}


/**
 * @brief Adds the mixer badge to the renderer.
 *
 * @param renderer Renderer to populate.
 */
void
AudioMixerListItem::SetRenderParameters(Renderer& renderer)
{
	renderer.AddIcon(&Icons()->mixerIcon);
}


// #pragma mark - MidiListItem

/**
 * @brief Constructs the MIDI settings entry with the given title.
 */
MidiListItem::MidiListItem(const char* title)
	:
	MediaListItem(),
	fTitle(title)
{
}


/**
 * @brief Asks the MediaWindow to show the MIDI settings pane.
 *
 * @param window Containing MediaWindow.
 */
void
MidiListItem::AlterWindow(MediaWindow* window)
{
	window->SelectMidiSettings(fTitle);
}


/**
 * @brief Returns the static label "MIDI" used by this entry.
 */
const char*
MidiListItem::Label()
{
	return "MIDI";
}


/**
 * @brief Visitor double-dispatch entry point for MidiListItem.
 */
void
MidiListItem::Accept(MediaListItem::Visitor& visitor)
{
	visitor.Visit(this);
}


/**
 * @brief Compares this MIDI entry with @a item via the visitor pattern.
 *
 * @param item Other entry to compare against.
 * @return Comparator result in @c -1, @c 0, @c 1 form.
 */
int
MidiListItem::CompareWith(MediaListItem* item)
{
	Comparator comparator(this);
	item->Accept(comparator);
	return comparator.result;
}


/**
 * @brief Constructs a comparator targeting @a compareOthersTo.
 */
MidiListItem::Comparator::Comparator(MidiListItem* compareOthersTo)
	:
	result(0),
	fTarget(compareOthersTo)
{
}


/**
 * @brief Orders the MIDI entry against a node entry.
 */
void
MidiListItem::Comparator::Visit(NodeListItem* item)
{
	result = GREATER_THAN;
}


/**
 * @brief Orders the MIDI entry against a category header.
 */
void
MidiListItem::Comparator::Visit(DeviceListItem* item)
{
	result = GREATER_THAN;
	if (item->Type() == AUDIO_TYPE)
		result = LESS_THAN;
}


/**
 * @brief Orders the MIDI entry against the audio mixer entry.
 */
void
MidiListItem::Comparator::Visit(AudioMixerListItem* item)
{
	result = LESS_THAN;
}


/**
 * @brief Two MIDI entries are always equal (only one exists).
 */
void
MidiListItem::Comparator::Visit(MidiListItem* item)
{
	result = 0;
}


/**
 * @brief Adds the mixer badge to the renderer.
 *
 * @param renderer Renderer to populate.
 * @todo Replace with a dedicated MIDI icon.
 */
void
MidiListItem::SetRenderParameters(Renderer& renderer)
{
	// TODO: Create a nice icon
	renderer.AddIcon(&Icons()->mixerIcon);
}
