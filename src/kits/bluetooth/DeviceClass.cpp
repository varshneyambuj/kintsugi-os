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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2007-2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file DeviceClass.cpp
 * @brief Implementation of DeviceClass, the Bluetooth device class descriptor
 *
 * DeviceClass encodes the Bluetooth Class of Device (CoD) bitfield that
 * describes a device's major/minor device class and supported service classes
 * (e.g., networking, audio, telephony). Helper methods translate the numeric
 * code to human-readable service and device class strings.
 *
 * @see LocalDevice, RemoteDevice
 */


#include <bluetooth/DeviceClass.h>
#include <bluetooth/debug.h>

#include <Catalog.h>
#include <Locale.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DeviceClass"


namespace Bluetooth {

/**
 * @brief Build a comma-separated string of active service class names.
 *
 * Inspects the service-class bits of the Class of Device field and appends
 * each enabled service name to @a serviceClass. If no service class bits are
 * set the string "Unspecified" is appended instead.
 *
 * Service class bits are mapped in order from the Bluetooth specification:
 * Positioning, Networking, Rendering, Capturing, Object Transfer, Audio,
 * Telephony, and Information.
 *
 * @param serviceClass BString reference that receives the result. The string
 *                     is appended to — it is the caller's responsibility to
 *                     initialise or clear it before calling this method.
 * @see GetMajorDeviceClass()
 * @see GetMinorDeviceClass()
 */
void
DeviceClass::GetServiceClass(BString& serviceClass)
{
	CALLED();
	static const char *services[] = {
		B_TRANSLATE_MARK("Positioning"),
		B_TRANSLATE_MARK("Networking"),
		B_TRANSLATE_MARK("Rendering"),
		B_TRANSLATE_MARK("Capturing"),
		B_TRANSLATE_MARK("Object transfer"),
		B_TRANSLATE_MARK("Audio"),
		B_TRANSLATE_MARK("Telephony"),
		B_TRANSLATE_MARK("Information")
	};

	if (ServiceClass() != 0) {
		bool first = true;

		for (uint s = 0; s < (sizeof(services) / sizeof(*services)); s++) {
			if (ServiceClass() & (1 << s)) {
				if (first) {
					first = false;
					serviceClass << services[s];
				} else {
					serviceClass << ", " << services[s];
				}

			}
		}

	} else
		serviceClass << B_TRANSLATE("Unspecified");
}


/**
 * @brief Append the major device class name to the supplied string.
 *
 * Maps the numeric major device class value extracted from the Class of Device
 * field to its human-readable name (e.g., "Computer", "Phone", "Audio/Video")
 * and appends it to @a majorClass.
 *
 * @param majorClass BString reference that receives the result. Content is
 *                   appended, not replaced.
 * @note If the major device class value is outside the range of known classes,
 *       "Invalid device class!" is appended instead.
 * @see GetServiceClass()
 * @see GetMinorDeviceClass()
 */
void
DeviceClass::GetMajorDeviceClass(BString& majorClass)
{
	CALLED();
	static const char *major_devices[] = {
		B_TRANSLATE_MARK("Miscellaneous"),
		B_TRANSLATE_MARK("Computer"),
		B_TRANSLATE_MARK("Phone"),
		B_TRANSLATE_MARK("LAN access"),
		B_TRANSLATE_MARK("Audio/Video"),
		B_TRANSLATE_MARK("Peripheral"),
		B_TRANSLATE_MARK("Imaging"),
		B_TRANSLATE_MARK("Uncategorized")
	};

	if (MajorDeviceClass() >= sizeof(major_devices) / sizeof(*major_devices))
		majorClass << B_TRANSLATE("Invalid device class!\n");
	else
		majorClass << major_devices[MajorDeviceClass()];

}


/**
 * @brief Append the minor device class name to the supplied string.
 *
 * Interprets the minor device class bits in the context of the major device
 * class and appends a descriptive label to @a minorClass. The mapping covers
 * all major class categories defined by the Bluetooth specification, including
 * computer, phone, LAN access point, audio/video, peripheral, imaging,
 * wearable, and toy sub-types.
 *
 * For the peripheral major class (5), the upper two bits select the input
 * device type (keyboard, pointing device, or combo) and the lower four bits
 * select the sub-type (joystick, gamepad, etc.), and both parts are appended
 * joined by a "/".
 *
 * For the imaging major class (6), multiple bits may be set simultaneously,
 * so multiple sub-type labels can be appended in sequence.
 *
 * @param minorClass BString reference that receives the result. Content is
 *                   appended, not replaced.
 * @see GetMajorDeviceClass()
 * @see GetServiceClass()
 */
void
DeviceClass::GetMinorDeviceClass(BString& minorClass)
{
	CALLED();
	uint major = MajorDeviceClass();
	uint minor = MinorDeviceClass();

	switch (major) {
		case 0:	/* misc */
			minorClass << " -";
			break;
		case 1:	/* computer */
			switch(minor) {
				case 0:
					minorClass << B_TRANSLATE("Uncategorized");
					break;
				case 1:
					minorClass << B_TRANSLATE("Desktop workstation");
					break;
				case 2:
					minorClass << B_TRANSLATE("Server");
					break;
				case 3:
					minorClass << B_TRANSLATE("Laptop");
					break;
				case 4:
					minorClass << B_TRANSLATE("Handheld");
					break;
				case 5:
					minorClass << B_TRANSLATE_COMMENT("Palm",
						"A palm-held device");
					break;
				case 6:
					minorClass << B_TRANSLATE_COMMENT("Wearable",
						"A wearable computer");
					break;
				}
			break;
		case 2:	/* phone */
			switch(minor) {
				case 0:
					minorClass << B_TRANSLATE("Uncategorized");
					break;
				case 1:
					minorClass << B_TRANSLATE("Cellular");
					break;
				case 2:
					minorClass << B_TRANSLATE("Cordless");
					break;
				case 3:
					minorClass << B_TRANSLATE("Smart phone");
					break;
				case 4:
					minorClass << B_TRANSLATE("Wired modem or voice gateway");
					break;
				case 5:
					minorClass << B_TRANSLATE("Common ISDN access");
					break;
				case 6:
					minorClass << B_TRANSLATE("SIM card reader");
					break;
			}
			break;
		case 3:	/* lan access */
			if (minor == 0) {
				minorClass << B_TRANSLATE("Uncategorized");
				break;
			}
			switch(minor / 8) {
				case 0:
					minorClass << B_TRANSLATE("Fully available");
					break;
				case 1:
					minorClass << B_TRANSLATE("1-17% utilized");
					break;
				case 2:
					minorClass << B_TRANSLATE("17-33% utilized");
					break;
				case 3:
					minorClass << B_TRANSLATE("33-50% utilized");
					break;
				case 4:
					minorClass << B_TRANSLATE("50-67% utilized");
					break;
				case 5:
					minorClass << B_TRANSLATE("67-83% utilized");
					break;
				case 6:
					minorClass << B_TRANSLATE("83-99% utilized");
					break;
				case 7:
					minorClass << B_TRANSLATE("No service available");
					break;
			}
			break;
		case 4:	/* audio/video */
			switch(minor) {
				case 0:
					minorClass << B_TRANSLATE("Uncategorized");
					break;
				case 1:
					minorClass << B_TRANSLATE("Device conforms to the headset profile");
					break;
				case 2:
					minorClass << B_TRANSLATE("Hands-free");
					break;
					/* 3 is reserved */
				case 4:
					minorClass << B_TRANSLATE("Microphone");
					break;
				case 5:
					minorClass << B_TRANSLATE("Loudspeaker");
					break;
				case 6:
					minorClass << B_TRANSLATE("Headphones");
					break;
				case 7:
					minorClass << B_TRANSLATE("Portable audio");
					break;
				case 8:
					minorClass << B_TRANSLATE("Car audio");
					break;
				case 9:
					minorClass << B_TRANSLATE("Set-top box");
					break;
				case 10:
					minorClass << B_TRANSLATE("HiFi audio device");
					break;
				case 11:
					minorClass << B_TRANSLATE("VCR");
					break;
				case 12:
					minorClass << B_TRANSLATE("Video camera");
					break;
				case 13:
					minorClass << B_TRANSLATE("Camcorder");
					break;
				case 14:
					minorClass << B_TRANSLATE("Video monitor");
					break;
				case 15:
					minorClass << B_TRANSLATE("Video display and loudspeaker");
					break;
				case 16:
					minorClass << B_TRANSLATE("Video conferencing");
					break;
					/* 17 is reserved */
				case 18:
					minorClass << B_TRANSLATE("Gaming/Toy");
					break;
			}
			break;
		case 5:	/* peripheral */
		{
			switch(minor & 48) {
				case 16:
					minorClass << B_TRANSLATE("Keyboard");
					if (minor & 15)
						minorClass << "/";
					break;
				case 32:
					minorClass << B_TRANSLATE("Pointing device");
					if (minor & 15)
						minorClass << "/";
					break;
				case 48:
					minorClass << B_TRANSLATE("Combo keyboard/pointing device");
					if (minor & 15)
						minorClass << "/";
					break;
			}

			switch(minor & 15) {
				case 0:
					break;
				case 1:
					minorClass << B_TRANSLATE("Joystick");
					break;
				case 2:
					minorClass << B_TRANSLATE("Gamepad");
					break;
				case 3:
					minorClass << B_TRANSLATE("Remote control");
					break;
				case 4:
					minorClass << B_TRANSLATE("Sensing device");
					break;
				case 5:
					minorClass << B_TRANSLATE("Digitizer tablet");
					break;
				case 6:
					minorClass << B_TRANSLATE("Card reader");
					break;
				default:
					minorClass << B_TRANSLATE("(reserved)");
					break;
			}
			break;
		}
		case 6:	/* imaging */
			if (minor & 4)
				minorClass << B_TRANSLATE("Display");
			if (minor & 8)
				minorClass << B_TRANSLATE("Camera");
			if (minor & 16)
				minorClass << B_TRANSLATE("Scanner");
			if (minor & 32)
				minorClass << B_TRANSLATE("Printer");
			break;
		case 7: /* wearable */
			switch(minor) {
				case 1:
					minorClass << B_TRANSLATE("Wrist watch");
					break;
				case 2:
					minorClass << B_TRANSLATE_COMMENT("Pager",
					"A small radio device to receive short text messages");
					break;
				case 3:
					minorClass << B_TRANSLATE("Jacket");
					break;
				case 4:
					minorClass << B_TRANSLATE("Helmet");
					break;
				case 5:
					minorClass << B_TRANSLATE("Glasses");
					break;
			}
			break;
		case 8: /* toy */
			switch(minor) {
				case 1:
					minorClass << B_TRANSLATE("Robot");
					break;
				case 2:
					minorClass << B_TRANSLATE("Vehicle");
					break;
				case 3:
					minorClass << B_TRANSLATE("Doll/Action figure");
					break;
				case 4:
					minorClass << B_TRANSLATE("Controller");
					break;
				case 5:
					minorClass << B_TRANSLATE("Game");
					break;
			}
			break;
		case 63:	/* uncategorised */
			minorClass << "";
			break;
		default:
			minorClass << B_TRANSLATE("Unknown (reserved) minor device class");
			break;
	}
}


/**
 * @brief Append a full human-readable description of the Class of Device to a string.
 *
 * Formats all three class components — service class, major device class, and
 * minor device class — separated by " | " delimiters and terminated with a
 * period, then appends the result to @a string.
 *
 * @param string BString reference that receives the formatted dump. Content is
 *               appended, not replaced.
 * @see GetServiceClass()
 * @see GetMajorDeviceClass()
 * @see GetMinorDeviceClass()
 */
void
DeviceClass::DumpDeviceClass(BString& string)
{
	CALLED();
	string << B_TRANSLATE("Service classes: ");
	GetServiceClass(string);
	string << " | ";
	string << B_TRANSLATE("Major class: ");
	GetMajorDeviceClass(string);
	string << " | ";
	string << B_TRANSLATE("Minor class: ");
	GetMinorDeviceClass(string);
	string << ".";
}


/**
 * @brief Draw a simple iconic representation of the device class into a BView.
 *
 * Renders a filled rounded rectangle using the Bluetooth blue brand colour
 * and then overlays a white icon that represents the device's major class:
 * a phone outline for phones (major class 2), a network diagram for LAN
 * access points (major class 3), a speaker/triangle shape for audio/video
 * devices (major class 4), or the Bluetooth logo for all other classes.
 *
 * The icon is placed relative to @a point using the IconInsets and
 * PixelsForIcon constants defined in DeviceClass.h.
 *
 * @param view  The BView into which the icon will be drawn. The view's high
 *              colour is modified and restored to black before the method
 *              returns.
 * @param point The top-left origin for the icon bounding box in the view's
 *              coordinate system.
 * @note The view must already be locked before calling this method.
 */
void
DeviceClass::Draw(BView* view, const BPoint& point)
{
	CALLED();
	rgb_color	kBlack = { 0,0,0,0 };
	rgb_color	kBlue = { 28,110,157,0 };
	rgb_color	kWhite = { 255,255,255,0 };


	view->SetHighColor(kBlue);
	view->FillRoundRect(BRect(point.x + IconInsets, point.y + IconInsets,
		point.x + IconInsets + PixelsForIcon, point.y + IconInsets + PixelsForIcon), 5, 5);

	view->SetHighColor(kWhite);

	switch (MajorDeviceClass()) {

		case 2: // phone
			view->StrokeRoundRect(BRect(point.x + IconInsets + uint(PixelsForIcon/4),
				 point.y + IconInsets + 6,
				 point.x + IconInsets + uint(PixelsForIcon*3/4),
			 	 point.y + IconInsets + PixelsForIcon - 2), 2, 2);
			view->StrokeRect(BRect(point.x + IconInsets + uint(PixelsForIcon/4) + 4,
			 	 point.y + IconInsets + 10,
				 point.x + IconInsets + uint(PixelsForIcon*3/4) - 4,
			 	 point.y + IconInsets + uint(PixelsForIcon*3/4)));
			view->StrokeLine(BPoint(point.x + IconInsets + uint(PixelsForIcon/4) + 4,
				 point.y + IconInsets + PixelsForIcon - 6),
				 BPoint(point.x + IconInsets + uint(PixelsForIcon*3/4) - 4,
				 point.y + IconInsets + PixelsForIcon - 6));
			view->StrokeLine(BPoint(point.x + IconInsets + uint(PixelsForIcon/4) + 4,
				 point.y + IconInsets + PixelsForIcon - 4),
				 BPoint(point.x + IconInsets + uint(PixelsForIcon*3/4) - 4,
				 point.y + IconInsets + PixelsForIcon - 4));
			view->StrokeLine(BPoint(point.x + IconInsets + uint(PixelsForIcon/4) + 4,
				 point.y + IconInsets + 2),
				 BPoint(point.x + IconInsets + uint(PixelsForIcon/4) + 4,
				 point.y + IconInsets + 6));
			break;
		case 3: // LAN
			view->StrokeLine(BPoint(point.x + IconInsets + uint(PixelsForIcon/4),
				 point.y + IconInsets + uint(PixelsForIcon*3/8)),
				BPoint(point.x + IconInsets + uint(PixelsForIcon*3/4),
				 point.y + IconInsets + uint(PixelsForIcon*3/8)));
			view->StrokeLine(BPoint(point.x + IconInsets + uint(PixelsForIcon*5/8),
				 point.y + IconInsets + uint(PixelsForIcon/8)));
			view->StrokeLine(BPoint(point.x + IconInsets + uint(PixelsForIcon*3/4),
				 point.y + IconInsets + uint(PixelsForIcon*5/8)),
				BPoint(point.x + IconInsets + uint(PixelsForIcon/4),
				 point.y + IconInsets + uint(PixelsForIcon*5/8)));
			view->StrokeLine(BPoint(point.x + IconInsets + uint(PixelsForIcon*3/8),
				 point.y + IconInsets + uint(PixelsForIcon*7/8)));
			break;
		case 4: // audio/video
			view->StrokeRect(BRect(point.x + IconInsets + uint(PixelsForIcon/4),
				 point.y + IconInsets + uint(PixelsForIcon*3/8),
				 point.x + IconInsets + uint(PixelsForIcon*3/8),
			 	 point.y + IconInsets + uint(PixelsForIcon*5/8)));
			view->StrokeLine(BPoint(point.x + IconInsets + uint(PixelsForIcon*3/8),
				 point.y + IconInsets + uint(PixelsForIcon*3/8)),
				BPoint(point.x + IconInsets + uint(PixelsForIcon*3/4),
				 point.y + IconInsets + uint(PixelsForIcon/8)));
			view->StrokeLine(BPoint(point.x + IconInsets + uint(PixelsForIcon*3/4),
				 point.y + IconInsets + uint(PixelsForIcon*7/8)));
			view->StrokeLine(BPoint(point.x + IconInsets + uint(PixelsForIcon*3/8),
				 point.y + IconInsets + uint(PixelsForIcon*5/8)));
			break;
		default: // Bluetooth Logo
			view->StrokeLine(BPoint(point.x + IconInsets + uint(PixelsForIcon/4),
				 point.y + IconInsets + uint(PixelsForIcon*3/4)),
				BPoint(point.x + IconInsets + uint(PixelsForIcon*3/4),
				 point.y + IconInsets + uint(PixelsForIcon/4)));
			view->StrokeLine(BPoint(point.x + IconInsets + uint(PixelsForIcon/2),
				 point.y + IconInsets +2));
			view->StrokeLine(BPoint(point.x + IconInsets + uint(PixelsForIcon/2),
				 point.y + IconInsets + PixelsForIcon - 2));
			view->StrokeLine(BPoint(point.x + IconInsets + uint(PixelsForIcon*3/4),
				 point.y + IconInsets + uint(PixelsForIcon*3/4)));
			view->StrokeLine(BPoint(point.x + IconInsets + uint(PixelsForIcon/4),
				point.y + IconInsets + uint(PixelsForIcon/4)));
			break;
	}
	view->SetHighColor(kBlack);
}

}
