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
 * MIT License. Copyright 2005-2012, Haiku.
 * Original authors: Stephan Aßmus.
 */

/** @file HWInterface.h
    @brief Abstract graphics hardware interface: framebuffer, mode setting, cursor, overlays. */

#ifndef HW_INTERFACE_H
#define HW_INTERFACE_H


#include <AutoDeleter.h>
#include <Accelerant.h>
#include <GraphicsCard.h>
#include <List.h>
#include <Locker.h>
#include <OS.h>
#include <Region.h>

#include <video_overlay.h>

#include <new>

#include "IntRect.h"
#include "MultiLocker.h"
#include "ServerCursor.h"


class BString;
class DrawingEngine;
class EventStream;
class Overlay;
class RenderingBuffer;
class ServerBitmap;


/** @brief Observer interface for parties that need to react to framebuffer or screen changes.
 *
 * DrawingEngines register as listeners so they can re-attach Painter to a
 * fresh framebuffer. Higher-level clients register so they can be informed of
 * mode changes (size, depth, refresh rate).
 */
class HWInterfaceListener {
public:
								HWInterfaceListener();
	virtual						~HWInterfaceListener();

	/** @brief Notification that the framebuffer pointer or stride changed.
	 *  @note  Default implementation is a no-op; downstream DrawingEngines override it. */
	virtual	void				FrameBufferChanged() {};
		// Informs a downstream DrawingEngine of a changed framebuffer.

	/** @brief Notification that the screen mode (resolution / depth) changed.
	 *  @note  Default implementation is a no-op; upstream clients override it. */
	virtual	void				ScreenChanged(HWInterface* interface) {};
		// Informs an upstream client of a changed screen configuration.
};


/** @brief Abstract base class for the graphics hardware backing a Screen.
 *
 * HWInterface owns the framebuffer pointer (front and optional back), the
 * cursor sprite, the drag bitmap, and the overlay channel state. Subclasses
 * implement the actual transport (real accelerant driver, VESA, virtual
 * framebuffer, off-screen bitmap, remote desktop). The class inherits a
 * MultiLocker so callers can grab parallel (read) or exclusive (write) access
 * before driving the hardware.
 */
class HWInterface : protected MultiLocker {
public:
								HWInterface();
	virtual						~HWInterface();

	// locking
	/** @brief Acquires a read (parallel) lock; multiple readers may hold it simultaneously. */
			bool				LockParallelAccess() { return ReadLock(); }
#if DEBUG
	/** @brief Returns true while the calling thread holds a read lock (debug only). */
			bool				IsParallelAccessLocked() const
									{ return IsReadLocked(); }
#endif
	/** @brief Releases the parallel lock previously acquired with LockParallelAccess(). */
			void				UnlockParallelAccess() { ReadUnlock(); }

	/** @brief Acquires the write (exclusive) lock used for mode set / framebuffer swap. */
			bool				LockExclusiveAccess() { return WriteLock(); }
	/** @brief Returns true while the calling thread holds the write lock. */
			bool				IsExclusiveAccessLocked()
									{ return IsWriteLocked(); }
	/** @brief Releases the exclusive lock. */
			void				UnlockExclusiveAccess() { WriteUnlock(); }

	// You need to WriteLock
	/** @brief Initialises the interface; call once after construction.
	 *  @return B_OK on success, otherwise the underlying lock or driver error. */
	virtual	status_t			Initialize();
	/** @brief Releases driver-side resources prior to destruction. */
	virtual	status_t			Shutdown() = 0;

	// allocating a DrawingEngine attached to this HWInterface
	/** @brief Returns a fresh DrawingEngine attached to this interface (caller owns). */
	virtual	DrawingEngine*		CreateDrawingEngine();

	// creating an event stream specific for this HWInterface
	// returns NULL when there is no specific event stream necessary
	/** @brief Returns a driver-specific event stream, or NULL when the default is fine. */
	virtual	EventStream*		CreateEventStream();

	// screen mode stuff
	/** @brief Switches the display to @a mode (must hold write lock). */
	virtual	status_t			SetMode(const display_mode& mode) = 0;
	/** @brief Fills @a mode with the current display mode. */
	virtual	void				GetMode(display_mode* mode) = 0;

	/** @brief Returns descriptive information about the underlying accelerant. */
	virtual status_t			GetDeviceInfo(accelerant_device_info* info) = 0;
	/** @brief Allocates and returns the list of supported display modes (caller frees). */
	virtual status_t			GetModeList(display_mode** _modeList,
									uint32* _count) = 0;
	/** @brief Returns the lowest and highest supported pixel clocks for @a mode. */
	virtual status_t			GetPixelClockLimits(display_mode* mode,
									uint32* _low, uint32* _high) = 0;
	/** @brief Returns the timing constraints of the connected display. */
	virtual status_t			GetTimingConstraints(display_timing_constraints*
									constraints) = 0;
	/** @brief Adjusts @a candidate to the closest mode within the @a low / @a high envelope. */
	virtual status_t			ProposeMode(display_mode* candidate,
									const display_mode* low,
									const display_mode* high) = 0;
	/** @brief Returns the monitor's preferred mode if the driver knows one. */
	virtual	status_t			GetPreferredMode(display_mode* mode);
	/** @brief Returns EDID-derived monitor info if available. */
	virtual status_t			GetMonitorInfo(monitor_info* info);

	/** @brief Returns a semaphore that fires once per vertical retrace. */
	virtual sem_id				RetraceSemaphore() = 0;
	/** @brief Blocks the caller until the next vertical retrace, up to @a timeout. */
	virtual status_t			WaitForRetrace(
									bigtime_t timeout = B_INFINITE_TIMEOUT) = 0;

	/** @brief Switches the monitor power state (B_DPMS_ON, _STAND_BY, _SUSPEND, _OFF). */
	virtual status_t			SetDPMSMode(uint32 state) = 0;
	/** @brief Returns the current DPMS state. */
	virtual uint32				DPMSMode() = 0;
	/** @brief Returns the bitmask of DPMS states supported by the driver. */
	virtual uint32				DPMSCapabilities() = 0;

	/** @brief Sets the display brightness in [0.0, 1.0]. */
	virtual status_t			SetBrightness(float) = 0;
	/** @brief Returns the current display brightness in [0.0, 1.0]. */
	virtual status_t			GetBrightness(float*) = 0;

	/** @brief Returns the file system path to the loaded accelerant add-on. */
	virtual status_t			GetAccelerantPath(BString& path);
	/** @brief Returns the file system path to the kernel graphics driver. */
	virtual status_t			GetDriverPath(BString& path);

	// cursor handling (these do their own Read/Write locking)
	/** @brief Returns the current cursor sprite (no drag bitmap composited). */
			ServerCursorReference Cursor() const;
	/** @brief Returns the cursor with the optional drag bitmap composited. */
			ServerCursorReference CursorAndDragBitmap() const;
	/** @brief Replaces the cursor sprite. */
	virtual	void				SetCursor(ServerCursor* cursor);
	/** @brief Shows or hides the cursor without changing the cursor image. */
	virtual	void				SetCursorVisible(bool visible);
	/** @brief Returns true when the cursor is currently visible (not obscured). */
			bool				IsCursorVisible();
	/** @brief Hides the cursor until the mouse moves again ("obscured" state). */
	virtual	void				ObscureCursor();
	/** @brief Repositions the cursor to logical coordinates (@a x, @a y). */
	virtual	void				MoveCursorTo(float x, float y);
	/** @brief Returns the current cursor position in logical screen coordinates. */
			BPoint				CursorPosition();

	/** @brief Attaches a drag bitmap composited under the cursor at @a offsetFromCursor. */
	virtual	void				SetDragBitmap(const ServerBitmap* bitmap,
									const BPoint& offsetFromCursor);

	// overlay support
	/** @brief Reserves a hardware overlay channel; returns NULL when none are available. */
	virtual overlay_token		AcquireOverlayChannel();
	/** @brief Releases an overlay channel previously obtained from AcquireOverlayChannel(). */
	virtual void				ReleaseOverlayChannel(overlay_token token);

	/** @brief Returns the alignment / size restrictions for the supplied overlay. */
	virtual status_t			GetOverlayRestrictions(const Overlay* overlay,
									overlay_restrictions* restrictions);
	/** @brief Reports whether a (width, height, color space) overlay request is supported. */
	virtual bool				CheckOverlayRestrictions(int32 width,
									int32 height, color_space colorSpace);
	/** @brief Allocates an overlay buffer of the given dimensions; NULL on failure. */
	virtual const overlay_buffer* AllocateOverlayBuffer(int32 width,
									int32 height, color_space space);
	/** @brief Frees a buffer previously returned by AllocateOverlayBuffer(). */
	virtual void				FreeOverlayBuffer(const overlay_buffer* buffer);

	/** @brief Pushes the overlay's view / window descriptors to the accelerant. */
	virtual void				ConfigureOverlay(Overlay* overlay);
	/** @brief Removes the overlay from the screen until it is reconfigured. */
	virtual void				HideOverlay(Overlay* overlay);

	// frame buffer access (you need to ReadLock!)
	/** @brief Returns BackBuffer() when double buffered, otherwise FrontBuffer(). */
			RenderingBuffer*	DrawingBuffer() const;
	/** @brief Returns the buffer that the accelerant scans out to the display. */
	virtual	RenderingBuffer*	FrontBuffer() const = 0;
	/** @brief Returns the back buffer used while double-buffering, or NULL. */
	virtual	RenderingBuffer*	BackBuffer() const = 0;
	/** @brief Returns true when a back buffer is allocated and used. */
	virtual	bool				IsDoubleBuffered() const = 0;

	// Invalidate is used for scheduling an area for updating
	/** @brief Schedules every rectangle in @a region for refresh. */
	virtual	status_t			InvalidateRegion(const BRegion& region);
	/** @brief Schedules @a frame for refresh; copies back-to-front when double buffered. */
	virtual	status_t			Invalidate(const BRect& frame);
	// while CopyBackToFront() actually performs the operation
	/** @brief Performs the actual back-to-front blit for the given @a frame. */
	virtual	status_t			CopyBackToFront(const BRect& frame);

protected:
	/** @brief Does the actual back-to-front blit, excluding the cursor area. */
	virtual	void				_CopyBackToFront(/*const*/ BRegion& region);

public:
	// TODO: Just a quick and primitive way to get single buffered mode working.
	// Later, the implementation should be smarter, right now, it will
	// draw the cursor for almost every drawing operation.
	// It seems to me BeOS hides the cursor (in laymans words) before
	// BView::Draw() is called (if the cursor is within that views clipping region),
	// then, after all drawing commands that triggered have been caried out,
	// it shows the cursor again. This approach would have the advantage of
	// the code not cluttering/slowing down DrawingEngine.
	// For now, we hide the cursor for any drawing operation that has
	// a bounding box containing the cursor (in DrawingEngine) so
	// the cursor hiding is completely transparent from code using DrawingEngine.
	// ---
	// NOTE: Investigate locking for these! The client code should already hold a
	// ReadLock, but maybe these functions should acquire a WriteLock!
	/** @brief Hides the cursor (and any drag bitmap) if it intersects @a area; returns true when hidden. */
			bool				HideFloatingOverlays(const BRect& area);
	/** @brief Hides the cursor unconditionally; returns true when it was visible. */
			bool				HideFloatingOverlays();
	/** @brief Restores the cursor previously hidden by HideFloatingOverlays(). */
			void				ShowFloatingOverlays();

	// Listener support
	/** @brief Adds @a listener to the set notified about framebuffer / mode changes. */
			bool				AddListener(HWInterfaceListener* listener);
	/** @brief Removes a previously registered listener. */
			void				RemoveListener(HWInterfaceListener* listener);

protected:
	// implement this in derived classes
	/** @brief Default software cursor renderer; subclasses with hw cursors override it. */
	virtual	void				_DrawCursor(IntRect area) const;

	// does the actual transfer and handles color space conversion
	/** @brief Copies @a src bytes to the front buffer, handling color-space conversion. */
			void				_CopyToFront(uint8* src, uint32 srcBPR, int32 x,
									int32 y, int32 right, int32 bottom) const;

	/** @brief Returns the rectangle currently occupied by the (visible) cursor. */
			IntRect				_CursorFrame() const;
	/** @brief Restores the framebuffer pixels saved when the cursor was drawn. */
			void				_RestoreCursorArea() const;
	/** @brief Composes the drag bitmap onto the cursor sprite. */
			void				_AdoptDragBitmap();

	/** @brief Notifies all registered listeners that the framebuffer changed. */
			void				_NotifyFrameBufferChanged();
	/** @brief Notifies all registered listeners that the screen mode changed. */
			void				_NotifyScreenChanged();

	/** @brief Returns true when @a mode passes basic sanity checks (width, height, etc.). */
	static	bool				_IsValidMode(const display_mode& mode);

			// If we draw the cursor somewhere in the drawing buffer,
			// we need to backup its contents before drawing, so that
			// we can restore that area when the cursor needs to be
			// drawn somewhere else.
			/** @brief Off-screen save of the framebuffer pixels covered by the software cursor. */
			struct buffer_clip {
				buffer_clip(int32 width, int32 height)
				{
					bpr = width * 4;
					if (bpr > 0 && height > 0)
						buffer = new(std::nothrow) uint8[bpr * height];
					else
						buffer = NULL;
					left = 0;
					top = 0;
					right = -1;
					bottom = -1;
					cursor_hidden = true;
				}

				~buffer_clip()
				{
					delete[] buffer;
				}

				uint8*			buffer;
				int32			left;
				int32			top;
				int32			right;
				int32			bottom;
				int32			bpr;
				bool			cursor_hidden;
			};

			ObjectDeleter<buffer_clip>
								fCursorAreaBackup;
	mutable	BLocker				fFloatingOverlaysLock;

			ServerCursorReference
								fCursor;
			BReference<ServerBitmap>
								fDragBitmap;
			BPoint				fDragBitmapOffset;
			ServerCursorReference
								fCursorAndDragBitmap;
			bool				fCursorVisible;
			bool				fCursorObscured;
			bool				fHardwareCursorEnabled;
			BPoint				fCursorLocation;

			BRect				fTrackingRect;

			int					fVGADevice;

private:
			BList				fListeners;
};

#endif // HW_INTERFACE_H
