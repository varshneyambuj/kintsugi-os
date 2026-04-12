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
 *   Copyright (c) 2002 Haiku.
 *   Author: Michael Pfeiffer
 *   Licensed under the MIT License.
 */


/**
 * @file PicturePrinter.cpp
 * @brief Debugging PictureIterator that prints a human-readable trace to stdout.
 *
 * PicturePrinter overrides every PictureIterator virtual method to emit an
 * indented, textual description of each BPicture opcode to standard output.
 * ShapePrinter is a helper that traces BShape iteration callbacks at one
 * additional level of indentation.
 *
 * @see PictureIterator, Template
 */


#include <stdio.h>

#include "PicturePrinter.h"

/**
 * @brief Constructs a PicturePrinter with an initial indentation level.
 *
 * @param indent  Number of two-space indentation units to prepend to each line.
 */
PicturePrinter::PicturePrinter(int indent)
	: fIndent(indent)
 {
 }

/**
 * @brief Prints a raw string token followed by a space.
 *
 * @param text  Null-terminated string to output.
 */
void PicturePrinter::Print(const char* text) {
	printf("%s ", text);
}

/**
 * @brief Prints a BPoint value as "(x, y) ".
 *
 * @param p  Pointer to the BPoint to format.
 */
void PicturePrinter::Print(BPoint* p) {
	printf("point (%f, %f) ", p->x, p->y);
}

/**
 * @brief Prints a BRect value as "[l: ... t: ... r: ... b: ...] ".
 *
 * @param r  Pointer to the BRect to format.
 */
void PicturePrinter::Print(BRect* r) {
	printf("rect [l: %f, t: %f, r: %f, b: %f] ", r->left, r->top, r->right, r->bottom);
}

/**
 * @brief Prints a numbered list of BPoint values, each on its own indented line.
 *
 * @param numPoints  Number of points in the array.
 * @param points     Array of BPoint values to print.
 */
void PicturePrinter::Print(int numPoints, BPoint* points) {
	for (int i = 0; i < numPoints; i ++) {
		Indent(1); printf("%d ", i); Print(&points[i]); Cr();
	}
}

/**
 * @brief Prints a numbered list of BRect values, each on its own indented line.
 *
 * @param numRects  Number of rects in the array.
 * @param rects     Array of BRect values to print.
 */
void PicturePrinter::Print(int numRects, BRect* rects) {
	for (int i = 0; i < numRects; i ++) {
		Indent(1); printf("%d ", i); Print(&rects[i]); Cr();
	}
}

/**
 * @brief Prints a BShape by iterating its path segments via ShapePrinter.
 *
 * @param shape  Pointer to the BShape to trace.
 */
void PicturePrinter::Print(BShape* shape) {
	printf("Shape %p\n", shape);
	ShapePrinter printer(this);
	printer.Iterate(shape);
}

/**
 * @brief Prints a label string followed by a float value.
 *
 * @param text  Descriptive label to prefix.
 * @param f     Float value to print.
 */
void PicturePrinter::Print(const char* text, float f) {
	printf("%s %f ", text, f);
}

/**
 * @brief Prints a label string followed by a BPoint value.
 *
 * @param text   Descriptive label to prefix.
 * @param point  Pointer to the BPoint to print.
 */
void PicturePrinter::Print(const char* text, BPoint* point) {
	Print(text); Print(point);
}

/**
 * @brief Prints an rgb_color as "color r: G g: G b: B".
 *
 * @param color  The color value to format.
 */
void PicturePrinter::Print(rgb_color color) {
	printf("color r: %d g: %d b: %d", color.red, color.green, color.blue);
}

/**
 * @brief Prints a single float value followed by a space.
 *
 * @param f  The float to print.
 */
void PicturePrinter::Print(float f) {
	printf("%f ", f);
}

/**
 * @brief Emits a newline character to terminate the current trace line.
 */
void PicturePrinter::Cr() {
	printf("\n");
}

/**
 * @brief Emits indentation whitespace for the current nesting level plus \a inc.
 *
 * Each indent unit is two spaces.
 *
 * @param inc  Additional temporary indent levels beyond the stored fIndent.
 */
void PicturePrinter::Indent(int inc) {
	for (int i = fIndent + inc; i > 0; i --) printf("  ");
}

/**
 * @brief Increases the stored indentation level by one unit.
 */
void PicturePrinter::IncIndent() {
	fIndent ++;
}

/**
 * @brief Decreases the stored indentation level by one unit.
 */
void PicturePrinter::DecIndent() {
	fIndent --;
}

/**
 * @brief Prints a trace line for an unknown or reserved BPicture opcode.
 *
 * @param number  The opcode number that was not recognised.
 */
void PicturePrinter::Op(int number) {
	Indent(); printf("Unknown operator %d\n", number); Cr();
}


/**
 * @brief Traces a MovePenBy drawing command.
 *
 * @param delta  The pen movement vector.
 */
void PicturePrinter::MovePenBy(BPoint delta) {
	Indent(); Print("MovePenBy"); Print(&delta); Cr();
}


/**
 * @brief Traces a StrokeLine drawing command.
 *
 * @param start  Starting point of the line.
 * @param end    Ending point of the line.
 */
void PicturePrinter::StrokeLine(BPoint start, BPoint end) {
	Indent(); Print("StrokeLine"); Print(&start); Print(&end); Cr();
}


/**
 * @brief Traces a StrokeRect drawing command.
 *
 * @param rect  The rectangle to stroke.
 */
void PicturePrinter::StrokeRect(BRect rect) {
	Indent(); Print("StrokeRect"); Print(&rect); Cr();
}


/**
 * @brief Traces a FillRect drawing command.
 *
 * @param rect  The rectangle to fill.
 */
void PicturePrinter::FillRect(BRect rect) {
	Indent(); Print("FillRect"); Print(&rect); Cr();
}


/**
 * @brief Traces a StrokeRoundRect drawing command.
 *
 * @param rect   The bounding rectangle.
 * @param radii  Corner radii as (x, y).
 */
void PicturePrinter::StrokeRoundRect(BRect rect, BPoint radii) {
	Indent(); Print("StrokeRoundRect"); Print(&rect); Print("radii", &radii); Cr();
}


/**
 * @brief Traces a FillRoundRect drawing command.
 *
 * @param rect   The bounding rectangle.
 * @param radii  Corner radii as (x, y).
 */
void PicturePrinter::FillRoundRect(BRect rect, BPoint radii) {
	Indent(); Print("FillRoundRect"); Print(&rect); Print("radii", &radii); Cr();
}


/**
 * @brief Traces a StrokeBezier drawing command.
 *
 * @param control  Array of four BPoint control points.
 */
void PicturePrinter::StrokeBezier(BPoint *control) {
	Indent(); Print("StrokeBezier"); Print(4, control); Cr();
}


/**
 * @brief Traces a FillBezier drawing command.
 *
 * @param control  Array of four BPoint control points.
 */
void PicturePrinter::FillBezier(BPoint *control) {
	Indent(); Print("FillBezier"); Print(4, control); Cr();
}


/**
 * @brief Traces a StrokeArc drawing command.
 *
 * @param center      Center of the ellipse defining the arc.
 * @param radii       Semi-axes of the ellipse.
 * @param startTheta  Starting angle in degrees.
 * @param arcTheta    Sweep angle in degrees.
 */
void PicturePrinter::StrokeArc(BPoint center, BPoint radii, float startTheta, float arcTheta) {
	Indent(); Print("StrokeArc center="); Print(&center); Print("radii="); Print(&radii); Print("arcTheta=", arcTheta); Cr();
}


/**
 * @brief Traces a FillArc drawing command.
 *
 * @param center      Center of the ellipse defining the arc.
 * @param radii       Semi-axes of the ellipse.
 * @param startTheta  Starting angle in degrees.
 * @param arcTheta    Sweep angle in degrees.
 */
void PicturePrinter::FillArc(BPoint center, BPoint radii, float startTheta, float arcTheta) {
	Indent(); Print("FillArc center="); Print(&center); Print("radii="); Print(&radii); Print("arcTheta=", arcTheta); Cr();
}


/**
 * @brief Traces a StrokeEllipse drawing command.
 *
 * @param center  Center of the ellipse.
 * @param radii   Semi-axes of the ellipse.
 */
void PicturePrinter::StrokeEllipse(BPoint center, BPoint radii) {
	Indent(); Print("StrokeEllipse center="); Print(&center); Print("radii="); Print(&radii); Cr();
}


/**
 * @brief Traces a FillEllipse drawing command.
 *
 * @param center  Center of the ellipse.
 * @param radii   Semi-axes of the ellipse.
 */
void PicturePrinter::FillEllipse(BPoint center, BPoint radii) {
	Indent(); Print("FillEllipse center="); Print(&center); Print("radii="); Print(&radii); Cr();
}


/**
 * @brief Traces a StrokePolygon drawing command.
 *
 * @param numPoints  Number of vertices.
 * @param points     Array of vertex coordinates.
 * @param isClosed   True if the polygon is closed (last point connects to first).
 */
void PicturePrinter::StrokePolygon(int32 numPoints, BPoint *points, bool isClosed) {
	Indent(); Print("StrokePolygon");
	printf("%s ", isClosed ? "closed" : "open"); Cr();
	Print(numPoints, points);
}


/**
 * @brief Traces a FillPolygon drawing command.
 *
 * @param numPoints  Number of vertices.
 * @param points     Array of vertex coordinates.
 * @param isClosed   True if the polygon is closed.
 */
void PicturePrinter::FillPolygon(int32 numPoints, BPoint *points, bool isClosed) {
	Indent(); Print("FillPolygon");
	printf("%s ", isClosed ? "closed" : "open"); Cr();
	Print(numPoints, points);
}


/**
 * @brief Traces a StrokeShape drawing command.
 *
 * @param shape  Pointer to the BShape to stroke.
 */
void PicturePrinter::StrokeShape(BShape *shape) {
	Indent(); Print("StrokeShape"); Print(shape); Cr();
}


/**
 * @brief Traces a FillShape drawing command.
 *
 * @param shape  Pointer to the BShape to fill.
 */
void PicturePrinter::FillShape(BShape *shape) {
	Indent(); Print("FillShape"); Print(shape); Cr();
}


/**
 * @brief Traces a DrawString drawing command.
 *
 * @param string                Null-terminated string to draw.
 * @param escapement_nospace    Per-character escapement for non-space characters.
 * @param escapement_space      Per-character escapement for space characters.
 */
void PicturePrinter::DrawString(char *string, float escapement_nospace, float escapement_space) {
	Indent(); Print("DrawString");
	Print("escapement_nospace", escapement_nospace);
	Print("escapement_space", escapement_space);
	Print("text:"); Print(string); Cr();
}


/**
 * @brief Traces a DrawPixels drawing command.
 *
 * @param src          Source rectangle within the pixel data.
 * @param dest         Destination rectangle on the drawing surface.
 * @param width        Width of the pixel buffer in pixels.
 * @param height       Height of the pixel buffer in pixels.
 * @param bytesPerRow  Row stride of the pixel buffer in bytes.
 * @param pixelFormat  Pixel format constant.
 * @param flags        Additional rendering flags.
 * @param data         Pointer to the raw pixel data.
 */
void PicturePrinter::DrawPixels(BRect src, BRect dest, int32 width, int32 height, int32 bytesPerRow, int32 pixelFormat, int32 flags, void *data) {
	Indent(); Print("DrawPixels"); Cr();
}


/**
 * @brief Traces a SetClippingRects drawing command.
 *
 * @param rects     Array of clipping rectangles.
 * @param numRects  Number of rectangles in the array.
 */
void PicturePrinter::SetClippingRects(BRect *rects, uint32 numRects) {
	Indent(); Print("SetClippingRects");
	if (numRects == 0) Print("none");
	Cr();
	Print(numRects, rects);
}


/**
 * @brief Traces a ClipToPicture or ClipToInversePicture drawing command.
 *
 * Recursively iterates \a picture at a deeper indentation level to show
 * the nested picture's contents.
 *
 * @param picture                  The BPicture used as the clip mask.
 * @param point                    Offset applied to the picture clip mask.
 * @param clip_to_inverse_picture  If true, the clip is inverted.
 */
void PicturePrinter::ClipToPicture(BPicture *picture, BPoint point, bool clip_to_inverse_picture) {
	Indent();
	Print(clip_to_inverse_picture ? "ClipToInversePicture" : "ClipToPicture");
	Print("point=", &point); Cr();
	PicturePrinter printer(fIndent+1);
	printer.Iterate(picture);
}


/**
 * @brief Traces a PushState drawing command and increases the indentation level.
 */
void PicturePrinter::PushState() {
	Indent(); Print("PushState"); Cr();
	IncIndent();
}


/**
 * @brief Decreases the indentation level and traces a PopState drawing command.
 */
void PicturePrinter::PopState() {
	DecIndent();
	Indent(); Print("PopState"); Cr();
}


/**
 * @brief Traces an EnterStateChange drawing command.
 */
void PicturePrinter::EnterStateChange() {
	Indent(); Print("EnterStateChange"); Cr();
}


/**
 * @brief Traces an ExitStateChange drawing command.
 */
void PicturePrinter::ExitStateChange() {
	Indent(); Print("ExitStateChange"); Cr();
}


/**
 * @brief Traces an EnterFontState drawing command.
 */
void PicturePrinter::EnterFontState() {
	Indent(); Print("EnterFontState"); Cr();
}


/**
 * @brief Traces an ExitFontState drawing command.
 */
void PicturePrinter::ExitFontState() {
	Indent(); Print("ExitFontState"); Cr();
}


/**
 * @brief Traces a SetOrigin drawing command.
 *
 * @param pt  The new origin point.
 */
void PicturePrinter::SetOrigin(BPoint pt) {
	Indent(); Print("SetOrigin"); Print(&pt); Cr();
}


/**
 * @brief Traces a SetPenLocation drawing command.
 *
 * @param pt  The new pen location.
 */
void PicturePrinter::SetPenLocation(BPoint pt) {
	Indent(); Print("SetPenLocation"); Print(&pt); Cr();
}


/**
 * @brief Traces a SetDrawingMode drawing command.
 *
 * Prints the symbolic name of the drawing mode constant.
 *
 * @param mode  The drawing_mode to activate.
 */
void PicturePrinter::SetDrawingMode(drawing_mode mode) {
	Indent(); Print("SetDrawingMode");
	switch (mode) {
		case B_OP_COPY: Print("B_OP_COPY"); break;
		case B_OP_OVER: Print("B_OP_OVER"); break;
		case B_OP_ERASE: Print("B_OP_ERASE"); break;
		case B_OP_INVERT: Print("B_OP_INVERT"); break;
		case B_OP_SELECT: Print("B_OP_SELECT"); break;
		case B_OP_ALPHA: Print("B_OP_ALPHA"); break;
		case B_OP_MIN: Print("B_OP_MIN"); break;
		case B_OP_MAX: Print("B_OP_MAX"); break;
		case B_OP_ADD: Print("B_OP_ADD"); break;
		case B_OP_SUBTRACT: Print("B_OP_SUBTRACT"); break;
		case B_OP_BLEND: Print("B_OP_BLEND"); break;
		default: Print("Unknown mode: ", (float)mode);
	}
	Cr();
}


/**
 * @brief Traces a SetLineMode drawing command.
 *
 * Prints symbolic names for the cap mode, join mode, and miter limit.
 *
 * @param capMode     The cap_mode to apply to line endpoints.
 * @param joinMode    The join_mode to apply at line intersections.
 * @param miterLimit  The miter limit for B_MITER_JOIN.
 */
void PicturePrinter::SetLineMode(cap_mode capMode, join_mode joinMode, float miterLimit) {
	Indent(); Print("SetLineMode");
	switch (capMode) {
		case B_BUTT_CAP:   Print("B_BUTT_CAP"); break;
		case B_ROUND_CAP:  Print("B_ROUND_CAP"); break;
		case B_SQUARE_CAP: Print("B_SQUARE_CAP"); break;
	}
	switch (joinMode) {
		case B_MITER_JOIN: Print("B_MITER_JOIN"); break;
		case B_ROUND_JOIN: Print("B_ROUND_JOIN"); break;
		case B_BUTT_JOIN: Print("B_BUTT_JOIN"); break;
		case B_SQUARE_JOIN: Print("B_SQUARE_JOIN"); break;
		case B_BEVEL_JOIN: Print("B_BEVEL_JOIN"); break;
	}
	Print("miterLimit", miterLimit);
	Cr();
}


/**
 * @brief Traces a SetPenSize drawing command.
 *
 * @param size  The new pen width in pixels.
 */
void PicturePrinter::SetPenSize(float size) {
	Indent(); Print("SetPenSize", size); Cr();
}


/**
 * @brief Traces a SetForeColor drawing command.
 *
 * @param color  The new foreground (high) color.
 */
void PicturePrinter::SetForeColor(rgb_color color) {
	Indent(); Print("SetForeColor"); Print(color); Cr();
}


/**
 * @brief Traces a SetBackColor drawing command.
 *
 * @param color  The new background (low) color.
 */
void PicturePrinter::SetBackColor(rgb_color color) {
	Indent(); Print("SetBackColor"); Print(color); Cr();
}

/**
 * @brief Compares two pattern structs byte-by-byte for equality.
 *
 * @param a  First pattern to compare.
 * @param b  Second pattern to compare.
 * @return true if all 8 bytes match, false otherwise.
 */
static bool compare(pattern a, pattern b) {
	for (int i = 0; i < 8; i ++) {
		if (a.data[i] != b.data[i]) return false;
	}
	return true;
}

/**
 * @brief Traces a SetStipplePattern drawing command.
 *
 * Prints a symbolic name for common patterns (B_SOLID_HIGH, B_SOLID_LOW,
 * B_MIXED_COLORS) or raw hex bytes for custom patterns.
 *
 * @param p  The pattern to set as the stipple.
 */
void PicturePrinter::SetStipplePattern(pattern p) {
	Indent(); Print("SetStipplePattern");
	if (compare(p, B_SOLID_HIGH)) Print("B_SOLID_HIGH");
	else if (compare(p, B_SOLID_LOW)) Print("B_SOLID_LOW");
	else if (compare(p, B_MIXED_COLORS)) Print("B_MIXED_COLORS");
	else {
		for (int i = 0; i < 8; i++) {
			printf("%2.2x ", (unsigned int)p.data[i]);
		}
	}
	Cr();
}


/**
 * @brief Traces a SetScale drawing command.
 *
 * @param scale  The new coordinate scale factor.
 */
void PicturePrinter::SetScale(float scale) {
	Indent(); Print("SetScale", scale); Cr();
}


/**
 * @brief Traces a SetFontFamily drawing command.
 *
 * @param family  Null-terminated font family name.
 */
void PicturePrinter::SetFontFamily(char *family) {
	Indent(); Print("SetFontFamily"); Print(family); Cr();
}


/**
 * @brief Traces a SetFontStyle drawing command.
 *
 * @param style  Null-terminated font style name (e.g. "Bold", "Italic").
 */
void PicturePrinter::SetFontStyle(char *style) {
	Indent(); Print("SetFontStyle"); Print(style); Cr();
}


/**
 * @brief Traces a SetFontSpacing drawing command.
 *
 * Prints a symbolic name for the spacing constant.
 *
 * @param spacing  One of the B_*_SPACING font spacing constants.
 */
void PicturePrinter::SetFontSpacing(int32 spacing) {
	Indent(); Print("SetFontSpacing");
	switch(spacing) {
		case B_CHAR_SPACING: Print("B_CHAR_SPACING"); break;
		case B_STRING_SPACING: Print("B_STRING_SPACING"); break;
		case B_BITMAP_SPACING: Print("B_BITMAP_SPACING"); break;
		case B_FIXED_SPACING: Print("B_FIXED_SPACING"); break;
		default: Print("Unknown: ", (float)spacing);
	}
	Cr();
}


/**
 * @brief Traces a SetFontSize drawing command.
 *
 * @param size  The new font size in points.
 */
void PicturePrinter::SetFontSize(float size) {
	Indent(); Print("SetFontSize", size); Cr();
}


/**
 * @brief Traces a SetFontRotate drawing command.
 *
 * @param rotation  The font rotation angle in degrees.
 */
void PicturePrinter::SetFontRotate(float rotation) {
	Indent(); Print("SetFontRotation", rotation); Cr();
}


/**
 * @brief Traces a SetFontEncoding drawing command.
 *
 * Prints the symbolic name of the character encoding constant.
 *
 * @param encoding  One of the B_*_ENCODING font encoding constants.
 */
void PicturePrinter::SetFontEncoding(int32 encoding) {
	Indent(); Print("SetFontEncoding");
	switch (encoding) {
		case B_UNICODE_UTF8: Print("B_UNICODE_UTF8"); break;
		case B_ISO_8859_1: Print("B_ISO_8859_1"); break;
		case B_ISO_8859_2: Print("B_ISO_8859_2"); break;
		case B_ISO_8859_3: Print("B_ISO_8859_3"); break;
		case B_ISO_8859_4: Print("B_ISO_8859_4"); break;
		case B_ISO_8859_5: Print("B_ISO_8859_5"); break;
		case B_ISO_8859_6: Print("B_ISO_8859_6"); break;
		case B_ISO_8859_7: Print("B_ISO_8859_7"); break;
		case B_ISO_8859_8: Print("B_ISO_8859_8"); break;
		case B_ISO_8859_9: Print("B_ISO_8859_9"); break;
		case B_ISO_8859_10: Print("B_ISO_8859_10"); break;
		case B_MACINTOSH_ROMAN: Print("B_MACINTOSH_ROMAN"); break;
		default: Print("Unknown:", (float)encoding);
	}
	Cr();
}

#define PRINT_FLAG(flag) \
  if (flags & flag) { f |= flag; Print(#flag); }

/**
 * @brief Traces a SetFontFlags drawing command.
 *
 * Prints symbolic names for each recognised flag bit and reports any
 * unrecognised bits numerically.
 *
 * @param flags  Bitfield of font flag constants (e.g. B_DISABLE_ANTIALIASING).
 */
void PicturePrinter::SetFontFlags(int32 flags) {
	Indent(); Print("SetFontFlags");
	int f = 0;
	if (flags == 0) Print("none set");
	PRINT_FLAG(B_DISABLE_ANTIALIASING);
	PRINT_FLAG(B_FORCE_ANTIALIASING);
	if (flags != f) printf("Unknown Additional Flags %" B_PRId32 "", flags & ~f);
	Cr();
}


/**
 * @brief Traces a SetFontShear drawing command.
 *
 * @param shear  The font shear angle in degrees (90 = upright).
 */
void PicturePrinter::SetFontShear(float shear) {
	Indent(); Print("SetFontShear", shear); Cr();
}


/**
 * @brief Traces a SetFontFace drawing command.
 *
 * Prints symbolic names for each recognised face bit and reports any
 * unrecognised bits numerically.
 *
 * @param flags  Bitfield of B_*_FACE font face constants.
 */
void PicturePrinter::SetFontFace(int32 flags) {
	Indent(); Print("SetFontFace");
	int32 f = 0;
	if (flags == 0) Print("none set");
	PRINT_FLAG(B_REGULAR_FACE);
	PRINT_FLAG(B_BOLD_FACE);
	PRINT_FLAG(B_ITALIC_FACE);
	PRINT_FLAG(B_NEGATIVE_FACE);
	PRINT_FLAG(B_OUTLINED_FACE);
	PRINT_FLAG(B_UNDERSCORE_FACE);
	PRINT_FLAG(B_STRIKEOUT_FACE);
	if (flags != f) printf("Unknown Additional Flags %" B_PRId32 "", flags & ~f);
	Cr();
}


// Implementation of ShapePrinter

/**
 * @brief Constructs a ShapePrinter that delegates trace output to \a printer.
 *
 * Increases the parent printer's indentation level so that shape sub-paths
 * are visually nested inside the enclosing picture trace.
 *
 * @param printer  The owning PicturePrinter to write trace output through.
 */
ShapePrinter::ShapePrinter(PicturePrinter* printer)
	: fPrinter(printer)
{
	fPrinter->IncIndent();
}

/**
 * @brief Destroys the ShapePrinter and restores the parent's indentation level.
 */
ShapePrinter::~ShapePrinter() {
	fPrinter->DecIndent();
}

/**
 * @brief BShapeIterator callback: traces one or more cubic Bezier segments.
 *
 * Prints each set of three control points at an additional indentation level.
 *
 * @param bezierCount  Number of cubic Bezier curves (each needs 3 control points).
 * @param control      Array of bezierCount * 3 BPoint control points.
 * @return B_OK.
 */
status_t
ShapePrinter::IterateBezierTo(int32 bezierCount, BPoint *control)
{
	fPrinter->Indent(); fPrinter->Print("BezierTo"); fPrinter->Cr();
	for (int32 i = 0; i < bezierCount; i++, control += 3) {
		fPrinter->Indent(1);
		fPrinter->Print(i / 3.0);
		fPrinter->Print(&control[0]);
		fPrinter->Print(&control[1]);
		fPrinter->Print(&control[2]);
		fPrinter->Cr();
	}
	return B_OK;
}

/**
 * @brief BShapeIterator callback: traces a Close sub-path command.
 *
 * @return B_OK.
 */
status_t
ShapePrinter::IterateClose(void)
{
	fPrinter->Indent(); fPrinter->Print("Close"); fPrinter->Cr();
	return B_OK;
}

/**
 * @brief BShapeIterator callback: traces one or more line segments.
 *
 * @param lineCount    Number of line endpoints.
 * @param linePoints   Array of lineCount destination BPoint values.
 * @return B_OK.
 */
status_t
ShapePrinter::IterateLineTo(int32 lineCount, BPoint *linePoints)
{
	fPrinter->Indent(); fPrinter->Print("LineTo"); fPrinter->Cr();
	BPoint *p = linePoints;
	for (int32 i = 0; i < lineCount; i++) {
		fPrinter->Indent(1); fPrinter->Print(p); fPrinter->Cr();
		p++;
	}
	return B_OK;
}

/**
 * @brief BShapeIterator callback: traces a MoveTo sub-path command.
 *
 * @param point  The destination point of the move.
 * @return B_OK.
 */
status_t
ShapePrinter::IterateMoveTo(BPoint *point)
{
	fPrinter->Indent(); fPrinter->Print("MoveTo", point); fPrinter->Cr();
	return B_OK;
}
