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
 *   Copyright 2005-2009, Stephan Aßmus <superstippi@gmx.de>.
 *   Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>.
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file AGGTextRenderer.cpp
 * @brief AGG-based glyph rasterizer used by the Painter for text rendering.
 *
 * Connects FreeType glyph caches (FontCacheEntry) to the AGG scanline
 * pipeline so UTF-8 strings can be drawn through Painter's solid, binary,
 * subpixel and alpha-masked renderers. Supports rotated/sheared fonts via
 * an embedded transform and falls back to AGG vector outlines when the
 * combined view + embedded transform requires it.
 *
 * @see Painter, GlyphLayoutEngine, FontCacheEntry
 */


#include "AGGTextRenderer.h"

#include <agg_basics.h>
#include <agg_bounding_rect.h>
#include <agg_conv_segmentator.h>
#include <agg_conv_stroke.h>
#include <agg_conv_transform.h>
#include <agg_path_storage.h>
#include <agg_scanline_boolean_algebra.h>
#include <agg_trans_affine.h>

#include <math.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

/** @brief When non-zero, draws each glyph's bounding rectangle for debugging. */
#define SHOW_GLYPH_BOUNDS 0

#include "GlobalSubpixelSettings.h"
#include "GlyphLayoutEngine.h"
#include "IntRect.h"


/**
 * @brief Constructs the renderer and wires it to a Painter's AGG pipeline.
 *
 * Stores references to the renderers, scanlines and rasterizers owned by
 * the host Painter, and initializes the curve and contour pipeline used for
 * vector glyph outlines.
 *
 * @param subpixRenderer    Subpixel-aware scanline renderer.
 * @param solidRenderer     Solid-color AA scanline renderer.
 * @param binRenderer       Binary (no-AA) renderer for mono glyphs.
 * @param scanline          Unpacked scanline used by AA glyphs.
 * @param subpixScanline    Unpacked subpixel scanline.
 * @param subpixRasterizer  Subpixel-aware rasterizer (3x horizontal cells).
 * @param maskedScanline    Optional masked scanline for ClipToPicture; may be NULL.
 * @param viewTransformation View-space affine transform shared with the Painter.
 */
AGGTextRenderer::AGGTextRenderer(renderer_subpix_type& subpixRenderer,
		renderer_type& solidRenderer, renderer_bin_type& binRenderer,
		scanline_unpacked_type& scanline,
		scanline_unpacked_subpix_type& subpixScanline,
		rasterizer_subpix_type& subpixRasterizer,
		scanline_unpacked_masked_type*& maskedScanline,
		agg::trans_affine& viewTransformation)
	:
	fPathAdaptor(),
	fGray8Adaptor(),
	fGray8Scanline(),
	fMonoAdaptor(),
	fMonoScanline(),

	fCurves(fPathAdaptor),
	fContour(fCurves),

	fSolidRenderer(solidRenderer),
	fBinRenderer(binRenderer),
	fSubpixRenderer(subpixRenderer),
	fScanline(scanline),
	fSubpixScanline(subpixScanline),
	fSubpixRasterizer(subpixRasterizer),
	fMaskedScanline(maskedScanline),

	fRasterizer(),

	fHinted(true),
	fAntialias(true),
	fEmbeddedTransformation(),
	fViewTransformation(viewTransformation)
{
	fCurves.approximation_scale(2.0);
	fContour.auto_detect_orientation(false);
}


/**
 * @brief Destructor.
 */
AGGTextRenderer::~AGGTextRenderer()
{
}


/**
 * @brief Adopts a new ServerFont and recomputes the embedded transform.
 *
 * The embedded transform encodes the font's shear and rotation so that
 * subsequent RenderString() calls combine view-space and font-space
 * transforms in a single matrix pipeline. Also updates the contour width
 * used for synthetic bold (false-bold) rendering.
 *
 * @param font ServerFont describing family, style, size, shear, rotation
 *        and false-bold width.
 */
void
AGGTextRenderer::SetFont(const ServerFont& font)
{
	fFont = font;

	// construct an embedded transformation (rotate & shear)
	fEmbeddedTransformation.Reset();
	fEmbeddedTransformation.ShearBy(B_ORIGIN,
		(90.0 - font.Shear()) * M_PI / 180.0, 0.0);
	fEmbeddedTransformation.RotateBy(B_ORIGIN,
		-font.Rotation() * M_PI / 180.0);

	fContour.width(font.FalseBoldWidth() * 2.0);
}


/**
 * @brief Enables or disables glyph hinting.
 *
 * @param hinting True to enable hinting, false to disable it.
 */
void
AGGTextRenderer::SetHinting(bool hinting)
{
	fHinted = hinting;
}


/**
 * @brief Enables or disables anti-aliased glyph rendering.
 *
 * Adjusts the rasterizer's gamma so disabled-AA produces a hard threshold
 * (binary glyphs) and enabled-AA uses a linear gamma curve.
 *
 * @param antialiasing True to enable AA, false for binary rendering.
 */
void
AGGTextRenderer::SetAntialiasing(bool antialiasing)
{
	if (fAntialias != antialiasing) {
		fAntialias = antialiasing;
		// NOTE: The fSubpixRasterizer is not used when anti-aliasing is
		// disbaled.
		if (!fAntialias)
			fRasterizer.gamma(agg::gamma_threshold(0.5));
		else
			fRasterizer.gamma(agg::gamma_power(1.0));
	}
}


/** @brief AGG transform pipeline: curve-converted glyph outline + Transformable. */
typedef agg::conv_transform<FontCacheEntry::CurveConverter, Transformable>
	conv_font_trans_type;

/** @brief AGG transform pipeline: contour-expanded glyph outline + Transformable. */
typedef agg::conv_transform<FontCacheEntry::ContourConverter, Transformable>
	conv_font_contour_trans_type;



/**
 * @brief Per-string visitor invoked by GlyphLayoutEngine for each glyph.
 *
 * Tracks the running bounding box, decides between bitmap and vector glyph
 * rendering, and dispatches into the appropriate AGG renderer (mono, gray8,
 * subpixel or vector). When the layout pass finishes, Finish() flushes any
 * pending vector spans and draws underline/strikeout decorations if the
 * font face requests them.
 */
class AGGTextRenderer::StringRenderer {
public:
	/**
	 * @brief Constructs the visitor for one RenderString() call.
	 *
	 * @param clippingFrame      Pixel-aligned clip box.
	 * @param dryRun             When true, only computes bounds (no pixels written).
	 * @param transformedGlyph   Transformed curve-pipeline outline source.
	 * @param transformedContour Transformed contour-pipeline outline source.
	 * @param transform          Combined view + embedded transform.
	 * @param transformOffset    Pre-transformed origin offset for bitmap glyphs.
	 * @param nextCharPos        Optional out parameter receiving the pen position.
	 * @param renderer           Owning AGGTextRenderer providing the AGG state.
	 */
	StringRenderer(const IntRect& clippingFrame, bool dryRun,
			FontCacheEntry::TransformedOutline& transformedGlyph,
			FontCacheEntry::TransformedContourOutline& transformedContour,
			const Transformable& transform,
			const BPoint& transformOffset,
			BPoint* nextCharPos,
			AGGTextRenderer& renderer)
		:
		fTransform(transform),
		fTransformOffset(transformOffset),
		fClippingFrame(clippingFrame),
		fDryRun(dryRun),
		fVector(false),
		fBounds(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN),
		fNextCharPos(nextCharPos),

		fTransformedGlyph(transformedGlyph),
		fTransformedContour(transformedContour),

		fRenderer(renderer)
	{
		fSubpixelAntiAliased = gSubpixelAntialiasing && fRenderer.Antialiasing();
	}

	/**
	 * @brief Returns true when the layout requires the vector glyph path.
	 *
	 * Vector rendering is needed when the transform is non-translational or
	 * subpixel rendering is paired with an alpha mask.
	 */
	bool NeedsVector()
	{
		return !fTransform.IsTranslationOnly()
			|| (fSubpixelAntiAliased && fRenderer.fMaskedScanline != NULL);
	}

	/** @brief Resets the AA and subpixel rasterizers before a glyph run. */
	void Start()
	{
		fRenderer.fRasterizer.reset();
		fRenderer.fSubpixRasterizer.reset();
	}

	/**
	 * @brief Flushes vector glyph spans and draws underline/strikeout if requested.
	 *
	 * @param x Pen x at the end of the run, in untransformed space.
	 * @param y Pen y at the end of the run, in untransformed space.
	 */
	void Finish(double x, double y)
	{
		if (fVector) {
			if (fRenderer.fMaskedScanline != NULL) {
				agg::render_scanlines(fRenderer.fRasterizer,
					*fRenderer.fMaskedScanline, fRenderer.fSolidRenderer);
			} else if (fSubpixelAntiAliased) {
				agg::render_scanlines(fRenderer.fSubpixRasterizer,
					fRenderer.fSubpixScanline, fRenderer.fSubpixRenderer);
			} else {
				agg::render_scanlines(fRenderer.fRasterizer,
					fRenderer.fScanline, fRenderer.fSolidRenderer);
			}
		}

		if (!fDryRun) {
			if ((fRenderer.fFont.Face() & B_UNDERSCORE_FACE) != 0)
				_DrawHorizontalLine(y + 2);

			if ((fRenderer.fFont.Face() & B_STRIKEOUT_FACE) != 0) {
				font_height fontHeight;
				fRenderer.fFont.GetHeight(fontHeight);
				_DrawHorizontalLine(y - (fontHeight.ascent + fontHeight.descent) / 4);
			}
		}

		if (fNextCharPos) {
			fNextCharPos->x = x;
			fNextCharPos->y = y;
			fTransform.Transform(fNextCharPos);
		}
	}

	/**
	 * @brief Visitor hook for code points that have no glyph data (zero-width).
	 *
	 * No-op; preserved to satisfy the GlyphLayoutEngine visitor interface.
	 */
	void ConsumeEmptyGlyph(int32 index, uint32 charCode, double x, double y)
	{
	}

	/**
	 * @brief Renders or measures a single glyph.
	 *
	 * Updates the running bounding box and, unless this is a dry run,
	 * dispatches to the AGG renderer that matches the glyph data type.
	 *
	 * @param index    Glyph index inside the laid-out string.
	 * @param charCode Unicode code point.
	 * @param glyph    Cached glyph (bounds + bitmap/outline payload).
	 * @param entry    Owning FontCacheEntry for adaptor initialization.
	 * @param x        Pen x for the glyph in untransformed space.
	 * @param y        Pen y for the glyph in untransformed space.
	 * @param advanceX Horizontal advance after this glyph.
	 * @param advanceY Vertical advance after this glyph.
	 * @return Always true (visitor never aborts the run).
	 */
	bool ConsumeGlyph(int32 index, uint32 charCode, const GlyphCache* glyph,
		FontCacheEntry* entry, double x, double y, double advanceX,
			double advanceY)
	{
		// "glyphBounds" is the bounds of the glyph transformed
		// by the x y location of the glyph along the base line,
		// it is therefor yet "untransformed" in case there is an
		// embedded transformation.
		const agg::rect_i& r = glyph->bounds;
		if (!r.is_valid())
			return true;
		IntRect glyphBounds(int32(r.x1 + x), int32(r.y1 + y - 1),
			int32(r.x2 + x + 1), int32(r.y2 + y + 1));
			// NOTE: "-1"/"+1" converts the glyph bounding box from pixel
			// indices to pixel area coordinates

		// track bounding box
		fBounds = fBounds | glyphBounds;

		// render the glyph if this is not a dry run
		if (!fDryRun) {
			// init the fontmanager's embedded adaptors
			// NOTE: The initialization for the "location" of
			// the glyph is different depending on whether we
			// deal with non-(rotated/sheared) text, in which
			// case we have a native FT bitmap. For rotated or
			// sheared text, we use AGG vector outlines and
			// a transformation pipeline, which will be applied
			// _after_ we retrieve the outline, and that's why
			// we simply pass x and y, which are untransformed.

			// "glyphBounds" is now transformed into screen coords
			// in order to stop drawing when we are already outside
			// of the clipping frame
			if (glyph->data_type != glyph_data_outline) {
				// we cannot use the transformation pipeline
				double transformedX = x + fTransformOffset.x;
				double transformedY = y + fTransformOffset.y;
				entry->InitAdaptors(glyph, transformedX, transformedY,
					fRenderer.fMonoAdaptor,
					fRenderer.fGray8Adaptor,
					fRenderer.fPathAdaptor);

				glyphBounds.OffsetBy(fTransformOffset);
			} else {
				entry->InitAdaptors(glyph, x, y,
					fRenderer.fMonoAdaptor,
					fRenderer.fGray8Adaptor,
					fRenderer.fPathAdaptor);

				int32 falseBoldWidth = (int32)fRenderer.fContour.width();
				if (falseBoldWidth != 0)
					glyphBounds.InsetBy(-falseBoldWidth, -falseBoldWidth);
				// TODO: not correct! this is later used for clipping,
				// but it doesn't get the rect right
				glyphBounds = fTransform.TransformBounds(glyphBounds);
			}

			if (fClippingFrame.Intersects(glyphBounds)) {
				switch (glyph->data_type) {
					case glyph_data_mono:
						agg::render_scanlines(fRenderer.fMonoAdaptor,
							fRenderer.fMonoScanline, fRenderer.fBinRenderer);
						break;

					case glyph_data_gray8:
						if (fRenderer.fMaskedScanline != NULL) {
							agg::render_scanlines(fRenderer.fGray8Adaptor,
								*fRenderer.fMaskedScanline,
								fRenderer.fSolidRenderer);
						} else {
							agg::render_scanlines(fRenderer.fGray8Adaptor,
								fRenderer.fGray8Scanline,
								fRenderer.fSolidRenderer);
						}
						break;

					case glyph_data_subpix:
						// TODO: Handle alpha mask (fRenderer.fMaskedScanline)
						//       and remove the grayscale workaround for that.
						agg::render_scanlines(fRenderer.fGray8Adaptor,
							fRenderer.fGray8Scanline,
							fRenderer.fSubpixRenderer);
						break;

					case glyph_data_outline: {
						fVector = true;
						if (fSubpixelAntiAliased && fRenderer.fMaskedScanline == NULL) {
							if (fRenderer.fContour.width() == 0.0) {
								fRenderer.fSubpixRasterizer.add_path(
									fTransformedGlyph);
							} else {
								fRenderer.fSubpixRasterizer.add_path(
									fTransformedContour);
							}
						} else {
							if (fRenderer.fContour.width() == 0.0) {
								fRenderer.fRasterizer.add_path(
									fTransformedGlyph);
							} else {
								fRenderer.fRasterizer.add_path(
									fTransformedContour);
							}
						}
#if SHOW_GLYPH_BOUNDS
	agg::path_storage p;
	p.move_to(glyphBounds.left + 0.5, glyphBounds.top + 0.5);
	p.line_to(glyphBounds.right + 0.5, glyphBounds.top + 0.5);
	p.line_to(glyphBounds.right + 0.5, glyphBounds.bottom + 0.5);
	p.line_to(glyphBounds.left + 0.5, glyphBounds.bottom + 0.5);
	p.close_polygon();
	agg::conv_stroke<agg::path_storage> ps(p);
	ps.width(1.0);
	if (fSubpixelAntiAliased && fRenderer.fMaskedScanline != NULL)
		fRenderer.fSubpixRasterizer.add_path(ps);
	else
		fRenderer.fRasterizer.add_path(ps);
#endif

						break;
					}
					default:
						break;
				}
			}
		}
		return true;
	}

	/** @brief Returns the running glyph bounding box accumulated so far. */
	IntRect Bounds() const
	{
		return fBounds;
	}

private:
	/**
	 * @brief Renders a single horizontal pen-stroke line at @a y.
	 *
	 * Used for underline and strikeout decorations. Honors the same
	 * masked/subpixel/AA dispatch as the glyph path so the line blends
	 * correctly with surrounding glyphs.
	 *
	 * @param y Y-coordinate of the line in untransformed space.
	 */
	void _DrawHorizontalLine(float y)
	{
		agg::path_storage path;
		IntRect bounds = fBounds;
		BPoint left(bounds.left, y);
		BPoint right(bounds.right, y);
		fTransform.Transform(&left);
		fTransform.Transform(&right);
		path.move_to(left.x + 0.5, left.y + 0.5);
		path.line_to(right.x + 0.5, right.y + 0.5);
		agg::conv_stroke<agg::path_storage> pathStorage(path);
		pathStorage.width(fRenderer.fFont.Size() / 12.0f);
		if (fRenderer.fMaskedScanline != NULL) {
			fRenderer.fRasterizer.add_path(pathStorage);
			agg::render_scanlines(fRenderer.fRasterizer,
				*fRenderer.fMaskedScanline, fRenderer.fSolidRenderer);
		} else if (fSubpixelAntiAliased) {
			fRenderer.fSubpixRasterizer.add_path(pathStorage);
			agg::render_scanlines(fRenderer.fSubpixRasterizer,
				fRenderer.fSubpixScanline, fRenderer.fSubpixRenderer);
		} else {
			fRenderer.fRasterizer.add_path(pathStorage);
			agg::render_scanlines(fRenderer.fRasterizer,
				fRenderer.fScanline, fRenderer.fSolidRenderer);
		}
	}

private:
	const Transformable& fTransform;
	const BPoint&		fTransformOffset;
	const IntRect&		fClippingFrame;
	bool				fDryRun;
	bool				fSubpixelAntiAliased;
	bool				fVector;
	IntRect				fBounds;
	BPoint*				fNextCharPos;

	FontCacheEntry::TransformedOutline& fTransformedGlyph;
	FontCacheEntry::TransformedContourOutline& fTransformedContour;
	AGGTextRenderer&	fRenderer;
};


/**
 * @brief Lays out and renders a UTF-8 string anchored at @a baseLine.
 *
 * Builds a combined view + embedded transform, plumbs it into the AGG
 * curve/contour pipelines, and lets GlyphLayoutEngine drive a StringRenderer
 * over each glyph. When @a dryRun is true the glyphs are only measured; no
 * pixels are written.
 *
 * @param string         UTF-8 input string.
 * @param length         Byte length of @a string.
 * @param baseLine       Pen origin in view space.
 * @param clippingFrame  Pixel-aligned clip rectangle.
 * @param dryRun         When true, computes bounds only.
 * @param nextCharPos    Optional out parameter for pen position after run.
 * @param delta          Optional escapement delta (per-character spacing).
 * @param cacheReference Optional cache reference to keep glyph cache live.
 * @return Bounding rectangle of the rendered string in view-space pixels.
 */
BRect
AGGTextRenderer::RenderString(const char* string, uint32 length,
	const BPoint& baseLine, const BRect& clippingFrame, bool dryRun,
	BPoint* nextCharPos, const escapement_delta* delta,
	FontCacheReference* cacheReference)
{
//printf("RenderString(\"%s\", length: %ld, dry: %d)\n", string, length, dryRun);

	Transformable transform(fEmbeddedTransformation);
	transform.TranslateBy(baseLine);
	transform *= fViewTransformation;

	fCurves.approximation_scale(transform.scale());

	// use a transformation behind the curves
	// (only if glyph->data_type == agg::glyph_data_outline)
	// in the pipeline for the rasterizer
	FontCacheEntry::TransformedOutline
		transformedOutline(fCurves, transform);
	FontCacheEntry::TransformedContourOutline
		transformedContourOutline(fContour, transform);

	// for when we bypass the transformation pipeline
	BPoint transformOffset(0.0, 0.0);
	transform.Transform(&transformOffset);
	IntRect clippingIntFrame(clippingFrame);

	StringRenderer renderer(clippingIntFrame, dryRun, transformedOutline, transformedContourOutline,
		transform, transformOffset, nextCharPos, *this);

	GlyphLayoutEngine::LayoutGlyphs(renderer, fFont, string, length, INT32_MAX,
		delta, fFont.Spacing(), NULL, cacheReference);

	return transform.TransformBounds(renderer.Bounds());
}


/**
 * @brief Lays out and renders a UTF-8 string with explicit per-character offsets.
 *
 * Identical to the baseline overload except that pen positions for every
 * character are supplied directly via @a offsets, bypassing escapement-delta
 * spacing.
 *
 * @param string         UTF-8 input string.
 * @param length         Byte length of @a string.
 * @param offsets        Array of per-character pen positions in view space.
 * @param clippingFrame  Pixel-aligned clip rectangle.
 * @param dryRun         When true, computes bounds only.
 * @param nextCharPos    Optional out parameter for pen position after run.
 * @param cacheReference Optional cache reference to keep glyph cache live.
 * @return Bounding rectangle of the rendered string in view-space pixels.
 */
BRect
AGGTextRenderer::RenderString(const char* string, uint32 length,
	const BPoint* offsets, const BRect& clippingFrame, bool dryRun,
	BPoint* nextCharPos, FontCacheReference* cacheReference)
{
//printf("RenderString(\"%s\", length: %ld, dry: %d)\n", string, length, dryRun);

	Transformable transform(fEmbeddedTransformation);
	transform *= fViewTransformation;

	fCurves.approximation_scale(transform.scale());

	// use a transformation behind the curves
	// (only if glyph->data_type == agg::glyph_data_outline)
	// in the pipeline for the rasterizer
	FontCacheEntry::TransformedOutline
		transformedOutline(fCurves, transform);
	FontCacheEntry::TransformedContourOutline
		transformedContourOutline(fContour, transform);

	// for when we bypass the transformation pipeline
	BPoint transformOffset(0.0, 0.0);
	transform.Transform(&transformOffset);
	IntRect clippingIntFrame(clippingFrame);

	StringRenderer renderer(clippingIntFrame, dryRun, transformedOutline, transformedContourOutline,
		transform, transformOffset, nextCharPos, *this);

	GlyphLayoutEngine::LayoutGlyphs(renderer, fFont, string, length, INT32_MAX,
		NULL, fFont.Spacing(), offsets, cacheReference);

	return transform.TransformBounds(renderer.Bounds());
}
