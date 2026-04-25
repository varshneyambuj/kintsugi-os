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
 * MIT License. Copyright 2005-2006, Stephan Aßmus; 2008, Andrej Spielmann;
 * 2015, Julian Harnath.
 *
 * Also incorporates work from the Anti-Grain Geometry library, used under
 * its permissive (MIT-style) license:
 *   Copyright 2002-2004 Maxim Shemanarev (http://www.antigrain.com)
 */

/** @file agg_renderer_region.h
    @brief BRegion-driven AGG base renderer; the multi-clip region equivalent
           of agg::renderer_mclip backed directly by a Haiku BRegion. */

#ifndef AGG_RENDERER_REGION_INCLUDED
#define AGG_RENDERER_REGION_INCLUDED

#include <Region.h>

#include "agg_basics.h"
#include "agg_array.h"
#include "agg_renderer_base.h"

namespace agg
{

	//----------------------------------------------------------renderer_region
	/**
	 * @brief AGG base renderer that iterates over the rectangles of a BRegion.
	 *
	 * Slightly-modified renderer_mclip that consumes a BRegion directly instead
	 * of an internal rectangle array. Each draw call iterates first_clip_box()/
	 * next_clip_box() across the region's rectangles, optionally translating
	 * coordinates by an (offset_x, offset_y) origin so the renderer can be
	 * shared across multiple BViews with different scroll positions.
	 */
	template<class PixelFormat> class renderer_region
	{
	public:
		typedef PixelFormat pixfmt_type;
		typedef typename pixfmt_type::color_type color_type;
		typedef renderer_base<pixfmt_type> base_ren_type;

		//--------------------------------------------------------------------
		renderer_region(pixfmt_type& ren) :
			m_ren(ren),
			m_region(NULL),
			m_curr_cb(0),
			m_bounds(m_ren.xmin(), m_ren.ymin(), m_ren.xmax(), m_ren.ymax()),
			m_offset_x(0),
			m_offset_y(0)
		{
		}

		//--------------------------------------------------------------------
		const pixfmt_type& ren() const { return m_ren.ren();  }
		pixfmt_type& ren() { return m_ren.ren();  }

		//--------------------------------------------------------------------
		unsigned width()  const { return m_ren.width();	 }
		unsigned height() const { return m_ren.height(); }

		//--------------------------------------------------------------------
		const rect_i& clip_box() const { return m_bounds; }
		int			  xmin()	 const { return translate_from_base_ren_x(
											m_ren.xmin()); }
		int			  ymin()	 const { return translate_from_base_ren_y(
											m_ren.ymin()); }
		int			  xmax()	 const { return translate_from_base_ren_x(
											m_ren.xmax()); }
		int			  ymax()	 const { return translate_from_base_ren_y(
											m_ren.ymax()); }

		//--------------------------------------------------------------------
		const rect_i& bounding_clip_box() const { return m_bounds;	  }
		int			  bounding_xmin()	  const { return m_bounds.x1; }
		int			  bounding_ymin()	  const { return m_bounds.y1; }
		int			  bounding_xmax()	  const { return m_bounds.x2; }
		int			  bounding_ymax()	  const { return m_bounds.y2; }

		//--------------------------------------------------------------------
		void first_clip_box()
		{
			m_curr_cb = 0;
			if(m_region && m_region->CountRects() > 0)
			{
				clipping_rect cb = m_region->RectAtInt(0);
				translate_to_base_ren(cb);
				m_ren.clip_box_naked(
					cb.left,
					cb.top,
					cb.right,
					cb.bottom);
			}
			else
				m_ren.clip_box_naked(0, 0, -1, -1);
		}

		//--------------------------------------------------------------------
		bool next_clip_box()
		{
			if(m_region && (int)(++m_curr_cb) < m_region->CountRects())
			{
				clipping_rect cb = m_region->RectAtInt(m_curr_cb);
				translate_to_base_ren(cb);
				m_ren.clip_box_naked(
					cb.left,
					cb.top,
					cb.right,
					cb.bottom);
				return true;
			}
			return false;
		}

		//--------------------------------------------------------------------
		void reset_clipping(bool visibility)
		{
			m_ren.reset_clipping(visibility);
			m_region = NULL;
			m_curr_cb = 0;
			m_bounds = m_ren.clip_box();
			translate_from_base_ren(m_bounds);
		}

		//--------------------------------------------------------------------
		void set_clipping_region(BRegion* region)
		{
			m_region = region;
			if (m_region) {
				clipping_rect r = m_region->FrameInt();
				if (r.left <= r.right && r.top <= r.bottom) {
					// clip rect_i to frame buffer bounds
					r.left = max_c(0, r.left);
					r.top = max_c(0, r.top);
					r.right = min_c((int)width() - 1, r.right);
					r.bottom = min_c((int)height() - 1, r.bottom);

					if(r.left < m_bounds.x1) m_bounds.x1 = r.left;
					if(r.top < m_bounds.y1) m_bounds.y1 = r.top;
					if(r.right > m_bounds.x2) m_bounds.x2 = r.right;
					if(r.bottom > m_bounds.y2) m_bounds.y2 = r.bottom;
				}
			}
		}

		//--------------------------------------------------------------------
		void set_offset(int offset_x, int offset_y)
		{
			m_offset_x = offset_x;
			m_offset_y = offset_y;

			if (m_region == NULL) {
				m_bounds = m_ren.clip_box();
				translate_from_base_ren(m_bounds);
			}
		}

		//--------------------------------------------------------------------
		void translate_to_base_ren_x(int& x)
		{
			x -= m_offset_x;
		}

		void translate_to_base_ren_y(int& y)
		{
			y -= m_offset_y;
		}

		void translate_to_base_ren(int& x, int&y)
		{
			x -= m_offset_x;
			y -= m_offset_y;
		}

		void translate_to_base_ren(clipping_rect& clip)
		{
			clip.left   -= m_offset_x;
			clip.right  -= m_offset_x;
			clip.top    -= m_offset_y;
			clip.bottom -= m_offset_y;
		}

		//--------------------------------------------------------------------
		int translate_from_base_ren_x(int x) const
		{
			return x + m_offset_x;
		}

		int translate_from_base_ren_y(int y) const
		{
			return y + m_offset_y;
		}

		void translate_from_base_ren(int& x, int& y)
		{
			x += m_offset_x;
			y += m_offset_y;
		}

		void translate_from_base_ren(rect_i& rect)
		{
			rect.x1 += m_offset_x;
			rect.x2 += m_offset_x;
			rect.y1 += m_offset_y;
			rect.y2 += m_offset_y;
		}

		//--------------------------------------------------------------------
		void clear(const color_type& c)
		{
			m_ren.clear(c);
		}

		//--------------------------------------------------------------------
		void copy_pixel(int x, int y, const color_type& c)
		{
			translate_to_base_ren(x, y);

			first_clip_box();
			do
			{
				if(m_ren.inbox(x, y))
				{
					m_ren.ren().copy_pixel(x, y, c);
					break;
				}
			}
			while(next_clip_box());
		}

		//--------------------------------------------------------------------
		void blend_pixel(int x, int y, const color_type& c, cover_type cover)
		{
			translate_to_base_ren(x, y);

			first_clip_box();
			do
			{
				if(m_ren.inbox(x, y))
				{
					m_ren.ren().blend_pixel(x, y, c, cover);
					break;
				}
			}
			while(next_clip_box());
		}

		//--------------------------------------------------------------------
		color_type pixel(int x, int y) const
		{
			translate_to_base_ren(x, y);

			first_clip_box();
			do
			{
				if(m_ren.inbox(x, y))
				{
					return m_ren.ren().pixel(x, y);
				}
			}
			while(next_clip_box());
			return color_type::no_color();
		}

		//--------------------------------------------------------------------
		void copy_hline(int x1, int y, int x2, const color_type& c)
		{
			translate_to_base_ren(x1, y);
			translate_to_base_ren_x(x2);

			first_clip_box();
			do
			{
				m_ren.copy_hline(x1, y, x2, c);
			}
			while(next_clip_box());
		}

		//--------------------------------------------------------------------
		void copy_vline(int x, int y1, int y2, const color_type& c)
		{
			translate_to_base_ren(x, y1);
			translate_to_base_ren_y(y2);

			first_clip_box();
			do
			{
				m_ren.copy_vline(x, y1, y2, c);
			}
			while(next_clip_box());
		}

		//--------------------------------------------------------------------
		void blend_hline(int x1, int y, int x2,
						 const color_type& c, cover_type cover)
		{
			translate_to_base_ren(x1, y);
			translate_to_base_ren_x(x2);

			first_clip_box();
			do
			{
				m_ren.blend_hline(x1, y, x2, c, cover);
			}
			while(next_clip_box());
		}

		//--------------------------------------------------------------------
		void blend_vline(int x, int y1, int y2,
						 const color_type& c, cover_type cover)
		{
			translate_to_base_ren(x, y1);
			translate_to_base_ren_y(y2);

			first_clip_box();
			do
			{
				m_ren.blend_vline(x, y1, y2, c, cover);
			}
			while(next_clip_box());
		}

		//--------------------------------------------------------------------
		void copy_bar(int x1, int y1, int x2, int y2, const color_type& c)
		{
			translate_to_base_ren(x1, y1);
			translate_to_base_ren(x2, y2);

			first_clip_box();
			do
			{
				m_ren.copy_bar(x1, y1, x2, y2, c);
			}
			while(next_clip_box());
		}

		//--------------------------------------------------------------------
		void blend_bar(int x1, int y1, int x2, int y2,
					   const color_type& c, cover_type cover)
		{
			translate_to_base_ren(x1, y1);
			translate_to_base_ren(x2, y2);

			first_clip_box();
			do
			{
				m_ren.blend_bar(x1, y1, x2, y2, c, cover);
			}
			while(next_clip_box());
		}


		//--------------------------------------------------------------------
		void blend_solid_hspan(int x, int y, int len,
							   const color_type& c, const cover_type* covers)
		{
			translate_to_base_ren(x, y);

			first_clip_box();
			do
			{
				m_ren.blend_solid_hspan(x, y, len, c, covers);
			}
			while(next_clip_box());
		}

		//--------------------------------------------------------------------
		void blend_solid_hspan_subpix(int x, int y, int len,
							   const color_type& c, const cover_type* covers)
		{
			translate_to_base_ren(x, y);

			first_clip_box();
			do
			{
				m_ren.blend_solid_hspan_subpix(x, y, len, c, covers);
			}
			while(next_clip_box());
		}

		//--------------------------------------------------------------------
		void blend_solid_vspan(int x, int y, int len,
							   const color_type& c, const cover_type* covers)
		{
			translate_to_base_ren(x, y);

			first_clip_box();
			do
			{
				m_ren.blend_solid_vspan(x, y, len, c, covers);
			}
			while(next_clip_box());
		}

		//--------------------------------------------------------------------
		void blend_color_hspan(int x, int y, int len,
							   const color_type* colors,
							   const cover_type* covers,
							   cover_type cover = cover_full)
		{
			translate_to_base_ren(x, y);

			first_clip_box();
			do
			{
				m_ren.blend_color_hspan(x, y, len, colors, covers, cover);
			}
			while(next_clip_box());
		}

		//--------------------------------------------------------------------
		void blend_color_vspan(int x, int y, int len,
							   const color_type* colors,
							   const cover_type* covers,
							   cover_type cover = cover_full)
		{
			translate_to_base_ren(x, y);

			first_clip_box();
			do
			{
				m_ren.blend_color_hspan(x, y, len, colors, covers, cover);
			}
			while(next_clip_box());
		}

		//--------------------------------------------------------------------
		void blend_color_hspan_no_clip(int x, int y, int len,
									   const color_type* colors,
									   const cover_type* covers,
									   cover_type cover = cover_full)
		{
			translate_to_base_ren(x, y);
			m_ren.blend_color_hspan_no_clip(x, y, len, colors, covers, cover);
		}

		//--------------------------------------------------------------------
		void blend_color_vspan_no_clip(int x, int y, int len,
									   const color_type* colors,
									   const cover_type* covers,
									   cover_type cover = cover_full)
		{
			translate_to_base_ren(x, y);
			m_ren.blend_color_vspan_no_clip(x, y, len, colors, covers, cover);
		}

		//--------------------------------------------------------------------
		void copy_from(const rendering_buffer& from,
					   const rect_i* rc=0,
					   int x_to=0,
					   int y_to=0)
		{
			translate_to_base_ren(x_to, y_to);
			first_clip_box();
			do
			{
				m_ren.copy_from(from, rc, x_to, y_to);
			}
			while(next_clip_box());
		}

	private:
		renderer_region(const renderer_region<PixelFormat>&);
		const renderer_region<PixelFormat>&
			operator = (const renderer_region<PixelFormat>&);

		base_ren_type	   m_ren;
		BRegion*		   m_region;
		unsigned		   m_curr_cb;
		rect_i			   m_bounds;

		int				   m_offset_x;
		int				   m_offset_y;
	};


}

#endif
