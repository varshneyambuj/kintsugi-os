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
 */
/** @file LEDAnimation.cpp
 *  @brief Keyboard LED animation for new mail notification. */
#include "LEDAnimation.h"

#include <InterfaceDefs.h>

#include <stdio.h>

#define SNOOZE_TIME 150000


/** @brief Constructs the LED animation, saving the current keyboard modifier state. */
LEDAnimation::LEDAnimation()
	:
	fThread(-1),
	fRunning(false),
	fOrigModifiers(::modifiers())
{
}


/** @brief Destroys the LED animation, stopping it if still running. */
LEDAnimation::~LEDAnimation()
{
	Stop();
}


/**
 * @brief Starts the LED animation by spawning a background thread.
 *
 * Clears all keyboard locks and begins cycling through Num Lock,
 * Caps Lock, and Scroll Lock LEDs. Does nothing if already running.
 */
void
LEDAnimation::Start()
{
	// don't do anything if the thread is already running
	if (fThread >= 0)
		return;

	fOrigModifiers = ::modifiers();
	::set_keyboard_locks(0);
	fRunning = true;
	fThread = ::spawn_thread(AnimationThread,"LED thread",B_NORMAL_PRIORITY,this);
	::resume_thread(fThread);
}


/**
 * @brief Stops the LED animation and restores the original keyboard modifier state.
 *
 * Waits for the animation thread to exit before returning.
 * Does nothing if the animation is not running.
 */
void
LEDAnimation::Stop()
{
	// don't do anything if the thread doesn't run
	if (fThread < 0)
		return;

	fRunning = false;
	status_t result;
	::wait_for_thread(fThread,&result);

	::set_keyboard_locks(fOrigModifiers);
}


/**
 * @brief Thread function that cycles keyboard LEDs in sequence.
 *
 * Loops through Num Lock, Caps Lock, and Scroll Lock in a wave pattern
 * until fRunning is set to false.
 *
 * @param data Pointer to the LEDAnimation instance.
 * @return 0 always.
 */
int32
LEDAnimation::AnimationThread(void* data)
{
	LEDAnimation *anim = (LEDAnimation*)data;

	while (anim->fRunning) {
		LED(B_NUM_LOCK,true);
		LED(B_NUM_LOCK,false);

		LED(B_CAPS_LOCK,true);
		LED(B_CAPS_LOCK,false);

		LED(B_SCROLL_LOCK,true);
		LED(B_SCROLL_LOCK,false);

		LED(B_CAPS_LOCK,true);
		LED(B_CAPS_LOCK,false);
	}
	anim->fThread = -1;
	return 0;
}


/**
 * @brief Sets or clears a single keyboard LED and pauses briefly when turning it on.
 *
 * @param mod The modifier flag (e.g., B_NUM_LOCK) representing the LED.
 * @param on  If true, turns the LED on and pauses; if false, turns it off.
 */
void
LEDAnimation::LED(uint32 mod,bool on)
{
	uint32 current_modifiers = ::modifiers();
	if (on)
		current_modifiers |= mod;
	else
		current_modifiers &= ~mod;
	::set_keyboard_locks(current_modifiers);
	if (on)
		::snooze(SNOOZE_TIME);
}
