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
 * @file FileTypesWindow.cpp
 * @brief Implementation of the main MIME-type browser window. Owns the
 *        MIME tree on the left and a stack of editor groups on the
 *        right (icon, file recognition, description, preferred app,
 *        and Tracker attributes), plus a couple of small helper views
 *        (TypeIconView, ExtensionListView).
 */


#include "AttributeListView.h"
#include "AttributeWindow.h"
#include "DropTargetListView.h"
#include "ExtensionWindow.h"
#include "FileTypes.h"
#include "FileTypesWindow.h"
#include "IconView.h"
#include "MimeTypeListView.h"
#include "NewFileTypeWindow.h"
#include "PreferredAppMenu.h"
#include "StringView.h"

#include <Alignment.h>
#include <AppFileInfo.h>
#include <Application.h>
#include <Bitmap.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Locale.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <OutlineListView.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <SpaceLayoutItem.h>
#include <SplitView.h>
#include <TextControl.h>

#include <OverrideAlert.h>
#include <be_apps/Tracker/RecentItems.h>

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "FileTypes Window"


/** @brief A row in the MIME type list was selected. */
const uint32 kMsgTypeSelected = 'typs';
/** @brief Open the New File Type dialog. */
const uint32 kMsgAddType = 'atyp';
/** @brief Delete the currently selected MIME type. */
const uint32 kMsgRemoveType = 'rtyp';

/** @brief A row in the extension list was selected. */
const uint32 kMsgExtensionSelected = 'exts';
/** @brief A row in the extension list was double-clicked. */
const uint32 kMsgExtensionInvoked = 'exti';
/** @brief Open the Add Extension dialog. */
const uint32 kMsgAddExtension = 'aext';
/** @brief Remove the currently selected extension. */
const uint32 kMsgRemoveExtension = 'rext';
/** @brief The sniffer rule text control was edited. */
const uint32 kMsgRuleEntered = 'rule';

/** @brief A row in the attribute list was selected. */
const uint32 kMsgAttributeSelected = 'atrs';
/** @brief A row in the attribute list was double-clicked. */
const uint32 kMsgAttributeInvoked = 'atri';
/** @brief Open the Add Attribute dialog. */
const uint32 kMsgAddAttribute = 'aatr';
/** @brief Remove the currently selected attribute. */
const uint32 kMsgRemoveAttribute = 'ratr';
/** @brief Move the currently selected attribute up by one slot. */
const uint32 kMsgMoveUpAttribute = 'muat';
/** @brief Move the currently selected attribute down by one slot. */
const uint32 kMsgMoveDownAttribute = 'mdat';

/** @brief A preferred-application menu entry was chosen. */
const uint32 kMsgPreferredAppChosen = 'papc';
/** @brief Open the file panel to pick a preferred application. */
const uint32 kMsgSelectPreferredApp = 'slpa';
/** @brief Open the file panel to copy the preferred app from another file. */
const uint32 kMsgSamePreferredAppAs = 'spaa';

/** @brief File panel returned a preferred-application executable. */
const uint32 kMsgPreferredAppOpened = 'paOp';
/** @brief File panel returned a "same preferred app as" target. */
const uint32 kMsgSamePreferredAppAsOpened = 'spaO';

/** @brief The type-name text control was edited. */
const uint32 kMsgTypeEntered = 'type';
/** @brief The long description text control was edited. */
const uint32 kMsgDescriptionEntered = 'dsce';

/** @brief Toggle icons in the type list (Settings menu). */
const uint32 kMsgToggleIcons = 'tgic';
/** @brief Toggle the sniffer rule control's visibility (Settings menu). */
const uint32 kMsgToggleRule = 'tgrl';


/**
 * @brief Names of the parallel BMessage fields that make up a Tracker
 *        attribute description in BMimeType::GetAttrInfo() / SetAttrInfo().
 */
static const char* kAttributeNames[] = {
	"attr:public_name",
	"attr:name",
	"attr:type",
	"attr:editable",
	"attr:viewable",
	"attr:extra",
	"attr:alignment",
	"attr:width",
	"attr:display_as"
};


/**
 * @brief IconView subclass that renders a placeholder caption underneath
 *        the icon ("no icon", "(from application)", "(from super type)")
 *        when the type does not own its own icon.
 */
class TypeIconView : public IconView {
	typedef IconView _inherited;

	public:
		TypeIconView(const char* name);
		virtual ~TypeIconView();

		virtual void Draw(BRect updateRect);
		virtual void GetPreferredSize(float* _width, float* _height);

	protected:
		virtual BRect BitmapRect() const;
};


/**
 * @brief Drop-aware list view of file-name extensions for the currently
 *        selected MIME type; accepts ref drops and merges their suffixes.
 */
class ExtensionListView : public DropTargetListView {
	public:
		ExtensionListView(const char* name,
			list_view_type type = B_SINGLE_SELECTION_LIST,
			uint32 flags = B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE);
		virtual ~ExtensionListView();

		virtual	BSize MinSize();

		virtual void MessageReceived(BMessage* message);
		virtual bool AcceptsDrag(const BMessage* message);

		void SetType(BMimeType* type);

	private:
		BMimeType	fType;
		BSize		fMinSize;
};


//	#pragma mark -


/**
 * @brief Constructs the type-icon view with a 48 px icon and no empty
 *        frame placeholder.
 *
 * @param name  Layout name forwarded to the base IconView.
 */
TypeIconView::TypeIconView(const char* name)
	: IconView(name)
{
	ShowEmptyFrame(false);
	SetIconSize((icon_size)48);
}


/**
 * @brief Destructor; layout-managed children are released by the parent.
 */
TypeIconView::~TypeIconView()
{
}


/**
 * @brief Draws the icon and overlays a translated caption identifying
 *        the source of the displayed icon.
 *
 * @param updateRect  Region requested by the redraw notification.
 */
void
TypeIconView::Draw(BRect updateRect)
{
	if (!IsEnabled())
		return;

	IconView::Draw(updateRect);

	const char* text = NULL;

	switch (IconSource()) {
		case kNoIcon:
			text = B_TRANSLATE("no icon");
			break;
		case kApplicationIcon:
			text = B_TRANSLATE("(from application)");
			break;
		case kSupertypeIcon:
			text = B_TRANSLATE("(from super type)");
			break;

		default:
			return;
	}

	SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DISABLED_LABEL_TINT));
	SetLowColor(ViewColor());

	font_height fontHeight;
	GetFontHeight(&fontHeight);

	const BRect bitmapRect = _inherited::BitmapRect();
	float y = fontHeight.ascent;
	if (IconSource() == kNoIcon) {
		// center text in the middle of the icon
		y += (bitmapRect.Height() - fontHeight.ascent - fontHeight.descent) / 2.0f;
	} else
		y += bitmapRect.Height() + 3.0f;

	DrawString(text, BPoint(ceilf((Bounds().Width() - StringWidth(text)) / 2.0f),
		ceilf(y)));
}


/**
 * @brief Computes the preferred size, accounting for the longest
 *        translated caption variant.
 *
 * @param _width   Optional output: preferred width.
 * @param _height  Optional output: preferred height.
 */
void
TypeIconView::GetPreferredSize(float* _width, float* _height)
{
	const BRect bitmapRect = _inherited::BitmapRect();

	if (_width) {
		float a = StringWidth(B_TRANSLATE("(from application)"));
		float b = StringWidth(B_TRANSLATE("(from super type)"));
		float width = max_c(a, b);
		if (width < bitmapRect.Width())
			width = bitmapRect.Width();

		*_width = ceilf(width);
	}

	if (_height) {
		font_height fontHeight;
		GetFontHeight(&fontHeight);

		*_height = bitmapRect.Height() + 3.0f + ceilf(fontHeight.ascent
			+ fontHeight.descent);
	}
}


/**
 * @brief Returns the rectangle that is treated as the icon for hit
 *        testing and for delineating the drop-target area.
 *
 * In the "no icon" state the rectangle is recentred around the caption
 * so users can drop bitmaps onto the placeholder text.
 *
 * @return  Bitmap rectangle in view coordinates.
 */
BRect
TypeIconView::BitmapRect() const
{
	const BRect bitmapRect = _inherited::BitmapRect();

	if (IconSource() == kNoIcon) {
		// this also defines the drop target area
		font_height fontHeight;
		GetFontHeight(&fontHeight);

		float width = StringWidth(B_TRANSLATE("no icon")) + 8.0f;
		float height = ceilf(fontHeight.ascent + fontHeight.descent) + 6.0f;
		float x = (Bounds().Width() - width) / 2.0f;
		float y = ceilf((bitmapRect.Height() - fontHeight.ascent - fontHeight.descent)
			/ 2.0f) - 3.0f;

		return BRect(x, y, x + width, y + height);
	}

	float x = (Bounds().Width() - bitmapRect.Width()) / 2.0f;
	return BRect(x, 0.0f, x + bitmapRect.Width(), bitmapRect.Height());
}


//	#pragma mark -


/**
 * @brief Constructs the extension list view with no associated MIME type.
 *
 * @param name   Layout name.
 * @param type   Selection style.
 * @param flags  View creation flags.
 */
ExtensionListView::ExtensionListView(const char* name,
		list_view_type type, uint32 flags)
	:
	DropTargetListView(name, type, flags)
{
}


/**
 * @brief Destructor; items are owned by BListView and released by it.
 */
ExtensionListView::~ExtensionListView()
{
}


/**
 * @brief Computes a minimum size large enough for "...mmmmm" and three
 *        line heights, lazily caching the result.
 *
 * @return Minimum size in pixels.
 */
BSize
ExtensionListView::MinSize()
{
	if (!fMinSize.IsWidthSet()) {
		BFont font;
		GetFont(&font);
		fMinSize.width = font.StringWidth(".mmmmm");

		font_height height;
		font.GetHeight(&height);
		fMinSize.height = (height.ascent + height.descent + height.leading) * 3;
	}

	return fMinSize;
}


/**
 * @brief Routes ref drops into merge_extensions(), forwarding all other
 *        messages to the base implementation.
 *
 * Each dropped ref's file-name extension (the suffix after the first
 * dot) is collected into a temporary list and then merged into the
 * MIME type's extensions field.
 *
 * @param message  Incoming BMessage.
 */
void
ExtensionListView::MessageReceived(BMessage* message)
{
	if (message->WasDropped() && AcceptsDrag(message)) {
		// create extension list
		BList list;
		entry_ref ref;
		for (int32 index = 0; message->FindRef("refs", index, &ref) == B_OK;
				index++) {
			const char* point = strchr(ref.name, '.');
			if (point != NULL && point[1])
				list.AddItem(strdup(++point));
		}

		merge_extensions(fType, list);

		// delete extension list
		for (int32 index = list.CountItems(); index-- > 0;) {
			free(list.ItemAt(index));
		}
	} else
		DropTargetListView::MessageReceived(message);
}


/**
 * @brief Drag policy: accept only when a MIME type is bound and at
 *        least one ref carries a usable file-name extension.
 *
 * @param message  Drag payload to test.
 * @return         True if the drop should be allowed.
 */
bool
ExtensionListView::AcceptsDrag(const BMessage* message)
{
	if (fType.Type() == NULL)
		return false;

	int32 count = 0;
	entry_ref ref;

	for (int32 index = 0; message->FindRef("refs", index, &ref) == B_OK;
			index++) {
		const char* point = strchr(ref.name, '.');
		if (point != NULL && point[1])
			count++;
	}

	return count > 0;
}


/**
 * @brief Binds the list view to a MIME type so subsequent drops can be
 *        routed into its extensions field.
 *
 * @param type  Target MIME type, or NULL to disable drops.
 */
void
ExtensionListView::SetType(BMimeType* type)
{
	if (type != NULL)
		fType.SetTo(type->Type());
	else
		fType.Unset();
}


//	#pragma mark -


/**
 * @brief Constructs the main FileTypes browser window from persisted
 *        @a settings.
 *
 * Builds the menu bar, the MIME type list on the left, and the right-
 * hand stack of editor groups (Icon, File recognition, Description,
 * Preferred application, Extra attributes) inside a horizontal split
 * view, restores split weights from @a settings, and registers for MIME
 * database notifications.
 *
 * @param settings  Persistent settings BMessage.
 */
FileTypesWindow::FileTypesWindow(const BMessage& settings)
	:
	BWindow(_Frame(settings), B_TRANSLATE_SYSTEM_NAME("FileTypes"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS
		| B_AUTO_UPDATE_SIZE_LIMITS),
	fNewTypeWindow(NULL)
{
	bool showIcons;
	bool showRule;
	if (settings.FindBool("show_icons", &showIcons) != B_OK)
		showIcons = true;
	if (settings.FindBool("show_rule", &showRule) != B_OK)
		showRule = false;

	float padding = be_control_look->DefaultItemSpacing();
	BAlignment labelAlignment = be_control_look->DefaultLabelAlignment();
	BAlignment fullAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_USE_FULL_HEIGHT);

	// add the menu
	BMenuBar* menuBar = new BMenuBar("");

	BMenu* menu = new BMenu(B_TRANSLATE("File"));
	BMenuItem* item = new BMenuItem(
		B_TRANSLATE("New resource file" B_UTF8_ELLIPSIS), NULL, 'N',
		B_COMMAND_KEY);
	item->SetEnabled(false);
	menu->AddItem(item);

	BMenu* recentsMenu = BRecentFilesList::NewFileListMenu(
		B_TRANSLATE("Open" B_UTF8_ELLIPSIS), NULL, NULL,
		be_app, 10, false, NULL, kSignature);
	item = new BMenuItem(recentsMenu, new BMessage(kMsgOpenFilePanel));
	item->SetShortcut('O', B_COMMAND_KEY);
	menu->AddItem(item);

	menu->AddItem(new BMenuItem(
		B_TRANSLATE("Application types" B_UTF8_ELLIPSIS),
		new BMessage(kMsgOpenApplicationTypesWindow)));
	menu->AddSeparatorItem();

	menu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED), 'Q', B_COMMAND_KEY));
	menu->SetTargetForItems(be_app);
	menuBar->AddItem(menu);

	menu = new BMenu(B_TRANSLATE("Settings"));
	item = new BMenuItem(B_TRANSLATE("Show icons in list"),
		new BMessage(kMsgToggleIcons));
	item->SetMarked(showIcons);
	item->SetTarget(this);
	menu->AddItem(item);

	item = new BMenuItem(B_TRANSLATE("Show recognition rule"),
		new BMessage(kMsgToggleRule));
	item->SetMarked(showRule);
	item->SetTarget(this);
	menu->AddItem(item);
	menuBar->AddItem(menu);
	menuBar->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));

	// MIME Types list
	BButton* addTypeButton = new BButton("add",
		B_TRANSLATE("Add" B_UTF8_ELLIPSIS), new BMessage(kMsgAddType));

	fRemoveTypeButton = new BButton("remove", B_TRANSLATE("Remove"),
		new BMessage(kMsgRemoveType) );

	fTypeListView = new MimeTypeListView("typeview", NULL, showIcons, false);
	fTypeListView->SetSelectionMessage(new BMessage(kMsgTypeSelected));
	fTypeListView->SetExplicitMinSize(BSize(200, B_SIZE_UNSET));

	BScrollView* typeListScrollView = new BScrollView("scrollview",
		fTypeListView, B_FRAME_EVENTS | B_WILL_DRAW, false, true);

	// "Icon" group

	fIconView = new TypeIconView("icon");
	fIconBox = new BBox("Icon BBox");
	fIconBox->SetLabel(B_TRANSLATE("Icon"));
	BLayoutBuilder::Group<>(fIconBox, B_VERTICAL, padding)
		.SetInsets(padding)
		.AddGlue(1)
		.Add(fIconView, 3)
		.AddGlue(1);

	// "File Recognition" group

	fRecognitionBox = new BBox("Recognition Box");
	fRecognitionBox->SetLabel(B_TRANSLATE("File recognition"));
	fRecognitionBox->SetExplicitAlignment(fullAlignment);

	fExtensionLabel = new StringView(B_TRANSLATE("Extensions:"), NULL);
	fExtensionLabel->LabelView()->SetExplicitAlignment(labelAlignment);

	fAddExtensionButton = new BButton("add ext",
		B_TRANSLATE("Add" B_UTF8_ELLIPSIS), new BMessage(kMsgAddExtension));
	fAddExtensionButton->SetExplicitMaxSize(
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fRemoveExtensionButton = new BButton("remove ext", B_TRANSLATE("Remove"),
		new BMessage(kMsgRemoveExtension));

	fExtensionListView = new ExtensionListView("listview ext",
		B_SINGLE_SELECTION_LIST);
	fExtensionListView->SetSelectionMessage(
		new BMessage(kMsgExtensionSelected));
	fExtensionListView->SetInvocationMessage(
		new BMessage(kMsgExtensionInvoked));

	BScrollView* scrollView = new BScrollView("scrollview ext",
		fExtensionListView, B_FRAME_EVENTS | B_WILL_DRAW, false, true);

	fRuleControl = new BTextControl("rule", B_TRANSLATE("Rule:"), "",
		new BMessage(kMsgRuleEntered));
	fRuleControl->SetAlignment(B_ALIGN_RIGHT, B_ALIGN_LEFT);
	fRuleControl->Hide();

	BLayoutBuilder::Grid<>(fRecognitionBox, padding, padding / 2)
		.SetInsets(padding, padding * 2, padding, padding)
		.Add(fExtensionLabel->LabelView(), 0, 0)
		.Add(scrollView, 0, 1, 2, 2)
		.Add(fAddExtensionButton, 2, 1)
		.Add(fRemoveExtensionButton, 2, 2)
		.Add(fRuleControl, 0, 3, 3, 1);

	// "Description" group

	fDescriptionBox = new BBox("description BBox");
	fDescriptionBox->SetLabel(B_TRANSLATE("Description"));
	fDescriptionBox->SetExplicitAlignment(fullAlignment);

	fInternalNameView = new StringView(B_TRANSLATE("Internal name:"), NULL);
	fInternalNameView->SetEnabled(false);
	fTypeNameControl = new BTextControl("type", B_TRANSLATE("Type name:"), "",
		new BMessage(kMsgTypeEntered));
	fDescriptionControl = new BTextControl("description",
		B_TRANSLATE("Description:"), "", new BMessage(kMsgDescriptionEntered));

	BLayoutBuilder::Grid<>(fDescriptionBox, padding / 2, padding / 2)
		.SetInsets(padding, padding * 2, padding, padding)
		.Add(fInternalNameView->LabelView(), 0, 0)
		.Add(fInternalNameView->TextView(), 1, 0)
		.Add(fTypeNameControl->CreateLabelLayoutItem(), 0, 1)
		.Add(fTypeNameControl->CreateTextViewLayoutItem(), 1, 1, 2)
		.Add(fDescriptionControl->CreateLabelLayoutItem(), 0, 2)
		.Add(fDescriptionControl->CreateTextViewLayoutItem(), 1, 2, 2);

	// "Preferred Application" group

	fPreferredBox = new BBox("preferred BBox");
	fPreferredBox->SetLabel(B_TRANSLATE("Preferred application"));

	menu = new BPopUpMenu("preferred");
	menu->AddItem(item = new BMenuItem(B_TRANSLATE("None"),
		new BMessage(kMsgPreferredAppChosen)));
	item->SetMarked(true);
	fPreferredField = new BMenuField("preferred", (char*)NULL, menu);

	fSelectButton = new BButton("select",
		B_TRANSLATE("Select" B_UTF8_ELLIPSIS),
		new BMessage(kMsgSelectPreferredApp));

	fSameAsButton = new BButton("same as",
		B_TRANSLATE("Same as" B_UTF8_ELLIPSIS),
		new BMessage(kMsgSamePreferredAppAs));

	BLayoutBuilder::Group<>(fPreferredBox, B_HORIZONTAL, padding)
		.SetInsets(padding, padding * 2, padding, padding)
		.Add(fPreferredField)
		.Add(fSelectButton)
		.Add(fSameAsButton);

	// "Extra Attributes" group

	fAttributeBox = new BBox("Attribute Box");
	fAttributeBox->SetLabel(B_TRANSLATE("Extra attributes"));

	fAddAttributeButton = new BButton("add attr",
		B_TRANSLATE("Add" B_UTF8_ELLIPSIS), new BMessage(kMsgAddAttribute));
	fAddAttributeButton->SetExplicitMaxSize(
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fRemoveAttributeButton = new BButton("remove attr", B_TRANSLATE("Remove"),
		new BMessage(kMsgRemoveAttribute));
	fRemoveAttributeButton->SetExplicitMaxSize(
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fMoveUpAttributeButton = new BButton("move up attr", B_TRANSLATE("Move up"),
		new BMessage(kMsgMoveUpAttribute));
	fMoveUpAttributeButton->SetExplicitMaxSize(
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fMoveDownAttributeButton = new BButton("move down attr",
		B_TRANSLATE("Move down"), new BMessage(kMsgMoveDownAttribute));
	fMoveDownAttributeButton->SetExplicitMaxSize(
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fAttributeListView = new AttributeListView("listview attr");
	fAttributeListView->SetSelectionMessage(
		new BMessage(kMsgAttributeSelected));
	fAttributeListView->SetInvocationMessage(
		new BMessage(kMsgAttributeInvoked));

	BScrollView* attributesScroller = new BScrollView("scrollview attr",
		fAttributeListView, B_FRAME_EVENTS | B_WILL_DRAW, false, true);

	BLayoutBuilder::Group<>(fAttributeBox, B_HORIZONTAL, padding)
		.SetInsets(padding, padding * 2, padding, padding)
		.Add(attributesScroller, 1.0f)
		.AddGroup(B_VERTICAL, padding / 2, 0.0f)
			.SetInsets(0)
			.Add(fAddAttributeButton)
			.Add(fRemoveAttributeButton)
			.AddStrut(padding)
			.Add(fMoveUpAttributeButton)
			.Add(fMoveDownAttributeButton)
			.AddGlue();

	fMainSplitView = new BSplitView(B_HORIZONTAL, floorf(padding / 2));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0)
		.Add(menuBar)
		.AddGroup(B_HORIZONTAL, 0)
			.SetInsets(B_USE_WINDOW_SPACING)
			.AddSplit(fMainSplitView)
				.AddGroup(B_VERTICAL, padding)
					.Add(typeListScrollView)
					.AddGroup(B_HORIZONTAL, padding)
						.Add(addTypeButton)
						.Add(fRemoveTypeButton)
						.AddGlue()
						.End()
					.End()
				// Right side
				.AddGroup(B_VERTICAL, padding)
					.AddGroup(B_HORIZONTAL, padding)
						.Add(fIconBox, 1)
						.Add(fRecognitionBox, 3)
						.End()
					.Add(fDescriptionBox)
					.Add(fPreferredBox)
					.Add(fAttributeBox, 5);

	_SetType(NULL);
	_ShowSnifferRule(showRule);

	float leftWeight;
	float rightWeight;
	if (settings.FindFloat("left_split_weight", &leftWeight) != B_OK
		|| settings.FindFloat("right_split_weight", &rightWeight) != B_OK) {
		leftWeight = 0.2;
		rightWeight = 1.0 - leftWeight;
	}
	fMainSplitView->SetItemWeight(0, leftWeight, false);
	fMainSplitView->SetItemWeight(1, rightWeight, true);

	BMimeType::StartWatching(this);
}


/**
 * @brief Stops MIME database watching and tears the window down.
 */
FileTypesWindow::~FileTypesWindow()
{
	BMimeType::StopWatching(this);
}


/**
 * @brief Top-level message dispatch for every editor pane in the window.
 *
 * Routes selection changes, add/remove/edit actions for types,
 * extensions, and attributes, the sniffer-rule and description text
 * controls, file-panel results for the preferred-application controls,
 * the Settings menu toggles, and incremental MIME database
 * notifications back to the appropriate helper.
 *
 * @param message  Incoming BMessage.
 */
void
FileTypesWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_SIMPLE_DATA:
		{
			type_code type;
			if (message->GetInfo("refs", &type) == B_OK
				&& type == B_REF_TYPE) {
				be_app->PostMessage(message);
			}
			break;
		}

		case kMsgToggleIcons:
		{
			BMenuItem* item;
			if (message->FindPointer("source", (void **)&item) != B_OK)
				break;

			item->SetMarked(!fTypeListView->IsShowingIcons());
			fTypeListView->ShowIcons(item->IsMarked());

			// update settings
			BMessage update(kMsgSettingsChanged);
			update.AddBool("show_icons", item->IsMarked());
			be_app_messenger.SendMessage(&update);
			break;
		}

		case kMsgToggleRule:
		{
			BMenuItem* item;
			if (message->FindPointer("source", (void **)&item) != B_OK)
				break;

			item->SetMarked(fRuleControl->IsHidden());
			_ShowSnifferRule(item->IsMarked());

			// update settings
			BMessage update(kMsgSettingsChanged);
			update.AddBool("show_rule", item->IsMarked());
			be_app_messenger.SendMessage(&update);
			break;
		}

		case kMsgTypeSelected:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				MimeTypeItem* item
					= (MimeTypeItem*)fTypeListView->ItemAt(index);
				if (item != NULL) {
					BMimeType type(item->Type());
					_SetType(&type);
				} else
					_SetType(NULL);
			}
			break;
		}

		case kMsgAddType:
			if (fNewTypeWindow == NULL) {
				fNewTypeWindow
					= new NewFileTypeWindow(this, fCurrentType.Type());
				fNewTypeWindow->Show();
			} else
				fNewTypeWindow->Activate();
			break;

		case kMsgNewTypeWindowClosed:
			fNewTypeWindow = NULL;
			break;

		case kMsgRemoveType:
		{
			if (fCurrentType.Type() == NULL)
				break;

			BAlert* alert;
			if (fCurrentType.IsSupertypeOnly()) {
				alert = new BPrivate::OverrideAlert(
					B_TRANSLATE("FileTypes request"),
					B_TRANSLATE("Removing a super type cannot be reverted.\n"
					"All file types that belong to this super type "
					"will be lost!\n\n"
					"Are you sure you want to do this? To remove the whole "
					"group, hold down the Shift key and press \"Remove\"."),
					B_TRANSLATE("Remove"), B_SHIFT_KEY, B_TRANSLATE("Cancel"),
					0, NULL, 0, B_WIDTH_AS_USUAL, B_STOP_ALERT);
				alert->SetShortcut(1, B_ESCAPE);
			} else {
				alert = new BAlert(B_TRANSLATE("FileTypes request"),
					B_TRANSLATE("Removing a file type cannot be reverted.\n"
					"Are you sure you want to remove it?"),
					B_TRANSLATE("Remove"), B_TRANSLATE("Cancel"),
					NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
				alert->SetShortcut(1, B_ESCAPE);
			}
			if (alert->Go())
				break;

			status_t status = fCurrentType.Delete();
			if (status != B_OK) {
				fprintf(stderr, B_TRANSLATE(
					"Could not remove file type: %s\n"), strerror(status));
			}
			break;
		}

		case kMsgSelectNewType:
		{
			const char* type;
			if (message->FindString("type", &type) == B_OK)
				fTypeListView->SelectNewType(type);
			break;
		}

		// File Recognition group

		case kMsgExtensionSelected:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				BStringItem* item
					= (BStringItem*)fExtensionListView->ItemAt(index);
				fRemoveExtensionButton->SetEnabled(item != NULL);
			}
			break;
		}

		case kMsgExtensionInvoked:
		{
			if (fCurrentType.Type() == NULL)
				break;

			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				BStringItem* item
					= (BStringItem*)fExtensionListView->ItemAt(index);
				if (item == NULL)
					break;

				BWindow* window
					= new ExtensionWindow(this, fCurrentType, item->Text());
				window->Show();
			}
			break;
		}

		case kMsgAddExtension:
		{
			if (fCurrentType.Type() == NULL)
				break;

			BWindow* window = new ExtensionWindow(this, fCurrentType, NULL);
			window->Show();
			break;
		}

		case kMsgRemoveExtension:
		{
			int32 index = fExtensionListView->CurrentSelection();
			if (index < 0 || fCurrentType.Type() == NULL)
				break;

			BMessage extensions;
			if (fCurrentType.GetFileExtensions(&extensions) == B_OK) {
				extensions.RemoveData("extensions", index);
				fCurrentType.SetFileExtensions(&extensions);
			}
			break;
		}

		case kMsgRuleEntered:
		{
			// check rule
			BString parseError;
			if (BMimeType::CheckSnifferRule(fRuleControl->Text(),
					&parseError) != B_OK) {
				parseError.Prepend(
					B_TRANSLATE("Recognition rule is not valid:\n\n"));
				error_alert(parseError.String());
			} else
				fCurrentType.SetSnifferRule(fRuleControl->Text());
			break;
		}

		// Description group

		case kMsgTypeEntered:
		{
			fCurrentType.SetShortDescription(fTypeNameControl->Text());
			break;
		}

		case kMsgDescriptionEntered:
		{
			fCurrentType.SetLongDescription(fDescriptionControl->Text());
			break;
		}

		// Preferred Application group

		case kMsgPreferredAppChosen:
		{
			const char* signature;
			if (message->FindString("signature", &signature) != B_OK)
				signature = NULL;

			fCurrentType.SetPreferredApp(signature);
			break;
		}

		case kMsgSelectPreferredApp:
		{
			BMessage panel(kMsgOpenFilePanel);
			panel.AddString("title",
				B_TRANSLATE("Select preferred application"));
			panel.AddInt32("message", kMsgPreferredAppOpened);
			panel.AddMessenger("target", this);

			be_app_messenger.SendMessage(&panel);
			break;
		}
		case kMsgPreferredAppOpened:
			_AdoptPreferredApplication(message, false);
			break;

		case kMsgSamePreferredAppAs:
		{
			BMessage panel(kMsgOpenFilePanel);
			panel.AddString("title",
				B_TRANSLATE("Select same preferred application as"));
			panel.AddInt32("message", kMsgSamePreferredAppAsOpened);
			panel.AddMessenger("target", this);
			panel.AddBool("allowDirs", true);

			be_app_messenger.SendMessage(&panel);
			break;
		}
		case kMsgSamePreferredAppAsOpened:
			_AdoptPreferredApplication(message, true);
			break;

		// Extra Attributes group

		case kMsgAttributeSelected:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				AttributeItem* item
					= (AttributeItem*)fAttributeListView->ItemAt(index);
				fRemoveAttributeButton->SetEnabled(item != NULL);
				fMoveUpAttributeButton->SetEnabled(index > 0);
				fMoveDownAttributeButton->SetEnabled(index >= 0
					&& index < fAttributeListView->CountItems() - 1);
			}
			break;
		}

		case kMsgAttributeInvoked:
		{
			if (fCurrentType.Type() == NULL)
				break;

			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				AttributeItem* item
					= (AttributeItem*)fAttributeListView->ItemAt(index);
				if (item == NULL)
					break;

				BWindow* window = new AttributeWindow(this, fCurrentType,
					item);
				window->Show();
			}
			break;
		}

		case kMsgAddAttribute:
		{
			if (fCurrentType.Type() == NULL)
				break;

			BWindow* window = new AttributeWindow(this, fCurrentType, NULL);
			window->Show();
			break;
		}

		case kMsgRemoveAttribute:
		{
			int32 index = fAttributeListView->CurrentSelection();
			if (index < 0 || fCurrentType.Type() == NULL)
				break;

			BMessage attributes;
			if (fCurrentType.GetAttrInfo(&attributes) == B_OK) {
				for (uint32 i = 0; i <
						sizeof(kAttributeNames) / sizeof(kAttributeNames[0]);
						i++) {
					attributes.RemoveData(kAttributeNames[i], index);
				}

				fCurrentType.SetAttrInfo(&attributes);
			}
			break;
		}

		case kMsgMoveUpAttribute:
		{
			int32 index = fAttributeListView->CurrentSelection();
			if (index < 1 || fCurrentType.Type() == NULL)
				break;

			_MoveUpAttributeIndex(index);
			break;
		}

		case kMsgMoveDownAttribute:
		{
			int32 index = fAttributeListView->CurrentSelection();
			if (index < 0 || index == fAttributeListView->CountItems() - 1
				|| fCurrentType.Type() == NULL) {
				break;
			}

			_MoveUpAttributeIndex(index + 1);
			break;
		}

		case B_META_MIME_CHANGED:
		{
			const char* type;
			int32 which;
			if (message->FindString("be:type", &type) != B_OK
				|| message->FindInt32("be:which", &which) != B_OK)
				break;

			if (fCurrentType.Type() == NULL)
				break;

			if (!strcasecmp(fCurrentType.Type(), type)) {
				if (which != B_MIME_TYPE_DELETED)
					_SetType(&fCurrentType, which);
				else
					_SetType(NULL);
			} else {
				// this change could still affect our current type

				if (which == B_MIME_TYPE_DELETED
					|| which == B_SUPPORTED_TYPES_CHANGED
					|| which == B_PREFERRED_APP_CHANGED) {
					_UpdatePreferredApps(&fCurrentType);
				}
			}
			break;
		}

		default:
			BWindow::MessageReceived(message);
	}
}


/**
 * @brief Programmatically selects a MIME type by identifier.
 *
 * Used by the FileTypes BApplication when the user passes "-type ..."
 * on the command line.
 *
 * @param type  MIME identifier to look up and select.
 */
void
FileTypesWindow::SelectType(const char* type)
{
	fTypeListView->SelectType(type);
}


/**
 * @brief Persists window frame and split weights, then notifies the
 *        BApplication that the main window is closing.
 *
 * @return Always true.
 */
bool
FileTypesWindow::QuitRequested()
{
	BMessage update(kMsgSettingsChanged);
	update.AddRect("file_types_frame", Frame());
	update.AddFloat("left_split_weight", fMainSplitView->ItemWeight((int32)0));
	update.AddFloat("right_split_weight", fMainSplitView->ItemWeight(1));
	be_app_messenger.SendMessage(&update);

	be_app->PostMessage(kMsgTypesWindowClosed);
	return true;
}


/**
 * @brief Centres @a window over this main window.
 *
 * Used by sub-dialogs (NewFileTypeWindow, ExtensionWindow,
 * AttributeWindow) so they appear visually anchored to the parent.
 *
 * @param window  Sub-window to move.
 */
void
FileTypesWindow::PlaceSubWindow(BWindow* window)
{
	window->MoveTo(Frame().left + (Frame().Width() - window->Frame().Width())
		/ 2.0f, Frame().top + (Frame().Height() - window->Frame().Height())
		/ 2.0f);
}


// #pragma mark - private


/**
 * @brief Returns the saved frame from @a settings or a default one.
 *
 * @param settings  Persistent settings message.
 * @return          Frame in screen coordinates; the default rectangle
 *                  has zero width/height to let the layout system size
 *                  the window from its content.
 */
BRect
FileTypesWindow::_Frame(const BMessage& settings) const
{
	BRect rect;
	if (settings.FindRect("file_types_frame", &rect) == B_OK)
		return rect;

	return BRect(80.0f, 80.0f, 0.0f, 0.0f);
}


/**
 * @brief Hides or unhides the sniffer rule text control to match the
 *        current Settings menu state.
 *
 * @param show  True to make the control visible.
 */
void
FileTypesWindow::_ShowSnifferRule(bool show)
{
	if (fRuleControl->IsHidden() == !show)
		return;

	if (!show)
		fRuleControl->Hide();
	else
		fRuleControl->Show();
}


/**
 * @brief Repopulates the extensions list view from @a type's
 *        BMimeType::GetFileExtensions() result.
 *
 * Existing items are deleted; a leading dot is prepended to each
 * extension so it reads naturally in the UI.
 *
 * @param type  Type to read from. NULL leaves the list empty.
 */
void
FileTypesWindow::_UpdateExtensions(BMimeType* type)
{
	// clear list

	for (int32 i = fExtensionListView->CountItems(); i-- > 0;) {
		delete fExtensionListView->ItemAt(i);
	}
	fExtensionListView->MakeEmpty();

	// fill it again

	if (type == NULL)
		return;

	BMessage extensions;
	if (type->GetFileExtensions(&extensions) != B_OK)
		return;

	const char* extension;
	int32 i = 0;
	while (extensions.FindString("extensions", i++, &extension) == B_OK) {
		char dotExtension[B_FILE_NAME_LENGTH];
		snprintf(dotExtension, B_FILE_NAME_LENGTH, ".%s", extension);

		fExtensionListView->AddItem(new BStringItem(dotExtension));
	}
}


/**
 * @brief Resolves the preferred application from @a message and writes
 *        it onto the current MIME type.
 *
 * @param message  Refs message returned by the file panel.
 * @param sameAs   True for "use the same preferred app as this file";
 *                 false for "use this file as the application".
 */
void
FileTypesWindow::_AdoptPreferredApplication(BMessage* message, bool sameAs)
{
	if (fCurrentType.Type() == NULL)
		return;

	BString preferred;
	if (retrieve_preferred_app(message, sameAs, fCurrentType.Type(), preferred)
		!= B_OK) {
		return;
	}

	status_t status = fCurrentType.SetPreferredApp(preferred.String());
	if (status != B_OK)
		error_alert(B_TRANSLATE("Could not set preferred application"),
			status);
}


void
FileTypesWindow::_UpdatePreferredApps(BMimeType* type)
{
	update_preferred_app_menu(fPreferredField->Menu(), type,
		kMsgPreferredAppChosen);
}


void
FileTypesWindow::_UpdateIcon(BMimeType* type)
{
	if (type != NULL)
		fIconView->SetTo(*type);
	else
		fIconView->Unset();
}


void
FileTypesWindow::_SetType(BMimeType* type, int32 forceUpdate)
{
	bool enabled = type != NULL;

	// update controls

	if (type != NULL) {
		if (fCurrentType == *type) {
			if (!forceUpdate)
				return;
		} else
			forceUpdate = B_EVERYTHING_CHANGED;

		if (&fCurrentType != type)
			fCurrentType.SetTo(type->Type());

		fInternalNameView->SetText(type->Type());

		char description[B_MIME_TYPE_LENGTH];

		if ((forceUpdate & B_SHORT_DESCRIPTION_CHANGED) != 0) {
			if (type->GetShortDescription(description) != B_OK)
				description[0] = '\0';
			fTypeNameControl->SetText(description);
		}

		if ((forceUpdate & B_LONG_DESCRIPTION_CHANGED) != 0) {
			if (type->GetLongDescription(description) != B_OK)
				description[0] = '\0';
			fDescriptionControl->SetText(description);
		}

		if ((forceUpdate & B_SNIFFER_RULE_CHANGED) != 0) {
			BString rule;
			if (type->GetSnifferRule(&rule) != B_OK)
				rule = "";
			fRuleControl->SetText(rule.String());
		}

		fExtensionListView->SetType(&fCurrentType);
	} else {
		fCurrentType.Unset();
		fInternalNameView->SetText(NULL);
		fTypeNameControl->SetText(NULL);
		fDescriptionControl->SetText(NULL);
		fRuleControl->SetText(NULL);
		fPreferredField->Menu()->ItemAt(0)->SetMarked(true);
		fExtensionListView->SetType(NULL);
		fAttributeListView->SetTo(NULL);
	}

	if ((forceUpdate & B_FILE_EXTENSIONS_CHANGED) != 0)
		_UpdateExtensions(type);

	if ((forceUpdate & B_PREFERRED_APP_CHANGED) != 0)
		_UpdatePreferredApps(type);

	if ((forceUpdate & (B_ICON_CHANGED | B_PREFERRED_APP_CHANGED)) != 0)
		_UpdateIcon(type);

	if ((forceUpdate & B_ATTR_INFO_CHANGED) != 0)
		fAttributeListView->SetTo(type);

	// enable/disable controls

	fIconView->SetEnabled(enabled);

	fInternalNameView->SetEnabled(enabled);
	fTypeNameControl->SetEnabled(enabled);
	fDescriptionControl->SetEnabled(enabled);
	fPreferredField->SetEnabled(enabled);

	fRemoveTypeButton->SetEnabled(enabled);

	fSelectButton->SetEnabled(enabled);
	fSameAsButton->SetEnabled(enabled);

	fExtensionLabel->SetEnabled(enabled);
	fAddExtensionButton->SetEnabled(enabled);
	fRemoveExtensionButton->SetEnabled(false);
	fRuleControl->SetEnabled(enabled);

	fAddAttributeButton->SetEnabled(enabled);
	fRemoveAttributeButton->SetEnabled(false);
	fMoveUpAttributeButton->SetEnabled(false);
	fMoveDownAttributeButton->SetEnabled(false);
}


void
FileTypesWindow::_MoveUpAttributeIndex(int32 index)
{
	BMessage attributes;
	if (fCurrentType.GetAttrInfo(&attributes) != B_OK)
		return;

	// Iterate over all known attribute fields, and for each field,
	// iterate over all fields of the same name and build a copy
	// of the attributes message with the field at the given index swapped
	// with the previous field.
	BMessage resortedAttributes;
	for (uint32 i = 0; i <
			sizeof(kAttributeNames) / sizeof(kAttributeNames[0]);
			i++) {

		type_code type;
		int32 count;
		bool isFixedSize;
		if (attributes.GetInfo(kAttributeNames[i], &type, &count,
				&isFixedSize) != B_OK) {
			// Apparently the message does not contain this name,
			// so just ignore this attribute name.
			// NOTE: This shows that the attribute description is
			// too fragile. It would have been better to pack each
			// attribute description into a separate BMessage.
			continue;
		}

		for (int32 j = 0; j < count; j++) {
			const void* data;
			ssize_t size;
			int32 originalIndex;
			if (j == index - 1)
				originalIndex = j + 1;
			else if (j == index)
				originalIndex = j - 1;
			else
				originalIndex = j;
			attributes.FindData(kAttributeNames[i], type,
				originalIndex, &data, &size);
			if (j == 0) {
				resortedAttributes.AddData(kAttributeNames[i], type,
					data, size, isFixedSize);
			} else {
				resortedAttributes.AddData(kAttributeNames[i], type,
					data, size);
			}
		}
	}

	// Setting it directly on the type will trigger an update of the GUI as
	// well. TODO: FileTypes is heavily descructive, it should use an
	// Undo/Redo stack.
	fCurrentType.SetAttrInfo(&resortedAttributes);
}


