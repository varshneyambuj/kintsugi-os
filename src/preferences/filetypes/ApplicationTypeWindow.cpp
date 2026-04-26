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
 * @file ApplicationTypeWindow.cpp
 * @brief Editor for an installed application's MIME-type registration.
 *
 * Lets the user inspect and modify the BMessage-shaped record stored in a
 * binary's resources: signature, application flags, icon, supported
 * types, and version info. Writes are committed back via BAppFileInfo on
 * Save.
 */


#include "ApplicationTypeWindow.h"
#include "DropTargetListView.h"
#include "FileTypes.h"
#include "IconView.h"
#include "PreferredAppMenu.h"
#include "StringView.h"
#include "TypeListWindow.h"

#include <Application.h>
#include <Bitmap.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <File.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Locale.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <Roster.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Application Type Window"


/** @brief Posted by the File menu's Save item. */
const uint32 kMsgSave = 'save';
/** @brief Live notification from the signature text control. */
const uint32 kMsgSignatureChanged = 'sgch';
/** @brief Posted when the "Application flags" check box is toggled. */
const uint32 kMsgToggleAppFlags = 'tglf';
/** @brief Posted whenever any of the launch-flag radio buttons or the
           args-only/background check boxes change. */
const uint32 kMsgAppFlagsChanged = 'afch';

/** @brief Posted when the application's own icon view is edited. */
const uint32 kMsgIconChanged = 'icch';
/** @brief Posted when an icon for one of the supported MIME types is
           edited. */
const uint32 kMsgTypeIconsChanged = 'tich';

/** @brief Posted when any version-info text control changes. */
const uint32 kMsgVersionInfoChanged = 'tvch';

/** @brief Listview selection-change message in the supported types
           pane. */
const uint32 kMsgTypeSelected = 'tpsl';
/** @brief Add-supported-type button click. */
const uint32 kMsgAddType = 'adtp';
/** @brief Reply from TypeListWindow carrying the chosen type. */
const uint32 kMsgTypeAdded = 'tpad';
/** @brief Remove-supported-type button click. */
const uint32 kMsgRemoveType = 'rmtp';
/** @brief Reserved for future use when the sister window confirms a
           removal. */
const uint32 kMsgTypeRemoved = 'tprm';


/**
 * @brief BTextView subclass that intercepts the Tab key so it advances
 *        keyboard focus instead of inserting a tab character.
 *
 * Optionally posts a "changed" message to the window after every text
 * mutation so the parent can recompute the dirty flag.
 */
class TabFilteringTextView : public BTextView {
public:
								TabFilteringTextView(const char* name,
									uint32 changedMessageWhat = 0);
	virtual						~TabFilteringTextView();

	virtual void				InsertText(const char* text, int32 length,
									int32 offset, const text_run_array* runs);
	virtual	void				DeleteText(int32 fromOffset, int32 toOffset);
	virtual	void				KeyDown(const char* bytes, int32 count);
	virtual	void				TargetedByScrollView(BScrollView* scroller);
	virtual	void				MakeFocus(bool focused = true);

private:
			BScrollView*		fScrollView;
			uint32				fChangedMessageWhat;
};


/**
 * @brief List row representing one MIME type the application declares it
 *        can open, paired with its custom icon.
 */
class SupportedTypeItem : public BStringItem {
public:
								SupportedTypeItem(const char* type);
	virtual						~SupportedTypeItem();

			const char*			Type() const { return fType.String(); }
			::Icon&				Icon() { return fIcon; }

			void				SetIcon(::Icon* icon);
			void				SetIcon(entry_ref& ref, const char* type);

	static	int					Compare(const void* _a, const void* _b);

private:
			BString				fType;
			::Icon				fIcon;
};


/**
 * @brief DropTargetListView that accepts file drops and synthesises new
 *        supported-type rows from the dragged files' MIME types.
 */
class SupportedTypeListView : public DropTargetListView {
public:
								SupportedTypeListView(const char* name,
									list_view_type
										type = B_SINGLE_SELECTION_LIST,
									uint32 flags = B_WILL_DRAW
										| B_FRAME_EVENTS | B_NAVIGABLE);
	virtual						~SupportedTypeListView();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				AcceptsDrag(const BMessage* message);
};


// #pragma mark -


/**
 * @brief Constructs the text view with optional change-notification.
 *
 * @param name                 BView name forwarded to BTextView.
 * @param changedMessageWhat   Message what code posted to the window after
 *                             every mutation; @c 0 disables the
 *                             notification.
 */
TabFilteringTextView::TabFilteringTextView(const char* name,
	uint32 changedMessageWhat)
	:
	BTextView(name, B_WILL_DRAW | B_PULSE_NEEDED | B_NAVIGABLE),
	fScrollView(NULL),
	fChangedMessageWhat(changedMessageWhat)
{
}


/** @brief Trivial destructor; the optional change message is owned by the
           BTextView base. */
TabFilteringTextView::~TabFilteringTextView()
{
}


/**
 * @brief Inserts @a text at @a offset and posts the optional changed
 *        notification.
 *
 * @param text    Bytes to insert.
 * @param length  Length of @a text in bytes.
 * @param offset  Insertion offset measured in bytes from the start.
 * @param runs    Optional formatting array passed through unchanged.
 */
void
TabFilteringTextView::InsertText(const char* text, int32 length, int32 offset,
	const text_run_array* runs)
{
	BTextView::InsertText(text, length, offset, runs);
	if (fChangedMessageWhat != 0)
		Window()->PostMessage(fChangedMessageWhat);
}


/**
 * @brief Deletes the byte range and posts the optional changed
 *        notification.
 *
 * @param fromOffset  Start of the range, inclusive.
 * @param toOffset    End of the range, exclusive.
 */
void
TabFilteringTextView::DeleteText(int32 fromOffset, int32 toOffset)
{
	BTextView::DeleteText(fromOffset, toOffset);
	if (fChangedMessageWhat != 0)
		Window()->PostMessage(fChangedMessageWhat);
}


/**
 * @brief Routes Tab keystrokes through BView so focus traversal works,
 *        leaving every other key to the BTextView default.
 *
 * @param bytes  UTF-8 bytes for the keystroke.
 * @param count  Length of @a bytes.
 */
void
TabFilteringTextView::KeyDown(const char* bytes, int32 count)
{
	if (bytes[0] == B_TAB)
		BView::KeyDown(bytes, count);
	else
		BTextView::KeyDown(bytes, count);
}


/**
 * @brief Records the wrapping scroll view so its border highlight tracks
 *        focus.
 *
 * @param scroller  Outer BScrollView that contains this text view.
 */
void
TabFilteringTextView::TargetedByScrollView(BScrollView* scroller)
{
	fScrollView = scroller;
}


/**
 * @brief Forwards focus changes and toggles the scroll view's highlighted
 *        border.
 *
 * @param focused  @c true if the view is gaining focus, @c false if it
 *                 is losing focus.
 */
void
TabFilteringTextView::MakeFocus(bool focused)
{
	BTextView::MakeFocus(focused);

	if (fScrollView)
		fScrollView->SetBorderHighlighted(focused);
}


// #pragma mark -


/**
 * @brief Constructs a row for the MIME type @a type.
 *
 * Replaces the default label with the type's localised short description
 * when one is registered, falling back to the raw type string otherwise.
 *
 * @param type  Full MIME type identifier.
 */
SupportedTypeItem::SupportedTypeItem(const char* type)
	: BStringItem(type),
	fType(type)
{
	BMimeType mimeType(type);

	char description[B_MIME_TYPE_LENGTH];
	if (mimeType.GetShortDescription(description) == B_OK && description[0])
		SetText(description);
}


/** @brief Trivial destructor; the embedded Icon owns its own data. */
SupportedTypeItem::~SupportedTypeItem()
{
}


/**
 * @brief Replaces this row's icon by copy, or clears it when @a icon is
 *        @c NULL.
 *
 * @param icon  Source icon copied into the row; @c NULL to remove the
 *              icon.
 */
void
SupportedTypeItem::SetIcon(::Icon* icon)
{
	if (icon != NULL)
		fIcon = *icon;
	else
		fIcon.Unset();
}


/**
 * @brief Loads this row's icon from the per-type icon stored on the file
 *        @a ref.
 *
 * @param ref   Application binary whose resources hold the icon.
 * @param type  Specific MIME type whose icon should be read.
 */
void
SupportedTypeItem::SetIcon(entry_ref& ref, const char* type)
{
	fIcon.SetTo(ref, type);
}


/**
 * @brief BList-style comparator that orders rows by display label and
 *        breaks ties on the underlying type string.
 *
 * @param _a  Pointer to a SupportedTypeItem pointer.
 * @param _b  Pointer to a SupportedTypeItem pointer.
 * @return Negative when @a a sorts before @a b, positive when after, zero
 *         when equal.
 */
/*static*/
int
SupportedTypeItem::Compare(const void* _a, const void* _b)
{
	const SupportedTypeItem* a = *(const SupportedTypeItem**)_a;
	const SupportedTypeItem* b = *(const SupportedTypeItem**)_b;

	int compare = strcasecmp(a->Text(), b->Text());
	if (compare != 0)
		return compare;

	return strcasecmp(a->Type(), b->Type());
}


//	#pragma mark -


/**
 * @brief Constructs the listview, forwarding all parameters to
 *        DropTargetListView.
 *
 * @param name   View name.
 * @param type   Selection mode (single or multiple).
 * @param flags  Standard BView creation flags.
 */
SupportedTypeListView::SupportedTypeListView(const char* name,
	list_view_type type, uint32 flags)
	:
	DropTargetListView(name, type, flags)
{
}


/** @brief Trivial destructor. */
SupportedTypeListView::~SupportedTypeListView()
{
}


/**
 * @brief Handles drops by adding each dragged file's MIME type as a new
 *        unique row, then re-sorting alphabetically.
 *
 * @param message  Incoming BMessage; only drop messages are intercepted,
 *                 others fall through to the base class.
 * @todo Identify the file when it has no MIME type so we can still suggest
 *       a type to add.
 */
void
SupportedTypeListView::MessageReceived(BMessage* message)
{
	if (message->WasDropped() && AcceptsDrag(message)) {
		// Add unique types
		entry_ref ref;
		for (int32 index = 0; message->FindRef("refs", index++, &ref) == B_OK; ) {
			BNode node(&ref);
			BNodeInfo info(&node);
			if (node.InitCheck() != B_OK || info.InitCheck() != B_OK)
				continue;

			// TODO: we could identify the file in case it doesn't have a type...
			char type[B_MIME_TYPE_LENGTH];
			if (info.GetType(type) != B_OK)
				continue;

			// check if that type is already in our list
			bool found = false;
			for (int32 i = CountItems(); i-- > 0;) {
				SupportedTypeItem* item = (SupportedTypeItem*)ItemAt(i);
				if (!strcmp(item->Text(), type)) {
					found = true;
					break;
				}
			}

			if (!found) {
				// add type
				AddItem(new SupportedTypeItem(type));
			}
		}

		SortItems(&SupportedTypeItem::Compare);
	} else
		DropTargetListView::MessageReceived(message);
}


/**
 * @brief Reports whether the listview should accept @a message as a drag.
 *
 * @param message  Drag message; only those carrying @c B_REF_TYPE refs
 *                 are accepted.
 * @return @c true when at least one entry_ref is present.
 */
bool
SupportedTypeListView::AcceptsDrag(const BMessage* message)
{
	type_code type;
	return message->GetInfo("refs", &type) == B_OK && type == B_REF_TYPE;
}


//	#pragma mark -


/**
 * @brief Lays out the editor and pre-fills it from the resources of the
 *        application at @a entry.
 *
 * Restores the previously saved frame from @a settings if present,
 * subscribes to MIME-database change notifications, and gives focus to
 * the signature field.
 *
 * @param settings  Persisted preferences; consulted for the
 *                  @c "app_type_next_frame" rect.
 * @param entry     Application binary to edit.
 */
ApplicationTypeWindow::ApplicationTypeWindow(const BMessage& settings, const BEntry& entry)
	:
	BWindow(_Frame(settings), B_TRANSLATE("Application type"), B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS | B_FRAME_EVENTS | B_AUTO_UPDATE_SIZE_LIMITS),
	fChangedProperties(0)
{
	float padding = be_control_look->DefaultItemSpacing();
	BAlignment labelAlignment = be_control_look->DefaultLabelAlignment();

	BMenuBar* menuBar = new BMenuBar((char*)NULL);
	menuBar->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));

	BMenu* menu = new BMenu(B_TRANSLATE("File"));
	fSaveMenuItem = new BMenuItem(B_TRANSLATE("Save"),
		new BMessage(kMsgSave), 'S');
	fSaveMenuItem->SetEnabled(false);
	menu->AddItem(fSaveMenuItem);
	BMenuItem* item;
	menu->AddItem(item = new BMenuItem(
		B_TRANSLATE("Save into resource file" B_UTF8_ELLIPSIS), NULL));
	item->SetEnabled(false);

	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Close"),
		new BMessage(B_QUIT_REQUESTED), 'W', B_COMMAND_KEY));
	menuBar->AddItem(menu);

	// Signature

	fSignatureControl = new BTextControl("signature",
		B_TRANSLATE("Signature:"), NULL, new BMessage(kMsgSignatureChanged));
	fSignatureControl->SetModificationMessage(
		new BMessage(kMsgSignatureChanged));

	// filter out invalid characters that can't be part of a MIME type name
	BTextView* textView = fSignatureControl->TextView();
	textView->SetMaxBytes(B_MIME_TYPE_LENGTH);
	const char* disallowedCharacters = "<>@,;:\"()[]?= ";
	for (int32 i = 0; disallowedCharacters[i]; i++) {
		textView->DisallowChar(disallowedCharacters[i]);
	}

	// "Application Flags" group

	BBox* flagsBox = new BBox("flagsBox");

	fFlagsCheckBox = new BCheckBox("flags", B_TRANSLATE("Application flags"),
		new BMessage(kMsgToggleAppFlags));
	fFlagsCheckBox->SetValue(B_CONTROL_ON);

	fSingleLaunchButton = new BRadioButton("single",
		B_TRANSLATE("Single launch"), new BMessage(kMsgAppFlagsChanged));

	fMultipleLaunchButton = new BRadioButton("multiple",
		B_TRANSLATE("Multiple launch"), new BMessage(kMsgAppFlagsChanged));

	fExclusiveLaunchButton = new BRadioButton("exclusive",
		B_TRANSLATE("Exclusive launch"), new BMessage(kMsgAppFlagsChanged));

	fArgsOnlyCheckBox = new BCheckBox("args only", B_TRANSLATE("Args only"),
		new BMessage(kMsgAppFlagsChanged));

	fBackgroundAppCheckBox = new BCheckBox("background",
		B_TRANSLATE("Background app"), new BMessage(kMsgAppFlagsChanged));

	BLayoutBuilder::Grid<>(flagsBox, 0, 0)
		.SetInsets(padding, padding * 2, padding, padding)
		.Add(fSingleLaunchButton, 0, 0).Add(fArgsOnlyCheckBox, 1, 0)
		.Add(fMultipleLaunchButton, 0, 1).Add(fBackgroundAppCheckBox, 1, 1)
		.Add(fExclusiveLaunchButton, 0, 2);
	flagsBox->SetLabel(fFlagsCheckBox);

	// "Icon" group

	BBox* iconBox = new BBox("IconBox");
	iconBox->SetLabel(B_TRANSLATE("Icon"));
	fIconView = new IconView("icon");
	fIconView->SetModificationMessage(new BMessage(kMsgIconChanged));
	BLayoutBuilder::Group<>(iconBox, B_HORIZONTAL)
		.SetInsets(padding, padding * 2, padding, padding)
		.Add(fIconView);

	// "Supported Types" group

	BBox* typeBox = new BBox("typesBox");
	typeBox->SetLabel(B_TRANSLATE("Supported types"));

	fTypeListView = new SupportedTypeListView("Suppported Types",
		B_SINGLE_SELECTION_LIST);
	fTypeListView->SetSelectionMessage(new BMessage(kMsgTypeSelected));

	BScrollView* scrollView = new BScrollView("type scrollview", fTypeListView,
		B_FRAME_EVENTS | B_WILL_DRAW, false, true);

	fAddTypeButton = new BButton("add type",
		B_TRANSLATE("Add" B_UTF8_ELLIPSIS), new BMessage(kMsgAddType));

	fRemoveTypeButton = new BButton("remove type", B_TRANSLATE("Remove"),
		new BMessage(kMsgRemoveType));

	fTypeIconView = new IconView("type icon");
	BGroupView* iconHolder = new BGroupView(B_HORIZONTAL);
	iconHolder->AddChild(fTypeIconView);
	fTypeIconView->SetModificationMessage(new BMessage(kMsgTypeIconsChanged));

	BLayoutBuilder::Grid<>(typeBox, padding, padding)
		.SetInsets(padding, padding * 2, padding, padding)
		.Add(scrollView, 0, 0, 1, 5)
		.Add(fAddTypeButton, 1, 0, 1, 2)
		.Add(fRemoveTypeButton, 1, 2, 1, 2)
		.Add(iconHolder, 2, 1, 1, 2)
		.SetColumnWeight(0, 3)
		.SetColumnWeight(1, 2)
		.SetColumnWeight(2, 1);

	iconHolder->SetExplicitAlignment(
		BAlignment(B_ALIGN_CENTER, B_ALIGN_MIDDLE));

	// "Version Info" group

	BBox* versionBox = new BBox("versionBox");
	versionBox->SetLabel(B_TRANSLATE("Version info"));

	fMajorVersionControl = new BTextControl("version major",
		B_TRANSLATE("Version:"), NULL, new BMessage(kMsgVersionInfoChanged));
	_MakeNumberTextControl(fMajorVersionControl);

	fMiddleVersionControl = new BTextControl("version middle", ".", NULL,
		new BMessage(kMsgVersionInfoChanged));
	_MakeNumberTextControl(fMiddleVersionControl);

	fMinorVersionControl = new BTextControl("version minor", ".", NULL,
		new BMessage(kMsgVersionInfoChanged));
	_MakeNumberTextControl(fMinorVersionControl);

	fVarietyMenu = new BPopUpMenu("variety", true, true);
	fVarietyMenu->AddItem(new BMenuItem(B_TRANSLATE("Development"),
		new BMessage(kMsgVersionInfoChanged)));
	fVarietyMenu->AddItem(new BMenuItem(B_TRANSLATE("Alpha"),
		new BMessage(kMsgVersionInfoChanged)));
	fVarietyMenu->AddItem(new BMenuItem(B_TRANSLATE("Beta"),
		new BMessage(kMsgVersionInfoChanged)));
	fVarietyMenu->AddItem(new BMenuItem(B_TRANSLATE("Gamma"),
		new BMessage(kMsgVersionInfoChanged)));
	item = new BMenuItem(B_TRANSLATE("Golden master"),
		new BMessage(kMsgVersionInfoChanged));
	fVarietyMenu->AddItem(item);
	item->SetMarked(true);
	fVarietyMenu->AddItem(new BMenuItem(B_TRANSLATE("Final"),
		new BMessage(kMsgVersionInfoChanged)));

	BMenuField* varietyField = new BMenuField("", fVarietyMenu);
	fInternalVersionControl = new BTextControl("version internal", "/", NULL,
		new BMessage(kMsgVersionInfoChanged));
	fShortDescriptionControl = new BTextControl("short description",
		B_TRANSLATE("Short description:"), NULL,
		new BMessage(kMsgVersionInfoChanged));

	// TODO: workaround for a GCC 4.1.0 bug? Or is that really what the standard says?
	version_info versionInfo;
	fShortDescriptionControl->TextView()->SetMaxBytes(
		sizeof(versionInfo.short_info));

	BStringView* longLabel = new BStringView(NULL,
		B_TRANSLATE("Long description:"));
	longLabel->SetExplicitAlignment(labelAlignment);
	fLongDescriptionView = new TabFilteringTextView("long desc",
		kMsgVersionInfoChanged);
	fLongDescriptionView->SetMaxBytes(sizeof(versionInfo.long_info));

	scrollView = new BScrollView("desc scrollview", fLongDescriptionView,
		B_FRAME_EVENTS | B_WILL_DRAW, false, true);

	// Manually set a minimum size for the version text controls
	// TODO: the same does not work when applied to the layout items
	float width = be_plain_font->StringWidth("99") + 16;
	fMajorVersionControl->TextView()->SetExplicitMinSize(
		BSize(width, B_SIZE_UNSET));
	fMiddleVersionControl->TextView()->SetExplicitMinSize(
		BSize(width, B_SIZE_UNSET));
	fMinorVersionControl->TextView()->SetExplicitMinSize(
		BSize(width, B_SIZE_UNSET));
	fInternalVersionControl->TextView()->SetExplicitMinSize(
		BSize(width, B_SIZE_UNSET));

	BLayoutBuilder::Grid<>(versionBox, padding / 2, padding / 2)
		.SetInsets(padding, padding * 2, padding, padding)
		.AddTextControl(fMajorVersionControl, 0, 0)
		.Add(fMiddleVersionControl, 2, 0, 2)
		.Add(fMinorVersionControl, 4, 0, 2)
		.Add(varietyField, 6, 0, 3)
		.Add(fInternalVersionControl, 9, 0, 2)
		.AddTextControl(fShortDescriptionControl, 0, 1,
			B_ALIGN_HORIZONTAL_UNSET, 1, 10)
		.Add(longLabel, 0, 2)
		.Add(scrollView, 1, 2, 10, 3)
		.SetRowWeight(3, 3);

	// put it all together
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0, 0, 0, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, padding)
			.SetInsets(padding, padding, padding, padding)
			.Add(fSignatureControl)
			.AddGroup(B_HORIZONTAL, padding)
				.Add(flagsBox, 3)
				.Add(iconBox, 1)
				.End()
			.Add(typeBox, 2)
			.Add(versionBox);

	SetKeyMenuBar(menuBar);

	fSignatureControl->MakeFocus(true);
	BMimeType::StartWatching(this);
	_SetTo(entry);

	Layout(false);
}


/** @brief Stops watching the MIME database before tearing the window
           down. */
ApplicationTypeWindow::~ApplicationTypeWindow()
{
	BMimeType::StopWatching(this);
}

/**
 * @brief Returns the saved window frame from @a settings, or a default
 *        rect when no value is stored.
 *
 * @param settings  Persisted preferences.
 * @return Frame rect to use for the window.
 */
BRect
ApplicationTypeWindow::_Frame(const BMessage& settings) const
{
	BRect rect;
	if (settings.FindRect("app_type_next_frame", &rect) == B_OK)
		return rect;

	return BRect(100.0f, 110.0f, 250.0f, 340.0f);
}

/**
 * @brief Builds the localised window title based on the application's
 *        leaf name.
 *
 * @param entry  Application binary.
 * @return Window title, e.g. "<NAME> application type".
 */
BString
ApplicationTypeWindow::_Title(const BEntry& entry)
{
	char name[B_FILE_NAME_LENGTH];
	if (entry.GetName(name) != B_OK)
		strcpy(name, "\"-\"");

	BString title = B_TRANSLATE("%1 application type");
	title.ReplaceFirst("%1", name);
	return title;
}


/**
 * @brief Loads the contents of @a entry into every editable field and
 *        snapshots the original values so dirty detection works.
 *
 * Silently returns if the file or its BAppFileInfo cannot be opened so
 * the rest of the window stays usable.
 *
 * @param entry  Application binary to edit.
 */
void
ApplicationTypeWindow::_SetTo(const BEntry& entry)
{
	SetTitle(_Title(entry).String());
	fEntry = entry;

	// Retrieve Info

	BFile file(&entry, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BAppFileInfo info(&file);
	if (info.InitCheck() != B_OK)
		return;

	char signature[B_MIME_TYPE_LENGTH];
	if (info.GetSignature(signature) != B_OK)
		signature[0] = '\0';

	bool gotFlags = false;
	uint32 flags;
	if (info.GetAppFlags(&flags) == B_OK)
		gotFlags = true;
	else
		flags = B_MULTIPLE_LAUNCH;

	version_info versionInfo;
	if (info.GetVersionInfo(&versionInfo, B_APP_VERSION_KIND) != B_OK)
		memset(&versionInfo, 0, sizeof(version_info));

	// Set Controls

	fSignatureControl->SetModificationMessage(NULL);
	fSignatureControl->SetText(signature);
	fSignatureControl->SetModificationMessage(
		new BMessage(kMsgSignatureChanged));

	// flags

	switch (flags & (B_SINGLE_LAUNCH | B_MULTIPLE_LAUNCH | B_EXCLUSIVE_LAUNCH)) {
		case B_SINGLE_LAUNCH:
			fSingleLaunchButton->SetValue(B_CONTROL_ON);
			break;

		case B_EXCLUSIVE_LAUNCH:
			fExclusiveLaunchButton->SetValue(B_CONTROL_ON);
			break;

		case B_MULTIPLE_LAUNCH:
		default:
			fMultipleLaunchButton->SetValue(B_CONTROL_ON);
			break;
	}

	fArgsOnlyCheckBox->SetValue((flags & B_ARGV_ONLY) != 0);
	fBackgroundAppCheckBox->SetValue((flags & B_BACKGROUND_APP) != 0);
	fFlagsCheckBox->SetValue(gotFlags);

	_UpdateAppFlagsEnabled();

	// icon

	entry_ref ref;
	if (entry.GetRef(&ref) == B_OK)
		fIcon.SetTo(ref);
	else
		fIcon.Unset();

	fIconView->SetModificationMessage(NULL);
	fIconView->SetTo(&fIcon);
	fIconView->SetModificationMessage(new BMessage(kMsgIconChanged));

	// supported types

	BMessage supportedTypes;
	info.GetSupportedTypes(&supportedTypes);

	for (int32 i = fTypeListView->CountItems(); i-- > 0;) {
		BListItem* item = fTypeListView->RemoveItem(i);
		delete item;
	}

	const char* type;
	for (int32 i = 0; supportedTypes.FindString("types", i, &type) == B_OK; i++) {
		SupportedTypeItem* item = new SupportedTypeItem(type);

		entry_ref ref;
		if (fEntry.GetRef(&ref) == B_OK)
			item->SetIcon(ref, type);

		fTypeListView->AddItem(item);
	}
	fTypeListView->SortItems(&SupportedTypeItem::Compare);
	fTypeIconView->SetModificationMessage(NULL);
	fTypeIconView->SetTo(NULL);
	fTypeIconView->SetModificationMessage(new BMessage(kMsgTypeIconsChanged));
	fTypeIconView->SetEnabled(false);
	fRemoveTypeButton->SetEnabled(false);

	// version info

	char text[256];
	snprintf(text, sizeof(text), "%" B_PRId32, versionInfo.major);
	fMajorVersionControl->SetText(text);
	snprintf(text, sizeof(text), "%" B_PRId32, versionInfo.middle);
	fMiddleVersionControl->SetText(text);
	snprintf(text, sizeof(text), "%" B_PRId32, versionInfo.minor);
	fMinorVersionControl->SetText(text);

	if (versionInfo.variety >= (uint32)fVarietyMenu->CountItems())
		versionInfo.variety = 0;
	BMenuItem* item = fVarietyMenu->ItemAt(versionInfo.variety);
	if (item != NULL)
		item->SetMarked(true);

	snprintf(text, sizeof(text), "%" B_PRId32, versionInfo.internal);
	fInternalVersionControl->SetText(text);

	fShortDescriptionControl->SetText(versionInfo.short_info);
	fLongDescriptionView->SetText(versionInfo.long_info);

	// store original data

	fOriginalInfo.signature = signature;
	fOriginalInfo.gotFlags = gotFlags;
	fOriginalInfo.flags = gotFlags ? flags : 0;
	fOriginalInfo.versionInfo = versionInfo;
	fOriginalInfo.supportedTypes = _SupportedTypes();
		// The list view has the types sorted possibly differently
		// to the supportedTypes message, so don't use that here, but
		// get the sorted message instead.
	fOriginalInfo.iconChanged = false;
	fOriginalInfo.typeIconsChanged = false;

	fChangedProperties = 0;
	_CheckSaveMenuItem(0);
}


/**
 * @brief Greys out the launch-flag controls when the master "Application
 *        flags" check box is off.
 */
void
ApplicationTypeWindow::_UpdateAppFlagsEnabled()
{
	bool enabled = fFlagsCheckBox->Value() != B_CONTROL_OFF;

	fSingleLaunchButton->SetEnabled(enabled);
	fMultipleLaunchButton->SetEnabled(enabled);
	fExclusiveLaunchButton->SetEnabled(enabled);
	fArgsOnlyCheckBox->SetEnabled(enabled);
	fBackgroundAppCheckBox->SetEnabled(enabled);
}


/**
 * @brief Configures @a control to accept only ASCII digits.
 *
 * Used for the version-number controls so non-numeric input cannot reach
 * the BAppFileInfo encoder.
 *
 * @param control  Text control to constrain in place.
 */
void
ApplicationTypeWindow::_MakeNumberTextControl(BTextControl* control)
{
	// filter out invalid characters that can't be part of a MIME type name
	BTextView* textView = control->TextView();
	textView->SetMaxBytes(10);

	for (int32 i = 0; i < 256; i++) {
		if (!isdigit(i))
			textView->DisallowChar(i);
	}
}


/**
 * @brief Writes every editable field back into the application binary's
 *        BAppFileInfo.
 *
 * Order matters: signature first, then flags (or removal), version info,
 * the application icon, supported types, and finally per-type icons. On
 * success the cached @c fOriginalInfo and dirty flags are reset so the
 * Save menu item disables itself.
 */
void
ApplicationTypeWindow::_Save()
{
	BFile file;
	status_t status = file.SetTo(&fEntry, B_READ_WRITE);
	if (status != B_OK)
		return;

	BAppFileInfo info(&file);
	status = info.InitCheck();
	if (status != B_OK)
		return;

	// Retrieve Info

	uint32 flags = 0;
	bool gotFlags = _Flags(flags);
	BMessage supportedTypes = _SupportedTypes();
	version_info versionInfo = _VersionInfo();

	// Save

	status = info.SetSignature(fSignatureControl->Text());
	if (status == B_OK) {
		if (gotFlags)
			status = info.SetAppFlags(flags);
		else
			status = info.RemoveAppFlags();
	}
	if (status == B_OK)
		status = info.SetVersionInfo(&versionInfo, B_APP_VERSION_KIND);
	if (status == B_OK)
		fIcon.CopyTo(info, NULL, true);

	// supported types and their icons
	if (status == B_OK)
		status = info.SetSupportedTypes(&supportedTypes);

	for (int32 i = 0; i < fTypeListView->CountItems(); i++) {
		SupportedTypeItem* item = dynamic_cast<SupportedTypeItem*>(
			fTypeListView->ItemAt(i));

		if (item != NULL)
			item->Icon().CopyTo(info, item->Type(), true);
	}

	// reset the saved info
	fOriginalInfo.signature = fSignatureControl->Text();
	fOriginalInfo.gotFlags = gotFlags;
	fOriginalInfo.flags = flags;
	fOriginalInfo.versionInfo = versionInfo;
	fOriginalInfo.supportedTypes = supportedTypes;
	fOriginalInfo.iconChanged = false;
	fOriginalInfo.typeIconsChanged = false;

	fChangedProperties = 0;
	_CheckSaveMenuItem(0);
}


/**
 * @brief Recomputes the dirty bitmap and enables the Save menu item if
 *        anything changed.
 *
 * @param flags  Bitmask of @c CHECK_* properties that just changed and
 *               need re-evaluation.
 */
void
ApplicationTypeWindow::_CheckSaveMenuItem(uint32 flags)
{
	fChangedProperties = _NeedsSaving(flags);
	fSaveMenuItem->SetEnabled(fChangedProperties != 0);
}


/**
 * @brief Field-wise inequality test for the @c version_info struct.
 *
 * @param a  Left operand.
 * @param b  Right operand.
 * @return @c true when any field differs, @c false when both records
 *         compare equal.
 */
bool
operator!=(const version_info& a, const version_info& b)
{
	return a.major != b.major || a.middle != b.middle || a.minor != b.minor
		|| a.variety != b.variety || a.internal != b.internal
		|| strcmp(a.short_info, b.short_info) != 0
		|| strcmp(a.long_info, b.long_info) != 0;
}


/**
 * @brief Computes which @c CHECK_* properties currently differ from their
 *        snapshot in @c fOriginalInfo.
 *
 * For each bit set in @a _flags the corresponding property is re-tested;
 * unset bits keep their previous dirty-bit value.
 *
 * @param _flags  Properties to re-evaluate.
 * @return Updated dirty bitmap.
 */
uint32
ApplicationTypeWindow::_NeedsSaving(uint32 _flags) const
{
	uint32 flags = fChangedProperties;
	if (_flags & CHECK_SIGNATUR) {
		if (fOriginalInfo.signature != fSignatureControl->Text())
			flags |= CHECK_SIGNATUR;
		else
			flags &= ~CHECK_SIGNATUR;
	}

	if (_flags & CHECK_FLAGS) {
		uint32 appFlags = 0;
		bool gotFlags = _Flags(appFlags);
		if (fOriginalInfo.gotFlags != gotFlags
			|| fOriginalInfo.flags != appFlags) {
			flags |= CHECK_FLAGS;
		} else
			flags &= ~CHECK_FLAGS;
	}

	if (_flags & CHECK_VERSION) {
		if (fOriginalInfo.versionInfo != _VersionInfo())
			flags |= CHECK_VERSION;
		else
			flags &= ~CHECK_VERSION;
	}

	if (_flags & CHECK_ICON) {
		if (fOriginalInfo.iconChanged)
			flags |= CHECK_ICON;
		else
			flags &= ~CHECK_ICON;
	}

	if (_flags & CHECK_TYPES) {
		if (!fOriginalInfo.supportedTypes.HasSameData(_SupportedTypes()))
			flags |= CHECK_TYPES;
		else
			flags &= ~CHECK_TYPES;
	}

	if (_flags & CHECK_TYPE_ICONS) {
		if (fOriginalInfo.typeIconsChanged)
			flags |= CHECK_TYPE_ICONS;
		else
			flags &= ~CHECK_TYPE_ICONS;
	}

	return flags;
}


// #pragma mark -


/**
 * @brief Reads the launch-flag radio buttons and check boxes into a
 *        @c B_*_LAUNCH bitmap.
 *
 * @param flags  Output: composite bitmap of B_SINGLE_LAUNCH /
 *               B_MULTIPLE_LAUNCH / B_EXCLUSIVE_LAUNCH plus
 *               B_ARGV_ONLY and B_BACKGROUND_APP.
 * @return @c true when the master "Application flags" check box is on
 *         and @a flags is meaningful; @c false when flags should not be
 *         persisted at all.
 */
bool
ApplicationTypeWindow::_Flags(uint32& flags) const
{
	flags = 0;
	if (fFlagsCheckBox->Value() != B_CONTROL_OFF) {
		if (fSingleLaunchButton->Value() != B_CONTROL_OFF)
			flags |= B_SINGLE_LAUNCH;
		else if (fMultipleLaunchButton->Value() != B_CONTROL_OFF)
			flags |= B_MULTIPLE_LAUNCH;
		else if (fExclusiveLaunchButton->Value() != B_CONTROL_OFF)
			flags |= B_EXCLUSIVE_LAUNCH;

		if (fArgsOnlyCheckBox->Value() != B_CONTROL_OFF)
			flags |= B_ARGV_ONLY;
		if (fBackgroundAppCheckBox->Value() != B_CONTROL_OFF)
			flags |= B_BACKGROUND_APP;
		return true;
	}
	return false;
}


/**
 * @brief Builds the BMessage representation of the supported-types list
 *        in the order the listview currently shows them.
 *
 * @return BMessage containing one @c "types" string per row.
 */
BMessage
ApplicationTypeWindow::_SupportedTypes() const
{
	BMessage supportedTypes;
	for (int32 i = 0; i < fTypeListView->CountItems(); i++) {
		SupportedTypeItem* item = dynamic_cast<SupportedTypeItem*>(
			fTypeListView->ItemAt(i));

		if (item != NULL)
			supportedTypes.AddString("types", item->Type());
	}
	return supportedTypes;
}


/**
 * @brief Reads the four version-number controls plus the variety menu and
 *        descriptions into a @c version_info.
 *
 * @return Populated version_info ready to be passed to
 *         BAppFileInfo::SetVersionInfo().
 */
version_info
ApplicationTypeWindow::_VersionInfo() const
{
	version_info versionInfo;
	versionInfo.major = atol(fMajorVersionControl->Text());
	versionInfo.middle = atol(fMiddleVersionControl->Text());
	versionInfo.minor = atol(fMinorVersionControl->Text());
	versionInfo.variety = fVarietyMenu->IndexOf(fVarietyMenu->FindMarked());
	versionInfo.internal = atol(fInternalVersionControl->Text());
	strlcpy(versionInfo.short_info, fShortDescriptionControl->Text(),
		sizeof(versionInfo.short_info));
	strlcpy(versionInfo.long_info, fLongDescriptionView->Text(),
		sizeof(versionInfo.long_info));
	return versionInfo;
}


// #pragma mark -


/**
 * @brief Routes BMessages from the menu, the toolbar, the supported-type
 *        listview, and the MIME database watcher.
 *
 * Most cases just flag the appropriate dirty bit via _CheckSaveMenuItem();
 * the supported-type add/remove cases manipulate the listview rows and
 * push the change through.
 *
 * @param message  Incoming BMessage.
 * @todo Add support for B_SIMPLE_DATA-dragged refs and react to
 *       B_META_MIME_CHANGED notifications.
 */
void
ApplicationTypeWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgToggleAppFlags:
			_UpdateAppFlagsEnabled();
			_CheckSaveMenuItem(CHECK_FLAGS);
			break;

		case kMsgSignatureChanged:
			_CheckSaveMenuItem(CHECK_SIGNATUR);
			break;

		case kMsgAppFlagsChanged:
			_CheckSaveMenuItem(CHECK_FLAGS);
			break;

		case kMsgIconChanged:
			fOriginalInfo.iconChanged = true;
			_CheckSaveMenuItem(CHECK_ICON);
			break;

		case kMsgTypeIconsChanged:
			fOriginalInfo.typeIconsChanged = true;
			_CheckSaveMenuItem(CHECK_TYPE_ICONS);
			break;

		case kMsgVersionInfoChanged:
			_CheckSaveMenuItem(CHECK_VERSION);
			break;

		case kMsgSave:
			_Save();
			break;

		case kMsgTypeSelected:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				SupportedTypeItem* item
					= (SupportedTypeItem*)fTypeListView->ItemAt(index);

				fTypeIconView->SetModificationMessage(NULL);
				fTypeIconView->SetTo(item != NULL ? &item->Icon() : NULL);
				fTypeIconView->SetModificationMessage(
					new BMessage(kMsgTypeIconsChanged));
				fTypeIconView->SetEnabled(item != NULL);
				fRemoveTypeButton->SetEnabled(item != NULL);

				_CheckSaveMenuItem(CHECK_TYPES);
			}
			break;
		}

		case kMsgAddType:
		{
			BWindow* window = new TypeListWindow(NULL,
				kMsgTypeAdded, this);
			window->Show();
			break;
		}

		case kMsgTypeAdded:
		{
			const char* type;
			if (message->FindString("type", &type) != B_OK)
				break;

			// check if this type already exists

			SupportedTypeItem* newItem = new SupportedTypeItem(type);
			int32 insertAt = 0;

			for (int32 i = fTypeListView->CountItems(); i-- > 0;) {
				SupportedTypeItem* item = dynamic_cast<SupportedTypeItem*>(
					fTypeListView->ItemAt(i));
				if (item == NULL)
					continue;

				int compare = strcasecmp(item->Type(), type);
				if (!compare) {
					// type does already exist, select it and bail out
					delete newItem;
					newItem = NULL;
					fTypeListView->Select(i);
					break;
				}
				if (compare < 0)
					insertAt = i + 1;
			}

			if (newItem == NULL)
				break;

			fTypeListView->AddItem(newItem, insertAt);
			fTypeListView->Select(insertAt);

			_CheckSaveMenuItem(CHECK_TYPES);
			break;
		}

		case kMsgRemoveType:
		{
			int32 index = fTypeListView->CurrentSelection();
			if (index < 0)
				break;

			delete fTypeListView->RemoveItem(index);
			fTypeIconView->SetModificationMessage(NULL);
			fTypeIconView->SetTo(NULL);
			fTypeIconView->SetModificationMessage(
				new BMessage(kMsgTypeIconsChanged));
			fTypeIconView->SetEnabled(false);
			fRemoveTypeButton->SetEnabled(false);

			_CheckSaveMenuItem(CHECK_TYPES);
			break;
		}

		case B_SIMPLE_DATA:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) != B_OK)
				break;

			// TODO: add to supported types
			break;
		}

		case B_META_MIME_CHANGED:
			const char* type;
			int32 which;
			if (message->FindString("be:type", &type) != B_OK
				|| message->FindInt32("be:which", &which) != B_OK)
				break;

			// TODO: update supported types names
//			if (which == B_MIME_TYPE_DELETED)

//			_CheckSaveMenuItem(...);
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


/**
 * @brief Prompts to save when there are unsaved changes, then persists
 *        the final frame and notifies the application.
 *
 * @return @c true when the window should actually close; @c false if the
 *         user picked Cancel from the save-prompt alert.
 */
bool
ApplicationTypeWindow::QuitRequested()
{
	if (_NeedsSaving(CHECK_ALL) != 0) {
		BAlert* alert = new BAlert(B_TRANSLATE("Save request"),
			B_TRANSLATE("Save changes before closing?"),
			B_TRANSLATE("Cancel"), B_TRANSLATE("Don't save"),
			B_TRANSLATE("Save"), B_WIDTH_AS_USUAL, B_OFFSET_SPACING,
			B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		alert->SetShortcut(1, 'd');
		alert->SetShortcut(2, 's');

		int32 choice = alert->Go();
		switch (choice) {
			case 0:
				return false;
			case 1:
				break;
			case 2:
				_Save();
				break;
		}
	}

	BMessage update(kMsgSettingsChanged);
	update.AddRect("app_type_next_frame", Frame());
	be_app_messenger.SendMessage(&update);

	be_app->PostMessage(kMsgTypeWindowClosed);
	return true;
}

