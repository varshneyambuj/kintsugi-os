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
 *   Copyright 2005, Michael Lotz <mmlr@mlotz.ch>.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file AccelerantBuffer.cpp
 * @brief RenderingBuffer that maps directly onto the graphics card's
 *        frame buffer as exposed by the accelerant.
 *
 * Used by AccelerantHWInterface to give the drawing engine access to
 * either the on-screen visible region or the off-screen back buffer that
 * many cards place immediately below the visible region.
 */


#include "AccelerantBuffer.h"


/**
 * @brief Constructs an empty buffer; the mode and frame-buffer config must
 *        be installed before InitCheck() will pass.
 */
AccelerantBuffer::AccelerantBuffer()
	: fDisplayModeSet(false),
	  fFrameBufferConfigSet(false),
	  fIsOffscreenBuffer(false)
{
}


/**
 * @brief Constructs a buffer already bound to a mode and frame-buffer
 *        config.
 *
 * @param mode    Active display mode.
 * @param config  Frame-buffer configuration reported by the accelerant.
 */
AccelerantBuffer::AccelerantBuffer(const display_mode& mode,
		const frame_buffer_config& config)
	: fDisplayModeSet(false),
	  fFrameBufferConfigSet(false),
	  fIsOffscreenBuffer(false)
{
	SetDisplayMode(mode);
	SetFrameBufferConfig(config);
}


/**
 * @brief Copy constructor that can promote the new buffer into the
 *        off-screen view of the same frame buffer.
 *
 * @param other            Source buffer.
 * @param offscreenBuffer  When true, force the new buffer to address the
 *                         off-screen region.
 */
AccelerantBuffer::AccelerantBuffer(const AccelerantBuffer& other,
		bool offscreenBuffer)
	: fDisplayMode(other.fDisplayMode),
	  fFrameBufferConfig(other.fFrameBufferConfig),
	  fDisplayModeSet(other.fDisplayModeSet),
	  fFrameBufferConfigSet(other.fFrameBufferConfigSet),
	  fIsOffscreenBuffer(other.fIsOffscreenBuffer || offscreenBuffer)
{
}


/**
 * @brief Destructor; nothing dynamic to release.
 */
AccelerantBuffer::~AccelerantBuffer()
{
}


/**
 * @brief Reports whether both the mode and the frame-buffer config are
 *        installed.
 *
 * @return     B_OK once both have been set, B_NO_INIT otherwise.
 * @retval B_OK       Buffer is fully initialised.
 * @retval B_NO_INIT  SetDisplayMode() or SetFrameBufferConfig() not yet
 *                    called.
 */
status_t
AccelerantBuffer::InitCheck() const
{
	if (fDisplayModeSet && fFrameBufferConfigSet)
		return B_OK;

	return B_NO_INIT;
}


/**
 * @brief Returns the colour space of the bound mode.
 *
 * @return     The display mode's colour space, or B_NO_COLOR_SPACE if the
 *             buffer is not yet initialised.
 */
color_space
AccelerantBuffer::ColorSpace() const
{
	if (InitCheck() == B_OK)
		return (color_space)fDisplayMode.space;

	return B_NO_COLOR_SPACE;
}


/**
 * @brief Returns a pointer to the first pixel of the buffer.
 *
 * For off-screen views the pointer is offset past the visible scan lines
 * so the caller addresses the secondary region the card places below the
 * displayed pixels.
 *
 * @return     Pointer to pixel memory, or NULL when not initialised.
 */
void*
AccelerantBuffer::Bits() const
{
	if (InitCheck() != B_OK)
		return NULL;

	uint8* bits = (uint8*)fFrameBufferConfig.frame_buffer;

	if (fIsOffscreenBuffer)
		bits += fDisplayMode.virtual_height * fFrameBufferConfig.bytes_per_row;

	return bits;
}


/**
 * @brief Returns the row stride in bytes for the bound frame buffer.
 *
 * @return     Bytes per row, or 0 when not initialised.
 */
uint32
AccelerantBuffer::BytesPerRow() const
{
	if (InitCheck() == B_OK)
		return fFrameBufferConfig.bytes_per_row;

	return 0;
}


/**
 * @brief Returns the buffer width in pixels.
 *
 * @return     virtual_width from the bound display mode, or 0 when not
 *             initialised.
 */
uint32
AccelerantBuffer::Width() const
{
	if (InitCheck() == B_OK)
		return fDisplayMode.virtual_width;

	return 0;
}


/**
 * @brief Returns the buffer height in scan lines.
 *
 * @return     virtual_height from the bound display mode, or 0 when not
 *             initialised.
 */
uint32
AccelerantBuffer::Height() const
{
	if (InitCheck() == B_OK)
		return fDisplayMode.virtual_height;

	return 0;
}


/**
 * @brief Installs the display mode and marks that half of the
 *        initialisation done.
 *
 * @param mode  New display mode.
 */
void
AccelerantBuffer::SetDisplayMode(const display_mode& mode)
{
	fDisplayMode = mode;
	fDisplayModeSet = true;
}


/**
 * @brief Installs the frame-buffer config and marks the other half of
 *        the initialisation done.
 *
 * @param config  Frame-buffer configuration reported by the accelerant.
 */
void
AccelerantBuffer::SetFrameBufferConfig(const frame_buffer_config& config)
{
	fFrameBufferConfig = config;
	fFrameBufferConfigSet = true;
}


/**
 * @brief Toggles whether Bits() returns the visible or off-screen view.
 *
 * @param offscreenBuffer  true to address the off-screen back buffer,
 *                         false for the visible region.
 */
void
AccelerantBuffer::SetOffscreenBuffer(bool offscreenBuffer)
{
	fIsOffscreenBuffer = offscreenBuffer;
}
