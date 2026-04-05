/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered trademarks
of Be Incorporated in the United States and other countries. Other brand product
names are registered trademarks or trademarks of their respective holders.
All rights reserved.
*/

/*******************************************************************************
/
/	File:			ColumnListView.cpp
/
/   Description:    Experimental multi-column list view.
/
/	Copyright 2000+, Be Incorporated, All Rights Reserved
/					 By Jeff Bush
/
*******************************************************************************/

#include "ColumnListView.h"

#include <typeinfo>

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>

#include <Application.h>
#include <Bitmap.h>
#include <ControlLook.h>
#include <Cursor.h>
#include <Debug.h>
#include <GraphicsDefs.h>
#include <LayoutUtils.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Region.h>
#include <ScrollBar.h>
#include <String.h>
#include <SupportDefs.h>
#include <Window.h>

#include <ObjectListPrivate.h>

#include "ObjectList.h"


#define DOUBLE_BUFFERED_COLUMN_RESIZE 1
#define SMART_REDRAW 1
#define DRAG_TITLE_OUTLINE 1
#define CONSTRAIN_CLIPPING_REGION 1
#define LOWER_SCROLLBAR 0


namespace BPrivate {

/** @brief 8x8 bitmap for a downward-pointing sort arrow in 8-bit palette mode. */
static const unsigned char kDownSortArrow8x8[] = {
	0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
	0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
	0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
	0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
	0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
	0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0xff
};

/** @brief 8x8 bitmap for an upward-pointing sort arrow in 8-bit palette mode. */
static const unsigned char kUpSortArrow8x8[] = {
	0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
	0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
	0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
	0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
	0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
	0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff
};

/** @brief Inverted 8x8 bitmap for a downward-pointing sort arrow, used on dark backgrounds. */
static const unsigned char kDownSortArrow8x8Invert[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0x1f, 0x1f, 0x1f, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0x1f, 0xff, 0xff, 0xff, 0xff
};

/** @brief Inverted 8x8 bitmap for an upward-pointing sort arrow, used on dark backgrounds. */
static const unsigned char kUpSortArrow8x8Invert[] = {
	0xff, 0xff, 0xff, 0x1f, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0x1f, 0x1f, 0x1f, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/** @brief Tint factor applied to alternating rows on light backgrounds. */
static const float kTintedLineTint = 1.04;
/** @brief Tint factor applied to alternating rows on dark backgrounds. */
static const float kTintedLineTintDark = 0.90;

/** @brief Minimum pixel height of the column title area. */
static const float kMinTitleHeight = 16.0;
/** @brief Minimum pixel height of a list row. */
static const float kMinRowHeight = 16.0;
/** @brief Multiplier applied to font size to derive the title area height. */
static const float kTitleSpacing = 1.4;
/** @brief Multiplier applied to font size to derive the default row height. */
static const float kRowSpacing = 1.4;
/** @brief Pixel width reserved for the expand/collapse latch widget. */
static const float kLatchWidth = 15.0;

/** @brief Maximum nesting depth supported by the recursive iterator stack. */
static const int32 kMaxDepth = 1024;
/** @brief Left margin width, at least as wide as the latch. */
static const float kLeftMargin = kLatchWidth;
/** @brief Right margin width in pixels. */
static const float kRightMargin = 8;
/** @brief Horizontal indent per outline nesting level in pixels. */
static const float kOutlineLevelIndent = kLatchWidth;
/** @brief Total width of the column-resize hit area centered on a column edge. */
static const float kColumnResizeAreaWidth = 10.0;
/** @brief Minimum pixel distance the mouse must move before a row drag begins. */
static const float kRowDragSensitivity = 5.0;
/** @brief Maximum pixel distance between click and release to count as a double-click. */
static const float kDoubleClickMoveSensitivity = 4.0;
/** @brief Pixel width reserved for the sort-order indicator arrow in a column header. */
static const float kSortIndicatorWidth = 9.0;
/** @brief Height of the horizontal drop-target highlight line during drag operations. */
static const float kDropHighlightLineHeight = 2.0;

/** @brief Message command sent when the user toggles a column's visibility via the popup menu. */
static const uint32 kToggleColumn = 'BTCL';


#ifdef DOUBLE_BUFFERED_COLUMN_RESIZE

class ColumnResizeBufferView : public BView
{
public:
							ColumnResizeBufferView();
	virtual					~ColumnResizeBufferView();
			void			UpdateMaxWidth(float width);
			void			UpdateMaxHeight(float height);
			bool			Lock();
			void			Unlock();
			const BBitmap* 	Bitmap();
private:
			void			_InitBitmap();
			void			_FreeBitmap();

			BBitmap*		fDrawBuffer;
};

#endif


class BRowContainer : public BObjectList<BRow>
{
};


class TitleView : public BView {
	typedef BView _inherited;
public:
								TitleView(BRect frame, OutlineView* outlineView,
									BList* visibleColumns, BList* sortColumns,
									BColumnListView* masterView,
									uint32 resizingMode);
	virtual						~TitleView();

			void				ColumnAdded(BColumn* column);
			void				ColumnResized(BColumn* column, float oldWidth);
			void				SetColumnVisible(BColumn* column, bool visible);

	virtual	void				Draw(BRect updateRect);
	virtual	void				ScrollTo(BPoint where);
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				MouseUp(BPoint where);
	virtual	void				FrameResized(float width, float height);

			void				MoveColumn(BColumn* column, int32 index);
			void				SetColumnFlags(column_flags flags);

			void				SetEditMode(bool state)
									{ fEditMode = state; }

			float				MarginWidth() const;

private:
			void				GetTitleRect(BColumn* column, BRect* _rect);
			int32				FindColumn(BPoint where, float* _leftEdge);
			void				FixScrollBar(bool scrollToFit);
			void				DragSelectedColumn(BPoint where);
			void				ResizeSelectedColumn(BPoint where,
									bool preferred = false);
			void				ComputeDragBoundries(BColumn* column,
									BPoint where);
			void				DrawTitle(BView* view, BRect frame,
									BColumn* column, bool depressed);

			float				_VirtualWidth() const;

			OutlineView*		fOutlineView;
			BList*				fColumns;
			BList*				fSortColumns;
//			float				fColumnsWidth;
			BRect				fVisibleRect;


			enum {
				INACTIVE,
				RESIZING_COLUMN,
				PRESSING_COLUMN,
				DRAG_COLUMN_INSIDE_TITLE,
				DRAG_COLUMN_OUTSIDE_TITLE
			}					fCurrentState;

			BPopUpMenu*			fColumnPop;
			BColumnListView*	fMasterView;
			bool				fEditMode;
			int32				fColumnFlags;

	// State information for resizing/dragging
			BColumn*			fSelectedColumn;
			BRect				fSelectedColumnRect;
			bool				fResizingFirstColumn;
			BPoint				fClickPoint; // offset within cell
			float				fLeftDragBoundry;
			float				fRightDragBoundry;
			BPoint				fCurrentDragPosition;


			BBitmap*			fUpSortArrow;
			BBitmap*			fDownSortArrow;

			BCursor*			fResizeCursor;
			BCursor*			fMinResizeCursor;
			BCursor*			fMaxResizeCursor;
			BCursor*			fColumnMoveCursor;
};


class OutlineView : public BView {
	typedef BView _inherited;
public:
								OutlineView(BRect, BList* visibleColumns,
									BList* sortColumns,
									BColumnListView* listView);
	virtual						~OutlineView();

	virtual void				Draw(BRect);
	const 	BRect&				VisibleRect() const;

			void				RedrawColumn(BColumn* column, float leftEdge,
									bool isFirstColumn);
			void 				StartSorting();
			float				GetColumnPreferredWidth(BColumn* column);

			void				AddRows(BList* rows, int32 index, BRow* parentRow);
			void				AddRow(BRow*, int32 index, BRow* TheRow);
			BRow*				CurrentSelection(BRow* lastSelected) const;
			void 				ToggleFocusRowSelection(bool selectRange);
			void 				ToggleFocusRowOpen();
			void 				ChangeFocusRow(bool up, bool updateSelection,
									bool addToCurrentSelection);
			void 				MoveFocusToVisibleRect();
			void 				ExpandOrCollapse(BRow* parent, bool expand);
			void				RemoveRows(BList* rows);
			void 				RemoveRow(BRow*);
			BRowContainer*		RowList();
			void				UpdateRow(BRow*);
			bool				FindParent(BRow* row, BRow** _parent,
									bool* _isVisible);
			int32				IndexOf(BRow* row);
			void				Deselect(BRow*);
			void				AddToSelection(BRow*);
			void				DeselectAll();
			BRow*				FocusRow() const;
			void				SetFocusRow(BRow* row, bool select);
			BRow*				FindRow(float ypos, int32* _indent,
									float* _top);
			bool				FindRect(const BRow* row, BRect* _rect);
			void				ScrollTo(const BRow* row);

			void				Clear();
			void				SetSelectionMode(list_view_type type);
			list_view_type		SelectionMode() const;
			void				SetMouseTrackingEnabled(bool);
			void				FixScrollBar(bool scrollToFit);
			void				SetEditMode(bool state)
									{ fEditMode = state; }

	virtual void				FrameResized(float width, float height);
	virtual void				ScrollTo(BPoint where);
	virtual void				MouseDown(BPoint where);
	virtual void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual void				MouseUp(BPoint where);
	virtual void				MessageReceived(BMessage* message);

#if DOUBLE_BUFFERED_COLUMN_RESIZE
			ColumnResizeBufferView* ResizeBufferView();
#endif

private:
			bool				SortList(BRowContainer* list, bool isVisible);
	static	int32				DeepSortThreadEntry(void* outlineView);
			void				DeepSort();
			void				SelectRange(BRow* start, BRow* end);
			int32				CompareRows(BRow* row1, BRow* row2);
			int32				AddRowToParentOnly(BRow* row, int32 index,
									BRow* parent);
			int32				AddSorted(BRowContainer* list, BRow* row);
			void				RecursiveDeleteRows(BRowContainer* list,
									bool owner);
			void				InvalidateCachedPositions();
			bool				FindVisibleRect(BRow* row, BRect* _rect);
			bool				RemoveRowFromSelectionOnly(BRow* row);

			BList*				fColumns;
			BList*				fSortColumns;
			float				fItemsHeight;
			BRowContainer		fRows;
			BRect				fVisibleRect;

#if DOUBLE_BUFFERED_COLUMN_RESIZE
			ColumnResizeBufferView* fResizeBufferView;
#endif

			BRow*				fFocusRow;
			BRect				fFocusRowRect;
			BRow*				fRollOverRow;

			BRow				fSelectionListDummyHead;
			BRow*				fLastSelectedItem;
			BRow*				fFirstSelectedItem;

			thread_id			fSortThread;
			int32				fNumSorted;
			bool				fSortCancelled;

			enum CurrentState {
				INACTIVE,
				LATCH_CLICKED,
				ROW_CLICKED,
				DRAGGING_ROWS
			};

			CurrentState		fCurrentState;


			BColumnListView*	fMasterView;
			list_view_type		fSelectionMode;
			bool				fTrackMouse;
			BField*				fCurrentField;
			BRow*				fCurrentRow;
			BColumn*			fCurrentColumn;
			bool				fMouseDown;
			BRect				fFieldRect;
			int32				fCurrentCode;
			bool				fEditMode;

	// State information for mouse/keyboard interaction
			BPoint				fClickPoint;
			bool				fDragging;
			int32				fClickCount;
			BRow*				fTargetRow;
			float				fTargetRowTop;
			BRect				fLatchRect;
			float				fDropHighlightY;

	friend class RecursiveOutlineIterator;
};


class RecursiveOutlineIterator {
public:
								RecursiveOutlineIterator(
									BRowContainer* container,
									bool openBranchesOnly = true);

			BRow*				CurrentRow() const;
			int32				CurrentLevel() const;
			void				GoToNext();

private:
			struct {
				BRowContainer* fRowSet;
				int32 fIndex;
				int32 fDepth;
			}					fStack[kMaxDepth];

			int32				fStackIndex;
			BRowContainer*		fCurrentList;
			int32				fCurrentListIndex;
			int32				fCurrentListDepth;
			bool				fOpenBranchesOnly;
};

}	// namespace BPrivate


using namespace BPrivate;


#ifdef DOUBLE_BUFFERED_COLUMN_RESIZE

/**
 * @brief Construct the resize buffer view and allocate its backing bitmap.
 *
 * Creates a 600x35-pixel off-screen BView and immediately allocates the
 * backing BBitmap used for double-buffered column resize redraws.
 */
ColumnResizeBufferView::ColumnResizeBufferView()
	: BView(BRect(0, 0, 600, 35), "double_buffer_view", B_FOLLOW_ALL_SIDES, 0), fDrawBuffer(NULL)
{
	_InitBitmap();
}


/**
 * @brief Destroy the resize buffer view and release the backing bitmap.
 */
ColumnResizeBufferView::~ColumnResizeBufferView()
{
	_FreeBitmap();
}


/**
 * @brief Grow the backing bitmap if @p width exceeds the current bitmap width.
 *
 * @param width The new minimum required width in pixels.
 */
void
ColumnResizeBufferView::UpdateMaxWidth(float width)
{
	Lock();
	BRect bounds = Bounds();
	Unlock();

	if (width > bounds.Width()) {
		Lock();
		ResizeTo(width, bounds.Height());
		Unlock();
		_InitBitmap();
	}
}


/**
 * @brief Grow the backing bitmap if @p height exceeds the current bitmap height.
 *
 * @param height The new minimum required height in pixels.
 */
void
ColumnResizeBufferView::UpdateMaxHeight(float height)
{
	Lock();
	BRect bounds = Bounds();
	Unlock();

	if (height > bounds.Height()) {
		Lock();
		ResizeTo(bounds.Width(), height);
		Unlock();
		_InitBitmap();
	}
}


/**
 * @brief Lock the backing bitmap for exclusive drawing access.
 *
 * @return true if the lock was acquired, false otherwise.
 */
bool
ColumnResizeBufferView::Lock()
{
	return fDrawBuffer->Lock();
}


/**
 * @brief Unlock the backing bitmap after drawing is complete.
 */
void
ColumnResizeBufferView::Unlock()
{
	fDrawBuffer->Unlock();
}


/**
 * @brief Return a pointer to the backing bitmap.
 *
 * @return The internal BBitmap used for off-screen rendering.
 */
const BBitmap*
ColumnResizeBufferView::Bitmap()
{
	return fDrawBuffer;
}


/**
 * @brief Allocate (or reallocate) the backing bitmap to match the current view bounds.
 *
 * Any previously allocated bitmap is freed first via _FreeBitmap().
 */
void
ColumnResizeBufferView::_InitBitmap()
{
	_FreeBitmap();

	fDrawBuffer = new BBitmap(Bounds(), B_RGB32, true);
	fDrawBuffer->Lock();
	fDrawBuffer->AddChild(this);
	fDrawBuffer->Unlock();
}


/**
 * @brief Release and delete the backing bitmap, detaching this view from it first.
 */
void
ColumnResizeBufferView::_FreeBitmap()
{
	if (fDrawBuffer) {
		fDrawBuffer->Lock();
		fDrawBuffer->RemoveChild(this);
		fDrawBuffer->Unlock();
		delete fDrawBuffer;
		fDrawBuffer = NULL;
	}
}

#endif


/**
 * @brief Construct a BField cell-data object.
 *
 * BField is an abstract base class; concrete subclasses hold the actual
 * data displayed in a column cell.
 */
BField::BField()
{
}


/**
 * @brief Destroy the BField.
 */
BField::~BField()
{
}


// #pragma mark -


/**
 * @brief Called when the mouse moves over a field cell owned by this column.
 *
 * The default implementation does nothing. Subclasses may override to provide
 * interactive behavior such as hover effects or tooltip activation.
 *
 * @param parent    The owning BColumnListView.
 * @param row       The row containing the field.
 * @param field     The field the mouse is over.
 * @param field_rect The bounding rectangle of the field in view coordinates.
 * @param point     Current mouse position in view coordinates.
 * @param buttons   Currently pressed mouse buttons.
 * @param code      Transit code (B_ENTERED_VIEW, B_INSIDE_VIEW, B_EXITED_VIEW, etc.).
 */
void
BColumn::MouseMoved(BColumnListView* /*parent*/, BRow* /*row*/,
	BField* /*field*/, BRect /*field_rect*/, BPoint/*point*/,
	uint32 /*buttons*/, int32 /*code*/)
{
}


/**
 * @brief Called when a mouse button is pressed over a field cell owned by this column.
 *
 * The default implementation does nothing. Subclasses may override to handle
 * in-cell editing or custom interactions.
 *
 * @param parent    The owning BColumnListView.
 * @param row       The row containing the field.
 * @param field     The field that was clicked.
 * @param field_rect The bounding rectangle of the field in view coordinates.
 * @param point     Mouse position in view coordinates at the time of the click.
 * @param buttons   Mouse buttons that are currently pressed.
 */
void
BColumn::MouseDown(BColumnListView* /*parent*/, BRow* /*row*/,
	BField* /*field*/, BRect /*field_rect*/, BPoint /*point*/,
	uint32 /*buttons*/)
{
}


/**
 * @brief Called when a mouse button is released over a field cell owned by this column.
 *
 * The default implementation does nothing. Subclasses may override to finalize
 * in-cell editing or commit an interaction started in MouseDown().
 *
 * @param parent The owning BColumnListView.
 * @param row    The row containing the field.
 * @param field  The field over which the button was released.
 */
void
BColumn::MouseUp(BColumnListView* /*parent*/, BRow* /*row*/, BField* /*field*/)
{
}


// #pragma mark -


/**
 * @brief Construct a BRow with a height derived from the current plain font size.
 *
 * The row height is computed as the ceiling of be_plain_font size multiplied
 * by kRowSpacing, clamped to at least kMinRowHeight pixels.
 */
BRow::BRow()
	:
	fChildList(NULL),
	fIsExpanded(false),
	fHeight(std::max(kMinRowHeight,
		ceilf(be_plain_font->Size() * kRowSpacing))),
	fNextSelected(NULL),
	fPrevSelected(NULL),
	fParent(NULL),
	fList(NULL)
{
}


/**
 * @brief Construct a BRow with an explicit pixel height.
 *
 * @param height The desired row height in pixels.
 */
BRow::BRow(float height)
	:
	fChildList(NULL),
	fIsExpanded(false),
	fHeight(height),
	fNextSelected(NULL),
	fPrevSelected(NULL),
	fParent(NULL),
	fList(NULL)
{
}


/**
 * @brief Destroy the BRow and delete all of its owned BField objects.
 */
BRow::~BRow()
{
	while (true) {
		BField* field = (BField*) fFields.RemoveItem((int32)0);
		if (field == 0)
			break;

		delete field;
	}
}


/**
 * @brief Return whether this row has at least one child row.
 *
 * A row with children displays an expand/collapse latch widget.
 *
 * @return true if the row has one or more child rows, false otherwise.
 */
bool
BRow::HasLatch() const
{
	return fChildList != 0;
}


/**
 * @brief Return the number of BField objects stored in this row.
 *
 * @return The total count of fields (including NULL slots) held by this row.
 */
int32
BRow::CountFields() const
{
	return fFields.CountItems();
}


/**
 * @brief Return the BField at the given logical field index.
 *
 * @param index Logical field index (matches the column's logical field number).
 * @return The BField at @p index, or NULL if none has been set.
 */
BField*
BRow::GetField(int32 index)
{
	return (BField*)fFields.ItemAt(index);
}


/**
 * @brief Return the BField at the given logical field index (const overload).
 *
 * @param index Logical field index.
 * @return The BField at @p index, or NULL if none has been set.
 */
const BField*
BRow::GetField(int32 index) const
{
	return (const BField*)fFields.ItemAt(index);
}


/**
 * @brief Set the BField for the given logical field index, replacing any existing field.
 *
 * If a field already exists at @p logicalFieldIndex it is deleted before the
 * new one is installed. If the row is already attached to a list view, the
 * field type is validated against the corresponding column and the row is
 * invalidated so it will be redrawn.
 *
 * @param field             The new field to store (ownership is transferred to the row).
 * @param logicalFieldIndex The logical field index that matches the target column.
 */
void
BRow::SetField(BField* field, int32 logicalFieldIndex)
{
	if (fFields.ItemAt(logicalFieldIndex) != 0)
		delete (BField*)fFields.RemoveItem(logicalFieldIndex);

	if (NULL != fList) {
		ValidateField(field, logicalFieldIndex);
		Invalidate();
	}

	fFields.AddItem(field, logicalFieldIndex);
}


/**
 * @brief Return the pixel height of this row.
 *
 * @return The row height in pixels.
 */
float
BRow::Height() const
{
	return fHeight;
}


/**
 * @brief Return whether this row's child list is currently expanded (visible).
 *
 * @return true if the row is expanded, false if collapsed.
 */
bool
BRow::IsExpanded() const
{
	return fIsExpanded;
}


/**
 * @brief Return whether this row is currently selected.
 *
 * A row is selected when it is linked into the selection intrusive list.
 *
 * @return true if the row is selected, false otherwise.
 */
bool
BRow::IsSelected() const
{
	return fPrevSelected != NULL;
}


/**
 * @brief Invalidate the visual area of this row so it is redrawn on the next update.
 *
 * Has no effect if the row is not currently attached to a BColumnListView.
 */
void
BRow::Invalidate()
{
	if (fList != NULL)
		fList->InvalidateRow(this);
}


/**
 * @brief Validate every field in this row against its corresponding column type.
 *
 * Iterates over all fields and calls ValidateField() for each one. Triggers a
 * debugger() call if any field type is incompatible with the column that owns it.
 */
void
BRow::ValidateFields() const
{
	for (int32 i = 0; i < CountFields(); i++)
		ValidateField(GetField(i), i);
}


/**
 * @brief Validate a single field against the column at the given logical index.
 *
 * Looks up the column whose logical field number matches @p logicalFieldIndex
 * and calls BColumn::AcceptsField(). If the column does not accept the field
 * type, debugger() is called with a descriptive message.
 *
 * @param field             The field to validate.
 * @param logicalFieldIndex The logical field index used to locate the column.
 */
void
BRow::ValidateField(const BField* field, int32 logicalFieldIndex) const
{
	// The Fields may be moved by the user, but the logicalFieldIndexes
	// do not change, so we need to map them over when checking the
	// Field types.
	BColumn* column = NULL;
	int32 items = fList->CountColumns();
	for (int32 i = 0 ; i < items; ++i) {
		column = fList->ColumnAt(i);
		if(column->LogicalFieldNum() == logicalFieldIndex )
			break;
	}

	if (column == NULL) {
		BString dbmessage("\n\n\tThe parent BColumnListView does not have "
			"\n\ta BColumn at the logical field index ");
		dbmessage << logicalFieldIndex << ".\n";
		puts(dbmessage.String());
	} else {
		if (!column->AcceptsField(field)) {
			BString dbmessage("\n\n\tThe BColumn of type ");
			dbmessage << typeid(*column).name() << "\n\tat logical field index "
				<< logicalFieldIndex << "\n\tdoes not support the field type "
				<< typeid(*field).name() << ".\n\n";
			debugger(dbmessage.String());
		}
	}
}


// #pragma mark -


/**
 * @brief Construct a BColumn with the given display properties.
 *
 * @param width    Initial column width in pixels.
 * @param minWidth Minimum allowed width in pixels.
 * @param maxWidth Maximum allowed width in pixels.
 * @param align    Horizontal alignment of cell content (B_ALIGN_LEFT, etc.).
 */
BColumn::BColumn(float width, float minWidth, float maxWidth, alignment align)
	:
	fWidth(width),
	fMinWidth(minWidth),
	fMaxWidth(maxWidth),
	fVisible(true),
	fList(0),
	fShowHeading(true),
	fAlignment(align)
{
}


/**
 * @brief Destroy the BColumn.
 */
BColumn::~BColumn()
{
}


/**
 * @brief Return the current display width of this column in pixels.
 *
 * @return The column width in pixels.
 */
float
BColumn::Width() const
{
	return fWidth;
}


/**
 * @brief Set the display width of this column.
 *
 * @param width New width in pixels. The caller is responsible for clamping to
 *              [MinWidth(), MaxWidth()] if needed.
 */
void
BColumn::SetWidth(float width)
{
	fWidth = width;
}


/**
 * @brief Return the minimum allowed width of this column in pixels.
 *
 * @return The minimum column width.
 */
float
BColumn::MinWidth() const
{
	return fMinWidth;
}


/**
 * @brief Return the maximum allowed width of this column in pixels.
 *
 * @return The maximum column width.
 */
float
BColumn::MaxWidth() const
{
	return fMaxWidth;
}


/**
 * @brief Draw the column header title into the given view and bounding rectangle.
 *
 * The default implementation does nothing. Subclasses must override to render
 * the column label text or other title content.
 *
 * @param rect The bounding rectangle of the header cell in view coordinates.
 * @param view The view to draw into.
 */
void
BColumn::DrawTitle(BRect, BView*)
{
}


/**
 * @brief Draw a field's content into the given view and bounding rectangle.
 *
 * The default implementation does nothing. Subclasses must override to render
 * the cell data contained in @p field.
 *
 * @param field The field whose data should be rendered.
 * @param rect  The bounding rectangle of the cell in view coordinates.
 * @param view  The view to draw into.
 */
void
BColumn::DrawField(BField*, BRect, BView*)
{
}


/**
 * @brief Compare two fields for sort ordering.
 *
 * The default implementation returns 0 (equal) for all field pairs. Subclasses
 * should override to implement a meaningful sort comparison.
 *
 * @param field1 The first field to compare.
 * @param field2 The second field to compare.
 * @return A negative value if field1 < field2, 0 if equal, positive if field1 > field2.
 */
int
BColumn::CompareFields(BField*, BField*)
{
	return 0;
}


/**
 * @brief Write the human-readable column name into @p into.
 *
 * The default implementation sets @p into to "(Unnamed)". Subclasses should
 * override to return a meaningful name used in the column-visibility popup menu.
 *
 * @param into Output parameter that receives the column name.
 */
void
BColumn::GetColumnName(BString* into) const
{
	*into = "(Unnamed)";
}


/**
 * @brief Return the preferred width needed to display @p field without clipping.
 *
 * The default implementation returns the current column width. Subclasses
 * should override to measure the actual rendered content width.
 *
 * @param field  The field whose content should be measured.
 * @param parent The view providing font metrics and drawing context.
 * @return The preferred width in pixels.
 */
float
BColumn::GetPreferredWidth(BField* field, BView* parent) const
{
	return fWidth;
}


/**
 * @brief Return whether this column is currently visible.
 *
 * @return true if the column is shown, false if hidden.
 */
bool
BColumn::IsVisible() const
{
	return fVisible;
}


/**
 * @brief Set the visibility of this column.
 *
 * If the column is attached to a list view and its visibility is changing,
 * delegates to BColumnListView::SetColumnVisible() which handles the
 * necessary relayout and redraws.
 *
 * @param visible true to show the column, false to hide it.
 */
void
BColumn::SetVisible(bool visible)
{
	if (fList && (fVisible != visible))
		fList->SetColumnVisible(this, visible);
}


/**
 * @brief Return whether this column's title heading is shown.
 *
 * @return true if the heading is shown, false otherwise.
 */
bool
BColumn::ShowHeading() const
{
	return fShowHeading;
}


/**
 * @brief Set whether this column's title heading should be shown.
 *
 * @param state true to show the heading, false to hide it.
 */
void
BColumn::SetShowHeading(bool state)
{
	fShowHeading = state;
}


/**
 * @brief Return the horizontal text alignment used when drawing cell content.
 *
 * @return One of B_ALIGN_LEFT, B_ALIGN_CENTER, or B_ALIGN_RIGHT.
 */
alignment
BColumn::Alignment() const
{
	return fAlignment;
}


/**
 * @brief Set the horizontal text alignment for cell content in this column.
 *
 * @param align The desired alignment (B_ALIGN_LEFT, B_ALIGN_CENTER, or B_ALIGN_RIGHT).
 */
void
BColumn::SetAlignment(alignment align)
{
	fAlignment = align;
}


/**
 * @brief Return whether this column receives mouse events for its field cells.
 *
 * @return true if the column wants mouse events, false otherwise.
 */
bool
BColumn::WantsEvents() const
{
	return fWantsEvents;
}


/**
 * @brief Set whether this column should receive mouse events for its field cells.
 *
 * When enabled, MouseDown(), MouseMoved(), and MouseUp() are dispatched to the
 * column for each cell interaction.
 *
 * @param state true to enable event delivery, false to disable.
 */
void
BColumn::SetWantsEvents(bool state)
{
	fWantsEvents = state;
}


/**
 * @brief Return the logical field number assigned to this column.
 *
 * The logical field number is the index used to look up a BField within a
 * BRow and is set when the column is added to a BColumnListView.
 *
 * @return The logical field index.
 */
int32
BColumn::LogicalFieldNum() const
{
	return fFieldID;
}


/**
 * @brief Return whether this column can display the given field type.
 *
 * The default implementation accepts all field types. Subclasses should
 * override to restrict to the specific BField subclass they understand.
 *
 * @param field The field whose type should be checked.
 * @return true if the column can display @p field, false otherwise.
 */
bool
BColumn::AcceptsField(const BField*) const
{
	return true;
}


// #pragma mark -


/**
 * @brief Construct a BColumnListView with an explicit frame rectangle (legacy layout).
 *
 * @param rect                    Frame rectangle in the parent view's coordinate system.
 * @param name                    View name.
 * @param resizingMode            Resizing mode flags (B_FOLLOW_* constants).
 * @param flags                   View flags; B_WILL_DRAW, B_FRAME_EVENTS, and
 *                                B_FULL_UPDATE_ON_RESIZE are added automatically.
 * @param border                  Border style (B_NO_BORDER, B_PLAIN_BORDER, or B_FANCY_BORDER).
 * @param showHorizontalScrollbar true to show the horizontal scroll bar.
 */
BColumnListView::BColumnListView(BRect rect, const char* name,
	uint32 resizingMode, uint32 flags, border_style border,
	bool showHorizontalScrollbar)
	:
	BView(rect, name, resizingMode,
		flags | B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fStatusView(NULL),
	fSelectionMessage(NULL),
	fSortingEnabled(true),
	fLatchWidth(kLatchWidth),
	fBorderStyle(border),
	fShowingHorizontalScrollBar(showHorizontalScrollbar)
{
	_Init();
}


/**
 * @brief Construct a BColumnListView using the layout-manager system (no explicit frame).
 *
 * @param name                    View name.
 * @param flags                   View flags; B_WILL_DRAW, B_FRAME_EVENTS, and
 *                                B_FULL_UPDATE_ON_RESIZE are added automatically.
 * @param border                  Border style (B_NO_BORDER, B_PLAIN_BORDER, or B_FANCY_BORDER).
 * @param showHorizontalScrollbar true to show the horizontal scroll bar.
 */
BColumnListView::BColumnListView(const char* name, uint32 flags,
	border_style border, bool showHorizontalScrollbar)
	:
	BView(name, flags | B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fStatusView(NULL),
	fSelectionMessage(NULL),
	fSortingEnabled(true),
	fLatchWidth(kLatchWidth),
	fBorderStyle(border),
	fShowingHorizontalScrollBar(showHorizontalScrollbar)
{
	_Init();
}


/**
 * @brief Destroy the BColumnListView and free all owned BColumn objects.
 *
 * BRow objects are not owned by the view and are not deleted here.
 */
BColumnListView::~BColumnListView()
{
	while (BColumn* column = (BColumn*)fColumns.RemoveItem((int32)0))
		delete column;
}


/**
 * @brief Called when the user begins dragging a row.
 *
 * The default implementation returns false, meaning dragging is not handled.
 * Subclasses may override to initiate a drag-and-drop operation.
 *
 * @param point     The point in view coordinates where the drag started.
 * @param wasSelected true if the dragged row was selected at the time of the drag.
 * @return true if the drag was accepted and initiated, false to cancel.
 */
bool
BColumnListView::InitiateDrag(BPoint, bool)
{
	return false;
}


/**
 * @brief Called when a BMessage is dropped onto the view during a drag-and-drop operation.
 *
 * The default implementation does nothing. Subclasses may override to handle
 * the dropped data.
 *
 * @param message The dropped message.
 * @param point   The drop location in view coordinates.
 */
void
BColumnListView::MessageDropped(BMessage*, BPoint)
{
}


/**
 * @brief Expand or collapse the child rows of @p row.
 *
 * @param row  The parent row whose child list should be expanded or collapsed.
 * @param Open true to expand, false to collapse.
 */
void
BColumnListView::ExpandOrCollapse(BRow* row, bool Open)
{
	fOutlineView->ExpandOrCollapse(row, Open);
}


/**
 * @brief Invoke the list view's action message.
 *
 * If @p message is NULL, the message set with SetMessage() is used.
 *
 * @param message The message to send, or NULL to use the default invocation message.
 * @return B_OK on success, or an error code.
 */
status_t
BColumnListView::Invoke(BMessage* message)
{
	if (message == 0)
		message = Message();

	return BInvoker::Invoke(message);
}


/**
 * @brief Called when an item is invoked (e.g., double-clicked or Enter pressed).
 *
 * The default implementation calls Invoke() with the default invocation message.
 * Subclasses may override to perform a custom action.
 */
void
BColumnListView::ItemInvoked()
{
	Invoke();
}


/**
 * @brief Set the message sent when an item is invoked.
 *
 * @param message The message to send on invocation; ownership is transferred.
 */
void
BColumnListView::SetInvocationMessage(BMessage* message)
{
	SetMessage(message);
}


/**
 * @brief Return the message sent when an item is invoked.
 *
 * @return The current invocation message, or NULL if none is set.
 */
BMessage*
BColumnListView::InvocationMessage() const
{
	return Message();
}


/**
 * @brief Return the what code of the current invocation message.
 *
 * @return The invocation message's what field, or 0 if no message is set.
 */
uint32
BColumnListView::InvocationCommand() const
{
	return Command();
}


/**
 * @brief Return the row that currently holds keyboard focus.
 *
 * @return The focused BRow, or NULL if no row has focus.
 */
BRow*
BColumnListView::FocusRow() const
{
	return fOutlineView->FocusRow();
}


/**
 * @brief Set keyboard focus to the row at the given top-level index.
 *
 * @param Index  Zero-based index of the root-level row to focus.
 * @param Select If true, the focused row is also selected.
 */
void
BColumnListView::SetFocusRow(int32 Index, bool Select)
{
	SetFocusRow(RowAt(Index), Select);
}


/**
 * @brief Set keyboard focus to the given row.
 *
 * @param row    The row to focus.
 * @param Select If true, the focused row is also added to the selection.
 */
void
BColumnListView::SetFocusRow(BRow* row, bool Select)
{
	fOutlineView->SetFocusRow(row, Select);
}


/**
 * @brief Enable or disable mouse-tracking (roll-over highlight) in the outline view.
 *
 * @param Enabled true to enable mouse tracking, false to disable.
 */
void
BColumnListView::SetMouseTrackingEnabled(bool Enabled)
{
	fOutlineView->SetMouseTrackingEnabled(Enabled);
}


/**
 * @brief Return the current selection mode of the list.
 *
 * @return B_SINGLE_SELECTION_LIST or B_MULTIPLE_SELECTION_LIST.
 */
list_view_type
BColumnListView::SelectionMode() const
{
	return fOutlineView->SelectionMode();
}


/**
 * @brief Remove @p row from the selection without clearing other selected rows.
 *
 * @param row The row to deselect.
 */
void
BColumnListView::Deselect(BRow* row)
{
	fOutlineView->Deselect(row);
}


/**
 * @brief Add @p row to the current selection.
 *
 * In B_SINGLE_SELECTION_LIST mode, any previously selected row is first
 * deselected.
 *
 * @param row The row to add to the selection.
 */
void
BColumnListView::AddToSelection(BRow* row)
{
	fOutlineView->AddToSelection(row);
}


/**
 * @brief Deselect all currently selected rows.
 */
void
BColumnListView::DeselectAll()
{
	fOutlineView->DeselectAll();
}


/**
 * @brief Iterate over selected rows.
 *
 * Pass NULL to get the first selected row. Pass the previously returned row
 * to get the next selected row.
 *
 * @param lastSelected The last row returned by a previous call, or NULL to start.
 * @return The next selected BRow, or NULL when the selection is exhausted.
 */
BRow*
BColumnListView::CurrentSelection(BRow* lastSelected) const
{
	return fOutlineView->CurrentSelection(lastSelected);
}


/**
 * @brief Called when the selection changes.
 *
 * Sends the selection message if one has been set with SetSelectionMessage().
 * Subclasses may override to be notified of selection changes without
 * needing to use a message.
 */
void
BColumnListView::SelectionChanged()
{
	if (fSelectionMessage)
		Invoke(fSelectionMessage);
}


/**
 * @brief Set the message sent whenever the selection changes.
 *
 * Any previously set selection message is deleted. Ownership of @p message
 * is transferred to the list view.
 *
 * @param message The message to send on selection change, or NULL to disable.
 */
void
BColumnListView::SetSelectionMessage(BMessage* message)
{
	if (fSelectionMessage == message)
		return;

	delete fSelectionMessage;
	fSelectionMessage = message;
}


/**
 * @brief Return the message sent whenever the selection changes.
 *
 * @return The selection message, or NULL if none has been set.
 */
BMessage*
BColumnListView::SelectionMessage()
{
	return fSelectionMessage;
}


/**
 * @brief Return the what code of the current selection message.
 *
 * @return The selection message's what field, or 0 if no message is set.
 */
uint32
BColumnListView::SelectionCommand() const
{
	if (fSelectionMessage)
		return fSelectionMessage->what;

	return 0;
}


/**
 * @brief Set whether the list allows single or multiple row selection.
 *
 * Changing the mode deselects all currently selected rows.
 *
 * @param mode B_SINGLE_SELECTION_LIST or B_MULTIPLE_SELECTION_LIST.
 */
void
BColumnListView::SetSelectionMode(list_view_type mode)
{
	fOutlineView->SetSelectionMode(mode);
}


/**
 * @brief Enable or disable column-header-click sorting.
 *
 * Disabling sorting also clears the current sort column list and redraws
 * the header to remove sort indicators.
 *
 * @param enabled true to enable sorting, false to disable.
 */
void
BColumnListView::SetSortingEnabled(bool enabled)
{
	fSortingEnabled = enabled;
	fSortColumns.MakeEmpty();
	fTitleView->Invalidate();
		// erase sort indicators
}


/**
 * @brief Return whether column-header-click sorting is enabled.
 *
 * @return true if sorting is enabled, false otherwise.
 */
bool
BColumnListView::SortingEnabled() const
{
	return fSortingEnabled;
}


/**
 * @brief Add or replace the primary sort column and trigger a re-sort.
 *
 * If sorting is disabled this method has no effect. When @p add is false all
 * existing sort columns are removed before @p column is added.
 *
 * @param column    The column to sort by.
 * @param add       If true, @p column is appended to any existing sort columns
 *                  (multi-column sort). If false, the sort list is cleared first.
 * @param ascending true to sort ascending, false for descending.
 */
void
BColumnListView::SetSortColumn(BColumn* column, bool add, bool ascending)
{
	if (!SortingEnabled())
		return;

	if (!add)
		fSortColumns.MakeEmpty();

	if (!fSortColumns.HasItem(column))
		fSortColumns.AddItem(column);

	column->fSortAscending = ascending;
	fTitleView->Invalidate();
	fOutlineView->StartSorting();
}


/**
 * @brief Remove all sort columns so that no sort is active.
 *
 * The column headers are redrawn to remove the sort indicator arrows.
 */
void
BColumnListView::ClearSortColumns()
{
	fSortColumns.MakeEmpty();
	fTitleView->Invalidate();
		// erase sort indicators
}


/**
 * @brief Attach a status view to the left side of the horizontal scroll bar.
 *
 * The status view is resized to fit within the available space and the
 * horizontal scroll bar is shortened accordingly. The view's width is clamped
 * to at most half the list view width.
 *
 * @param view The view to attach. It must not already be a child of another view.
 */
void
BColumnListView::AddStatusView(BView* view)
{
	BRect bounds = Bounds();
	float width = view->Bounds().Width();
	if (width > bounds.Width() / 2)
		width = bounds.Width() / 2;

	fStatusView = view;

	Window()->BeginViewTransaction();
	fHorizontalScrollBar->ResizeBy(-(width + 1), 0);
	fHorizontalScrollBar->MoveBy((width + 1), 0);
	AddChild(view);

	BRect viewRect(bounds);
	viewRect.right = width;
	viewRect.top = viewRect.bottom - B_H_SCROLL_BAR_HEIGHT;
	if (fBorderStyle == B_PLAIN_BORDER)
		viewRect.OffsetBy(1, -1);
	else if (fBorderStyle == B_FANCY_BORDER)
		viewRect.OffsetBy(2, -2);

	view->SetResizingMode(B_FOLLOW_LEFT | B_FOLLOW_BOTTOM);
	view->ResizeTo(viewRect.Width(), viewRect.Height());
	view->MoveTo(viewRect.left, viewRect.top);
	Window()->EndViewTransaction();
}


/**
 * @brief Detach and return the previously added status view.
 *
 * Restores the horizontal scroll bar to its original full width.
 *
 * @return The removed status view, or NULL if none was attached.
 */
BView*
BColumnListView::RemoveStatusView()
{
	if (fStatusView) {
		float width = fStatusView->Bounds().Width();
		Window()->BeginViewTransaction();
		fStatusView->RemoveSelf();
		fHorizontalScrollBar->MoveBy(-width, 0);
		fHorizontalScrollBar->ResizeBy(width, 0);
		Window()->EndViewTransaction();
	}

	BView* view = fStatusView;
	fStatusView = 0;
	return view;
}


/**
 * @brief Add a column to the list view at the given logical field index.
 *
 * If a column with the same logical field index already exists it is removed
 * first. The column's width is clamped to [MinWidth(), MaxWidth()].
 *
 * @param column            The column to add. Ownership is transferred to the view.
 * @param logicalFieldIndex The logical field index that maps this column to BRow fields.
 */
void
BColumnListView::AddColumn(BColumn* column, int32 logicalFieldIndex)
{
	ASSERT(column != NULL);

	column->fList = this;
	column->fFieldID = logicalFieldIndex;

	// sanity check -- if there is already a field with this ID, remove it.
	for (int32 index = 0; index < fColumns.CountItems(); index++) {
		BColumn* existingColumn = (BColumn*) fColumns.ItemAt(index);
		if (existingColumn && existingColumn->fFieldID == logicalFieldIndex) {
			RemoveColumn(existingColumn);
			break;
		}
	}

	if (column->Width() < column->MinWidth())
		column->SetWidth(column->MinWidth());
	else if (column->Width() > column->MaxWidth())
		column->SetWidth(column->MaxWidth());

	fColumns.AddItem((void*) column);
	fTitleView->ColumnAdded(column);
}


/**
 * @brief Move @p column to the given display position.
 *
 * @param column The column to reorder.
 * @param index  The target display index, or -1 to move to the end.
 */
void
BColumnListView::MoveColumn(BColumn* column, int32 index)
{
	ASSERT(column != NULL);
	fTitleView->MoveColumn(column, index);
}


/**
 * @brief Remove @p column from the list view.
 *
 * The column is hidden first so the outline view is redrawn, then removed from
 * the internal column list. Ownership is not transferred back to the caller;
 * the column must be deleted separately.
 *
 * @param column The column to remove.
 */
void
BColumnListView::RemoveColumn(BColumn* column)
{
	if (fColumns.HasItem(column)) {
		SetColumnVisible(column, false);
		if (Window() != NULL)
			Window()->UpdateIfNeeded();
		fColumns.RemoveItem(column);
	}
}


/**
 * @brief Return the total number of columns (visible and hidden).
 *
 * @return The number of columns currently registered with the list view.
 */
int32
BColumnListView::CountColumns() const
{
	return fColumns.CountItems();
}


/**
 * @brief Return the column at the given display index.
 *
 * @param field Zero-based column index in the current display order.
 * @return The BColumn at @p field, or NULL if @p field is out of range.
 */
BColumn*
BColumnListView::ColumnAt(int32 field) const
{
	return (BColumn*) fColumns.ItemAt(field);
}


/**
 * @brief Return the visible column located at the given view-coordinate point.
 *
 * @param point A point in the outline view's coordinate system.
 * @return The BColumn whose display area contains @p point, or NULL if none does.
 */
BColumn*
BColumnListView::ColumnAt(BPoint point) const
{
	float left = MAX(kLeftMargin, LatchWidth());

	for (int i = 0; BColumn* column = (BColumn*)fColumns.ItemAt(i); i++) {
		if (column == NULL || !column->IsVisible())
			continue;

		float right = left + column->Width();
		if (point.x >= left && point.x <= right)
			return column;

		left = right + 1;
	}

	return NULL;
}


/**
 * @brief Show or hide the given column.
 *
 * @param column  The column whose visibility should change.
 * @param visible true to show, false to hide.
 */
void
BColumnListView::SetColumnVisible(BColumn* column, bool visible)
{
	fTitleView->SetColumnVisible(column, visible);
}


/**
 * @brief Show or hide the column at the given display index.
 *
 * @param index     Zero-based column display index.
 * @param isVisible true to show, false to hide.
 */
void
BColumnListView::SetColumnVisible(int32 index, bool isVisible)
{
	BColumn* column = ColumnAt(index);
	if (column != NULL)
		column->SetVisible(isVisible);
}


/**
 * @brief Return whether the column at the given display index is visible.
 *
 * @param index Zero-based column display index.
 * @return true if the column is visible, false if hidden or @p index is invalid.
 */
bool
BColumnListView::IsColumnVisible(int32 index) const
{
	BColumn* column = ColumnAt(index);
	if (column != NULL)
		return column->IsVisible();

	return false;
}


/**
 * @brief Set the column interaction flags controlling which operations are allowed.
 *
 * @param flags A combination of B_ALLOW_COLUMN_MOVE, B_ALLOW_COLUMN_RESIZE,
 *              B_ALLOW_COLUMN_POPUP, and B_ALLOW_COLUMN_REMOVE.
 */
void
BColumnListView::SetColumnFlags(column_flags flags)
{
	fTitleView->SetColumnFlags(flags);
}


/**
 * @brief Resize the column at @p index to its preferred width.
 *
 * The preferred width is determined by iterating over all visible rows and
 * measuring the widest field in that column.
 *
 * @param index Zero-based column display index.
 */
void
BColumnListView::ResizeColumnToPreferred(int32 index)
{
	BColumn* column = ColumnAt(index);
	if (column == NULL)
		return;

	// get the preferred column width
	float width = fOutlineView->GetColumnPreferredWidth(column);

	// set it
	float oldWidth = column->Width();
	column->SetWidth(width);

	fTitleView->ColumnResized(column, oldWidth);
	fOutlineView->Invalidate();
}


/**
 * @brief Resize every column to its preferred width.
 *
 * @see ResizeColumnToPreferred()
 */
void
BColumnListView::ResizeAllColumnsToPreferred()
{
	int32 count = CountColumns();
	for (int32 i = 0; i < count; i++)
		ResizeColumnToPreferred(i);
}


/**
 * @brief Return the row at the given index within @p parentRow's children (const).
 *
 * @param Index     Zero-based row index within the parent's child list, or within
 *                  the root list if @p parentRow is NULL.
 * @param parentRow The parent row, or NULL to index into the root list.
 * @return The BRow at @p Index, or NULL if @p Index is out of range.
 */
const BRow*
BColumnListView::RowAt(int32 Index, BRow* parentRow) const
{
	if (parentRow == 0)
		return fOutlineView->RowList()->ItemAt(Index);

	return parentRow->fChildList ? parentRow->fChildList->ItemAt(Index) : NULL;
}


/**
 * @brief Return the row at the given index within @p parentRow's children.
 *
 * @param Index     Zero-based row index.
 * @param parentRow The parent row, or NULL for the root list.
 * @return The BRow at @p Index, or NULL if out of range.
 */
BRow*
BColumnListView::RowAt(int32 Index, BRow* parentRow)
{
	if (parentRow == 0)
		return fOutlineView->RowList()->ItemAt(Index);

	return parentRow->fChildList ? parentRow->fChildList->ItemAt(Index) : 0;
}


/**
 * @brief Return the row displayed at the given view-coordinate point (const).
 *
 * @param point A point in the outline view's coordinate system.
 * @return The BRow under @p point, or NULL if no row is there.
 */
const BRow*
BColumnListView::RowAt(BPoint point) const
{
	float top;
	int32 indent;
	return fOutlineView->FindRow(point.y, &indent, &top);
}


/**
 * @brief Return the row displayed at the given view-coordinate point.
 *
 * @param point A point in the outline view's coordinate system.
 * @return The BRow under @p point, or NULL if no row is there.
 */
BRow*
BColumnListView::RowAt(BPoint point)
{
	float top;
	int32 indent;
	return fOutlineView->FindRow(point.y, &indent, &top);
}


/**
 * @brief Retrieve the bounding rectangle of @p row in the outline view's coordinates.
 *
 * @param row     The row to find.
 * @param outRect Output parameter that receives the row's bounding rectangle.
 * @return true if the row was found and @p outRect was set, false otherwise.
 */
bool
BColumnListView::GetRowRect(const BRow* row, BRect* outRect) const
{
	return fOutlineView->FindRect(row, outRect);
}


/**
 * @brief Find the parent row of @p row.
 *
 * @param row        The row whose parent should be found.
 * @param _parent    Output: set to the parent BRow, or NULL for root rows.
 * @param _isVisible Output: set to true if @p row is visible in the current tree state.
 * @return true if @p row has a parent, false if it is a root row.
 */
bool
BColumnListView::FindParent(BRow* row, BRow** _parent, bool* _isVisible) const
{
	return fOutlineView->FindParent(row, _parent, _isVisible);
}


/**
 * @brief Return the index of @p row within its parent's child list (or the root list).
 *
 * @param row The row to locate.
 * @return The zero-based index of @p row, or B_ERROR if not found.
 */
int32
BColumnListView::IndexOf(BRow* row)
{
	return fOutlineView->IndexOf(row);
}


/**
 * @brief Return the number of child rows under @p parentRow.
 *
 * @param parentRow The parent row to count children of, or NULL to count root rows.
 * @return The number of immediate child rows.
 */
int32
BColumnListView::CountRows(BRow* parentRow) const
{
	if (parentRow == 0)
		return fOutlineView->RowList()->CountItems();
	if (parentRow->fChildList)
		return parentRow->fChildList->CountItems();
	else
		return 0;
}


/**
 * @brief Append @p row to the end of the root list or @p parentRow's children.
 *
 * Equivalent to calling AddRow(row, -1, parentRow).
 *
 * @param row       The row to add. Ownership is not transferred.
 * @param parentRow The parent row, or NULL to add at the root level.
 */
void
BColumnListView::AddRow(BRow* row, BRow* parentRow)
{
	AddRow(row, -1, parentRow);
}


/**
 * @brief Add @p row at the given index within the root list or @p parentRow's children.
 *
 * If sorting is enabled, @p index is ignored and the row is inserted in sorted order.
 * Pass -1 for @p index to append to the end.
 *
 * @param row       The row to add. Ownership is not transferred.
 * @param index     The desired insertion index, or -1 to append.
 * @param parentRow The parent row, or NULL to add at the root level.
 */
void
BColumnListView::AddRow(BRow* row, int32 index, BRow* parentRow)
{
	row->fChildList = 0;
	row->fList = this;
	row->ValidateFields();
	fOutlineView->AddRow(row, index, parentRow);
}


/**
 * @brief Add multiple rows at once, starting at the given index.
 *
 * This is more efficient than calling AddRow() for each row individually when
 * adding many rows to the same parent, as invalidation is batched.
 * All rows must share the same parent.
 *
 * @param rows   A BList of BRow pointers to add. Ownership is not transferred.
 * @param index  The desired insertion index for the first row, or -1 to append.
 * @param parent The parent row, or NULL to add at the root level.
 */
void
BColumnListView::AddRows(BList* rows, int32 index, BRow* parent)
{
	for (int32 i = rows->CountItems() - 1; i >= 0; i--) {
		BRow* row = static_cast<BRow*>(rows->ItemAt(i));
		row->fChildList = 0;
		row->fList = this;
		row->ValidateFields();
	}

	fOutlineView->AddRows(rows, index, parent);
}


/**
 * @brief Remove @p row from the list view.
 *
 * The row is detached from the outline view and its fList pointer is cleared.
 * Ownership of the row is returned to the caller; it must be deleted separately.
 *
 * @param row The row to remove.
 */
void
BColumnListView::RemoveRow(BRow* row)
{
	fOutlineView->RemoveRow(row);
	row->fList = NULL;
}


/*!	This method will allow for multiple rows to be removed at the same time. All
	of the rows must belong to the same parent row.
*/
void
BColumnListView::RemoveRows(BList* rows)
{
	fOutlineView->RemoveRows(rows);

	for (int32 i = rows->CountItems() - 1; i >= 0; i--) {
		BRow* row = static_cast<BRow*>(rows->ItemAt(i));
		row->fList = NULL;
	}
}


/**
 * @brief Notify the list view that the data in @p row has changed.
 *
 * If sorting is active and the row's sort key has changed, the row is
 * repositioned in the sorted order. Otherwise the row is simply redrawn.
 *
 * @param row The row whose data has been updated.
 */
void
BColumnListView::UpdateRow(BRow* row)
{
	fOutlineView->UpdateRow(row);
}


/**
 * @brief Swap two rows at the given indices within their respective parent containers.
 *
 * Both parent containers must be non-NULL and valid. The region spanning both
 * rows is invalidated so the view is redrawn.
 *
 * @param index1    Index of the first row within @p parentRow1's child list
 *                  (or the root list if NULL).
 * @param index2    Index of the second row within @p parentRow2's child list
 *                  (or the root list if NULL).
 * @param parentRow1 Parent of the first row, or NULL for root.
 * @param parentRow2 Parent of the second row, or NULL for root.
 * @return true if the swap succeeded, false if either row could not be located.
 */
bool
BColumnListView::SwapRows(int32 index1, int32 index2, BRow* parentRow1,
	BRow* parentRow2)
{
	BRow* row1 = NULL;
	BRow* row2 = NULL;

	BRowContainer* container1 = NULL;
	BRowContainer* container2 = NULL;

	if (parentRow1 == NULL)
		container1 = fOutlineView->RowList();
	else
		container1 = parentRow1->fChildList;

	if (container1 == NULL)
		return false;

	if (parentRow2 == NULL)
		container2 = fOutlineView->RowList();
	else
		container2 = parentRow2->fChildList;

	if (container2 == NULL)
		return false;

	row1 = container1->ItemAt(index1);

	if (row1 == NULL)
		return false;

	row2 = container2->ItemAt(index2);

	if (row2 == NULL)
		return false;

	container1->ReplaceItem(index2, row1);
	container2->ReplaceItem(index1, row2);

	BRect rect1;
	BRect rect2;
	BRect rect;

	fOutlineView->FindRect(row1, &rect1);
	fOutlineView->FindRect(row2, &rect2);

	rect = rect1 | rect2;

	fOutlineView->Invalidate(rect);

	return true;
}


/**
 * @brief Scroll the view so that @p row is visible.
 *
 * @param row The row to scroll into view.
 */
void
BColumnListView::ScrollTo(const BRow* row)
{
	fOutlineView->ScrollTo(row);
}


/**
 * @brief Scroll the outline view to the given position.
 *
 * @param point The top-left position to scroll to, in the outline view's coordinate system.
 */
void
BColumnListView::ScrollTo(BPoint point)
{
	fOutlineView->ScrollTo(point);
}


/**
 * @brief Remove and delete all rows from the list view.
 *
 * The selection is cleared and the scroll bar range is reset.
 */
void
BColumnListView::Clear()
{
	fOutlineView->Clear();
}


/**
 * @brief Invalidate the display area of @p row so it is redrawn on the next update.
 *
 * Has no effect if @p row's bounding rectangle does not intersect the visible area.
 *
 * @param row The row to invalidate.
 */
void
BColumnListView::InvalidateRow(BRow* row)
{
	BRect updateRect;
	GetRowRect(row, &updateRect);

	if (fOutlineView->VisibleRect().Intersects(updateRect))
		fOutlineView->Invalidate(updateRect);
}


// This method is deprecated.
/**
 * @brief Set the font for both the row area and the column header (deprecated).
 *
 * Prefer SetFont(ColumnListViewFont, const BFont*, uint32) for clarity.
 *
 * @param font The font to apply.
 * @param mask Font property mask (see BFont::SetFamilyAndFace(), etc.).
 */
void
BColumnListView::SetFont(const BFont* font, uint32 mask)
{
	fOutlineView->SetFont(font, mask);
	fTitleView->SetFont(font, mask);
}


/**
 * @brief Set the font for either the row area or the column header.
 *
 * @param font_num B_FONT_ROW to set the row font, B_FONT_HEADER for the header.
 * @param font     The font to apply.
 * @param mask     Font property mask.
 */
void
BColumnListView::SetFont(ColumnListViewFont font_num, const BFont* font,
	uint32 mask)
{
	switch (font_num) {
		case B_FONT_ROW:
			fOutlineView->SetFont(font, mask);
			break;

		case B_FONT_HEADER:
			fTitleView->SetFont(font, mask);
			break;

		default:
			ASSERT(false);
			break;
	}
}


/**
 * @brief Retrieve the font currently used for the row area or column header.
 *
 * @param font_num B_FONT_ROW to get the row font, B_FONT_HEADER for the header.
 * @param font     Output parameter that receives the font.
 */
void
BColumnListView::GetFont(ColumnListViewFont font_num, BFont* font) const
{
	switch (font_num) {
		case B_FONT_ROW:
			fOutlineView->GetFont(font);
			break;

		case B_FONT_HEADER:
			fTitleView->GetFont(font);
			break;

		default:
			ASSERT(false);
			break;
	}
}


/**
 * @brief Override a specific color slot in the list view's color palette.
 *
 * Enables custom colors mode (fCustomColors = true) so that system color
 * updates no longer override the custom values.
 *
 * @param colorIndex The color slot to set (B_COLOR_BACKGROUND, B_COLOR_TEXT, etc.).
 * @param color      The color value to assign.
 */
void
BColumnListView::SetColor(ColumnListViewColor colorIndex, const rgb_color color)
{
	if ((int)colorIndex < 0) {
		ASSERT(false);
		colorIndex = (ColumnListViewColor)0;
	}

	if ((int)colorIndex >= (int)B_COLOR_TOTAL) {
		ASSERT(false);
		colorIndex = (ColumnListViewColor)(B_COLOR_TOTAL - 1);
	}

	fColorList[colorIndex] = color;
	fCustomColors = true;
}


/**
 * @brief Reset all colors to the current system UI colors.
 *
 * Disables custom colors mode so that subsequent B_COLORS_UPDATED messages
 * will update the palette automatically.
 */
void
BColumnListView::ResetColors()
{
	fCustomColors = false;
	_UpdateColors();
	Invalidate();
}


/**
 * @brief Return the color currently assigned to the given color slot.
 *
 * @param colorIndex The color slot to query.
 * @return The rgb_color for that slot.
 */
rgb_color
BColumnListView::Color(ColumnListViewColor colorIndex) const
{
	if ((int)colorIndex < 0) {
		ASSERT(false);
		colorIndex = (ColumnListViewColor)0;
	}

	if ((int)colorIndex >= (int)B_COLOR_TOTAL) {
		ASSERT(false);
		colorIndex = (ColumnListViewColor)(B_COLOR_TOTAL - 1);
	}

	return fColorList[colorIndex];
}


/**
 * @brief Set the high color of the list view (stored for column use).
 *
 * @note Calling this will not trigger an immediate repaint because doing so
 *       causes an infinite redraw loop in the current implementation.
 *
 * @param color The new high color.
 */
void
BColumnListView::SetHighColor(rgb_color color)
{
	BView::SetHighColor(color);
//	fOutlineView->Invalidate();
		// Redraw with the new color.
		// Note that this will currently cause an infinite loop, refreshing
		// over and over. A better solution is needed.
}


/**
 * @brief Convenience method to set the selection highlight color.
 *
 * @param color The background color used for selected rows.
 */
void
BColumnListView::SetSelectionColor(rgb_color color)
{
	fColorList[B_COLOR_SELECTION] = color;
	fCustomColors = true;
}


/**
 * @brief Convenience method to set the list background color.
 *
 * Triggers an immediate repaint of the outline view.
 *
 * @param color The new background color.
 */
void
BColumnListView::SetBackgroundColor(rgb_color color)
{
	fColorList[B_COLOR_BACKGROUND] = color;
	fCustomColors = true;
	fOutlineView->Invalidate();
		// repaint with new color
}


/**
 * @brief Convenience method to set the edit-mode background color.
 *
 * @param color The background color used in edit mode.
 */
void
BColumnListView::SetEditColor(rgb_color color)
{
	fColorList[B_COLOR_EDIT_BACKGROUND] = color;
	fCustomColors = true;
}


/**
 * @brief Return the current selection highlight color.
 *
 * @return The color used for selected-row backgrounds.
 */
const rgb_color
BColumnListView::SelectionColor() const
{
	return fColorList[B_COLOR_SELECTION];
}


/**
 * @brief Return the current list background color.
 *
 * @return The color used for unselected row backgrounds.
 */
const rgb_color
BColumnListView::BackgroundColor() const
{
	return fColorList[B_COLOR_BACKGROUND];
}


/**
 * @brief Return the current edit-mode background color.
 *
 * @return The color used for the edit-mode background.
 */
const rgb_color
BColumnListView::EditColor() const
{
	return fColorList[B_COLOR_EDIT_BACKGROUND];
}


/**
 * @brief Suggest a text baseline position for rendering text in a field cell.
 *
 * Computes the point at which a text pen should be placed so that the text
 * is vertically centered within the row and horizontally offset from the left
 * edge of the column.
 *
 * @param row      The row containing the cell.
 * @param inColumn The column containing the cell.
 * @return A BPoint suitable for passing to MovePenTo() before DrawString().
 */
BPoint
BColumnListView::SuggestTextPosition(const BRow* row,
	const BColumn* inColumn) const
{
	BRect rect(GetFieldRect(row, inColumn));

	font_height fh;
	fOutlineView->GetFontHeight(&fh);
	float baseline = floor(rect.top + fh.ascent
		+ (rect.Height() + 1 - (fh.ascent + fh.descent)) / 2);
	return BPoint(rect.left + 8, baseline);
}


/**
 * @brief Return the bounding rectangle of the field cell for @p row and @p inColumn.
 *
 * If @p inColumn is NULL, the full row rectangle is returned.
 *
 * @param row      The row containing the cell.
 * @param inColumn The column whose cell rectangle should be returned, or NULL
 *                 for the entire row.
 * @return The bounding BRect of the requested cell in outline-view coordinates.
 */
BRect
BColumnListView::GetFieldRect(const BRow* row, const BColumn* inColumn) const
{
	BRect rect;
	GetRowRect(row, &rect);
	if (inColumn != NULL) {
		float leftEdge = MAX(kLeftMargin, LatchWidth());
		for (int index = 0; index < fColumns.CountItems(); index++) {
			BColumn* column = (BColumn*) fColumns.ItemAt(index);
			if (column == NULL || !column->IsVisible())
				continue;

			if (column == inColumn) {
				rect.left = leftEdge;
				rect.right = rect.left + column->Width();
				break;
			}

			leftEdge += column->Width() + 1;
		}
	}

	return rect;
}


/**
 * @brief Set the pixel width reserved for the expand/collapse latch widget.
 *
 * @param width The latch width in pixels.
 */
void
BColumnListView::SetLatchWidth(float width)
{
	fLatchWidth = width;
	Invalidate();
}


/**
 * @brief Return the current latch width.
 *
 * @return The latch width in pixels.
 */
float
BColumnListView::LatchWidth() const
{
	return fLatchWidth;
}

/**
 * @brief Draw the expand/collapse latch widget for a row.
 *
 * Renders a square +/- box inside @p rect according to the current @p position.
 * Subclasses may override to provide a custom latch appearance.
 *
 * @param view     The view to draw into.
 * @param rect     The bounding rectangle allocated for the latch.
 * @param position B_OPEN_LATCH, B_CLOSED_LATCH, B_PRESSED_LATCH, or B_NO_LATCH.
 * @param row      The row whose latch is being drawn (unused by the default implementation).
 */
void
BColumnListView::DrawLatch(BView* view, BRect rect, LatchType position, BRow*)
{
	const int32 rectInset = 4;

	// make square
	int32 sideLen = rect.IntegerWidth();
	if (sideLen > rect.IntegerHeight())
		sideLen = rect.IntegerHeight();

	// make center
	int32 halfWidth  = rect.IntegerWidth() / 2;
	int32 halfHeight = rect.IntegerHeight() / 2;
	int32 halfSide   = sideLen / 2;

	float left = rect.left + halfWidth  - halfSide;
	float top  = rect.top  + halfHeight - halfSide;

	BRect itemRect(left, top, left + sideLen, top + sideLen);

	// Why it is a pixel high? I don't know.
	itemRect.OffsetBy(0, -1);

	itemRect.InsetBy(rectInset, rectInset);

	// make it an odd number of pixels wide, the latch looks better this way
	if ((itemRect.IntegerWidth() % 2) == 1) {
		itemRect.right += 1;
		itemRect.bottom += 1;
	}

	rgb_color highColor = view->HighColor();
	if (highColor.IsLight())
		view->SetHighColor(make_color(0, 0, 0));
	else
		view->SetHighColor(make_color(255, 255, 255));

	switch (position) {
		case B_OPEN_LATCH:
			view->StrokeRect(itemRect);
			view->StrokeLine(
				BPoint(itemRect.left + 2,
					(itemRect.top + itemRect.bottom) / 2),
				BPoint(itemRect.right - 2,
					(itemRect.top + itemRect.bottom) / 2));
			break;

		case B_PRESSED_LATCH:
			view->StrokeRect(itemRect);
			view->StrokeLine(
				BPoint(itemRect.left + 2,
					(itemRect.top + itemRect.bottom) / 2),
				BPoint(itemRect.right - 2,
					(itemRect.top + itemRect.bottom) / 2));
			view->StrokeLine(
				BPoint((itemRect.left + itemRect.right) / 2,
					itemRect.top +  2),
				BPoint((itemRect.left + itemRect.right) / 2,
					itemRect.bottom - 2));
			view->InvertRect(itemRect);
			break;

		case B_CLOSED_LATCH:
			view->StrokeRect(itemRect);
			view->StrokeLine(
				BPoint(itemRect.left + 2,
					(itemRect.top + itemRect.bottom) / 2),
				BPoint(itemRect.right - 2,
					(itemRect.top + itemRect.bottom) / 2));
			view->StrokeLine(
				BPoint((itemRect.left + itemRect.right) / 2,
					itemRect.top +  2),
				BPoint((itemRect.left + itemRect.right) / 2,
					itemRect.bottom - 2));
			break;

		case B_NO_LATCH:
		default:
			// No drawing
			break;
	}

	view->SetHighColor(highColor);
}


/**
 * @brief Handle focus changes by updating the focus border and scroll bar highlights.
 *
 * @param isFocus true if the view is gaining focus, false if losing it.
 */
void
BColumnListView::MakeFocus(bool isFocus)
{
	if (fBorderStyle != B_NO_BORDER) {
		// Redraw focus marks around view
		Invalidate();
		fHorizontalScrollBar->SetBorderHighlighted(isFocus);
		fVerticalScrollBar->SetBorderHighlighted(isFocus);
	}

	BView::MakeFocus(isFocus);
}


/**
 * @brief Handle incoming messages, including mouse wheel scrolling and system color updates.
 *
 * Mouse wheel messages are forwarded to the outline view to perform scrolling.
 * B_COLORS_UPDATED messages trigger a palette refresh when custom colors are
 * not in use.
 *
 * @param message The message to handle.
 */
void
BColumnListView::MessageReceived(BMessage* message)
{
	// Propagate mouse wheel messages down to child, so that it can
	// scroll.  Note we have done so, so we don't go into infinite
	// recursion if this comes back up here.
	if (message->what == B_MOUSE_WHEEL_CHANGED) {
		bool handled;
		if (message->FindBool("be:clvhandled", &handled) != B_OK) {
			message->AddBool("be:clvhandled", true);
			fOutlineView->MessageReceived(message);
			return;
		}
	} else if (message->what == B_COLORS_UPDATED) {
		// Todo: Is it worthwhile to optimize this?
		_UpdateColors();
	}

	BView::MessageReceived(message);
}


/**
 * @brief Handle keyboard navigation and interaction within the list view.
 *
 * Arrow keys move or extend the selection. B_PAGE_UP / B_PAGE_DOWN scroll
 * by a full page. B_ENTER invokes the selected item. B_SPACE toggles row
 * selection. '+' toggles the focused row's expand/collapse state.
 *
 * @param bytes    Pointer to the key byte(s).
 * @param numBytes Number of bytes in @p bytes.
 */
void
BColumnListView::KeyDown(const char* bytes, int32 numBytes)
{
	char key = bytes[0];
	switch (key) {
		case B_RIGHT_ARROW:
		case B_LEFT_ARROW:
		{
			if ((modifiers() & B_SHIFT_KEY) != 0) {
				float  minVal, maxVal;
				fHorizontalScrollBar->GetRange(&minVal, &maxVal);
				float smallStep, largeStep;
				fHorizontalScrollBar->GetSteps(&smallStep, &largeStep);
				float oldVal = fHorizontalScrollBar->Value();
				float newVal = oldVal;

				if (key == B_LEFT_ARROW)
					newVal -= smallStep;
				else if (key == B_RIGHT_ARROW)
					newVal += smallStep;

				if (newVal < minVal)
					newVal = minVal;
				else if (newVal > maxVal)
					newVal = maxVal;

				fHorizontalScrollBar->SetValue(newVal);
			} else {
				BRow* focusRow = fOutlineView->FocusRow();
				if (focusRow == NULL)
					break;

				bool isExpanded = focusRow->HasLatch()
					&& focusRow->IsExpanded();
				switch (key) {
					case B_LEFT_ARROW:
						if (isExpanded)
							fOutlineView->ToggleFocusRowOpen();
						else if (focusRow->fParent != NULL) {
							fOutlineView->DeselectAll();
							fOutlineView->SetFocusRow(focusRow->fParent, true);
							fOutlineView->ScrollTo(focusRow->fParent);
						}
						break;

					case B_RIGHT_ARROW:
						if (!isExpanded)
							fOutlineView->ToggleFocusRowOpen();
						else
							fOutlineView->ChangeFocusRow(false, true, false);
						break;
				}
			}
			break;
		}

		case B_DOWN_ARROW:
			fOutlineView->ChangeFocusRow(false,
				(modifiers() & B_CONTROL_KEY) == 0,
				(modifiers() & B_SHIFT_KEY) != 0);
			break;

		case B_UP_ARROW:
			fOutlineView->ChangeFocusRow(true,
				(modifiers() & B_CONTROL_KEY) == 0,
				(modifiers() & B_SHIFT_KEY) != 0);
			break;

		case B_PAGE_UP:
		case B_PAGE_DOWN:
		{
			float minValue, maxValue;
			fVerticalScrollBar->GetRange(&minValue, &maxValue);
			float smallStep, largeStep;
			fVerticalScrollBar->GetSteps(&smallStep, &largeStep);
			float currentValue = fVerticalScrollBar->Value();
			float newValue = currentValue;

			if (key == B_PAGE_UP)
				newValue -= largeStep;
			else
				newValue += largeStep;

			if (newValue > maxValue)
				newValue = maxValue;
			else if (newValue < minValue)
				newValue = minValue;

			fVerticalScrollBar->SetValue(newValue);

			// Option + pgup or pgdn scrolls and changes the selection.
			if (modifiers() & B_OPTION_KEY)
				fOutlineView->MoveFocusToVisibleRect();

			break;
		}

		case B_ENTER:
			Invoke();
			break;

		case B_SPACE:
			fOutlineView->ToggleFocusRowSelection(
				(modifiers() & B_SHIFT_KEY) != 0);
			break;

		case '+':
			fOutlineView->ToggleFocusRowOpen();
			break;

		default:
			BView::KeyDown(bytes, numBytes);
	}
}


/**
 * @brief Called when the view is attached to a window.
 *
 * Sets the default invocation target to the window if no target has been set,
 * and starts the background sort thread if sorting is enabled.
 */
void
BColumnListView::AttachedToWindow()
{
	if (!Messenger().IsValid())
		SetTarget(Window());

	if (SortingEnabled()) fOutlineView->StartSorting();
}


/**
 * @brief Called when the window containing this view is activated or deactivated.
 *
 * Forces a repaint of the outline view and the border frame so that focus and
 * selection indicators are updated to reflect the new activation state.
 *
 * @param active true if the window is now active, false if it lost focus.
 */
void
BColumnListView::WindowActivated(bool active)
{
	fOutlineView->Invalidate();
		// focus and selection appearance changes with focus

	Invalidate();
		// redraw focus marks around view
	BView::WindowActivated(active);
}


/**
 * @brief Draw the list view border and scroll-view frame.
 *
 * Renders the border around the child views using be_control_look, respecting
 * the configured border style (B_NO_BORDER, B_PLAIN_BORDER, or B_FANCY_BORDER)
 * and the current focus state.
 *
 * @param updateRect The rectangle that needs to be redrawn.
 */
void
BColumnListView::Draw(BRect updateRect)
{
	BRect rect = Bounds();

	uint32 flags = 0;
	if (IsFocus() && Window()->IsActive())
		flags |= BControlLook::B_FOCUSED;

	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);

	BRect verticalScrollBarFrame;
	if (!fVerticalScrollBar->IsHidden())
		verticalScrollBarFrame = fVerticalScrollBar->Frame();

	BRect horizontalScrollBarFrame;
	if (!fHorizontalScrollBar->IsHidden())
		horizontalScrollBarFrame = fHorizontalScrollBar->Frame();

	if (fBorderStyle == B_NO_BORDER) {
		// We still draw the left/top border, but not focused.
		// The scrollbars cannot be displayed without frame and
		// it looks bad to have no frame only along the left/top
		// side.
		rgb_color borderColor = tint_color(base, B_DARKEN_2_TINT);
		SetHighColor(borderColor);
		StrokeLine(BPoint(rect.left, rect.bottom),
			BPoint(rect.left, rect.top));
		StrokeLine(BPoint(rect.left + 1, rect.top),
			BPoint(rect.right, rect.top));
	}

	be_control_look->DrawScrollViewFrame(this, rect, updateRect,
		verticalScrollBarFrame, horizontalScrollBarFrame,
		base, fBorderStyle, flags);

	if (fStatusView != NULL) {
		rect = Bounds();
		BRegion region(rect & fStatusView->Frame().InsetByCopy(-2, -2));
		ConstrainClippingRegion(&region);
		rect.bottom = fStatusView->Frame().top - 1;
		be_control_look->DrawScrollViewFrame(this, rect, updateRect,
			BRect(), BRect(), base, fBorderStyle, flags);
	}
}


/**
 * @brief Serialize column widths, visibility, and sort order into @p message.
 *
 * The message is cleared before writing. Saved state can be restored with
 * LoadState().
 *
 * @param message The BMessage to write state into.
 */
void
BColumnListView::SaveState(BMessage* message)
{
	message->MakeEmpty();

	for (int32 i = 0; BColumn* column = (BColumn*)fColumns.ItemAt(i); i++) {
		message->AddInt32("ID", column->fFieldID);
		message->AddFloat("width", column->fWidth);
		message->AddBool("visible", column->fVisible);
	}

	message->AddBool("sortingenabled", fSortingEnabled);

	if (fSortingEnabled) {
		for (int32 i = 0; BColumn* column = (BColumn*)fSortColumns.ItemAt(i);
				i++) {
			message->AddInt32("sortID", column->fFieldID);
			message->AddBool("sortascending", column->fSortAscending);
		}
	}
}


/**
 * @brief Restore column widths, visibility, and sort order from @p message.
 *
 * Uses the logical field IDs stored by SaveState() to match columns, then
 * reorders, resizes, and shows/hides them accordingly.
 *
 * @param message The BMessage previously produced by SaveState().
 */
void
BColumnListView::LoadState(BMessage* message)
{
	int32 id;
	for (int i = 0; message->FindInt32("ID", i, &id) == B_OK; i++) {
		for (int j = 0; BColumn* column = (BColumn*)fColumns.ItemAt(j); j++) {
			if (column->fFieldID == id) {
				// move this column to position 'i' and set its attributes
				MoveColumn(column, i);
				float width;
				if (message->FindFloat("width", i, &width) == B_OK)
					column->SetWidth(width);
				bool visible;
				if (message->FindBool("visible", i, &visible) == B_OK)
					column->SetVisible(visible);
			}
		}
	}
	bool b;
	if (message->FindBool("sortingenabled", &b) == B_OK) {
		SetSortingEnabled(b);
		for (int k = 0; message->FindInt32("sortID", k, &id) == B_OK; k++) {
			for (int j = 0; BColumn* column = (BColumn*)fColumns.ItemAt(j);
					j++) {
				if (column->fFieldID == id) {
					// add this column to the sort list
					bool value;
					if (message->FindBool("sortascending", k, &value) == B_OK)
						SetSortColumn(column, true, value);
				}
			}
		}
	}
}


/**
 * @brief Enable or disable edit mode for the list view.
 *
 * In edit mode, mouse events are not used for row selection or latch
 * toggling, allowing in-cell editing widgets to function correctly.
 *
 * @param state true to enter edit mode, false to return to normal operation.
 */
void
BColumnListView::SetEditMode(bool state)
{
	fOutlineView->SetEditMode(state);
	fTitleView->SetEditMode(state);
}


/**
 * @brief Force an immediate synchronous repaint of the entire list view.
 *
 * Locks the looper, fixes the scroll bar range, invalidates both the outline
 * and parent views, and then flushes pending updates.
 */
void
BColumnListView::Refresh()
{
	if (LockLooper()) {
		Invalidate();
		fOutlineView->FixScrollBar (true);
		fOutlineView->Invalidate();
		Window()->UpdateIfNeeded();
		UnlockLooper();
	}
}


/**
 * @brief Return the minimum size of the list view for the layout manager.
 *
 * The minimum width is fixed at 100 px. The minimum height accounts for the
 * title bar, four scroll-bar heights, and (if visible) the horizontal scroll bar.
 *
 * @return The minimum BSize, composed with any explicitly set minimum.
 */
BSize
BColumnListView::MinSize()
{
	BSize size;
	size.width = 100;
	size.height = std::max(kMinTitleHeight,
		ceilf(be_plain_font->Size() * kTitleSpacing))
		+ 4 * B_H_SCROLL_BAR_HEIGHT;
	if (!fHorizontalScrollBar->IsHidden())
		size.height += fHorizontalScrollBar->Frame().Height() + 1;
	// TODO: Take border size into account

	return BLayoutUtils::ComposeSize(ExplicitMinSize(), size);
}


/**
 * @brief Return the preferred size of the list view for the layout manager.
 *
 * The preferred height adds 20 lines of text to the minimum. The preferred
 * width is the sum of all column widths plus border and margin overhead, or
 * the minimum width if there are no columns.
 *
 * @return The preferred BSize, composed with any explicitly set preferred size.
 */
BSize
BColumnListView::PreferredSize()
{
	BSize size = MinSize();
	size.height += ceilf(be_plain_font->Size()) * 20;

	// return MinSize().width if there are no columns.
	int32 count = CountColumns();
	if (count > 0) {
		BRect titleRect;
		BRect outlineRect;
		BRect vScrollBarRect;
		BRect hScrollBarRect;
		_GetChildViewRects(Bounds(), titleRect, outlineRect, vScrollBarRect,
			hScrollBarRect);
		// Start with the extra width for border and scrollbars etc.
		size.width = titleRect.left - Bounds().left;
		size.width += Bounds().right - titleRect.right;

		// If we want all columns to be visible at their current width,
		// we also need to add the extra margin width that the TitleView
		// uses to compute its _VirtualWidth() for the horizontal scroll bar.
		size.width += fTitleView->MarginWidth();
		for (int32 i = 0; i < count; i++) {
			BColumn* column = ColumnAt(i);
			if (column != NULL)
				size.width += column->Width();
		}
	}

	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), size);
}


/**
 * @brief Return the maximum size of the list view for the layout manager.
 *
 * @return B_SIZE_UNLIMITED in both dimensions, composed with any explicit maximum.
 */
BSize
BColumnListView::MaxSize()
{
	BSize size(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), size);
}


/**
 * @brief Called by the layout system when the layout is invalidated.
 *
 * Currently a no-op placeholder; layout work is deferred to DoLayout().
 *
 * @param descendants true if descendant layouts were also invalidated.
 */
void
BColumnListView::LayoutInvalidated(bool descendants)
{
}


/**
 * @brief Perform the layout pass, positioning and sizing all child views.
 *
 * Computes the rectangles for the title view, outline view, vertical scroll
 * bar, and horizontal scroll bar, then moves and resizes each child accordingly.
 * Also repositions and resizes the optional status view if one is attached.
 */
void
BColumnListView::DoLayout()
{
	if ((Flags() & B_SUPPORTS_LAYOUT) == 0)
		return;

	BRect titleRect;
	BRect outlineRect;
	BRect vScrollBarRect;
	BRect hScrollBarRect;
	_GetChildViewRects(Bounds(), titleRect, outlineRect, vScrollBarRect,
		hScrollBarRect);

	fTitleView->MoveTo(titleRect.LeftTop());
	fTitleView->ResizeTo(titleRect.Width(), titleRect.Height());

	fOutlineView->MoveTo(outlineRect.LeftTop());
	fOutlineView->ResizeTo(outlineRect.Width(), outlineRect.Height());

	fVerticalScrollBar->MoveTo(vScrollBarRect.LeftTop());
	fVerticalScrollBar->ResizeTo(vScrollBarRect.Width(),
		vScrollBarRect.Height());

	if (fStatusView != NULL) {
		BSize size = fStatusView->MinSize();
		float hScrollBarHeight = fHorizontalScrollBar->Frame().Height();

		if (size.height > hScrollBarHeight)
			size.height = hScrollBarHeight;
		if (size.width > Bounds().Width() / 2)
			size.width = floorf(Bounds().Width() / 2);

		BPoint offset(hScrollBarRect.LeftTop());

		if (fBorderStyle == B_PLAIN_BORDER) {
			offset += BPoint(0, 1);
		} else if (fBorderStyle == B_FANCY_BORDER) {
			offset += BPoint(-1, 2);
			size.height -= 1;
		}

		fStatusView->MoveTo(offset);
		fStatusView->ResizeTo(size.width, size.height);
		hScrollBarRect.left = offset.x + size.width + 1;
	}

	fHorizontalScrollBar->MoveTo(hScrollBarRect.LeftTop());
	fHorizontalScrollBar->ResizeTo(hScrollBarRect.Width(),
		hScrollBarRect.Height());

	fOutlineView->FixScrollBar(true);
}


/**
 * @brief Initialize child views, scroll bars, and color palette.
 *
 * Called from both constructors. Creates the OutlineView, TitleView, and
 * both BScrollBar children, then hides the horizontal scroll bar if it was
 * not requested.
 */
void
BColumnListView::_Init()
{
	SetViewColor(B_TRANSPARENT_32_BIT);

	BRect bounds(Bounds());
	if (bounds.Width() <= 0)
		bounds.right = 100;

	if (bounds.Height() <= 0)
		bounds.bottom = 100;

	fCustomColors = false;
	_UpdateColors();

	BRect titleRect;
	BRect outlineRect;
	BRect vScrollBarRect;
	BRect hScrollBarRect;
	_GetChildViewRects(bounds, titleRect, outlineRect, vScrollBarRect,
		hScrollBarRect);

	fOutlineView = new OutlineView(outlineRect, &fColumns, &fSortColumns, this);
	AddChild(fOutlineView);


	fTitleView = new TitleView(titleRect, fOutlineView, &fColumns,
		&fSortColumns, this, B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP);
	AddChild(fTitleView);

	fVerticalScrollBar = new BScrollBar(vScrollBarRect, "vertical_scroll_bar",
		fOutlineView, 0.0, bounds.Height(), B_VERTICAL);
	AddChild(fVerticalScrollBar);

	fHorizontalScrollBar = new BScrollBar(hScrollBarRect,
		"horizontal_scroll_bar", fTitleView, 0.0, bounds.Width(), B_HORIZONTAL);
	AddChild(fHorizontalScrollBar);

	if (!fShowingHorizontalScrollBar)
		fHorizontalScrollBar->Hide();

	fOutlineView->FixScrollBar(true);
}


/**
 * @brief Refresh the color palette from the current system UI colors.
 *
 * Has no effect when custom colors are active (fCustomColors is true).
 */
void
BColumnListView::_UpdateColors()
{
	if (fCustomColors)
		return;

	fColorList[B_COLOR_BACKGROUND] = ui_color(B_LIST_BACKGROUND_COLOR);
	fColorList[B_COLOR_TEXT] = ui_color(B_LIST_ITEM_TEXT_COLOR);
	fColorList[B_COLOR_ROW_DIVIDER] = tint_color(
		ui_color(B_LIST_SELECTED_BACKGROUND_COLOR), B_DARKEN_2_TINT);
	fColorList[B_COLOR_SELECTION] = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
	fColorList[B_COLOR_SELECTION_TEXT] =
		ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);

	// For non focus selection uses the selection color as BListView
	fColorList[B_COLOR_NON_FOCUS_SELECTION] =
		ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);

	// edit mode doesn't work very well
	fColorList[B_COLOR_EDIT_BACKGROUND] = tint_color(
		ui_color(B_LIST_SELECTED_BACKGROUND_COLOR), B_DARKEN_1_TINT);
	fColorList[B_COLOR_EDIT_BACKGROUND].alpha = 180;

	// Unused color
	fColorList[B_COLOR_EDIT_TEXT] = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);

	fColorList[B_COLOR_HEADER_BACKGROUND] = ui_color(B_CONTROL_BACKGROUND_COLOR);
	fColorList[B_COLOR_HEADER_TEXT] = ui_color(B_PANEL_TEXT_COLOR);

	// Unused colors
	fColorList[B_COLOR_SEPARATOR_LINE] = ui_color(B_LIST_ITEM_TEXT_COLOR);
	fColorList[B_COLOR_SEPARATOR_BORDER] = ui_color(B_LIST_ITEM_TEXT_COLOR);
}


/**
 * @brief Compute the frame rectangles for all child views within @p bounds.
 *
 * Splits @p bounds into non-overlapping regions for the title bar, outline
 * area, vertical scroll bar, and horizontal scroll bar, taking the border
 * style insets into account.
 *
 * @param bounds          The available area (typically Bounds()).
 * @param titleRect       Output: rectangle for the TitleView.
 * @param outlineRect     Output: rectangle for the OutlineView.
 * @param vScrollBarRect  Output: rectangle for the vertical BScrollBar.
 * @param hScrollBarRect  Output: rectangle for the horizontal BScrollBar.
 */
void
BColumnListView::_GetChildViewRects(const BRect& bounds, BRect& titleRect,
	BRect& outlineRect, BRect& vScrollBarRect, BRect& hScrollBarRect)
{
	const float vScrollBarWidth = be_control_look->GetScrollBarWidth(B_VERTICAL),
		hScrollBarHeight = be_control_look->GetScrollBarWidth(B_HORIZONTAL);

	titleRect = bounds;
	titleRect.bottom = titleRect.top + std::max(kMinTitleHeight,
		ceilf(be_plain_font->Size() * kTitleSpacing));
#if !LOWER_SCROLLBAR
	titleRect.right -= vScrollBarWidth;
#endif

	outlineRect = bounds;
	outlineRect.top = titleRect.bottom + 1.0;
	outlineRect.right -= vScrollBarWidth;
	if (fShowingHorizontalScrollBar)
		outlineRect.bottom -= hScrollBarHeight;

	vScrollBarRect = bounds;
#if LOWER_SCROLLBAR
	vScrollBarRect.top += std::max(kMinTitleHeight,
		ceilf(be_plain_font->Size() * kTitleSpacing));
#endif

	vScrollBarRect.left = vScrollBarRect.right - vScrollBarWidth;
	if (fShowingHorizontalScrollBar)
		vScrollBarRect.bottom -= hScrollBarHeight;

	hScrollBarRect = bounds;
	hScrollBarRect.top = hScrollBarRect.bottom - hScrollBarHeight;
	hScrollBarRect.right -= vScrollBarWidth;

	// Adjust stuff so the border will fit.
	if (fBorderStyle == B_PLAIN_BORDER || fBorderStyle == B_NO_BORDER) {
		titleRect.InsetBy(1, 0);
		titleRect.OffsetBy(0, 1);
		outlineRect.InsetBy(1, 1);
	} else if (fBorderStyle == B_FANCY_BORDER) {
		titleRect.InsetBy(2, 0);
		titleRect.OffsetBy(0, 2);
		outlineRect.InsetBy(2, 2);

		vScrollBarRect.OffsetBy(-1, 0);
#if LOWER_SCROLLBAR
		vScrollBarRect.top += 2;
		vScrollBarRect.bottom -= 1;
#else
		vScrollBarRect.InsetBy(0, 1);
#endif
		hScrollBarRect.OffsetBy(0, -1);
		hScrollBarRect.InsetBy(1, 0);
	}
}


// #pragma mark -


/**
 * @brief Construct the column header bar (TitleView).
 *
 * Creates the sort-arrow bitmaps and resize/move cursors, then fixes the
 * horizontal scroll bar range.
 *
 * @param rect           Initial frame rectangle.
 * @param horizontalSlave The OutlineView whose horizontal position is synchronized.
 * @param visibleColumns  Shared list of BColumn pointers (owned by BColumnListView).
 * @param sortColumns     Shared list of sort-order BColumn pointers.
 * @param listView        The master BColumnListView.
 * @param resizingMode    BView resizing flags.
 */
TitleView::TitleView(BRect rect, OutlineView* horizontalSlave,
	BList* visibleColumns, BList* sortColumns, BColumnListView* listView,
	uint32 resizingMode)
	:
	BView(rect, "title_view", resizingMode, B_WILL_DRAW | B_FRAME_EVENTS),
	fOutlineView(horizontalSlave),
	fColumns(visibleColumns),
	fSortColumns(sortColumns),
//	fColumnsWidth(0),
	fVisibleRect(rect.OffsetToCopy(0, 0)),
	fCurrentState(INACTIVE),
	fColumnPop(NULL),
	fMasterView(listView),
	fEditMode(false),
	fColumnFlags(B_ALLOW_COLUMN_MOVE | B_ALLOW_COLUMN_RESIZE
		| B_ALLOW_COLUMN_POPUP | B_ALLOW_COLUMN_REMOVE)
{
	SetViewColor(B_TRANSPARENT_COLOR);

	fUpSortArrow = new BBitmap(BRect(0, 0, 7, 7), B_CMAP8);
	fDownSortArrow = new BBitmap(BRect(0, 0, 7, 7), B_CMAP8);

	fUpSortArrow->SetBits((const void*) kUpSortArrow8x8, 64, 0, B_CMAP8);
	fDownSortArrow->SetBits((const void*) kDownSortArrow8x8, 64, 0, B_CMAP8);

	fResizeCursor = new BCursor(B_CURSOR_ID_RESIZE_EAST_WEST);
	fMinResizeCursor = new BCursor(B_CURSOR_ID_RESIZE_EAST);
	fMaxResizeCursor = new BCursor(B_CURSOR_ID_RESIZE_WEST);
	fColumnMoveCursor = new BCursor(B_CURSOR_ID_MOVE);

	FixScrollBar(true);
}


/**
 * @brief Destroy the TitleView and release all owned resources.
 *
 * Deletes the column-visibility popup menu, sort-arrow bitmaps, and cursor objects.
 */
TitleView::~TitleView()
{
	delete fColumnPop;
	fColumnPop = NULL;

	delete fUpSortArrow;
	delete fDownSortArrow;

	delete fResizeCursor;
	delete fMaxResizeCursor;
	delete fMinResizeCursor;
	delete fColumnMoveCursor;
}


/**
 * @brief Notify the title view that a column has been added to the list.
 *
 * Updates the double-buffer view's maximum width if needed, then fixes the
 * horizontal scroll bar range and invalidates the header area.
 *
 * @param column The newly added column.
 */
void
TitleView::ColumnAdded(BColumn* column)
{
#ifdef DOUBLE_BUFFERED_COLUMN_RESIZE
	fOutlineView->ResizeBufferView()->UpdateMaxWidth(column->MaxWidth());
#endif
//	fColumnsWidth += column->Width();
	FixScrollBar(false);
	Invalidate();
}


/**
 * @brief Notify the title view that a column has been resized.
 *
 * Fixes the horizontal scroll bar range and invalidates the header area.
 *
 * @param column   The column that was resized.
 * @param oldWidth The column's previous width in pixels.
 */
void
TitleView::ColumnResized(BColumn* column, float oldWidth)
{
//	fColumnsWidth += column->Width() - oldWidth;
	FixScrollBar(false);
	Invalidate();
}


/**
 * @brief Show or hide a column in both the title and outline areas.
 *
 * Computes the invalid region before and after changing the visibility so
 * that only the affected area is redrawn. Updates the scroll bar range.
 *
 * @param column  The column to show or hide.
 * @param visible true to show, false to hide.
 */
void
TitleView::SetColumnVisible(BColumn* column, bool visible)
{
	if (column->fVisible == visible)
		return;

	// If setting it visible, do this first so we can find its position
	// to invalidate.  If hiding it, do it last.
	if (visible)
		column->fVisible = visible;

	BRect titleInvalid;
	GetTitleRect(column, &titleInvalid);

	// Now really set the visibility
	column->fVisible = visible;

//	if (visible)
//		fColumnsWidth += column->Width();
//	else
//		fColumnsWidth -= column->Width();

	BRect outlineInvalid(fOutlineView->VisibleRect());
	outlineInvalid.left = titleInvalid.left;
	titleInvalid.right = outlineInvalid.right;

	Invalidate(titleInvalid);
	fOutlineView->Invalidate(outlineInvalid);

	FixScrollBar(false);
}


/**
 * @brief Compute the bounding rectangle of a column header cell.
 *
 * Iterates over visible columns to find @p findColumn and fills @p _rect
 * with its header-cell rectangle. Calls TRESPASS() if the column is not found.
 *
 * @param findColumn The column to find.
 * @param _rect      Output: the header-cell bounding rectangle.
 */
void
TitleView::GetTitleRect(BColumn* findColumn, BRect* _rect)
{
	float leftEdge = MAX(kLeftMargin, fMasterView->LatchWidth());
	int32 numColumns = fColumns->CountItems();
	for (int index = 0; index < numColumns; index++) {
		BColumn* column = (BColumn*) fColumns->ItemAt(index);
		if (!column->IsVisible())
			continue;

		if (column == findColumn) {
			_rect->Set(leftEdge, 0, leftEdge + column->Width(),
				fVisibleRect.bottom);
			return;
		}

		leftEdge += column->Width() + 1;
	}

	TRESPASS();
}


/**
 * @brief Find the visible column whose header cell contains the given position.
 *
 * @param position  A point in the title view's coordinate system.
 * @param _leftEdge Output: the left edge of the found column's header cell.
 * @return The display index of the found column, or 0 if none was found.
 */
int32
TitleView::FindColumn(BPoint position, float* _leftEdge)
{
	float leftEdge = MAX(kLeftMargin, fMasterView->LatchWidth());
	int32 numColumns = fColumns->CountItems();
	for (int index = 0; index < numColumns; index++) {
		BColumn* column = (BColumn*) fColumns->ItemAt(index);
		if (!column->IsVisible())
			continue;

		if (leftEdge > position.x)
			break;

		if (position.x >= leftEdge
			&& position.x <= leftEdge + column->Width()) {
			*_leftEdge = leftEdge;
			return index;
		}

		leftEdge += column->Width() + 1;
	}

	return 0;
}


/**
 * @brief Update the horizontal scroll bar range to match the total virtual width.
 *
 * @param scrollToFit If true, forces the range to be updated even when the user
 *                    is scrolled beyond the new maximum; otherwise, range updates
 *                    are deferred until the user scrolls back.
 */
void
TitleView::FixScrollBar(bool scrollToFit)
{
	BScrollBar* hScrollBar = ScrollBar(B_HORIZONTAL);
	if (hScrollBar == NULL)
		return;

	float virtualWidth = _VirtualWidth();

	if (virtualWidth > fVisibleRect.Width()) {
		hScrollBar->SetProportion(fVisibleRect.Width() / virtualWidth);

		// Perform the little trick if the user is scrolled over too far.
		// See OutlineView::FixScrollBar for a more in depth explanation
		float maxScrollBarValue = virtualWidth - fVisibleRect.Width();
		if (scrollToFit || hScrollBar->Value() <= maxScrollBarValue) {
			hScrollBar->SetRange(0.0, maxScrollBarValue);
			hScrollBar->SetSteps(50, fVisibleRect.Width());
		}
	} else if (hScrollBar->Value() == 0.0) {
		// disable scroll bar.
		hScrollBar->SetRange(0.0, 0.0);
	}
}


/**
 * @brief Update the position of the column being dragged.
 *
 * Finds the column under @p position, moves the selected column to that slot,
 * recalculates drag boundaries, and redraws the affected region.
 *
 * @param position Current drag position in title-view coordinates.
 */
void
TitleView::DragSelectedColumn(BPoint position)
{
	float invalidLeft = fSelectedColumnRect.left;
	float invalidRight = fSelectedColumnRect.right;

	float leftEdge;
	int32 columnIndex = FindColumn(position, &leftEdge);
	fSelectedColumnRect.OffsetTo(leftEdge, 0);

	MoveColumn(fSelectedColumn, columnIndex);

	fSelectedColumn->fVisible = true;
	ComputeDragBoundries(fSelectedColumn, position);

	// Redraw the new column position
	GetTitleRect(fSelectedColumn, &fSelectedColumnRect);
	invalidLeft = MIN(fSelectedColumnRect.left, invalidLeft);
	invalidRight = MAX(fSelectedColumnRect.right, invalidRight);

	Invalidate(BRect(invalidLeft, 0, invalidRight, fVisibleRect.bottom));
	fOutlineView->Invalidate(BRect(invalidLeft, 0, invalidRight,
		fOutlineView->VisibleRect().bottom));

	DrawTitle(this, fSelectedColumnRect, fSelectedColumn, true);
}


/**
 * @brief Move @p column to the given display position in the column list.
 *
 * @param column The column to reposition.
 * @param index  Target display index, or -1 to append to the end.
 */
void
TitleView::MoveColumn(BColumn* column, int32 index)
{
	fColumns->RemoveItem((void*) column);

	if (-1 == index) {
		// Re-add the column at the end of the list.
		fColumns->AddItem((void*) column);
	} else {
		fColumns->AddItem((void*) column, index);
	}
}


/**
 * @brief Set the column interaction flags for the title view.
 *
 * @param flags A bitmask of B_ALLOW_COLUMN_MOVE, B_ALLOW_COLUMN_RESIZE,
 *              B_ALLOW_COLUMN_POPUP, and B_ALLOW_COLUMN_REMOVE.
 */
void
TitleView::SetColumnFlags(column_flags flags)
{
	fColumnFlags = flags;
}


/**
 * @brief Return the total non-column margin width used by the title view.
 *
 * This includes the left margin (at least as wide as the latch) plus the
 * right margin, and is used when computing the horizontal scroll bar range.
 *
 * @return The margin width in pixels.
 */
float
TitleView::MarginWidth() const
{
	return MAX(kLeftMargin, fMasterView->LatchWidth()) + kRightMargin;
}


/**
 * @brief Resize the currently selected column to match @p position or its preferred width.
 *
 * Clamps the width to [MinWidth(), MaxWidth()]. Uses double-buffered rendering
 * (when enabled) to move the column header and outline areas efficiently with CopyBits.
 * Updates the resize cursor to reflect the new width state.
 *
 * @param position  The current mouse position in title-view coordinates; the column
 *                  right edge tracks this x coordinate.
 * @param preferred If true, the column is resized to its preferred content width
 *                  instead of following @p position.
 */
void
TitleView::ResizeSelectedColumn(BPoint position, bool preferred)
{
	float minWidth = fSelectedColumn->MinWidth();
	float maxWidth = fSelectedColumn->MaxWidth();

	float oldWidth = fSelectedColumn->Width();
	float originalEdge = fSelectedColumnRect.left + oldWidth;
	if (preferred) {
		float width = fOutlineView->GetColumnPreferredWidth(fSelectedColumn);
		fSelectedColumn->SetWidth(width);
	} else if (position.x > fSelectedColumnRect.left + maxWidth)
		fSelectedColumn->SetWidth(maxWidth);
	else if (position.x < fSelectedColumnRect.left + minWidth)
		fSelectedColumn->SetWidth(minWidth);
	else
		fSelectedColumn->SetWidth(position.x - fSelectedColumnRect.left - 1);

	float dX = fSelectedColumnRect.left + fSelectedColumn->Width()
		 - originalEdge;
	if (dX != 0) {
		float columnHeight = fVisibleRect.Height();
		BRect originalRect(originalEdge, 0, 1000000.0, columnHeight);
		BRect movedRect(originalRect);
		movedRect.OffsetBy(dX, 0);

		// Update the size of the title column
		BRect sourceRect(0, 0, fSelectedColumn->Width(), columnHeight);
		BRect destRect(sourceRect);
		destRect.OffsetBy(fSelectedColumnRect.left, 0);

#if DOUBLE_BUFFERED_COLUMN_RESIZE
		ColumnResizeBufferView* bufferView = fOutlineView->ResizeBufferView();
		bufferView->Lock();
		DrawTitle(bufferView, sourceRect, fSelectedColumn, false);
		bufferView->Sync();
		bufferView->Unlock();

		CopyBits(originalRect, movedRect);
		DrawBitmap(bufferView->Bitmap(), sourceRect, destRect);
#else
		CopyBits(originalRect, movedRect);
		DrawTitle(this, destRect, fSelectedColumn, false);
#endif

		// Update the body view
		BRect slaveSize = fOutlineView->VisibleRect();
		BRect slaveSource(originalRect);
		slaveSource.bottom = slaveSize.bottom;
		BRect slaveDest(movedRect);
		slaveDest.bottom = slaveSize.bottom;
		fOutlineView->CopyBits(slaveSource, slaveDest);
		fOutlineView->RedrawColumn(fSelectedColumn, fSelectedColumnRect.left,
			fResizingFirstColumn);

//		fColumnsWidth += dX;

		// Update the cursor
		if (fSelectedColumn->Width() == minWidth)
			SetViewCursor(fMinResizeCursor, true);
		else if (fSelectedColumn->Width() == maxWidth)
			SetViewCursor(fMaxResizeCursor, true);
		else
			SetViewCursor(fResizeCursor, true);

		ColumnResized(fSelectedColumn, oldWidth);
	}
}


/**
 * @brief Compute the left and right drag boundaries for a column being moved.
 *
 * Sets fLeftDragBoundry and fRightDragBoundry so that the column cannot be
 * dragged past its nearest neighbors.
 *
 * @param findColumn The column being dragged.
 */
void
TitleView::ComputeDragBoundries(BColumn* findColumn, BPoint)
{
	float previousColumnLeftEdge = -1000000.0;
	float nextColumnRightEdge = 1000000.0;

	bool foundColumn = false;
	float leftEdge = MAX(kLeftMargin, fMasterView->LatchWidth());
	int32 numColumns = fColumns->CountItems();
	for (int index = 0; index < numColumns; index++) {
		BColumn* column = (BColumn*) fColumns->ItemAt(index);
		if (!column->IsVisible())
			continue;

		if (column == findColumn) {
			foundColumn = true;
			continue;
		}

		if (foundColumn) {
			nextColumnRightEdge = leftEdge + column->Width();
			break;
		} else
			previousColumnLeftEdge = leftEdge;

		leftEdge += column->Width() + 1;
	}

	float rightEdge = leftEdge + findColumn->Width();

	fLeftDragBoundry = MIN(previousColumnLeftEdge + findColumn->Width(),
		leftEdge);
	fRightDragBoundry = MAX(nextColumnRightEdge, rightEdge);
}


/**
 * @brief Draw a single column header cell.
 *
 * Renders the button background, sort indicator arrow and index (if applicable),
 * and delegates actual title text rendering to BColumn::DrawTitle(). Passing
 * NULL for @p column draws an empty header background (used for margin areas).
 *
 * @param view      The view to draw into.
 * @param rect      The bounding rectangle of the header cell.
 * @param column    The column whose title should be drawn, or NULL for margins.
 * @param depressed If true, the cell is drawn in a pressed/active state.
 */
void
TitleView::DrawTitle(BView* view, BRect rect, BColumn* column, bool depressed)
{
	BRect drawRect;
	drawRect = rect;

	font_height fh;
	GetFontHeight(&fh);

	float baseline = floor(drawRect.top + fh.ascent
		+ (drawRect.Height() + 1 - (fh.ascent + fh.descent)) / 2);

	BRect bgRect = rect;

	rgb_color base = fMasterView->Color(B_COLOR_HEADER_BACKGROUND);
	view->SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_2_TINT));
	view->StrokeLine(bgRect.LeftBottom(), bgRect.RightBottom());

	bgRect.bottom--;
	bgRect.right--;

	if (depressed)
		base = tint_color(base, B_DARKEN_1_TINT);

	be_control_look->DrawButtonBackground(view, bgRect, rect, base, 0,
		BControlLook::B_TOP_BORDER | BControlLook::B_BOTTOM_BORDER);

	view->SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_2_TINT));
	view->StrokeLine(rect.RightTop(), rect.RightBottom());

	// If no column given, nothing else to draw.
	if (column == NULL)
		return;

	view->SetHighColor(fMasterView->Color(B_COLOR_HEADER_TEXT));

	BFont font;
	GetFont(&font);
	view->SetFont(&font);

	int sortIndex = fSortColumns->IndexOf(column);
	if (sortIndex >= 0) {
		// Draw sort notation.
		BPoint upperLeft(drawRect.right - kSortIndicatorWidth, baseline);

		if (fSortColumns->CountItems() > 1) {
			char str[256];
			sprintf(str, "%d", sortIndex + 1);
			const float w = view->StringWidth(str);
			upperLeft.x -= w;

			view->SetDrawingMode(B_OP_COPY);
			view->MovePenTo(BPoint(upperLeft.x + kSortIndicatorWidth,
				baseline));
			view->DrawString(str);
		}

		float bmh = fDownSortArrow->Bounds().Height()+1;

		view->SetDrawingMode(B_OP_OVER);

		if (column->fSortAscending) {
			BPoint leftTop(upperLeft.x, drawRect.top + (drawRect.IntegerHeight()
				- fDownSortArrow->Bounds().IntegerHeight()) / 2);
			view->DrawBitmapAsync(fDownSortArrow, leftTop);
		} else {
			BPoint leftTop(upperLeft.x, drawRect.top + (drawRect.IntegerHeight()
				- fUpSortArrow->Bounds().IntegerHeight()) / 2);
			view->DrawBitmapAsync(fUpSortArrow, leftTop);
		}

		upperLeft.y = baseline - bmh + floor((fh.ascent + fh.descent - bmh) / 2);
		if (upperLeft.y < drawRect.top)
			upperLeft.y = drawRect.top;

		// Adjust title stuff for sort indicator
		drawRect.right = upperLeft.x - 2;
	}

	if (drawRect.right > drawRect.left) {
#if CONSTRAIN_CLIPPING_REGION
		BRegion clipRegion(drawRect);
		view->PushState();
		view->ConstrainClippingRegion(&clipRegion);
#endif
		view->MovePenTo(BPoint(drawRect.left + 8, baseline));
		view->SetDrawingMode(B_OP_OVER);
		view->SetHighColor(fMasterView->Color(B_COLOR_HEADER_TEXT));
		column->DrawTitle(drawRect, view);

#if CONSTRAIN_CLIPPING_REGION
		view->PopState();
#endif
	}
}


/**
 * @brief Compute the total virtual width of all visible columns plus margins.
 *
 * Used to set the horizontal scroll bar range.
 *
 * @return The total virtual width in pixels.
 */
float
TitleView::_VirtualWidth() const
{
	float width = MarginWidth();

	int32 count = fColumns->CountItems();
	for (int32 i = 0; i < count; i++) {
		BColumn* column = reinterpret_cast<BColumn*>(fColumns->ItemAt(i));
		if (column->IsVisible())
			width += column->Width();
	}

	return width;
}


/**
 * @brief Draw the entire title bar, including all column headers and margin areas.
 *
 * Iterates over visible columns, draws each header cell, fills margin areas,
 * and (when DRAG_TITLE_OUTLINE is enabled) draws a blue outline for the column
 * being dragged.
 *
 * @param invalidRect The area of the title view that needs to be redrawn.
 */
void
TitleView::Draw(BRect invalidRect)
{
	float columnLeftEdge = MAX(kLeftMargin, fMasterView->LatchWidth());
	for (int32 columnIndex = 0; columnIndex < fColumns->CountItems();
		columnIndex++) {

		BColumn* column = (BColumn*) fColumns->ItemAt(columnIndex);
		if (!column->IsVisible())
			continue;

		if (columnLeftEdge > invalidRect.right)
			break;

		if (columnLeftEdge + column->Width() >= invalidRect.left) {
			BRect titleRect(columnLeftEdge, 0,
				columnLeftEdge + column->Width(), fVisibleRect.Height());
			DrawTitle(this, titleRect, column,
				(fCurrentState == DRAG_COLUMN_INSIDE_TITLE
				&& fSelectedColumn == column));
		}

		columnLeftEdge += column->Width() + 1;
	}


	// bevels for right title margin
	if (columnLeftEdge <= invalidRect.right) {
		BRect titleRect(columnLeftEdge, 0, Bounds().right + 2,
			fVisibleRect.Height());
		DrawTitle(this, titleRect, NULL, false);
	}

	// bevels for left title margin
	if (invalidRect.left < MAX(kLeftMargin, fMasterView->LatchWidth())) {
		BRect titleRect(0, 0, MAX(kLeftMargin, fMasterView->LatchWidth()) - 1,
			fVisibleRect.Height());
		DrawTitle(this, titleRect, NULL, false);
	}

#if DRAG_TITLE_OUTLINE
	// (internal) column drag indicator
	if (fCurrentState == DRAG_COLUMN_INSIDE_TITLE) {
		BRect dragRect(fSelectedColumnRect);
		dragRect.OffsetTo(fCurrentDragPosition.x - fClickPoint.x, 0);
		if (dragRect.Intersects(invalidRect)) {
			SetHighColor(0, 0, 255);
			StrokeRect(dragRect);
		}
	}
#endif
}


/**
 * @brief Scroll the title view and synchronize the slave outline view.
 *
 * Also checks whether the horizontal scroll bar range needs to be corrected
 * after a user-initiated scroll that moved past the new maximum.
 *
 * @param position The new scroll position.
 */
void
TitleView::ScrollTo(BPoint position)
{
	fOutlineView->ScrollBy(position.x - fVisibleRect.left, 0);
	fVisibleRect.OffsetTo(position.x, position.y);

	// Perform the little trick if the user is scrolled over too far.
	// See OutlineView::ScrollTo for a more in depth explanation
	float maxScrollBarValue = _VirtualWidth() - fVisibleRect.Width();
	BScrollBar* hScrollBar = ScrollBar(B_HORIZONTAL);
	float min, max;
	hScrollBar->GetRange(&min, &max);
	if (max != maxScrollBarValue && position.x > maxScrollBarValue)
		FixScrollBar(true);

	_inherited::ScrollTo(position);
}


/**
 * @brief Handle the kToggleColumn message from the column-visibility popup menu.
 *
 * Toggles the visibility of the column identified by the "be:field_num" field
 * in @p message. Other messages are forwarded to the base class.
 *
 * @param message The received BMessage.
 */
void
TitleView::MessageReceived(BMessage* message)
{
	if (message->what == kToggleColumn) {
		int32 num;
		if (message->FindInt32("be:field_num", &num) == B_OK) {
			for (int index = 0; index < fColumns->CountItems(); index++) {
				BColumn* column = (BColumn*) fColumns->ItemAt(index);
				if (column == NULL)
					continue;

				if (column->LogicalFieldNum() == num)
					column->SetVisible(!column->IsVisible());
			}
		}
		return;
	}

	BView::MessageReceived(message);
}


/**
 * @brief Handle a mouse-button press in the title view.
 *
 * A right-click shows the column-visibility popup (if allowed). A left-click
 * near a column edge starts resizing; a double-click or middle-click resizes
 * to the preferred width. A left-click within a column starts a potential drag
 * or sort-column toggle.
 *
 * @param position The mouse position in title-view coordinates.
 */
void
TitleView::MouseDown(BPoint position)
{
	if (fEditMode)
		return;

	int32 buttons = 1;
	Window()->CurrentMessage()->FindInt32("buttons", &buttons);
	if (buttons == B_SECONDARY_MOUSE_BUTTON
		&& (fColumnFlags & B_ALLOW_COLUMN_POPUP)) {
		// Right mouse button -- bring up menu to show/hide columns.
		if (fColumnPop == NULL)
			fColumnPop = new BPopUpMenu("Columns", false, false);

		fColumnPop->RemoveItems(0, fColumnPop->CountItems(), true);
		BMessenger me(this);
		for (int index = 0; index < fColumns->CountItems(); index++) {
			BColumn* column = (BColumn*) fColumns->ItemAt(index);
			if (column == NULL)
				continue;

			BString name;
			column->GetColumnName(&name);
			BMessage* message = new BMessage(kToggleColumn);
			message->AddInt32("be:field_num", column->LogicalFieldNum());
			BMenuItem* item = new BMenuItem(name.String(), message);
			item->SetMarked(column->IsVisible());
			item->SetTarget(me);
			fColumnPop->AddItem(item);
		}

		BPoint screenPosition = ConvertToScreen(position);
		BRect sticky(screenPosition, screenPosition);
		sticky.InsetBy(-5, -5);
		fColumnPop->Go(ConvertToScreen(position), true, false, sticky, true);

		return;
	}

	fResizingFirstColumn = true;
	float leftEdge = MAX(kLeftMargin, fMasterView->LatchWidth());
	for (int index = 0; index < fColumns->CountItems(); index++) {
		BColumn* column = (BColumn*)fColumns->ItemAt(index);
		if (column == NULL || !column->IsVisible())
			continue;

		if (leftEdge > position.x + kColumnResizeAreaWidth / 2)
			break;

		// check for resizing a column
		float rightEdge = leftEdge + column->Width();

		if (column->ShowHeading()) {
			if (position.x > rightEdge - kColumnResizeAreaWidth / 2
				&& position.x < rightEdge + kColumnResizeAreaWidth / 2
				&& column->MaxWidth() > column->MinWidth()
				&& (fColumnFlags & B_ALLOW_COLUMN_RESIZE) != 0) {

				int32 clicks = 0;
				fSelectedColumn = column;
				fSelectedColumnRect.Set(leftEdge, 0, rightEdge,
					fVisibleRect.Height());
				Window()->CurrentMessage()->FindInt32("clicks", &clicks);
				if (clicks == 2 || buttons == B_TERTIARY_MOUSE_BUTTON) {
					ResizeSelectedColumn(position, true);
					fCurrentState = INACTIVE;
					break;
				}
				fCurrentState = RESIZING_COLUMN;
				fClickPoint = BPoint(position.x - rightEdge - 1,
					position.y - fSelectedColumnRect.top);
				SetMouseEventMask(B_POINTER_EVENTS,
					B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY);
				break;
			}

			fResizingFirstColumn = false;

			// check for clicking on a column
			if (position.x > leftEdge && position.x < rightEdge) {
				fCurrentState = PRESSING_COLUMN;
				fSelectedColumn = column;
				fSelectedColumnRect.Set(leftEdge, 0, rightEdge,
					fVisibleRect.Height());
				DrawTitle(this, fSelectedColumnRect, fSelectedColumn, true);
				fClickPoint = BPoint(position.x - fSelectedColumnRect.left,
					position.y - fSelectedColumnRect.top);
				SetMouseEventMask(B_POINTER_EVENTS,
					B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY);
				break;
			}
		}
		leftEdge = rightEdge + 1;
	}
}


/**
 * @brief Handle mouse movement in the title view.
 *
 * Dispatches to the appropriate handler based on fCurrentState: resizes the
 * selected column, updates the drag position, or adjusts the resize cursor
 * when the mouse is near a column edge while inactive.
 *
 * @param position    Current mouse position in title-view coordinates.
 * @param transit     Transit code (B_ENTERED_VIEW, B_INSIDE_VIEW, B_EXITED_VIEW).
 * @param dragMessage The drag message if a system drag-and-drop is in progress, or NULL.
 */
void
TitleView::MouseMoved(BPoint position, uint32 transit,
	const BMessage* dragMessage)
{
	if (fEditMode)
		return;

	// Handle column manipulation
	switch (fCurrentState) {
		case RESIZING_COLUMN:
			ResizeSelectedColumn(position - BPoint(fClickPoint.x, 0));
			break;

		case PRESSING_COLUMN: {
			if (abs((int32)(position.x - (fClickPoint.x
					+ fSelectedColumnRect.left))) > kColumnResizeAreaWidth
				|| abs((int32)(position.y - (fClickPoint.y
					+ fSelectedColumnRect.top))) > kColumnResizeAreaWidth) {
				// User has moved the mouse more than the tolerable amount,
				// initiate a drag.
				if (transit == B_INSIDE_VIEW || transit == B_ENTERED_VIEW) {
					if(fColumnFlags & B_ALLOW_COLUMN_MOVE) {
						fCurrentState = DRAG_COLUMN_INSIDE_TITLE;
						ComputeDragBoundries(fSelectedColumn, position);
						SetViewCursor(fColumnMoveCursor, true);
#if DRAG_TITLE_OUTLINE
						BRect invalidRect(fSelectedColumnRect);
						invalidRect.OffsetTo(position.x - fClickPoint.x, 0);
						fCurrentDragPosition = position;
						Invalidate(invalidRect);
#endif
					}
				} else {
					if(fColumnFlags & B_ALLOW_COLUMN_REMOVE) {
						// Dragged outside view
						fCurrentState = DRAG_COLUMN_OUTSIDE_TITLE;
						fSelectedColumn->SetVisible(false);
						BRect dragRect(fSelectedColumnRect);

						// There is a race condition where the mouse may have
						// moved by the time we get to handle this message.
						// If the user drags a column very quickly, this
						// results in the annoying bug where the cursor is
						// outside of the rectangle that is being dragged
						// around.  Call GetMouse with the checkQueue flag set
						// to false so we can get the most recent position of
						// the mouse.  This minimizes this problem (although
						// it is currently not possible to completely eliminate
						// it).
						uint32 buttons;
						GetMouse(&position, &buttons, false);
						dragRect.OffsetTo(position.x - fClickPoint.x,
							position.y - dragRect.Height() / 2);
						BeginRectTracking(dragRect, B_TRACK_WHOLE_RECT);
					}
				}
			}

			break;
		}

		case DRAG_COLUMN_INSIDE_TITLE: {
			if (transit == B_EXITED_VIEW
				&& (fColumnFlags & B_ALLOW_COLUMN_REMOVE)) {
				// Dragged outside view
				fCurrentState = DRAG_COLUMN_OUTSIDE_TITLE;
				fSelectedColumn->SetVisible(false);
				BRect dragRect(fSelectedColumnRect);

				// See explanation above.
				uint32 buttons;
				GetMouse(&position, &buttons, false);

				dragRect.OffsetTo(position.x - fClickPoint.x,
					position.y - fClickPoint.y);
				BeginRectTracking(dragRect, B_TRACK_WHOLE_RECT);
			} else if (position.x < fLeftDragBoundry
				|| position.x > fRightDragBoundry) {
				DragSelectedColumn(position - BPoint(fClickPoint.x, 0));
			}

#if DRAG_TITLE_OUTLINE
			// Set up the invalid rect to include the rect for the previous
			// position of the drag rect, as well as the new one.
			BRect invalidRect(fSelectedColumnRect);
			invalidRect.OffsetTo(fCurrentDragPosition.x - fClickPoint.x, 0);
			if (position.x < fCurrentDragPosition.x)
				invalidRect.left -= fCurrentDragPosition.x - position.x;
			else
				invalidRect.right += position.x - fCurrentDragPosition.x;

			fCurrentDragPosition = position;
			Invalidate(invalidRect);
#endif
			break;
		}

		case DRAG_COLUMN_OUTSIDE_TITLE:
			if (transit == B_ENTERED_VIEW) {
				// Drag back into view
				EndRectTracking();
				fCurrentState = DRAG_COLUMN_INSIDE_TITLE;
				fSelectedColumn->SetVisible(true);
				DragSelectedColumn(position - BPoint(fClickPoint.x, 0));
			}

			break;

		case INACTIVE:
			// Check for cursor changes if we are over the resize area for
			// a column.
			BColumn* resizeColumn = 0;
			float leftEdge = MAX(kLeftMargin, fMasterView->LatchWidth());
			for (int index = 0; index < fColumns->CountItems(); index++) {
				BColumn* column = (BColumn*) fColumns->ItemAt(index);
				if (!column->IsVisible())
					continue;

				if (leftEdge > position.x + kColumnResizeAreaWidth / 2)
					break;

				float rightEdge = leftEdge + column->Width();
				if (position.x > rightEdge - kColumnResizeAreaWidth / 2
					&& position.x < rightEdge + kColumnResizeAreaWidth / 2
					&& column->MaxWidth() > column->MinWidth()) {
					resizeColumn = column;
					break;
				}

				leftEdge = rightEdge + 1;
			}

			// Update the cursor
			if (resizeColumn) {
				if (resizeColumn->Width() == resizeColumn->MinWidth())
					SetViewCursor(fMinResizeCursor, true);
				else if (resizeColumn->Width() == resizeColumn->MaxWidth())
					SetViewCursor(fMaxResizeCursor, true);
				else
					SetViewCursor(fResizeCursor, true);
			} else
				SetViewCursor(B_CURSOR_SYSTEM_DEFAULT, true);
			break;
	}
}


/**
 * @brief Handle a mouse-button release in the title view.
 *
 * Finalizes column resize or drag, or (when a column was simply clicked)
 * updates the sort order and triggers a re-sort.
 *
 * @param position The mouse position in title-view coordinates at release time.
 */
void
TitleView::MouseUp(BPoint position)
{
	if (fEditMode)
		return;

	switch (fCurrentState) {
		case RESIZING_COLUMN:
			ResizeSelectedColumn(position - BPoint(fClickPoint.x, 0));
			fCurrentState = INACTIVE;
			FixScrollBar(false);
			break;

		case PRESSING_COLUMN: {
			if (fMasterView->SortingEnabled()) {
				if (fSortColumns->HasItem(fSelectedColumn)) {
					if ((modifiers() & B_CONTROL_KEY) == 0
						&& fSortColumns->CountItems() > 1) {
						fSortColumns->MakeEmpty();
						fSortColumns->AddItem(fSelectedColumn);
					}

					fSelectedColumn->fSortAscending
						= !fSelectedColumn->fSortAscending;
				} else {
					if ((modifiers() & B_CONTROL_KEY) == 0)
						fSortColumns->MakeEmpty();

					fSortColumns->AddItem(fSelectedColumn);
					fSelectedColumn->fSortAscending = true;
				}

				fOutlineView->StartSorting();
			}

			fCurrentState = INACTIVE;
			Invalidate();
			break;
		}

		case DRAG_COLUMN_INSIDE_TITLE:
			fCurrentState = INACTIVE;

#if DRAG_TITLE_OUTLINE
			Invalidate();	// xxx Can make this smaller
#else
			Invalidate(fSelectedColumnRect);
#endif
			SetViewCursor(B_CURSOR_SYSTEM_DEFAULT, true);
			break;

		case DRAG_COLUMN_OUTSIDE_TITLE:
			fCurrentState = INACTIVE;
			EndRectTracking();
			SetViewCursor(B_CURSOR_SYSTEM_DEFAULT, true);
			break;

		default:
			;
	}
}


/**
 * @brief Called when the title view is resized.
 *
 * Updates fVisibleRect and recalculates the horizontal scroll bar range.
 *
 * @param width  New view width in pixels.
 * @param height New view height in pixels.
 */
void
TitleView::FrameResized(float width, float height)
{
	fVisibleRect.right = fVisibleRect.left + width;
	fVisibleRect.bottom = fVisibleRect.top + height;
	FixScrollBar(true);
}


// #pragma mark - OutlineView


/**
 * @brief Construct the outline (row) view.
 *
 * Initializes all interaction state, creates the double-buffer resize view,
 * fixes the vertical scroll bar, and sets up the selection list sentinel.
 *
 * @param rect          Initial frame rectangle.
 * @param visibleColumns Shared column list (owned by BColumnListView).
 * @param sortColumns   Shared sort-column list.
 * @param listView      The master BColumnListView.
 */
OutlineView::OutlineView(BRect rect, BList* visibleColumns, BList* sortColumns,
	BColumnListView* listView)
	:
	BView(rect, "outline_view", B_FOLLOW_ALL_SIDES,
		B_WILL_DRAW | B_FRAME_EVENTS),
	fColumns(visibleColumns),
	fSortColumns(sortColumns),
	fItemsHeight(0.0),
	fVisibleRect(rect.OffsetToCopy(0, 0)),
	fFocusRow(0),
	fRollOverRow(0),
	fLastSelectedItem(0),
	fFirstSelectedItem(0),
	fSortThread(B_BAD_THREAD_ID),
	fCurrentState(INACTIVE),
	fMasterView(listView),
	fSelectionMode(B_MULTIPLE_SELECTION_LIST),
	fTrackMouse(false),
	fCurrentField(0),
	fCurrentRow(0),
	fCurrentColumn(0),
	fMouseDown(false),
	fCurrentCode(B_OUTSIDE_VIEW),
	fEditMode(false),
	fDragging(false),
	fClickCount(0),
	fDropHighlightY(-1)
{
	SetViewColor(B_TRANSPARENT_COLOR);

#if DOUBLE_BUFFERED_COLUMN_RESIZE
	fResizeBufferView = new ColumnResizeBufferView();
#endif

	FixScrollBar(true);
	fSelectionListDummyHead.fNextSelected = &fSelectionListDummyHead;
	fSelectionListDummyHead.fPrevSelected = &fSelectionListDummyHead;
}


/**
 * @brief Destroy the OutlineView and free all owned resources.
 *
 * Deletes the double-buffer view and removes all rows via Clear().
 */
OutlineView::~OutlineView()
{
#if DOUBLE_BUFFERED_COLUMN_RESIZE
	delete fResizeBufferView;
#endif

	Clear();
}


/**
 * @brief Remove and delete all rows, reset item height, and invalidate the view.
 *
 * The selection is cleared first to prevent dangling pointers in the selection list.
 */
void
OutlineView::Clear()
{
	DeselectAll();
		// Make sure selection list doesn't point to deleted rows!
	RecursiveDeleteRows(&fRows, false);
	fItemsHeight = 0.0;
	FixScrollBar(true);
	Invalidate();
}


/**
 * @brief Set the selection mode and deselect all currently selected rows.
 *
 * @param mode B_SINGLE_SELECTION_LIST or B_MULTIPLE_SELECTION_LIST.
 */
void
OutlineView::SetSelectionMode(list_view_type mode)
{
	DeselectAll();
	fSelectionMode = mode;
}


/**
 * @brief Return the current selection mode.
 *
 * @return B_SINGLE_SELECTION_LIST or B_MULTIPLE_SELECTION_LIST.
 */
list_view_type
OutlineView::SelectionMode() const
{
	return fSelectionMode;
}


/**
 * @brief Remove @p row from the selection list and invalidate it.
 *
 * @param row The row to deselect, or NULL (no-op).
 */
void
OutlineView::Deselect(BRow* row)
{
	if (row == NULL)
		return;

	if (row->fNextSelected != 0) {
		row->fNextSelected->fPrevSelected = row->fPrevSelected;
		row->fPrevSelected->fNextSelected = row->fNextSelected;
		row->fNextSelected = 0;
		row->fPrevSelected = 0;
		Invalidate();
	}
}


/**
 * @brief Add @p row to the selection list and invalidate it if visible.
 *
 * In B_SINGLE_SELECTION_LIST mode, all existing selections are cleared first.
 *
 * @param row The row to select, or NULL (no-op).
 */
void
OutlineView::AddToSelection(BRow* row)
{
	if (row == NULL)
		return;

	if (row->fNextSelected == 0) {
		if (fSelectionMode == B_SINGLE_SELECTION_LIST)
			DeselectAll();

		row->fNextSelected = fSelectionListDummyHead.fNextSelected;
		row->fPrevSelected = &fSelectionListDummyHead;
		row->fNextSelected->fPrevSelected = row;
		row->fPrevSelected->fNextSelected = row;

		BRect invalidRect;
		if (FindVisibleRect(row, &invalidRect))
			Invalidate(invalidRect);
	}
}


/**
 * @brief Recursively delete all rows in @p list and, optionally, the list itself.
 *
 * @param list    The container of rows to delete.
 * @param isOwner If true, @p list is deleted after all its rows are removed.
 */
void
OutlineView::RecursiveDeleteRows(BRowContainer* list, bool isOwner)
{
	if (list == NULL)
		return;

	while (true) {
		BRow* row = list->RemoveItemAt(0L);
		if (row == 0)
			break;

		if (row->fChildList)
			RecursiveDeleteRows(row->fChildList, true);

		delete row;
	}

	if (isOwner)
		delete list;
}


/**
 * @brief Redraw a single column's cells for all visible rows.
 *
 * Used during column resize to efficiently redraw only the affected column
 * strip. Uses double-buffered rendering when DOUBLE_BUFFERED_COLUMN_RESIZE
 * is enabled.
 *
 * @param column       The column to redraw.
 * @param leftEdge     The x coordinate of the column's left edge.
 * @param isFirstColumn true if this is the leftmost visible column (draws
 *                      latch widgets and applies outline indentation).
 */
void
OutlineView::RedrawColumn(BColumn* column, float leftEdge, bool isFirstColumn)
{
	// TODO: Remove code duplication (private function which takes a view
	// pointer, pass "this" in non-double buffered mode)!
	// Watch out for sourceRect versus destRect though!
	if (!column)
		return;

	PushState();

	font_height fh;
	GetFontHeight(&fh);
	float line = 0.0;
	bool tintedLine = true;
	for (RecursiveOutlineIterator iterator(&fRows); iterator.CurrentRow();
		line += iterator.CurrentRow()->Height() + 1, iterator.GoToNext()) {

		BRow* row = iterator.CurrentRow();
		float rowHeight = row->Height();
		if (line > fVisibleRect.bottom)
			break;
		tintedLine = !tintedLine;

		if (line + rowHeight >= fVisibleRect.top) {
#if DOUBLE_BUFFERED_COLUMN_RESIZE
			BRect sourceRect(0, 0, column->Width(), rowHeight);
#endif
			BRect destRect(leftEdge, line, leftEdge + column->Width(),
				line + rowHeight);

			rgb_color highColor;
			rgb_color lowColor;
			if (row->fNextSelected != 0) {
				if (fEditMode) {
					highColor = fMasterView->Color(B_COLOR_EDIT_BACKGROUND);
					lowColor = fMasterView->Color(B_COLOR_EDIT_BACKGROUND);
				} else {
					highColor = fMasterView->Color(B_COLOR_SELECTION);
					lowColor = fMasterView->Color(B_COLOR_SELECTION);
				}
			} else {
				highColor = fMasterView->Color(B_COLOR_BACKGROUND);
				lowColor = fMasterView->Color(B_COLOR_BACKGROUND);
			}
			if (tintedLine) {
				if (lowColor.IsLight())
					lowColor = tint_color(lowColor, kTintedLineTint);
				else
					lowColor = tint_color(lowColor, kTintedLineTintDark);
			}


#if DOUBLE_BUFFERED_COLUMN_RESIZE
			fResizeBufferView->Lock();

			fResizeBufferView->SetHighColor(highColor);
			fResizeBufferView->SetLowColor(lowColor);

			BFont font;
			GetFont(&font);
			fResizeBufferView->SetFont(&font);
			fResizeBufferView->FillRect(sourceRect, B_SOLID_LOW);

			if (isFirstColumn) {
				// If this is the first column, double buffer drawing the latch
				// too.
				destRect.left += iterator.CurrentLevel() * kOutlineLevelIndent
					- fMasterView->LatchWidth();
				sourceRect.left += iterator.CurrentLevel() * kOutlineLevelIndent
					- fMasterView->LatchWidth();

				LatchType pos = B_NO_LATCH;
				if (row->HasLatch())
					pos = row->fIsExpanded ? B_OPEN_LATCH : B_CLOSED_LATCH;

				BRect latchRect(sourceRect);
				latchRect.right = latchRect.left + fMasterView->LatchWidth();
				fMasterView->DrawLatch(fResizeBufferView, latchRect, pos, row);
			}

			BField* field = row->GetField(column->fFieldID);
			if (field) {
				BRect fieldRect(sourceRect);
				if (isFirstColumn)
					fieldRect.left += fMasterView->LatchWidth();

	#if CONSTRAIN_CLIPPING_REGION
				BRegion clipRegion(fieldRect);
				fResizeBufferView->PushState();
				fResizeBufferView->ConstrainClippingRegion(&clipRegion);
	#endif
				fResizeBufferView->SetHighColor(fMasterView->Color(
					row->fNextSelected ? B_COLOR_SELECTION_TEXT
						: B_COLOR_TEXT));
				float baseline = floor(fieldRect.top + fh.ascent
					+ (fieldRect.Height() + 1 - (fh.ascent+fh.descent)) / 2);
				fResizeBufferView->MovePenTo(fieldRect.left + 8, baseline);
				column->DrawField(field, fieldRect, fResizeBufferView);
	#if CONSTRAIN_CLIPPING_REGION
				fResizeBufferView->PopState();
	#endif
			}

			if (fFocusRow == row && !fEditMode && fMasterView->IsFocus()
				&& Window()->IsActive()) {
				fResizeBufferView->SetHighColor(fMasterView->Color(
					B_COLOR_ROW_DIVIDER));
				fResizeBufferView->StrokeRect(BRect(-1, sourceRect.top,
					10000.0, sourceRect.bottom));
			}

			fResizeBufferView->Sync();
			fResizeBufferView->Unlock();
			SetDrawingMode(B_OP_COPY);
			DrawBitmap(fResizeBufferView->Bitmap(), sourceRect, destRect);

#else

			SetHighColor(highColor);
			SetLowColor(lowColor);
			FillRect(destRect, B_SOLID_LOW);

			BField* field = row->GetField(column->fFieldID);
			if (field) {
	#if CONSTRAIN_CLIPPING_REGION
				BRegion clipRegion(destRect);
				PushState();
				ConstrainClippingRegion(&clipRegion);
	#endif
				SetHighColor(fMasterView->Color(row->fNextSelected
					? B_COLOR_SELECTION_TEXT : B_COLOR_TEXT));
				float baseline = floor(destRect.top + fh.ascent
					+ (destRect.Height() + 1 - (fh.ascent + fh.descent)) / 2);
				MovePenTo(destRect.left + 8, baseline);
				column->DrawField(field, destRect, this);
	#if CONSTRAIN_CLIPPING_REGION
				PopState();
	#endif
			}

			if (fFocusRow == row && !fEditMode && fMasterView->IsFocus()
				&& Window()->IsActive()) {
				SetHighColor(fMasterView->Color(B_COLOR_ROW_DIVIDER));
				StrokeRect(BRect(0, destRect.top, 10000.0, destRect.bottom));
			}
#endif
		}
	}

	PopState();
}


/**
 * @brief Draw all visible rows within @p invalidBounds.
 *
 * Iterates over the recursive row tree, renders each row's field cells,
 * latch widgets, and focus indicator, and fills empty space below the last row.
 * Also draws the drop-target highlight line when a drag is in progress.
 *
 * @param invalidBounds The rectangle that needs to be redrawn.
 */
void
OutlineView::Draw(BRect invalidBounds)
{
#if SMART_REDRAW
	BRegion invalidRegion;
	GetClippingRegion(&invalidRegion);
#endif

	font_height fh;
	GetFontHeight(&fh);

	float line = 0.0;
	bool tintedLine = true;
	int32 numColumns = fColumns->CountItems();
	for (RecursiveOutlineIterator iterator(&fRows); iterator.CurrentRow();
		iterator.GoToNext()) {
		BRow* row = iterator.CurrentRow();
		if (line > invalidBounds.bottom)
			break;

		tintedLine = !tintedLine;
		float rowHeight = row->Height();

		if (line >= invalidBounds.top - rowHeight) {
			bool isFirstColumn = true;
			float fieldLeftEdge = MAX(kLeftMargin, fMasterView->LatchWidth());

			// setup background color
			rgb_color lowColor;
			if (row->fNextSelected != 0) {
				if (Window()->IsActive()) {
					if (fEditMode)
						lowColor = fMasterView->Color(B_COLOR_EDIT_BACKGROUND);
					else
						lowColor = fMasterView->Color(B_COLOR_SELECTION);
				}
				else
					lowColor = fMasterView->Color(B_COLOR_NON_FOCUS_SELECTION);
			} else
				lowColor = fMasterView->Color(B_COLOR_BACKGROUND);
			if (tintedLine) {
				if (lowColor.IsLight())
					lowColor = tint_color(lowColor, kTintedLineTint);
				else
					lowColor = tint_color(lowColor, kTintedLineTintDark);
			}

			for (int columnIndex = 0; columnIndex < numColumns; columnIndex++) {
				BColumn* column = (BColumn*) fColumns->ItemAt(columnIndex);
				if (!column->IsVisible())
					continue;

				if (!isFirstColumn && fieldLeftEdge > invalidBounds.right)
					break;

				if (fieldLeftEdge + column->Width() >= invalidBounds.left) {
					BRect fullRect(fieldLeftEdge, line,
						fieldLeftEdge + column->Width(), line + rowHeight);

					bool clippedFirstColumn = false;
						// This happens when a column is indented past the
						// beginning of the next column.

					SetHighColor(lowColor);

					BRect destRect(fullRect);
					if (isFirstColumn) {
						fullRect.left -= fMasterView->LatchWidth();
						destRect.left += iterator.CurrentLevel()
							* kOutlineLevelIndent;
						if (destRect.left >= destRect.right) {
							// clipped
							FillRect(BRect(0, line, fieldLeftEdge
								+ column->Width(), line + rowHeight));
							clippedFirstColumn = true;
						}

						FillRect(BRect(0, line, MAX(kLeftMargin,
							fMasterView->LatchWidth()), line + row->Height()));
					}


#if SMART_REDRAW
					if (!clippedFirstColumn
						&& invalidRegion.Intersects(fullRect)) {
#else
					if (!clippedFirstColumn) {
#endif
						FillRect(fullRect);	// Using color set above

						// Draw the latch widget if it has one.
						if (isFirstColumn) {
							if (row == fTargetRow
								&& fCurrentState == LATCH_CLICKED) {
								// Note that this only occurs if the user is
								// holding down a latch while items are added
								// in the background.
								BPoint pos;
								uint32 buttons;
								GetMouse(&pos, &buttons);
								if (fLatchRect.Contains(pos)) {
									fMasterView->DrawLatch(this, fLatchRect,
										B_PRESSED_LATCH, fTargetRow);
								} else {
									fMasterView->DrawLatch(this, fLatchRect,
										row->fIsExpanded ? B_OPEN_LATCH
											: B_CLOSED_LATCH, fTargetRow);
								}
							} else {
								LatchType pos = B_NO_LATCH;
								if (row->HasLatch())
									pos = row->fIsExpanded ? B_OPEN_LATCH
										: B_CLOSED_LATCH;

								fMasterView->DrawLatch(this,
									BRect(destRect.left
										- fMasterView->LatchWidth(),
									destRect.top, destRect.left,
									destRect.bottom), pos, row);
							}
						}

						SetHighColor(fMasterView->HighColor());
							// The master view just holds the high color for us.
						SetLowColor(lowColor);

						BField* field = row->GetField(column->fFieldID);
						if (field) {
#if CONSTRAIN_CLIPPING_REGION
							BRegion clipRegion(destRect);
							PushState();
							ConstrainClippingRegion(&clipRegion);
#endif
							SetHighColor(fMasterView->Color(
								row->fNextSelected ? B_COLOR_SELECTION_TEXT
								: B_COLOR_TEXT));
							float baseline = floor(destRect.top + fh.ascent
								+ (destRect.Height() + 1
								- (fh.ascent+fh.descent)) / 2);
							MovePenTo(destRect.left + 8, baseline);
							column->DrawField(field, destRect, this);
#if CONSTRAIN_CLIPPING_REGION
							PopState();
#endif
						}
					}
				}

				isFirstColumn = false;
				fieldLeftEdge += column->Width() + 1;
			}

			if (fieldLeftEdge <= invalidBounds.right) {
				SetHighColor(lowColor);
				FillRect(BRect(fieldLeftEdge, line, invalidBounds.right,
					line + rowHeight));
			}
		}

		// indicate the keyboard focus row
		if (fFocusRow == row && !fEditMode && fMasterView->IsFocus()
			&& Window()->IsActive()) {
			SetHighColor(fMasterView->Color(B_COLOR_ROW_DIVIDER));
			StrokeRect(BRect(0, line, 10000.0, line + rowHeight));
		}

		line += rowHeight + 1;
	}

	if (line <= invalidBounds.bottom) {
		// fill background below last item
		SetHighColor(fMasterView->Color(B_COLOR_BACKGROUND));
		FillRect(BRect(invalidBounds.left, line, invalidBounds.right,
			invalidBounds.bottom));
	}

	// Draw the drop target line
	if (fDropHighlightY != -1) {
		InvertRect(BRect(0, fDropHighlightY - kDropHighlightLineHeight / 2,
			1000000, fDropHighlightY + kDropHighlightLineHeight / 2));
	}
}


/**
 * @brief Find the row displayed at the given vertical position.
 *
 * @param ypos       Vertical position in the outline view's coordinate system.
 * @param _rowIndent Output: the nesting depth (indent level) of the found row.
 * @param _top       Output: the y coordinate of the found row's top edge.
 * @return The BRow at @p ypos, or NULL if no row is there.
 */
BRow*
OutlineView::FindRow(float ypos, int32* _rowIndent, float* _top)
{
	if (_rowIndent && _top) {
		float line = 0.0;
		for (RecursiveOutlineIterator iterator(&fRows); iterator.CurrentRow();
			iterator.GoToNext()) {

			BRow* row = iterator.CurrentRow();
			if (line > ypos)
				break;

			float rowHeight = row->Height();
			if (ypos <= line + rowHeight) {
				*_top = line;
				*_rowIndent = iterator.CurrentLevel();
				return row;
			}

			line += rowHeight + 1;
		}
	}

	return NULL;
}

/**
 * @brief Enable or disable mouse-over (roll-over) row highlighting.
 *
 * When disabled, any existing drop highlight line is immediately erased.
 *
 * @param enabled true to enable tracking, false to disable.
 */
void OutlineView::SetMouseTrackingEnabled(bool enabled)
{
	fTrackMouse = enabled;
	if (!enabled && fDropHighlightY != -1) {
		// Erase the old target line
		InvertRect(BRect(0, fDropHighlightY - kDropHighlightLineHeight / 2,
			1000000, fDropHighlightY + kDropHighlightLineHeight / 2));
		fDropHighlightY = -1;
	}
}


//
// Note that this interaction is not totally safe.  If items are added to
// the list in the background, the widget rect will be incorrect, possibly
// resulting in drawing glitches.  The code that adds items needs to be a little smarter
// about invalidating state.
//
/**
 * @brief Handle a mouse-button press in the outline view.
 *
 * Determines whether the click landed on a latch widget or a data row, then
 * updates the focus row, selection state, and field mouse-down state accordingly.
 * Captures the mouse for subsequent MouseMoved() and MouseUp() events.
 *
 * @param position The mouse position in outline-view coordinates.
 */
void
OutlineView::MouseDown(BPoint position)
{
	if (!fEditMode)
		fMasterView->MakeFocus(true);

	// Check to see if the user is clicking on a widget to open a section
	// of the list.
	bool reset_click_count = false;
	int32 indent;
	float rowTop;
	BRow* row = FindRow(position.y, &indent, &rowTop);
	if (row != NULL) {

		// Update fCurrentField
		bool handle_field = false;
		BField* new_field = 0;
		BRow* new_row = 0;
		BColumn* new_column = 0;
		BRect new_rect;

		if (position.y >= 0) {
			if (position.x >= 0) {
				float x = 0;
				for (int32 c = 0; c < fMasterView->CountColumns(); c++) {
					new_column = fMasterView->ColumnAt(c);
					if (!new_column->IsVisible())
						continue;
					if ((MAX(kLeftMargin, fMasterView->LatchWidth()) + x)
						+ new_column->Width() >= position.x) {
						if (new_column->WantsEvents()) {
							new_field = row->GetField(c);
							new_row = row;
							FindRect(new_row,&new_rect);
							new_rect.left = MAX(kLeftMargin,
								fMasterView->LatchWidth()) + x;
							new_rect.right = new_rect.left
								+ new_column->Width() - 1;
							handle_field = true;
						}
						break;
					}
					x += new_column->Width();
				}
			}
		}

		// Handle mouse down
		if (handle_field) {
			fMouseDown = true;
			fFieldRect = new_rect;
			fCurrentColumn = new_column;
			fCurrentRow = new_row;
			fCurrentField = new_field;
			fCurrentCode = B_INSIDE_VIEW;
			BMessage* message = Window()->CurrentMessage();
			int32 buttons = 1;
			message->FindInt32("buttons", &buttons);
			fCurrentColumn->MouseDown(fMasterView, fCurrentRow,
				fCurrentField, fFieldRect, position, buttons);
		}

		if (!fEditMode) {

			fTargetRow = row;
			fTargetRowTop = rowTop;
			FindVisibleRect(fFocusRow, &fFocusRowRect);

			float leftWidgetBoundry = indent * kOutlineLevelIndent
				+ MAX(kLeftMargin, fMasterView->LatchWidth())
				- fMasterView->LatchWidth();
			fLatchRect.Set(leftWidgetBoundry, rowTop, leftWidgetBoundry
				+ fMasterView->LatchWidth(), rowTop + row->Height());
			if (fLatchRect.Contains(position) && row->HasLatch()) {
				fCurrentState = LATCH_CLICKED;
				if (fTargetRow->fNextSelected != 0)
					SetHighColor(fMasterView->Color(B_COLOR_SELECTION));
				else
					SetHighColor(fMasterView->Color(B_COLOR_BACKGROUND));

				FillRect(fLatchRect);
				if (fLatchRect.Contains(position)) {
					fMasterView->DrawLatch(this, fLatchRect, B_PRESSED_LATCH,
						row);
				} else {
					fMasterView->DrawLatch(this, fLatchRect,
						fTargetRow->fIsExpanded ? B_OPEN_LATCH
						: B_CLOSED_LATCH, row);
				}
			} else {
				Invalidate(fFocusRowRect);
				fFocusRow = fTargetRow;
				FindVisibleRect(fFocusRow, &fFocusRowRect);

				ASSERT(fTargetRow != 0);

				if ((modifiers() & B_CONTROL_KEY) == 0)
					DeselectAll();

				if ((modifiers() & B_SHIFT_KEY) != 0 && fFirstSelectedItem != 0
					&& fSelectionMode == B_MULTIPLE_SELECTION_LIST) {
					SelectRange(fFirstSelectedItem, fTargetRow);
				}
				else {
					if (fTargetRow->fNextSelected != 0) {
						// Unselect row
						fTargetRow->fNextSelected->fPrevSelected
							= fTargetRow->fPrevSelected;
						fTargetRow->fPrevSelected->fNextSelected
							= fTargetRow->fNextSelected;
						fTargetRow->fPrevSelected = 0;
						fTargetRow->fNextSelected = 0;
						fFirstSelectedItem = NULL;
					} else {
						// Select row
						if (fSelectionMode == B_SINGLE_SELECTION_LIST)
							DeselectAll();

						fTargetRow->fNextSelected
							= fSelectionListDummyHead.fNextSelected;
						fTargetRow->fPrevSelected
							= &fSelectionListDummyHead;
						fTargetRow->fNextSelected->fPrevSelected = fTargetRow;
						fTargetRow->fPrevSelected->fNextSelected = fTargetRow;
						fFirstSelectedItem = fTargetRow;
					}

					Invalidate(BRect(fVisibleRect.left, fTargetRowTop,
						fVisibleRect.right,
						fTargetRowTop + fTargetRow->Height()));
				}

				fCurrentState = ROW_CLICKED;
				if (fLastSelectedItem != fTargetRow)
					reset_click_count = true;
				fLastSelectedItem = fTargetRow;
				fMasterView->SelectionChanged();

			}
		}

		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS |
			B_NO_POINTER_HISTORY);

	} else if (fFocusRow != 0) {
		// User clicked in open space, unhighlight focus row.
		FindVisibleRect(fFocusRow, &fFocusRowRect);
		fFocusRow = 0;
		Invalidate(fFocusRowRect);
	}

	// We stash the click counts here because the 'clicks' field
	// is not in the CurrentMessage() when MouseUp is called... ;(
	if (reset_click_count)
		fClickCount = 1;
	else
		Window()->CurrentMessage()->FindInt32("clicks", &fClickCount);
	fClickPoint = position;

}


/**
 * @brief Handle mouse movement in the outline view.
 *
 * While no button is held, tracks the field under the cursor and dispatches
 * BColumn::MouseMoved() enter/inside/exit events. While dragging, either
 * updates the latch visual, initiates a row drag via BColumnListView::InitiateDrag(),
 * or tracks the drop highlight position.
 *
 * @param position    Current mouse position in outline-view coordinates.
 */
void
OutlineView::MouseMoved(BPoint position, uint32 /*transit*/,
	const BMessage* /*dragMessage*/)
{
	if (!fMouseDown) {
		// Update fCurrentField
		bool handle_field = false;
		BField* new_field = 0;
		BRow* new_row = 0;
		BColumn* new_column = 0;
		BRect new_rect(0,0,0,0);
		if (position.y >=0 ) {
			float top;
			int32 indent;
			BRow* row = FindRow(position.y, &indent, &top);
			if (row && position.x >=0 ) {
				float x=0;
				for (int32 c=0;c<fMasterView->CountColumns();c++) {
					new_column = fMasterView->ColumnAt(c);
					if (!new_column->IsVisible())
						continue;
					if ((MAX(kLeftMargin,
						fMasterView->LatchWidth()) + x) + new_column->Width()
						> position.x) {

						if(new_column->WantsEvents()) {
							new_field = row->GetField(c);
							new_row = row;
							FindRect(new_row,&new_rect);
							new_rect.left = MAX(kLeftMargin,
								fMasterView->LatchWidth()) + x;
							new_rect.right = new_rect.left
								+ new_column->Width() - 1;
							handle_field = true;
						}
						break;
					}
					x += new_column->Width();
				}
			}
		}

		// Handle mouse moved
		if (handle_field) {
			if (new_field != fCurrentField) {
				if (fCurrentField) {
					fCurrentColumn->MouseMoved(fMasterView, fCurrentRow,
						fCurrentField, fFieldRect, position, 0,
						fCurrentCode = B_EXITED_VIEW);
				}
				fCurrentColumn = new_column;
				fCurrentRow = new_row;
				fCurrentField = new_field;
				fFieldRect = new_rect;
				if (fCurrentField) {
					fCurrentColumn->MouseMoved(fMasterView, fCurrentRow,
						fCurrentField, fFieldRect, position, 0,
						fCurrentCode = B_ENTERED_VIEW);
				}
			} else {
				if (fCurrentField) {
					fCurrentColumn->MouseMoved(fMasterView, fCurrentRow,
						fCurrentField, fFieldRect, position, 0,
						fCurrentCode = B_INSIDE_VIEW);
				}
			}
		} else {
			if (fCurrentField) {
				fCurrentColumn->MouseMoved(fMasterView, fCurrentRow,
					fCurrentField, fFieldRect, position, 0,
					fCurrentCode = B_EXITED_VIEW);
				fCurrentField = 0;
				fCurrentColumn = 0;
				fCurrentRow = 0;
			}
		}
	} else {
		if (fCurrentField) {
			if (fFieldRect.Contains(position)) {
				if (fCurrentCode == B_OUTSIDE_VIEW
					|| fCurrentCode == B_EXITED_VIEW) {
					fCurrentColumn->MouseMoved(fMasterView, fCurrentRow,
						fCurrentField, fFieldRect, position, 1,
						fCurrentCode = B_ENTERED_VIEW);
				} else {
					fCurrentColumn->MouseMoved(fMasterView, fCurrentRow,
						fCurrentField, fFieldRect, position, 1,
						fCurrentCode = B_INSIDE_VIEW);
				}
			} else {
				if (fCurrentCode == B_INSIDE_VIEW
					|| fCurrentCode == B_ENTERED_VIEW) {
					fCurrentColumn->MouseMoved(fMasterView, fCurrentRow,
						fCurrentField, fFieldRect, position, 1,
						fCurrentCode = B_EXITED_VIEW);
				} else {
					fCurrentColumn->MouseMoved(fMasterView, fCurrentRow,
						fCurrentField, fFieldRect, position, 1,
						fCurrentCode = B_OUTSIDE_VIEW);
				}
			}
		}
	}

	if (!fEditMode) {

		switch (fCurrentState) {
			case LATCH_CLICKED:
				if (fTargetRow->fNextSelected != 0)
					SetHighColor(fMasterView->Color(B_COLOR_SELECTION));
				else
					SetHighColor(fMasterView->Color(B_COLOR_BACKGROUND));

				FillRect(fLatchRect);
				if (fLatchRect.Contains(position)) {
					fMasterView->DrawLatch(this, fLatchRect, B_PRESSED_LATCH,
						fTargetRow);
				} else {
					fMasterView->DrawLatch(this, fLatchRect,
						fTargetRow->fIsExpanded ? B_OPEN_LATCH : B_CLOSED_LATCH,
						fTargetRow);
				}
				break;

			case ROW_CLICKED:
				if (abs((int)(position.x - fClickPoint.x)) > kRowDragSensitivity
					|| abs((int)(position.y - fClickPoint.y))
						> kRowDragSensitivity) {
					fCurrentState = DRAGGING_ROWS;
					fMasterView->InitiateDrag(fClickPoint,
						fTargetRow->fNextSelected != 0);
				}
				break;

			case DRAGGING_ROWS:
#if 0
				// falls through...
#else
				if (fTrackMouse /*&& message*/) {
					if (fVisibleRect.Contains(position)) {
						float top;
						int32 indent;
						BRow* target = FindRow(position.y, &indent, &top);
						if (target)
							SetFocusRow(target, true);
					}
				}
				break;
#endif

			default: {

				if (fTrackMouse /*&& message*/) {
					// Draw a highlight line...
					if (fVisibleRect.Contains(position)) {
						float top;
						int32 indent;
						BRow* target = FindRow(position.y, &indent, &top);
						if (target == fRollOverRow)
							break;
						if (fRollOverRow) {
							BRect rect;
							FindRect(fRollOverRow, &rect);
							Invalidate(rect);
						}
						fRollOverRow = target;
#if 0
						SetFocusRow(fRollOverRow,false);
#else
						PushState();
						SetDrawingMode(B_OP_BLEND);
						SetHighColor(255, 255, 255, 255);
						BRect rect;
						FindRect(fRollOverRow, &rect);
						rect.bottom -= 1.0;
						FillRect(rect);
						PopState();
#endif
					} else {
						if (fRollOverRow) {
							BRect rect;
							FindRect(fRollOverRow, &rect);
							Invalidate(rect);
							fRollOverRow = NULL;
						}
					}
				}
			}
		}
	}
}


/**
 * @brief Handle a mouse-button release in the outline view.
 *
 * Finalizes a latch click (expand/collapse), detects double-clicks for item
 * invocation, ends a row drag, and erases the drop highlight line.
 *
 * @param position The mouse position in outline-view coordinates at release time.
 */
void
OutlineView::MouseUp(BPoint position)
{
	if (fCurrentField) {
		fCurrentColumn->MouseUp(fMasterView, fCurrentRow, fCurrentField);
		fMouseDown = false;
	}

	if (fEditMode)
		return;

	switch (fCurrentState) {
		case LATCH_CLICKED:
			if (fLatchRect.Contains(position)) {
				fMasterView->ExpandOrCollapse(fTargetRow,
					!fTargetRow->fIsExpanded);
			}

			Invalidate(fLatchRect);
			fCurrentState = INACTIVE;
			break;

		case ROW_CLICKED:
			if (fClickCount > 1
				&& abs((int)fClickPoint.x - (int)position.x)
					< kDoubleClickMoveSensitivity
				&& abs((int)fClickPoint.y - (int)position.y)
					< kDoubleClickMoveSensitivity) {
				fMasterView->ItemInvoked();
			}
			fCurrentState = INACTIVE;
			break;

		case DRAGGING_ROWS:
			fCurrentState = INACTIVE;
			// Falls through

		default:
			if (fDropHighlightY != -1) {
				InvertRect(BRect(0,
					fDropHighlightY - kDropHighlightLineHeight / 2,
					1000000, fDropHighlightY + kDropHighlightLineHeight / 2));
					// Erase the old target line
				fDropHighlightY = -1;
			}
	}
}


/**
 * @brief Handle messages received by the outline view.
 *
 * Dropped messages are forwarded to BColumnListView::MessageDropped().
 * All other messages are passed to the base class.
 *
 * @param message The received BMessage.
 */
void
OutlineView::MessageReceived(BMessage* message)
{
	if (message->WasDropped()) {
		fMasterView->MessageDropped(message,
			ConvertFromScreen(message->DropPoint()));
	} else {
		BView::MessageReceived(message);
	}
}


#if DOUBLE_BUFFERED_COLUMN_RESIZE

/**
 * @brief Return the double-buffer resize view used for off-screen column rendering.
 *
 * @return Pointer to the ColumnResizeBufferView.
 */
ColumnResizeBufferView*
OutlineView::ResizeBufferView()
{
	return fResizeBufferView;
}

#endif


/**
 * @brief Move keyboard focus to the next or previous row and optionally update the selection.
 *
 * @param up                  true to move focus upward, false to move downward.
 * @param updateSelection     If true, the newly focused row is selected.
 * @param addToCurrentSelection If true, the new row is added to an existing
 *                            multi-row selection rather than replacing it.
 */
void
OutlineView::ChangeFocusRow(bool up, bool updateSelection,
	bool addToCurrentSelection)
{
	int32 indent;
	float top;
	float newRowPos = 0;
	float verticalScroll = 0;

	if (fFocusRow) {
		// A row currently has the focus, get information about it
		newRowPos = fFocusRowRect.top + (up ? -4 : fFocusRow->Height() + 4);
		if (newRowPos < fVisibleRect.top + 20)
			verticalScroll = newRowPos - 20;
		else if (newRowPos > fVisibleRect.bottom - 20)
			verticalScroll = newRowPos - fVisibleRect.Height() + 20;
	} else
		newRowPos = fVisibleRect.top + 2;
			// no row is currently focused, set this to the top of the window
			// so we will select the first visible item in the list.

	BRow* newRow = FindRow(newRowPos, &indent, &top);
	if (newRow) {
		if (fFocusRow) {
			fFocusRowRect.right = 10000;
			Invalidate(fFocusRowRect);
		}
		BRow* oldFocusRow = fFocusRow;
		fFocusRow = newRow;
		fFocusRowRect.top = top;
		fFocusRowRect.left = 0;
		fFocusRowRect.right = 10000;
		fFocusRowRect.bottom = fFocusRowRect.top + fFocusRow->Height();
		Invalidate(fFocusRowRect);

		if (updateSelection) {
			if (!addToCurrentSelection
				|| fSelectionMode == B_SINGLE_SELECTION_LIST) {
				DeselectAll();
			}

			// if the focus row isn't selected, add it to the selection
			if (fFocusRow->fNextSelected == 0) {
				fFocusRow->fNextSelected
					= fSelectionListDummyHead.fNextSelected;
				fFocusRow->fPrevSelected = &fSelectionListDummyHead;
				fFocusRow->fNextSelected->fPrevSelected = fFocusRow;
				fFocusRow->fPrevSelected->fNextSelected = fFocusRow;
			} else if (oldFocusRow != NULL
				&& fSelectionListDummyHead.fNextSelected == oldFocusRow
				&& (((IndexOf(oldFocusRow->fNextSelected)
						< IndexOf(oldFocusRow)) == up)
					|| fFocusRow == oldFocusRow->fNextSelected)) {
					// if the focus row is selected, if:
					// 1. the previous focus row is last in the selection
					// 2a. the next selected row is now the focus row
					// 2b. or the next selected row is beyond the focus row
					//	   in the move direction
					// then deselect the previous focus row
				fSelectionListDummyHead.fNextSelected
					= oldFocusRow->fNextSelected;
				if (fSelectionListDummyHead.fNextSelected != NULL) {
					fSelectionListDummyHead.fNextSelected->fPrevSelected
						= &fSelectionListDummyHead;
					oldFocusRow->fNextSelected = NULL;
				}
				oldFocusRow->fPrevSelected = NULL;
			}

			fLastSelectedItem = fFocusRow;
		}
	} else
		Invalidate(fFocusRowRect);

	if (verticalScroll != 0) {
		BScrollBar* vScrollBar = ScrollBar(B_VERTICAL);
		float min, max;
		vScrollBar->GetRange(&min, &max);
		if (verticalScroll < min)
			verticalScroll = min;
		else if (verticalScroll > max)
			verticalScroll = max;

		vScrollBar->SetValue(verticalScroll);
	}

	if (newRow && updateSelection)
		fMasterView->SelectionChanged();
}


/**
 * @brief Move keyboard focus to the first visible row in the current viewport.
 *
 * Clears the current focus and calls ChangeFocusRow() to select the first
 * row visible at the top of the scrolled region.
 */
void
OutlineView::MoveFocusToVisibleRect()
{
	fFocusRow = 0;
	ChangeFocusRow(true, true, false);
}


/**
 * @brief Return the next selected row after @p lastSelected in selection order.
 *
 * Pass NULL to start from the beginning of the selection. The selection list
 * is a circular intrusive doubly-linked list with a dummy head sentinel.
 *
 * @param lastSelected The previously returned row, or NULL to start.
 * @return The next selected BRow, or NULL when the list is exhausted.
 */
BRow*
OutlineView::CurrentSelection(BRow* lastSelected) const
{
	BRow* row;
	if (lastSelected == 0)
		row = fSelectionListDummyHead.fNextSelected;
	else
		row = lastSelected->fNextSelected;


	if (row == &fSelectionListDummyHead)
		row = 0;

	return row;
}


/**
 * @brief Toggle the selection state of the focused row.
 *
 * When @p selectRange is true and the selection mode is multiple, selects the
 * range of rows between the last selected row and the focus row.
 *
 * @param selectRange If true, a range selection from fLastSelectedItem to the
 *                    focus row is performed.
 */
void
OutlineView::ToggleFocusRowSelection(bool selectRange)
{
	if (fFocusRow == 0)
		return;

	if (selectRange && fSelectionMode == B_MULTIPLE_SELECTION_LIST)
		SelectRange(fLastSelectedItem, fFocusRow);
	else {
		if (fFocusRow->fNextSelected != 0) {
			// Unselect row
			fFocusRow->fNextSelected->fPrevSelected = fFocusRow->fPrevSelected;
			fFocusRow->fPrevSelected->fNextSelected = fFocusRow->fNextSelected;
			fFocusRow->fPrevSelected = 0;
			fFocusRow->fNextSelected = 0;
		} else {
			// Select row
			if (fSelectionMode == B_SINGLE_SELECTION_LIST)
				DeselectAll();

			fFocusRow->fNextSelected = fSelectionListDummyHead.fNextSelected;
			fFocusRow->fPrevSelected = &fSelectionListDummyHead;
			fFocusRow->fNextSelected->fPrevSelected = fFocusRow;
			fFocusRow->fPrevSelected->fNextSelected = fFocusRow;
		}
	}

	fLastSelectedItem = fFocusRow;
	fMasterView->SelectionChanged();
	Invalidate(fFocusRowRect);
}


/**
 * @brief Toggle the expand/collapse state of the focused row.
 *
 * Has no effect if there is no focused row.
 */
void
OutlineView::ToggleFocusRowOpen()
{
	if (fFocusRow)
		fMasterView->ExpandOrCollapse(fFocusRow, !fFocusRow->fIsExpanded);
}


/**
 * @brief Expand or collapse the child list of @p parentRow.
 *
 * Recomputes fItemsHeight by summing (or subtracting) the heights of all rows
 * in the sub-tree, then invalidates and fixes the scroll bar.
 *
 * @param parentRow The row to expand or collapse.
 * @param expand    true to expand, false to collapse.
 */
void
OutlineView::ExpandOrCollapse(BRow* parentRow, bool expand)
{
	// TODO: Could use CopyBits here to speed things up.

	if (parentRow == NULL)
		return;

	if (parentRow->fIsExpanded == expand)
		return;

	parentRow->fIsExpanded = expand;

	BRect parentRect;
	if (FindRect(parentRow, &parentRect)) {
		// Determine my new height
		float subTreeHeight = 0.0;
		if (parentRow->fIsExpanded)
			for (RecursiveOutlineIterator iterator(parentRow->fChildList);
			     iterator.CurrentRow();
			     iterator.GoToNext()
			    )
			{
				subTreeHeight += iterator.CurrentRow()->Height()+1;
			}
		else
			for (RecursiveOutlineIterator iterator(parentRow->fChildList);
			     iterator.CurrentRow();
			     iterator.GoToNext()
			    )
			{
				subTreeHeight -= iterator.CurrentRow()->Height()+1;
			}
		fItemsHeight += subTreeHeight;

		// Adjust focus row if necessary.
		if (FindRect(fFocusRow, &fFocusRowRect) == false) {
			// focus row is in a subtree that has collapsed,
			// move it up to the parent.
			fFocusRow = parentRow;
			FindRect(fFocusRow, &fFocusRowRect);
		}

		Invalidate(BRect(0, parentRect.top, fVisibleRect.right,
			fVisibleRect.bottom));
		FixScrollBar(false);
	}
}


/*!	This method will remove the row from the selection if it is in the
	selection, but does not trigger any UI update.
*/
/**
 * @brief Unlink @p row from the selection list without triggering any UI update.
 *
 * @param row The row to remove from the selection intrusive list.
 * @return true if the row was in the selection and was removed, false otherwise.
 */
bool
OutlineView::RemoveRowFromSelectionOnly(BRow* row)
{
	if (row->fNextSelected != 0) {
		row->fNextSelected->fPrevSelected = row->fPrevSelected;
		row->fPrevSelected->fNextSelected = row->fNextSelected;
		row->fPrevSelected = 0;
		row->fNextSelected = 0;
		return true;
	}
	return false;
}


/**
 * @brief Remove a single @p row from the outline view.
 *
 * Adjusts fItemsHeight, fixes the scroll bar, updates the focus row if it was
 * inside the removed sub-tree, removes the row from the selection if necessary,
 * and clears the current field/column/row pointers.
 *
 * @param row The row to remove, or NULL (no-op).
 */
void
OutlineView::RemoveRow(BRow* row)
{
	if (row == NULL)
		return;

	BRow* parentRow = NULL;
	bool parentIsVisible = false;
	FindParent(row, &parentRow, &parentIsVisible);
		// NOTE: This could be a root row without a parent, in which case
		// it is always visible, though.

	// Adjust height for the visible sub-tree that is going to be removed.
	float subTreeHeight = 0.0f;

	if (parentIsVisible && (parentRow == NULL || parentRow->fIsExpanded)) {
		// The row itself is visible at least.
		subTreeHeight = row->Height() + 1;
		if (row->fIsExpanded) {
			// Adjust for the height of visible sub-items as well.
			// (By default, the iterator follows open branches only.)
			for (RecursiveOutlineIterator iterator(row->fChildList);
				iterator.CurrentRow(); iterator.GoToNext())
				subTreeHeight += iterator.CurrentRow()->Height() + 1;
		}
		BRect invalid;
		if (FindRect(row, &invalid)) {
			invalid.bottom = Bounds().bottom;
			if (invalid.IsValid())
				Invalidate(invalid);
		}
	}

	fItemsHeight -= subTreeHeight;

	FixScrollBar(false);
	int32 indent = 0;
	float top = 0.0;
	if (FindRow(fVisibleRect.top, &indent, &top) == NULL && ScrollBar(B_VERTICAL) != NULL) {
		// after removing this row, no rows are actually visible any more,
		// force a scroll to make them visible again
		if (fItemsHeight > fVisibleRect.Height())
			ScrollBy(0.0, fItemsHeight - fVisibleRect.Height() - Bounds().top);
		else
			ScrollBy(0.0, -Bounds().top);
	}
	if (parentRow != NULL) {
		parentRow->fChildList->RemoveItem(row);
		if (parentRow->fChildList->CountItems() == 0) {
			delete parentRow->fChildList;
			parentRow->fChildList = 0;
			// It was the last child row of the parent, which also means the
			// latch disappears.
			BRect parentRowRect;
			if (parentIsVisible && FindRect(parentRow, &parentRowRect))
				Invalidate(parentRowRect);
		}
	} else
		fRows.RemoveItem(row);

	// Adjust focus row if necessary.
	if (fFocusRow && !FindRect(fFocusRow, &fFocusRowRect)) {
		// focus row is in a subtree that is gone, move it up to the parent.
		fFocusRow = parentRow;
		if (fFocusRow)
			FindRect(fFocusRow, &fFocusRowRect);
	}

	// Remove this from the selection if necessary
	if (RemoveRowFromSelectionOnly(row))
		fMasterView->SelectionChanged();

	fCurrentColumn = 0;
	fCurrentRow = 0;
	fCurrentField = 0;
}


/**
 * @brief Remove multiple rows at once from the outline view.
 *
 * All rows must share the same parent. Invalidation is batched for better
 * performance compared to calling RemoveRow() individually for each row.
 * Triggers SelectionChanged() once if any removed row was selected.
 *
 * @param rows A BList of BRow pointers to remove. All must have the same parent.
 */
void
OutlineView::RemoveRows(BList* rows)
{
	if (rows->IsEmpty())
		return;

	// a limitation of the method is that all of the rows must be on the same
	// parent.

	BRow* parentRow = NULL;
	int32 countRows = rows->CountItems();

	for (int32 i = 0; i < countRows; i++) {
		BRow* row = static_cast<BRow*>(rows->ItemAt(i));
		if (i == 0) {
			parentRow = row->fParent;
		} else if (parentRow != row->fParent) {
			debugger("during bulk removal all rows must be from the same parent");
			return;
		}
	}

	// figure out the size to remove from the parent.

	bool parentIsVisible = parentRow == NULL;
		// NOTE: This could be a root row without a parent, in which case
		// it is always visible.
	BRect parentRowRect;

	if (parentRow)
		parentIsVisible = FindRect(parentRow, &parentRowRect);

	// Adjust height for the visible sub-tree that is going to be removed.
	float subTreesHeight = 0.0f;
	if (parentIsVisible && (parentRow == NULL || parentRow->fIsExpanded)) {

		BRect invalidAll;

		for (int32 i = 0; i < countRows; i++) {
			BRow* row = static_cast<BRow*>(rows->ItemAt(i));

			// The row itself is visible at least.
			subTreesHeight += row->Height() + 1;

			if (row->fIsExpanded) {
				// Adjust for the height of visible sub-items as well.
				// (By default, the iterator follows open branches only.)
				for (RecursiveOutlineIterator iterator(row->fChildList);
					iterator.CurrentRow(); iterator.GoToNext()) {
					subTreesHeight += iterator.CurrentRow()->Height() + 1;
				}
			}

			// Collect a rect of all of the deleted rects then they can be
			// invalidated at once.

			BRect invalid;
			if (FindRect(row, &invalid)) {
				if (!invalidAll.IsValid())
					invalidAll = invalid;
				else
					invalidAll = invalidAll | invalid;
			}
		}

		if (invalidAll.IsValid()) {
			invalidAll.bottom = Bounds().bottom;
			if (invalidAll.IsValid() && invalidAll.top < fVisibleRect.bottom)
				Invalidate(invalidAll);
		}
	}

	fItemsHeight -= subTreesHeight;

	FixScrollBar(true);

	int32 indent = 0;
	float top = 0.0;
	if (FindRow(fVisibleRect.top, &indent, &top) == NULL && ScrollBar(B_VERTICAL) != NULL) {
		// after removing this row, no rows are actually visible any more,
		// force a scroll to make them visible again
		if (fItemsHeight > fVisibleRect.Height())
			ScrollBy(0.0, fItemsHeight - fVisibleRect.Height() - Bounds().top);
		else
			ScrollBy(0.0, -Bounds().top);
	}

	if (parentRow != NULL) {

		for (int32 i = 0; i < countRows; i++) {
			BRow* row = static_cast<BRow*>(rows->ItemAt(i));
			parentRow->fChildList->RemoveItem(row);
		}

		if (parentRow->fChildList->CountItems() == 0) {
			delete parentRow->fChildList;
			parentRow->fChildList = 0;
			// It was the last child row of the parent, which also means the
			// latch disappears.
			BRect parentRowRect;
			if (parentIsVisible && FindRect(parentRow, &parentRowRect))
				Invalidate(parentRowRect);
		}
	} else {
		for (int32 i = 0; i < countRows; i++) {
			BRow* row = static_cast<BRow*>(rows->ItemAt(i));
			fRows.RemoveItem(row);
		}
	}

	if (fFocusRow) {
		if (fFocusRowRect.top < fVisibleRect.bottom)
			Invalidate(fFocusRowRect);

		// Adjust focus row if necessary.

		if (fFocusRow && !FindRect(fFocusRow, &fFocusRowRect)) {
			// focus row is in a subtree that is gone, move it up to the parent.
			fFocusRow = parentRow;

			if (fFocusRow)
				FindRect(fFocusRow, &fFocusRowRect);

			if (fFocusRowRect.top < fVisibleRect.bottom)
				Invalidate(fFocusRowRect);
		}
	}

	bool anyRowRemovedFromSelection = false;
	for (int32 i = 0; i < countRows; i++) {
		BRow* row = static_cast<BRow*>(rows->ItemAt(i));
		if (RemoveRowFromSelectionOnly(row))
			anyRowRemovedFromSelection = true;
	}

	if (anyRowRemovedFromSelection)
		fMasterView->SelectionChanged();

	fCurrentColumn = 0;
	fCurrentRow = 0;
	fCurrentField = 0;
}


/**
 * @brief Return the root-level row container.
 *
 * @return Pointer to the BRowContainer that holds all top-level rows.
 */
BRowContainer*
OutlineView::RowList()
{
	return &fRows;
}


/**
 * @brief Notify the outline view that the data in @p row has changed.
 *
 * If the row's position in the sort order has changed, the entire list is
 * re-sorted. Otherwise only the row's visible area is invalidated.
 *
 * @param row The row whose data has been updated.
 */
void
OutlineView::UpdateRow(BRow* row)
{
	if (row) {
		// Determine if this row has changed its sort order
		BRow* parentRow = NULL;
		bool parentIsVisible = false;
		FindParent(row, &parentRow, &parentIsVisible);

		BRowContainer* list = (parentRow == NULL) ? &fRows : parentRow->fChildList;

		if(list) {
			int32 rowIndex = list->IndexOf(row);
			ASSERT(rowIndex >= 0);
			ASSERT(list->ItemAt(rowIndex) == row);

			bool rowMoved = false;
			if (rowIndex > 0 && CompareRows(list->ItemAt(rowIndex - 1), row) > 0)
				rowMoved = true;

			if (rowIndex < list->CountItems() - 1 && CompareRows(list->ItemAt(rowIndex + 1),
				row) < 0)
				rowMoved = true;

			if (rowMoved) {
				// Sort location of this row has changed.
				// Remove and re-add in the right spot
				SortList(list, parentIsVisible && (parentRow == NULL || parentRow->fIsExpanded));
			} else if (parentIsVisible && (parentRow == NULL || parentRow->fIsExpanded)) {
				BRect invalidRect;
				if (FindVisibleRect(row, &invalidRect))
					Invalidate(invalidRect);
			}
		}
	}
}


/*!	This method will add the row to the supplied parent or to the root parent
	but will make no adjustments to the UI elements such as the scrollbar.
	Returns the index of the row at which the row was added.
*/
/**
 * @brief Insert @p row into its parent container without updating the UI.
 *
 * When sorting is enabled the row is inserted in sorted order and @p index
 * is ignored. When sorting is disabled, @p index determines the position (-1
 * appends to the end). Creates parentRow->fChildList if necessary.
 *
 * @param row       The row to insert.
 * @param index     The desired insertion position, or -1 to append.
 * @param parentRow The parent row, or NULL for the root list.
 * @return The index at which the row was actually inserted.
 */
int32
OutlineView::AddRowToParentOnly(BRow* row, int32 index, BRow* parentRow)
{
	row->fParent = parentRow;

	if (fMasterView->SortingEnabled() && !fSortColumns->IsEmpty()) {
		// Ignore index here.
		if (parentRow) {
			if (parentRow->fChildList == NULL)
				parentRow->fChildList = new BRowContainer;

			return AddSorted(parentRow->fChildList, row);
		}
		return AddSorted(&fRows, row);
	}

	// Note, a -1 index implies add to end if sorting is not enabled
	if (parentRow) {
		if (parentRow->fChildList == 0)
			parentRow->fChildList = new BRowContainer;

		int32 parentRowCount = parentRow->fChildList->CountItems();

		if (index < 0 || index > parentRowCount) {
			parentRow->fChildList->AddItem(row);
			return parentRowCount;
		}

		parentRow->fChildList->AddItem(row, index);
		return index;
	}

	int32 rowCount = fRows.CountItems();

	if (index < 0 || index >= rowCount) {
		fRows.AddItem(row);
		return rowCount;
	}

	fRows.AddItem(row, index);
	return index;
}


/**
 * @brief Add multiple rows to the outline view at once.
 *
 * Inserts all rows via AddRowToParentOnly(), updates fItemsHeight, fixes
 * the scroll bar, and batches invalidation for efficiency.
 *
 * @param addedRows List of BRow pointers to insert.
 * @param index     Insertion index for the first row, or -1 to append.
 * @param parentRow The parent row, or NULL for the root level.
 */
void
OutlineView::AddRows(BList* addedRows, int32 index, BRow* parentRow)
{
	if (addedRows->IsEmpty())
		return;

	bool parentRowEmptyOnEntry = true;

	if (parentRow)
		parentRowEmptyOnEntry = parentRow->fChildList->CountItems() == 0;

	float maxRowHeight = 0.0f;
	float sumRowHeight = 0.0f;
	int32 countAddedRows = addedRows->CountItems();
	int32 firstIndex = -1;
	BRow* firstRow = NULL;

	for (int32 i = 0; i < countAddedRows; i++) {
		BRow* row = static_cast<BRow*>(addedRows->ItemAt(i));
		int insertedIndex = AddRowToParentOnly(row, index, parentRow);

		if (insertedIndex >= 0) {
			if (firstIndex < 0 || insertedIndex <= firstIndex) {
				firstIndex = insertedIndex;
				firstRow = row;
			}

			float rowHeight = row->Height();

			sumRowHeight += rowHeight;

			if (rowHeight > maxRowHeight)
				maxRowHeight = rowHeight;
		}
	}

#ifdef DOUBLE_BUFFERED_COLUMN_RESIZE
	ResizeBufferView()->UpdateMaxHeight(maxRowHeight);
#endif

	// Now that the rows are loaded in, the metrics and other aspects of the
	// user interface need to be updated.

	if (parentRow == 0 || parentRow->fIsExpanded)
		fItemsHeight += (sumRowHeight + static_cast<float>(countAddedRows));
			// the height of the rows plus 1.0 for each row.

	FixScrollBar(false);

	BRect firstAddedRowRect;
	const bool firstAddedRowIsInOpenBranch = FindRect(firstRow, &firstAddedRowRect);

	// The assumption here is that if the first row is in an open branch then
	// the rest are as well since they have the same parent. The area that needs
	// redrawing is everything below this item.

	if (firstAddedRowIsInOpenBranch && firstAddedRowRect.top < fVisibleRect.bottom) {
		BRect invalidRect = firstAddedRowRect;
		invalidRect.bottom = fItemsHeight;
		Invalidate(invalidRect);
	}

	if (fFocusRow) {
		if (fFocusRowRect.top < fVisibleRect.bottom)
			Invalidate(fFocusRowRect);

		FindRect(fFocusRow, &fFocusRowRect);

		if (fFocusRowRect.top < fVisibleRect.bottom)
			Invalidate(fFocusRowRect);
	}

	// If the parent was previously childless, it will need to have a latch
	// drawn.

	if (parentRow && parentRowEmptyOnEntry) {
		BRect parentRect;
		if (FindVisibleRect(parentRow, &parentRect))
			Invalidate(parentRect);
	}
}


/**
 * @brief Add a single row to the outline view.
 *
 * Inserts the row, updates fItemsHeight, fixes the scroll bar, and performs
 * optimized partial-view updates using CopyBits where possible.
 *
 * @param row       The row to insert.
 * @param Index     The insertion index, or -1 to append.
 * @param parentRow The parent row, or NULL for the root level.
 */
void
OutlineView::AddRow(BRow* row, int32 Index, BRow* parentRow)
{
	if (!row)
		return;

	AddRowToParentOnly(row, Index, parentRow);

#ifdef DOUBLE_BUFFERED_COLUMN_RESIZE
	ResizeBufferView()->UpdateMaxHeight(row->Height());
#endif

	if (parentRow == 0 || parentRow->fIsExpanded)
		fItemsHeight += row->Height() + 1;

	FixScrollBar(false);

	BRect newRowRect;
	const bool newRowIsInOpenBranch = FindRect(row, &newRowRect);

	if (newRowIsInOpenBranch) {
		if (fFocusRow && fFocusRowRect.top > newRowRect.bottom) {
			// The focus row has moved.
			Invalidate(fFocusRowRect);
			FindRect(fFocusRow, &fFocusRowRect);
			Invalidate(fFocusRowRect);
		}

		if (fCurrentState == INACTIVE) {
			if (newRowRect.bottom < fVisibleRect.top) {
				// The new row is totally above the current viewport, move
				// everything down and redraw the first line.
				BRect source(fVisibleRect);
				BRect dest(fVisibleRect);
				source.bottom -= row->Height() + 1;
				dest.top += row->Height() + 1;
				CopyBits(source, dest);
				Invalidate(BRect(fVisibleRect.left, fVisibleRect.top, fVisibleRect.right,
					fVisibleRect.top + newRowRect.Height()));
			} else if (newRowRect.top < fVisibleRect.bottom) {
				// New item is somewhere in the current region.  Scroll everything
				// beneath it down and invalidate just the new row rect.
				BRect source(fVisibleRect.left, newRowRect.top, fVisibleRect.right,
					fVisibleRect.bottom - newRowRect.Height());
				BRect dest(source);
				dest.OffsetBy(0, newRowRect.Height() + 1);
				CopyBits(source, dest);
				Invalidate(newRowRect);
			} // otherwise, this is below the currently visible region
		} else {
			// Adding the item may have caused the item that the user is currently
			// selected to move.  This would cause annoying drawing and interaction
			// bugs, as the position of that item is cached.  If this happens, resize
			// the scroll bar, then scroll back so the selected item is in view.
			BRect targetRect;
			if (FindRect(fTargetRow, &targetRect)) {
				float delta = targetRect.top - fTargetRowTop;
				if (delta != 0) {
					// This causes a jump because ScrollBy will copy a chunk of the view.
					// Since the actual contents of the view have been offset, we don't
					// want this, we just want to change the virtual origin of the window.
					// Constrain the clipping region so everything is clipped out so no
					// copy occurs.
					//
					//	xxx this currently doesn't work if the scroll bars aren't enabled.
					//  everything will still move anyway.  A minor annoyance.
					BRegion emptyRegion;
					ConstrainClippingRegion(&emptyRegion);
					PushState();
					ScrollBy(0, delta);
					PopState();
					ConstrainClippingRegion(NULL);

					fTargetRowTop += delta;
					fClickPoint.y += delta;
					fLatchRect.OffsetBy(0, delta);
				}
			}
		}
	}

	// If the parent was previously childless, it will need to have a latch
	// drawn.
	BRect parentRect;
	if (parentRow && parentRow->fChildList->CountItems() == 1
		&& FindVisibleRect(parentRow, &parentRect))
		Invalidate(parentRect);
}


/**
 * @brief Update the vertical scroll bar range to match the current total row height.
 *
 * If the user has scrolled past the new maximum, the range update is deferred
 * until ScrollTo() detects the discrepancy and corrects it.
 *
 * @param scrollToFit If true, forces the range to be updated immediately.
 */
void
OutlineView::FixScrollBar(bool scrollToFit)
{
	BScrollBar* vScrollBar = ScrollBar(B_VERTICAL);
	if (vScrollBar) {
		if (fItemsHeight > fVisibleRect.Height()) {
			float maxScrollBarValue = fItemsHeight - fVisibleRect.Height();
			vScrollBar->SetProportion(fVisibleRect.Height() / fItemsHeight);

			// If the user is scrolled down too far when making the range smaller, the list
			// will jump suddenly, which is undesirable.  In this case, don't fix the scroll
			// bar here. In ScrollTo, it checks to see if this has occured, and will
			// fix the scroll bars sneakily if the user has scrolled up far enough.
			if (scrollToFit || vScrollBar->Value() <= maxScrollBarValue) {
				vScrollBar->SetRange(0.0, maxScrollBarValue);
				vScrollBar->SetSteps(20.0, fVisibleRect.Height());
			}
		} else if (vScrollBar->Value() == 0.0 || fItemsHeight == 0.0)
			vScrollBar->SetRange(0.0, 0.0);		// disable scroll bar.
	}
}


/*!	Returns the index at which the row was added. */
/**
 * @brief Insert @p row into @p list in sorted order using binary search.
 *
 * Uses a shell-sort-style binary search to find the correct insertion point,
 * then inserts the row. If the list is empty or the row belongs at the end,
 * the row is appended.
 *
 * @param list The sorted row container to insert into.
 * @param row  The row to insert.
 * @return The index at which @p row was inserted, or -1 if @p list or @p row is NULL.
 */
int32
OutlineView::AddSorted(BRowContainer* list, BRow* row)
{
	if (list && row) {
		// Find general vicinity with binary search.
		int32 lower = 0;
		int32 upper = list->CountItems()-1;
		while( lower < upper ) {
			int32 middle = lower + (upper-lower+1)/2;
			int32 cmp = CompareRows(row, list->ItemAt(middle));
			if( cmp < 0 ) upper = middle-1;
			else if( cmp > 0 ) lower = middle+1;
			else lower = upper = middle;
		}

		// At this point, 'upper' and 'lower' at the last found item.
		// Arbitrarily use 'upper' and determine the final insertion
		// point -- either before or after this item.
		if( upper < 0 ) upper = 0;
		else if( upper < list->CountItems() ) {
			if( CompareRows(row, list->ItemAt(upper)) > 0 ) upper++;
		}

		if (upper >= list->CountItems()) {
			list->AddItem(row);
				// Adding to end.
			return list->CountItems() - 1;
		}

		list->AddItem(row, upper);
			// Insert at specific location
		return upper;
	}

	return -1;
}


/**
 * @brief Compare two rows according to the current sort column list.
 *
 * Iterates over all active sort columns in order, calling BColumn::CompareFields()
 * on the corresponding field pair. The result is negated for descending sorts.
 *
 * @param row1 First row to compare.
 * @param row2 Second row to compare.
 * @return Negative if row1 < row2, 0 if equal, positive if row1 > row2.
 */
int32
OutlineView::CompareRows(BRow* row1, BRow* row2)
{
	int32 itemCount (fSortColumns->CountItems());
	if (row1 && row2) {
		for (int32 index = 0; index < itemCount; index++) {
			BColumn* column = (BColumn*) fSortColumns->ItemAt(index);
			int comp = 0;
			BField* field1 = (BField*) row1->GetField(column->fFieldID);
			BField* field2 = (BField*) row2->GetField(column->fFieldID);
			if (field1 && field2)
				comp = column->CompareFields(field1, field2);

			if (!column->fSortAscending)
				comp = -comp;

			if (comp != 0)
				return comp;
		}
	}
	return 0;
}


/**
 * @brief Called when the outline view is resized.
 *
 * Updates fVisibleRect and recalculates the vertical scroll bar range.
 *
 * @param width  New view width in pixels.
 * @param height New view height in pixels.
 */
void
OutlineView::FrameResized(float width, float height)
{
	fVisibleRect.right = fVisibleRect.left + width;
	fVisibleRect.bottom = fVisibleRect.top + height;
	FixScrollBar(true);
	_inherited::FrameResized(width, height);
}


/**
 * @brief Scroll the outline view to the given position.
 *
 * Updates fVisibleRect and, if the view was scrolled past the previous scroll
 * bar maximum, corrects the range via FixScrollBar().
 *
 * @param position The new scroll position (top-left of the visible area).
 */
void
OutlineView::ScrollTo(BPoint position)
{
	fVisibleRect.OffsetTo(position.x, position.y);

	// In FixScrollBar, we might not have been able to change the size of
	// the scroll bar because the user was scrolled down too far.  Take
	// this opportunity to sneak it in if we can.
	BScrollBar* vScrollBar = ScrollBar(B_VERTICAL);
	float maxScrollBarValue = fItemsHeight - fVisibleRect.Height();
	float min, max;
	vScrollBar->GetRange(&min, &max);
	if (max != maxScrollBarValue && position.y > maxScrollBarValue)
		FixScrollBar(true);

	_inherited::ScrollTo(position);
}


/**
 * @brief Return the currently visible rectangle in the outline view's coordinate system.
 *
 * @return Reference to fVisibleRect, which tracks the scrolled viewport.
 */
const BRect&
OutlineView::VisibleRect() const
{
	return fVisibleRect;
}


/**
 * @brief Find the visible-area bounding rectangle of @p row.
 *
 * Similar to FindRect() but returns false for rows whose top is below
 * fVisibleRect.bottom (i.e., rows that are scrolled out of view).
 *
 * @param row   The row to find.
 * @param _rect Output: the row's bounding rectangle in view coordinates.
 * @return true if the row was found and is within the visible area.
 */
bool
OutlineView::FindVisibleRect(BRow* row, BRect* _rect)
{
	if (row && _rect) {
		float line = 0.0;
		for (RecursiveOutlineIterator iterator(&fRows); iterator.CurrentRow();
			iterator.GoToNext()) {

			if (iterator.CurrentRow() == row) {
				_rect->Set(fVisibleRect.left, line, fVisibleRect.right,
					line + row->Height());
				return line <= fVisibleRect.bottom;
			}

			line += iterator.CurrentRow()->Height() + 1;
		}
	}
	return false;
}


/*!	This method will store the visible rectangle of the supplied `row` into
	`_rect` returning true if the row is currently visible.
*/
/**
 * @brief Find the bounding rectangle of @p row in the outline view's coordinate system.
 *
 * Iterates over the full row tree (including collapsed branches) using
 * a non-open-branches-only iterator.
 *
 * @param row   The row to find.
 * @param _rect Output: the row's bounding rectangle.
 * @return true if the row was found, false otherwise.
 */
bool
OutlineView::FindRect(const BRow* row, BRect* _rect)
{
	float line = 0.0;
	for (RecursiveOutlineIterator iterator(&fRows); iterator.CurrentRow();
		iterator.GoToNext()) {
		if (iterator.CurrentRow() == row) {
			_rect->Set(fVisibleRect.left, line, fVisibleRect.right,
				line + row->Height());
			return true;
		}

		line += iterator.CurrentRow()->Height() + 1;
	}

	return false;
}


/**
 * @brief Scroll the view so that @p row is visible.
 *
 * If the row's top is above the current viewport, scrolls up to show it.
 * If the row's bottom is below, scrolls down by the minimum amount needed.
 *
 * @param row The row to scroll into view.
 */
void
OutlineView::ScrollTo(const BRow* row)
{
	BRect rect;
	if (FindRect(row, &rect)) {
		BRect bounds = Bounds();
		if (rect.top < bounds.top)
			ScrollTo(BPoint(bounds.left, rect.top));
		else if (rect.bottom > bounds.bottom)
			ScrollBy(0, rect.bottom - bounds.bottom);
	}
}


/**
 * @brief Deselect all rows and invalidate their visible areas.
 *
 * Iterates the full row tree to find and invalidate visible selected rows,
 * then unlinks all rows from the selection intrusive list.
 */
void
OutlineView::DeselectAll()
{
	// Invalidate all selected rows
	float line = 0.0;
	for (RecursiveOutlineIterator iterator(&fRows); iterator.CurrentRow();
		iterator.GoToNext()) {
		if (line > fVisibleRect.bottom)
			break;

		BRow* row = iterator.CurrentRow();
		if (line + row->Height() > fVisibleRect.top) {
			if (row->fNextSelected != 0)
				Invalidate(BRect(fVisibleRect.left, line, fVisibleRect.right,
					line + row->Height()));
		}

		line += row->Height() + 1;
	}

	// Set items not selected
	while (fSelectionListDummyHead.fNextSelected != &fSelectionListDummyHead) {
		BRow* row = fSelectionListDummyHead.fNextSelected;
		row->fNextSelected->fPrevSelected = row->fPrevSelected;
		row->fPrevSelected->fNextSelected = row->fNextSelected;
		row->fNextSelected = 0;
		row->fPrevSelected = 0;
	}
}


/**
 * @brief Return the row that currently holds keyboard focus.
 *
 * @return The focused BRow, or NULL if no row has focus.
 */
BRow*
OutlineView::FocusRow() const
{
	return fFocusRow;
}


/**
 * @brief Set keyboard focus to @p row, optionally adding it to the selection.
 *
 * Invalidates the previous focus row rect and the new one to trigger redraws.
 *
 * @param row    The row to focus.
 * @param Select If true, the row is also added to the selection.
 */
void
OutlineView::SetFocusRow(BRow* row, bool Select)
{
	if (row) {
		if (Select)
			AddToSelection(row);

		if (fFocusRow == row)
			return;

		Invalidate(fFocusRowRect); // invalidate previous

		fTargetRow = fFocusRow = row;

		FindVisibleRect(fFocusRow, &fFocusRowRect);
		Invalidate(fFocusRowRect); // invalidate current

		fFocusRowRect.right = 10000;
		fMasterView->SelectionChanged();
	}
}


/**
 * @brief Sort @p list in-place using a shell sort.
 *
 * If @p isVisible is true, the view is invalidated after the sort and the
 * window is briefly unlocked to allow the display to refresh. Returns false
 * if the window was destroyed during the unlock.
 *
 * @param list      The row container to sort.
 * @param isVisible true if the list is currently visible and should be redrawn.
 * @return true if sorting completed successfully, false if the window was destroyed.
 */
bool
OutlineView::SortList(BRowContainer* list, bool isVisible)
{
	if (list) {
		// Shellsort
		BRow** items
			= (BRow**) BObjectList<BRow>::Private(list).AsBList()->Items();
		int32 numItems = list->CountItems();
		int h;
		for (h = 1; h < numItems / 9; h = 3 * h + 1)
			;

		for (;h > 0; h /= 3) {
			for (int step = h; step < numItems; step++) {
				BRow* temp = items[step];
				int i;
				for (i = step - h; i >= 0; i -= h) {
					if (CompareRows(temp, items[i]) < 0)
						items[i + h] = items[i];
					else
						break;
				}

				items[i + h] = temp;
			}
		}

		if (isVisible) {
			Invalidate();

			InvalidateCachedPositions();
			int lockCount = Window()->CountLocks();
			for (int i = 0; i < lockCount; i++)
				Window()->Unlock();

			while (lockCount--)
				if (!Window()->Lock())
					return false;	// Window is gone...
		}
	}
	return true;
}


/**
 * @brief Thread entry point that calls DeepSort() on the OutlineView.
 *
 * @param _outlineView Pointer to the OutlineView instance cast as void*.
 * @return Always 0.
 */
int32
OutlineView::DeepSortThreadEntry(void* _outlineView)
{
	((OutlineView*) _outlineView)->DeepSort();
	return 0;
}


/**
 * @brief Recursively sort the entire row tree on a background thread.
 *
 * Uses an explicit stack to perform a depth-first traversal, calling SortList()
 * on each container. Respects fSortCancelled so that a new sort can preempt a
 * running one. Unlocks the window during SortList() calls to allow redraws.
 */
void
OutlineView::DeepSort()
{
	struct stack_entry {
		bool isVisible;
		BRowContainer* list;
		int32 listIndex;
	} stack[kMaxDepth];
	int32 stackTop = 0;

	stack[stackTop].list = &fRows;
	stack[stackTop].isVisible = true;
	stack[stackTop].listIndex = 0;
	fNumSorted = 0;

	if (Window()->Lock() == false)
		return;

	bool doneSorting = false;
	while (!doneSorting && !fSortCancelled) {

		stack_entry* currentEntry = &stack[stackTop];

		// xxx Can make the invalidate area smaller by finding the rect for the
		// parent item and using that as the top of the invalid rect.

		bool haveLock = SortList(currentEntry->list, currentEntry->isVisible);
		if (!haveLock)
			return ;	// window is gone.

		// Fix focus rect.
		InvalidateCachedPositions();
		if (fCurrentState != INACTIVE)
			fCurrentState = INACTIVE;	// sorry...

		// next list.
		bool foundNextList = false;
		while (!foundNextList && !fSortCancelled) {
			for (int32 index = currentEntry->listIndex; index < currentEntry->list->CountItems();
				index++) {
				BRow* parentRow = currentEntry->list->ItemAt(index);
				BRowContainer* childList = parentRow->fChildList;
				if (childList != 0) {
					currentEntry->listIndex = index + 1;
					stackTop++;
					ASSERT(stackTop < kMaxDepth);
					stack[stackTop].listIndex = 0;
					stack[stackTop].list = childList;
					stack[stackTop].isVisible = (currentEntry->isVisible && parentRow->fIsExpanded);
					foundNextList = true;
					break;
				}
			}

			if (!foundNextList) {
				// back up
				if (--stackTop < 0) {
					doneSorting = true;
					break;
				}

				currentEntry = &stack[stackTop];
			}
		}
	}

	Window()->Unlock();
}


/**
 * @brief Launch a background thread to sort the entire row tree.
 *
 * If a previous sort thread is still running, it is cancelled and waited for
 * before spawning a new one. Has no effect if the view is not attached to a window.
 */
void
OutlineView::StartSorting()
{
	// If this view is not yet attached to a window, don't start a sort thread!
	if (Window() == NULL)
		return;

	if (fSortThread != B_BAD_THREAD_ID) {
		thread_info tinfo;
		if (get_thread_info(fSortThread, &tinfo) == B_OK) {
			// Unlock window so this won't deadlock (sort thread is probably
			// waiting to lock window).

			int lockCount = Window()->CountLocks();
			for (int i = 0; i < lockCount; i++)
				Window()->Unlock();

			fSortCancelled = true;
			int32 status;
			wait_for_thread(fSortThread, &status);

			while (lockCount--)
				if (!Window()->Lock())
					return ;	// Window is gone...
		}
	}

	fSortCancelled = false;
	fSortThread = spawn_thread(DeepSortThreadEntry, "sort_thread", B_NORMAL_PRIORITY, this);
	resume_thread(fSortThread);
}


/**
 * @brief Select all rows between @p start and @p end in display order.
 *
 * Traverses the full row tree (including collapsed branches) to find the
 * range. If @p end appears before @p start in the traversal order, the
 * parameters are swapped before selection begins.
 *
 * @param start The first boundary row of the range (already selected on entry).
 * @param end   The last boundary row of the range.
 */
void
OutlineView::SelectRange(BRow* start, BRow* end)
{
	if (!start || !end)
		return;

	if (start == end)	// start is always selected when this is called
		return;

	RecursiveOutlineIterator iterator(&fRows, false);
	while (iterator.CurrentRow() != 0) {
		if (iterator.CurrentRow() == end) {
			// reverse selection, swap to fix special case
			BRow* temp = start;
			start = end;
			end = temp;
			break;
		} else if (iterator.CurrentRow() == start)
			break;

		iterator.GoToNext();
	}

	while (true) {
		BRow* row = iterator.CurrentRow();
		if (row) {
			if (row->fNextSelected == 0) {
				row->fNextSelected = fSelectionListDummyHead.fNextSelected;
				row->fPrevSelected = &fSelectionListDummyHead;
				row->fNextSelected->fPrevSelected = row;
				row->fPrevSelected->fNextSelected = row;
			}
		} else
			break;

		if (row == end)
			break;

		iterator.GoToNext();
	}

	Invalidate();  // xxx make invalidation smaller
}


/**
 * @brief Find the parent row of @p row and determine its visibility.
 *
 * Walks the parent chain to determine if all ancestors are expanded.
 *
 * @param row              The row whose parent to find.
 * @param outParent        Output: set to the parent BRow, or NULL for root rows.
 * @param outParentIsVisible Output: set to true if the parent chain is fully expanded.
 * @return true if @p row has a parent, false if it is a root row.
 */
bool
OutlineView::FindParent(BRow* row, BRow** outParent, bool* outParentIsVisible)
{
	bool result = false;
	if (row != NULL && outParent != NULL) {
		*outParent = row->fParent;

		if (outParentIsVisible != NULL) {
			// Walk up the parent chain to determine if this row is visible
			*outParentIsVisible = true;
			for (BRow* currentRow = row->fParent; currentRow != NULL;
				currentRow = currentRow->fParent) {
				if (!currentRow->fIsExpanded) {
					*outParentIsVisible = false;
					break;
				}
			}
		}

		result = *outParent != NULL;
	}

	return result;
}


/**
 * @brief Return the index of @p row within its container (parent's child list or root).
 *
 * @param row The row to locate.
 * @return The zero-based index, or B_ERROR if @p row is NULL.
 */
int32
OutlineView::IndexOf(BRow* row)
{
	if (row) {
		if (row->fParent == 0)
			return fRows.IndexOf(row);

		ASSERT(row->fParent->fChildList);
		return row->fParent->fChildList->IndexOf(row);
	}

	return B_ERROR;
}


/**
 * @brief Refresh the cached focus-row rectangle after a sort or structural change.
 *
 * Calls FindRect() to recompute fFocusRowRect from the current row positions.
 */
void
OutlineView::InvalidateCachedPositions()
{
	if (fFocusRow)
		FindRect(fFocusRow, &fFocusRowRect);
}


/**
 * @brief Calculate the preferred width of @p column given the current row data.
 *
 * Iterates over all rows, queries BColumn::GetPreferredWidth() for each field,
 * accounts for indent, and also measures the column name. The result is clamped
 * to [MinWidth(), MaxWidth()].
 *
 * @param column The column to measure.
 * @return The preferred width in pixels.
 */
float
OutlineView::GetColumnPreferredWidth(BColumn* column)
{
	float preferred = 0.0;
	for (RecursiveOutlineIterator iterator(&fRows); BRow* row =
		iterator.CurrentRow(); iterator.GoToNext()) {
		BField* field = row->GetField(column->fFieldID);
		if (field) {
			float width = column->GetPreferredWidth(field, this)
				+ iterator.CurrentLevel() * kOutlineLevelIndent;
			preferred = max_c(preferred, width);
		}
	}

	BString name;
	column->GetColumnName(&name);
	preferred = max_c(preferred, StringWidth(name));

	// Constrain to preferred width. This makes the method do a little
	// more than asked, but it's for convenience.
	if (preferred < column->MinWidth())
		preferred = column->MinWidth();
	else if (preferred > column->MaxWidth())
		preferred = column->MaxWidth();

	return preferred;
}


// #pragma mark -


/**
 * @brief Construct an iterator that traverses a row tree depth-first.
 *
 * @param list             The root BRowContainer to iterate.
 * @param openBranchesOnly If true (the default), collapsed sub-trees are skipped.
 *                         Pass false to iterate the entire tree regardless of
 *                         expand/collapse state.
 */
RecursiveOutlineIterator::RecursiveOutlineIterator(BRowContainer* list,
	bool openBranchesOnly)
	:
	fStackIndex(0),
	fCurrentListIndex(0),
	fCurrentListDepth(0),
	fOpenBranchesOnly(openBranchesOnly)
{
	if (list == 0 || list->CountItems() == 0)
		fCurrentList = 0;
	else
		fCurrentList = list;
}


/**
 * @brief Return the row at the current iterator position.
 *
 * @return The current BRow, or NULL if the iterator is exhausted.
 */
BRow*
RecursiveOutlineIterator::CurrentRow() const
{
	if (fCurrentList == 0)
		return 0;

	return fCurrentList->ItemAt(fCurrentListIndex);
}


/**
 * @brief Advance the iterator to the next row in depth-first order.
 *
 * If the current row has an expanded (or all, when not open-branches-only)
 * child list, the iterator descends into it. Otherwise it advances to the next
 * sibling, popping the stack when a list is exhausted.
 */
void
RecursiveOutlineIterator::GoToNext()
{
	if (fCurrentList == 0)
		return;
	if (fCurrentListIndex < 0 || fCurrentListIndex >= fCurrentList->CountItems()) {
		fCurrentList = 0;
		return;
	}

	BRow* currentRow = fCurrentList->ItemAt(fCurrentListIndex);
	if(currentRow) {
		if (currentRow->fChildList && (currentRow->fIsExpanded || !fOpenBranchesOnly)
			&& currentRow->fChildList->CountItems() > 0) {
			// Visit child.
			// Put current list on the stack if it needs to be revisited.
			if (fCurrentListIndex < fCurrentList->CountItems() - 1) {
				fStack[fStackIndex].fRowSet = fCurrentList;
				fStack[fStackIndex].fIndex = fCurrentListIndex + 1;
				fStack[fStackIndex].fDepth = fCurrentListDepth;
				fStackIndex++;
			}

			fCurrentList = currentRow->fChildList;
			fCurrentListIndex = 0;
			fCurrentListDepth++;
		} else if (fCurrentListIndex < fCurrentList->CountItems() - 1)
			fCurrentListIndex++; // next item in current list
		else if (--fStackIndex >= 0) {
			fCurrentList = fStack[fStackIndex].fRowSet;
			fCurrentListIndex = fStack[fStackIndex].fIndex;
			fCurrentListDepth = fStack[fStackIndex].fDepth;
		} else
			fCurrentList = 0;
	}
}


/**
 * @brief Return the nesting depth of the current row.
 *
 * @return 0 for root-level rows, 1 for their children, and so on.
 */
int32
RecursiveOutlineIterator::CurrentLevel() const
{
	return fCurrentListDepth;
}


