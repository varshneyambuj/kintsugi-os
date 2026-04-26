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
 *   Copyright 2004-2019 Haiku Inc., All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Adrien Destugues, <pulkomandy@pulkomandy.tk>
 *       Axel Dörfler, <axeld@pinc-software.de>
 *       Alexander von Gluck, <kallisti5@unixzen.com>
 */


/**
 * @file NetworkWindow.cpp
 * @brief Implementation of NetworkWindow, the main window of the Network
 *        preference application.
 *
 * Coordinates discovery of network interfaces and add-ons, hosts the
 * selected item's editor in a shell view, and bridges between the live
 * BNetworkSettings instance and registered listeners. Broadcasts
 * configuration and settings updates to every list item and add-on so the
 * UI stays consistent with the running stack.
 */


#include "NetworkWindow.h"

#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Deskbar.h>
#include <Directory.h>
#include <LayoutBuilder.h>
#include <NetworkDevice.h>
#include <NetworkInterface.h>
#include <NetworkNotifications.h>
#include <NetworkRoster.h>
#include <OutlineListView.h>
#include <Path.h>
#include <PathFinder.h>
#include <PathMonitor.h>
#include <Roster.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <SymLink.h>

#define ENABLE_PROFILES 0
#if ENABLE_PROFILES
#	include <PopUpMenu.h>
#endif

#include "InterfaceListItem.h"
#include "InterfaceView.h"
#include "ServiceListItem.h"


/** @brief MIME signature of the deskbar replicant launched by the
           "Show network status in Deskbar" checkbox. */
const char* kNetworkStatusSignature = "application/x-vnd.Haiku-NetworkStatus";

/** @brief Profile menu: a saved profile was chosen. */
static const uint32 kMsgProfileSelected = 'prof';
/** @brief Profile menu: open the management UI. */
static const uint32 kMsgProfileManage = 'mngp';
/** @brief Profile menu: create a new profile. */
static const uint32 kMsgProfileNew = 'newp';
/** @brief Revert button pressed: undo every revertable add-on item. */
static const uint32 kMsgRevert = 'rvrt';
/** @brief Replicant checkbox toggled. */
static const uint32 kMsgToggleReplicant = 'trep';
/** @brief Outline list selection changed. */
static const uint32 kMsgItemSelected = 'ItSl';

/** @brief Process-wide messenger pointing at the active NetworkWindow. */
BMessenger gNetworkWindow;


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT	"NetworkWindow"


/**
 * @brief Bold-text BStringItem used for top-level group headings
 *        (Services, Other, ...) in the outline list.
 */
class TitleItem : public BStringItem {
public:
	/** @brief Constructs a heading row carrying @a title as its label. */
	TitleItem(const char* title)
		:
		BStringItem(title)
	{
	}

	/**
	 * @brief Renders the row using the bold system font and restores the
	 *        plain font afterwards.
	 */
	void DrawItem(BView* owner, BRect bounds, bool complete)
	{
		owner->SetFont(be_bold_font);
		BStringItem::DrawItem(owner, bounds, complete);
		owner->SetFont(be_plain_font);
	}

	/**
	 * @brief Sizes the row using the bold font so it doesn't clip.
	 */
	void Update(BView* owner, const BFont* font)
	{
		BStringItem::Update(owner, be_bold_font);
	}
};


// #pragma mark -


/**
 * @brief Constructs the window, builds the outline list and shell view,
 *        scans interfaces and add-ons, and starts monitoring settings.
 *
 * Initially selects the first list item so the right-hand pane is populated
 * and the window can size itself before being centered on screen.
 */
NetworkWindow::NetworkWindow()
	:
	BWindow(BRect(100, 100, 750, 400), B_TRANSLATE_SYSTEM_NAME("Network"),
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS),
	fServicesItem(NULL),
	fDialUpItem(NULL),
	fVPNItem(NULL),
	fOtherItem(NULL)
{
	// Profiles section
#if ENABLE_PROFILES
	BPopUpMenu* profilesPopup = new BPopUpMenu("<none>");
	_BuildProfilesMenu(profilesPopup, kMsgProfileSelected);

	BMenuField* profilesMenuField = new BMenuField("profiles_menu",
		B_TRANSLATE("Profile:"), profilesPopup);

	profilesMenuField->SetFont(be_bold_font);
	profilesMenuField->SetEnabled(false);
#endif

	// Settings section

	fRevertButton = new BButton("revert", B_TRANSLATE("Revert"),
		new BMessage(kMsgRevert));

	BMessage* message = new BMessage(kMsgToggleReplicant);
	BCheckBox* showReplicantCheckBox = new BCheckBox("showReplicantCheckBox",
		B_TRANSLATE("Show network status in Deskbar"), message);
	showReplicantCheckBox->SetExplicitMaxSize(
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	showReplicantCheckBox->SetValue(_IsReplicantInstalled());

	fListView = new BOutlineListView("list", B_SINGLE_SELECTION_LIST,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_FRAME_EVENTS | B_NAVIGABLE);
	fListView->SetSelectionMessage(new BMessage(kMsgItemSelected));

	BScrollView* scrollView = new BScrollView("ScrollView", fListView,
		0, false, true);
	scrollView->SetExplicitMaxSize(BSize(B_SIZE_UNSET, B_SIZE_UNLIMITED));

	fAddOnShellView = new BView("add-on shell", 0,
		new BGroupLayout(B_VERTICAL));
	fAddOnShellView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fAddOnShellView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));

	fInterfaceView = new InterfaceView();

	// Build the layout
	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)

#if ENABLE_PROFILES
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(profilesMenuField)
			.AddGlue()
		.End()
#endif
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(scrollView)
			.Add(fAddOnShellView)
		.End()

		.Add(showReplicantCheckBox)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fRevertButton)
			.AddGlue()
		.End();

	gNetworkWindow = this;

	_ScanInterfaces();
	_ScanAddOns();
	_UpdateRevertButton();

	fListView->Select(0);
	_SelectItem(fListView->ItemAt(0));
		// Call this manually, so that CenterOnScreen() below already
		// knows the final window size.

	// Set size of the list view from its contents
	float width;
	float height;
	fListView->GetPreferredSize(&width, &height);
	width += 2 * be_control_look->DefaultItemSpacing();
	fListView->SetExplicitSize(BSize(width, B_SIZE_UNSET));
	fListView->SetExplicitMinSize(BSize(width, std::min(height, 400.f)));

	CenterOnScreen();

	fSettings.StartMonitoring(this);
	start_watching_network(B_WATCH_NETWORK_INTERFACE_CHANGES
		| B_WATCH_NETWORK_LINK_CHANGES | B_WATCH_NETWORK_WLAN_CHANGES, this);
}


/**
 * @brief Stops settings and network monitors before the window is destroyed.
 */
NetworkWindow::~NetworkWindow()
{
	stop_watching_network(this);
	fSettings.StopMonitoring(this);
}


/**
 * @brief Handles window-close: forwards a quit to the application so the
 *        process exits when the user closes this window.
 *
 * @return Always true.
 */
bool
NetworkWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


/**
 * @brief Window message dispatcher.
 *
 * Routes profile-menu actions, list selection, the Revert button, the
 * Deskbar replicant toggle, path-monitor and network-monitor updates, and
 * BNetworkSettings update notifications.
 *
 * @param message  Incoming BMessage.
 */
void
NetworkWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgProfileNew:
			break;

		case kMsgProfileSelected:
		{
			const char* path;
			if (message->FindString("path", &path) != B_OK)
				break;

			// TODO!
			break;
		}

		case kMsgItemSelected:
		{
			BListItem* listItem = fListView->FullListItemAt(
				fListView->FullListCurrentSelection());
			if (listItem == NULL)
				break;

			_SelectItem(listItem);
			break;
		}

		case kMsgRevert:
		{
			SettingsMap::const_iterator iterator = fSettingsMap.begin();
			for (; iterator != fSettingsMap.end(); iterator++)
				iterator->second->Revert();
			break;
		}

		case kMsgToggleReplicant:
		{
			_ShowReplicant(
				message->GetInt32("be:value", B_CONTROL_OFF) == B_CONTROL_ON);
			break;
		}

		case B_PATH_MONITOR:
		{
			fSettings.Update(message);
			break;
		}

		case B_NETWORK_MONITOR:
			_BroadcastConfigurationUpdate(*message);
			break;

		case BNetworkSettings::kMsgInterfaceSettingsUpdated:
		case BNetworkSettings::kMsgNetworkSettingsUpdated:
		case BNetworkSettings::kMsgServiceSettingsUpdated:
			_BroadcastSettingsUpdate(message->what);
			break;

		case kMsgSettingsItemUpdated:
			// TODO: update list item
			_UpdateRevertButton();
			break;

		default:
			inherited::MessageReceived(message);
	}
}


/**
 * @brief Populates @a menu with the profiles found under the system profile
 *        directory plus the New / Manage entries.
 *
 * Marks the entry currently pointed to by the "current" symlink. The menu
 * uses radio mode so only one profile can be marked at a time.
 *
 * @param menu  Menu to populate (assumed empty).
 * @param what  BMessage @c what assigned to each profile entry.
 */
void
NetworkWindow::_BuildProfilesMenu(BMenu* menu, int32 what)
{
	char currentProfile[256] = { 0 };

	menu->SetRadioMode(true);

	BDirectory dir("/boot/system/settings/network/profiles");
	if (dir.InitCheck() == B_OK) {
		BEntry entry;

		dir.Rewind();
		while (dir.GetNextEntry(&entry) >= 0) {
			BPath name;
			entry.GetPath(&name);

			if (entry.IsSymLink() &&
				strcmp("current", name.Leaf()) == 0) {
				BSymLink symlink(&entry);

				if (symlink.IsAbsolute())
					// oh oh, sorry, wrong symlink...
					continue;

				symlink.ReadLink(currentProfile, sizeof(currentProfile));
				continue;
			};

			if (!entry.IsDirectory())
				continue;

			BMessage* message = new BMessage(what);
			message->AddString("path", name.Path());

			BMenuItem* item = new BMenuItem(name.Leaf(), message);
			menu->AddItem(item);
		}
	}

	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("New" B_UTF8_ELLIPSIS),
		new BMessage(kMsgProfileNew)));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Manage" B_UTF8_ELLIPSIS),
		new BMessage(kMsgProfileManage)));

	if (currentProfile[0] != '\0') {
		BMenuItem* item = menu->FindItem(currentProfile);
		if (item != NULL) {
			// TODO: translate
			BString label(item->Label());
			label << " (current)";
			item->SetLabel(label.String());
			item->SetMarked(true);
		}
	}
}


/**
 * @brief Walks the BNetworkRoster and adds an InterfaceListItem for each
 *        non-loopback interface, classifying it by media type.
 *
 * @todo Also add interfaces that exist only in settings (e.g. unplugged USB).
 */
void
NetworkWindow::_ScanInterfaces()
{
	// Try existing devices first
	BNetworkRoster& roster = BNetworkRoster::Default();
	BNetworkInterface interface;
	uint32 cookie = 0;

	while (roster.GetNextInterface(&cookie, interface) == B_OK) {
		if ((interface.Flags() & IFF_LOOPBACK) != 0)
			continue;

		BNetworkDevice device(interface.Name());
		BNetworkInterfaceType type = B_NETWORK_INTERFACE_TYPE_OTHER;

		if (device.IsWireless())
			type = B_NETWORK_INTERFACE_TYPE_WIFI;
		else if (device.IsEthernet())
			type = B_NETWORK_INTERFACE_TYPE_ETHERNET;

		InterfaceListItem* item = new InterfaceListItem(interface.Name(), type);
		item->SetExpanded(true);

		fInterfaceItemMap.insert(std::pair<BString, InterfaceListItem*>(
			BString(interface.Name()), item));
		fListView->AddItem(item);
	}

	// TODO: Then consider those from the settings (for example, for USB)
}


/**
 * @brief Discovers, loads, and instantiates Network Settings add-ons.
 *
 * For each unique add-on file found under any "Network Settings" add-on
 * directory the function loads the image, looks up
 * @c instantiate_network_settings_add_on, asks the add-on to enumerate
 * interface-scoped and generic settings items, and inserts each item into
 * the outline list under the appropriate group.
 *
 * @note Add-ons that fail to load or expose the symbol are skipped without
 *       aborting the scan.
 */
void
NetworkWindow::_ScanAddOns()
{
	BStringList paths;
	BPathFinder::FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY, "Network Settings",
		paths);

	// Collect add-on paths by name, so that each name will only be
	// loaded once.
	typedef std::map<BString, BPath> PathMap;
	PathMap addOnMap;

	for (int32 i = 0; i < paths.CountStrings(); i++) {
		BDirectory directory(paths.StringAt(i));
		BEntry entry;
		while (directory.GetNextEntry(&entry) == B_OK) {
			BPath path;
			if (entry.GetPath(&path) != B_OK)
				continue;

			if (addOnMap.find(path.Leaf()) == addOnMap.end())
				addOnMap.insert(std::pair<BString, BPath>(path.Leaf(), path));
		}
	}

	for (PathMap::const_iterator addOnIterator = addOnMap.begin();
			addOnIterator != addOnMap.end(); addOnIterator++) {
		const BPath& path = addOnIterator->second;

		image_id image = load_add_on(path.Path());
		if (image < 0) {
			printf("Failed to load %s addon: %s.\n", path.Path(),
				strerror(image));
			continue;
		}

		BNetworkSettingsAddOn* (*instantiateAddOn)(image_id image,
			BNetworkSettings& settings);

		status_t status = get_image_symbol(image,
			"instantiate_network_settings_add_on",
			B_SYMBOL_TYPE_TEXT, (void**)&instantiateAddOn);
		if (status != B_OK) {
			// No "addon instantiate function" symbol found in this addon
			printf("No symbol \"instantiate_network_settings_add_on\" found "
				"in %s addon: not a network setup addon!\n", path.Path());
			unload_add_on(image);
			continue;
		}

		BNetworkSettingsAddOn* addOn = instantiateAddOn(image, fSettings);
		if (addOn == NULL) {
			unload_add_on(image);
			continue;
		}

		fAddOns.AddItem(addOn);

		// Per interface items
		ItemMap::const_iterator iterator = fInterfaceItemMap.begin();
		for (; iterator != fInterfaceItemMap.end(); iterator++) {
			const BString& interface = iterator->first;
			BListItem* interfaceItem = iterator->second;

			uint32 cookie = 0;
			while (true) {
				BNetworkSettingsItem* item = addOn->CreateNextInterfaceItem(
					cookie, interface.String());
				if (item == NULL)
					break;

				fSettingsMap[item->ListItem()] = item;
				fListView->AddUnder(item->ListItem(), interfaceItem);
			}
			fListView->SortItemsUnder(interfaceItem, true,
				NetworkWindow::_CompareListItems);
		}

		// Generic items
		uint32 cookie = 0;
		while (true) {
			BNetworkSettingsItem* item = addOn->CreateNextItem(cookie);
			if (item == NULL)
				break;

			fSettingsMap[item->ListItem()] = item;
			fListView->AddUnder(item->ListItem(),
				_ListItemFor(item->Type()));
		}

		_SortItemsUnder(fServicesItem);
		_SortItemsUnder(fOtherItem);
	}

	fListView->SortItemsUnder(NULL, true,
		NetworkWindow::_CompareTopLevelListItems);
}


/**
 * @brief Looks up the BNetworkSettingsItem associated with a list row.
 *
 * @param item  Outline list row to resolve.
 * @return Pointer to the matching settings item, or NULL when @a item is
 *         not a settings entry.
 */
BNetworkSettingsItem*
NetworkWindow::_SettingsItemFor(BListItem* item)
{
	SettingsMap::const_iterator found = fSettingsMap.find(item);
	if (found != fSettingsMap.end())
		return found->second;

	return NULL;
}


/**
 * @brief Sorts children of @a item alphabetically.
 *
 * @param item  Parent item; ignored when NULL.
 */
void
NetworkWindow::_SortItemsUnder(BListItem* item)
{
	if (item != NULL)
		fListView->SortItemsUnder(item, true, NetworkWindow::_CompareListItems);
}


/**
 * @brief Returns (lazily creating) the top-level group row for @a type.
 *
 * @param type  Settings type whose grouping row is requested.
 * @return List item to use as parent, or NULL for unknown types.
 */
BListItem*
NetworkWindow::_ListItemFor(BNetworkSettingsType type)
{
	switch (type) {
		case B_NETWORK_SETTINGS_TYPE_SERVICE:
			if (fServicesItem == NULL)
				fServicesItem = _CreateItem(B_TRANSLATE("Services"));
			return fServicesItem;

		case B_NETWORK_SETTINGS_TYPE_OTHER:
			if (fOtherItem == NULL)
				fOtherItem = _CreateItem(B_TRANSLATE("Other"));
			return fOtherItem;

		default:
			return NULL;
	}
}


/**
 * @brief Allocates a TitleItem grouping row labelled @a label, adds it to
 *        the outline view, and returns it.
 *
 * @param label  Translated heading text.
 * @return The newly inserted list item.
 */
BListItem*
NetworkWindow::_CreateItem(const char* label)
{
	BListItem* item = new TitleItem(label);
	item->SetExpanded(true);
	fListView->AddItem(item);
	return item;
}


/**
 * @brief Replaces the contents of the right-hand shell view with the editor
 *        appropriate for @a listItem.
 *
 * For settings items the add-on-supplied View() is shown; for interface
 * rows the shared InterfaceView is bound to the interface name and shown.
 *
 * @param listItem  The newly selected outline row; may be NULL.
 */
void
NetworkWindow::_SelectItem(BListItem* listItem)
{
	while (fAddOnShellView->CountChildren() > 0)
		fAddOnShellView->ChildAt(0)->RemoveSelf();

	BView* nextView = NULL;

	BNetworkSettingsItem* item = _SettingsItemFor(listItem);
	if (item != NULL) {
		nextView = item->View();
	} else {
		InterfaceListItem* item = dynamic_cast<InterfaceListItem*>(
			listItem);
		if (item != NULL) {
			fInterfaceView->SetTo(item->Name());
			nextView = fInterfaceView;
		}
	}

	if (nextView != NULL)
		fAddOnShellView->AddChild(nextView);
}


/**
 * @brief Forwards a settings update to every list item and add-on item, and
 *        refreshes the Revert button afterwards.
 *
 * @param type  Settings category that changed (one of the
 *              kMsgInterfaceSettingsUpdated / kMsgNetworkSettingsUpdated /
 *              kMsgServiceSettingsUpdated codes).
 */
void
NetworkWindow::_BroadcastSettingsUpdate(uint32 type)
{
	for (int32 index = 0; index < fListView->FullListCountItems(); index++) {
		BNetworkSettingsListener* listener
			= dynamic_cast<BNetworkSettingsListener*>(
				fListView->FullListItemAt(index));
		if (listener != NULL)
			listener->SettingsUpdated(type);
	}

	SettingsMap::const_iterator iterator = fSettingsMap.begin();
	for (; iterator != fSettingsMap.end(); iterator++)
		iterator->second->SettingsUpdated(type);

	_UpdateRevertButton();
}


/**
 * @brief Forwards a runtime configuration update to listeners and asks the
 *        list to redraw.
 *
 * @param message  Configuration message from the network monitor.
 */
void
NetworkWindow::_BroadcastConfigurationUpdate(const BMessage& message)
{
	for (int32 index = 0; index < fListView->FullListCountItems(); index++) {
		BNetworkConfigurationListener* listener
			= dynamic_cast<BNetworkConfigurationListener*>(
				fListView->FullListItemAt(index));
		if (listener != NULL)
			listener->ConfigurationUpdated(message);
	}

	SettingsMap::const_iterator iterator = fSettingsMap.begin();
	for (; iterator != fSettingsMap.end(); iterator++)
		iterator->second->ConfigurationUpdated(message);

	// TODO: improve invalidated region to the one that matters
	fListView->Invalidate();
	_UpdateRevertButton();
}


/**
 * @brief Enables the Revert button only when at least one settings item
 *        reports that it can revert.
 */
void
NetworkWindow::_UpdateRevertButton()
{
	bool enabled = false;
	SettingsMap::const_iterator iterator = fSettingsMap.begin();
	for (; iterator != fSettingsMap.end(); iterator++) {
		if (iterator->second->IsRevertable()) {
			enabled = true;
			break;
		}
	}

	fRevertButton->SetEnabled(enabled);
}


/**
 * @brief Installs or removes the NetworkStatus replicant in the Deskbar.
 *
 * On install, the NetworkStatus app is launched with @c --deskbar; on
 * removal, the existing entry is asked to leave. Failures during install
 * are reported via a BAlert.
 *
 * @param show  true to install, false to remove.
 */
void
NetworkWindow::_ShowReplicant(bool show)
{
	if (show) {
		const char* argv[] = {"--deskbar", NULL};

		status_t status = be_roster->Launch(kNetworkStatusSignature, 1, argv);
		if (status != B_OK && status != B_ALREADY_RUNNING) {
			BString errorMessage;
			errorMessage.SetToFormat(
				B_TRANSLATE("Installing NetworkStatus in Deskbar failed: %s"),
				strerror(status));
			BAlert* alert = new BAlert(B_TRANSLATE("launch error"),
				errorMessage, B_TRANSLATE("OK"));
			alert->Go(NULL);
		}
	} else {
		BDeskbar deskbar;
		deskbar.RemoveItem("NetworkStatus");
	}
}


/**
 * @brief Reports whether the NetworkStatus replicant is currently in the
 *        Deskbar.
 *
 * @return true when the Deskbar already hosts a "NetworkStatus" entry.
 */
bool
NetworkWindow::_IsReplicantInstalled()
{
	BDeskbar deskbar;
	return deskbar.HasItem("NetworkStatus");
}


/**
 * @brief Returns a comparable name for sorting list items.
 *
 * Recognizes the three concrete row types (network interface item, service
 * item, generic string item).
 *
 * @param item  List item to inspect.
 * @return Pointer to the item's label, or NULL when none is available.
 */
/*static*/ const char*
NetworkWindow::_ItemName(const BListItem* item)
{
	if (const BNetworkInterfaceListItem* listItem = dynamic_cast<
			const BNetworkInterfaceListItem*>(item))
		return listItem->Label();

	if (const ServiceListItem* listItem = dynamic_cast<
			const ServiceListItem*>(item))
		return listItem->Label();

	if (const BStringItem* stringItem = dynamic_cast<const BStringItem*>(item))
		return stringItem->Text();

	return NULL;
}


/**
 * @brief Comparator for top-level outline rows.
 *
 * Ensures network interfaces sort ahead of group headings, then defers to
 * _CompareListItems for the rest.
 *
 * @param a  Left-hand item.
 * @param b  Right-hand item.
 * @return Negative if a precedes b, positive if b precedes a, zero on
 *         equality.
 */
/*static*/ int
NetworkWindow::_CompareTopLevelListItems(const BListItem* a, const BListItem* b)
{
	if (a == b)
		return 0;

	if (const InterfaceListItem* itemA
			= dynamic_cast<const InterfaceListItem*>(a)) {
		if (const InterfaceListItem* itemB
				= dynamic_cast<const InterfaceListItem*>(b)) {
			return strcasecmp(itemA->Name(), itemB->Name());
		}
		return -1;
	} else if (dynamic_cast<const InterfaceListItem*>(b) != NULL)
		return 1;
/*
	if (a == fDialUpItem)
		return -1;
	if (b == fDialUpItem)
		return 1;

	if (a == fServicesItem)
		return -1;
	if (b == fServicesItem)
		return 1;
*/
	return _CompareListItems(a, b);
}


/**
 * @brief Default comparator for outline rows: sorts case-insensitively by
 *        label, falling back to pointer order when both labels are absent.
 *
 * @param a  Left-hand item.
 * @param b  Right-hand item.
 * @return Negative if a precedes b, positive if b precedes a, zero on
 *         equality.
 */
/*static*/ int
NetworkWindow::_CompareListItems(const BListItem* a, const BListItem* b)
{
	if (a == b)
		return 0;

	const char* nameA = _ItemName(a);
	const char* nameB = _ItemName(b);

	if (nameA != NULL && nameB != NULL)
		return strcasecmp(nameA, nameB);
	if (nameA != NULL)
		return 1;
	if (nameB != NULL)
		return -1;

	return (addr_t)a > (addr_t)b ? 1 : -1;
}
