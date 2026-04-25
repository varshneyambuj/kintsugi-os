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
 * MIT License. Copyright 2006-2007, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 */

/** @file SimpleLayouter.h
    @brief Layouter for the common case of independent single-element constraints;
           distributes residual space by weight. */

#ifndef	SIMPLE_LAYOUTER_H
#define	SIMPLE_LAYOUTER_H

#include "Layouter.h"


class BList;

namespace BPrivate {
namespace Layout {

/**
 * @brief Layouter that handles only single-element (one-row/column) constraints.
 *
 * Each element carries an independent (min, max, preferred) triple and a
 * weight. SimpleLayouter computes overall min/max/preferred sizes by summing
 * per-element values plus inter-element spacing, then distributes free space
 * proportionally to weights at layout time.
 */
class SimpleLayouter : public Layouter {
public:
								SimpleLayouter(int32 elementCount,
									float spacing);
	virtual						~SimpleLayouter();

	virtual	void				AddConstraints(int32 element, int32 length,
									float min, float max, float preferred);
	virtual	void				SetWeight(int32 element, float weight);

	virtual	float				MinSize();
	virtual	float				MaxSize();
	virtual	float				PreferredSize();

	virtual	LayoutInfo*			CreateLayoutInfo();

	virtual	void				Layout(LayoutInfo* layoutInfo, float size);

	virtual	Layouter*			CloneLayouter();

	/**
	 * @brief Distributes @a size pixels across @a count slots proportional
	 *        to @a weights, writing the integer results into @a sizes.
	 *
	 * @param size    Total integer size to distribute.
	 * @param weights Array of @a count non-negative weights.
	 * @param sizes   Output array of @a count integer sizes; written by this call.
	 * @param count   Number of slots in @a weights and @a sizes.
	 */
	static	void				DistributeSize(int32 size, float weights[],
									int32 sizes[], int32 count);

private:
	static	long				_CalculateSumWeight(BList& elementInfos);

			void				_ValidateMinMax();
			void				_LayoutMax();
			void				_LayoutStandard();

private:
			class ElementLayoutInfo;
			class ElementInfo;
			class MyLayoutInfo;

			int32				fElementCount;
			int32				fSpacing;
			ElementInfo*		fElements;

			int32				fMin;
			int32				fMax;
			int32				fPreferred;

			bool				fMinMaxValid;

			MyLayoutInfo*		fLayoutInfo;
};

}	// namespace Layout
}	// namespace BPrivate

using BPrivate::Layout::SimpleLayouter;

#endif	// SIMPLE_LAYOUTER_H
