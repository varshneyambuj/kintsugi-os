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
 * MIT License. Copyright 2008, Andrej Spielmann.
 *
 * Also incorporates work from the Anti-Grain Geometry library, used under
 * its permissive (MIT-style) license:
 *   Copyright 2002-2004 Maxim Shemanarev (http://www.antigrain.com)
 */

/** @file agg_renderer_scanline_subpix.h
    @brief Solid-color subpixel scanline renderer; fans subpixel scanlines into
           a base renderer's blend_solid_hspan_subpix() entry point. */

#include <Region.h>

#include "agg_basics.h"
#include "agg_array.h"
#include "agg_renderer_base.h"
#include "agg_renderer_scanline.h"


namespace agg
{

	//============================================render_scanline_subpix_solid
	/**
	 * @brief Iterates a subpixel scanline and dispatches to a base renderer.
	 *
	 * For each span the function picks between the subpixel-aware
	 * blend_solid_hspan_subpix() (for AA spans) and the cheaper blend_hline()
	 * (for solid runs). Mirrors AGG's render_scanline_aa_solid for the
	 * 3-cover-per-pixel scanline layout.
	 */
	template<class Scanline, class BaseRenderer, class ColorT>
	void render_scanline_subpix_solid(const Scanline& sl,
								  BaseRenderer& ren,
								  const ColorT& color)
	{
		int y = sl.y();
		unsigned num_spans = sl.num_spans();
		typename Scanline::const_iterator span = sl.begin();

		for(;;)
		{
			int x = span->x;
			if(span->len > 0)
			{
				ren.blend_solid_hspan_subpix(x, y, (unsigned)span->len,
									  color,
									  span->covers);
			}
			else
			{
				ren.blend_hline(x, y, (unsigned)(x - (span->len / 3) - 1),
								color,
								*(span->covers));
			}
			if(--num_spans == 0) break;
			++span;
		}
	}

	//==========================================renderer_scanline_subpix_solid
	/**
	 * @brief Stateful solid-color subpixel renderer wrapping a base renderer.
	 *
	 * Holds a current color and forwards each scanline to
	 * render_scanline_subpix_solid(). Plays the same role as
	 * agg::renderer_scanline_aa_solid does for grayscale AA.
	 */
	template<class BaseRenderer> class renderer_scanline_subpix_solid
	{
	public:
		typedef BaseRenderer base_ren_type;
		typedef typename base_ren_type::color_type color_type;

		//--------------------------------------------------------------------
		renderer_scanline_subpix_solid() : m_ren(0) {}
		renderer_scanline_subpix_solid(base_ren_type& ren) : m_ren(&ren) {}
		void attach(base_ren_type& ren)
		{
			m_ren = &ren;
		}

		//--------------------------------------------------------------------
		void color(const color_type& c) { m_color = c; }
		const color_type& color() const { return m_color; }

		//--------------------------------------------------------------------
		void prepare() {}

		//--------------------------------------------------------------------
		template<class Scanline> void render(const Scanline& sl)
		{
			render_scanline_subpix_solid(sl, *m_ren, m_color);
		}

	private:
		base_ren_type* m_ren;
		color_type m_color;
	};
}
