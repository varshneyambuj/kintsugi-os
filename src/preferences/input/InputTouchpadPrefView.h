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
 * MIT License. Copyright 2019-2025, Haiku, Inc.
 * Original authors: Preetpal Kaur, Samuel Rodríguez Pérez.
 */

/** @file InputTouchpadPrefView.h
    @brief Declares TouchpadView and TouchpadPrefView, the touchpad UI classes. */

#ifndef TOUCHPAD_PREF_VIEW_H
#define TOUCHPAD_PREF_VIEW_H

#include <Bitmap.h>
#include <Button.h>
#include <CheckBox.h>
#include <GroupView.h>
#include <Invoker.h>
#include <OptionPopUp.h>
#include <Slider.h>
#include <StringView.h>
#include <View.h>

#include "InputTouchpadPref.h"
#include "touchpad_settings.h"

#include <Debug.h>

#if DEBUG
#	define LOG(text...) PRINT((text))
#else
#	define LOG(text...)
#endif

/** @brief BMessage code: scroll-zone delimiter drag committed. */
const uint SCROLL_AREA_CHANGED = '&sac';
/** @brief BMessage code: any scrolling control changed. */
const uint SCROLL_CONTROL_CHANGED = '&scc';
/** @brief BMessage code: tap sensitivity slider changed. */
const uint TAP_CONTROL_CHANGED = '&tcc';
/** @brief BMessage code: Defaults button pressed. */
const uint DEFAULT_SETTINGS = '&dse';
/** @brief BMessage code: Revert button pressed. */
const uint REVERT_SETTINGS = '&rse';
/** @brief BMessage code: padblocker delay slider changed. */
const uint PADBLOCK_TIME_CHANGED = '&ptc';
/** @brief BMessage code: trackpad speed slider changed. */
const uint PAD_SPEED_CHANGED = '&psc';
/** @brief BMessage code: trackpad acceleration slider changed. */
const uint PAD_ACCELERATION_CHANGED = '&pac';
/** @brief BMessage code: edge motion pop-up changed. */
const uint EDGE_MOTION_CHANGED = '&emc';
/** @brief BMessage code: finger click toggle changed. */
const uint FINGER_CLICK_CHANGED = '&fcc';
/** @brief BMessage code: software button areas toggle changed. */
const uint SOFTWARE_BUTTON_AREAS_CHANGED = '&sbc';

class DeviceListView;


/**
 * @brief Custom BView that previews the touchpad with draggable scroll zones.
 *
 * Renders the pad rectangle and the X/Y scroll-zone delimiters into an
 * off-screen bitmap for flicker-free updates. Posts SCROLL_AREA_CHANGED
 * via BInvoker once the user finalises a drag.
 */
class TouchpadView : public BView, public BInvoker {
public:
							TouchpadView(BRect frame);
	virtual					~TouchpadView();
	virtual void			Draw(BRect updateRect);
	virtual void			MouseDown(BPoint point);
	virtual void			MouseUp(BPoint point);
	virtual void			MouseMoved(BPoint point, uint32 transit,
								const BMessage* dragMessage);

	virtual void			AttachedToWindow();
	virtual void			GetPreferredSize(float* width, float* height);

			void			SetValues(float rightRange, float bottomRange);
			/** @brief Returns the right-edge scroll zone as a fraction of pad width. */
			float			GetRightScrollRatio()
								{ return 1 - fXScrollRange / fPadRect.Width(); }
			/** @brief Returns the bottom-edge scroll zone as a fraction of pad height. */
			float			GetBottomScrollRatio()
								{ return 1
									- fYScrollRange / fPadRect.Height(); }
private:
	virtual void 			DrawSliders();

			BRect			fPrefRect;
			BRect			fPadRect;
			BRect			fXScrollDragZone;
			float			fXScrollRange;
			float			fOldXScrollRange;
			BRect			fYScrollDragZone;
			float			fYScrollRange;
			float			fOldYScrollRange;

			bool			fXTracking;
			bool			fYTracking;
			BView*			fOffScreenView;
			BBitmap*		fOffScreenBitmap;
};


/**
 * @brief Full touchpad preferences card view.
 *
 * Hosts a TouchpadView preview alongside the scroll, edge-motion, tap,
 * padblocker, speed, and acceleration controls plus a Defaults/Revert
 * button pair. Persists its state through an embedded TouchpadPref.
 */
class TouchpadPrefView : public BGroupView {
public:
							TouchpadPrefView(BInputDevice* dev);
	virtual					~TouchpadPrefView();
	virtual	void			MessageReceived(BMessage* message);
	virtual	void			AttachedToWindow();
	virtual	void			DetachedFromWindow();
			void			SetupView();

			void			SetValues(touchpad_settings *settings);
private:
			TouchpadPref	fTouchpadPref;
			TouchpadView*	fTouchpadView;
			BCheckBox*		fScrollReverseBox;
			BCheckBox*		fTwoFingerBox;
			BCheckBox*		fTwoFingerHorizontalBox;
			BCheckBox*		fTwoFingerNaturalScrollingBox;
			BCheckBox*		fFingerClickBox;
			BCheckBox*		fSoftwareButtonAreasBox;
			BSlider*		fScrollStepXSlider;
			BSlider*		fScrollStepYSlider;
			BSlider*		fScrollAccelSlider;
			BSlider*		fPadBlockerSlider;
			BSlider*		fTapSlider;
			BSlider*		fSpeedSlider;
			BSlider*		fAccelSlider;
			BOptionPopUp*	fEdgeMotionOptionPopUp;
			BButton*		fDefaultButton;
			BButton*		fRevertButton;
};

#endif	// TOUCHPAD_PREF_VIEW_H
