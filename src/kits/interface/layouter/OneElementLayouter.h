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
 * MIT License. Copyright 2006, Haiku, Inc.
 */

/** @file OneElementLayouter.h
    @brief Trivial Layouter implementation for the degenerate one-element case. */

#ifndef	ONE_ELEMENT_LAYOUTER_H
#define	ONE_ELEMENT_LAYOUTER_H

#include "Layouter.h"

namespace BPrivate {
namespace Layout {

/**
 * @brief Degenerate Layouter for a single element.
 *
 * Used when a layout has exactly one slot to fill. Constraint and weight
 * inputs collapse to plain min/max/preferred floats; layout assigns the
 * element the entire available size.
 */
class OneElementLayouter : public Layouter {
public:
								OneElementLayouter();
	virtual						~OneElementLayouter();

	virtual	void				AddConstraints(int32 element, int32 length,
									float min, float max, float preferred);
	virtual	void				SetWeight(int32 element, float weight);

	virtual	float				MinSize();
	virtual	float				MaxSize();
	virtual	float				PreferredSize();

	virtual	LayoutInfo*			CreateLayoutInfo();

	virtual	void				Layout(LayoutInfo* layoutInfo, float size);

	virtual	Layouter*			CloneLayouter();

private:
			class MyLayoutInfo;

			float				fMin;
			float				fMax;
			float				fPreferred;
};

}	// namespace Layout
}	// namespace BPrivate

using BPrivate::Layout::OneElementLayouter;

#endif	// ONE_ELEMENT_LAYOUTER_H
