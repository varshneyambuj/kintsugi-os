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
 * MIT License. Copyright 2005-2007, Stephan Aßmus; 2008, Andrej Spielmann;
 * 2015, Julian Harnath.
 */

/** @file BitmapPainter.h
    @brief Declares Painter::BitmapPainter, the dispatcher that picks the
           specialised bitmap-blit implementation for the current draw. */

#ifndef BITMAP_PAINTER_H
#define BITMAP_PAINTER_H

#include <AutoDeleter.h>

#include "Painter.h"


/**
 * @brief Inner helper of Painter that selects between the bilinear,
 *        nearest-neighbor, no-scale and generic bitmap-blit code paths
 *        based on the current drawing mode, color space, options and
 *        transform.
 */
class Painter::BitmapPainter {
public:

public:
								BitmapPainter(const Painter* painter,
									const ServerBitmap* bitmap,
									uint32 options);

			void				Draw(const BRect& sourceRect,
									const BRect& destinationRect);

private:
			bool				_DetermineTransform(
									BRect sourceRect,
									const BRect& destinationRect);

			bool				_HasScale();
			bool				_HasAffineTransform();
			bool				_HasAlphaMask();

			void				_ConvertColorSpace(ObjectDeleter<BBitmap>&
									convertedBitmapDeleter);

			template<typename sourcePixel>
			void				_TransparentMagicToAlpha(sourcePixel *buffer,
									uint32 width, uint32 height,
									uint32 sourceBytesPerRow,
									sourcePixel transparentMagic,
									BBitmap *output);

private:
			const Painter*			fPainter;
			status_t				fStatus;
			agg::rendering_buffer	fBitmap;
			BRect					fBitmapBounds;
			color_space				fColorSpace;
			uint32					fOptions;

			BRect					fDestinationRect;
			double					fScaleX;
			double					fScaleY;
			BPoint					fOffset;
};


#endif // BITMAP_PAINTER_H
