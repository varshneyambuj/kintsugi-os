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
 * MIT License. Copyright 2008, Andrej Spielmann; 2008, Stephan Aßmus.
 *
 * Also incorporates work from the Anti-Grain Geometry library, used under
 * its permissive (MIT-style) license:
 *   Copyright 2002-2004 Maxim Shemanarev (http://www.antigrain.com)
 */

/** @file agg_scanline_u_subpix_avrg_filtering.h
    @brief Unpacked subpixel scanline with average-based filtering used to
           suppress color fringes when AVERAGE_BASED_SUBPIXEL_FILTERING is on. */

#ifndef AGG_SCANLINE_U_SUBPIX_AVRG_FILTERING_INCLUDED
#define AGG_SCANLINE_U_SUBPIX_AVRG_FILTERING_INCLUDED

#include "GlobalSubpixelSettings.h"

namespace agg
{
	//=======================================scanline_u8_subpix_avrg_filtering
	//------------------------------------------------------------------------
	/**
	 * @brief Unpacked subpixel scanline with average-based color-fringe filtering.
	 *
	 * Same layout as scanline_u8_subpix (three covers per pixel) but, in
	 * add_cell(), blends each cover toward the average of the three using
	 * gSubpixelAverageWeight, reducing color fringes at the cost of some
	 * subpixel sharpness.
	 */
	class scanline_u8_subpix_avrg_filtering
	{
	public:
		typedef scanline_u8_subpix_avrg_filtering self_type;
		typedef int8u		cover_type;
		typedef int16		coord_type;

		//--------------------------------------------------------------------
		struct span
		{
			coord_type	x;
			coord_type	len;
			cover_type* covers;
		};

		typedef span* iterator;
		typedef const span* const_iterator;

		//--------------------------------------------------------------------
		scanline_u8_subpix_avrg_filtering() :
			m_min_x(0),
			m_last_x(0x7FFFFFF0),
			m_cur_span(0)
		{}

		//--------------------------------------------------------------------
		void reset(int min_x, int max_x)
		{
			unsigned max_len = 3*(max_x - min_x + 2);
			if(max_len > m_spans.size())
			{
				m_spans.resize(max_len);
				m_covers.resize(max_len);
			}
			m_last_x   = 0x7FFFFFF0;
			m_min_x	   = min_x;
			m_cur_span = &m_spans[0];
		}

		//--------------------------------------------------------------------
		void add_cell(int x, unsigned cover1, unsigned cover2, unsigned cover3)
		{
		
		// NOTE stippi: My basic idea is that filtering tries to minimize colored
		// edges, but it does so by blurring the coverage values. This will also
		// affect neighboring pixels and add blur where there were perfectly sharp
		// edges. Andrej's method of filtering adds a special case for perfectly
		// sharp edges, but the drawback here is that there will be a visible
		// transition between blurred and non-blurred subpixels. I had the idea that
		// when simply fading the subpixels towards the plain gray-scale-aa values,
		// there must be a sweet spot for when colored edges become non-disturbing
		// and there is still a benefit of sharpness compared to straight gray-scale-
		// aa. The define above enables this method against the colored edges. My
		// method still has a drawback, since jaggies in diagonal lines will be more
		// visible again than with the filter method.

			unsigned char averageWeight = gSubpixelAverageWeight;
			unsigned char subpixelWeight = 255 - averageWeight;

			unsigned int coverR;
			unsigned int coverG;
			unsigned int coverB;
			
			unsigned int average = (cover1 + cover2 + cover3 + 2) / 3;
			
			coverR = (cover1 * subpixelWeight + average * averageWeight) >> 8;
			coverG = (cover2 * subpixelWeight + average * averageWeight) >> 8;
			coverB = (cover3 * subpixelWeight + average * averageWeight) >> 8;

			x -= m_min_x;
			m_covers[3 * x] = (cover_type)coverR;
			m_covers[3 * x + 1] = (cover_type)coverG;
			m_covers[3 * x + 2] = (cover_type)coverB;
			if(x == m_last_x + 1)
			{
				m_cur_span->len += 3;
			}
			else
			{
				m_cur_span++;
				m_cur_span->x	   = (coord_type)(x + m_min_x);
				m_cur_span->len	   = 3;
				m_cur_span->covers = &m_covers[3 * x];
			}
			m_last_x = x;
		}

		//--------------------------------------------------------------------
		void add_span(int x, unsigned len, unsigned cover)
		{
			x -= m_min_x;
			memset(&m_covers[3 * x], cover, 3 * len);
			if(x == m_last_x+1)
			{
				m_cur_span->len += 3 * (coord_type)len;
			}
			else
			{
				m_cur_span++;
				m_cur_span->x	   = (coord_type)(x + m_min_x);
				m_cur_span->len	   = 3 * (coord_type)len;
				m_cur_span->covers = &m_covers[3 * x];
			}
			m_last_x = x + len - 1;
		}

		//--------------------------------------------------------------------
		void finalize(int y)
		{
			m_y = y;
		}

		//--------------------------------------------------------------------
		void reset_spans()
		{
			m_last_x	= 0x7FFFFFF0;
			m_cur_span	= &m_spans[0];
		}

		//--------------------------------------------------------------------
		int		 y()		   const { return m_y; }
		unsigned num_spans()   const { return unsigned(m_cur_span - &m_spans[0]); }
		const_iterator begin() const { return &m_spans[1]; }
		iterator	   begin()		 { return &m_spans[1]; }

	private:
		scanline_u8_subpix_avrg_filtering(const self_type&);
		const self_type& operator = (const self_type&);

	private:
		int					  m_min_x;
		int					  m_last_x;
		int					  m_y;
		pod_array<cover_type> m_covers;
		pod_array<span>		  m_spans;
		span*				  m_cur_span;
	};

}

#endif

