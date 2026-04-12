/*
 * Copyright 2026 Kintsugi OS Project. All rights reserved.
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
 * Authors:
 *     Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2011, Haiku, Inc.
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file CollapsingLayouter.cpp
 * @brief Layouter that collapses invisible (zero-size) elements out of a
 *        delegate layouter.
 *
 * CollapsingLayouter wraps any other Layouter implementation and compresses
 * the element array by skipping elements that have no valid constraints.
 * This allows callers to use stable element indices even when some UI
 * elements are hidden, while the inner layouter only sees the active subset.
 *
 * @see Layouter, ComplexLayouter, SimpleLayouter, OneElementLayouter
 */


#include "CollapsingLayouter.h"

#include "ComplexLayouter.h"
#include "OneElementLayouter.h"
#include "SimpleLayouter.h"

#include <ObjectList.h>
#include <Size.h>


/**
 * @brief LayoutInfo proxy that maps outer element indices to the inner
 *        layouter's compressed element positions.
 *
 * Stores the mapping built during collapse and delegates all geometric
 * queries to the underlying LayoutInfo.
 */
class CollapsingLayouter::ProxyLayoutInfo : public LayoutInfo {
public:
	/**
	 * @brief Constructs the proxy, allocating the element-position mapping.
	 *
	 * @param target       LayoutInfo produced by the inner layouter (takes
	 *                     ownership).
	 * @param elementCount Total number of outer elements (including collapsed
	 *                     ones).
	 */
	ProxyLayoutInfo(LayoutInfo* target, int32 elementCount)
		:
		fTarget(target),
		fElementCount(elementCount)
	{
		fElements = new int32[elementCount];
	}

	/**
	 * @brief Destroys the proxy and frees the element map and target info.
	 */
	~ProxyLayoutInfo()
	{
		delete[] fElements;
		delete fTarget;
	}

	/**
	 * @brief Triggers layout on the inner LayoutInfo using the delegate layouter.
	 *
	 * @param layouter The inner Layouter to use for layout; may be NULL.
	 * @param size     The total available size to distribute.
	 */
	void
	LayoutTarget(Layouter* layouter, float size)
	{
		if (layouter)
			layouter->Layout(fTarget, size);
	}

	/**
	 * @brief Records which inner position corresponds to a given outer element.
	 *
	 * @param element  Outer element index.
	 * @param position Inner (compressed) position index, or -1 if collapsed.
	 */
	void
	SetElementPosition(int32 element, int32 position)
	{
		fElements[element] = position;
	}

	/**
	 * @brief Returns the pixel location of an outer element.
	 *
	 * @param element Outer element index.
	 * @return Pixel offset from the layout origin, or 0 if collapsed/invalid.
	 */
	float
	ElementLocation(int32 element)
	{
		if (element < 0 || element >= fElementCount || fElements[element] < 0)
			return 0;
		return fTarget->ElementLocation(fElements[element]);
	}

	/**
	 * @brief Returns the pixel size of an outer element.
	 *
	 * @param element Outer element index.
	 * @return Size in pixels, or 0 if collapsed/invalid.
	 */
	float
	ElementSize(int32 element)
	{
		if (element < 0 || element >= fElementCount || fElements[element] < 0)
			return 0;
		return fTarget->ElementSize(fElements[element]);
	}

	/**
	 * @brief Returns the combined pixel size of a range of outer elements.
	 *
	 * @param element Outer index of the first element in the range.
	 * @param length  Number of elements in the range.
	 * @return Combined size in pixels, or 0 if collapsed/invalid.
	 */
	float
	ElementRangeSize(int32 element, int32 length)
	{
		if (element < 0 || element >= fElementCount || fElements[element] < 0)
			return 0;
		return fTarget->ElementRangeSize(fElements[element], length);
	}

private:
	int32*					fElements;
	LayoutInfo*				fTarget;
	int32					fElementCount;
};


/** @brief Stores min/max/preferred size constraints for a span of elements. */
struct CollapsingLayouter::Constraint {
	int32 length;
	float min;
	float max;
	float preferred;
};


/** @brief Per-element bookkeeping: weight, collapsed position, validity flag,
 *         and the list of constraints referencing this element. */
struct CollapsingLayouter::ElementInfo {
	float weight;
	int32 position;
	bool valid;
	BObjectList<Constraint, true> constraints;

	ElementInfo()
		:
		weight(0),
		position(-1),
		valid(false),
		constraints(5)
	{
	}

	~ElementInfo()
	{
	}

	/**
	 * @brief Deep-copies another ElementInfo into this one.
	 *
	 * @param other Source element info to copy from.
	 */
	void SetTo(const ElementInfo& other)
	{
		weight = other.weight;
		position = other.position;
		valid = other.valid;
		for (int32 i = other.constraints.CountItems() - 1; i >= 0; i--)
			constraints.AddItem(new Constraint(*other.constraints.ItemAt(i)));
	}
};


/**
 * @brief Constructs a CollapsingLayouter for the given element count.
 *
 * @param elementCount Total number of layout elements (including potentially
 *                     invisible ones).
 * @param spacing      Pixel gap to place between active elements.
 */
CollapsingLayouter::CollapsingLayouter(int32 elementCount, float spacing)
	:
	fElementCount(elementCount),
	fElements(new ElementInfo[elementCount]),
	fValidElementCount(0),
	fHaveMultiElementConstraints(false),
	fSpacing(spacing),
	fLayouter(NULL)
{
}


/**
 * @brief Destroys the layouter and frees element and delegate resources.
 */
CollapsingLayouter::~CollapsingLayouter()
{
	delete[] fElements;
	delete fLayouter;
}


/**
 * @brief Adds a size constraint spanning one or more consecutive elements.
 *
 * Marks each covered element as valid, records the constraint, and
 * invalidates the delegate layouter if the valid-element count changed.
 *
 * @param element   Index of the first element covered by the constraint.
 * @param length    Number of elements covered (must be >= 1).
 * @param min       Minimum combined size; use B_SIZE_UNSET to leave open.
 * @param max       Maximum combined size; use B_SIZE_UNSET to leave open.
 * @param preferred Preferred combined size.
 */
void
CollapsingLayouter::AddConstraints(int32 element, int32 length, float min,
	float max, float preferred)
{
	if (min == B_SIZE_UNSET && max == B_SIZE_UNSET)
		return;
	if (element < 0 || length <= 0 || element + length > fElementCount)
		return;

	Constraint* constraint = new Constraint();
	constraint->length = length;
	constraint->min = min;
	constraint->max = max;
	constraint->preferred = preferred;

	if (length > 1)
		fHaveMultiElementConstraints = true;

	int32 validElements = fValidElementCount;

	for (int32 i = element; i < element + length; i++) {
		if (fElements[i].valid == false) {
			fElements[i].valid = true;
			fValidElementCount++;
		}
	}

	fElements[element].constraints.AddItem(constraint);
	if (fValidElementCount > validElements) {
		delete fLayouter;
		fLayouter = NULL;
	}

	if (fLayouter)
		_AddConstraints(element, constraint);

}


/**
 * @brief Sets the weight of an element for proportional size distribution.
 *
 * @param element Index of the element whose weight to set.
 * @param weight  Non-negative weight value; higher weight gets more space.
 */
void
CollapsingLayouter::SetWeight(int32 element, float weight)
{
	if (element < 0 || element >= fElementCount)
		return;

	ElementInfo& elementInfo = fElements[element];
	elementInfo.weight = weight;

	if (fLayouter && elementInfo.position >= 0)
		fLayouter->SetWeight(elementInfo.position, weight);
}


/**
 * @brief Returns the minimum size required to satisfy all constraints.
 *
 * @return Minimum size in pixels, or 0 if no valid elements exist.
 */
float
CollapsingLayouter::MinSize()
{
	_ValidateLayouter();
	return fLayouter ? fLayouter->MinSize() : 0;
}


/**
 * @brief Returns the maximum size this layouter can use.
 *
 * @return Maximum size in pixels, or B_SIZE_UNLIMITED if no valid elements.
 */
float
CollapsingLayouter::MaxSize()
{
	_ValidateLayouter();
	return fLayouter ? fLayouter->MaxSize() : B_SIZE_UNLIMITED;
}


/**
 * @brief Returns the preferred size for this layouter.
 *
 * @return Preferred size in pixels, or 0 if no valid elements exist.
 */
float
CollapsingLayouter::PreferredSize()
{
	_ValidateLayouter();
	return fLayouter ? fLayouter->PreferredSize() : 0;
}


/**
 * @brief Allocates a LayoutInfo that maps outer element indices to positions.
 *
 * @return A new ProxyLayoutInfo wrapping the delegate's LayoutInfo.
 */
LayoutInfo*
CollapsingLayouter::CreateLayoutInfo()
{
	_ValidateLayouter();

	LayoutInfo* info = fLayouter ? fLayouter->CreateLayoutInfo() : NULL;
	return new ProxyLayoutInfo(info, fElementCount);
}


/**
 * @brief Performs layout, distributing @p size across the active elements.
 *
 * Fills the element-position map in the proxy info and then delegates the
 * actual size distribution to the inner layouter.
 *
 * @param layoutInfo LayoutInfo previously created by CreateLayoutInfo().
 * @param size       Total available size in pixels.
 */
void
CollapsingLayouter::Layout(LayoutInfo* layoutInfo, float size)
{
	_ValidateLayouter();
	ProxyLayoutInfo* info = static_cast<ProxyLayoutInfo*>(layoutInfo);
	for (int32 i = 0; i < fElementCount; i++) {
		info->SetElementPosition(i, fElements[i].position);
	}

	info->LayoutTarget(fLayouter, size);
}


/**
 * @brief Creates an independent deep copy of this layouter.
 *
 * @return A new CollapsingLayouter with identical constraints and weights.
 */
Layouter*
CollapsingLayouter::CloneLayouter()
{
	CollapsingLayouter* clone = new CollapsingLayouter(fElementCount, fSpacing);
	for (int32 i = 0; i < fElementCount; i++)
		clone->fElements[i].SetTo(fElements[i]);

	clone->fValidElementCount = fValidElementCount;
	clone->fHaveMultiElementConstraints = fHaveMultiElementConstraints;

	if (fLayouter)
		clone->fLayouter = fLayouter->CloneLayouter();
	return clone;
}


/**
 * @brief Ensures the delegate layouter is created, collapsed, and populated.
 *
 * Lazily calls _CreateLayouter(), _DoCollapse(), _AddConstraints(), and
 * _SetWeights() when the delegate is NULL.
 */
void
CollapsingLayouter::_ValidateLayouter()
{
	if (fLayouter)
		return;

	_CreateLayouter();
	_DoCollapse();
	_AddConstraints();
	_SetWeights();
}


/**
 * @brief Selects and instantiates the most appropriate delegate layouter.
 *
 * Chooses OneElementLayouter, SimpleLayouter, or ComplexLayouter depending
 * on the number of valid elements and whether multi-element constraints exist.
 *
 * @return The newly created delegate, or NULL if there are no valid elements.
 */
Layouter*
CollapsingLayouter::_CreateLayouter()
{
	if (fLayouter)
		return fLayouter;

	if (fValidElementCount == 0) {
		fLayouter =  NULL;
	} else if (fValidElementCount == 1) {
		fLayouter =  new OneElementLayouter();
	} else if (fHaveMultiElementConstraints) {
		fLayouter =  new ComplexLayouter(fValidElementCount, fSpacing);
	} else {
		fLayouter = new SimpleLayouter(fValidElementCount, fSpacing);
	}

	return fLayouter;
}


/**
 * @brief Computes the compressed inner position for each outer element.
 *
 * Invalid (hidden) elements are assigned position -1; valid elements receive
 * consecutive positions starting from 0.
 */
void
CollapsingLayouter::_DoCollapse()
{
	int32 shift = 0;
	for (int32 i = 0; i < fElementCount; i++) {
		ElementInfo& element = fElements[i];
		if (!element.valid) {
			shift++;
			element.position = -1;
			continue;
		} else {
			element.position = i - shift;
		}
	}
}


/**
 * @brief Forwards all stored constraints to the delegate layouter.
 *
 * Iterates every element's constraint list and passes each to the delegate
 * using the element's collapsed inner position.
 */
void
CollapsingLayouter::_AddConstraints()
{
	if (fLayouter == NULL)
		return;

	for (int32 i = 0; i < fElementCount; i++) {
		ElementInfo& element = fElements[i];
		for (int32 i = element.constraints.CountItems() - 1; i >= 0; i--)
			_AddConstraints(element.position, element.constraints.ItemAt(i));
	}
}


/**
 * @brief Forwards a single constraint to the delegate layouter.
 *
 * @param position Collapsed inner position of the first element.
 * @param c        Constraint to forward; its length is used as-is.
 */
void
CollapsingLayouter::_AddConstraints(int32 position, const Constraint* c)
{
	fLayouter->AddConstraints(position, c->length, c->min, c->max,
		c->preferred);
}


/**
 * @brief Forwards all element weights to the delegate layouter.
 *
 * Elements whose position is -1 (collapsed) are skipped silently by the
 * delegate's SetWeight implementation.
 */
void
CollapsingLayouter::_SetWeights()
{
	if (!fLayouter)
		return;

	for (int32 i = 0; i < fElementCount; i++) {
		fLayouter->SetWeight(fElements[i].position, fElements[i].weight);
	}
}
