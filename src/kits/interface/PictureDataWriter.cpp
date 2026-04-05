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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2006-2018 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stefano Ceccherini, stefano.ceccherini@gmail.com
 *       Julian Harnath, <julian.harnath@rwth-achen.de>
 *       Stephan Aßmus <superstippi@gmx.de>
 */


/**
 * @file PictureDataWriter.cpp
 * @brief Implementation of PictureDataWriter, the serializer for BPicture data
 *
 * PictureDataWriter encodes drawing operations into the binary stream format used
 * by BPicture. It is used by the BView drawing implementation to record commands
 * into an active picture.
 *
 * @see BPicture, BView
 */


#include <PictureDataWriter.h>

#include <stdio.h>
#include <string.h>

#include <DataIO.h>
#include <Gradient.h>
#include <Point.h>
#include <Rect.h>
#include <Region.h>

#include <PictureProtocol.h>

/** @brief Throws @a error as a status_t exception, aborting the current operation. */
#define THROW_ERROR(error) throw (status_t)(error)


// TODO: Review writing of strings. AFAIK in the picture data format
// They are not supposed to be NULL terminated
// (at least, it's not mandatory) so we should write their size too.

/**
 * @brief Default constructor; creates an uninitialized writer with no target stream.
 *
 * Call SetTo() before using any Write* methods, otherwise they will return
 * B_NO_INIT.
 */
PictureDataWriter::PictureDataWriter()
	:
	fData(NULL)
{
}


/**
 * @brief Constructs the writer and attaches it to the given data stream.
 *
 * @param data The BPositionIO stream that will receive serialized picture
 *             opcodes. Must remain valid for the lifetime of this object.
 */
PictureDataWriter::PictureDataWriter(BPositionIO* data)
	:
	fData(data)
{
}


/**
 * @brief Destructor.
 *
 * Releases no stream ownership; the caller remains responsible for the
 * lifetime of the BPositionIO passed to the constructor or SetTo().
 */
PictureDataWriter::~PictureDataWriter()
{
}


/**
 * @brief Attaches the writer to a new target stream, replacing any previous one.
 *
 * @param data The BPositionIO stream to write picture data into. Must not be NULL.
 * @return B_OK on success, or B_BAD_VALUE if @a data is NULL.
 */
status_t
PictureDataWriter::SetTo(BPositionIO* data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	fData = data;

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_ORIGIN opcode that shifts the drawing origin.
 *
 * @param point The new origin point, relative to the current coordinate system.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetOrigin(const BPoint& point)
{
	try {
		BeginOp(B_PIC_SET_ORIGIN);
		Write<BPoint>(point);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records an invert-rectangle operation using B_OP_INVERT drawing mode.
 *
 * Pushes the current drawing state, switches to B_OP_INVERT, emits a
 * B_PIC_FILL_RECT opcode for @a rect, then restores the prior state.
 *
 * @param rect The rectangle whose pixels are to be inverted.
 * @return B_OK on success, or an error code if any write step fails.
 */
status_t
PictureDataWriter::WriteInvertRect(const BRect& rect)
{
	try {
		WritePushState();
		WriteSetDrawingMode(B_OP_INVERT);

		BeginOp(B_PIC_FILL_RECT);
		Write<BRect>(rect);
		EndOp();

		WritePopState();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_DRAWING_MODE opcode that sets the drawing mode.
 *
 * @param mode The drawing mode to apply to subsequent drawing operations.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetDrawingMode(const drawing_mode& mode)
{
	try {
		BeginOp(B_PIC_SET_DRAWING_MODE);
		Write<int16>((int16)mode);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_PEN_LOCATION opcode that repositions the pen.
 *
 * @param point The new pen location in the current coordinate system.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetPenLocation(const BPoint& point)
{
	try {
		BeginOp(B_PIC_SET_PEN_LOCATION);
		Write<BPoint>(point);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_PEN_SIZE opcode that sets the stroke width.
 *
 * @param penSize The pen (stroke) width in pixels.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetPenSize(const float& penSize)
{
	try {
		BeginOp(B_PIC_SET_PEN_SIZE);
		Write<float>(penSize);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_LINE_MODE opcode for line-cap, join, and miter limit.
 *
 * @param cap        The line-cap style (butt, round, or square).
 * @param join       The line-join style (miter, round, or bevel).
 * @param miterLimit The maximum ratio of miter length to stroke width before
 *                   a miter join is beveled.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetLineMode(const cap_mode& cap, const join_mode& join,
	const float& miterLimit)
{
	try {
		BeginOp(B_PIC_SET_LINE_MODE);
		Write<int16>((int16)cap);
		Write<int16>((int16)join);
		Write<float>(miterLimit);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_FILL_RULE opcode that controls how overlapping
 *        sub-paths are filled.
 *
 * @param fillRule The fill rule constant (e.g. B_EVEN_ODD or B_NONZERO).
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetFillRule(int32 fillRule)
{
	try {
		BeginOp(B_PIC_SET_FILL_RULE);
		Write<int32>(fillRule);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_BLENDING_MODE opcode for alpha compositing.
 *
 * @param srcAlpha  The source-alpha formula (how the source pixel's alpha
 *                  is interpreted).
 * @param alphaFunc The alpha compositing function applied when blending source
 *                  and destination pixels.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetBlendingMode(source_alpha srcAlpha, alpha_function alphaFunc)
{
	try {
		BeginOp(B_PIC_SET_BLENDING_MODE);
		Write<int16>(srcAlpha);
		Write<int16>(alphaFunc);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_SCALE opcode that sets a uniform scaling factor.
 *
 * @param scale The uniform scale factor applied to subsequent drawing operations.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetScale(const float& scale)
{
	try {
		BeginOp(B_PIC_SET_SCALE);
		Write<float>(scale);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_TRANSFORM opcode that replaces the current
 *        affine transformation matrix.
 *
 * The six elements of @a transform (sx, shy, shx, sy, tx, ty) are written
 * as consecutive doubles.
 *
 * @param transform The affine transformation to record.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetTransform(const BAffineTransform& transform)
{
	try {
		BeginOp(B_PIC_SET_TRANSFORM);
		WriteData(&transform.sx, 6 * sizeof(double));
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_AFFINE_TRANSLATE opcode that appends a translation.
 *
 * @param x Horizontal translation distance in the current coordinate system.
 * @param y Vertical translation distance in the current coordinate system.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteTranslateBy(double x, double y)
{
	try {
		BeginOp(B_PIC_AFFINE_TRANSLATE);
		Write<double>(x);
		Write<double>(y);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_AFFINE_SCALE opcode that appends a non-uniform scale.
 *
 * @param x Horizontal scale factor.
 * @param y Vertical scale factor.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteScaleBy(double x, double y)
{
	try {
		BeginOp(B_PIC_AFFINE_SCALE);
		Write<double>(x);
		Write<double>(y);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_AFFINE_ROTATE opcode that appends a rotation.
 *
 * @param angleRadians The rotation angle in radians, measured counter-clockwise.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteRotateBy(double angleRadians)
{
	try {
		BeginOp(B_PIC_AFFINE_ROTATE);
		Write<double>(angleRadians);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_STIPLE_PATTERN opcode that sets the fill/stroke pattern.
 *
 * @param pattern The 8-byte stipple pattern to apply to subsequent drawing operations.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetPattern(const ::pattern& pattern)
{
	try {
		BeginOp(B_PIC_SET_STIPLE_PATTERN);
		Write< ::pattern>(pattern);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_CLIP_TO_PICTURE opcode that clips the drawing to a
 *        previously recorded picture.
 *
 * @param pictureToken The token identifying the picture to use as the clip mask.
 * @param origin       The offset applied to the picture clip shape.
 * @param inverse      If true, the clip is inverted (drawing occurs outside the picture).
 * @return B_OK on success, or an error code if the write fails.
 * @note Compatibility with the R5 BPicture clip format has not been confirmed.
 */
status_t
PictureDataWriter::WriteClipToPicture(int32 pictureToken,
						const BPoint& origin, bool inverse)
{
	// TODO: I don't know if it's compatible with R5's BPicture version
	try {
		BeginOp(B_PIC_CLIP_TO_PICTURE);
		Write<int32>(pictureToken);
		Write<BPoint>(origin);
		Write<bool>(inverse);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_CLIPPING_RECTS opcode that constrains drawing to
 *        a region.
 *
 * Writes the bounding rectangle of @a region followed by each individual
 * clipping rectangle it contains.
 *
 * @param region The clipping region; its rectangles are serialized as
 *               clipping_rect values.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetClipping(const BRegion& region)
{
	try {
		BeginOp(B_PIC_SET_CLIPPING_RECTS);
		Write<clipping_rect>(region.FrameInt());
		const int32 numRects = region.CountRects();
		for (int32 i = 0; i < numRects; i++)
			Write<clipping_rect>(region.RectAtInt(i));

		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_CLEAR_CLIPPING_RECTS opcode that removes all clipping.
 *
 * After this opcode is played back, drawing is unrestricted by any previously
 * set clipping region.
 *
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteClearClipping()
{
	try {
		BeginOp(B_PIC_CLEAR_CLIPPING_RECTS);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_FORE_COLOR opcode that sets the high (foreground) color.
 *
 * @param color The RGBA color to use as the high color for subsequent operations.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetHighColor(const rgb_color& color)
{
	try {
		BeginOp(B_PIC_SET_FORE_COLOR);
		Write<rgb_color>(color);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_BACK_COLOR opcode that sets the low (background) color.
 *
 * @param color The RGBA color to use as the low color for subsequent operations.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetLowColor(const rgb_color& color)
{
	try {
		BeginOp(B_PIC_SET_BACK_COLOR);
		Write<rgb_color>(color);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a fill or stroke rectangle opcode.
 *
 * Emits either B_PIC_FILL_RECT or B_PIC_STROKE_RECT depending on @a fill.
 *
 * @param rect The rectangle to draw.
 * @param fill If true, the rectangle is filled; otherwise it is stroked.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteDrawRect(const BRect& rect, const bool& fill)
{
	try {
		BeginOp(fill ? B_PIC_FILL_RECT : B_PIC_STROKE_RECT);
		Write<BRect>(rect);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a fill or stroke rounded-rectangle opcode.
 *
 * Emits either B_PIC_FILL_ROUND_RECT or B_PIC_STROKE_ROUND_RECT depending on
 * @a fill.
 *
 * @param rect   The bounding rectangle of the rounded rectangle.
 * @param radius The x and y radii of the corner arcs.
 * @param fill   If true, the shape is filled; otherwise it is stroked.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteDrawRoundRect(const BRect& rect, const BPoint& radius,
	const bool& fill)
{
	try {
		BeginOp(fill ? B_PIC_FILL_ROUND_RECT : B_PIC_STROKE_ROUND_RECT);
		Write<BRect>(rect);
		Write<BPoint>(radius);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a fill or stroke ellipse opcode.
 *
 * Emits either B_PIC_FILL_ELLIPSE or B_PIC_STROKE_ELLIPSE depending on
 * @a fill.
 *
 * @param rect The bounding rectangle that defines the ellipse.
 * @param fill If true, the ellipse is filled; otherwise it is stroked.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteDrawEllipse(const BRect& rect, const bool& fill)
{
	try {
		BeginOp(fill ? B_PIC_FILL_ELLIPSE : B_PIC_STROKE_ELLIPSE);
		Write<BRect>(rect);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a fill or stroke arc opcode.
 *
 * Emits either B_PIC_FILL_ARC or B_PIC_STROKE_ARC depending on @a fill.
 *
 * @param center     The center point of the ellipse that defines the arc.
 * @param radius     The x and y radii of the ellipse.
 * @param startTheta The starting angle of the arc in degrees.
 * @param arcTheta   The angular span of the arc in degrees.
 * @param fill       If true, the arc is filled as a pie wedge; otherwise it is stroked.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteDrawArc(const BPoint& center, const BPoint& radius,
	const float& startTheta, const float& arcTheta, const bool& fill)
{
	try {
		BeginOp(fill ? B_PIC_FILL_ARC : B_PIC_STROKE_ARC);
		Write<BPoint>(center);
		Write<BPoint>(radius);
		Write<float>(startTheta);
		Write<float>(arcTheta);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a fill or stroke polygon opcode.
 *
 * Emits either B_PIC_FILL_POLYGON or B_PIC_STROKE_POLYGON depending on
 * @a fill.  For stroked polygons, an additional byte encodes whether the
 * last vertex should be connected back to the first.
 *
 * @param numPoints The number of vertices in @a points.
 * @param points    Array of polygon vertices.
 * @param isClosed  For stroke mode only: if true, a closing segment is drawn
 *                  from the last point back to the first.
 * @param fill      If true, the polygon is filled; otherwise it is stroked.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteDrawPolygon(const int32& numPoints, BPoint* points,
	const bool& isClosed, const bool& fill)
{
	try {
		BeginOp(fill ? B_PIC_FILL_POLYGON : B_PIC_STROKE_POLYGON);
		Write<int32>(numPoints);
		for (int32 i = 0; i < numPoints; i++)
			Write<BPoint>(points[i]);

		if (!fill)
			Write<uint8>((uint8)isClosed);

		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a fill or stroke cubic Bezier curve opcode.
 *
 * Emits either B_PIC_FILL_BEZIER or B_PIC_STROKE_BEZIER depending on
 * @a fill. The four control points are written in order.
 *
 * @param points Array of exactly four BPoint values: the start point, two
 *               control points, and the end point.
 * @param fill   If true, the closed Bezier region is filled; otherwise it is
 *               stroked.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteDrawBezier(const BPoint points[4], const bool& fill)
{
	try {
		BeginOp(fill ? B_PIC_FILL_BEZIER : B_PIC_STROKE_BEZIER);
		for (int32 i = 0; i < 4; i++)
			Write<BPoint>(points[i]);

		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_STROKE_LINE opcode that draws a line segment.
 *
 * @param start The starting point of the line.
 * @param end   The ending point of the line.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteStrokeLine(const BPoint& start, const BPoint& end)
{
	try {
		BeginOp(B_PIC_STROKE_LINE);
		Write<BPoint>(start);
		Write<BPoint>(end);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_DRAW_STRING opcode for a string at a specific location.
 *
 * First emits a B_PIC_SET_PEN_LOCATION to position the pen at @a where, then
 * writes the string data together with escapement delta values.
 *
 * @param where       The baseline origin for the text.
 * @param string      The raw character data to render; need not be NUL-terminated
 *                    within the recorded range.
 * @param length      The number of bytes from @a string to write.
 * @param escapement  Per-character spacing adjustments for space and non-space glyphs.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteDrawString(const BPoint& where, const char* string,
	const int32& length, const escapement_delta& escapement)
{
	try {
		BeginOp(B_PIC_SET_PEN_LOCATION);
		Write<BPoint>(where);
		EndOp();

		BeginOp(B_PIC_DRAW_STRING);
		Write<int32>(length);
		WriteData(string, length);
		Write<float>(escapement.space);
		Write<float>(escapement.nonspace);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_DRAW_STRING_LOCATIONS opcode that draws each glyph
 *        at an individual position.
 *
 * @param string        The raw character data to render.
 * @param length        The number of bytes from @a string to write.
 * @param locations     Array of per-glyph baseline origin points.
 * @param locationCount The number of entries in @a locations.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteDrawString(const char* string,
	int32 length, const BPoint* locations, int32 locationCount)
{
	try {
		BeginOp(B_PIC_DRAW_STRING_LOCATIONS);
		Write<int32>(locationCount);
		for (int32 i = 0; i < locationCount; i++) {
			Write<BPoint>(locations[i]);
		}
		Write<int32>(length);
		WriteData(string, length);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a fill or stroke shape opcode defined by a BShape op/point list.
 *
 * Emits either B_PIC_FILL_SHAPE or B_PIC_STROKE_SHAPE depending on @a fill.
 * The shape is encoded as a pair of parallel arrays: an array of uint32 opcodes
 * and an array of BPoint coordinates.
 *
 * @param opCount Number of entries in @a opList.
 * @param opList  Array of BShape opcodes (uint32 each).
 * @param ptCount Number of entries in @a ptList.
 * @param ptList  Array of BPoint coordinates referenced by the opcodes.
 * @param fill    If true, the shape is filled; otherwise it is stroked.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteDrawShape(const int32& opCount, const void* opList,
	const int32& ptCount, const void* ptList, const bool& fill)
{
	try {
		BeginOp(fill ? B_PIC_FILL_SHAPE : B_PIC_STROKE_SHAPE);
		Write<int32>(opCount);
		Write<int32>(ptCount);
		WriteData(opList, opCount * sizeof(uint32));
		WriteData(ptList, ptCount * sizeof(BPoint));
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a fill or stroke gradient rectangle opcode.
 *
 * Emits either B_PIC_FILL_RECT_GRADIENT or B_PIC_STROKE_RECT_GRADIENT and
 * flattens the gradient into the stream immediately after the rect.
 *
 * @param rect     The rectangle to draw.
 * @param gradient The gradient to use for filling or stroking.
 * @param fill     If true, the rectangle is filled; otherwise it is stroked.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteDrawRectGradient(const BRect& rect, const BGradient& gradient, const bool& fill)
{
	try {
		BeginOp(fill ? B_PIC_FILL_RECT_GRADIENT : B_PIC_STROKE_RECT_GRADIENT);
		Write<BRect>(rect);
		gradient.Flatten(fData);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a fill or stroke gradient rounded-rectangle opcode.
 *
 * Emits either B_PIC_FILL_ROUND_RECT_GRADIENT or
 * B_PIC_STROKE_ROUND_RECT_GRADIENT and flattens the gradient into the stream.
 *
 * @param rect     The bounding rectangle of the rounded rectangle.
 * @param radius   The x and y corner radii.
 * @param gradient The gradient to use for filling or stroking.
 * @param fill     If true, the shape is filled; otherwise it is stroked.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteDrawRoundRectGradient(const BRect& rect, const BPoint& radius, const BGradient& gradient,
	const bool& fill)
{
	try {
		BeginOp(fill ? B_PIC_FILL_ROUND_RECT_GRADIENT : B_PIC_STROKE_ROUND_RECT_GRADIENT);
		Write<BRect>(rect);
		Write<BPoint>(radius);
		gradient.Flatten(fData);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a fill or stroke gradient cubic Bezier curve opcode.
 *
 * Emits either B_PIC_FILL_BEZIER_GRADIENT or B_PIC_STROKE_BEZIER_GRADIENT and
 * flattens the gradient into the stream after the four control points.
 *
 * @param points   Array of exactly four control points defining the curve.
 * @param gradient The gradient to use for filling or stroking.
 * @param fill     If true, the closed region is filled; otherwise it is stroked.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteDrawBezierGradient(const BPoint points[4], const BGradient& gradient, const bool& fill)
{
	try {
		BeginOp(fill ? B_PIC_FILL_BEZIER_GRADIENT : B_PIC_STROKE_BEZIER_GRADIENT);
		for (int32 i = 0; i < 4; i++)
			Write<BPoint>(points[i]);

		gradient.Flatten(fData);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a fill or stroke gradient arc opcode.
 *
 * Emits either B_PIC_FILL_ARC_GRADIENT or B_PIC_STROKE_ARC_GRADIENT and
 * flattens the gradient into the stream after the arc parameters.
 *
 * @param center     The center of the ellipse defining the arc.
 * @param radius     The x and y radii of the ellipse.
 * @param startTheta The starting angle of the arc in degrees.
 * @param arcTheta   The angular span of the arc in degrees.
 * @param gradient   The gradient to use for filling or stroking.
 * @param fill       If true, the arc wedge is filled; otherwise it is stroked.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteDrawArcGradient(const BPoint& center, const BPoint& radius,
	const float& startTheta, const float& arcTheta, const BGradient& gradient, const bool& fill)
{
	try {
		BeginOp(fill ? B_PIC_FILL_ARC_GRADIENT : B_PIC_STROKE_ARC_GRADIENT);
		Write<BPoint>(center);
		Write<BPoint>(radius);
		Write<float>(startTheta);
		Write<float>(arcTheta);
		gradient.Flatten(fData);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a fill or stroke gradient ellipse opcode.
 *
 * Emits either B_PIC_FILL_ELLIPSE_GRADIENT or B_PIC_STROKE_ELLIPSE_GRADIENT
 * and flattens the gradient into the stream after the bounding rectangle.
 *
 * @param rect     The bounding rectangle defining the ellipse.
 * @param gradient The gradient to use for filling or stroking.
 * @param fill     If true, the ellipse is filled; otherwise it is stroked.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteDrawEllipseGradient(const BRect& rect, const BGradient& gradient, const bool& fill)
{
	try {
		BeginOp(fill ? B_PIC_FILL_ELLIPSE_GRADIENT : B_PIC_STROKE_ELLIPSE_GRADIENT);
		Write<BRect>(rect);
		gradient.Flatten(fData);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a fill or stroke gradient polygon opcode.
 *
 * Emits either B_PIC_FILL_POLYGON_GRADIENT or B_PIC_STROKE_POLYGON_GRADIENT.
 * For stroked polygons, an extra byte encodes the closed flag before the
 * flattened gradient.
 *
 * @param numPoints The number of vertices in @a points.
 * @param points    Array of polygon vertices.
 * @param isClosed  For stroke mode only: if true, a closing segment is added.
 * @param gradient  The gradient to use for filling or stroking.
 * @param fill      If true, the polygon is filled; otherwise it is stroked.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteDrawPolygonGradient(const int32& numPoints, BPoint* points,
	const bool& isClosed, const BGradient& gradient, const bool& fill)
{
	try {
		BeginOp(fill ? B_PIC_FILL_POLYGON_GRADIENT : B_PIC_STROKE_POLYGON_GRADIENT);
		Write<int32>(numPoints);
		for (int32 i = 0; i < numPoints; i++)
			Write<BPoint>(points[i]);

		if (!fill)
			Write<uint8>((uint8)isClosed);

		gradient.Flatten(fData);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a fill or stroke gradient shape opcode.
 *
 * Emits either B_PIC_FILL_SHAPE_GRADIENT or B_PIC_STROKE_SHAPE_GRADIENT and
 * flattens the gradient into the stream after the op/point arrays.
 *
 * @param opCount  Number of entries in @a opList.
 * @param opList   Array of BShape opcodes (uint32 each).
 * @param ptCount  Number of entries in @a ptList.
 * @param ptList   Array of BPoint coordinates referenced by the opcodes.
 * @param gradient The gradient to use for filling or stroking.
 * @param fill     If true, the shape is filled; otherwise it is stroked.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteDrawShapeGradient(const int32& opCount, const void* opList,
	const int32& ptCount, const void* ptList, const BGradient& gradient, const bool& fill)
{
	try {
		BeginOp(fill ? B_PIC_FILL_SHAPE_GRADIENT : B_PIC_STROKE_SHAPE_GRADIENT);
		Write<int32>(opCount);
		Write<int32>(ptCount);
		WriteData(opList, opCount * sizeof(uint32));
		WriteData(ptList, ptCount * sizeof(BPoint));
		gradient.Flatten(fData);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_STROKE_LINE_GRADIENT opcode that strokes a line with
 *        a gradient.
 *
 * @param start    The starting endpoint of the line.
 * @param end      The ending endpoint of the line.
 * @param gradient The gradient applied along the stroke.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteStrokeLineGradient(const BPoint& start, const BPoint& end,
	const BGradient& gradient)
{
	try {
		BeginOp(B_PIC_STROKE_LINE_GRADIENT);
		Write<BPoint>(start);
		Write<BPoint>(end);
		gradient.Flatten(fData);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_DRAW_PIXELS opcode that blits raw bitmap data.
 *
 * The caller must ensure that @a length equals @a height * @a bytesPerRow;
 * a violation calls debugger() immediately.
 *
 * @param srcRect     The sub-rectangle of the source bitmap to copy.
 * @param dstRect     The destination rectangle in the view's coordinate system.
 * @param width       The width of the full source bitmap in pixels.
 * @param height      The height of the full source bitmap in pixels.
 * @param bytesPerRow The number of bytes per row in the source bitmap.
 * @param colorSpace  The color space constant (B_RGB32, B_RGBA32, etc.).
 * @param flags       Bitmap drawing flags (e.g. B_FILTER_BITMAP_BILINEAR).
 * @param data        Pointer to the raw pixel data buffer.
 * @param length      Total byte length of @a data; must equal height * bytesPerRow.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteDrawBitmap(const BRect& srcRect, const BRect& dstRect,
	const int32& width, const int32& height, const int32& bytesPerRow,
	const int32& colorSpace, const int32& flags, const void* data,
	const int32& length)
{
	if (length != height * bytesPerRow)
		debugger("PictureDataWriter::WriteDrawBitmap: invalid length");
	try {
		BeginOp(B_PIC_DRAW_PIXELS);
		Write<BRect>(srcRect);
		Write<BRect>(dstRect);
		Write<int32>(width);
		Write<int32>(height);
		Write<int32>(bytesPerRow);
		Write<int32>(colorSpace);
		Write<int32>(flags);
		Write<int32>(length);
		WriteData(data, length);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_DRAW_PICTURE opcode that plays back a nested picture.
 *
 * @param where The offset applied to the nested picture's coordinate system.
 * @param token The server-side token that identifies the nested BPicture.
 * @return B_OK on success, or an error code if the write fails.
 * @note The token alone may not be sufficient for archiving or flattening; the
 *       picture data itself is not embedded by this method.
 */
status_t
PictureDataWriter::WriteDrawPicture(const BPoint& where, const int32& token)
{
	// TODO: I'm not sure about this function. I think we need
	// to attach the picture data too.
	// The token won't be sufficient in many cases (for example, when
	// we archive/flatten the picture.
	try {
		BeginOp(B_PIC_DRAW_PICTURE);
		Write<BPoint>(where);
		Write<int32>(token);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_FONT_FAMILY opcode that selects the font family.
 *
 * The family name is written with its length (including the NUL terminator)
 * preceding the string data, matching the BeOS picture format convention.
 *
 * @param family The font family name string.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetFontFamily(const font_family family)
{
	try {
		BeginOp(B_PIC_SET_FONT_FAMILY);
		// BeOS writes string size including terminating null character for some reason.
		uint32 length = strlen(family) + 1;
		Write<uint32>(length);
		WriteData(family, length);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_FONT_STYLE opcode that selects the font style.
 *
 * The style name is written with its length (including the NUL terminator)
 * preceding the string data, matching the BeOS picture format convention.
 *
 * @param style The font style name string (e.g. "Bold", "Italic").
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetFontStyle(const font_style style)
{
	try {
		BeginOp(B_PIC_SET_FONT_STYLE);
		// BeOS writes string size including terminating null character for some reason.
		uint32 length = strlen(style) + 1;
		Write<uint32>(length);
		WriteData(style, length);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_FONT_SPACING opcode that sets character spacing.
 *
 * @param spacing The spacing constant (e.g. B_BITMAP_SPACING, B_STRING_SPACING).
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetFontSpacing(const int32& spacing)
{
	try {
		BeginOp(B_PIC_SET_FONT_SPACING);
		Write<int32>(spacing);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_FONT_SIZE opcode that sets the font point size.
 *
 * @param size The font size in points.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetFontSize(const float& size)
{
	try {
		BeginOp(B_PIC_SET_FONT_SIZE);
		Write<float>(size);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_FONT_ROTATE opcode that sets the text rotation.
 *
 * @param rotation The baseline rotation angle in degrees, measured
 *                 counter-clockwise from the horizontal.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetFontRotation(const float& rotation)
{
	try {
		BeginOp(B_PIC_SET_FONT_ROTATE);
		Write<float>(rotation);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_FONT_ENCODING opcode that selects the character
 *        encoding.
 *
 * @param encoding The encoding constant (e.g. B_UNICODE_UTF8, B_ISO_8859_1).
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetFontEncoding(const int32& encoding)
{
	try {
		BeginOp(B_PIC_SET_FONT_ENCODING);
		Write<int32>(encoding);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_FONT_FLAGS opcode that sets miscellaneous font flags.
 *
 * @param flags A bitmask of font attribute flags (e.g. B_DISABLE_ANTIALIASING).
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetFontFlags(const int32& flags)
{
	try {
		BeginOp(B_PIC_SET_FONT_FLAGS);
		Write<int32>(flags);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_FONT_SHEAR opcode that sets the italic shear angle.
 *
 * @param shear The shear angle in degrees (90 degrees = upright, no shear).
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetFontShear(const float& shear)
{
	try {
		BeginOp(B_PIC_SET_FONT_SHEAR);
		Write<float>(shear);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_SET_FONT_FACE opcode that sets the font face flags.
 *
 * @param face A bitmask of face attributes (e.g. B_BOLD_FACE, B_ITALIC_FACE).
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteSetFontFace(const int32& face)
{
	try {
		BeginOp(B_PIC_SET_FONT_FACE);
		Write<int32>(face);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_PUSH_STATE opcode that saves the current drawing state.
 *
 * The saved state is restored by a matching WritePopState() call during
 * picture playback.
 *
 * @return B_OK on success, or an error code if the write fails.
 * @see WritePopState()
 */
status_t
PictureDataWriter::WritePushState()
{
	try {
		BeginOp(B_PIC_PUSH_STATE);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_POP_STATE opcode that restores the most recently
 *        pushed drawing state.
 *
 * @return B_OK on success, or an error code if the write fails.
 * @see WritePushState()
 */
status_t
PictureDataWriter::WritePopState()
{
	try {
		BeginOp(B_PIC_POP_STATE);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_BLEND_LAYER opcode that composites a rendering layer.
 *
 * @param layer Pointer to the Layer object to blend into the picture output.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteBlendLayer(Layer* layer)
{
	try {
		BeginOp(B_PIC_BLEND_LAYER);
		Write<Layer*>(layer);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_CLIP_TO_RECT opcode that clips drawing to a rectangle.
 *
 * @param rect    The rectangle to use as the clip boundary.
 * @param inverse If true, drawing is clipped to the area outside @a rect.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteClipToRect(const BRect& rect, bool inverse)
{
	try {
		BeginOp(B_PIC_CLIP_TO_RECT);
		Write<bool>(inverse);
		Write<BRect>(rect);
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


/**
 * @brief Records a B_PIC_CLIP_TO_SHAPE opcode that clips drawing to a shape.
 *
 * The shape is encoded as a pair of parallel arrays identical to those used
 * by WriteDrawShape().
 *
 * @param opCount Number of entries in @a opList.
 * @param opList  Array of BShape opcodes (uint32 each).
 * @param ptCount Number of entries in @a ptList.
 * @param ptList  Array of BPoint coordinates referenced by the opcodes.
 * @param inverse If true, drawing is clipped to the area outside the shape.
 * @return B_OK on success, or an error code if the write fails.
 */
status_t
PictureDataWriter::WriteClipToShape(int32 opCount, const void* opList,
	int32 ptCount, const void* ptList, bool inverse)
{
	try {
		BeginOp(B_PIC_CLIP_TO_SHAPE);
		Write<bool>(inverse);
		Write<int32>(opCount);
		Write<int32>(ptCount);
		WriteData(opList, opCount * sizeof(uint32));
		WriteData(ptList, ptCount * sizeof(BPoint));
		EndOp();
	} catch (status_t& status) {
		return status;
	}

	return B_OK;
}


// private

/**
 * @brief Begins a new opcode block by writing the opcode and a placeholder size.
 *
 * Pushes the current stream position onto @c fStack so that EndOp() can later
 * seek back and patch the block's byte count. The size field is initialized to
 * zero and overwritten with the correct value by EndOp().
 *
 * @param op The 16-bit picture opcode constant (e.g. B_PIC_FILL_RECT).
 * @note Throws B_NO_INIT if @c fData is NULL.
 * @see EndOp()
 */
void
PictureDataWriter::BeginOp(const int16& op)
{
	if (fData == NULL)
		THROW_ERROR(B_NO_INIT);

	fStack.push(fData->Position());
	fData->Write(&op, sizeof(op));

	// Init the size of the opcode block to 0
	int32 size = 0;
	fData->Write(&size, sizeof(size));
}


/**
 * @brief Closes the current opcode block and patches its size field.
 *
 * Pops the start position saved by BeginOp() from @c fStack, computes the
 * payload byte count (total bytes written minus the opcode and size header),
 * seeks back to the size field, writes the correct value, then seeks forward
 * to resume normal writing.
 *
 * @note Throws B_NO_INIT if @c fData is NULL.
 * @see BeginOp()
 */
void
PictureDataWriter::EndOp()
{
	if (fData == NULL)
		THROW_ERROR(B_NO_INIT);

	off_t curPos = fData->Position();
	off_t stackPos = fStack.top();
	fStack.pop();

	// The size of the op is calculated like this:
	// current position on the stream minus the position on the stack,
	// minus the space occupied by the op code itself (int16)
	// and the space occupied by the size field (int32)
	int32 size = curPos - stackPos - sizeof(int32) - sizeof(int16);

	// Size was set to 0 in BeginOp()
	// Now we overwrite it with the correct value
	fData->Seek(stackPos + sizeof(int16), SEEK_SET);
	fData->Write(&size, sizeof(size));
	fData->Seek(curPos, SEEK_SET);
}


/**
 * @brief Writes a raw block of bytes to the picture stream.
 *
 * Calls fData->Write() and verifies that the entire buffer was accepted.
 * Throws an error status if the write fails or returns a short count.
 *
 * @param data Pointer to the data buffer to write.
 * @param size Number of bytes to write from @a data.
 * @note Throws the negative error code returned by BPositionIO::Write() on
 *       failure, or B_IO_ERROR if fewer bytes than requested were written.
 */
void
PictureDataWriter::WriteData(const void* data, size_t size)
{
	ssize_t result = fData->Write(data, size);
	if (result < 0)
		THROW_ERROR(result);

	if ((size_t)result != size)
		THROW_ERROR(B_IO_ERROR);
}
