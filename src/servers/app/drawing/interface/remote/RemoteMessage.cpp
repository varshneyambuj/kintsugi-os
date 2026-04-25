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
 *   Copyright 2009-2019, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Lotz <mmlr@mlotz.ch>
 */


/**
 * @file RemoteMessage.cpp
 * @brief Out-of-line encoders / decoders for the RP_* remote-display
 *        protocol: bitmap, font, draw state, gradient, transform, view
 *        state, line-array entry, and the frame-header machinery driven
 *        by NextMessage().
 *
 * Encoders all use Add() / _MakeSpace() under the hood; decoders read
 * directly from the source ring buffer and decrement fDataLeft. The
 * server-only and client-only variants are guarded by CLIENT_COMPILE
 * because the bitmap encoding differs between the two sides.
 */


#include "RemoteMessage.h"

#ifndef CLIENT_COMPILE
#include "DrawState.h"
#include "ServerBitmap.h"
#include "ServerCursor.h"
#endif

#include <Bitmap.h>
#include <Font.h>
#include <View.h>

#include <Gradient.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <GradientRadialFocus.h>
#include <GradientDiamond.h>
#include <GradientConic.h>

#include <new>


#ifdef CLIENT_COMPILE
#define TRACE_ALWAYS(x...)		printf("RemoteMessage: " x)
#else
#define TRACE_ALWAYS(x...)		debug_printf("RemoteMessage: " x)
#endif

#define TRACE(x...)				/*TRACE_ALWAYS(x)*/
#define TRACE_ERROR(x...)		TRACE_ALWAYS(x)


/**
 * @brief Advances the decoder to the next inbound frame.
 *
 * Discards any unconsumed bytes from the previous frame, reads the uint16
 * opcode and the uint32 length field, validates the length includes the
 * header, and stores the payload size in fDataLeft so subsequent Read()
 * calls cannot run past the frame.
 *
 * @param code  Output, set to the opcode of the new frame.
 * @return      B_OK on success, B_ERROR on a malformed length, or the
 *              negative ring-read error code.
 */
status_t
RemoteMessage::NextMessage(uint16& code)
{
	if (fDataLeft > 0) {
		// discard remainder of message
		int32 readSize = fSource->Read(NULL, fDataLeft);
		if (readSize < 0) {
			TRACE_ERROR("failed to read from source: %s\n", strerror(readSize));
			return readSize;
		}
	}

	static const uint32 kHeaderSize = sizeof(uint16) + sizeof(uint32);

	fDataLeft = kHeaderSize;
	status_t result = Read(code);
	if (result != B_OK) {
		TRACE_ERROR("failed to read message code: %s\n", strerror(result));
		return result;
	}

	uint32 dataLeft;
	result = Read(dataLeft);
	if (result != B_OK) {
		TRACE_ERROR("failed to read message length: %s\n", strerror(result));
		return result;
	}

	if (dataLeft < kHeaderSize) {
		TRACE_ERROR("message claims %" B_PRIu32 " bytes, needed at least %"
			B_PRIu32 " for the header\n", dataLeft, kHeaderSize);
		return B_ERROR;
	}

	fDataLeft = dataLeft - kHeaderSize;
	fCode = code;
	return B_OK;
}


/**
 * @brief Discards everything staged since the last Start() / Flush().
 *
 * Used when the encoder has buffered some payload but the higher-level
 * draw operation has been aborted before flushing.
 */
void
RemoteMessage::Cancel()
{
	fAvailable += fWriteIndex;
	fWriteIndex = 0;
}


#ifndef CLIENT_COMPILE
/**
 * @brief Encodes a ServerBitmap as width / height / stride / colour space /
 *        flags / payload (server-side encoder).
 *
 * @param bitmap   Bitmap to serialise.
 * @param minimal  When true, omits the colour-space and flags fields for
 *                 frames that already pin them out-of-band.
 */
void
RemoteMessage::AddBitmap(const ServerBitmap& bitmap, bool minimal)
{
	Add(bitmap.Width());
	Add(bitmap.Height());
	Add(bitmap.BytesPerRow());

	if (!minimal) {
		Add(bitmap.ColorSpace());
		Add(bitmap.Flags());
	}

	uint32 bitsLength = bitmap.BitsLength();
	Add(bitsLength);

	if (!_MakeSpace(bitsLength))
		return;

	memcpy(fBuffer + fWriteIndex, bitmap.Bits(), bitsLength);
	fWriteIndex += bitsLength;
	fAvailable -= bitsLength;
}


/**
 * @brief Encodes the visible state of a ServerFont (direction, encoding,
 *        flags, spacing, shear, rotation, false-bold width, size, face,
 *        family-and-style cookie).
 *
 * @param font  Font whose state is serialised.
 */
void
RemoteMessage::AddFont(const ServerFont& font)
{
	Add((uint8)font.Direction());
	Add((uint8)font.Encoding());
	Add(font.Flags());
	Add((uint8)font.Spacing());
	Add(font.Shear());
	Add(font.Rotation());
	Add(font.FalseBoldWidth());
	Add(font.Size());
	Add(font.Face());
	Add(font.GetFamilyAndStyle());
}


/**
 * @brief Encodes the drawing parameters of a DrawState (pen, blending,
 *        pattern, line caps/joins, miter limit, high/low colours).
 *
 * @param drawState  State to serialise; subpixel precision and pattern are
 *                   embedded inline.
 */
void
RemoteMessage::AddDrawState(const DrawState& drawState)
{
	Add(drawState.PenSize());
	Add(drawState.SubPixelPrecise());
	Add(drawState.GetDrawingMode());
	Add(drawState.AlphaSrcMode());
	Add(drawState.AlphaFncMode());
	AddPattern(drawState.GetPattern());
	Add(drawState.LineCapMode());
	Add(drawState.LineJoinMode());
	Add(drawState.MiterLimit());
	Add(drawState.HighColor());
	Add(drawState.LowColor());
}


/**
 * @brief Encodes one entry of a BView line array: start, end, colour.
 *
 * @param line  Line entry to serialise.
 */
void
RemoteMessage::AddArrayLine(const ViewLineArrayInfo& line)
{
	Add(line.startPoint);
	Add(line.endPoint);
	Add(line.color);
}


/**
 * @brief Encodes a ServerCursor as hotspot followed by its bitmap.
 *
 * @param cursor  Cursor to serialise.
 */
void
RemoteMessage::AddCursor(const ServerCursor& cursor)
{
	Add(cursor.GetHotSpot());
	AddBitmap(cursor);
}


/**
 * @brief Encodes the 64-bit pattern of a Pattern.
 *
 * @param pattern  Pattern to serialise.
 */
void
RemoteMessage::AddPattern(const Pattern& pattern)
{
	Add(pattern.GetPattern());
}

#else // !CLIENT_COMPILE

/**
 * @brief Encodes a BBitmap as width / height / stride / colour space /
 *        flags / payload (client-side encoder).
 *
 * @param bitmap  Bitmap to serialise.
 */
void
RemoteMessage::AddBitmap(const BBitmap& bitmap)
{
	BRect bounds = bitmap.Bounds();
	Add(bounds.IntegerWidth() + 1);
	Add(bounds.IntegerHeight() + 1);
	Add(bitmap.BytesPerRow());
	Add((uint32)bitmap.ColorSpace());
	Add(bitmap.Flags());

	uint32 bitsLength = bitmap.BitsLength();
	Add(bitsLength);

	if (!_MakeSpace(bitsLength))
		return;

	memcpy(fBuffer + fWriteIndex, bitmap.Bits(), bitsLength);
	fWriteIndex += bitsLength;
	fAvailable -= bitsLength;
}
#endif // !CLIENT_COMPILE


/**
 * @brief Encodes a BGradient: type tag, type-specific geometry, then a
 *        sequence of (colour, offset) stops.
 *
 * Linear / radial / radial-focus / diamond / conic gradients each
 * contribute their own geometry payload after the type tag.
 *
 * @param gradient  Gradient to serialise.
 * @note  Silently bails out if a downcast fails; the receiver will then
 *        observe a truncated frame.
 */
void
RemoteMessage::AddGradient(const BGradient& gradient)
{
	Add((uint32)gradient.GetType());

	switch (gradient.GetType()) {
		case BGradient::TYPE_NONE:
			break;

		case BGradient::TYPE_LINEAR:
		{
			const BGradientLinear* linear
				= dynamic_cast<const BGradientLinear *>(&gradient);
			if (linear == NULL)
				return;

			Add(linear->Start());
			Add(linear->End());
			break;
		}

		case BGradient::TYPE_RADIAL:
		{
			const BGradientRadial* radial
				= dynamic_cast<const BGradientRadial *>(&gradient);
			if (radial == NULL)
				return;

			Add(radial->Center());
			Add(radial->Radius());
			break;
		}

		case BGradient::TYPE_RADIAL_FOCUS:
		{
			const BGradientRadialFocus* radialFocus
				= dynamic_cast<const BGradientRadialFocus *>(&gradient);
			if (radialFocus == NULL)
				return;

			Add(radialFocus->Center());
			Add(radialFocus->Focal());
			Add(radialFocus->Radius());
			break;
		}

		case BGradient::TYPE_DIAMOND:
		{
			const BGradientDiamond* diamond
				= dynamic_cast<const BGradientDiamond *>(&gradient);
			if (diamond == NULL)
				return;

			Add(diamond->Center());
			break;
		}

		case BGradient::TYPE_CONIC:
		{
			const BGradientConic* conic
				= dynamic_cast<const BGradientConic *>(&gradient);
			if (conic == NULL)
				return;

			Add(conic->Center());
			Add(conic->Angle());
			break;
		}
	}

	int32 stopCount = gradient.CountColorStops();
	Add(stopCount);

	for (int32 i = 0; i < stopCount; i++) {
		BGradient::ColorStop* stop = gradient.ColorStopAt(i);
		if (stop == NULL)
			return;

		Add(stop->color);
		Add(stop->offset);
	}
}


/**
 * @brief Encodes a BAffineTransform.
 *
 * Identity transforms are encoded as a single bool to save bytes; non-
 * identity transforms emit all six matrix entries.
 *
 * @param transform  Transform to serialise.
 */
void
RemoteMessage::AddTransform(const BAffineTransform& transform)
{
	bool isIdentity = transform.IsIdentity();
	Add(isIdentity);

	if (isIdentity)
		return;

	Add(transform.sx);
	Add(transform.shy);
	Add(transform.shx);
	Add(transform.sy);
	Add(transform.tx);
	Add(transform.ty);
}


/**
 * @brief Reads a length-prefixed string from the inbound ring.
 *
 * Allocates one extra byte for a trailing NUL.
 *
 * @param _string  Output, receives the malloc'd string; caller frees.
 * @param _length  Output, populated with the string length in bytes
 *                 (excluding the appended NUL).
 * @return         B_OK on success, B_ERROR if the declared length runs
 *                 past the frame, B_NO_MEMORY on allocation failure, or
 *                 the negative ring-read error code.
 */
status_t
RemoteMessage::ReadString(char** _string, size_t& _length)
{
	uint32 length;
	status_t result = Read(length);
	if (result != B_OK)
		return result;

	if (length > fDataLeft)
		return B_ERROR;

	char *string = (char *)malloc(length + 1);
	if (string == NULL)
		return B_NO_MEMORY;

	int32 readSize = fSource->Read(string, length);
	if (readSize < 0) {
		free(string);
		return readSize;
	}

	if ((uint32)readSize != length) {
		free(string);
		return B_ERROR;
	}

	fDataLeft -= readSize;

	string[length] = 0;
	*_string = string;
	_length = length;
	return B_OK;
}


/**
 * @brief Decodes a BBitmap previously encoded by AddBitmap().
 *
 * On the server side (CLIENT_COMPILE undefined) the decoded bitmap is
 * forced to B_BITMAP_NO_SERVER_LINK so it does not try to allocate a
 * shared area.
 *
 * @param _bitmap     Output, receives a heap-allocated BBitmap; caller owns.
 * @param minimal     true if the encoder emitted the minimal layout (no
 *                    colour space or flags fields).
 * @param colorSpace  Colour space to use when @a minimal is true.
 * @param flags       Bitmap flags to use when @a minimal is true (server
 *                    side overrides this).
 * @return            B_OK on success, B_ERROR on inconsistent length,
 *                    B_NO_MEMORY on allocation failure, or the negative
 *                    ring-read error code.
 */
status_t
RemoteMessage::ReadBitmap(BBitmap** _bitmap, bool minimal,
	color_space colorSpace, uint32 flags)
{
	uint32 bitsLength;
	int32 width, height, bytesPerRow;

	Read(width);
	Read(height);
	Read(bytesPerRow);

	if (!minimal) {
		Read(colorSpace);
		Read(flags);
	}

	Read(bitsLength);

	if (bitsLength > fDataLeft)
		return B_ERROR;

#ifndef CLIENT_COMPILE
	flags = B_BITMAP_NO_SERVER_LINK;
#endif

	BBitmap *bitmap = new(std::nothrow) BBitmap(
		BRect(0, 0, width - 1, height - 1), flags, colorSpace, bytesPerRow);
	if (bitmap == NULL)
		return B_NO_MEMORY;

	status_t result = bitmap->InitCheck();
	if (result != B_OK) {
		delete bitmap;
		return result;
	}

	if (bitmap->BitsLength() < (int32)bitsLength) {
		delete bitmap;
		return B_ERROR;
	}

	int32 readSize = fSource->Read(bitmap->Bits(), bitsLength);
	if ((uint32)readSize != bitsLength) {
		delete bitmap;
		return readSize < 0 ? readSize : B_ERROR;
	}

	fDataLeft -= readSize;
	*_bitmap = bitmap;
	return B_OK;
}


/**
 * @brief Decodes the font state encoded by AddFont() and applies it to
 *        @a font.
 *
 * @param font  Output font; setters are called in the order required to
 *              avoid clobbering family-and-style.
 * @return      B_OK on success, otherwise the first underlying read
 *              error.
 */
status_t
RemoteMessage::ReadFontState(BFont& font)
{
	uint8 direction;
	uint8 encoding;
	uint8 spacing;
	uint16 face;
	uint32 flags, familyAndStyle;
	float falseBoldWidth, rotation, shear, size;

	Read(direction);
	Read(encoding);
	Read(flags);
	Read(spacing);
	Read(shear);
	Read(rotation);
	Read(falseBoldWidth);
	Read(size);
	Read(face);
	status_t result = Read(familyAndStyle);
	if (result != B_OK)
		return result;

	font.SetFamilyAndStyle(familyAndStyle);
	font.SetEncoding(encoding);
	font.SetFlags(flags);
	font.SetSpacing(spacing);
	font.SetShear(shear);
	font.SetRotation(rotation);
	font.SetFalseBoldWidth(falseBoldWidth);
	font.SetSize(size);
	font.SetFace(face);
	return B_OK;
}


/**
 * @brief Decodes the view-side draw state encoded by AddDrawState() and
 *        applies it to @a view, returning the embedded pattern via
 *        @a pattern.
 *
 * Subpixel precision is reflected by setting or clearing the
 * B_SUBPIXEL_PRECISE view flag.
 *
 * @param view     View whose state is updated.
 * @param pattern  Output pattern recovered from the frame.
 * @return         B_OK on success, otherwise the first underlying read
 *                 error.
 */
status_t
RemoteMessage::ReadViewState(BView& view, ::pattern& pattern)
{
	bool subPixelPrecise;
	float penSize, miterLimit;
	drawing_mode drawingMode;
	source_alpha sourceAlpha;
	alpha_function alphaFunction;
	cap_mode capMode;
	join_mode joinMode;
	rgb_color highColor, lowColor;

	Read(penSize);
	Read(subPixelPrecise);
	Read(drawingMode);
	Read(sourceAlpha);
	Read(alphaFunction);
	Read(pattern);
	Read(capMode);
	Read(joinMode);
	Read(miterLimit);
	Read(highColor);
	status_t result = Read(lowColor);
	if (result != B_OK)
		return result;

	uint32 flags = view.Flags() & ~B_SUBPIXEL_PRECISE;
	view.SetFlags(flags | (subPixelPrecise ? B_SUBPIXEL_PRECISE : 0));
	view.SetPenSize(penSize);
	view.SetDrawingMode(drawingMode);
	view.SetBlendingMode(sourceAlpha, alphaFunction);
	view.SetLineMode(capMode, joinMode, miterLimit);
	view.SetHighColor(highColor);
	view.SetLowColor(lowColor);
	return B_OK;
}


/**
 * @brief Decodes a BGradient previously encoded by AddGradient().
 *
 * Allocates the concrete subtype dictated by the type tag, populates its
 * geometry, then appends every (colour, offset) stop.
 *
 * @param _gradient  Output, receives a heap-allocated BGradient; caller
 *                   owns.
 * @return           B_OK on success, B_NO_MEMORY when the subtype could
 *                   not be allocated, or the first underlying read error.
 */
status_t
RemoteMessage::ReadGradient(BGradient** _gradient)
{
	BGradient::Type type;
	Read(type);

	BGradient *gradient = NULL;
	switch (type) {
		case BGradient::TYPE_NONE:
			break;

		case BGradient::TYPE_LINEAR:
		{
			BPoint start, end;

			Read(start);
			Read(end);

			gradient = new(std::nothrow) BGradientLinear(start, end);
			break;
		}

		case BGradient::TYPE_RADIAL:
		{
			BPoint center;
			float radius;

			Read(center);
			Read(radius);

			gradient = new(std::nothrow) BGradientRadial(center, radius);
			break;
		}

		case BGradient::TYPE_RADIAL_FOCUS:
		{
			BPoint center, focal;
			float radius;

			Read(center);
			Read(focal);
			Read(radius);

			gradient = new(std::nothrow) BGradientRadialFocus(center, radius,
				focal);
			break;
		}

		case BGradient::TYPE_DIAMOND:
		{
			BPoint center;

			Read(center);

			gradient = new(std::nothrow) BGradientDiamond(center);
			break;
		}

		case BGradient::TYPE_CONIC:
		{
			BPoint center;
			float angle;

			Read(center);
			Read(angle);

			gradient = new(std::nothrow) BGradientConic(center, angle);
			break;
		}
	}

	if (gradient == NULL)
		return B_NO_MEMORY;

	int32 stopCount;
	status_t result = Read(stopCount);
	if (result != B_OK) {
		delete gradient;
		return result;
	}

	for (int32 i = 0; i < stopCount; i++) {
		rgb_color color;
		float offset;

		Read(color);
		result = Read(offset);
		if (result != B_OK) {
			delete gradient;
			return result;
		}

		gradient->AddColor(color, offset);
	}

	*_gradient = gradient;
	return B_OK;
}


/**
 * @brief Decodes a BAffineTransform previously encoded by AddTransform().
 *
 * Identity-encoded transforms reset @a transform to the identity matrix.
 *
 * @param transform  Output transform.
 * @return           B_OK on success, otherwise the first underlying read
 *                   error.
 */
status_t
RemoteMessage::ReadTransform(BAffineTransform& transform)
{
	bool isIdentity;
	status_t result = Read(isIdentity);
	if (result != B_OK)
		return result;

	if (isIdentity) {
		transform = BAffineTransform();
		return B_OK;
	}

	Read(transform.sx);
	Read(transform.shy);
	Read(transform.shx);
	Read(transform.sy);
	Read(transform.tx);
	return Read(transform.ty);
}


/**
 * @brief Decodes one BView line-array entry: start, end, colour.
 *
 * @param startPoint  Output start point.
 * @param endPoint    Output end point.
 * @param color       Output colour.
 * @return            B_OK on success, otherwise the first underlying read
 *                    error.
 */
status_t
RemoteMessage::ReadArrayLine(BPoint& startPoint, BPoint& endPoint,
	rgb_color& color)
{
	Read(startPoint);
	Read(endPoint);
	return Read(color);
}
