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
 * MIT License. Copyright 2011, Haiku, Inc.
 */

/** @file CollapsingLayouter.h
    @brief Wrapper Layouter that hides unconstrained elements before delegating
           to an inner Simple, Complex, or OneElement layouter. */

#ifndef COLLAPSING_LAYOUTER_H
#define COLLAPSING_LAYOUTER_H

#include "Layouter.h"


namespace BPrivate {
namespace Layout {


/* This layouter wraps either a Compound, Simple or OneElement layouter, and
 * removes elements which have no constraints, or min/max constraints of
 * B_SIZE_UNSET. The child layouter is given only the constraints for the
 * remaining elements. When using the LayoutInfo of this layouter,
 * collapsed (removed) elements are given no space on screen.
 */
/**
 * @brief Layouter wrapper that collapses unconstrained slots to zero size.
 *
 * Constraints with min/max of B_SIZE_UNSET indicate empty cells; this
 * layouter strips those before forwarding the remaining constraints to a
 * dynamically chosen child layouter (OneElement, Simple, or Complex). The
 * accompanying ProxyLayoutInfo translates queries on the original element
 * indices back to the surviving ones, returning zero geometry for the
 * collapsed entries.
 */
class CollapsingLayouter : public Layouter {
public:
								CollapsingLayouter(int32 elementCount,
									float spacing);
	virtual						~CollapsingLayouter();

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
	class	ProxyLayoutInfo;
	struct	Constraint;
	struct	ElementInfo;

			void				_ValidateLayouter();
			Layouter*			_CreateLayouter();
			void				_DoCollapse();
			void				_AddConstraints();
			void				_AddConstraints(int32 position,
									const Constraint* c);
			void				_SetWeights();

			int32				fElementCount;
			ElementInfo*		fElements;
			int32				fValidElementCount;
			bool				fHaveMultiElementConstraints;
			float				fSpacing;
			Layouter*			fLayouter;
};

} // namespace Layout
} // namespace BPrivate

using BPrivate::Layout::CollapsingLayouter;

#endif // COLLAPSING_LAYOUTER_H
