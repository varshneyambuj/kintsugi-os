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
 * MIT License. Copyright 2006-2024, Axel Dörfler, axeld@pinc-software.de.
 */

/**
 * @file IconView.h
 * @brief Icon container model and view: an Icon storage class that holds
 *        large/mini bitmaps plus vector data, and an IconView control that
 *        renders, drags, drops, and edits icons attached to MIME types or
 *        files.
 */

#ifndef ICON_VIEW_H
#define ICON_VIEW_H


#include <Control.h>
#include <Entry.h>
#include <Messenger.h>
#include <Mime.h>
#include <String.h>


/**
 * @brief Source from which an icon was resolved when looking it up for a
 *        given MIME type.
 */
enum icon_source {
	kNoIcon = 0,
	kOwnIcon,
	kApplicationIcon,
	kSupertypeIcon
};


/**
 * @brief Icon storage holding large and mini bitmaps and the original
 *        vector icon data, with copy-in/out helpers for app and type info.
 */
class Icon {
public:
								Icon();
								Icon(const Icon& source);
								~Icon();

			void				SetTo(const BAppFileInfo& info,
									const char* type = NULL);
			void				SetTo(const entry_ref& ref,
									const char* type = NULL);
			void				SetTo(const BMimeType& type,
									icon_source* _source = NULL);
			status_t			CopyTo(BAppFileInfo& info,
									const char* type = NULL,
									bool force = false) const;
			status_t			CopyTo(const entry_ref& ref,
									const char* type = NULL,
									bool force = false) const;
			status_t			CopyTo(BMimeType& type,
									bool force = false) const;
			status_t			CopyTo(BMessage& message) const;

			void				SetData(const uint8* data, size_t size);
			void				SetLarge(const BBitmap* large);
			void				SetMini(const BBitmap* large);
			void				Unset();

			bool				HasData() const;
			status_t			GetData(icon_size which,
									BBitmap** _bitmap) const;
			status_t			GetData(uint8** _data, size_t* _size) const;

			status_t			GetIcon(BBitmap* bitmap) const;

			Icon&				operator=(const Icon& source);

			void				AdoptLarge(BBitmap* large);
			void				AdoptMini(BBitmap* mini);
			void				AdoptData(uint8* data, size_t size);

	static	BBitmap*			AllocateBitmap(icon_size size, int32 space = -1);

private:
			BBitmap*			fLarge;
			BBitmap*			fMini;
			uint8*				fData;
			size_t				fSize;
};


class BSize;


/**
 * @brief BControl-derived widget that draws an icon, supports drag/drop,
 *        and launches the external icon-O-matic editor on invoke.
 */
class IconView : public BControl {
public:
								IconView(const char* name,
									uint32 flags = B_NAVIGABLE);
	virtual						~IconView();

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				Draw(BRect updateRect);
	virtual	void				GetPreferredSize(float* _width, float* _height);

	virtual	BSize				MaxSize();
	virtual	BSize				MinSize();
	virtual	BSize				PreferredSize();

	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseUp(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);

	virtual	void				MakeFocus(bool focus = true);

			void				SetTo(const entry_ref& file,
									const char* fileType = NULL);
			void				SetTo(const BMimeType& type);
			void				SetTo(::Icon* icon);
			void				Unset();
			void				Update();

			void				SetIconSize(icon_size size);
			void				ShowIconHeap(bool show);
			void				ShowEmptyFrame(bool show);
			status_t			SetTarget(const BMessenger& target);
			void				SetModificationMessage(BMessage* message);
			status_t			Invoke(BMessage* message = NULL);

			::Icon*				Icon();
			icon_size			IconSize() const { return fIconSize; }
			icon_source			IconSource() const { return fSource; }
			status_t			GetRef(entry_ref& ref) const;
			status_t			GetMimeType(BMimeType& type) const;

#if __GNUC__ == 2
	virtual	status_t			SetTarget(BMessenger target);
	virtual	status_t			SetTarget(const BHandler* handler,
									const BLooper* looper = NULL);
#else
			using BControl::SetTarget;
#endif


protected:
	virtual	bool				AcceptsDrag(const BMessage* message);
	virtual	BRect				BitmapRect() const;

private:
			void				_AddOrEditIcon();
			void				_SetIcon(BBitmap* large, BBitmap* mini,
									const uint8* data, size_t size,
									bool force = false);
			void				_SetIcon(entry_ref* ref);
			void				_RemoveIcon();
			void				_DeleteIcons();
			void				_StartWatching();
			void				_StopWatching();

			BMessenger			fTarget;
			BMessage*			fModificationMessage;
			icon_size			fIconSize;
			BRect				fIconRect;
			BBitmap*			fIconBitmap;
			BBitmap*			fHeapIconBitmap;

			bool				fHasRef;
			bool				fHasType;
			entry_ref			fRef;
			BMimeType			fType;
			icon_source			fSource;
			::Icon*				fIcon;

			BPoint				fDragPoint;
			bool				fTracking;
			bool				fDragging;
			bool				fDropTarget;
			bool				fShowEmptyFrame;
};


/** @brief Message: the icon was double-clicked or otherwise invoked. */
static const uint32 kMsgIconInvoked	= 'iciv';
/** @brief Message: user asked to remove the current icon. */
static const uint32 kMsgRemoveIcon	= 'icrm';
/** @brief Message: user asked to add an icon. */
static const uint32 kMsgAddIcon		= 'icad';
/** @brief Message: user asked to edit the current icon. */
static const uint32 kMsgEditIcon	= 'iced';


extern status_t icon_for_type(const BMimeType& type, uint8** _data,
	size_t* _size, icon_source* _source = NULL);
extern status_t icon_for_type(const BMimeType& type, BBitmap& bitmap,
	icon_size size, icon_source* _source = NULL);


#endif	// ICON_VIEW_H
