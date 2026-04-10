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
 *   Copyright 2001-2012, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers (mflerackers@androme.be)
 *       Rene Gollent (rene@gollent.com)
 *       Alexandre Deckner (alex@zappotek.com)
 */


/**
 * @file Dragger.cpp
 * @brief Implementation of BDragger, the replicant drag handle view
 *
 * BDragger is a small view (usually shown in a corner of its parent) that allows
 * the user to drag the parent view and drop it onto a BShelf to create a replicant.
 * It manages the drag initiation and drop protocol.
 *
 * @see BShelf, BView
 */


#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include <Alert.h>
#include <Beep.h>
#include <Bitmap.h>
#include <Dragger.h>
#include <MenuItem.h>
#include <Message.h>
#include <PopUpMenu.h>
#include <Shelf.h>
#include <SystemCatalog.h>
#include <Window.h>

#include <AutoLocker.h>

#include <AppServerLink.h>
#include <DragTrackingFilter.h>
#include <binary_compatibility/Interface.h>
#include <ServerProtocol.h>
#include <ViewPrivate.h>

#include "ZombieReplicantView.h"

using BPrivate::gSystemCatalog;

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Dragger"

#undef B_TRANSLATE
#define B_TRANSLATE(str) \
	gSystemCatalog.GetString(B_TRANSLATE_MARK(str), "Dragger")


/** @brief Internal message code sent by DragTrackingFilter when a drag gesture begins. */
static const uint32 kMsgDragStarted = 'Drgs';

/** @brief Raw 8×8 pixel bitmap data (B_CMAP8) for the drag-handle icon. */
static const unsigned char kHandBitmap[] = {
	255, 255,   0,   0,   0, 255, 255, 255,
	255, 255,   0, 131, 131,   0, 255, 255,
	  0,   0,   0,   0, 131, 131,   0,   0,
	  0, 131,   0,   0, 131, 131,   0,   0,
	  0, 131, 131, 131, 131, 131,   0,   0,
	255,   0, 131, 131, 131, 131,   0,   0,
	255, 255,   0,   0,   0,   0,   0,   0,
	255, 255, 255, 255, 255, 255,   0,   0
};


namespace {

/**
 * @brief Process-wide singleton that tracks all BDragger instances and their
 *        shared visibility state.
 *
 * DraggerManager maintains a locked list of every BDragger that is currently
 * attached to a window so that ShowAllDraggers() / HideAllDraggers() can
 * broadcast a visibility change to all of them at once.  The singleton is
 * initialised lazily via pthread_once.
 */
struct DraggerManager {
	/** @brief Whether draggers are currently set to be drawn. */
	bool	visible;
	/** @brief Whether @a visible has been initialised from the app server. */
	bool	visibleInitialized;
	/** @brief List of all currently attached BDragger instances (unowned). */
	BList	list;

	DraggerManager()
		:
		visible(false),
		visibleInitialized(false),
		fLock("BDragger static")
	{
	}

	/** @brief Acquires the internal BLocker; returns @c true on success. */
	bool Lock()
	{
		return fLock.Lock();
	}

	/** @brief Releases the internal BLocker. */
	void Unlock()
	{
		fLock.Unlock();
	}

	/**
	 * @brief Returns the process-wide DraggerManager singleton.
	 *
	 * Initialises the singleton on first call using pthread_once so that
	 * the operation is thread-safe without requiring a pre-existing lock.
	 *
	 * @return Pointer to the singleton DraggerManager instance.
	 */
	static DraggerManager* Default()
	{
		if (sDefaultInstance == NULL)
			pthread_once(&sDefaultInitOnce, &_InitSingleton);

		return sDefaultInstance;
	}

private:
	/** @brief pthread_once helper that allocates the singleton on the heap. */
	static void _InitSingleton()
	{
		sDefaultInstance = new DraggerManager;
	}

private:
	BLocker					fLock;

	static pthread_once_t	sDefaultInitOnce;
	static DraggerManager*	sDefaultInstance;
};

pthread_once_t DraggerManager::sDefaultInitOnce = PTHREAD_ONCE_INIT;
DraggerManager* DraggerManager::sDefaultInstance = NULL;

}	// unnamed namespace


/**
 * @brief Constructs a BDragger with an explicit frame rectangle.
 *
 * Creates the dragger with the legacy frame-based BView constructor.
 * @a target specifies the view that will be dragged; the relationship
 * between the dragger and the target is determined later by
 * _DetermineRelationship().
 *
 * @param frame        The frame rectangle of the dragger in its parent's
 *                     coordinate system.
 * @param target       The view to be dragged, or @c NULL.
 * @param resizingMode BView resizing mode flags (e.g. B_FOLLOW_RIGHT).
 * @param flags        BView creation flags.
 * @see BDragger(BView*, uint32)
 */
BDragger::BDragger(BRect frame, BView* target, uint32 resizingMode,
	uint32 flags)
	:
	BView(frame, "_dragger_", resizingMode, flags),
	fTarget(target),
	fRelation(TARGET_UNKNOWN),
	fShelf(NULL),
	fTransition(false),
	fIsZombie(false),
	fErrCount(0),
	fPopUpIsCustom(false),
	fPopUp(NULL)
{
	_InitData();
}


/**
 * @brief Constructs a BDragger without an explicit frame rectangle.
 *
 * Uses the layout-aware BView constructor; size is determined by the
 * layout engine at attachment time.
 *
 * @param target  The view to be dragged, or @c NULL.
 * @param flags   BView creation flags.
 * @see BDragger(BRect, BView*, uint32, uint32)
 */
BDragger::BDragger(BView* target, uint32 flags)
	:
	BView("_dragger_", flags),
	fTarget(target),
	fRelation(TARGET_UNKNOWN),
	fShelf(NULL),
	fTransition(false),
	fIsZombie(false),
	fErrCount(0),
	fPopUpIsCustom(false),
	fPopUp(NULL)
{
	_InitData();
}


/**
 * @brief Archive-reconstruction constructor used by Instantiate().
 *
 * Restores a BDragger from a flattened BMessage archive.  The target
 * view relationship and optional custom pop-up menu are restored from
 * the archive fields "_rel" and "_popup" respectively.
 *
 * @param data  The BMessage archive produced by Archive().
 * @see Archive(), Instantiate()
 */
BDragger::BDragger(BMessage* data)
	:
	BView(data),
	fTarget(NULL),
	fRelation(TARGET_UNKNOWN),
	fShelf(NULL),
	fTransition(false),
	fIsZombie(false),
	fErrCount(0),
	fPopUpIsCustom(false),
	fPopUp(NULL)
{
	data->FindInt32("_rel", (int32*)&fRelation);

	_InitData();

	BMessage popupMsg;
	if (data->FindMessage("_popup", &popupMsg) == B_OK) {
		BArchivable* archivable = instantiate_object(&popupMsg);

		if (archivable) {
			fPopUp = dynamic_cast<BPopUpMenu*>(archivable);
			fPopUpIsCustom = true;
		}
	}
}


/**
 * @brief Destroys the BDragger and frees its pop-up menu and bitmap resources.
 */
BDragger::~BDragger()
{
	delete fPopUp;
	delete fBitmap;
}


/**
 * @brief Creates a BDragger from a BMessage archive (BArchivable hook).
 *
 * Validates the archive class name and constructs a new BDragger via the
 * archive constructor.
 *
 * @param data  The archive message to instantiate from.
 * @return A newly allocated BDragger, or @c NULL if validation fails.
 * @see Archive()
 */
BArchivable	*
BDragger::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BDragger"))
		return new BDragger(data);
	return NULL;
}


/**
 * @brief Flattens the BDragger into a BMessage for archiving.
 *
 * Calls the base BView::Archive() and then adds the target relationship
 * ("_rel") and, if a custom pop-up menu is set, the archived pop-up
 * menu ("_popup").
 *
 * @param data   The message to store archive data into.
 * @param deep   If @c true, child views are archived recursively.
 * @return B_OK on success, or the first error encountered.
 * @retval B_OK On success.
 * @see Instantiate()
 */
status_t
BDragger::Archive(BMessage* data, bool deep) const
{
	status_t ret = BView::Archive(data, deep);
	if (ret != B_OK)
		return ret;

	BMessage popupMsg;

	if (fPopUp != NULL && fPopUpIsCustom) {
		bool windowLocked = fPopUp->Window()->Lock();

		ret = fPopUp->Archive(&popupMsg, deep);

		if (windowLocked) {
			fPopUp->Window()->Unlock();
				// TODO: Investigate, in some (rare) occasions the menu window
				//		 has already been unlocked
		}

		if (ret == B_OK)
			ret = data->AddMessage("_popup", &popupMsg);
	}

	if (ret == B_OK)
		ret = data->AddInt32("_rel", fRelation);
	return ret;
}


/**
 * @brief Called when the dragger is added to a window hierarchy.
 *
 * Configures the background transparency, resolves the target relationship,
 * registers with the DraggerManager list, and installs the drag-tracking
 * message filter.  Zombie draggers are rendered with a special color.
 *
 * @see DetachedFromWindow(), _DetermineRelationship(), _AddToList()
 */
void
BDragger::AttachedToWindow()
{
	if (fIsZombie) {
		SetLowColor(kZombieColor);
		SetViewColor(kZombieColor);
	} else {
		SetFlags(Flags() | B_TRANSPARENT_BACKGROUND);
		SetLowColor(B_TRANSPARENT_COLOR);
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	_DetermineRelationship();
	_AddToList();

	AddFilter(new DragTrackingFilter(this, kMsgDragStarted));
}


/**
 * @brief Called when the dragger is removed from the window hierarchy.
 *
 * Unregisters the dragger from the DraggerManager list so that global
 * show/hide broadcasts no longer target it.
 *
 * @see AttachedToWindow(), _RemoveFromList()
 */
void
BDragger::DetachedFromWindow()
{
	_RemoveFromList();
}


/**
 * @brief Draws the drag-handle bitmap in the lower-right corner of the view.
 *
 * The bitmap is only drawn when draggers are globally visible and the
 * shelf (if any) permits dragging.  Zombie draggers use the same drawing
 * path but may in the future render differently.
 *
 * @param update  The dirty rectangle that needs to be redrawn.
 * @see AreDraggersDrawn(), BShelf::AllowsDragging()
 */
void
BDragger::Draw(BRect update)
{
	BRect bounds(Bounds());

	if (AreDraggersDrawn() && (fShelf == NULL || fShelf->AllowsDragging())) {
		BPoint where = bounds.RightBottom() - BPoint(fBitmap->Bounds().Width(),
			fBitmap->Bounds().Height());
		SetDrawingMode(B_OP_OVER);
		DrawBitmap(fBitmap, where);
		SetDrawingMode(B_OP_COPY);

		if (fIsZombie) {
			// TODO: should draw it differently ?
		}
	}
}


/**
 * @brief Handles a mouse-button-down event on the dragger.
 *
 * If the secondary (right) mouse button is pressed while the dragger is
 * on a shelf, the replicant context pop-up menu is displayed.  Primary
 * button presses are handled by the DragTrackingFilter which posts
 * kMsgDragStarted when the pointer moves far enough.
 *
 * @param where  The mouse position in the dragger's local coordinates.
 * @see _ShowPopUp(), MessageReceived()
 */
void
BDragger::MouseDown(BPoint where)
{
	if (fTarget == NULL || !AreDraggersDrawn())
		return;

	uint32 buttons;
	Window()->CurrentMessage()->FindInt32("buttons", (int32*)&buttons);

	if (fShelf != NULL && (buttons & B_SECONDARY_MOUSE_BUTTON) != 0)
		_ShowPopUp(fTarget, where);
}


/**
 * @brief Handles a mouse-button-up event; delegates to the base class.
 *
 * @param point  The mouse position in the dragger's local coordinates.
 */
void
BDragger::MouseUp(BPoint point)
{
	BView::MouseUp(point);
}


/**
 * @brief Handles mouse-moved events; delegates to the base class.
 *
 * @param point  The current mouse position in the dragger's local coordinates.
 * @param code   B_ENTERED_VIEW, B_INSIDE_VIEW, or B_EXITED_VIEW.
 * @param msg    Optional drag message if a drag is in progress.
 */
void
BDragger::MouseMoved(BPoint point, uint32 code, const BMessage* msg)
{
	BView::MouseMoved(point, code, msg);
}


/**
 * @brief Dispatches messages directed at the dragger.
 *
 * Handles three internal messages:
 * - B_TRASH_TARGET: the user dropped the replicant on the Trash; posts a
 *   delete request to the shelf, or shows an explanatory alert if unshelved.
 * - _SHOW_DRAG_HANDLES_: the global dragger-visibility setting changed;
 *   shows or hides this dragger accordingly.
 * - kMsgDragStarted: the drag-tracking filter reports that a drag gesture
 *   has begun; archives the target and initiates DragMessage().
 *
 * All other messages are forwarded to BView::MessageReceived().
 *
 * @param msg  The message to handle.
 * @see AttachedToWindow(), MouseDown()
 */
void
BDragger::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case B_TRASH_TARGET:
			if (fShelf != NULL)
				Window()->PostMessage(kDeleteReplicant, fTarget, NULL);
			else {
				BAlert* alert = new BAlert(B_TRANSLATE("Warning"),
					B_TRANSLATE("Can't delete this replicant from its original "
					"application. Life goes on."),
					B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_FROM_WIDEST,
					B_WARNING_ALERT);
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go(NULL);
			}
			break;

		case _SHOW_DRAG_HANDLES_:
			// This code is used whenever the "are draggers drawn" option is
			// changed.
			if (fRelation == TARGET_IS_CHILD) {
				Invalidate(Bounds());
			} else {
				if ((fShelf != NULL && fShelf->AllowsDragging()
						&& AreDraggersDrawn())
					|| AreDraggersDrawn()) {
					Show();
				} else
					Hide();
			}
			break;

		case kMsgDragStarted:
			if (fTarget != NULL) {
				BMessage archive(B_ARCHIVED_OBJECT);

				if (fRelation == TARGET_IS_PARENT)
					fTarget->Archive(&archive);
				else if (fRelation == TARGET_IS_CHILD)
					Archive(&archive);
				else if (fTarget->Archive(&archive)) {
					BMessage archivedSelf(B_ARCHIVED_OBJECT);

					if (Archive(&archivedSelf))
						archive.AddMessage("__widget", &archivedSelf);
				}

				archive.AddInt32("be:actions", B_TRASH_TARGET);
				BPoint offset;
				drawing_mode mode;
				BBitmap* bitmap = DragBitmap(&offset, &mode);
				if (bitmap != NULL)
					DragMessage(&archive, bitmap, mode, offset, this);
				else {
					DragMessage(&archive, ConvertFromScreen(
						fTarget->ConvertToScreen(fTarget->Bounds())), this);
				}
			}
			break;

		default:
			BView::MessageReceived(msg);
			break;
	}
}


/** @brief Forwards a FrameMoved notification to the base BView. */
void
BDragger::FrameMoved(BPoint newPosition)
{
	BView::FrameMoved(newPosition);
}


/** @brief Forwards a FrameResized notification to the base BView. */
void
BDragger::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);
}


/**
 * @brief Makes all BDragger instances in the process visible.
 *
 * Sends AS_SET_SHOW_ALL_DRAGGERS(true) to the app server and updates the
 * DraggerManager cache so that subsequently attached draggers also become
 * visible without an extra round-trip.
 *
 * @return B_OK on success, or an app-server communication error code.
 * @retval B_OK On success.
 * @see HideAllDraggers(), AreDraggersDrawn()
 */
status_t
BDragger::ShowAllDraggers()
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_SET_SHOW_ALL_DRAGGERS);
	link.Attach<bool>(true);

	status_t status = link.Flush();
	if (status == B_OK) {
		DraggerManager* manager = DraggerManager::Default();
		AutoLocker<DraggerManager> locker(manager);
		manager->visible = true;
		manager->visibleInitialized = true;
	}

	return status;
}


/**
 * @brief Makes all BDragger instances in the process invisible.
 *
 * Sends AS_SET_SHOW_ALL_DRAGGERS(false) to the app server and updates the
 * DraggerManager cache.
 *
 * @return B_OK on success, or an app-server communication error code.
 * @retval B_OK On success.
 * @see ShowAllDraggers(), AreDraggersDrawn()
 */
status_t
BDragger::HideAllDraggers()
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_SET_SHOW_ALL_DRAGGERS);
	link.Attach<bool>(false);

	status_t status = link.Flush();
	if (status == B_OK) {
		DraggerManager* manager = DraggerManager::Default();
		AutoLocker<DraggerManager> locker(manager);
		manager->visible = false;
		manager->visibleInitialized = true;
	}

	return status;
}


/**
 * @brief Returns whether dragger views are currently set to be drawn.
 *
 * Checks the DraggerManager's cached value; if not yet initialised,
 * queries the app server via AS_GET_SHOW_ALL_DRAGGERS and caches the result.
 *
 * @return @c true if draggers should be drawn, @c false otherwise.
 * @see ShowAllDraggers(), HideAllDraggers()
 */
bool
BDragger::AreDraggersDrawn()
{
	DraggerManager* manager = DraggerManager::Default();
	AutoLocker<DraggerManager> locker(manager);

	if (!manager->visibleInitialized) {
		BPrivate::AppServerLink link;
		link.StartMessage(AS_GET_SHOW_ALL_DRAGGERS);

		status_t status;
		if (link.FlushWithReply(status) == B_OK && status == B_OK) {
			link.Read<bool>(&manager->visible);
			manager->visibleInitialized = true;
		} else
			return false;
	}

	return manager->visible;
}


/** @brief Scripting hook; delegates to BView::ResolveSpecifier(). */
BHandler*
BDragger::ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier,
	int32 form, const char* property)
{
	return BView::ResolveSpecifier(message, index, specifier, form, property);
}


/** @brief Scripting hook; delegates to BView::GetSupportedSuites(). */
status_t
BDragger::GetSupportedSuites(BMessage* data)
{
	return BView::GetSupportedSuites(data);
}


/**
 * @brief Binary-compatibility perform hook; dispatches layout perform codes.
 *
 * Handles PERFORM_CODE_MIN_SIZE, PERFORM_CODE_MAX_SIZE,
 * PERFORM_CODE_PREFERRED_SIZE, PERFORM_CODE_LAYOUT_ALIGNMENT,
 * PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH, PERFORM_CODE_GET_HEIGHT_FOR_WIDTH,
 * PERFORM_CODE_SET_LAYOUT, PERFORM_CODE_LAYOUT_INVALIDATED, and
 * PERFORM_CODE_DO_LAYOUT.  All other codes are forwarded to BView::Perform().
 *
 * @param code   The perform operation code.
 * @param _data  Opaque pointer to the perform data structure.
 * @return B_OK on success, or B_NAME_NOT_FOUND for unknown codes.
 */
status_t
BDragger::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BDragger::MinSize();
			return B_OK;
		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BDragger::MaxSize();
			return B_OK;
		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BDragger::PreferredSize();
			return B_OK;
		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BDragger::LayoutAlignment();
			return B_OK;
		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BDragger::HasHeightForWidth();
			return B_OK;
		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BDragger::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
}
		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BDragger::SetLayout(data->layout);
			return B_OK;
		}
		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BDragger::LayoutInvalidated(data->descendants);
			return B_OK;
		}
		case PERFORM_CODE_DO_LAYOUT:
		{
			BDragger::DoLayout();
			return B_OK;
		}
	}

	return BView::Perform(code, _data);
}


/** @brief Delegates to BView::ResizeToPreferred(). */
void
BDragger::ResizeToPreferred()
{
	BView::ResizeToPreferred();
}


/** @brief Delegates to BView::GetPreferredSize(). */
void
BDragger::GetPreferredSize(float* _width, float* _height)
{
	BView::GetPreferredSize(_width, _height);
}


/** @brief Delegates to BView::MakeFocus(). */
void
BDragger::MakeFocus(bool state)
{
	BView::MakeFocus(state);
}


/** @brief Delegates to BView::AllAttached(). */
void
BDragger::AllAttached()
{
	BView::AllAttached();
}


/** @brief Delegates to BView::AllDetached(). */
void
BDragger::AllDetached()
{
	BView::AllDetached();
}


/**
 * @brief Replaces the dragger's pop-up menu with a custom one.
 *
 * Takes ownership of @a menu.  The previous custom menu (if any) is deleted.
 * Has no effect and returns B_ERROR if @a menu is @c NULL or is already the
 * current pop-up.
 *
 * @param menu  The new BPopUpMenu to use; must not be @c NULL and must differ
 *              from the existing pop-up.
 * @return B_OK on success, B_ERROR if @a menu was rejected.
 * @retval B_OK On success.
 * @see PopUp()
 */
status_t
BDragger::SetPopUp(BPopUpMenu* menu)
{
	if (menu != NULL && menu != fPopUp) {
		delete fPopUp;
		fPopUp = menu;
		fPopUpIsCustom = true;
		return B_OK;
	}
	return B_ERROR;
}


/**
 * @brief Returns the current pop-up menu, building the default one if needed.
 *
 * If no custom pop-up has been set and a target view exists, calls
 * _BuildDefaultPopUp() to create the standard "About / Remove replicant"
 * menu before returning it.
 *
 * @return The current BPopUpMenu, or @c NULL if there is no target view.
 * @see SetPopUp(), _BuildDefaultPopUp()
 */
BPopUpMenu*
BDragger::PopUp() const
{
	if (fPopUp == NULL && fTarget)
		const_cast<BDragger*>(this)->_BuildDefaultPopUp();

	return fPopUp;
}


/**
 * @brief Returns whether the dragger is currently hosted in a BShelf.
 *
 * @return @c true if the dragger has been placed on a shelf, @c false if it
 *         is still in its originating application window.
 */
bool
BDragger::InShelf() const
{
	return fShelf != NULL;
}


/**
 * @brief Returns the view that this dragger is associated with.
 *
 * @return Pointer to the target BView, or @c NULL if none has been set.
 * @see _SetViewToDrag()
 */
BView*
BDragger::Target() const
{
	return fTarget;
}


/**
 * @brief Returns a custom bitmap to use as the drag image, or @c NULL.
 *
 * The default implementation returns @c NULL, causing the system to use the
 * target view's bounds rectangle as the drag outline.  Subclasses may
 * override this to supply a translucent drag image.
 *
 * @param offset  Output: the bitmap's hot-spot offset relative to the cursor.
 * @param mode    Output: the drawing mode to use when rendering the bitmap.
 * @return @c NULL (base-class implementation always returns @c NULL).
 */
BBitmap*
BDragger::DragBitmap(BPoint* offset, drawing_mode* mode)
{
	return NULL;
}


/**
 * @brief Returns whether the dragger's visibility is in transition.
 *
 * @return @c true while a show/hide transition is in progress.
 */
bool
BDragger::IsVisibilityChanging() const
{
	return fTransition;
}


void BDragger::_ReservedDragger2() {}
void BDragger::_ReservedDragger3() {}
void BDragger::_ReservedDragger4() {}


/** @brief Unimplemented copy-assignment operator; returns @c *this unchanged. */
BDragger&
BDragger::operator=(const BDragger&)
{
	return *this;
}


/**
 * @brief Updates the DraggerManager cache and broadcasts a visibility change.
 *
 * Called by the app server (via BApplication) when the global dragger
 * visibility setting changes.  Updates the cached visibility flag and posts
 * _SHOW_DRAG_HANDLES_ to every registered BDragger so each can show or hide
 * itself accordingly.
 *
 * @param visible  @c true if draggers should now be drawn, @c false to hide.
 * @see ShowAllDraggers(), HideAllDraggers()
 */
/*static*/ void
BDragger::_UpdateShowAllDraggers(bool visible)
{
	DraggerManager* manager = DraggerManager::Default();
	AutoLocker<DraggerManager> locker(manager);

	manager->visibleInitialized = true;
	manager->visible = visible;

	for (int32 i = manager->list.CountItems(); i-- > 0;) {
		BDragger* dragger = (BDragger*)manager->list.ItemAt(i);
		BMessenger target(dragger);
		target.SendMessage(_SHOW_DRAG_HANDLES_);
	}
}


/**
 * @brief Initialises the drag-handle bitmap used by Draw().
 *
 * Allocates an 8×8 B_CMAP8 BBitmap and fills it with the kHandBitmap pixel
 * data.  Called from every constructor.
 *
 * @see Draw()
 */
void
BDragger::_InitData()
{
	fBitmap = new BBitmap(BRect(0.0f, 0.0f, 7.0f, 7.0f), B_CMAP8, false, false);
	fBitmap->SetBits(kHandBitmap, fBitmap->BitsLength(), 0, B_CMAP8);
}


/**
 * @brief Registers this dragger in the DraggerManager list.
 *
 * Also applies the current global visibility setting: if draggers are
 * hidden and this dragger is not the child of its target, it hides itself
 * immediately.
 *
 * @see _RemoveFromList(), AttachedToWindow()
 */
void
BDragger::_AddToList()
{
	DraggerManager* manager = DraggerManager::Default();
	AutoLocker<DraggerManager> locker(manager);
	manager->list.AddItem(this);

	bool allowsDragging = true;
	if (fShelf)
		allowsDragging = fShelf->AllowsDragging();

	if (!AreDraggersDrawn() || !allowsDragging) {
		// The dragger is not shown - but we can't hide us in case we're the
		// parent of the actual target view (because then you couldn't see
		// it anymore).
		if (fRelation != TARGET_IS_CHILD && !IsHidden(this))
			Hide();
	}
}


/**
 * @brief Removes this dragger from the DraggerManager list.
 *
 * Called from DetachedFromWindow() so that global broadcasts no longer
 * target this dragger after it has been removed from its window.
 *
 * @see _AddToList(), DetachedFromWindow()
 */
void
BDragger::_RemoveFromList()
{
	DraggerManager* manager = DraggerManager::Default();
	AutoLocker<DraggerManager> locker(manager);
	manager->list.RemoveItem(this);
}


/**
 * @brief Determines and caches the geometric relationship between the dragger
 *        and its target view.
 *
 * If @a fTarget is set, the relationship is detected by comparing the target
 * with this view's parent and first child.  If @a fTarget is @c NULL it is
 * resolved from the cached @a fRelation value.  When the dragger is the
 * parent of its target, it is repositioned to remain within the parent bounds.
 *
 * @return B_OK on success, B_ERROR if the relationship cannot be determined.
 * @retval B_OK On success.
 */
status_t
BDragger::_DetermineRelationship()
{
	if (fTarget != NULL) {
		if (fTarget == Parent())
			fRelation = TARGET_IS_PARENT;
		else if (fTarget == ChildAt(0))
			fRelation = TARGET_IS_CHILD;
		else
			fRelation = TARGET_IS_SIBLING;
	} else {
		if (fRelation == TARGET_IS_PARENT)
			fTarget = Parent();
		else if (fRelation == TARGET_IS_CHILD)
			fTarget = ChildAt(0);
		else
			return B_ERROR;
	}

	if (fRelation == TARGET_IS_PARENT) {
		BRect bounds(Frame());
		BRect parentBounds(Parent()->Bounds());
		if (!parentBounds.Contains(bounds)) {
			MoveTo(parentBounds.right - bounds.Width(),
				parentBounds.bottom - bounds.Height());
		}
	}

	return B_OK;
}


/**
 * @brief Sets the view to be dragged and refreshes the relationship cache.
 *
 * @a target must belong to the same window as the dragger.
 *
 * @param target  The new target view.
 * @return B_OK on success, B_ERROR if @a target is in a different window.
 * @retval B_OK On success.
 * @retval B_ERROR If @a target belongs to a different window.
 * @see _DetermineRelationship()
 */
status_t
BDragger::_SetViewToDrag(BView* target)
{
	if (target->Window() != Window())
		return B_ERROR;

	fTarget = target;

	if (Window() != NULL)
		_DetermineRelationship();

	return B_OK;
}


/**
 * @brief Records the BShelf that is hosting this dragger.
 *
 * Called by BShelf internals when the dragger's replicant is placed on
 * or removed from a shelf.
 *
 * @param shelf  The shelf hosting this dragger, or @c NULL.
 * @see InShelf()
 */
void
BDragger::_SetShelf(BShelf* shelf)
{
	fShelf = shelf;
}


/**
 * @brief Marks the dragger as a zombie and updates its visual appearance.
 *
 * A zombie dragger is one whose replicant view could not be instantiated.
 * When @a state is @c true the background and low colors are set to the
 * zombie indicator color.
 *
 * @param state  @c true to enter zombie mode, @c false to clear it.
 */
void
BDragger::_SetZombied(bool state)
{
	fIsZombie = state;

	if (state) {
		SetLowColor(kZombieColor);
		SetViewColor(kZombieColor);
	}
}


/**
 * @brief Builds the default "About / Remove replicant" pop-up menu.
 *
 * Creates a non-locking B_ITEMS_IN_COLUMN BPopUpMenu with two items:
 * an "About <app>" item that sends B_ABOUT_REQUESTED, and a
 * "Remove replicant" item that sends kDeleteReplicant.  The resulting
 * menu is stored in @a fPopUp.
 *
 * @see PopUp(), SetPopUp(), _ShowPopUp()
 */
void
BDragger::_BuildDefaultPopUp()
{
	fPopUp = new BPopUpMenu("Shelf", false, false, B_ITEMS_IN_COLUMN);

	// About
	BMessage* msg = new BMessage(B_ABOUT_REQUESTED);

	const char* name = fTarget->Name();
	if (name != NULL)
		msg->AddString("target", name);

	BString about(B_TRANSLATE("About %app" B_UTF8_ELLIPSIS));
	about.ReplaceFirst("%app", name);

	fPopUp->AddItem(new BMenuItem(about.String(), msg));
	fPopUp->AddSeparatorItem();
	fPopUp->AddItem(new BMenuItem(B_TRANSLATE("Remove replicant"),
		new BMessage(kDeleteReplicant)));
}


/**
 * @brief Displays the replicant context pop-up menu at the given position.
 *
 * Converts @a where to screen coordinates, ensures the pop-up exists (building
 * the default one if necessary), sets all menu items' target to the replicant
 * view, and runs the menu synchronously.
 *
 * @param target  The replicant view that will receive menu item messages.
 * @param where   The click location in the dragger's local coordinates.
 * @see _BuildDefaultPopUp(), PopUp()
 */
void
BDragger::_ShowPopUp(BView* target, BPoint where)
{
	BPoint point = ConvertToScreen(where);

	if (fPopUp == NULL && fTarget != NULL)
		_BuildDefaultPopUp();

	fPopUp->SetTargetForItems(fTarget);

	float menuWidth, menuHeight;
	fPopUp->GetPreferredSize(&menuWidth, &menuHeight);
	BRect rect(0, 0, menuWidth, menuHeight);
	rect.InsetBy(-0.5, -0.5);
	rect.OffsetTo(point);

	fPopUp->Go(point, true, false, rect, true);
}


#if __GNUC__ < 3

extern "C" BBitmap*
_ReservedDragger1__8BDragger(BDragger* dragger, BPoint* offset,
	drawing_mode* mode)
{
	return dragger->BDragger::DragBitmap(offset, mode);
}

#endif
