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
 * MIT License. Copyright 2006-2007, Haiku.
 * Original authors: Axel Dörfler.
 */

/** @file Overlay.h
    @brief Hardware overlay channel wrapper used to display video / DMA bitmaps. */

#ifndef OVERLAY_H
#define OVERLAY_H


#include <InterfaceDefs.h>

#include <video_overlay.h>


class HWInterface;
class ServerBitmap;
struct overlay_client_data;


/** @brief Wraps a hardware overlay channel and the buffer being shown through it.
 *
 * An Overlay couples a ServerBitmap with an accelerant overlay slot, exposing
 * the channel's view/window descriptors and managing the per-overlay semaphore
 * that the client uses to synchronise frame updates with the app_server.
 */
class Overlay {
	public:
		/** @brief Constructs an overlay bound to @a interface and backed by @a bitmap.
		 *  @param interface Hardware interface owning the overlay channel.
		 *  @param bitmap    Bitmap whose pixel storage will be displayed through the overlay.
		 *  @param token     Accelerant token previously obtained from the HWInterface. */
		Overlay(HWInterface& interface, ServerBitmap* bitmap,
			overlay_token token);

		/** @brief Releases the overlay channel and frees the overlay buffer. */
		~Overlay();

		/** @brief Reports whether the semaphore and overlay buffer were created successfully.
		 *  @return B_OK on success, B_NO_MEMORY when the overlay buffer could not be allocated. */
		status_t InitCheck() const;

		/** @brief Frees the overlay buffer while keeping the channel descriptor alive.
		 *  @param bitmap         Bitmap currently associated with the overlay.
		 *  @param needTemporary  Reserved; reserved for future temporary-buffer support. */
		status_t Suspend(ServerBitmap* bitmap, bool needTemporary);

		/** @brief Re-allocates an overlay buffer for a previously suspended overlay.
		 *  @param bitmap Bitmap describing the desired width, height, and color space. */
		status_t Resume(ServerBitmap* bitmap);

		/** @brief Records the client-shared data block describing the overlay buffer. */
		void SetClientData(overlay_client_data* clientData);

		/** @brief Translates B_OVERLAY_* user flags into accelerant overlay window flags. */
		void SetFlags(uint32 flags);

		/** @brief Steals the channel token from @a other; used during overlay handoff. */
		void TakeOverToken(Overlay* other);

		/** @brief Returns the current accelerant overlay buffer descriptor. */
		const overlay_buffer* OverlayBuffer() const;

		/** @brief Returns the client-shared overlay data structure. */
		overlay_client_data* ClientData() const;

		/** @brief Returns the accelerant overlay channel token. */
		overlay_token OverlayToken() const;

		/** @brief Computes the color-key bit pattern for the supplied @a colorSpace. */
		void SetColorSpace(uint32 colorSpace);

		/** @brief Returns the cached overlay window descriptor used by the accelerant. */
		const overlay_window* OverlayWindow() const
			{ return &fWindow; }

		/** @brief Returns the cached overlay view descriptor used by the accelerant. */
		const overlay_view* OverlayView() const
			{ return &fView; }

		/** @brief Returns the synchronisation semaphore handed to the client. */
		sem_id Semaphore() const
			{ return fSemaphore; }

		/** @brief Returns the color-key value used by the overlay. */
		const rgb_color& Color() const
			{ return fColor; }

		/** @brief Configures source and destination rectangles and pushes them to the accelerant.
		 *  @param source      Source rectangle inside the overlay buffer.
		 *  @param destination Destination rectangle on screen. */
		void Configure(const BRect& source, const BRect& destination);

		/** @brief Hides the overlay until the next Configure() call. */
		void Hide();

	private:
		void _FreeBuffer();
		status_t _AllocateBuffer(ServerBitmap* bitmap);

		HWInterface&			fHWInterface;
		const overlay_buffer*	fOverlayBuffer;
		overlay_client_data*	fClientData;
		overlay_token			fOverlayToken;
		overlay_view			fView;
		overlay_window			fWindow;
		sem_id					fSemaphore;
		rgb_color				fColor;
};

#endif	// OVERLAY_H
