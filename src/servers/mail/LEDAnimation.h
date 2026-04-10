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
/** @file LEDAnimation.h
 *  @brief Keyboard LED blink animation class for mail alerts. */
#ifndef __LEDANIMATION_H__
#define __LEDANIMATION_H__

#include <SupportDefs.h>
#include <OS.h>

/** @brief Animates keyboard LEDs to signal new mail arrival. */
class LEDAnimation {
public:
				/** @brief Construct the animation (initially stopped). */
							LEDAnimation();
				/** @brief Destructor; stops the animation and restores modifiers. */
							~LEDAnimation();
				/** @brief Spawn the animation thread. */
				void		Start();
				/** @brief Stop the animation thread and restore LED state. */
				void		Stop();
				/** @brief Return true if the animation thread is active. */
				bool		IsRunning() const {return fRunning;}
private:
	static	int32			AnimationThread(void *data);
	static	void			LED(uint32 mod, bool on);
		thread_id			fThread; /**< Animation thread ID */
			volatile bool	fRunning; /**< Thread running flag */
			uint32			fOrigModifiers; /**< Saved modifier state to restore on stop */
};

#endif
