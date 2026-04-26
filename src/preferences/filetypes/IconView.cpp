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
 *   Copyright 2006-2024, Axel Dörfler, axeld@pinc-software.de.
 *   All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file IconView.cpp
 * @brief Reusable icon container plus the IconView control used across
 *        the FileTypes preference panes.
 *
 * The Icon helper class owns the three icon representations (large
 * bitmap, mini bitmap, vector data) and serialises them to and from
 * BAppFileInfo, BMimeType, BMessage, and entry_refs. IconView is a
 * BControl that paints an Icon, supports drag-and-drop with both bitmap
 * and HVIF payloads, and dispatches edits to Icon-O-Matic.
 */


#include "IconView.h"

#include <new>
#include <stdlib.h>
#include <strings.h>

#include <Application.h>
#include <AppFileInfo.h>
#include <Attributes.h>
#include <Bitmap.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Directory.h>
#include <IconEditorProtocol.h>
#include <IconUtils.h>
#include <Locale.h>
#include <MenuItem.h>
#include <Mime.h>
#include <NodeMonitor.h>
#include <PopUpMenu.h>
#include <Resources.h>
#include <Roster.h>
#include <Size.h>

#include "FileTypes.h"
#include "MimeTypeListView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Icon View"


using namespace std;


/**
 * @brief Resolves the vector icon for @a type using the same fallback
 *        chain Tracker uses.
 *
 * Looks first at the type's own icon, then at the type's preferred
 * application's per-type icon, then at the supertype, then at the
 * supertype's preferred application. Reports which step succeeded via
 * @a _source.
 *
 * @param type     MIME type to resolve.
 * @param _data    Output: malloc'd buffer with the HVIF data; caller
 *                 frees with free().
 * @param _size    Output: size of @a *_data in bytes.
 * @param _source  Optional output: which fallback rung produced the icon
 *                 (kOwnIcon, kApplicationIcon, kSupertypeIcon, or kNoIcon
 *                 on failure).
 * @retval B_OK         Icon found and ownership of @a *_data transferred
 *                      to the caller.
 * @retval B_BAD_VALUE  @a _data or @a _size was @c NULL.
 * @retval B_ERROR      No icon found at any rung.
 */
status_t
icon_for_type(const BMimeType& type, uint8** _data, size_t* _size,
	icon_source* _source)
{
	if (_data == NULL || _size == NULL)
		return B_BAD_VALUE;

	icon_source source = kNoIcon;
	uint8* data;
	size_t size;

	if (type.GetIcon(&data, &size) == B_OK)
		source = kOwnIcon;

	if (source == kNoIcon) {
		// check for icon from preferred app

		char preferred[B_MIME_TYPE_LENGTH];
		if (type.GetPreferredApp(preferred) == B_OK) {
			BMimeType preferredApp(preferred);

			if (preferredApp.GetIconForType(type.Type(), &data, &size) == B_OK)
				source = kApplicationIcon;
		}
	}

	if (source == kNoIcon) {
		// check super type for an icon

		BMimeType superType;
		if (type.GetSupertype(&superType) == B_OK) {
			if (superType.GetIcon(&data, &size) == B_OK)
				source = kSupertypeIcon;
			else {
				// check the super type's preferred app
				char preferred[B_MIME_TYPE_LENGTH];
				if (superType.GetPreferredApp(preferred) == B_OK) {
					BMimeType preferredApp(preferred);

					if (preferredApp.GetIconForType(superType.Type(),
							&data, &size) == B_OK)
						source = kSupertypeIcon;
				}
			}
		}
	}

	if (source != kNoIcon) {
		*_data = data;
		*_size = size;
	} // NOTE: else there is no data, so nothing is leaked.
	if (_source)
		*_source = source;

	return source != kNoIcon ? B_OK : B_ERROR;
}


/**
 * @brief Bitmap-output overload of icon_for_type() used when the caller
 *        already has an allocated BBitmap to render into.
 *
 * Walks the same fallback chain as the vector overload but stops as soon
 * as a bitmap of the requested @a size is found.
 *
 * @param type     MIME type to resolve.
 * @param bitmap   Destination bitmap; must already match @a size.
 * @param size     Pixel size requested (e.g. B_LARGE_ICON, B_MINI_ICON).
 * @param _source  Optional output: which fallback rung produced the icon.
 * @retval B_OK     Bitmap was filled in.
 * @retval B_ERROR  No bitmap icon found at any rung.
 */
status_t
icon_for_type(const BMimeType& type, BBitmap& bitmap, icon_size size,
	icon_source* _source)
{
	icon_source source = kNoIcon;

	if (type.GetIcon(&bitmap, size) == B_OK)
		source = kOwnIcon;

	if (source == kNoIcon) {
		// check for icon from preferred app

		char preferred[B_MIME_TYPE_LENGTH];
		if (type.GetPreferredApp(preferred) == B_OK) {
			BMimeType preferredApp(preferred);

			if (preferredApp.GetIconForType(type.Type(), &bitmap, size) == B_OK)
				source = kApplicationIcon;
		}
	}

	if (source == kNoIcon) {
		// check super type for an icon

		BMimeType superType;
		if (type.GetSupertype(&superType) == B_OK) {
			if (superType.GetIcon(&bitmap, size) == B_OK)
				source = kSupertypeIcon;
			else {
				// check the super type's preferred app
				char preferred[B_MIME_TYPE_LENGTH];
				if (superType.GetPreferredApp(preferred) == B_OK) {
					BMimeType preferredApp(preferred);

					if (preferredApp.GetIconForType(superType.Type(),
							&bitmap, size) == B_OK)
						source = kSupertypeIcon;
				}
			}
		}
	}

	if (_source)
		*_source = source;

	return source != kNoIcon ? B_OK : B_ERROR;
}


//	#pragma mark -


/** @brief Constructs an empty Icon with no large, mini, or vector data. */
Icon::Icon()
	:
	fLarge(NULL),
	fMini(NULL),
	fData(NULL),
	fSize(0)
{
}


/**
 * @brief Deep-copies @a source via the assignment operator.
 *
 * @param source  Icon to clone.
 */
Icon::Icon(const Icon& source)
	:
	fLarge(NULL),
	fMini(NULL),
	fData(NULL),
	fSize(0)
{
	*this = source;
}


/** @brief Releases the bitmaps and free()s the vector data. */
Icon::~Icon()
{
	delete fLarge;
	delete fMini;
	free(fData);
}


/**
 * @brief Loads the icon registered for @a type out of @a info.
 *
 * Tries the vector representation first; if present, the large/mini
 * bitmaps are skipped because the vector icon is sufficient on its own.
 *
 * @param info  BAppFileInfo wrapping the application file.
 * @param type  Optional MIME type whose per-type icon should be used;
 *              @c NULL means the application's own icon.
 */
void
Icon::SetTo(const BAppFileInfo& info, const char* type)
{
	Unset();

	uint8* data;
	size_t size;

	if (info.GetIconForType(type, &data, &size) == B_OK) {
		// we have the vector icon, no need to get the rest
		AdoptData(data, size);
		return;
	}

	BBitmap* icon = AllocateBitmap(B_LARGE_ICON, B_CMAP8);
	if (icon && info.GetIconForType(type, icon, B_LARGE_ICON) == B_OK)
		AdoptLarge(icon);
	else
		delete icon;

	icon = AllocateBitmap(B_MINI_ICON, B_CMAP8);
	if (icon && info.GetIconForType(type, icon, B_MINI_ICON) == B_OK)
		AdoptMini(icon);
	else
		delete icon;
}


/**
 * @brief Convenience overload that opens @a ref as a BAppFileInfo and
 *        delegates to the BAppFileInfo overload.
 *
 * @param ref   File whose icon should be loaded.
 * @param type  Optional MIME type for per-type icons; @c NULL for the
 *              file's own icon.
 */
void
Icon::SetTo(const entry_ref& ref, const char* type)
{
	Unset();

	BFile file(&ref, B_READ_ONLY);
	BAppFileInfo info(&file);
	if (file.InitCheck() == B_OK && info.InitCheck() == B_OK)
		SetTo(info, type);
}


/**
 * @brief Loads the icon for the MIME type @a type using the
 *        Tracker-equivalent fallback chain.
 *
 * @param type     MIME type to resolve.
 * @param _source  Optional output: which fallback rung produced the icon.
 */
void
Icon::SetTo(const BMimeType& type, icon_source* _source)
{
	Unset();

	uint8* data;
	size_t size;
	if (icon_for_type(type, &data, &size, _source) == B_OK) {
		// we have the vector icon, no need to get the rest
		AdoptData(data, size);
		return;
	}

	BBitmap* icon = AllocateBitmap(B_LARGE_ICON, B_CMAP8);
	if (icon && icon_for_type(type, *icon, B_LARGE_ICON, _source) == B_OK)
		AdoptLarge(icon);
	else
		delete icon;

	icon = AllocateBitmap(B_MINI_ICON, B_CMAP8);
	if (icon && icon_for_type(type, *icon, B_MINI_ICON) == B_OK)
		AdoptMini(icon);
	else
		delete icon;
}


/**
 * @brief Writes the held icon into @a info.
 *
 * Each representation (large, mini, vector) is written only when it is
 * present, unless @a force is @c true in which case @c NULL/0 is written
 * to clear that representation.
 *
 * @param info   Destination application file info.
 * @param type   Optional per-type icon target; @c NULL writes the file's
 *               own icon.
 * @param force  When @c true, missing representations are written as
 *               @c NULL to remove any previously stored value.
 * @return The last status returned by SetIconForType(); @c B_OK on
 *         success.
 */
status_t
Icon::CopyTo(BAppFileInfo& info, const char* type, bool force) const
{
	status_t status = B_OK;

	if (fLarge != NULL || force)
		status = info.SetIconForType(type, fLarge, B_LARGE_ICON);
	if (fMini != NULL || force)
		status = info.SetIconForType(type, fMini, B_MINI_ICON);
	if (fData != NULL || force)
		status = info.SetIconForType(type, fData, fSize);

	return status;
}


/**
 * @brief Convenience overload that wraps @a ref in a BAppFileInfo and
 *        delegates.
 *
 * @param ref    Destination file.
 * @param type   Optional per-type icon target.
 * @param force  Forwarded to the BAppFileInfo overload.
 * @return Whatever the BAppFileInfo overload returned, or an init error
 *         from BFile/BAppFileInfo.
 */
status_t
Icon::CopyTo(const entry_ref& ref, const char* type, bool force) const
{
	BFile file;
	status_t status = file.SetTo(&ref, B_READ_ONLY);
	if (status < B_OK)
		return status;

	BAppFileInfo info(&file);
	status = info.InitCheck();
	if (status < B_OK)
		return status;

	return CopyTo(info, type, force);
}


/**
 * @brief Writes the held icon directly to the MIME type entry.
 *
 * @param type   Destination MIME type (typically the type being edited).
 * @param force  When @c true, missing representations are cleared.
 * @return The last status returned by SetIcon(); @c B_OK on success.
 */
status_t
Icon::CopyTo(BMimeType& type, bool force) const
{
	status_t status = B_OK;

	if (fLarge != NULL || force)
		status = type.SetIcon(fLarge, B_LARGE_ICON);
	if (fMini != NULL || force)
		status = type.SetIcon(fMini, B_MINI_ICON);
	if (fData != NULL || force)
		status = type.SetIcon(fData, fSize);

	return status;
}


/**
 * @brief Archives the icon into @a message under the conventional
 *        @c icon/large, @c icon/mini, and @c icon keys.
 *
 * Used as the payload for clipboard and drag-and-drop operations.
 *
 * @param message  Destination message; existing keys are left intact.
 * @return Always @c B_OK; per-representation errors are silently ignored
 *         after the first failure since the message may still carry
 *         partial data.
 */
status_t
Icon::CopyTo(BMessage& message) const
{
	status_t status = B_OK;

	if (status == B_OK && fLarge != NULL) {
		BMessage archive;
		status = fLarge->Archive(&archive);
		if (status == B_OK)
			status = message.AddMessage("icon/large", &archive);
	}
	if (status == B_OK && fMini != NULL) {
		BMessage archive;
		status = fMini->Archive(&archive);
		if (status == B_OK)
			status = message.AddMessage("icon/mini", &archive);
	}
	if (status == B_OK && fData != NULL)
		status = message.AddData("icon", B_VECTOR_ICON_TYPE, fData, fSize);

	return B_OK;
}


/**
 * @brief Replaces the large bitmap by deep-copying @a large.
 *
 * @param large  Source bitmap; @c NULL clears the held large icon.
 */
void
Icon::SetLarge(const BBitmap* large)
{
	if (large != NULL) {
		if (fLarge == NULL)
			fLarge = new BBitmap(BRect(0, 0, 31, 31), B_CMAP8);

		memcpy(fLarge->Bits(), large->Bits(), min_c(large->BitsLength(),
			fLarge->BitsLength()));
	} else {
		delete fLarge;
		fLarge = NULL;
	}
}


/**
 * @brief Replaces the mini bitmap by deep-copying @a mini.
 *
 * @param mini  Source bitmap; @c NULL clears the held mini icon.
 */
void
Icon::SetMini(const BBitmap* mini)
{
	if (mini != NULL) {
		if (fMini == NULL)
			fMini = new BBitmap(BRect(0, 0, 15, 15), B_CMAP8);

		memcpy(fMini->Bits(), mini->Bits(), min_c(mini->BitsLength(),
			fMini->BitsLength()));
	} else {
		delete fMini;
		fMini = NULL;
	}
}


/**
 * @brief Replaces the held HVIF vector data with a copy of @a data.
 *
 * @param data  Source bytes; @c NULL clears the held vector data.
 * @param size  Length of @a data in bytes.
 */
void
Icon::SetData(const uint8* data, size_t size)
{
	free(fData);
	fData = NULL;

	if (data != NULL) {
		fData = (uint8*)malloc(size);
		if (fData != NULL) {
			fSize = size;
			//fType = B_VECTOR_ICON_TYPE;
			memcpy(fData, data, size);
		}
	}
}


/**
 * @brief Releases all icon representations and resets the cached size to
 *        zero.
 */
void
Icon::Unset()
{
	delete fLarge;
	delete fMini;
	free(fData);

	fLarge = fMini = NULL;
	fData = NULL;
}


/**
 * @brief Reports whether at least one icon representation has been
 *        loaded.
 *
 * @return @c true when any of large, mini, or vector data is set.
 */
bool
Icon::HasData() const
{
	return fData != NULL || fLarge != NULL || fMini != NULL;
}


/**
 * @brief Returns a freshly allocated copy of one bitmap representation.
 *
 * @param which    Either @c B_LARGE_ICON or @c B_MINI_ICON.
 * @param _bitmap  Output: newly allocated BBitmap; caller takes ownership.
 * @retval B_OK              Bitmap returned via @a _bitmap.
 * @retval B_BAD_VALUE       @a which was neither large nor mini.
 * @retval B_ENTRY_NOT_FOUND The requested representation is empty.
 * @retval B_NO_MEMORY       Allocation failed.
 */
status_t
Icon::GetData(icon_size which, BBitmap** _bitmap) const
{
	BBitmap* source;
	switch (which) {
		case B_LARGE_ICON:
			source = fLarge;
			break;
		case B_MINI_ICON:
			source = fMini;
			break;
		default:
			return B_BAD_VALUE;
	}

	if (source == NULL)
		return B_ENTRY_NOT_FOUND;

	BBitmap* bitmap = new (nothrow) BBitmap(source);
	if (bitmap == NULL || bitmap->InitCheck() != B_OK) {
		delete bitmap;
		return B_NO_MEMORY;
	}

	*_bitmap = bitmap;
	return B_OK;
}


/**
 * @brief Returns a malloc'd copy of the held HVIF vector data.
 *
 * @param _data  Output: malloc'd buffer; caller frees with free().
 * @param _size  Output: number of bytes in @a *_data.
 * @retval B_OK              Buffer returned via @a _data.
 * @retval B_ENTRY_NOT_FOUND No vector data is held.
 * @retval B_NO_MEMORY       Allocation failed.
 */
status_t
Icon::GetData(uint8** _data, size_t* _size) const
{
	if (fData == NULL)
		return B_ENTRY_NOT_FOUND;

	uint8* data = (uint8*)malloc(fSize);
	if (data == NULL)
		return B_NO_MEMORY;

	memcpy(data, fData, fSize);
	*_data = data;
	*_size = fSize;
	return B_OK;
}


/**
 * @brief Renders the icon into @a bitmap at the bitmap's existing size.
 *
 * Prefers the vector representation when present; otherwise picks the
 * closest of the cached large/mini bitmaps and rescales it via an
 * intermediate offscreen view.
 *
 * @param bitmap  Destination bitmap; must be initialised with the desired
 *                size and color space.
 * @retval B_OK              Bitmap was filled.
 * @retval B_BAD_VALUE       @a bitmap was @c NULL.
 * @retval B_ENTRY_NOT_FOUND Neither vector nor bitmap data is held.
 */
status_t
Icon::GetIcon(BBitmap* bitmap) const
{
	if (bitmap == NULL)
		return B_BAD_VALUE;

	if (fData != NULL && BIconUtils::GetVectorIcon(fData, fSize, bitmap) == B_OK)
		return B_OK;

	int32 width = bitmap->Bounds().IntegerWidth() + 1;

	if (width == B_LARGE_ICON && fLarge != NULL) {
		bitmap->SetBits(fLarge->Bits(), fLarge->BitsLength(), 0,
			fLarge->ColorSpace());
		return B_OK;
	}
	if (width == B_MINI_ICON && fMini != NULL) {
		bitmap->SetBits(fMini->Bits(), fMini->BitsLength(), 0,
			fMini->ColorSpace());
		return B_OK;
	}

	BBitmap* source = (width > B_LARGE_ICON && fLarge != NULL) || fMini == NULL
		? fLarge : fMini;
	if (source == NULL)
		return B_ENTRY_NOT_FOUND;

	// Resize bitmap to fit the target
	BBitmap* target = new (nothrow) BBitmap(bitmap->Bounds(),
		B_BITMAP_ACCEPTS_VIEWS, bitmap->ColorSpace());
	if (target != NULL && target->InitCheck() == B_OK && target->Lock()) {
		BView* view = new BView(bitmap->Bounds(), NULL, B_FOLLOW_NONE,
			B_WILL_DRAW);
		target->AddChild(view);
		view->DrawBitmap(source, bitmap->Bounds());
		view->Flush();
		target->RemoveChild(view);
		target->Unlock();

		// Copy target to original bitmap
		bitmap->SetBits(target->Bits(), target->BitsLength(), 0,
			target->ColorSpace());

		delete view;
	}
	delete target;

	return B_OK;
}


/**
 * @brief Deep-copy assignment operator that mirrors all three
 *        representations from @a source.
 *
 * @param source  Icon to copy from.
 * @return Reference to @c *this.
 */
Icon&
Icon::operator=(const Icon& source)
{
	Unset();

	SetData(source.fData, source.fSize);
	SetLarge(source.fLarge);
	SetMini(source.fMini);

	return *this;
}


/**
 * @brief Takes ownership of @a large, replacing any previous large
 *        bitmap.
 *
 * @param large  New bitmap; ownership transfers to this Icon. May be
 *               @c NULL.
 */
void
Icon::AdoptLarge(BBitmap *large)
{
	delete fLarge;
	fLarge = large;
}


/**
 * @brief Takes ownership of @a mini, replacing any previous mini bitmap.
 *
 * @param mini  New bitmap; ownership transfers to this Icon. May be
 *              @c NULL.
 */
void
Icon::AdoptMini(BBitmap *mini)
{
	delete fMini;
	fMini = mini;
}


/**
 * @brief Takes ownership of @a data, replacing any previous vector data.
 *
 * @param data  Malloc'd buffer; ownership transfers to this Icon (which
 *              will free() it). May be @c NULL.
 * @param size  Length of @a data in bytes.
 */
void
Icon::AdoptData(uint8* data, size_t size)
{
	free(fData);
	fData = data;
	fSize = size;
}


/**
 * @brief Allocates a BBitmap sized according to the system control look,
 *        for use as a render target.
 *
 * Special-cases @c B_CMAP8 to allocate the legacy 8-bit-indexed bitmap
 * without any compose-size scaling.
 *
 * @param size   Logical icon size (e.g. B_LARGE_ICON or B_MINI_ICON).
 * @param space  Color space; @c -1 selects @c B_RGBA32.
 * @return Newly allocated BBitmap (caller owns), or @c NULL on
 *         allocation failure.
 */
/*static*/ BBitmap*
Icon::AllocateBitmap(icon_size size, int32 space)
{
	int32 kSpace = B_RGBA32;
	if (space == -1)
		space = kSpace;

	BBitmap* bitmap;
	if (space == B_CMAP8) {
		// Legacy mode; no scaling
		bitmap = new (nothrow) BBitmap(BRect(0, 0, (int32)size - 1, (int32)size - 1), B_CMAP8);
	} else {
		bitmap = new (nothrow) BBitmap(BRect(BPoint(0, 0),
			be_control_look->ComposeIconSize(size)), (color_space)space);
	}
	if (bitmap == NULL || bitmap->InitCheck() != B_OK) {
		delete bitmap;
		return NULL;
	}

	return bitmap;
}


//	#pragma mark -


/**
 * @brief Constructs the icon control at the default large-icon size.
 *
 * @param name   View name forwarded to BControl.
 * @param flags  Additional creation flags OR'd with @c B_WILL_DRAW.
 */
IconView::IconView(const char* name, uint32 flags)
	:
	BControl(name, NULL, NULL, B_WILL_DRAW | flags),
	fModificationMessage(NULL),
	fIconSize((icon_size)0),
	fIconBitmap(NULL),
	fHeapIconBitmap(NULL),
	fHasRef(false),
	fHasType(false),
	fIcon(NULL),
	fTracking(false),
	fDragging(false),
	fDropTarget(false),
	fShowEmptyFrame(true)
{
	SetIconSize(B_LARGE_ICON);
}


/** @brief Destroys the cached bitmap and the modification-message
           template. */
IconView::~IconView()
{
	delete fIconBitmap;
	delete fModificationMessage;
}


/**
 * @brief Adopts parent colors and starts watching the bound source if one
 *        was provided before the BLooper was available.
 */
void
IconView::AttachedToWindow()
{
	AdoptParentColors();

	fTarget = this;

	// SetTo() was already called before we were a valid messenger
	if (fHasRef || fHasType)
		_StartWatching();
}


/** @brief Stops node and MIME-type watching when the view leaves the
           window. */
void
IconView::DetachedFromWindow()
{
	_StopWatching();
}


/**
 * @brief Routes drop messages, popup-menu commands, and node-monitor /
 *        MIME-database notifications to the appropriate update path.
 *
 * Drop messages are converted into either a vector-data icon assignment
 * or, if only refs are present, the file-import path. Other notifications
 * trigger Update() so the displayed bitmap stays in sync with the
 * external source.
 *
 * @param message  Incoming BMessage.
 */
void
IconView::MessageReceived(BMessage* message)
{
	if (message->WasDropped() && message->ReturnAddress() != BMessenger(this)
		&& AcceptsDrag(message)) {
		// set icon from message
		BBitmap* mini = NULL;
		BBitmap* large = NULL;
		const uint8* data = NULL;
		ssize_t size = 0;

		message->FindData("icon", B_VECTOR_ICON_TYPE, (const void**)&data,
			&size);

		BMessage archive;
		if (message->FindMessage("icon/large", &archive) == B_OK)
			large = (BBitmap*)BBitmap::Instantiate(&archive);
		if (message->FindMessage("icon/mini", &archive) == B_OK)
			mini = (BBitmap*)BBitmap::Instantiate(&archive);

		if (large != NULL || mini != NULL || (data != NULL && size > 0))
			_SetIcon(large, mini, data, size);
		else {
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK)
				_SetIcon(&ref);
		}

		delete large;
		delete mini;

		return;
	}

	switch (message->what) {
		case kMsgIconInvoked:
		case kMsgEditIcon:
		case kMsgAddIcon:
			_AddOrEditIcon();
			break;
		case kMsgRemoveIcon:
			_RemoveIcon();
			break;

		case B_NODE_MONITOR:
		{
			if (!fHasRef)
				break;

			int32 opcode;
			if (message->FindInt32("opcode", &opcode) != B_OK
				|| opcode != B_ATTR_CHANGED)
				break;

			const char* name;
			if (message->FindString("attr", &name) != B_OK)
				break;

			if (!strcmp(name, kAttrMiniIcon)
				|| !strcmp(name, kAttrLargeIcon)
				|| !strcmp(name, kAttrIcon))
				Update();
			break;
		}

		case B_META_MIME_CHANGED:
		{
			if (!fHasType)
				break;

			const char* type;
			int32 which;
			if (message->FindString("be:type", &type) != B_OK
				|| message->FindInt32("be:which", &which) != B_OK)
				break;

			if (!strcasecmp(type, fType.Type())) {
				switch (which) {
					case B_MIME_TYPE_DELETED:
						Unset();
						break;

					case B_ICON_CHANGED:
						Update();
						break;

					default:
						break;
				}
			} else if (fSource != kOwnIcon
				&& message->FindString("be:extra_type", &type) == B_OK
				&& !strcasecmp(type, fType.Type())) {
				// this change could still affect our current icon

				if (which == B_MIME_TYPE_DELETED
					|| which == B_PREFERRED_APP_CHANGED
					|| which == B_SUPPORTED_TYPES_CHANGED
					|| which == B_ICON_FOR_TYPE_CHANGED)
					Update();
			}
			break;
		}

		case B_ICON_DATA_EDITED:
		{
			const uint8* data;
			ssize_t size;
			if (message->FindData("icon data", B_VECTOR_ICON_TYPE,
					(const void**)&data, &size) < B_OK)
				break;

			_SetIcon(NULL, NULL, data, size);
			break;
		}

		default:
			BControl::MessageReceived(message);
			break;
	}
}


/**
 * @brief Reports whether the view should accept @a message as a drop.
 *
 * Accepts a single ref drop (so long as it is not the file the view is
 * already bound to), or a message carrying any of the icon archive keys.
 *
 * @param message  Drag message to inspect.
 * @return @c true when the drop would be acted on by MessageReceived().
 */
bool
IconView::AcceptsDrag(const BMessage* message)
{
	if (!IsEnabled())
		return false;

	type_code type;
	int32 count;
	if (message->GetInfo("refs", &type, &count) == B_OK && count == 1
		&& type == B_REF_TYPE) {
		// if we're bound to an entry, check that no one drops this to us
		entry_ref ref;
		if (fHasRef && message->FindRef("refs", &ref) == B_OK && fRef == ref)
			return false;

		return true;
	}

	if ((message->GetInfo("icon/large", &type) == B_OK
			&& type == B_MESSAGE_TYPE)
		|| (message->GetInfo("icon", &type) == B_OK
			&& type == B_VECTOR_ICON_TYPE)
		|| (message->GetInfo("icon/mini", &type) == B_OK
			&& type == B_MESSAGE_TYPE))
		return true;

	return false;
}


/**
 * @brief Returns the local rectangle used to render the icon bitmap.
 *
 * @return Cached frame matched to the current icon size.
 */
BRect
IconView::BitmapRect() const
{
	return fIconRect;
}


/**
 * @brief Paints the cached icon, focus ring, and drop-target frame.
 *
 * Falls through to a one-pixel placeholder rectangle when no icon is
 * loaded and the empty-frame option is on.
 *
 * @param updateRect  Region the system asks us to repaint.
 */
void
IconView::Draw(BRect updateRect)
{
	SetDrawingMode(B_OP_ALPHA);

	if (fHeapIconBitmap != NULL)
		DrawBitmap(fHeapIconBitmap, BitmapRect());
	else if (fIconBitmap != NULL)
		DrawBitmap(fIconBitmap, BitmapRect());
	else if (!fDropTarget && fShowEmptyFrame) {
		// draw frame so that the user knows here is something he
		// might be able to click on
		SetHighColor(tint_color(ViewColor(), B_DARKEN_1_TINT));
		StrokeRect(BitmapRect());
	}

	if (IsFocus()) {
		// mark this view as a having focus
		SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
		StrokeRect(BitmapRect());
	}
	if (fDropTarget) {
		// mark this view as a drop target
		SetHighColor(0, 0, 0);
		SetPenSize(2);
		BRect rect = BitmapRect();
// TODO: this is an incompatibility between R5 and Haiku and should be fixed!
// (Necessary adjustment differs.)
		rect.left++;
		rect.top++;

		StrokeRect(rect);
		SetPenSize(1);
	}
}


/**
 * @brief Reports the bitmap rect's width and height as the preferred size.
 *
 * @param _width   Optional output: preferred width in pixels.
 * @param _height  Optional output: preferred height in pixels.
 */
void
IconView::GetPreferredSize(float* _width, float* _height)
{
	if (_width)
		*_width = fIconRect.Width();

	if (_height)
		*_height = fIconRect.Height();
}


/** @brief Returns the bitmap rect size as the minimum layout size. */
BSize
IconView::MinSize()
{
	float width, height;
	GetPreferredSize(&width, &height);
	return BSize(width, height);
}


/** @brief Returns the same value as MinSize() so the layout fixes the
           view at its preferred size. */
BSize
IconView::PreferredSize()
{
	return MinSize();
}


/** @brief Returns the same value as MinSize() so the layout never grows
           the icon. */
BSize
IconView::MaxSize()
{
	return MinSize();
}


/**
 * @brief Handles primary-button clicks (single click starts drag, double
 *        click invokes Icon-O-Matic) and secondary-button clicks
 *        (context menu).
 *
 * @param where  Mouse-down location in view coordinates.
 */
void
IconView::MouseDown(BPoint where)
{
	if (!IsEnabled())
		return;

	int32 buttons = B_PRIMARY_MOUSE_BUTTON;
	int32 clicks = 1;
	if (Looper() != NULL && Looper()->CurrentMessage() != NULL) {
		if (Looper()->CurrentMessage()->FindInt32("buttons", &buttons) != B_OK)
			buttons = B_PRIMARY_MOUSE_BUTTON;
		if (Looper()->CurrentMessage()->FindInt32("clicks", &clicks) != B_OK)
			clicks = 1;
	}

	if ((buttons & B_PRIMARY_MOUSE_BUTTON) != 0
		&& BitmapRect().Contains(where)) {
		if (clicks == 2) {
			// double click - open Icon-O-Matic
			Invoke();
		} else if (fIconBitmap != NULL) {
			// start tracking - this icon might be dragged around
			fDragPoint = where;
			fTracking = true;
			SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
		}
	}

	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		// show context menu

		ConvertToScreen(&where);

		BPopUpMenu* menu = new BPopUpMenu("context");
		menu->SetFont(be_plain_font);

		bool hasIcon = fHasType ? fSource == kOwnIcon : fIconBitmap != NULL;
		if (hasIcon) {
			menu->AddItem(new BMenuItem(
				B_TRANSLATE("Edit icon" B_UTF8_ELLIPSIS),
				new BMessage(kMsgEditIcon)));
		} else {
			menu->AddItem(new BMenuItem(
				B_TRANSLATE("Add icon" B_UTF8_ELLIPSIS),
				new BMessage(kMsgAddIcon)));
		}

		BMenuItem* item = new BMenuItem(
			B_TRANSLATE("Remove icon"), new BMessage(kMsgRemoveIcon));
		if (!hasIcon)
			item->SetEnabled(false);

		menu->AddItem(item);
		menu->SetTargetForItems(fTarget);

		menu->Go(where, true, false, true);
	}
}


/**
 * @brief Resets drag/tracking flags and erases the drop-target ring.
 *
 * @param where  Mouse-up location (unused).
 */
void
IconView::MouseUp(BPoint where)
{
	fTracking = false;
	fDragging = false;

	if (fDropTarget) {
		fDropTarget = false;
		Invalidate();
	}
}


/**
 * @brief Initiates a drag once the cursor has moved a few pixels with the
 *        primary button held, and tracks drop-target highlighting for
 *        incoming drags.
 *
 * The drag bitmap is built from the current icon, alpha-blended with a
 * translucent gray so the user can see the icon being lifted.
 *
 * @param where        Cursor location in view coordinates.
 * @param transit      Standard BView transit code.
 * @param dragMessage  Drag message attached to an in-flight drag.
 */
void
IconView::MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage)
{
	if (fTracking && !fDragging && fIconBitmap != NULL
		&& (abs((int32)(where.x - fDragPoint.x)) > 3
			|| abs((int32)(where.y - fDragPoint.y)) > 3)) {
		// Start drag
		BMessage message(B_SIMPLE_DATA);

		::Icon* icon = fIcon;
		if (fHasRef || fHasType) {
			icon = new ::Icon;
			if (fHasRef)
				icon->SetTo(fRef, fType.Type());
			else if (fHasType)
				icon->SetTo(fType);
		}

		icon->CopyTo(message);

		if (icon != fIcon)
			delete icon;

		BBitmap *dragBitmap = new BBitmap(fIconBitmap->Bounds(), B_RGBA32, true);
		dragBitmap->Lock();
		BView *view
			= new BView(dragBitmap->Bounds(), B_EMPTY_STRING, B_FOLLOW_NONE, 0);
		dragBitmap->AddChild(view);

		view->SetHighColor(B_TRANSPARENT_COLOR);
		view->FillRect(dragBitmap->Bounds());
		view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetHighColor(0, 0, 0, 160);
		view->DrawBitmap(fIconBitmap);

		view->Sync();
		dragBitmap->Unlock();

		DragMessage(&message, dragBitmap, B_OP_ALPHA,
			fDragPoint - BitmapRect().LeftTop(), this);
		fDragging = true;
	}

	if (dragMessage != NULL && !fDragging && AcceptsDrag(dragMessage)) {
		bool dropTarget = transit == B_ENTERED_VIEW || transit == B_INSIDE_VIEW;
		if (dropTarget != fDropTarget) {
			fDropTarget = dropTarget;
			Invalidate();
		}
	} else if (fDropTarget) {
		fDropTarget = false;
		Invalidate();
	}
}


/**
 * @brief Handles delete/backspace as remove-icon and enter/space as
 *        invoke; everything else falls through to BControl.
 *
 * @param bytes     UTF-8 bytes for the keystroke.
 * @param numBytes  Length of @a bytes.
 */
void
IconView::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes == 1) {
		switch (bytes[0]) {
			case B_DELETE:
			case B_BACKSPACE:
				_RemoveIcon();
				return;
			case B_ENTER:
			case B_SPACE:
				Invoke();
				return;
		}
	}

	BControl::KeyDown(bytes, numBytes);
}


/**
 * @brief Forces a redraw whenever the focus state actually changes.
 *
 * @param focus  @c true if the view is gaining focus, @c false if it is
 *               losing focus.
 */
void
IconView::MakeFocus(bool focus)
{
	if (focus != IsFocus())
		Invalidate();

	BControl::MakeFocus(focus);
}


/**
 * @brief Binds the view to a file's icon, optionally restricted to a
 *        per-type icon.
 *
 * @param ref       Source file.
 * @param fileType  Optional MIME type to read instead of the file's own
 *                  icon.
 */
void
IconView::SetTo(const entry_ref& ref, const char* fileType)
{
	Unset();

	fHasRef = true;
	fRef = ref;
	if (fileType != NULL)
		fType.SetTo(fileType);
	else
		fType.Unset();

	_StartWatching();
	Update();
}


/**
 * @brief Binds the view to the icon registered for the MIME type @a type.
 *
 * @param type  MIME type to display; an empty type clears the binding.
 */
void
IconView::SetTo(const BMimeType& type)
{
	Unset();

	if (type.Type() == NULL)
		return;

	fHasType = true;
	fType.SetTo(type.Type());

	_StartWatching();
	Update();
}


/**
 * @brief Binds the view to a free-standing Icon object owned by the
 *        caller.
 *
 * @param icon  Source icon; pointer is borrowed and must outlive the
 *              binding. May be @c NULL to clear.
 */
void
IconView::SetTo(::Icon* icon)
{
	if (fIcon == icon)
		return;

	Unset();

	fIcon = icon;

	Update();
}


/**
 * @brief Clears the current binding, stopping any node or MIME watching
 *        first.
 */
void
IconView::Unset()
{
	if (fHasRef || fHasType)
		_StopWatching();

	fHasRef = false;
	fHasType = false;

	fType.Unset();
	fIcon = NULL;
}


/**
 * @brief Re-renders the cached bitmap from whichever source is bound
 *        (file ref, MIME type, or freestanding Icon) and invalidates the
 *        view.
 */
void
IconView::Update()
{
	delete fIconBitmap;
	fIconBitmap = NULL;

	Invalidate();
		// this will actually trigger a redraw *after* we updated the icon below

	BBitmap* bitmap = NULL;

	if (fHasRef) {
		BFile file(&fRef, B_READ_ONLY);
		if (file.InitCheck() != B_OK)
			return;

		BNodeInfo info;
		if (info.SetTo(&file) != B_OK)
			return;

		bitmap = Icon::AllocateBitmap(fIconSize);
		if (bitmap != NULL && info.GetTrackerIcon(bitmap,
				(icon_size)(bitmap->Bounds().IntegerWidth() + 1)) != B_OK) {
			delete bitmap;
			return;
		}
	} else if (fHasType) {
		bitmap = Icon::AllocateBitmap(fIconSize);
		if (bitmap != NULL && icon_for_type(fType, *bitmap, (icon_size)fIconSize,
				&fSource) != B_OK) {
			delete bitmap;
			return;
		}
	} else if (fIcon != NULL) {
		bitmap = Icon::AllocateBitmap(fIconSize);
		if (fIcon->GetIcon(bitmap) != B_OK) {
			delete bitmap;
			bitmap = NULL;
		}
	}

	fIconBitmap = bitmap;
}


/**
 * @brief Resizes the displayed icon to @a size, clamped to
 *        [B_MINI_ICON, 256].
 *
 * @param size  New icon size; values outside the range are clamped before
 *              the bitmap is re-rendered.
 */
void
IconView::SetIconSize(icon_size size)
{
	if (size < B_MINI_ICON)
		size = B_MINI_ICON;
	if (size > 256)
		size = (icon_size)256;
	if (size == fIconSize)
		return;

	fIconSize = size;
	fIconRect = BRect(BPoint(0, 0), be_control_look->ComposeIconSize(fIconSize));
	Update();
}


/**
 * @brief Toggles a built-in "no icon" placeholder bitmap loaded from the
 *        application resources.
 *
 * Tries the vector @c VICN resource first and falls back to the legacy
 * 8-bit bitmap resource when no vector data is shipped.
 *
 * @param show  @c true to load and display the placeholder; @c false to
 *              release it.
 */
void
IconView::ShowIconHeap(bool show)
{
	if (show == (fHeapIconBitmap != NULL))
		return;

	if (show) {
		BResources* resources = be_app->AppResources();
		if (resources != NULL) {
			const void* data = NULL;
			size_t size;
			data = resources->LoadResource('VICN', "icon heap", &size);
			if (data != NULL) {
				// got vector icon data
				fHeapIconBitmap = Icon::AllocateBitmap(B_LARGE_ICON, B_RGBA32);
				if (BIconUtils::GetVectorIcon((const uint8*)data,
						size, fHeapIconBitmap) != B_OK) {
					// bad data
					delete fHeapIconBitmap;
					fHeapIconBitmap = NULL;
					data = NULL;
				}
			}
			if (data == NULL) {
				// no vector icon or failed to get bitmap
				// try bitmap icon
				data = resources->LoadResource(B_LARGE_ICON_TYPE, "icon heap",
					NULL);
				if (data != NULL) {
					fHeapIconBitmap = Icon::AllocateBitmap(B_LARGE_ICON, B_CMAP8);
					if (fHeapIconBitmap != NULL) {
						memcpy(fHeapIconBitmap->Bits(), data,
							fHeapIconBitmap->BitsLength());
					}
				}
			}
		}
	} else {
		delete fHeapIconBitmap;
		fHeapIconBitmap = NULL;
	}
}


/**
 * @brief Toggles the dotted placeholder frame drawn when the view is
 *        empty.
 *
 * @param show  @c true to paint the placeholder rectangle, @c false to
 *              suppress it.
 */
void
IconView::ShowEmptyFrame(bool show)
{
	if (show == fShowEmptyFrame)
		return;

	fShowEmptyFrame = show;
	if (fIconBitmap == NULL)
		Invalidate();
}


/**
 * @brief Redirects context-menu and Icon-O-Matic launch messages to
 *        @a target.
 *
 * @param target  New BMessenger; the previous target is discarded.
 * @return Always @c B_OK.
 */
status_t
IconView::SetTarget(const BMessenger& target)
{
	fTarget = target;
	return B_OK;
}


/**
 * @brief Replaces the modification-message template that is fired after
 *        every successful icon edit.
 *
 * @param message  New template; ownership transfers to this view. May be
 *                 @c NULL.
 */
void
IconView::SetModificationMessage(BMessage* message)
{
	delete fModificationMessage;
	fModificationMessage = message;
}


/**
 * @brief Sends @a message (or the @c kMsgIconInvoked default) to the
 *        configured target.
 *
 * @param message  Optional payload. Defaults to a freshly synthesised
 *                 @c kMsgIconInvoked message when @c NULL.
 * @return Always @c B_OK.
 */
status_t
IconView::Invoke(BMessage* message)
{
	if (message == NULL)
		fTarget.SendMessage(kMsgIconInvoked);
	else
		fTarget.SendMessage(message);
	return B_OK;
}


/**
 * @brief Returns the freestanding Icon bound to the view, if any.
 *
 * @return Borrowed pointer; @c NULL when no Icon was set or the view is
 *         bound to a file or MIME type instead.
 */
Icon*
IconView::Icon()
{
	return fIcon;
}


/**
 * @brief Returns the file ref the view is bound to.
 *
 * @param ref  Output: only valid on @c B_OK.
 * @retval B_OK        View is in ref-binding mode.
 * @retval B_BAD_TYPE  View is bound to a MIME type or freestanding Icon.
 */
status_t
IconView::GetRef(entry_ref& ref) const
{
	if (!fHasRef)
		return B_BAD_TYPE;

	ref = fRef;
	return B_OK;
}


/**
 * @brief Returns the MIME type the view is bound to.
 *
 * @param type  Output: only valid on @c B_OK.
 * @retval B_OK        View is in MIME-type binding mode.
 * @retval B_BAD_TYPE  View is bound to a file ref or freestanding Icon.
 */
status_t
IconView::GetMimeType(BMimeType& type) const
{
	if (!fHasType)
		return B_BAD_TYPE;

	type.SetTo(fType.Type());
	return B_OK;
}


/**
 * @brief Launches Icon-O-Matic with either a refs-received message or a
 *        round-trip B_EDIT_ICON_DATA message.
 *
 * In ref-binding mode the editor edits the file directly and we pick up
 * changes via node monitoring. In static or MIME-type mode the editor
 * sends back the new vector data to a reply messenger.
 *
 * @todo Preserve object names in the round-trip path, possibly via a
 *       sidecar attribute.
 */
void
IconView::_AddOrEditIcon()
{
	BMessage message;
	if (fHasRef && fType.Type() == NULL) {
		// in ref mode, Icon-O-Matic can change the icon directly, and
		// we'll pick it up via node monitoring
		message.what = B_REFS_RECEIVED;
		message.AddRef("refs", &fRef);
	} else {
		// in static or MIME type mode, Icon-O-Matic needs to return the
		// buffer it changed once its done
		message.what = B_EDIT_ICON_DATA;
		message.AddMessenger("reply to", BMessenger(this));

		::Icon* icon = fIcon;
		if (icon == NULL) {
			icon = new ::Icon();
			if (fHasRef)
				icon->SetTo(fRef, fType.Type());
			else
				icon->SetTo(fType);
		}

		if (icon->HasData()) {
			uint8* data;
			size_t size;
			if (icon->GetData(&data, &size) == B_OK) {
				message.AddData("icon data", B_VECTOR_ICON_TYPE, data, size);
				free(data);
			}

			// TODO: somehow figure out how names of objects in the icon
			// can be preserved. Maybe in a second (optional) attribute
			// where ever a vector icon attribute is present?
		}

		if (icon != fIcon)
			delete icon;
	}

	be_roster->Launch("application/x-vnd.haiku-icon_o_matic", &message);
}


/**
 * @brief Writes new icon data to whichever source is bound: file ref,
 *        MIME type, or freestanding Icon.
 *
 * For ref bindings the visible icon refreshes via node monitoring; for
 * MIME bindings via the database watcher; for Icon bindings the bitmap is
 * regenerated synchronously. Fires the modification message if one was
 * configured.
 *
 * @param large  Optional new large bitmap.
 * @param mini   Optional new mini bitmap.
 * @param data   Optional new HVIF vector data.
 * @param size   Length of @a data in bytes.
 * @param force  When @c true, missing inputs clear the corresponding
 *               representation rather than being skipped.
 */
void
IconView::_SetIcon(BBitmap* large, BBitmap* mini, const uint8* data,
	size_t size, bool force)
{
	if (fHasRef) {
		BNodeInfo node;
		BDirectory refdir;
		BFile file;
		BEntry entry(&fRef, true);

		if (entry.IsFile()) {
			file.SetTo(&fRef, B_READ_WRITE);
			if (is_application(file)) {
				BAppFileInfo info(&file);
				if (info.InitCheck() == B_OK) {
					if (large != NULL || force)
						info.SetIconForType(fType.Type(), large, B_LARGE_ICON);
					if (mini != NULL || force)
						info.SetIconForType(fType.Type(), mini, B_MINI_ICON);
					if (data != NULL || force)
						info.SetIconForType(fType.Type(), data, size);
				}
			} else
				node.SetTo(&file);
		}
		if (entry.IsDirectory()) {
			refdir.SetTo(&fRef);
			node.SetTo(&refdir);
		}
		if (node.InitCheck() == B_OK) {
			if (large != NULL || force)
				node.SetIcon(large, B_LARGE_ICON);
			if (mini != NULL || force)
				node.SetIcon(mini, B_MINI_ICON);
			if (data != NULL || force)
				node.SetIcon(data, size);
		}
		// the icon shown will be updated using node monitoring
	} else if (fHasType) {
		if (large != NULL || force)
			fType.SetIcon(large, B_LARGE_ICON);
		if (mini != NULL || force)
			fType.SetIcon(mini, B_MINI_ICON);
		if (data != NULL || force)
			fType.SetIcon(data, size);

		// the icon shown will be updated automatically - we're watching
		// any changes to the MIME database
	} else if (fIcon != NULL) {
		if (large != NULL || force)
			fIcon->SetLarge(large);
		if (mini != NULL || force)
			fIcon->SetMini(mini);
		if (data != NULL || force)
			fIcon->SetData(data, size);

		// replace visible icon
		if (fIconBitmap == NULL && fIcon->HasData())
			fIconBitmap = Icon::AllocateBitmap(fIconSize);

		if (fIcon->GetIcon(fIconBitmap) != B_OK) {
			delete fIconBitmap;
			fIconBitmap = NULL;
		}
		Invalidate();
	}

	if (fModificationMessage)
		Invoke(fModificationMessage);
}


/**
 * @brief Imports an icon from the file pointed to by @a ref.
 *
 * Tries the vector representation first, then large/mini bitmaps, then
 * looks up the file's declared MIME type and re-runs the icon search at
 * that type.
 *
 * @param ref  Source file (typically dropped onto the view).
 * @todo Recognise device icons in addition to MIME types.
 */
void
IconView::_SetIcon(entry_ref* ref)
{
	// retrieve icons from file
	BFile file(ref, B_READ_ONLY);
	BAppFileInfo info(&file);
	if (file.InitCheck() != B_OK || info.InitCheck() != B_OK)
		return;

	// try vector/PNG icon first
	uint8* data = NULL;
	size_t size = 0;
	if (info.GetIcon(&data, &size) == B_OK) {
		_SetIcon(NULL, NULL, data, size);
		free(data);
		return;
	}

	// try large/mini icons
	bool hasMini = false;
	bool hasLarge = false;

	BBitmap* large = new BBitmap(BRect(0, 0, 31, 31), B_CMAP8);
	if (large->InitCheck() != B_OK) {
		delete large;
		large = NULL;
	}
	BBitmap* mini = new BBitmap(BRect(0, 0, 15, 15), B_CMAP8);
	if (mini->InitCheck() != B_OK) {
		delete mini;
		mini = NULL;
	}

	if (large != NULL && info.GetIcon(large, B_LARGE_ICON) == B_OK)
		hasLarge = true;
	if (mini != NULL && info.GetIcon(mini, B_MINI_ICON) == B_OK)
		hasMini = true;

	if (!hasMini && !hasLarge) {
		// TODO: don't forget device icons!

		// try MIME type icon
		char type[B_MIME_TYPE_LENGTH];
		if (info.GetType(type) != B_OK)
			return;

		BMimeType mimeType(type);
		if (icon_for_type(mimeType, &data, &size) != B_OK) {
			// only try large/mini icons when there is no vector icon
			if (large != NULL
				&& icon_for_type(mimeType, *large, B_LARGE_ICON) == B_OK)
				hasLarge = true;
			if (mini != NULL
				&& icon_for_type(mimeType, *mini, B_MINI_ICON) == B_OK)
				hasMini = true;
		}
	}

	if (data != NULL) {
		_SetIcon(NULL, NULL, data, size);
		free(data);
	} else if (hasLarge || hasMini)
		_SetIcon(large, mini, NULL, 0);

	delete large;
	delete mini;
}


/** @brief Clears every icon representation by forcing a NULL write. */
void
IconView::_RemoveIcon()
{
	_SetIcon(NULL, NULL, NULL, 0, true);
}


/**
 * @brief Subscribes to the source change feed appropriate for the current
 *        binding (node monitor for refs, MIME watcher for types).
 *
 * @note Silently does nothing when invoked before the BLooper is
 *       attached.
 */
void
IconView::_StartWatching()
{
	if (Looper() == NULL) {
		// we are not a valid messenger yet
		return;
	}

	if (fHasRef) {
		BNode node(&fRef);
		node_ref nodeRef;
		if (node.InitCheck() == B_OK
			&& node.GetNodeRef(&nodeRef) == B_OK)
			watch_node(&nodeRef, B_WATCH_ATTR, this);
	} else if (fHasType)
		BMimeType::StartWatching(this);
}


/**
 * @brief Counterpart to _StartWatching(): unsubscribes from whichever
 *        feed was in use.
 */
void
IconView::_StopWatching()
{
	if (fHasRef)
		stop_watching(this);
	else if (fHasType)
		BMimeType::StopWatching(this);
}


#if __GNUC__ == 2

/**
 * @brief Legacy GCC2 ABI shim that defers to BControl::SetTarget().
 *
 * @param target  New messenger.
 * @return Whatever the BControl override returns.
 */
status_t
IconView::SetTarget(BMessenger target)
{
	return BControl::SetTarget(target);
}


/**
 * @brief Legacy GCC2 ABI shim that defers to the handler+looper variant
 *        of BControl::SetTarget().
 *
 * @param handler  Target handler.
 * @param looper   Optional looper; defaults to @c NULL.
 * @return Whatever the BControl override returns.
 */
status_t
IconView::SetTarget(const BHandler* handler, const BLooper* looper = NULL)
{
	return BControl::SetTarget(handler,
		looper);
}

#endif

