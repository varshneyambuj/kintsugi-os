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
 * MIT License. Copyright 2015, Julian Harnath <julian.harnath@rwth-aachen.de>.
 * All rights reserved.
 */

/** @file Layer.h
    @brief Off-screen compositing layer with opacity and nested layer support. */

#ifndef LAYER_H
#define LAYER_H


#include "ServerPicture.h"

#include "IntPoint.h"


class AlphaMask;
class Canvas;
class UtilityBitmap;


/** @brief An off-screen rendering layer that records drawing operations and can
           be composited onto a Canvas with a given opacity level. */
class Layer : public ServerPicture {
public:
								Layer(uint8 opacity);
	virtual						~Layer();

			/** @brief Pushes a nested layer onto this layer's stack.
			    @param layer The Layer to push. */
			void				PushLayer(Layer* layer);

			/** @brief Pops and returns the topmost nested layer from the stack.
			    @return Pointer to the popped Layer, or NULL if the stack is empty. */
			Layer*				PopLayer();

			/** @brief Renders the recorded operations into a bitmap, cropped to actual content.
			    @param canvas The Canvas whose coordinate context to use for bounds determination.
			    @return Pointer to the rendered UtilityBitmap, or NULL on failure. */
			UtilityBitmap*		RenderToBitmap(Canvas* canvas);

			/** @brief Returns the offset of the content's top-left corner within the bitmap.
			    @return IntPoint offset. */
			IntPoint			LeftTopOffset() const;

			/** @brief Returns the opacity value used when compositing this layer.
			    @return Opacity in [0, 255] where 255 is fully opaque. */
			uint8				Opacity() const;

private:
			BRect				_DetermineBoundingBox(Canvas* canvas);
			UtilityBitmap*		_AllocateBitmap(const BRect& bounds);

private:
			uint8				fOpacity;
			IntPoint			fLeftTopOffset;
};


#endif // LAYER_H
