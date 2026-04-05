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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2018 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers (mflerackers@androme.be)
 *       Stefano Ceccherini (stefano.ceccherini@gmail.com)
 *       Marcus Overhagen (marcus@overhagen.de)
 *       Stephan Aßmus <superstippi@gmx.de>
 */


/**
 * @file PicturePlayer.cpp
 * @brief Implementation of PicturePlayer, the replayer for BPicture data
 *
 * PicturePlayer decodes and executes the binary drawing command stream stored in a
 * BPicture, dispatching each opcode to a set of callback functions that perform the
 * actual drawing on a BView or equivalent surface.
 *
 * @see BPicture, BView
 */


#include <PicturePlayer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <AffineTransform.h>
#include <DataIO.h>
#include <Gradient.h>
#include <PictureProtocol.h>
#include <Shape.h>
#include <ShapePrivate.h>

#include <AutoDeleter.h>
#include <StackOrHeapArray.h>


using BPrivate::PicturePlayer;
using BPrivate::PicturePlayerCallbacks;
using BPrivate::picture_player_callbacks_compat;


/**
 * @brief Adapter that bridges the legacy C function-pointer table to the
 *        PicturePlayerCallbacks virtual-method interface.
 *
 * CallbackAdapterPlayer wraps a @c picture_player_callbacks_compat table
 * supplied by older callers and translates each virtual callback dispatch
 * into the corresponding C function-pointer call. This allows PicturePlayer::_Play()
 * to work uniformly against PicturePlayerCallbacks regardless of whether the
 * caller uses the modern virtual-method API or the legacy flat function table.
 *
 * @see PicturePlayer::Play(void**, int32, void*)
 * @see picture_player_callbacks_compat
 */
class CallbackAdapterPlayer : public PicturePlayerCallbacks {
public:
	CallbackAdapterPlayer(void* userData, void** functionTable);

	virtual void MovePenBy(const BPoint& where);
	virtual void StrokeLine(const BPoint& start, const BPoint& end);
	virtual void DrawRect(const BRect& rect, bool fill);
	virtual void DrawRoundRect(const BRect& rect, const BPoint& radii, bool fill);
	virtual void DrawBezier(const BPoint controlPoints[4], bool fill);
	virtual void DrawArc(const BPoint& center, const BPoint& radii, float startTheta,
		float arcTheta, bool fill);
	virtual void DrawEllipse(const BRect& rect, bool fill);
	virtual void DrawPolygon(size_t numPoints, const BPoint points[], bool isClosed, bool fill);
	virtual void DrawShape(const BShape& shape, bool fill);
	virtual void DrawString(const char* string, size_t length, float spaceEscapement,
		float nonSpaceEscapement);
	virtual void DrawPixels(const BRect& source, const BRect& destination, uint32 width,
		uint32 height, size_t bytesPerRow, color_space pixelFormat, uint32 flags, const void* data,
		size_t length);
	virtual void DrawPicture(const BPoint& where, int32 token);
	virtual void SetClippingRects(size_t numRects, const clipping_rect rects[]);
	virtual void ClipToPicture(int32 token, const BPoint& where, bool clipToInverse);
	virtual void PushState();
	virtual void PopState();
	virtual void EnterStateChange();
	virtual void ExitStateChange();
	virtual void EnterFontState();
	virtual void ExitFontState();
	virtual void SetOrigin(const BPoint& origin);
	virtual void SetPenLocation(const BPoint& location);
	virtual void SetDrawingMode(drawing_mode mode);
	virtual void SetLineMode(cap_mode capMode, join_mode joinMode, float miterLimit);
	virtual void SetPenSize(float size);
	virtual void SetForeColor(const rgb_color& color);
	virtual void SetBackColor(const rgb_color& color);
	virtual void SetStipplePattern(const pattern& patter);
	virtual void SetScale(float scale);
	virtual void SetFontFamily(const char* familyName, size_t length);
	virtual void SetFontStyle(const char* styleName, size_t length);
	virtual void SetFontSpacing(uint8 spacing);
	virtual void SetFontSize(float size);
	virtual void SetFontRotation(float rotation);
	virtual void SetFontEncoding(uint8 encoding);
	virtual void SetFontFlags(uint32 flags);
	virtual void SetFontShear(float shear);
	virtual void SetFontFace(uint16 face);
	virtual void SetBlendingMode(source_alpha alphaSourceMode, alpha_function alphaFunctionMode);
	virtual void SetTransform(const BAffineTransform& transform);
	virtual void TranslateBy(double x, double y);
	virtual void ScaleBy(double x, double y);
	virtual void RotateBy(double angleRadians);
	virtual void BlendLayer(Layer* layer);
	virtual void ClipToRect(const BRect& rect, bool inverse);
	virtual void ClipToShape(int32 opCount, const uint32 opList[], int32 ptCount,
		const BPoint ptList[], bool inverse);
	virtual void DrawStringLocations(const char* string, size_t length, const BPoint locations[],
		size_t locationCount);
	virtual void DrawRectGradient(const BRect& rect, BGradient& gradient, bool fill);
	virtual void DrawRoundRectGradient(const BRect& rect, const BPoint& radii, BGradient& gradient,
		bool fill);
	virtual void DrawBezierGradient(const BPoint controlPoints[4], BGradient& gradient, bool fill);
	virtual void DrawArcGradient(const BPoint& center, const BPoint& radii, float startTheta,
		float arcTheta, BGradient& gradient, bool fill);
	virtual void DrawEllipseGradient(const BRect& rect, BGradient& gradient, bool fill);
	virtual void DrawPolygonGradient(size_t numPoints, const BPoint points[], bool isClosed,
		BGradient& gradient, bool fill);
	virtual void DrawShapeGradient(const BShape& shape, BGradient& gradient, bool fill);
	virtual void SetFillRule(int32 fillRule);
	virtual void StrokeLineGradient(const BPoint& start, const BPoint& end, BGradient& gradient);

private:
	/** @brief Opaque user data pointer forwarded to every legacy callback. */
	void* fUserData;
	/** @brief Pointer to the legacy C function-pointer table cast from the caller's void** table. */
	picture_player_callbacks_compat* fCallbacks;
};


/**
 * @brief No-operation placeholder used to pad a short legacy callback table.
 *
 * When a caller supplies a function table with fewer entries than kOpsTableSize,
 * PicturePlayer::Play() fills the missing slots with this function so that
 * unsupported opcodes are silently ignored rather than invoking a NULL pointer.
 */
static void
nop()
{
}


/**
 * @brief Constructs a CallbackAdapterPlayer wrapping a legacy C callback table.
 *
 * @param userData      Opaque pointer passed verbatim as the first argument to
 *                      every function in @a functionTable.
 * @param functionTable Pointer to an array of function pointers cast to
 *                      @c picture_player_callbacks_compat.
 */
CallbackAdapterPlayer::CallbackAdapterPlayer(void* userData, void** functionTable)
	:
	fUserData(userData),
	fCallbacks((picture_player_callbacks_compat*)functionTable)
{
}


/**
 * @brief Forwards a pen-move request to the legacy move_pen_by callback.
 *
 * @param delta The displacement by which the current pen position is moved.
 */
void
CallbackAdapterPlayer::MovePenBy(const BPoint& delta)
{
	fCallbacks->move_pen_by(fUserData, delta);
}


/**
 * @brief Forwards a stroke-line request to the legacy stroke_line callback.
 *
 * @param start The starting point of the line in view coordinates.
 * @param end   The ending point of the line in view coordinates.
 */
void
CallbackAdapterPlayer::StrokeLine(const BPoint& start, const BPoint& end)
{
	fCallbacks->stroke_line(fUserData, start, end);
}


/**
 * @brief Forwards a draw-rectangle request to the appropriate legacy callback.
 *
 * Dispatches to fill_rect when @a fill is @c true, or stroke_rect otherwise.
 *
 * @param rect The rectangle to draw, in view coordinates.
 * @param fill @c true to fill the rectangle; @c false to stroke its outline.
 */
void
CallbackAdapterPlayer::DrawRect(const BRect& rect, bool fill)
{
	if (fill)
		fCallbacks->fill_rect(fUserData, rect);
	else
		fCallbacks->stroke_rect(fUserData, rect);
}


/**
 * @brief Forwards a draw-round-rectangle request to the appropriate legacy callback.
 *
 * Dispatches to fill_round_rect when @a fill is @c true, or stroke_round_rect otherwise.
 *
 * @param rect   The bounding rectangle of the rounded rectangle.
 * @param radii  The x and y corner radii stored as BPoint::x and BPoint::y respectively.
 * @param fill   @c true to fill; @c false to stroke the outline.
 */
void
CallbackAdapterPlayer::DrawRoundRect(const BRect& rect, const BPoint& radii,
	bool fill)
{
	if (fill)
		fCallbacks->fill_round_rect(fUserData, rect, radii);
	else
		fCallbacks->stroke_round_rect(fUserData, rect, radii);
}


/**
 * @brief Forwards a draw-bezier request to the appropriate legacy callback.
 *
 * Copies the four control points into a local array (the legacy callbacks
 * accept a non-const pointer) and dispatches to fill_bezier or stroke_bezier.
 *
 * @param _points Array of exactly four BPoint control points defining the curve.
 * @param fill    @c true to fill the enclosed area; @c false to stroke the curve.
 */
void
CallbackAdapterPlayer::DrawBezier(const BPoint _points[4], bool fill)
{
	BPoint points[4] = { _points[0], _points[1], _points[2], _points[3] };

	if (fill)
		fCallbacks->fill_bezier(fUserData, points);
	else
		fCallbacks->stroke_bezier(fUserData, points);
}


/**
 * @brief Forwards a draw-arc request to the appropriate legacy callback.
 *
 * Dispatches to fill_arc when @a fill is @c true, or stroke_arc otherwise.
 *
 * @param center     The center point of the arc's ellipse.
 * @param radii      The x and y radii of the ellipse.
 * @param startTheta The starting angle of the arc, in degrees.
 * @param arcTheta   The angular extent of the arc, in degrees.
 * @param fill       @c true to fill the arc sector; @c false to stroke the arc.
 */
void
CallbackAdapterPlayer::DrawArc(const BPoint& center, const BPoint& radii,
	float startTheta, float arcTheta, bool fill)
{
	if (fill)
		fCallbacks->fill_arc(fUserData, center, radii, startTheta, arcTheta);
	else
		fCallbacks->stroke_arc(fUserData, center, radii, startTheta, arcTheta);
}


/**
 * @brief Forwards a draw-ellipse request to the appropriate legacy callback.
 *
 * Converts the bounding @a rect to a center+radii representation required by
 * the legacy fill_ellipse / stroke_ellipse callbacks.
 *
 * @param rect The bounding rectangle of the ellipse.
 * @param fill @c true to fill the ellipse; @c false to stroke its outline.
 */
void
CallbackAdapterPlayer::DrawEllipse(const BRect& rect, bool fill)
{
	BPoint radii((rect.Width() + 1) / 2.0f, (rect.Height() + 1) / 2.0f);
	BPoint center = rect.LeftTop() + radii;

	if (fill)
		fCallbacks->fill_ellipse(fUserData, center, radii);
	else
		fCallbacks->stroke_ellipse(fUserData, center, radii);
}


/**
 * @brief Forwards a draw-polygon request to the appropriate legacy callback.
 *
 * Copies the point array into a stack-or-heap buffer (the legacy callbacks
 * accept a non-const pointer) and dispatches to fill_polygon or stroke_polygon.
 *
 * @param numPoints The number of vertices in @a _points.
 * @param _points   Array of polygon vertices in view coordinates.
 * @param isClosed  @c true if the polygon's last vertex is connected back to
 *                  the first; ignored when filling (always treated as closed).
 * @param fill      @c true to fill the polygon; @c false to stroke its outline.
 */
void
CallbackAdapterPlayer::DrawPolygon(size_t numPoints, const BPoint _points[],
	bool isClosed, bool fill)
{

	BStackOrHeapArray<BPoint, 200> points(numPoints);
	if (!points.IsValid())
		return;

	memcpy((void*)points, _points, numPoints * sizeof(BPoint));

	if (fill)
		fCallbacks->fill_polygon(fUserData, numPoints, points, isClosed);
	else
		fCallbacks->stroke_polygon(fUserData, numPoints, points, isClosed);
}


/**
 * @brief Forwards a draw-shape request to the appropriate legacy callback.
 *
 * Dispatches to fill_shape when @a fill is @c true, or stroke_shape otherwise.
 *
 * @param shape The BShape object to draw.
 * @param fill  @c true to fill the shape; @c false to stroke its outline.
 */
void
CallbackAdapterPlayer::DrawShape(const BShape& shape, bool fill)
{
	if (fill)
		fCallbacks->fill_shape(fUserData, &shape);
	else
		fCallbacks->stroke_shape(fUserData, &shape);
}


/**
 * @brief Forwards a draw-string request to the legacy draw_string callback.
 *
 * Duplicates the string with strndup() because the legacy callback receives
 * a null-terminated char* rather than a pointer-plus-length pair.
 *
 * @param _string        The character data to draw (not necessarily null-terminated).
 * @param length         Number of bytes in @a _string to render.
 * @param deltaSpace     Extra escapement added for space characters.
 * @param deltaNonSpace  Extra escapement added for non-space characters.
 */
void
CallbackAdapterPlayer::DrawString(const char* _string, size_t length,
	float deltaSpace, float deltaNonSpace)
{
	char* string = strndup(_string, length);

	fCallbacks->draw_string(fUserData, string, deltaSpace, deltaNonSpace);

	free(string);
}


/**
 * @brief Forwards a draw-pixels request to the legacy draw_pixels callback.
 *
 * Copies the pixel data into a heap buffer because the legacy callback
 * receives a non-const void* and may modify the data.
 *
 * @param src         The source rectangle within the pixel buffer.
 * @param dest        The destination rectangle in view coordinates.
 * @param width       Width of the pixel buffer in pixels.
 * @param height      Height of the pixel buffer in pixels.
 * @param bytesPerRow Number of bytes per row in the pixel buffer.
 * @param pixelFormat The color_space describing the pixel encoding.
 * @param options     Renderer-specific option flags.
 * @param _data       Pointer to the raw pixel data.
 * @param length      Total byte length of @a _data.
 */
void
CallbackAdapterPlayer::DrawPixels(const BRect& src, const BRect& dest, uint32 width,
	uint32 height, size_t bytesPerRow, color_space pixelFormat, uint32 options,
	const void* _data, size_t length)
{
	void* data = malloc(length);
	if (data == NULL)
		return;

	memcpy(data, _data, length);

	fCallbacks->draw_pixels(fUserData, src, dest, width,
			height, bytesPerRow, pixelFormat, options, data);

	free(data);
}


/**
 * @brief Forwards a draw-picture request to the legacy draw_picture callback.
 *
 * @param where  The point in view coordinates at which the nested picture is rendered.
 * @param token  The server-side token identifying the nested BPicture object.
 */
void
CallbackAdapterPlayer::DrawPicture(const BPoint& where, int32 token)
{
	fCallbacks->draw_picture(fUserData, where, token);
}


/**
 * @brief Forwards a set-clipping-rects request to the legacy set_clipping_rects callback.
 *
 * Converts the array of @c clipping_rect (integer) values to an array of BRect
 * (floating-point) objects required by the legacy callback.
 *
 * @param numRects The number of rectangles in @a _rects.
 * @param _rects   Array of integer-coordinate clipping rectangles.
 */
void
CallbackAdapterPlayer::SetClippingRects(size_t numRects, const clipping_rect _rects[])
{
	// This is rather ugly but works for such a trivial class.
	BStackOrHeapArray<BRect, 100> rects(numRects);
	if (!rects.IsValid())
		return;

	for (size_t i = 0; i < numRects; i++) {
		clipping_rect srcRect = _rects[i];
		rects[i] = BRect(srcRect.left, srcRect.top, srcRect.right, srcRect.bottom);
	}

	fCallbacks->set_clipping_rects(fUserData, rects, numRects);
}


/**
 * @brief Forwards a clip-to-picture request to the legacy clip_to_picture callback.
 *
 * @param token         Server-side token identifying the clipping BPicture.
 * @param origin        The origin offset applied when rendering the clipping picture.
 * @param clipToInverse @c true to clip to the area outside the picture shape;
 *                      @c false to clip to the inside.
 */
void
CallbackAdapterPlayer::ClipToPicture(int32 token, const BPoint& origin,
	bool clipToInverse)
{
	fCallbacks->clip_to_picture(fUserData, token, origin, clipToInverse);
}


/**
 * @brief Forwards a push-state request to the legacy push_state callback.
 *
 * Saves the current drawing state onto the state stack.
 */
void
CallbackAdapterPlayer::PushState()
{
	fCallbacks->push_state(fUserData);
}


/**
 * @brief Forwards a pop-state request to the legacy pop_state callback.
 *
 * Restores the most recently pushed drawing state from the state stack.
 */
void
CallbackAdapterPlayer::PopState()
{
	fCallbacks->pop_state(fUserData);
}


/**
 * @brief Forwards an enter-state-change notification to the legacy enter_state_change callback.
 *
 * Called before a block of state-setting opcodes is processed, allowing the
 * rendering back-end to batch or defer state updates.
 */
void
CallbackAdapterPlayer::EnterStateChange()
{
	fCallbacks->enter_state_change(fUserData);
}


/**
 * @brief Forwards an exit-state-change notification to the legacy exit_state_change callback.
 *
 * Called after a block of state-setting opcodes has been processed, signalling
 * the rendering back-end to commit any deferred state updates.
 */
void
CallbackAdapterPlayer::ExitStateChange()
{
	fCallbacks->exit_state_change(fUserData);
}


/**
 * @brief Forwards an enter-font-state notification to the legacy enter_font_state callback.
 *
 * Called before a block of font-setting opcodes is processed.
 */
void
CallbackAdapterPlayer::EnterFontState()
{
	fCallbacks->enter_font_state(fUserData);
}


/**
 * @brief Forwards an exit-font-state notification to the legacy exit_font_state callback.
 *
 * Called after a block of font-setting opcodes has been processed.
 */
void
CallbackAdapterPlayer::ExitFontState()
{
	fCallbacks->exit_font_state(fUserData);
}


/**
 * @brief Forwards a set-origin request to the legacy set_origin callback.
 *
 * @param origin The new coordinate-system origin in the parent coordinate space.
 */
void
CallbackAdapterPlayer::SetOrigin(const BPoint& origin)
{
	fCallbacks->set_origin(fUserData, origin);
}


/**
 * @brief Forwards a set-pen-location request to the legacy set_pen_location callback.
 *
 * @param penLocation The new pen position in view coordinates.
 */
void
CallbackAdapterPlayer::SetPenLocation(const BPoint& penLocation)
{
	fCallbacks->set_pen_location(fUserData, penLocation);
}


/**
 * @brief Forwards a set-drawing-mode request to the legacy set_drawing_mode callback.
 *
 * @param mode The drawing mode (e.g. B_OP_COPY, B_OP_OVER) to apply.
 */
void
CallbackAdapterPlayer::SetDrawingMode(drawing_mode mode)
{
	fCallbacks->set_drawing_mode(fUserData, mode);
}


/**
 * @brief Forwards a set-line-mode request to the legacy set_line_mode callback.
 *
 * @param capMode    The cap style for line endpoints (e.g. B_BUTT_CAP).
 * @param joinMode   The join style for connected line segments (e.g. B_MITER_JOIN).
 * @param miterLimit The miter limit used when @a joinMode is B_MITER_JOIN.
 */
void
CallbackAdapterPlayer::SetLineMode(cap_mode capMode, join_mode joinMode,
	float miterLimit)
{
	fCallbacks->set_line_mode(fUserData, capMode, joinMode, miterLimit);
}


/**
 * @brief Forwards a set-pen-size request to the legacy set_pen_size callback.
 *
 * @param size The new pen (stroke) width in view coordinates.
 */
void
CallbackAdapterPlayer::SetPenSize(float size)
{
	fCallbacks->set_pen_size(fUserData, size);
}


/**
 * @brief Forwards a set-foreground-color request to the legacy set_fore_color callback.
 *
 * @param color The new foreground color used for stroking and text rendering.
 */
void
CallbackAdapterPlayer::SetForeColor(const rgb_color& color)
{
	fCallbacks->set_fore_color(fUserData, color);
}


/**
 * @brief Forwards a set-background-color request to the legacy set_back_color callback.
 *
 * @param color The new background color used for clearing and certain drawing modes.
 */
void
CallbackAdapterPlayer::SetBackColor(const rgb_color& color)
{
	fCallbacks->set_back_color(fUserData, color);
}


/**
 * @brief Forwards a set-stipple-pattern request to the legacy set_stipple_pattern callback.
 *
 * @param stipplePattern The 8x8 bit pattern used to mask drawing operations.
 */
void
CallbackAdapterPlayer::SetStipplePattern(const pattern& stipplePattern)
{
	fCallbacks->set_stipple_pattern(fUserData, stipplePattern);
}


/**
 * @brief Forwards a set-scale request to the legacy set_scale callback.
 *
 * @param scale The uniform scale factor applied to subsequent drawing operations.
 */
void
CallbackAdapterPlayer::SetScale(float scale)
{
	fCallbacks->set_scale(fUserData, scale);
}


/**
 * @brief Forwards a set-font-family request to the legacy set_font_family callback.
 *
 * Duplicates the family name with strndup() because the legacy callback
 * receives a null-terminated char* rather than a pointer-plus-length pair.
 *
 * @param _family The font family name (not necessarily null-terminated).
 * @param length  Number of bytes in @a _family.
 */
void
CallbackAdapterPlayer::SetFontFamily(const char* _family, size_t length)
{
	char* family = strndup(_family, length);

	fCallbacks->set_font_family(fUserData, family);

	free(family);
}


/**
 * @brief Forwards a set-font-style request to the legacy set_font_style callback.
 *
 * Duplicates the style name with strndup() because the legacy callback
 * receives a null-terminated char* rather than a pointer-plus-length pair.
 *
 * @param _style The font style name (not necessarily null-terminated).
 * @param length Number of bytes in @a _style.
 */
void
CallbackAdapterPlayer::SetFontStyle(const char* _style, size_t length)
{
	char* style = strndup(_style, length);

	fCallbacks->set_font_style(fUserData, style);

	free(style);
}


/**
 * @brief Forwards a set-font-spacing request to the legacy set_font_spacing callback.
 *
 * @param spacing The character spacing mode (e.g. B_CHAR_SPACING, B_STRING_SPACING).
 */
void
CallbackAdapterPlayer::SetFontSpacing(uint8 spacing)
{
	fCallbacks->set_font_spacing(fUserData, spacing);
}


/**
 * @brief Forwards a set-font-size request to the legacy set_font_size callback.
 *
 * @param size The new font size in points.
 */
void
CallbackAdapterPlayer::SetFontSize(float size)
{
	fCallbacks->set_font_size(fUserData, size);
}


/**
 * @brief Forwards a set-font-rotation request to the legacy set_font_rotate callback.
 *
 * @param rotation The font rotation angle in degrees, measured counter-clockwise.
 */
void
CallbackAdapterPlayer::SetFontRotation(float rotation)
{
	fCallbacks->set_font_rotate(fUserData, rotation);
}


/**
 * @brief Forwards a set-font-encoding request to the legacy set_font_encoding callback.
 *
 * @param encoding The character encoding identifier (e.g. B_UNICODE_UTF8).
 */
void
CallbackAdapterPlayer::SetFontEncoding(uint8 encoding)
{
	fCallbacks->set_font_encoding(fUserData, encoding);
}


/**
 * @brief Forwards a set-font-flags request to the legacy set_font_flags callback.
 *
 * @param flags Bitmask of font rendering flags (e.g. B_DISABLE_ANTIALIASING).
 */
void
CallbackAdapterPlayer::SetFontFlags(uint32 flags)
{
	fCallbacks->set_font_flags(fUserData, flags);
}


/**
 * @brief Forwards a set-font-shear request to the legacy set_font_shear callback.
 *
 * @param shear The shear angle in degrees; 90.0 produces upright (unsheared) glyphs.
 */
void
CallbackAdapterPlayer::SetFontShear(float shear)
{
	fCallbacks->set_font_shear(fUserData, shear);
}


/**
 * @brief Forwards a set-font-face request to the legacy set_font_face callback.
 *
 * @param face Bitmask of font face attributes (e.g. B_BOLD_FACE, B_ITALIC_FACE).
 */
void
CallbackAdapterPlayer::SetFontFace(uint16 face)
{
	fCallbacks->set_font_face(fUserData, face);
}


/**
 * @brief Forwards a set-blending-mode request to the legacy set_blending_mode callback.
 *
 * @param alphaSrcMode  Selects which source alpha value is used (e.g. B_PIXEL_ALPHA).
 * @param alphaFncMode  Selects how source and destination alpha values are combined
 *                      (e.g. B_ALPHA_OVERLAY).
 */
void
CallbackAdapterPlayer::SetBlendingMode(source_alpha alphaSrcMode,
	alpha_function alphaFncMode)
{
	fCallbacks->set_blending_mode(fUserData, alphaSrcMode, alphaFncMode);
}


/**
 * @brief Forwards a set-transform request to the legacy set_transform callback.
 *
 * @param transform The affine transformation matrix to apply to subsequent drawing.
 */
void
CallbackAdapterPlayer::SetTransform(const BAffineTransform& transform)
{
	fCallbacks->set_transform(fUserData, transform);
}


/**
 * @brief Forwards a translate-by request to the legacy translate_by callback.
 *
 * @param x Horizontal translation offset in view coordinates.
 * @param y Vertical translation offset in view coordinates.
 */
void
CallbackAdapterPlayer::TranslateBy(double x, double y)
{
	fCallbacks->translate_by(fUserData, x, y);
}


/**
 * @brief Forwards a scale-by request to the legacy scale_by callback.
 *
 * @param x Horizontal scale factor.
 * @param y Vertical scale factor.
 */
void
CallbackAdapterPlayer::ScaleBy(double x, double y)
{
	fCallbacks->scale_by(fUserData, x, y);
}


/**
 * @brief Forwards a rotate-by request to the legacy rotate_by callback.
 *
 * @param angleRadians The rotation angle in radians, applied counter-clockwise.
 */
void
CallbackAdapterPlayer::RotateBy(double angleRadians)
{
	fCallbacks->rotate_by(fUserData, angleRadians);
}


/**
 * @brief Forwards a blend-layer request to the legacy blend_layer callback.
 *
 * @param layer Pointer to the compositing layer to blend into the current surface.
 */
void
CallbackAdapterPlayer::BlendLayer(Layer* layer)
{
	fCallbacks->blend_layer(fUserData, layer);
}


/**
 * @brief Forwards a clip-to-rect request to the legacy clip_to_rect callback.
 *
 * @param rect    The rectangle used as the clipping boundary.
 * @param inverse @c true to clip to the region outside @a rect;
 *                @c false to clip to the inside.
 */
void
CallbackAdapterPlayer::ClipToRect(const BRect& rect, bool inverse)
{
	fCallbacks->clip_to_rect(fUserData, rect, inverse);
}


/**
 * @brief Forwards a clip-to-shape request to the legacy clip_to_shape callback.
 *
 * @param opCount  Number of shape opcodes in @a opList.
 * @param opList   Array of BShape opcode values.
 * @param ptCount  Number of points in @a ptList.
 * @param ptList   Array of BPoint coordinates referenced by the opcodes.
 * @param inverse  @c true to clip to the region outside the shape;
 *                 @c false to clip to the inside.
 */
void
CallbackAdapterPlayer::ClipToShape(int32 opCount, const uint32 opList[],
	int32 ptCount, const BPoint ptList[], bool inverse)
{
	fCallbacks->clip_to_shape(fUserData, opCount, opList,
			ptCount, ptList, inverse);
}


/**
 * @brief Forwards a draw-string-at-locations request to the legacy draw_string_locations callback.
 *
 * Duplicates the string with strndup() because the legacy callback receives
 * a null-terminated char* rather than a pointer-plus-length pair.
 *
 * @param _string       The character data to draw (not necessarily null-terminated).
 * @param length        Number of bytes in @a _string.
 * @param locations     Array of per-glyph positions in view coordinates.
 * @param locationCount Number of elements in @a locations.
 */
void
CallbackAdapterPlayer::DrawStringLocations(const char* _string, size_t length,
	const BPoint* locations, size_t locationCount)
{
	char* string = strndup(_string, length);

	fCallbacks->draw_string_locations(fUserData, string, locations,
			locationCount);

	free(string);
}


/**
 * @brief Forwards a draw-rectangle-with-gradient request to the appropriate legacy callback.
 *
 * Dispatches to fill_rect_gradient when @a fill is @c true, or
 * stroke_rect_gradient otherwise.
 *
 * @param rect     The rectangle to draw.
 * @param gradient The gradient to apply when rendering.
 * @param fill     @c true to fill; @c false to stroke the outline.
 */
void
CallbackAdapterPlayer::DrawRectGradient(const BRect& rect, BGradient& gradient, bool fill)
{
	if (fill)
		fCallbacks->fill_rect_gradient(fUserData, rect, gradient);
	else
		fCallbacks->stroke_rect_gradient(fUserData, rect, gradient);
}


/**
 * @brief Forwards a draw-round-rectangle-with-gradient request to the appropriate legacy callback.
 *
 * Dispatches to fill_round_rect_gradient or stroke_round_rect_gradient based on @a fill.
 *
 * @param rect     The bounding rectangle of the rounded rectangle.
 * @param radii    The x and y corner radii.
 * @param gradient The gradient to apply when rendering.
 * @param fill     @c true to fill; @c false to stroke the outline.
 */
void
CallbackAdapterPlayer::DrawRoundRectGradient(const BRect& rect, const BPoint& radii, BGradient& gradient,
	bool fill)
{
	if (fill)
		fCallbacks->fill_round_rect_gradient(fUserData, rect, radii, gradient);
	else
		fCallbacks->stroke_round_rect_gradient(fUserData, rect, radii, gradient);
}


/**
 * @brief Forwards a draw-bezier-with-gradient request to the appropriate legacy callback.
 *
 * Copies the control points into a local array and dispatches to fill_bezier_gradient
 * or stroke_bezier_gradient based on @a fill.
 *
 * @param _points  Array of exactly four BPoint control points.
 * @param gradient The gradient to apply when rendering.
 * @param fill     @c true to fill the enclosed area; @c false to stroke the curve.
 */
void
CallbackAdapterPlayer::DrawBezierGradient(const BPoint _points[4], BGradient& gradient, bool fill)
{
	BPoint points[4] = { _points[0], _points[1], _points[2], _points[3] };

	if (fill)
		fCallbacks->fill_bezier_gradient(fUserData, points, gradient);
	else
		fCallbacks->stroke_bezier_gradient(fUserData, points, gradient);
}


/**
 * @brief Forwards a draw-arc-with-gradient request to the appropriate legacy callback.
 *
 * Dispatches to fill_arc_gradient or stroke_arc_gradient based on @a fill.
 *
 * @param center     The center point of the arc's ellipse.
 * @param radii      The x and y radii of the ellipse.
 * @param startTheta The starting angle of the arc, in degrees.
 * @param arcTheta   The angular extent of the arc, in degrees.
 * @param gradient   The gradient to apply when rendering.
 * @param fill       @c true to fill the arc sector; @c false to stroke the arc.
 */
void
CallbackAdapterPlayer::DrawArcGradient(const BPoint& center, const BPoint& radii,
	float startTheta, float arcTheta, BGradient& gradient, bool fill)
{
	if (fill)
		fCallbacks->fill_arc_gradient(fUserData, center, radii, startTheta, arcTheta, gradient);
	else
		fCallbacks->stroke_arc_gradient(fUserData, center, radii, startTheta, arcTheta, gradient);
}


/**
 * @brief Forwards a draw-ellipse-with-gradient request to the appropriate legacy callback.
 *
 * Converts the bounding @a rect to center+radii form and dispatches to
 * fill_ellipse_gradient or stroke_ellipse_gradient based on @a fill.
 *
 * @param rect     The bounding rectangle of the ellipse.
 * @param gradient The gradient to apply when rendering.
 * @param fill     @c true to fill the ellipse; @c false to stroke its outline.
 */
void
CallbackAdapterPlayer::DrawEllipseGradient(const BRect& rect, BGradient& gradient, bool fill)
{
	BPoint radii((rect.Width() + 1) / 2.0f, (rect.Height() + 1) / 2.0f);
	BPoint center = rect.LeftTop() + radii;

	if (fill)
		fCallbacks->fill_ellipse_gradient(fUserData, center, radii, gradient);
	else
		fCallbacks->stroke_ellipse_gradient(fUserData, center, radii, gradient);
}


/**
 * @brief Forwards a draw-polygon-with-gradient request to the appropriate legacy callback.
 *
 * Copies the point array into a stack-or-heap buffer and dispatches to
 * fill_polygon_gradient or stroke_polygon_gradient based on @a fill.
 *
 * @param numPoints The number of vertices in @a _points.
 * @param _points   Array of polygon vertices in view coordinates.
 * @param isClosed  @c true if the polygon is closed.
 * @param gradient  The gradient to apply when rendering.
 * @param fill      @c true to fill the polygon; @c false to stroke its outline.
 */
void
CallbackAdapterPlayer::DrawPolygonGradient(size_t numPoints, const BPoint _points[],
	bool isClosed, BGradient& gradient, bool fill)
{
	BStackOrHeapArray<BPoint, 200> points(numPoints);
	if (!points.IsValid())
		return;

	memcpy((void*)points, _points, numPoints * sizeof(BPoint));

	if (fill)
		fCallbacks->fill_polygon_gradient(fUserData, numPoints, points, isClosed, gradient);
	else
		fCallbacks->stroke_polygon_gradient(fUserData, numPoints, points, isClosed, gradient);
}


/**
 * @brief Forwards a draw-shape-with-gradient request to the appropriate legacy callback.
 *
 * Dispatches to fill_shape_gradient or stroke_shape_gradient based on @a fill.
 *
 * @param shape    The BShape object to draw.
 * @param gradient The gradient to apply when rendering.
 * @param fill     @c true to fill the shape; @c false to stroke its outline.
 */
void
CallbackAdapterPlayer::DrawShapeGradient(const BShape& shape, BGradient& gradient, bool fill)
{
	if (fill)
		fCallbacks->fill_shape_gradient(fUserData, shape, gradient);
	else
		fCallbacks->stroke_shape_gradient(fUserData, shape, gradient);
}


/**
 * @brief Forwards a set-fill-rule request to the legacy set_fill_rule callback.
 *
 * @param fillRule The fill rule to use for self-intersecting shapes
 *                 (e.g. B_EVEN_ODD or B_NONZERO).
 */
void
CallbackAdapterPlayer::SetFillRule(int32 fillRule)
{
	fCallbacks->set_fill_rule(fUserData, fillRule);
}


/**
 * @brief Forwards a stroke-line-with-gradient request to the legacy stroke_line_gradient callback.
 *
 * @param start    The starting point of the line in view coordinates.
 * @param end      The ending point of the line in view coordinates.
 * @param gradient The gradient to apply along the stroke.
 */
void
CallbackAdapterPlayer::StrokeLineGradient(const BPoint& start, const BPoint& end, BGradient& gradient)
{
	fCallbacks->stroke_line_gradient(fUserData, start, end, gradient);
}


#if DEBUG > 1
/**
 * @brief Returns a human-readable string name for a BPicture opcode.
 *
 * Used only in debug builds (DEBUG > 1) to produce diagnostic log output
 * during picture playback.
 *
 * @param op The numeric opcode value (e.g. B_PIC_MOVE_PEN_BY).
 * @return A string literal naming the opcode, or "Unknown op" if unrecognised.
 */
static const char *
PictureOpToString(int op)
{
	#define RETURN_STRING(x) case x: return #x

	switch(op) {
		RETURN_STRING(B_PIC_MOVE_PEN_BY);
		RETURN_STRING(B_PIC_STROKE_LINE);
		RETURN_STRING(B_PIC_STROKE_RECT);
		RETURN_STRING(B_PIC_FILL_RECT);
		RETURN_STRING(B_PIC_STROKE_ROUND_RECT);
		RETURN_STRING(B_PIC_FILL_ROUND_RECT);
		RETURN_STRING(B_PIC_STROKE_BEZIER);
		RETURN_STRING(B_PIC_FILL_BEZIER);
		RETURN_STRING(B_PIC_STROKE_POLYGON);
		RETURN_STRING(B_PIC_FILL_POLYGON);
		RETURN_STRING(B_PIC_STROKE_SHAPE);
		RETURN_STRING(B_PIC_FILL_SHAPE);
		RETURN_STRING(B_PIC_DRAW_STRING);
		RETURN_STRING(B_PIC_DRAW_STRING_LOCATIONS);
		RETURN_STRING(B_PIC_DRAW_PIXELS);
		RETURN_STRING(B_PIC_DRAW_PICTURE);
		RETURN_STRING(B_PIC_STROKE_ARC);
		RETURN_STRING(B_PIC_FILL_ARC);
		RETURN_STRING(B_PIC_STROKE_ELLIPSE);
		RETURN_STRING(B_PIC_FILL_ELLIPSE);
		RETURN_STRING(B_PIC_STROKE_RECT_GRADIENT);
		RETURN_STRING(B_PIC_FILL_RECT_GRADIENT);
		RETURN_STRING(B_PIC_STROKE_ROUND_RECT_GRADIENT);
		RETURN_STRING(B_PIC_FILL_ROUND_RECT_GRADIENT);
		RETURN_STRING(B_PIC_STROKE_BEZIER_GRADIENT);
		RETURN_STRING(B_PIC_FILL_BEZIER_GRADIENT);
		RETURN_STRING(B_PIC_STROKE_POLYGON_GRADIENT);
		RETURN_STRING(B_PIC_FILL_POLYGON_GRADIENT);
		RETURN_STRING(B_PIC_STROKE_SHAPE_GRADIENT);
		RETURN_STRING(B_PIC_FILL_SHAPE_GRADIENT);
		RETURN_STRING(B_PIC_STROKE_ARC_GRADIENT);
		RETURN_STRING(B_PIC_FILL_ARC_GRADIENT);
		RETURN_STRING(B_PIC_STROKE_ELLIPSE_GRADIENT);
		RETURN_STRING(B_PIC_FILL_ELLIPSE_GRADIENT);
		RETURN_STRING(B_PIC_STROKE_LINE_GRADIENT);

		RETURN_STRING(B_PIC_ENTER_STATE_CHANGE);
		RETURN_STRING(B_PIC_SET_CLIPPING_RECTS);
		RETURN_STRING(B_PIC_CLIP_TO_PICTURE);
		RETURN_STRING(B_PIC_PUSH_STATE);
		RETURN_STRING(B_PIC_POP_STATE);
		RETURN_STRING(B_PIC_CLEAR_CLIPPING_RECTS);
		RETURN_STRING(B_PIC_CLIP_TO_RECT);
		RETURN_STRING(B_PIC_CLIP_TO_SHAPE);

		RETURN_STRING(B_PIC_SET_ORIGIN);
		RETURN_STRING(B_PIC_SET_PEN_LOCATION);
		RETURN_STRING(B_PIC_SET_DRAWING_MODE);
		RETURN_STRING(B_PIC_SET_LINE_MODE);
		RETURN_STRING(B_PIC_SET_PEN_SIZE);
		RETURN_STRING(B_PIC_SET_SCALE);
		RETURN_STRING(B_PIC_SET_TRANSFORM);
		RETURN_STRING(B_PIC_SET_FORE_COLOR);
		RETURN_STRING(B_PIC_SET_BACK_COLOR);
		RETURN_STRING(B_PIC_SET_STIPLE_PATTERN);
		RETURN_STRING(B_PIC_ENTER_FONT_STATE);
		RETURN_STRING(B_PIC_SET_BLENDING_MODE);
		RETURN_STRING(B_PIC_SET_FILL_RULE);
		RETURN_STRING(B_PIC_SET_FONT_FAMILY);
		RETURN_STRING(B_PIC_SET_FONT_STYLE);
		RETURN_STRING(B_PIC_SET_FONT_SPACING);
		RETURN_STRING(B_PIC_SET_FONT_ENCODING);
		RETURN_STRING(B_PIC_SET_FONT_FLAGS);
		RETURN_STRING(B_PIC_SET_FONT_SIZE);
		RETURN_STRING(B_PIC_SET_FONT_ROTATE);
		RETURN_STRING(B_PIC_SET_FONT_SHEAR);
		RETURN_STRING(B_PIC_SET_FONT_BPP);
		RETURN_STRING(B_PIC_SET_FONT_FACE);

		RETURN_STRING(B_PIC_AFFINE_TRANSLATE);
		RETURN_STRING(B_PIC_AFFINE_SCALE);
		RETURN_STRING(B_PIC_AFFINE_ROTATE);

		RETURN_STRING(B_PIC_BLEND_LAYER);

		default: return "Unknown op";
	}
	#undef RETURN_STRING
}
#endif


/**
 * @brief Constructs a PicturePlayer bound to a raw BPicture data buffer.
 *
 * The player does not copy the data; the caller must ensure the buffer
 * remains valid for the lifetime of the PicturePlayer instance.
 *
 * @param data     Pointer to the raw BPicture command stream.
 * @param size     Byte length of @a data.
 * @param pictures BList of nested BPicture pointers referenced by
 *                 B_PIC_DRAW_PICTURE opcodes, or NULL if there are none.
 */
PicturePlayer::PicturePlayer(const void *data, size_t size, BList *pictures)
	:	fData(data),
		fSize(size),
		fPictures(pictures)
{
}


/**
 * @brief Destroys the PicturePlayer.
 *
 * Does not free the data buffer or the pictures list; those are owned by
 * the caller.
 */
PicturePlayer::~PicturePlayer()
{
}


/**
 * @brief Plays back the picture using a legacy C function-pointer callback table.
 *
 * This overload exists for source compatibility with older code that passes a
 * flat array of function pointers rather than a PicturePlayerCallbacks subclass.
 * If @a tableEntries is smaller than kOpsTableSize, missing slots are filled with
 * a no-op function so that unknown opcodes are silently skipped.
 *
 * @param callBackTable  Pointer to an array of function pointers matching the
 *                       layout of picture_player_callbacks_compat.
 * @param tableEntries   Number of valid entries in @a callBackTable.
 * @param userData       Opaque pointer forwarded as the first argument to every callback.
 *
 * @return A status code.
 * @retval B_OK       Playback completed successfully.
 * @retval B_BAD_DATA The picture data stream is malformed.
 *
 * @see Play(PicturePlayerCallbacks&), _Play()
 */
status_t
PicturePlayer::Play(void** callBackTable, int32 tableEntries, void* userData)
{
	// We don't check if the functions in the table are NULL, but we
	// check the tableEntries to see if the table is big enough.
	// If an application supplies the wrong size or an invalid pointer,
	// it's its own fault.

	// If the caller supplied a function table smaller than needed,
	// we use our dummy table, and copy the supported ops from the supplied one.
	void *dummyTable[kOpsTableSize];

	if ((size_t)tableEntries < kOpsTableSize) {
		memcpy(dummyTable, callBackTable, tableEntries * sizeof(void*));
		for (size_t i = (size_t)tableEntries; i < kOpsTableSize; i++)
			dummyTable[i] = (void*)nop;

		callBackTable = dummyTable;
	}

	CallbackAdapterPlayer callbackAdapterPlayer(userData, callBackTable);
	return _Play(callbackAdapterPlayer, fData, fSize, 0);
}


/**
 * @brief Plays back the picture using a PicturePlayerCallbacks virtual-method object.
 *
 * This is the preferred modern overload. Each decoded opcode in the picture
 * stream results in a virtual method call on @a callbacks.
 *
 * @param callbacks Reference to the callback object that receives drawing operations.
 *
 * @return A status code.
 * @retval B_OK       Playback completed successfully.
 * @retval B_BAD_DATA The picture data stream is malformed.
 *
 * @see Play(void**, int32, void*), _Play()
 */
status_t
PicturePlayer::Play(PicturePlayerCallbacks& callbacks)
{
	return _Play(callbacks, fData, fSize, 0);
}


/**
 * @brief Lightweight bounds-checked reader over a flat byte buffer.
 *
 * DataReader wraps a raw memory region and provides typed, bounds-checked
 * pointer access without copying data. It is used by _Play() to safely
 * decode the packed binary fields within each picture opcode's payload.
 *
 * @see PicturePlayer::_Play()
 */
class DataReader {
public:
		/**
		 * @brief Constructs a DataReader over an existing memory buffer.
		 *
		 * @param buffer Pointer to the start of the data to read.
		 * @param length Total number of bytes available in @a buffer.
		 */
		DataReader(const void* buffer, size_t length)
			:
			fBuffer((const uint8*)buffer),
			fRemaining(length)
		{
		}

		/**
		 * @brief Returns the number of bytes not yet consumed from the buffer.
		 *
		 * @return Remaining byte count.
		 */
		size_t
		Remaining() const
		{
			return fRemaining;
		}

		/**
		 * @brief Returns a typed pointer into the buffer and advances the read cursor.
		 *
		 * Checks that at least @c sizeof(T) * @a count bytes remain before
		 * advancing.  No data is copied; @a typed is set to point directly
		 * into the underlying buffer.
		 *
		 * @tparam T     The type to interpret the next bytes as.
		 * @param  typed On success, set to point at the next @a count elements of type T.
		 * @param  count Number of consecutive T elements to consume (default 1).
		 * @return @c true on success; @c false if the buffer has insufficient data.
		 */
		template<typename T>
		bool
		Get(const T*& typed, size_t count = 1)
		{
			if (fRemaining < sizeof(T) * count)
				return false;

			typed = reinterpret_cast<const T *>(fBuffer);
			fRemaining -= sizeof(T) * count;
			fBuffer += sizeof(T) * count;
			return true;
		}

		/**
		 * @brief Unflattens a BGradient from the current buffer position and advances the cursor.
		 *
		 * Uses BGradient::Unflatten() with a BMemoryIO wrapper over the remaining
		 * buffer bytes. The cursor is advanced by however many bytes the gradient
		 * consumed from the stream.
		 *
		 * @param gradient On success, set to a newly allocated BGradient object;
		 *                 the caller takes ownership.
		 * @return @c true on success; @c false if unflattening failed.
		 */
		bool GetGradient(BGradient*& gradient)
		{
			BMemoryIO stream(fBuffer, fRemaining);
			printf("fRemaining: %ld\n", fRemaining);
			if (BGradient::Unflatten(gradient, &stream) != B_OK) {
				printf("BGradient::Unflatten(_gradient, &stream) != B_OK\n");
				return false;
			}

			fRemaining -= stream.Position();
			fBuffer += stream.Position();
			return true;
		}

		/**
		 * @brief Returns a typed pointer to all remaining bytes and marks the buffer as exhausted.
		 *
		 * After a successful call, Remaining() will return 0.
		 *
		 * @tparam T      The type to interpret the remaining data as.
		 * @param  buffer On success, set to point at the start of the remaining data.
		 * @param  size   On success, set to the number of bytes remaining.
		 * @return @c true on success; @c false if the buffer is already empty.
		 */
		template<typename T>
		bool
		GetRemaining(const T*& buffer, size_t& size)
		{
			if (fRemaining == 0)
				return false;

			buffer = reinterpret_cast<const T*>(fBuffer);
			size = fRemaining;
			fRemaining = 0;
			return true;
		}

private:
		/** @brief Current read position within the buffer. */
		const uint8*	fBuffer;
		/** @brief Number of bytes not yet consumed. */
		size_t			fRemaining;
};


/**
 * @brief Packed header preceding each opcode record in the BPicture data stream.
 *
 * Every command in the stream starts with a picture_data_entry_header that
 * identifies the opcode and gives the byte length of its payload, allowing
 * the player to skip unknown opcodes safely.
 */
struct picture_data_entry_header {
	uint16 op;   /**< @brief Opcode identifying the drawing command (e.g. B_PIC_STROKE_LINE). */
	uint32 size; /**< @brief Byte length of the opcode's payload that follows this header. */
} _PACKED;


/**
 * @brief Iterates over and executes all opcodes in a BPicture data buffer.
 *
 * _Play() is the core dispatch loop. It reads successive picture_data_entry_header
 * records from @a buffer, validates each opcode against the nesting context
 * imposed by @a parentOp, decodes the typed payload fields using a DataReader,
 * and calls the appropriate PicturePlayerCallbacks virtual method. Nested
 * B_PIC_ENTER_STATE_CHANGE and B_PIC_ENTER_FONT_STATE blocks are handled by
 * recursive calls.
 *
 * @param callbacks Reference to the callback object that receives each drawing command.
 * @param buffer    Pointer to the start of the picture command stream to decode.
 * @param length    Byte length of @a buffer.
 * @param parentOp  The enclosing opcode when called recursively (0 for the top level,
 *                  B_PIC_ENTER_STATE_CHANGE for a state-change block, or
 *                  B_PIC_ENTER_FONT_STATE for a font-state block). Used to enforce
 *                  which opcodes are legal within the current nesting context.
 *
 * @return A status code.
 * @retval B_OK       All opcodes were decoded and dispatched successfully.
 * @retval B_BAD_DATA The stream is truncated or contains an opcode that is illegal
 *                    in the current @a parentOp context.
 *
 * @see Play(PicturePlayerCallbacks&), Play(void**, int32, void*)
 */
status_t
PicturePlayer::_Play(PicturePlayerCallbacks& callbacks,
	const void* buffer, size_t length, uint16 parentOp)
{
#if DEBUG
	printf("Start rendering %sBPicture...\n", parentOp != 0 ? "sub " : "");
	bigtime_t startTime = system_time();
	int32 numOps = 0;
#endif

	DataReader pictureReader(buffer, length);

	while (pictureReader.Remaining() > 0) {
		const picture_data_entry_header* header;
		const uint8* opData = NULL;
		if (!pictureReader.Get(header)
			|| !pictureReader.Get(opData, header->size)) {
			return B_BAD_DATA;
		}

		DataReader reader(opData, header->size);

		// Disallow ops that don't fit the parent.
		switch (parentOp) {
			case 0:
				// No parent op, no restrictions.
				break;

			case B_PIC_ENTER_STATE_CHANGE:
				if (header->op <= B_PIC_ENTER_STATE_CHANGE
					|| header->op > B_PIC_SET_TRANSFORM) {
					return B_BAD_DATA;
				}
				break;

			case B_PIC_ENTER_FONT_STATE:
				if (header->op < B_PIC_SET_FONT_FAMILY
					|| header->op > B_PIC_SET_FONT_FACE) {
					return B_BAD_DATA;
					}
				break;

			default:
				return B_BAD_DATA;
		}

#if DEBUG > 1
		bigtime_t startOpTime = system_time();
		printf("Op %s ", PictureOpToString(header->op));
#endif
		switch (header->op) {
			case B_PIC_MOVE_PEN_BY:
			{
				const BPoint* where;
				if (!reader.Get(where))
					break;

				callbacks.MovePenBy(*where);
				break;
			}

			case B_PIC_STROKE_LINE:
			{
				const BPoint* start;
				const BPoint* end;
				if (!reader.Get(start) || !reader.Get(end))
					break;

				callbacks.StrokeLine(*start, *end);
				break;
			}

			case B_PIC_STROKE_RECT:
			case B_PIC_FILL_RECT:
			{
				const BRect* rect;
				if (!reader.Get(rect))
					break;

				callbacks.DrawRect(*rect, header->op == B_PIC_FILL_RECT);
				break;
			}

			case B_PIC_STROKE_ROUND_RECT:
			case B_PIC_FILL_ROUND_RECT:
			{
				const BRect* rect;
				const BPoint* radii;
				if (!reader.Get(rect) || !reader.Get(radii))
					break;

				callbacks.DrawRoundRect(*rect, *radii,
					header->op == B_PIC_FILL_ROUND_RECT);
				break;
			}

			case B_PIC_STROKE_BEZIER:
			case B_PIC_FILL_BEZIER:
			{
				const size_t kNumControlPoints = 4;
				const BPoint* controlPoints;
				if (!reader.Get(controlPoints, kNumControlPoints))
					break;

				callbacks.DrawBezier(controlPoints, header->op == B_PIC_FILL_BEZIER);
				break;
			}

			case B_PIC_STROKE_ARC:
			case B_PIC_FILL_ARC:
			{
				const BPoint* center;
				const BPoint* radii;
				const float* startTheta;
				const float* arcTheta;
				if (!reader.Get(center)
					|| !reader.Get(radii) || !reader.Get(startTheta)
					|| !reader.Get(arcTheta)) {
					break;
				}

				callbacks.DrawArc(*center, *radii, *startTheta,
					*arcTheta, header->op == B_PIC_FILL_ARC);
				break;
			}

			case B_PIC_STROKE_ELLIPSE:
			case B_PIC_FILL_ELLIPSE:
			{
				const BRect* rect;
				if (!reader.Get(rect))
					break;

				callbacks.DrawEllipse(*rect,
					header->op == B_PIC_FILL_ELLIPSE);
				break;
			}

			case B_PIC_STROKE_POLYGON:
			case B_PIC_FILL_POLYGON:
			{
				const uint32* numPoints;
				const BPoint* points;
				if (!reader.Get(numPoints) || !reader.Get(points, *numPoints))
					break;

				bool isClosed = true;
				const bool* closedPointer;
				if (header->op != B_PIC_FILL_POLYGON) {
					if (!reader.Get(closedPointer))
						break;

					isClosed = *closedPointer;
				}

				callbacks.DrawPolygon(*numPoints, points, isClosed,
					header->op == B_PIC_FILL_POLYGON);
				break;
			}

			case B_PIC_STROKE_SHAPE:
			case B_PIC_FILL_SHAPE:
			{
				const uint32* opCount;
				const uint32* pointCount;
				const uint32* opList;
				const BPoint* pointList;
				if (!reader.Get(opCount)
					|| !reader.Get(pointCount) || !reader.Get(opList, *opCount)
					|| !reader.Get(pointList, *pointCount)) {
					break;
				}

				// TODO: remove BShape data copying
				BShape shape;
				BShape::Private(shape).SetData(*opCount, *pointCount, opList, pointList);

				callbacks.DrawShape(shape, header->op == B_PIC_FILL_SHAPE);
				break;
			}

			case B_PIC_STROKE_RECT_GRADIENT:
			case B_PIC_FILL_RECT_GRADIENT:
			{
				const BRect* rect;
				BGradient* gradient;
				if (!reader.Get(rect) || !reader.GetGradient(gradient))
					break;
				ObjectDeleter<BGradient> gradientDeleter(gradient);

				callbacks.DrawRectGradient(*rect, *gradient,
					header->op == B_PIC_FILL_RECT_GRADIENT);
				break;
			}

			case B_PIC_STROKE_ROUND_RECT_GRADIENT:
			case B_PIC_FILL_ROUND_RECT_GRADIENT:
			{
				const BRect* rect;
				const BPoint* radii;
				BGradient* gradient;
				if (!reader.Get(rect)
					|| !reader.Get(radii) || !reader.GetGradient(gradient)) {
					break;
				}
				ObjectDeleter<BGradient> gradientDeleter(gradient);

				callbacks.DrawRoundRectGradient(*rect, *radii, *gradient,
					header->op == B_PIC_FILL_ROUND_RECT_GRADIENT);
				break;
			}

			case B_PIC_STROKE_BEZIER_GRADIENT:
			case B_PIC_FILL_BEZIER_GRADIENT:
			{
				const size_t kNumControlPoints = 4;
				const BPoint* controlPoints;
				BGradient* gradient;
				if (!reader.Get(controlPoints, kNumControlPoints) || !reader.GetGradient(gradient))
					break;
				ObjectDeleter<BGradient> gradientDeleter(gradient);

				callbacks.DrawBezierGradient(controlPoints, *gradient,
					header->op == B_PIC_FILL_BEZIER_GRADIENT);
				break;
			}

			case B_PIC_STROKE_POLYGON_GRADIENT:
			case B_PIC_FILL_POLYGON_GRADIENT:
			{
				const uint32* numPoints;
				const BPoint* points;
				BGradient* gradient;
				if (!reader.Get(numPoints) || !reader.Get(points, *numPoints))
					break;

				bool isClosed = true;
				const bool* closedPointer;
				if (header->op != B_PIC_FILL_POLYGON_GRADIENT) {
					if (!reader.Get(closedPointer))
						break;

					isClosed = *closedPointer;
				}

				if (!reader.GetGradient(gradient))
					break;
				ObjectDeleter<BGradient> gradientDeleter(gradient);

				callbacks.DrawPolygonGradient(*numPoints, points, isClosed, *gradient,
					header->op == B_PIC_FILL_POLYGON_GRADIENT);
				break;
			}

			case B_PIC_STROKE_SHAPE_GRADIENT:
			case B_PIC_FILL_SHAPE_GRADIENT:
			{
				const uint32* opCount;
				const uint32* pointCount;
				const uint32* opList;
				const BPoint* pointList;
				BGradient* gradient;
				if (!reader.Get(opCount)
					|| !reader.Get(pointCount) || !reader.Get(opList, *opCount)
					|| !reader.Get(pointList, *pointCount) || !reader.GetGradient(gradient)) {
					break;
				}
				ObjectDeleter<BGradient> gradientDeleter(gradient);

				// TODO: remove BShape data copying
				BShape shape;
				BShape::Private(shape).SetData(*opCount, *pointCount, opList, pointList);

				callbacks.DrawShapeGradient(shape, *gradient,
					header->op == B_PIC_FILL_SHAPE_GRADIENT);
				break;
			}

			case B_PIC_STROKE_ARC_GRADIENT:
			case B_PIC_FILL_ARC_GRADIENT:
			{
				const BPoint* center;
				const BPoint* radii;
				const float* startTheta;
				const float* arcTheta;
				BGradient* gradient;
				if (!reader.Get(center)
					|| !reader.Get(radii) || !reader.Get(startTheta)
					|| !reader.Get(arcTheta) || !reader.GetGradient(gradient)) {
					break;
				}
				ObjectDeleter<BGradient> gradientDeleter(gradient);

				callbacks.DrawArcGradient(*center, *radii, *startTheta,
					*arcTheta, *gradient, header->op == B_PIC_FILL_ARC_GRADIENT);
				break;
			}

			case B_PIC_STROKE_ELLIPSE_GRADIENT:
			case B_PIC_FILL_ELLIPSE_GRADIENT:
			{
				const BRect* rect;
				BGradient* gradient;
				if (!reader.Get(rect) || !reader.GetGradient(gradient))
					break;
				ObjectDeleter<BGradient> gradientDeleter(gradient);

				callbacks.DrawEllipseGradient(*rect, *gradient,
					header->op == B_PIC_FILL_ELLIPSE_GRADIENT);
				break;
			}

			case B_PIC_STROKE_LINE_GRADIENT:
			{
				const BPoint* start;
				const BPoint* end;
				BGradient* gradient;
				if (!reader.Get(start)
					|| !reader.Get(end) || !reader.GetGradient(gradient)) {
					break;
				}

				callbacks.StrokeLineGradient(*start, *end, *gradient);
				break;
			}

			case B_PIC_DRAW_STRING:
			{
				const int32* length;
				const char* string;
				const float* escapementSpace;
				const float* escapementNonSpace;
				if (!reader.Get(length)
					|| !reader.Get(string, *length)
					|| !reader.Get(escapementSpace)
					|| !reader.Get(escapementNonSpace)) {
					break;
				}

				callbacks.DrawString(string, *length,
					*escapementSpace, *escapementNonSpace);
				break;
			}

			case B_PIC_DRAW_STRING_LOCATIONS:
			{
				const uint32* pointCount;
				const BPoint* pointList;
				const int32* length;
				const char* string;
				if (!reader.Get(pointCount)
					|| !reader.Get(pointList, *pointCount)
					|| !reader.Get(length)
					|| !reader.Get(string, *length)) {
					break;
				}

				callbacks.DrawStringLocations(string, *length,
					pointList, *pointCount);
				break;
			}

			case B_PIC_DRAW_PIXELS:
			{
				const BRect* sourceRect;
				const BRect* destinationRect;
				const uint32* width;
				const uint32* height;
				const uint32* bytesPerRow;
				const uint32* colorSpace;
				const uint32* flags;
				const int32* length;
				const uint8* data;
				if (!reader.Get(sourceRect)
					|| !reader.Get(destinationRect) || !reader.Get(width)
					|| !reader.Get(height) || !reader.Get(bytesPerRow)
					|| !reader.Get(colorSpace) || !reader.Get(flags)
					|| !reader.Get(length) || !reader.Get(data, *length)) {
					break;
				}

				callbacks.DrawPixels(*sourceRect, *destinationRect,
					*width, *height, *bytesPerRow, (color_space)*colorSpace,
					*flags, data, *length);
				break;
			}

			case B_PIC_DRAW_PICTURE:
			{
				const BPoint* where;
				const int32* token;
				if (!reader.Get(where) || !reader.Get(token))
					break;

				callbacks.DrawPicture(*where, *token);
				break;
			}

			case B_PIC_SET_CLIPPING_RECTS:
			{
				const clipping_rect* frame;
				if (!reader.Get(frame))
					break;

				uint32 numRects = reader.Remaining() / sizeof(clipping_rect);

				const clipping_rect* rects;
				if (!reader.Get(rects, numRects))
					break;

				callbacks.SetClippingRects(numRects, rects);
				break;
			}

			case B_PIC_CLEAR_CLIPPING_RECTS:
			{
				callbacks.SetClippingRects(0, NULL);
				break;
			}

			case B_PIC_CLIP_TO_PICTURE:
			{
				const int32* token;
				const BPoint* where;
				const bool* inverse;
				if (!reader.Get(token)
					|| !reader.Get(where) || !reader.Get(inverse))
					break;

				callbacks.ClipToPicture(*token, *where, *inverse);
				break;
			}

			case B_PIC_PUSH_STATE:
			{
				callbacks.PushState();
				break;
			}

			case B_PIC_POP_STATE:
			{
				callbacks.PopState();
				break;
			}

			case B_PIC_ENTER_STATE_CHANGE:
			case B_PIC_ENTER_FONT_STATE:
			{
				const void* data;
				size_t length;
				if (!reader.GetRemaining(data, length))
					break;

				if (header->op == B_PIC_ENTER_STATE_CHANGE)
					callbacks.EnterStateChange();
				else
					callbacks.EnterFontState();

				status_t result = _Play(callbacks, data, length,
					header->op);
				if (result != B_OK)
					return result;

				if (header->op == B_PIC_ENTER_STATE_CHANGE)
					callbacks.ExitStateChange();
				else
					callbacks.ExitFontState();

				break;
			}

			case B_PIC_SET_ORIGIN:
			{
				const BPoint* origin;
				if (!reader.Get(origin))
					break;

				callbacks.SetOrigin(*origin);
				break;
			}

			case B_PIC_SET_PEN_LOCATION:
			{
				const BPoint* location;
				if (!reader.Get(location))
					break;

				callbacks.SetPenLocation(*location);
				break;
			}

			case B_PIC_SET_DRAWING_MODE:
			{
				const uint16* mode;
				if (!reader.Get(mode))
					break;

				callbacks.SetDrawingMode((drawing_mode)*mode);
				break;
			}

			case B_PIC_SET_LINE_MODE:
			{
				const uint16* capMode;
				const uint16* joinMode;
				const float* miterLimit;
				if (!reader.Get(capMode)
					|| !reader.Get(joinMode) || !reader.Get(miterLimit)) {
					break;
				}

				callbacks.SetLineMode((cap_mode)*capMode,
					(join_mode)*joinMode, *miterLimit);
				break;
			}

			case B_PIC_SET_PEN_SIZE:
			{
				const float* penSize;
				if (!reader.Get(penSize))
					break;

				callbacks.SetPenSize(*penSize);
				break;
			}

			case B_PIC_SET_FORE_COLOR:
			{
				const rgb_color* color;
				if (!reader.Get(color))
					break;

				callbacks.SetForeColor(*color);
				break;
			}

			case B_PIC_SET_BACK_COLOR:
			{
				const rgb_color* color;
				if (!reader.Get(color))
					break;

				callbacks.SetBackColor(*color);
				break;
			}

			case B_PIC_SET_STIPLE_PATTERN:
			{
				const pattern* stipplePattern;
				if (!reader.Get(stipplePattern)) {
					break;
				}

				callbacks.SetStipplePattern(*stipplePattern);
				break;
			}

			case B_PIC_SET_SCALE:
			{
				const float* scale;
				if (!reader.Get(scale))
					break;

				callbacks.SetScale(*scale);
				break;
			}

			case B_PIC_SET_FONT_FAMILY:
			{
				const int32* length;
				const char* family;
				if (!reader.Get(length)
					|| !reader.Get(family, *length)) {
					break;
				}

				callbacks.SetFontFamily(family, *length);
				break;
			}

			case B_PIC_SET_FONT_STYLE:
			{
				const int32* length;
				const char* style;
				if (!reader.Get(length)
					|| !reader.Get(style, *length)) {
					break;
				}

				callbacks.SetFontStyle(style, *length);
				break;
			}

			case B_PIC_SET_FONT_SPACING:
			{
				const uint32* spacing;
				if (!reader.Get(spacing))
					break;

				callbacks.SetFontSpacing(*spacing);
				break;
			}

			case B_PIC_SET_FONT_SIZE:
			{
				const float* size;
				if (!reader.Get(size))
					break;

				callbacks.SetFontSize(*size);
				break;
			}

			case B_PIC_SET_FONT_ROTATE:
			{
				const float* rotation;
				if (!reader.Get(rotation))
					break;

				callbacks.SetFontRotation(*rotation);
				break;
			}

			case B_PIC_SET_FONT_ENCODING:
			{
				const uint32* encoding;
				if (!reader.Get(encoding))
					break;

				callbacks.SetFontEncoding(*encoding);
				break;
			}

			case B_PIC_SET_FONT_FLAGS:
			{
				const uint32* flags;
				if (!reader.Get(flags))
					break;

				callbacks.SetFontFlags(*flags);
				break;
			}

			case B_PIC_SET_FONT_SHEAR:
			{
				const float* shear;
				if (!reader.Get(shear))
					break;

				callbacks.SetFontShear(*shear);
				break;
			}

			case B_PIC_SET_FONT_FACE:
			{
				const uint32* face;
				if (!reader.Get(face))
					break;

				callbacks.SetFontFace(*face);
				break;
			}

			case B_PIC_SET_BLENDING_MODE:
			{
				const uint16* alphaSourceMode;
				const uint16* alphaFunctionMode;
				if (!reader.Get(alphaSourceMode)
					|| !reader.Get(alphaFunctionMode)) {
					break;
				}

				callbacks.SetBlendingMode(
					(source_alpha)*alphaSourceMode,
					(alpha_function)*alphaFunctionMode);
				break;
			}

			case B_PIC_SET_FILL_RULE:
			{
				const uint32* fillRule;
				if (!reader.Get(fillRule))
					break;

				callbacks.SetFillRule(*fillRule);
				break;
			}

			case B_PIC_SET_TRANSFORM:
			{
				const double* transformValues;
				if (!reader.Get(transformValues, 6))
					break;

				BAffineTransform transform;
				memcpy(&transform.sx, transformValues, 6 * sizeof(double));
				callbacks.SetTransform(transform);
				break;
			}

			case B_PIC_AFFINE_TRANSLATE:
			{
				const double* x;
				const double* y;
				if (!reader.Get(x) || !reader.Get(y))
					break;

				callbacks.TranslateBy(*x, *y);
				break;
			}

			case B_PIC_AFFINE_SCALE:
			{
				const double* x;
				const double* y;
				if (!reader.Get(x) || !reader.Get(y))
					break;

				callbacks.ScaleBy(*x, *y);
				break;
			}

			case B_PIC_AFFINE_ROTATE:
			{
				const double* angleRadians;
				if (!reader.Get(angleRadians))
					break;

				callbacks.RotateBy(*angleRadians);
				break;
			}

			case B_PIC_BLEND_LAYER:
			{
				Layer* const* layer;
				if (!reader.Get<Layer*>(layer))
					break;

				callbacks.BlendLayer(*layer);
				break;
			}

			case B_PIC_CLIP_TO_RECT:
			{
				const bool* inverse;
				const BRect* rect;

				if (!reader.Get(inverse) || !reader.Get(rect))
					break;

				callbacks.ClipToRect(*rect, *inverse);
				break;
			}

			case B_PIC_CLIP_TO_SHAPE:
			{
				const bool* inverse;
				const uint32* opCount;
				const uint32* pointCount;
				const uint32* opList;
				const BPoint* pointList;
				if (!reader.Get(inverse)
					|| !reader.Get(opCount) || !reader.Get(pointCount)
					|| !reader.Get(opList, *opCount)
					|| !reader.Get(pointList, *pointCount)) {
					break;
				}

				callbacks.ClipToShape(*opCount, opList,
					*pointCount, pointList, *inverse);
				break;
			}

			default:
				break;
		}

#if DEBUG
		numOps++;
#if DEBUG > 1
		printf("executed in %" B_PRId64 " usecs\n", system_time()
			- startOpTime);
#endif
#endif
	}

#if DEBUG
	printf("Done! %" B_PRId32 " ops, rendering completed in %" B_PRId64
		" usecs.\n", numOps, system_time() - startTime);
#endif
	return B_OK;
}
