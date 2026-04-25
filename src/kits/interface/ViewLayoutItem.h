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
 * MIT License. Copyright 2006-2010, Haiku.
 */

/** @file ViewLayoutItem.h
    @brief BLayoutItem adapter that wraps a BView so it can participate in BLayout. */

#ifndef	_VIEW_LAYOUT_ITEM_H
#define	_VIEW_LAYOUT_ITEM_H

#include <LayoutItem.h>


/**
 * @brief Adapter that exposes a BView as a BLayoutItem.
 *
 * BViewLayoutItem forwards size, alignment, and frame queries to the wrapped
 * BView so layouts can position views uniformly alongside non-view layout
 * items. It also tracks ancestor visibility so a view nested inside a
 * collapsed container is correctly hidden.
 */
class BViewLayoutItem : public BLayoutItem {
public:
								BViewLayoutItem(BView* view);
								BViewLayoutItem(BMessage* from);
	virtual						~BViewLayoutItem();

	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();
	virtual	BAlignment			Alignment();

	virtual	void				SetExplicitMinSize(BSize size);
	virtual	void				SetExplicitMaxSize(BSize size);
	virtual	void				SetExplicitPreferredSize(BSize size);
	virtual	void				SetExplicitAlignment(BAlignment alignment);

	virtual	bool				IsVisible();
	virtual	void				SetVisible(bool visible);

	virtual	BRect				Frame();
	virtual	void				SetFrame(BRect frame);

	virtual	bool				HasHeightForWidth();
	virtual	void				GetHeightForWidth(float width, float* min,
									float* max, float* preferred);

	virtual	BView*				View();

	virtual	void				Relayout(bool immediate = false);

	virtual	status_t			Archive(BMessage* into, bool deep = true) const;
	virtual status_t			AllArchived(BMessage* into) const;
	virtual status_t			AllUnarchived(const BMessage* from);
	static	BArchivable*		Instantiate(BMessage* from);

protected:
	virtual	void				LayoutInvalidated(bool children);
	virtual void				AncestorVisibilityChanged(bool shown);

private:
			BView*				fView;
			int32				fAncestorsVisible;
};

#endif	//	_VIEW_LAYOUT_ITEM_H
