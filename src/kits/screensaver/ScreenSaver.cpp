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
 *   Copyright 2003-2006, Michael Phipps. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ScreenSaver.cpp
 * @brief Base class implementation for all Kintsugi OS screen saver add-ons.
 *
 * BScreenSaver is the abstract base class that every screen saver add-on must
 * subclass. It manages the tick interval and loop parameters used by
 * ScreenSaverRunner and provides virtual hooks — InitCheck(), StartSaver(),
 * StopSaver(), Draw(), DirectDraw(), StartConfig(), StopConfig(), SupplyInfo(),
 * ModulesChanged(), and SaveState() — that subclasses override to implement
 * their animation and preferences UI logic.
 *
 * @see ScreenSaverRunner, ScreenSaverSettings
 */


#include "ScreenSaver.h"


/**
 * @brief Constructor — initialises the tick size and loop counters to defaults.
 *
 * @param archive    The BMessage carrying saved state from a previous run,
 *                   or an empty message on first load.
 * @param thisImage  The image_id of the add-on itself; can be used to locate
 *                   resources bundled in the add-on binary.
 */
BScreenSaver::BScreenSaver(BMessage *archive, image_id thisImage)
	:
	fTickSize(50000),
	fLoopOnCount(0),
	fLoopOffCount(0)
{
}


/**
 * @brief Destructor — subclasses should release their resources here.
 */
BScreenSaver::~BScreenSaver()
{
}


/**
 * @brief Check whether the screen saver initialised successfully.
 *
 * Called by ScreenSaverRunner after construction. Subclasses should override
 * this to return an appropriate error code if construction failed.
 *
 * @return B_OK always in the base implementation.
 */
status_t
BScreenSaver::InitCheck()
{
    // This method is meant to be overridden
    return B_OK;
}


/**
 * @brief Begin the screen saver animation.
 *
 * Called by ScreenSaverRunner when the screen saver is activated. Subclasses
 * should perform any one-time setup here (e.g. allocate off-screen bitmaps).
 *
 * @param view     The BView the saver should draw into.
 * @param preview  True when the saver is running in the small preview area
 *                 of the Screen Saver preferences panel.
 * @return B_OK always in the base implementation.
 */
status_t
BScreenSaver::StartSaver(BView *view, bool preview)
{
    // This method is meant to be overridden
    return B_OK;
}


/**
 * @brief Cease the screen saver animation and release acquired resources.
 *
 * Called before the screen saver is unloaded or when the screen saver is
 * deactivated. Subclasses should undo anything done in StartSaver().
 */
void
BScreenSaver::StopSaver()
{
	// This method is meant to be overridden
	return;
}


/**
 * @brief Render the next animation frame into @a view.
 *
 * Called by ScreenSaverRunner once per tick interval. Subclasses must override
 * this to produce the screen saver visuals.
 *
 * @param view   The BView to draw into (same view passed to StartSaver()).
 * @param frame  Zero-based frame counter; incremented on each Draw() call.
 */
void
BScreenSaver::Draw(BView *view, int32 frame)
{
	// This method is meant to be overridden
	return;
}


/**
 * @brief Provide direct-framebuffer rendering for BDirectWindow-backed savers.
 *
 * Called with direct-access information when the screen saver is running in a
 * BDirectWindow. Subclasses that prefer direct pixel access override this.
 *
 * @param info  Pointer to the direct_buffer_info structure with framebuffer
 *              address and geometry.
 */
void
BScreenSaver::DirectConnected(direct_buffer_info *info)
{
	// This method is meant to be overridden
	return;
}


/**
 * @brief Render a direct-framebuffer frame (BDirectWindow variant).
 *
 * Called instead of, or in addition to, Draw() when the screen saver is
 * running inside a BDirectWindow. Subclasses override to perform direct pixel
 * writes.
 *
 * @param frame  Zero-based frame counter; incremented on each call.
 */
void
BScreenSaver::DirectDraw(int32 frame)
{
	// This method is meant to be overridden
	return;
}


/**
 * @brief Build and attach the configuration UI into @a configView.
 *
 * Called when the user selects this screen saver in the preferences panel.
 * Subclasses should populate @a configView with their settings controls.
 * The default implementation does nothing (no configuration available).
 *
 * @param configView  The BView that should host the configuration controls.
 */
void
BScreenSaver::StartConfig(BView *configView)
{
	// This method is meant to be overridden
	return;
}


/**
 * @brief Tear down the configuration UI.
 *
 * Called when the user switches away from this screen saver in the preferences
 * panel. Subclasses should perform any cleanup needed when the config view is
 * about to be removed.
 */
void
BScreenSaver::StopConfig()
{
	// This method is meant to be overridden
	return;
}


/**
 * @brief Fill @a info with metadata about this screen saver module.
 *
 * Subclasses override this to add name, version, author, and copyright fields
 * to @a info so that the preferences panel can display them.
 *
 * @param info  Output BMessage to populate with module metadata.
 */
void
BScreenSaver::SupplyInfo(BMessage* info) const
{
	// This method is meant to be overridden
	return;
}


/**
 * @brief Notify the saver that the list of available modules has changed.
 *
 * Called by the Screen Saver infrastructure when add-ons are installed or
 * removed while this saver's configuration view is visible. Subclasses may
 * update their UI in response.
 *
 * @param info  BMessage describing the change (currently unused in the base).
 */
void
BScreenSaver::ModulesChanged(const BMessage* info)
{
	// This method is meant to be overridden
	return;
}


/**
 * @brief Persist the screen saver's current state to @a into.
 *
 * Called before the add-on is unloaded. Subclasses should flatten their
 * settings into @a into so they can be restored in the next StartSaver() call.
 *
 * @param into  Output BMessage to populate with the saver's state.
 * @return B_OK on success; the base returns B_ERROR (no state to save).
 */
status_t
BScreenSaver::SaveState(BMessage *into) const
{
    // This method is meant to be overridden
    return B_ERROR;
}


/**
 * @brief Set the tick interval between successive Draw() calls.
 *
 * @param tickSize  Desired interval in microseconds; 50000 by default (20 fps).
 */
void
BScreenSaver::SetTickSize(bigtime_t tickSize)
{
	fTickSize = tickSize;
}


/**
 * @brief Return the current tick interval in microseconds.
 *
 * @return The tick size last set by SetTickSize(), or the default 50000 µs.
 */
bigtime_t
BScreenSaver::TickSize() const
{
    return fTickSize;
}


/**
 * @brief Configure the on/off looping cadence for the animation.
 *
 * When @a onCount is non-zero, the runner will draw @a onCount frames, then
 * sleep for @a offCount ticks before drawing again.
 *
 * @param onCount   Number of consecutive active frames before a pause;
 *                  0 disables looping (always active).
 * @param offCount  Number of idle ticks to sleep after the active run.
 */
void
BScreenSaver::SetLoop(int32 onCount, int32 offCount)
{
	fLoopOnCount = onCount;
	fLoopOffCount = offCount;
}


/**
 * @brief Return the number of consecutive active frames before looping pauses.
 *
 * @return The on-count set by SetLoop(), or 0 if looping is disabled.
 */
int32
BScreenSaver::LoopOnCount() const
{
    return fLoopOnCount;
}


/**
 * @brief Return the number of idle ticks to sleep between active runs.
 *
 * @return The off-count set by SetLoop(), or 0 if looping is disabled.
 */
int32
BScreenSaver::LoopOffCount() const
{
    return fLoopOffCount;
}


void BScreenSaver::_ReservedScreenSaver1() {}
void BScreenSaver::_ReservedScreenSaver2() {}
void BScreenSaver::_ReservedScreenSaver3() {}
void BScreenSaver::_ReservedScreenSaver4() {}
void BScreenSaver::_ReservedScreenSaver5() {}
void BScreenSaver::_ReservedScreenSaver6() {}
void BScreenSaver::_ReservedScreenSaver7() {}
void BScreenSaver::_ReservedScreenSaver8() {}

// for compatibility with older BeOS versions
extern "C" {
void ReservedScreenSaver1__12BScreenSaver() {}
void ReservedScreenSaver2__12BScreenSaver() {}
void ReservedScreenSaver3__12BScreenSaver() {}
void ReservedScreenSaver4__12BScreenSaver() {}
void ReservedScreenSaver5__12BScreenSaver() {}
void ReservedScreenSaver6__12BScreenSaver() {}
void ReservedScreenSaver7__12BScreenSaver() {}
void ReservedScreenSaver8__12BScreenSaver() {}
}
