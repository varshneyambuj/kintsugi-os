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
 *   Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 *   All rights reserved. Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Julian Harnath <julian.harnath@rwth-aachen.de>
 */

/** @file BarberPole.cpp
 *  @brief Implements the BarberPole animated progress indicator view and the
 *         MachineRoom singleton that drives all barber-pole instances in the
 *         team via a dedicated spin-loop thread.
 */


#include "BarberPole.h"

#include <AutoLocker.h>
#include <ControlLook.h>
#include <Locker.h>
#include <ObjectList.h>
#include <Messenger.h>

#include <pthread.h>
#include <stdio.h>


// #pragma mark - MachineRoom


/*! The machine room spins all the barber poles.
    Keeps a list of all barber poles of this team and runs its own
    thread to invalidate them in regular intervals.
*/
class MachineRoom
{
private:
	enum {
		kSpinInterval = 20000 // us
	};

private:
	/** @brief Constructs the MachineRoom singleton.
	 *
	 *  Creates the spin-loop semaphore and spawns the background thread
	 *  that periodically refreshes all attached barber poles.
	 */
	MachineRoom()
		:
		fMessengers(20)
	{
		fSpinLoopLock = create_sem(0, "BarberPole lock");
		fSpinLoopThread = spawn_thread(&MachineRoom::_StartSpinLoop,
			"The Barber Machine", B_DISPLAY_PRIORITY, this);
		resume_thread(fSpinLoopThread);
	}

public:
	/** @brief Registers a BarberPole with the MachineRoom so it is spun.
	 *  @param pole  The BarberPole instance to attach.
	 */
	static void AttachBarberPole(BarberPole* pole)
	{
		_InitializeIfNeeded();
		sInstance->_Attach(pole);
	}

	/** @brief Unregisters a BarberPole from the MachineRoom.
	 *  @param pole  The BarberPole instance to detach.
	 */
	static void DetachBarberPole(BarberPole* pole)
	{
		sInstance->_Detach(pole);
	}

private:
	/** @brief Creates the singleton MachineRoom instance. */
	static void _Initialize()
	{
		sInstance = new MachineRoom();
	}

	/** @brief Thread entry point that starts the spin loop.
	 *  @param instance  Pointer to the MachineRoom object.
	 *  @return Always returns B_OK.
	 */
	static status_t _StartSpinLoop(void* instance)
	{
		static_cast<MachineRoom*>(instance)->_SpinLoop();
		return B_OK;
	}

	/** @brief Initializes the MachineRoom singleton exactly once. */
	static void _InitializeIfNeeded()
	{
		pthread_once(&sOnceControl, &MachineRoom::_Initialize);
	}

	/** @brief Adds a BarberPole messenger to the tracked list.
	 *
	 *  If the list was previously empty the spin-loop semaphore is
	 *  released so the thread wakes up.
	 *
	 *  @param pole  The BarberPole to start tracking.
	 */
	void _Attach(BarberPole* pole)
	{
		AutoLocker<BLocker> locker(fLock);

		bool wasEmpty = fMessengers.IsEmpty();

		BMessenger* messenger = new BMessenger(pole);
		fMessengers.AddItem(messenger);

		if (wasEmpty)
			release_sem(fSpinLoopLock);
	}

	/** @brief Removes a BarberPole messenger from the tracked list.
	 *
	 *  If the list becomes empty after removal the spin-loop semaphore is
	 *  acquired so the thread blocks until a new pole is attached.
	 *
	 *  @param pole  The BarberPole to stop tracking.
	 */
	void _Detach(BarberPole* pole)
	{
		AutoLocker<BLocker> locker(fLock);

		for (int32 i = 0; i < fMessengers.CountItems(); i++) {
			BMessenger* messenger = fMessengers.ItemAt(i);
			if (messenger->Target(NULL) == pole) {
				fMessengers.RemoveItem(messenger, true);
				break;
			}
		}

		if (fMessengers.IsEmpty())
			acquire_sem(fSpinLoopLock);
	}

	/** @brief Main loop executed by the background thread.
	 *
	 *  Sends a refresh message to every registered BarberPole at
	 *  kSpinInterval microsecond intervals.  Blocks on the semaphore
	 *  whenever there are no poles to spin.
	 */
	void _SpinLoop()
	{
		for (;;) {
			AutoLocker<BLocker> locker(fLock);

			for (int32 i = 0; i < fMessengers.CountItems(); i++) {
				BMessenger* messenger = fMessengers.ItemAt(i);
				messenger->SendMessage(BarberPole::kRefreshMessage);
			}

			locker.Unset();

			acquire_sem(fSpinLoopLock);
			release_sem(fSpinLoopLock);

			snooze(kSpinInterval);
		}
	}

private:
	static MachineRoom*		sInstance;
	static pthread_once_t	sOnceControl;

	thread_id				fSpinLoopThread;
	sem_id					fSpinLoopLock;

	BLocker					fLock;
	BObjectList<BMessenger, true> fMessengers;
};


MachineRoom* MachineRoom::sInstance = NULL;
pthread_once_t MachineRoom::sOnceControl = PTHREAD_ONCE_INIT;


// #pragma mark - BarberPole


/** @brief Constructs a BarberPole with a default two-color scheme.
 *  @param name  The view name passed to BView.
 */
BarberPole::BarberPole(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fIsSpinning(false),
	fSpinSpeed(0.05),
	fColors(NULL),
	fNumColors(0),
	fScrollOffset(0.0),
	fStripeWidth(0.0),
	fNumStripes(0)
{
	// Default colors, chosen from system color scheme
	rgb_color defaultColors[2];
	rgb_color otherColor = tint_color(ui_color(B_STATUS_BAR_COLOR), 1.3);
	otherColor.alpha = 50;
	defaultColors[0] = otherColor;
	defaultColors[1] = B_TRANSPARENT_COLOR;
	SetColors(defaultColors, 2);
}


/** @brief Destructor — stops spinning and frees color storage. */
BarberPole::~BarberPole()
{
	Stop();
	delete[] fColors;
}


/** @brief Handles incoming messages, dispatching the refresh tick internally.
 *  @param message  The message to process.
 */
void
BarberPole::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kRefreshMessage:
			_Spin();
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


/** @brief Draws the barber pole, either spinning or in its idle state.
 *  @param updateRect  The rectangle that needs to be redrawn.
 */
void
BarberPole::Draw(BRect updateRect)
{
	if (fIsSpinning)
		_DrawSpin(updateRect);
	else
		_DrawNonSpin(updateRect);
}


/** @brief Renders the animated spinning stripe pattern.
 *  @param updateRect  The dirty rectangle passed from Draw().
 */
void
BarberPole::_DrawSpin(BRect updateRect)
{
	// Draw color stripes
	float position = -fStripeWidth * (fNumColors + 0.5) + fScrollOffset;
		// Starting position: beginning of the second color cycle
		// The + 0.5 is so we start out without a partially visible stripe
		// on the left side (makes it simpler to loop)
	BRect bounds = Bounds();
	bounds.InsetBy(-2, -2);
	be_control_look->DrawStatusBar(this, bounds, updateRect,
		ui_color(B_PANEL_BACKGROUND_COLOR), ui_color(B_STATUS_BAR_COLOR),
		bounds.Width());
	SetDrawingMode(B_OP_ALPHA);
	uint32 colorIndex = 0;
	for (uint32 i = 0; i < fNumStripes; i++) {
		SetHighColor(fColors[colorIndex]);
		colorIndex++;
		if (colorIndex >= fNumColors)
			colorIndex = 0;

		BRect stripeFrame = fStripe.Frame();
		fStripe.MapTo(stripeFrame,
			stripeFrame.OffsetToCopy(position, 0.0));
		FillPolygon(&fStripe);

		position += fStripeWidth;
	}

	SetDrawingMode(B_OP_COPY);
	// Draw box around it
	bounds = Bounds();
	be_control_look->DrawBorder(this, bounds, updateRect,
		ui_color(B_PANEL_BACKGROUND_COLOR), B_PLAIN_BORDER);
}


/*! This will show something in the place of the spinner when there is no
    spinning going on.  The logic to render the striped background comes
    from the 'drivesetup' application.
*/

/** @brief Renders the static hatched pattern shown when the pole is stopped.
 *  @param updateRect  The dirty rectangle passed from Draw().
 */
void
BarberPole::_DrawNonSpin(BRect updateRect)
{
	// approach copied from the DiskSetup application.
	static const pattern kStripes = { { 0xc7, 0x8f, 0x1f, 0x3e, 0x7c,
		0xf8, 0xf1, 0xe3 } };
	BRect bounds = Bounds();
	SetHighUIColor(B_PANEL_BACKGROUND_COLOR, B_DARKEN_1_TINT);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	FillRect(bounds, kStripes);
	be_control_look->DrawBorder(this, bounds, updateRect,
		ui_color(B_PANEL_BACKGROUND_COLOR), B_PLAIN_BORDER);
}


/** @brief Recomputes stripe geometry when the view is resized.
 *  @param width   New view width in pixels.
 *  @param height  New view height in pixels.
 */
void
BarberPole::FrameResized(float width, float height)
{
	// Choose stripe width so that at least 2 full stripes fit into the view,
	// but with a minimum of 5px. Larger views get wider stripes, but they
	// grow slower than the view and are capped to a maximum of 200px.
	fStripeWidth = (width / 4) + 5;
	if (fStripeWidth > 200)
		fStripeWidth = 200;

	BPoint stripePoints[4];
	stripePoints[0].Set(fStripeWidth * 0.5, 0.0); // top left
	stripePoints[1].Set(fStripeWidth * 1.5, 0.0); // top right
	stripePoints[2].Set(fStripeWidth, height);    // bottom right
	stripePoints[3].Set(0.0, height);             // bottom left

	fStripe = BPolygon(stripePoints, 4);

	fNumStripes = (int32)ceilf((width) / fStripeWidth) + 1 + fNumColors;
		// Number of color stripes drawn in total for the barber pole, the
		// user-visible part is a "window" onto the complete pole. We need
		// as many stripes as are visible, an extra one on the right side
		// (will be partially visible, that's the + 1); and then a whole color
		// cycle of strips extra which we scroll into until we loop.
		//
		// Example with 3 colors and a visible area of 2*fStripeWidth (which means
		// that 2 will be fully visible, and a third one partially):
		//               ........
		//   X___________v______v___
		//  / 1 / 2 / 3 / 1 / 2 / 3 /
		//  `````````````````````````
		// Pole is scrolled to the right into the visible region, which is marked
		// between the two 'v'. Once the left edge of the visible area reaches
		// point X, we can jump back to the initial region position.
}


/** @brief Returns the minimum size of the barber pole view.
 *  @return A BSize with at least 50 px width and 5 px height.
 */
BSize
BarberPole::MinSize()
{
	BSize result = BView::MinSize();

	if (result.width < 50)
		result.SetWidth(50);

	if (result.height < 5)
		result.SetHeight(5);

	return result;
}


/** @brief Starts the spinning animation.
 *
 *  Attaches this pole to the MachineRoom so the background thread begins
 *  sending refresh ticks.  Does nothing if already spinning.
 */
void
BarberPole::Start()
{
	if (fIsSpinning)
		return;
	MachineRoom::AttachBarberPole(this);
	fIsSpinning = true;
}


/** @brief Stops the spinning animation and redraws the idle state.
 *
 *  Detaches this pole from the MachineRoom.  Does nothing if not spinning.
 */
void
BarberPole::Stop()
{
	if (!fIsSpinning)
		return;
	MachineRoom::DetachBarberPole(this);
	fIsSpinning = false;
	Invalidate();
}


/** @brief Sets the animation speed of the barber pole.
 *  @param speed  Fractional speed in the range [-1.0, 1.0].  Positive values
 *                scroll left-to-right; negative values scroll right-to-left.
 *                Values outside the range are clamped.
 */
void
BarberPole::SetSpinSpeed(float speed)
{
	if (speed > 1.0f)
		speed = 1.0f;
	if (speed < -1.0f)
		speed = -1.0f;
	fSpinSpeed = speed;
}


/** @brief Replaces the stripe color palette.
 *  @param colors     Array of rgb_color values to cycle through.
 *  @param numColors  Number of entries in the \a colors array.
 */
void
BarberPole::SetColors(const rgb_color* colors, uint32 numColors)
{
	delete[] fColors;
	rgb_color* colorsCopy = new rgb_color[numColors];
	for (uint32 i = 0; i < numColors; i++)
		colorsCopy[i] = colors[i];

	fColors = colorsCopy;
	fNumColors = numColors;
}


/** @brief Advances the scroll offset by one tick and invalidates the view. */
void
BarberPole::_Spin()
{
	fScrollOffset += fStripeWidth / (1.0f / fSpinSpeed);
	if (fScrollOffset >= fStripeWidth * fNumColors) {
		// Cycle completed, jump back to where we started
		fScrollOffset = 0;
	}
	Invalidate();
	//Parent()->Invalidate();
}
