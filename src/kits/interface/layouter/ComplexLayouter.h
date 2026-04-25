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
 * MIT License. Copyright 2007, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 */

/** @file ComplexLayouter.h
    @brief Layouter that handles constraints spanning multiple elements (used by
           BGridLayout); falls back on LayoutOptimizer for the hard cases. */

#ifndef COMPLEX_LAYOUTER_H
#define COMPLEX_LAYOUTER_H

#include <List.h>

#include "Layouter.h"


namespace BPrivate {
namespace Layout {

class LayoutOptimizer;

/**
 * @brief Layouter that supports min/max/preferred constraints spanning
 *        multiple consecutive elements (e.g. a grid cell that straddles
 *        several columns).
 *
 * Internally maintains per-element constraint lists and a running prefix-sum
 * representation. When constraints cannot be satisfied directly the layouter
 * delegates to a LayoutOptimizer to find a feasible solution by quadratic
 * programming.
 */
class ComplexLayouter : public Layouter {
public:
								ComplexLayouter(int32 elementCount,
									float spacing);
	virtual						~ComplexLayouter();

	virtual	status_t			InitCheck() const;

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
			struct Constraint;
			struct SumItem;
			struct SumItemBackup;

			bool				_Layout(int32 size, SumItem* sums,
									int32* sizes);
			bool				_AddOptimizerConstraints();
			bool				_SatisfiesConstraints(int32* sizes) const;
			bool				_SatisfiesConstraintsSums(int32* sums) const;

			void				_ValidateLayout();
			void				_ApplyMaxConstraint(
									Constraint* currentConstraint, int32 index);
			void				_PropagateChanges(SumItem* sums, int32 toIndex,
									Constraint* lastMaxConstraint);
			void				_PropagateChangesBack(SumItem* sums,
									int32 changedIndex,
									Constraint* lastMaxConstraint);

			void				_BackupValues(int32 maxIndex);
			void				_RestoreValues(int32 maxIndex);

private:
			int32				fElementCount;
			int32				fSpacing;
			Constraint**		fConstraints;
			float*				fWeights;
			SumItem*			fSums;
			SumItemBackup*		fSumBackups;
			LayoutOptimizer*	fOptimizer;
			float				fMin;
			float				fMax;
			int32				fUnlimited;
			bool				fMinMaxValid;
			bool				fOptimizerConstraintsAdded;
};

}	// namespace Layout
}	// namespace BPrivate

using BPrivate::Layout::ComplexLayouter;


#endif	// COMPLEX_LAYOUTER_H
