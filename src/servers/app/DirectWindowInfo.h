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
 * MIT License. Copyright 2008-2009, Haiku. All rights reserved.
 */

/** @file DirectWindowInfo.h
    @brief Manages the shared memory and synchronisation state for a BDirectWindow. */

#ifndef DIRECT_WINDOW_INFO_H
#define DIRECT_WINDOW_INFO_H


#include <Autolock.h>
#include <DirectWindow.h>

#include <DirectWindowPrivate.h>

class RenderingBuffer;


/** @brief Holds the server-side state required to give a BDirectWindow client
           direct access to the frame buffer, including shared buffer info and
           synchronisation semaphores. */
class DirectWindowInfo {
public:
								DirectWindowInfo();
								~DirectWindowInfo();

			/** @brief Returns B_OK if the shared memory and semaphores were created successfully.
			    @return B_OK on success, or an error code. */
			status_t			InitCheck() const;

			/** @brief Copies synchronisation data into the caller's structure.
			    @param data Output direct_window_sync_data to populate.
			    @return B_OK on success, or an error code. */
			status_t			GetSyncData(
									direct_window_sync_data& data) const;

			/** @brief Updates the shared buffer state and synchronises with the client.
			    @param bufferState New direct buffer state flags.
			    @param driverState New driver state flags.
			    @param renderingBuffer Pointer to the current rendering buffer.
			    @param windowFrame Frame of the window in screen coordinates.
			    @param clipRegion Visible clip region for the window.
			    @return B_OK on success, or an error code. */
			status_t			SetState(direct_buffer_state bufferState,
									direct_driver_state driverState,
									RenderingBuffer* renderingBuffer,
									const BRect& windowFrame,
									const BRegion& clipRegion);

			/** @brief Saves the window's current frame and feel, then sets full-screen mode.
			    @param frame The full-screen frame to occupy.
			    @param feel The window feel to apply during full-screen. */
			void				EnableFullScreen(const BRect& frame,
									window_feel feel);

			/** @brief Restores the window's saved frame and feel, ending full-screen mode. */
			void				DisableFullScreen();

			/** @brief Returns whether the window is currently in full-screen mode.
			    @return true if full-screen is active. */
			bool				IsFullScreen() const { return fFullScreen; }

			/** @brief Returns the saved window frame from before full-screen was entered.
			    @return Const reference to the original BRect. */
			const BRect&		OriginalFrame() const { return fOriginalFrame; }

			/** @brief Returns the saved window feel from before full-screen was entered.
			    @return Original window_feel value. */
			window_feel			OriginalFeel() const { return fOriginalFeel; }

private:
			status_t			_SynchronizeWithClient();

			direct_buffer_info*	fBufferInfo;
			sem_id				fSem;
			sem_id				fAcknowledgeSem;
			area_id				fBufferArea;

			BRect				fOriginalFrame;
			window_feel			fOriginalFeel;
			bool				fFullScreen;
};


#endif	// DIRECT_WINDOW_INFO_H
