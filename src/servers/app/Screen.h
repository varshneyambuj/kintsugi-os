/*
 * Copyright 2025, Kintsugi OS Contributors.
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
 * This file incorporates work from the Haiku project, originally
 * distributed under the MIT License.
 * Copyright 2001-2009, Haiku, Inc.
 * Authors:
 *		Adi Oanca <adioanca@myrealbox.com>
 *		Axel Dörfler, axeld@pinc-software.de
 *		Stephan Aßmus, <superstippi@gmx.de>
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file Screen.h
 *  @brief Represents a single physical output screen with display-mode control. */

#ifndef SCREEN_H
#define SCREEN_H


#include <AutoDeleter.h>
#include <Accelerant.h>
#include <GraphicsDefs.h>
#include <Point.h>


class DrawingEngine;
class HWInterface;

/** @brief Encapsulates a physical display, its drawing engine, and mode management. */
class Screen {
public:
	/** @brief Constructs a Screen bound to an existing hardware interface.
	 *  @param interface Hardware interface to use.
	 *  @param id        Unique screen identifier. */
								Screen(::HWInterface *interface, int32 id);

	/** @brief Constructs an uninitialised Screen placeholder. */
								Screen();
	virtual						~Screen();

	/** @brief Initialises the hardware interface and drawing engine.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			Initialize();

	/** @brief Shuts down the drawing engine and hardware interface. */
			void				Shutdown();

	/** @brief Returns the unique identifier for this screen.
	 *  @return Screen ID. */
			int32				ID() const { return fID; }

	/** @brief Retrieves hardware monitor information.
	 *  @param info Reference that receives monitor details.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			GetMonitorInfo(monitor_info& info) const;

	/** @brief Switches the display to the given mode.
	 *  @param mode Desired display mode.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			SetMode(const display_mode& mode);

	/** @brief Switches the display to the specified resolution and timing.
	 *  @param width      Pixel width.
	 *  @param height     Pixel height.
	 *  @param colorspace Color space constant.
	 *  @param timing     Desired display timing.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			SetMode(uint16 width, uint16 height,
									uint32 colorspace,
									const display_timing& timing);

	/** @brief Applies the monitor's preferred display mode.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			SetPreferredMode();

	/** @brief Applies the mode that best matches the requested parameters.
	 *  @param width      Desired pixel width.
	 *  @param height     Desired pixel height.
	 *  @param colorspace Desired color space.
	 *  @param frequency  Desired refresh rate in Hz.
	 *  @param strict     If true, must match all parameters exactly.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			SetBestMode(uint16 width, uint16 height,
									uint32 colorspace, float frequency,
									bool strict = true);

	/** @brief Returns the currently active display mode.
	 *  @param mode Reference that receives the mode. */
			void				GetMode(display_mode& mode) const;

	/** @brief Returns basic parameters of the current display mode.
	 *  @param width      Receives pixel width.
	 *  @param height     Receives pixel height.
	 *  @param colorspace Receives color space.
	 *  @param frequency  Receives refresh rate in Hz. */
			void				GetMode(uint16 &width, uint16 &height,
									uint32 &colorspace, float &frequency) const;

	/** @brief Sets the virtual frame position of this screen in the desktop.
	 *  @param rect New frame rectangle. */
			void				SetFrame(const BRect& rect);

	/** @brief Returns the screen's frame in the virtual desktop coordinate system.
	 *  @return Current frame rectangle. */
			BRect				Frame() const;

	/** @brief Returns the color space of the current display mode.
	 *  @return Active color_space constant. */
			color_space			ColorSpace() const;

	/** @brief Returns the drawing engine associated with this screen.
	 *  @return Pointer to the DrawingEngine, or NULL if not initialised. */
	inline	DrawingEngine*		GetDrawingEngine() const
									{ return fDriver.Get(); }

	/** @brief Returns the hardware interface used by this screen.
	 *  @return Pointer to the HWInterface. */
	inline	::HWInterface*		HWInterface() const
									{ return fHWInterface.Get(); }

private:
			int32				_FindBestMode(const display_mode* modeList,
									uint32 count, uint16 width, uint16 height,
									uint32 colorspace, float frequency) const;

			int32				fID;
			ObjectDeleter< ::HWInterface>
								fHWInterface;
			ObjectDeleter<DrawingEngine>
								fDriver;
};

#endif	/* SCREEN_H */
