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
 *   Copyright (c) 2001, 2002 Haiku.
 *   Authors: Philippe Houdoin, Simon Gauvin, Michael Pfeiffer
 *   Licensed under the MIT License.
 */


/**
 * @file PictureIterator.cpp
 * @brief BPicture playback dispatcher that routes opcodes to virtual callbacks.
 *
 * Builds the static playback-handler table required by BPicture::Play() and
 * routes each opcode to the corresponding virtual method of a PictureIterator
 * subclass. Subclasses override the individual drawing callbacks to implement
 * custom rendering back-ends such as PicturePrinter or Template.
 *
 * @see PicturePrinter, Template
 */


#include "PictureIterator.h"

// BPicture playback handlers class instance redirectors
/** @brief Playback shim: routes MovePenBy opcode to the PictureIterator instance. */
static void	_MovePenBy(void *p, BPoint delta) 														{ return ((PictureIterator *) p)->MovePenBy(delta); }
/** @brief Playback shim: routes StrokeLine opcode to the PictureIterator instance. */
static void	_StrokeLine(void *p, BPoint start, BPoint end) 											{ return ((PictureIterator *) p)->StrokeLine(start, end); }
/** @brief Playback shim: routes StrokeRect opcode to the PictureIterator instance. */
static void	_StrokeRect(void *p, BRect rect) 														{ return ((PictureIterator *) p)->StrokeRect(rect); }
/** @brief Playback shim: routes FillRect opcode to the PictureIterator instance. */
static void	_FillRect(void *p, BRect rect) 															{ return ((PictureIterator *) p)->FillRect(rect); }
/** @brief Playback shim: routes StrokeRoundRect opcode to the PictureIterator instance. */
static void	_StrokeRoundRect(void *p, BRect rect, BPoint radii) 									{ return ((PictureIterator *) p)->StrokeRoundRect(rect, radii); }
/** @brief Playback shim: routes FillRoundRect opcode to the PictureIterator instance. */
static void	_FillRoundRect(void *p, BRect rect, BPoint radii)  										{ return ((PictureIterator *) p)->FillRoundRect(rect, radii); }
/** @brief Playback shim: routes StrokeBezier opcode to the PictureIterator instance. */
static void	_StrokeBezier(void *p, BPoint *control)  												{ return ((PictureIterator *) p)->StrokeBezier(control); }
/** @brief Playback shim: routes FillBezier opcode to the PictureIterator instance. */
static void	_FillBezier(void *p, BPoint *control)  													{ return ((PictureIterator *) p)->FillBezier(control); }
/** @brief Playback shim: routes StrokeArc opcode to the PictureIterator instance. */
static void	_StrokeArc(void *p, BPoint center, BPoint radii, float startTheta, float arcTheta)		{ return ((PictureIterator *) p)->StrokeArc(center, radii, startTheta, arcTheta); }
/** @brief Playback shim: routes FillArc opcode to the PictureIterator instance. */
static void	_FillArc(void *p, BPoint center, BPoint radii, float startTheta, float arcTheta)		{ return ((PictureIterator *) p)->FillArc(center, radii, startTheta, arcTheta); }
/** @brief Playback shim: routes StrokeEllipse opcode to the PictureIterator instance. */
static void	_StrokeEllipse(void *p, BPoint center, BPoint radii)									{ return ((PictureIterator *) p)->StrokeEllipse(center, radii); }
/** @brief Playback shim: routes FillEllipse opcode to the PictureIterator instance. */
static void	_FillEllipse(void *p, BPoint center, BPoint radii)										{ return ((PictureIterator *) p)->FillEllipse(center, radii); }
/** @brief Playback shim: routes StrokePolygon opcode to the PictureIterator instance. */
static void	_StrokePolygon(void *p, int32 numPoints, BPoint *points, bool isClosed) 				{ return ((PictureIterator *) p)->StrokePolygon(numPoints, points, isClosed); }
/** @brief Playback shim: routes FillPolygon opcode to the PictureIterator instance. */
static void	_FillPolygon(void *p, int32 numPoints, BPoint *points, bool isClosed)					{ return ((PictureIterator *) p)->FillPolygon(numPoints, points, isClosed); }
/** @brief Playback shim: routes StrokeShape opcode to the PictureIterator instance. */
static void	_StrokeShape(void * p, BShape *shape)													{ return ((PictureIterator *) p)->StrokeShape(shape); }
/** @brief Playback shim: routes FillShape opcode to the PictureIterator instance. */
static void	_FillShape(void * p, BShape *shape)														{ return ((PictureIterator *) p)->FillShape(shape); }
/** @brief Playback shim: routes DrawString opcode to the PictureIterator instance. */
static void	_DrawString(void *p, char *string, float deltax, float deltay)							{ return ((PictureIterator *) p)->DrawString(string, deltax, deltay); }
/** @brief Playback shim: routes DrawPixels opcode to the PictureIterator instance. */
static void	_DrawPixels(void *p, BRect src, BRect dest, int32 width, int32 height, int32 bytesPerRow, int32 pixelFormat, int32 flags, void *data)
						{ return ((PictureIterator *) p)->DrawPixels(src, dest, width, height, bytesPerRow, pixelFormat, flags, data); }
/** @brief Playback shim: routes SetClippingRects opcode to the PictureIterator instance. */
static void	_SetClippingRects(void *p, BRect *rects, uint32 numRects)								{ return ((PictureIterator *) p)->SetClippingRects(rects, numRects); }
/** @brief Playback shim: routes ClipToPicture opcode to the PictureIterator instance. */
static void	_ClipToPicture(void * p, BPicture *picture, BPoint point, bool clip_to_inverse_picture)	{ return ((PictureIterator *) p)->ClipToPicture(picture, point, clip_to_inverse_picture); }
/** @brief Playback shim: routes PushState opcode to the PictureIterator instance. */
static void	_PushState(void *p)  																	{ return ((PictureIterator *) p)->PushState(); }
/** @brief Playback shim: routes PopState opcode to the PictureIterator instance. */
static void	_PopState(void *p)  																	{ return ((PictureIterator *) p)->PopState(); }
/** @brief Playback shim: routes EnterStateChange opcode to the PictureIterator instance. */
static void	_EnterStateChange(void *p) 																{ return ((PictureIterator *) p)->EnterStateChange(); }
/** @brief Playback shim: routes ExitStateChange opcode to the PictureIterator instance. */
static void	_ExitStateChange(void *p) 																{ return ((PictureIterator *) p)->ExitStateChange(); }
/** @brief Playback shim: routes EnterFontState opcode to the PictureIterator instance. */
static void	_EnterFontState(void *p) 																{ return ((PictureIterator *) p)->EnterFontState(); }
/** @brief Playback shim: routes ExitFontState opcode to the PictureIterator instance. */
static void	_ExitFontState(void *p) 																{ return ((PictureIterator *) p)->ExitFontState(); }
/** @brief Playback shim: routes SetOrigin opcode to the PictureIterator instance. */
static void	_SetOrigin(void *p, BPoint pt)															{ return ((PictureIterator *) p)->SetOrigin(pt); }
/** @brief Playback shim: routes SetPenLocation opcode to the PictureIterator instance. */
static void	_SetPenLocation(void *p, BPoint pt)														{ return ((PictureIterator *) p)->SetPenLocation(pt); }
/** @brief Playback shim: routes SetDrawingMode opcode to the PictureIterator instance. */
static void	_SetDrawingMode(void *p, drawing_mode mode)												{ return ((PictureIterator *) p)->SetDrawingMode(mode); }
/** @brief Playback shim: routes SetLineMode opcode to the PictureIterator instance. */
static void	_SetLineMode(void *p, cap_mode capMode, join_mode joinMode, float miterLimit)			{ return ((PictureIterator *) p)->SetLineMode(capMode, joinMode, miterLimit); }
/** @brief Playback shim: routes SetPenSize opcode to the PictureIterator instance. */
static void	_SetPenSize(void *p, float size)														{ return ((PictureIterator *) p)->SetPenSize(size); }
/** @brief Playback shim: routes SetForeColor opcode to the PictureIterator instance. */
static void	_SetForeColor(void *p, rgb_color color)													{ return ((PictureIterator *) p)->SetForeColor(color); }
/** @brief Playback shim: routes SetBackColor opcode to the PictureIterator instance. */
static void	_SetBackColor(void *p, rgb_color color)													{ return ((PictureIterator *) p)->SetBackColor(color); }
/** @brief Playback shim: routes SetStipplePattern opcode to the PictureIterator instance. */
static void	_SetStipplePattern(void *p, pattern pat)												{ return ((PictureIterator *) p)->SetStipplePattern(pat); }
/** @brief Playback shim: routes SetScale opcode to the PictureIterator instance. */
static void	_SetScale(void *p, float scale)															{ return ((PictureIterator *) p)->SetScale(scale); }
/** @brief Playback shim: routes SetFontFamily opcode to the PictureIterator instance. */
static void	_SetFontFamily(void *p, char *family)													{ return ((PictureIterator *) p)->SetFontFamily(family); }
/** @brief Playback shim: routes SetFontStyle opcode to the PictureIterator instance. */
static void	_SetFontStyle(void *p, char *style)														{ return ((PictureIterator *) p)->SetFontStyle(style); }
/** @brief Playback shim: routes SetFontSpacing opcode to the PictureIterator instance. */
static void	_SetFontSpacing(void *p, int32 spacing)													{ return ((PictureIterator *) p)->SetFontSpacing(spacing); }
/** @brief Playback shim: routes SetFontSize opcode to the PictureIterator instance. */
static void	_SetFontSize(void *p, float size)														{ return ((PictureIterator *) p)->SetFontSize(size); }
/** @brief Playback shim: routes SetFontRotate opcode to the PictureIterator instance. */
static void	_SetFontRotate(void *p, float rotation)													{ return ((PictureIterator *) p)->SetFontRotate(rotation); }
/** @brief Playback shim: routes SetFontEncoding opcode to the PictureIterator instance. */
static void	_SetFontEncoding(void *p, int32 encoding)												{ return ((PictureIterator *) p)->SetFontEncoding(encoding); }
/** @brief Playback shim: routes SetFontFlags opcode to the PictureIterator instance. */
static void	_SetFontFlags(void *p, int32 flags)														{ return ((PictureIterator *) p)->SetFontFlags(flags); }
/** @brief Playback shim: routes SetFontShear opcode to the PictureIterator instance. */
static void	_SetFontShear(void *p, float shear)														{ return ((PictureIterator *) p)->SetFontShear(shear); }
/** @brief Playback shim: routes SetFontFace opcode to the PictureIterator instance. */
static void	_SetFontFace(void * p, int32 flags)														{ return ((PictureIterator *) p)->SetFontFace(flags); }

// undefined or undocumented operation handlers...
/** @brief Fallback shim for opcode 0 (no operation). */
static void	_op0(void * p)	{ return ((PictureIterator *) p)->Op(0); }
/** @brief Fallback shim for reserved opcode 19. */
static void	_op19(void * p)	{ return ((PictureIterator *) p)->Op(19); }
/** @brief Fallback shim for reserved opcode 45. */
static void	_op45(void * p)	{ return ((PictureIterator *) p)->Op(45); }
/** @brief Fallback shim for reserved opcode 47. */
static void	_op47(void * p)	{ return ((PictureIterator *) p)->Op(47); }
/** @brief Fallback shim for reserved opcode 48. */
static void	_op48(void * p)	{ return ((PictureIterator *) p)->Op(48); }
/** @brief Fallback shim for reserved opcode 49. */
static void	_op49(void * p)	{ return ((PictureIterator *) p)->Op(49); }

// Private Variables
// -----------------

/**
 * @brief Table of 50 playback handler function pointers indexed by BPicture opcode.
 *
 * Passed directly to BPicture::Play(). Each slot corresponds to one BPicture
 * drawing opcode; reserved or unknown opcodes map to the generic Op() shims.
 */
static void *
playbackHandlers[] = {
		(void *)_op0,					// 0	no operation
		(void *)_MovePenBy,				// 1	MovePenBy(void *user, BPoint delta)
		(void *)_StrokeLine,			// 2	StrokeLine(void *user, BPoint start, BPoint end)
		(void *)_StrokeRect,			// 3	StrokeRect(void *user, BRect rect)
		(void *)_FillRect,				// 4	FillRect(void *user, BRect rect)
		(void *)_StrokeRoundRect,		// 5	StrokeRoundRect(void *user, BRect rect, BPoint radii)
		(void *)_FillRoundRect,			// 6	FillRoundRect(void *user, BRect rect, BPoint radii)
		(void *)_StrokeBezier,			// 7	StrokeBezier(void *user, BPoint *control)
		(void *)_FillBezier,			// 8	FillBezier(void *user, BPoint *control)
		(void *)_StrokeArc,				// 9	StrokeArc(void *user, BPoint center, BPoint radii, float startTheta, float arcTheta)
		(void *)_FillArc,				// 10	FillArc(void *user, BPoint center, BPoint radii, float startTheta, float arcTheta)
		(void *)_StrokeEllipse,			// 11	StrokeEllipse(void *user, BPoint center, BPoint radii)
		(void *)_FillEllipse,			// 12	FillEllipse(void *user, BPoint center, BPoint radii)
		(void *)_StrokePolygon,			// 13	StrokePolygon(void *user, int32 numPoints, BPoint *points, bool isClosed)
		(void *)_FillPolygon,			// 14	FillPolygon(void *user, int32 numPoints, BPoint *points, bool isClosed)
		(void *)_StrokeShape,			// 15	StrokeShape(void *user, BShape *shape)
		(void *)_FillShape,				// 16	FillShape(void *user, BShape *shape)
		(void *)_DrawString,			// 17	DrawString(void *user, char *string, float deltax, float deltay)
		(void *)_DrawPixels,			// 18	DrawPixels(void *user, BRect src, BRect dest, int32 width, int32 height, int32 bytesPerRow, int32 pixelFormat, int32 flags, void *data)
		(void *)_op19,					// 19	*reserved*
		(void *)_SetClippingRects,		// 20	SetClippingRects(void *user, BRect *rects, uint32 numRects)
		(void *)_ClipToPicture,			// 21	ClipToPicture(void *user, BPicture *picture, BPoint pt, bool clip_to_inverse_picture)
		(void *)_PushState,				// 22	PushState(void *user)
		(void *)_PopState,				// 23	PopState(void *user)
		(void *)_EnterStateChange,		// 24	EnterStateChange(void *user)
		(void *)_ExitStateChange,		// 25	ExitStateChange(void *user)
		(void *)_EnterFontState,		// 26	EnterFontState(void *user)
		(void *)_ExitFontState,			// 27	ExitFontState(void *user)
		(void *)_SetOrigin,				// 28	SetOrigin(void *user, BPoint pt)
		(void *)_SetPenLocation,		// 29	SetPenLocation(void *user, BPoint pt)
		(void *)_SetDrawingMode,		// 30	SetDrawingMode(void *user, drawing_mode mode)
		(void *)_SetLineMode,			// 31	SetLineMode(void *user, cap_mode capMode, join_mode joinMode, float miterLimit)
		(void *)_SetPenSize,			// 32	SetPenSize(void *user, float size)
		(void *)_SetForeColor,			// 33	SetForeColor(void *user, rgb_color color)
		(void *)_SetBackColor,			// 34	SetBackColor(void *user, rgb_color color)
		(void *)_SetStipplePattern,		// 35	SetStipplePattern(void *user, pattern p)
		(void *)_SetScale,				// 36	SetScale(void *user, float scale)
		(void *)_SetFontFamily,			// 37	SetFontFamily(void *user, char *family)
		(void *)_SetFontStyle,			// 38	SetFontStyle(void *user, char *style)
		(void *)_SetFontSpacing,		// 39	SetFontSpacing(void *user, int32 spacing)
		(void *)_SetFontSize,			// 40	SetFontSize(void *user, float size)
		(void *)_SetFontRotate,			// 41	SetFontRotate(void *user, float rotation)
		(void *)_SetFontEncoding,		// 42	SetFontEncoding(void *user, int32 encoding)
		(void *)_SetFontFlags,			// 43	SetFontFlags(void *user, int32 flags)
		(void *)_SetFontShear,			// 44	SetFontShear(void *user, float shear)
		(void *)_op45,					// 45	*reserved*
		(void *)_SetFontFace,			// 46	SetFontFace(void *user, int32 flags)
		(void *)_op47,
		(void *)_op48,
		(void *)_op49,

		NULL
	};

/**
 * @brief Plays back all drawing commands recorded in \a picture.
 *
 * Passes the static playbackHandlers table and this object's pointer to
 * BPicture::Play(), which invokes each shim function for every opcode
 * encoded in the picture stream.
 *
 * @param picture  The BPicture whose recorded operations are to be iterated.
 */
void
PictureIterator::Iterate(BPicture* picture) {
	picture->Play(playbackHandlers, 50, this);
}
