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
 * MIT License. Copyright 2014, Haiku, Inc.
 *
 * Also incorporates work from the Anti-Grain Geometry library, used under
 * its permissive (MIT-style) license:
 *   Copyright 2002-2004 Maxim Shemanarev (http://www.antigrain.com)
 */

/** @file agg_clipped_alpha_mask.h
    @brief AGG alpha_mask_u8 variant that supports an offset and a configurable
           outside-cover value, used by Painter for ClipToPicture-style masking. */

#ifndef AGG_CLIPED_ALPHA_MASK_INCLUDED
#define AGG_CLIPED_ALPHA_MASK_INCLUDED


#include <agg_alpha_mask_u8.h>
#include <agg_rendering_buffer.h>


namespace agg
{
	/**
	 * @brief Alpha mask wrapping a rendering buffer with offset and out-of-bounds cover.
	 *
	 * Modified copy of agg::alpha_mask_u8 that lets callers translate the mask
	 * relative to the rendered geometry (m_xOffset/m_yOffset) and pick the
	 * coverage value (m_outside) returned for spans that fall outside the
	 * mask buffer. Used by the Painter when ClipToPicture pictures are smaller
	 * than the destination canvas.
	 */
	class clipped_alpha_mask
	{
		public:
			typedef int8u cover_type;
			enum cover_scale_e
			{
				cover_shift = 8,
				cover_none  = 0,
				cover_full  = 255
			};

			clipped_alpha_mask()
				: m_xOffset(0), m_yOffset(0), m_rbuf(0), m_outside(0) {}
			clipped_alpha_mask(rendering_buffer& rbuf)
				: m_xOffset(0), m_yOffset(0), m_rbuf(&rbuf), m_outside(0) {}

			void attach(rendering_buffer& rbuf)
			{
				m_rbuf = &rbuf;
			}

			void attach(rendering_buffer& rbuf, int x, int y, int8u outside)
			{
				m_rbuf = &rbuf;
				m_xOffset = x;
				m_yOffset = y;
				m_outside = outside;
			}

			void combine_hspan(int x, int y, cover_type* dst, int num_pix) const
			{
				int count = num_pix;
				cover_type* covers = dst;

				bool has_inside = _set_outside(x, y, covers, count);
				if (!has_inside)
					return;

				const int8u* mask = m_rbuf->row_ptr(y) + x * Step + Offset;
				do
				{
					*covers = (cover_type)((cover_full + (*covers) * (*mask))
						>> cover_shift);
					++covers;
					mask += Step;
				}
				while(--count);
			}

			void get_hspan(int x, int y, cover_type* dst, int num_pix) const
			{
				int count = num_pix;
				cover_type* covers = dst;

				bool has_inside = _set_outside(x, y, covers, count);
				if (!has_inside)
					return;

				const int8u* mask = m_rbuf->row_ptr(y) + x * Step + Offset;
				memcpy(covers, mask, count);
			}

		private:
			bool _set_outside(int& x, int& y, cover_type*& covers,
				int& count) const
			{
				x -= m_xOffset;
				y -= m_yOffset;

				int xmax = m_rbuf->width() - 1;
				int ymax = m_rbuf->height() - 1;

				int num_pix = count;
				cover_type* dst = covers;

				if(y < 0 || y > ymax)
				{
					memset(dst, m_outside, num_pix * sizeof(cover_type));
					return false;
				}

				if(x < 0)
				{
					count += x;
					if(count <= 0)
					{
						memset(dst, m_outside, num_pix * sizeof(cover_type));
						return false;
					}
					memset(covers, m_outside, -x * sizeof(cover_type));
					covers -= x;
					x = 0;
				}

				if(x + count > xmax)
				{
					int rest = x + count - xmax - 1;
					count -= rest;
					if(count <= 0)
					{
						memset(dst, m_outside, num_pix * sizeof(cover_type));
						return false;
					}
					memset(covers + count, m_outside, rest * sizeof(cover_type));
				}

				return true;
			}


		private:
			int m_xOffset;
			int m_yOffset;

			rendering_buffer* m_rbuf;
			int8u m_outside;

			static const int Step = 1;
			static const int Offset = 0;
	};
}


#endif
