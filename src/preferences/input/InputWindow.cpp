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
 *   Copyright 2019-2020, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Preetpal Kaur <preetpalok123@gmail.com>
 *       Adrien Destugues <pulkomandy@gmail.com>
 */


/**
 * @file InputWindow.cpp
 * @brief Implementation of InputWindow, the top-level Input preference window.
 *
 * InputWindow shows a BListView of input devices on the left and a
 * BCardView holding the matching settings card on the right (Mouse,
 * Touchpad, or Keyboard). It listens for B_INPUT_DEVICES_CHANGED to add
 * or remove cards as devices are hot-plugged. Per-device messages are
 * dispatched to the visible card.
 *
 * @see InputApplication, DeviceListItemView
 */


#include <CardLayout.h>
#include <CardView.h>
#include <Catalog.h>
#include <Control.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <ScrollView.h>

#include <private/input/InputServerTypes.h>

#include "InputConstants.h"
#include "InputDeviceView.h"
#include "InputKeyboard.h"
#include "InputMouse.h"
#include "InputTouchpadPref.h"
#include "InputTouchpadPrefView.h"
#include "InputWindow.h"
#include "MouseSettings.h"
#include "SettingsView.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InputWindow"


/**
 * @brief Constructs the Input preferences window.
 *
 * Builds the device list inside a fancy-bordered BScrollView, the BCardView
 * that hosts the per-device settings cards, and a 1:3 horizontal layout
 * for the two. Calls FindDevice() to populate the list with currently
 * attached input devices.
 *
 * @param rect  Initial screen rectangle for the window.
 */
InputWindow::InputWindow(BRect rect)
	:
	BWindow(rect, B_TRANSLATE_SYSTEM_NAME("Input"), B_TITLED_WINDOW,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS
			| B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE)
{
	fDeviceListView = new BListView(B_TRANSLATE("Device List"));
	fDeviceListView->SetSelectionMessage(new BMessage(ITEM_SELECTED));
	fDeviceListView->SetExplicitMinSize(
		BSize(be_control_look->ComposeIconSize(32).Width()
				+ fDeviceListView->StringWidth("Extended PS/2 Mouse 1"),
			B_SIZE_UNSET));

	BScrollView* scrollView = new BScrollView(
		"scrollView", fDeviceListView, 0, false, B_FANCY_BORDER);
	fCardView = new BCardView();

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, 10)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(scrollView, 1)
		.Add(fCardView, 3);

	FindDevice();
}


/**
 * @brief Routes selection changes and per-device messages to the right card.
 *
 * ITEM_SELECTED switches the BCardView to the matching index. Mouse,
 * touchpad, and keyboard messages are forwarded to the currently visible
 * card so each settings page only sees messages it can handle.
 * B_INPUT_DEVICES_CHANGED triggers AddDevice/remove dispatch as devices
 * are hot-plugged.
 *
 * @param message  Incoming BMessage. Unhandled messages fall through to
 *                 BWindow::MessageReceived.
 */
void
InputWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case ITEM_SELECTED:
		{
			int32 index = message->GetInt32("index", 0);
			if (index >= 0)
				fCardView->CardLayout()->SetVisibleItem(index);
			break;
		}
		case kMsgMouseType:
		case kMsgMouseMap:
		case kMsgMouseFocusMode:
		case kMsgFollowsMouseMode:
		case kMsgAcceptFirstClick:
		case kMsgDoubleClickSpeed:
		case kMsgMouseSpeed:
		case kMsgAccelerationFactor:
		case kMsgDefaults:
		case kMsgRevert:
		{
			PostMessage(
				message, fCardView->CardLayout()->VisibleItem()->View());
			break;
		}
		case SCROLL_AREA_CHANGED:
		case SCROLL_CONTROL_CHANGED:
		case TAP_CONTROL_CHANGED:
		case PAD_SPEED_CHANGED:
		case PAD_ACCELERATION_CHANGED:
		case DEFAULT_SETTINGS:
		case REVERT_SETTINGS:
		{
			PostMessage(
				message, fCardView->CardLayout()->VisibleItem()->View());
			break;
		}
		case kMsgSliderrepeatrate:
		case kMsgSliderdelayrate:
		{
			PostMessage(
				message, fCardView->CardLayout()->VisibleItem()->View());
			break;
		}

		case B_INPUT_DEVICES_CHANGED:
		{
			int32 operation = message->FindInt32("be:opcode");
			BString name = message->FindString("be:device_name");

			if (operation == B_INPUT_DEVICE_ADDED) {
				BInputDevice* device = find_input_device(name);
				if (device)
					AddDevice(device);
			} else if (operation == B_INPUT_DEVICE_REMOVED) {
				for (int i = 0; i < fDeviceListView->CountItems(); i++) {
					DeviceListItemView* item
						= dynamic_cast<DeviceListItemView*>(
							fDeviceListView->ItemAt(i));
					if (item != NULL && item->Label() == name) {
						fDeviceListView->RemoveItem(i);
						BView* settings = fCardView->ChildAt(i);
						fCardView->RemoveChild(settings);
						delete settings;
						break;
					}
				}
			}

			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


/**
 * @brief Centres the window on screen and starts watching device hot-plug.
 *
 * Calls watch_input_devices(this, true) so that subsequent add/remove
 * notifications are delivered to MessageReceived().
 */
void
InputWindow::Show()
{
	CenterOnScreen();
	BWindow::Show();
	watch_input_devices(this, true);
}


/**
 * @brief Hides the window and stops watching for device hot-plug events.
 */
void
InputWindow::Hide()
{
	BWindow::Hide();
	watch_input_devices(this, false);
}


/**
 * @brief Populates the device list from the current input device set.
 *
 * Calls get_input_devices() and walks the resulting BList, handing each
 * BInputDevice to AddDevice().
 *
 * @return B_OK on success or an error code propagated from
 *         get_input_devices().
 */
status_t
InputWindow::FindDevice()
{
	BList devList;
	status_t status = get_input_devices(&devList);
	if (status != B_OK)
		return status;

	int32 i = 0;

	while (true) {
		BInputDevice* dev = (BInputDevice*)devList.ItemAt(i);
		if (dev == NULL)
			break;
		i++;

		AddDevice(dev);
	}

	return B_OK;
}


/**
 * @brief Adds a settings card and a list row for a single input device.
 *
 * Inspects the device type and name to build the appropriate card:
 * touchpad pointer devices get a TouchpadPrefView, other pointing devices
 * get an InputMouse, keyboards get an InputKeyboard. Devices of unknown
 * type are deleted to release the kit-allocated BInputDevice.
 *
 * @param dev  BInputDevice to add. Ownership is transferred to either
 *             this view or to delete on the unhandled path.
 */
void
InputWindow::AddDevice(BInputDevice* dev)
{
	BString name = dev->Name();

	if (dev->Type() == B_POINTING_DEVICE && name.FindFirst("Touchpad") >= 0) {
		TouchpadPrefView* view = new TouchpadPrefView(dev);
		fCardView->AddChild(view);

		DeviceListItemView* touchpad
			= new DeviceListItemView(name, TOUCHPAD_TYPE);
		fDeviceListView->AddItem(touchpad);
	} else if (dev->Type() == B_POINTING_DEVICE) {
		MouseSettings* settings;
		settings = fMultipleMouseSettings.AddMouseSettings(name);

		InputMouse* view = new InputMouse(dev, settings);
		fCardView->AddChild(view);

		DeviceListItemView* mouse = new DeviceListItemView(name, MOUSE_TYPE);
		fDeviceListView->AddItem(mouse);
	} else if (dev->Type() == B_KEYBOARD_DEVICE) {
		InputKeyboard* view = new InputKeyboard(dev);
		fCardView->AddChild(view);

		DeviceListItemView* keyboard
			= new DeviceListItemView(name, KEYBOARD_TYPE);
		fDeviceListView->AddItem(keyboard);
	} else
		delete dev;
}
