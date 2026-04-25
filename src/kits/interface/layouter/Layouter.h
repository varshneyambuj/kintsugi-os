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

/** @file Layouter.h
    @brief Abstract strategy interfaces (Layouter and LayoutInfo) used by the
           BLayout machinery to size and position elements along one axis. */

#ifndef	LAYOUTER_H
#define	LAYOUTER_H

#include <SupportDefs.h>

namespace BPrivate {
namespace Layout {

/**
 * @brief Result object produced by a Layouter pass.
 *
 * After Layouter::Layout() runs, the LayoutInfo answers per-element location
 * and size queries used by BLayout to position child views or items.
 */
class LayoutInfo {
public:
								LayoutInfo();
	virtual						~LayoutInfo();

	virtual	float				ElementLocation(int32 element) = 0;
	virtual	float				ElementSize(int32 element) = 0;

	virtual	float				ElementRangeSize(int32 position, int32 length);
};


/**
 * @brief Abstract layout strategy: turns per-element constraints and weights
 *        into computed sizes for a given total length.
 *
 * Subclasses implement specific algorithms (single element, simple weighted
 * distribution, complex multi-element constraints, collapsing wrapper, etc.).
 * Constraints and weights are loaded first; MinSize(), MaxSize(), and
 * PreferredSize() then yield aggregate metrics, and Layout() writes results
 * into a LayoutInfo of the same family.
 */
class Layouter {
public:
								Layouter();
	virtual						~Layouter();

	virtual	void				AddConstraints(int32 element, int32 length,
									float min, float max, float preferred) = 0;
	virtual	void				SetWeight(int32 element, float weight) = 0;

	virtual	float				MinSize() = 0;
	virtual	float				MaxSize() = 0;
	virtual	float				PreferredSize() = 0;

	virtual	LayoutInfo*			CreateLayoutInfo() = 0;

	virtual	void				Layout(LayoutInfo* layoutInfo, float size) = 0;

	virtual	Layouter*			CloneLayouter() = 0;
};


}	// namespace Layout
}	// namespace BPrivate

using BPrivate::Layout::LayoutInfo;
using BPrivate::Layout::Layouter;

#endif	// LAYOUTER_H
