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
 * MIT License. Copyright 2020, Kacper Kasper.
 * Also incorporates work Copyright 2002-2005 Maxim Shemanarev
 * (http://www.antigrain.com).
 */

/** @file AggCompOpAdapter.h
    @brief Template adapter that wires AGG's comp_op_rgba_* compositing
           operators into the PixelFormat blend_* function pointer ABI. */

#ifndef AGG_COMP_OP_ADAPTER_H
#define AGG_COMP_OP_ADAPTER_H

#include <stdio.h>

#include <agg_pixfmt_rgba.h>

#include "PatternHandler.h"


/**
 * @brief Adapts an AGG compositing op (CompOp) to the PixelFormat blend_*
 *        signature so that B_ALPHA_COMPOSITE_* Porter-Duff modes can plug
 *        directly into the function-pointer dispatch table.
 *
 * Provides static blend_pixel, blend_hline, blend_solid_hspan,
 * blend_solid_hspan_subpix, blend_solid_vspan and blend_color_hspan
 * implementations that read source colors from the bound PatternHandler
 * (or a per-pixel color array for blend_color_hspan) and forward them to
 * @c CompOp::blend_pix, which performs the actual Porter-Duff math.
 */
template<typename CompOp, typename RenBuf>
struct AggCompOpAdapter {
	typedef typename CompOp::color_type color_type;
	typedef typename CompOp::color_type::value_type value_type;

	/** @brief Composites a single pattern-sourced pixel at (@a x, @a y)
	           through @c CompOp with the given AA coverage. */
	static void
	blend_pixel(int x, int y,
				const color_type& c,
				uint8 cover, RenBuf* buffer,
				const PatternHandler* pattern)
	{
		value_type* p = buffer->row_ptr(y) + x * sizeof(color_type);
		rgb_color color = pattern->ColorAt(x, y);
		CompOp::blend_pix(p,
			color.red, color.green, color.blue, color.alpha, cover);
	}

	/** @brief Composites @a len pattern-sourced pixels along a horizontal
	           run starting at (@a x, @a y) with a single AA cover. */
	static void
	blend_hline(int x, int y,
				unsigned len,
				const color_type& c,
				uint8 cover, RenBuf* buffer,
				const PatternHandler* pattern)
	{
		value_type* p = buffer->row_ptr(y) + x * sizeof(color_type);
		do {
			rgb_color color = pattern->ColorAt(x, y);
			CompOp::blend_pix(p,
				color.red, color.green, color.blue, color.alpha, cover);
			x++;
			p += sizeof(color_type) / sizeof(value_type);
		} while(--len);
	}

	/** @brief Composites @a len pattern-sourced pixels along a horizontal
	           anti-aliased solid span using per-pixel coverage from
	           @a covers. */
	static void
	blend_solid_hspan(int x, int y,
					  unsigned len,
					  const color_type& c,
					  const uint8* covers, RenBuf* buffer,
					  const PatternHandler* pattern)
	{
		value_type* p = buffer->row_ptr(y) + x * sizeof(color_type);
		do {
			rgb_color color = pattern->ColorAt(x, y);
			CompOp::blend_pix(p,
				color.red, color.green, color.blue, color.alpha, *covers);
			covers++;
			p += sizeof(color_type) / sizeof(value_type);
			x++;
		} while(--len);
	}

	/** @brief Stub: subpixel solid hspan compositing is not implemented for
	           the B_ALPHA_COMPOSITE_* Porter-Duff family; logs a notice. */
	static void
	blend_solid_hspan_subpix(int x, int y,
							 unsigned len,
							 const color_type& c,
							 const uint8* covers, RenBuf* buffer,
							 const PatternHandler* pattern)
	{
		fprintf(stderr,
			"B_ALPHA_COMPOSITE_* subpixel drawing not implemented\n");
	}


	/** @brief Composites @a len pattern-sourced pixels along a vertical
	           anti-aliased solid span using per-pixel coverage from
	           @a covers. */
	static void
	blend_solid_vspan(int x, int y,
					  unsigned len,
					  const color_type& c,
					  const uint8* covers, RenBuf* buffer,
					  const PatternHandler* pattern)
	{
		value_type* p = buffer->row_ptr(y) + x * sizeof(color_type);
		do {
			rgb_color color = pattern->ColorAt(x, y);
			CompOp::blend_pix(p,
				color.red, color.green, color.blue, color.alpha, *covers);
			covers++;
			p += buffer->stride();
			y++;
		} while(--len);
	}

	/** @brief Composites @a len pixels along a horizontal span where each
	           pixel has its own color in @a colors and either per-pixel
	           coverage in @a covers or the constant @a cover. */
	static void
	blend_color_hspan(int x, int y,
					  unsigned len,
					  const color_type* colors,
					  const uint8* covers,
					  uint8 cover, RenBuf* buffer,
					  const PatternHandler* pattern)
	{
		value_type* p = buffer->row_ptr(y) + x * sizeof(color_type);
		do {
			CompOp::blend_pix(p,
				colors->r, colors->g, colors->b, colors->a,
				covers ? *covers++ : cover);
			p += sizeof(color_type) / sizeof(value_type);
			++colors;
		} while(--len);
	}
};


#endif // AGG_COMP_OP_ADAPTER_H
