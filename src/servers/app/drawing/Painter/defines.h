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
 * MIT License. Copyright 2005-2006, Stephan Aßmus; 2008, Andrej Spielmann.
 *
 * Global definitions for the Painter framework, mainly the AGG pipeline
 * typedefs that wire up rasterizers, scanlines and renderers.
 */

/** @file defines.h
    @brief Painter-wide AGG pipeline typedefs (pixfmt, renderers, rasterizers,
           scanlines) shared by Painter, AGGTextRenderer and the bitmap path. */

#ifndef DEFINES_H
#define DEFINES_H

#include <agg_alpha_mask_u8.h>
#include <agg_rasterizer_outline.h>
#include <agg_rasterizer_outline_aa.h>
#include <agg_rasterizer_scanline_aa.h>
#include <agg_renderer_outline_aa.h>
#include <agg_renderer_primitives.h>
#include <agg_renderer_scanline.h>
#include <agg_scanline_bin.h>
#include <agg_scanline_p.h>
#include <agg_scanline_u.h>
#include <agg_span_allocator.h>
#include <agg_span_gradient.h>
#include <agg_span_interpolator_linear.h>
#include <agg_rendering_buffer.h>
#include <agg_trans_affine.h>

#include "agg_clipped_alpha_mask.h"
#include "agg_rasterizer_scanline_aa_subpix.h"
#include "agg_renderer_region.h"
#include "agg_renderer_scanline_subpix.h"
#include "agg_scanline_p_subpix.h"
#include "agg_scanline_p_subpix_avrg_filtering.h"
#include "agg_scanline_u_subpix.h"
#include "agg_scanline_u_subpix_avrg_filtering.h"

#include "GlobalSubpixelSettings.h"
#include "drawing_modes/PixelFormat.h"


/** @brief When non-zero, switches the Painter pipeline to aliased (no AA) drawing. */
#define ALIASED_DRAWING 0

	/** @brief Active 32-bit pixel format wrapping a PatternHandler-aware blender. */
	typedef PixelFormat											pixfmt;
	/** @brief BRegion-clipped base renderer that all Painter drawing flows through. */
	typedef agg::renderer_region<pixfmt>						renderer_base;

#if ALIASED_DRAWING
	typedef agg::renderer_primitives<renderer_base>				outline_renderer_type;
	typedef agg::rasterizer_outline<outline_renderer_type>		outline_rasterizer_type;

	typedef agg::scanline_bin									scanline_unpacked_type;
	typedef agg::scanline_bin									scanline_packed_type;
	typedef agg::renderer_scanline_bin_solid<renderer_base>		renderer_type;
#else
	/** @brief Anti-aliased outline renderer used for stroke primitives. */
	typedef agg::renderer_outline_aa<renderer_base>				outline_renderer_type;
	/** @brief Outline rasterizer that drives the AA outline renderer. */
	typedef agg::rasterizer_outline_aa<outline_renderer_type>	outline_rasterizer_type;

	/** @brief Unpacked 8-bit scanline used during glyph and text rasterization. */
	typedef agg::scanline_u8									scanline_unpacked_type;
	/** @brief Packed 8-bit scanline used for general fill/stroke output. */
	typedef agg::scanline_p8									scanline_packed_type;
#ifdef AVERAGE_BASED_SUBPIXEL_FILTERING
	typedef agg::scanline_p8_subpix_avrg_filtering				scanline_packed_subpix_type;
	typedef agg::scanline_u8_subpix_avrg_filtering				scanline_unpacked_subpix_type;
#else
	/** @brief Packed subpixel scanline, three covers per pixel. */
	typedef agg::scanline_p8_subpix								scanline_packed_subpix_type;
	/** @brief Unpacked subpixel scanline, three covers per pixel. */
	typedef agg::scanline_u8_subpix								scanline_unpacked_subpix_type;
#endif

	/** @brief Alpha-masked unpacked scanline used by ClipToPicture. */
	typedef agg::scanline_u8_am<agg::clipped_alpha_mask>		scanline_unpacked_masked_type;

	/** @brief Solid-color anti-aliased scanline renderer. */
	typedef agg::renderer_scanline_aa_solid<renderer_base>		renderer_type;
#endif // !ALIASED_DRAWING

	/** @brief Solid-color binary (no AA) scanline renderer; used for fast paths. */
	typedef agg::renderer_scanline_bin_solid<renderer_base>		renderer_bin_type;
	/** @brief Solid-color subpixel scanline renderer for LCD anti-aliasing. */
	typedef agg::renderer_scanline_subpix_solid<renderer_base>  renderer_subpix_type;

	/** @brief General-purpose AA scanline rasterizer. */
	typedef agg::rasterizer_scanline_aa<>						rasterizer_type;
	/** @brief Subpixel-aware AA scanline rasterizer (3x horizontal resolution). */
	typedef agg::rasterizer_scanline_aa_subpix<>				rasterizer_subpix_type;

#endif // DEFINES_H


