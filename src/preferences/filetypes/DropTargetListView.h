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
 * MIT License. Copyright 2006, Axel Dörfler, axeld@pinc-software.de.
 */

/**
 * @file DropTargetListView.h
 * @brief BListView subclass that paints a focus frame while a valid drag
 *        hovers over it, used for accepting MIME-type drops.
 */

#ifndef DROP_TARGET_LIST_VIEW_H
#define DROP_TARGET_LIST_VIEW_H


#include <ListView.h>


/**
 * @brief List view that highlights itself as a drop target during drag-and-drop.
 */
class DropTargetListView : public BListView {
	public:
		DropTargetListView(const char* name,
			list_view_type type = B_SINGLE_SELECTION_LIST,
			uint32 flags = B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE);
		virtual ~DropTargetListView();

		virtual void Draw(BRect updateRect);
		virtual void MouseMoved(BPoint where, uint32 transit,
			const BMessage* dragMessage);

		virtual bool AcceptsDrag(const BMessage* message);

	private:
		void _InvalidateFrame();

		bool	fDropTarget;
};

#endif	// DROP_TARGET_LIST_VIEW_H
