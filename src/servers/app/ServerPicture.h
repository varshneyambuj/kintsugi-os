/*
 * Copyright 2025, Kintsugi OS Contributors.
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
 * This file incorporates work from the Haiku project, originally
 * distributed under the MIT License.
 * Copyright 2001-2019, Haiku.
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Stefano Ceccherini <stefano.ceccherini@gmail.com>
 *		Julian Harnath <julian.harnath@rwth-aachen.de>
 *		Stephan Aßmus <superstippi@gmx.de>
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file ServerPicture.h
 *  @brief Server-side picture that records and replays drawing commands. */

#ifndef SERVER_PICTURE_H
#define SERVER_PICTURE_H


#include <DataIO.h>

#include <AutoDeleter.h>
#include <ObjectList.h>
#include <PictureDataWriter.h>
#include <Referenceable.h>


class BFile;
class Canvas;
class ServerApp;
class ServerFont;
class View;

namespace BPrivate {
	class LinkReceiver;
	class PortLink;
}
class BList;


/** @brief Records drawing commands and replays them onto a Canvas target. */
class ServerPicture : public BReferenceable, public PictureDataWriter {
public:
	/** @brief Constructs an empty ServerPicture with an in-memory data store. */
								ServerPicture();

	/** @brief Copy-constructs a ServerPicture from an existing one.
	 *  @param other Source picture to clone. */
								ServerPicture(const ServerPicture& other);

	/** @brief Constructs a ServerPicture backed by data in a file.
	 *  @param fileName Path to the file containing picture data.
	 *  @param offset   Byte offset within the file where data begins. */
								ServerPicture(const char* fileName,
									int32 offset);
	virtual						~ServerPicture();

	/** @brief Returns the unique token identifying this picture.
	 *  @return Token value. */
			int32				Token() { return fToken; }

	/** @brief Sets the owning ServerApp for this picture.
	 *  @param owner New owner application.
	 *  @return true on success. */
			bool				SetOwner(ServerApp* owner);

	/** @brief Returns the ServerApp that owns this picture.
	 *  @return Pointer to the owning ServerApp. */
			ServerApp*			Owner() const { return fOwner; }

	/** @brief Releases the client-side reference, allowing cleanup if appropriate.
	 *  @return true if the picture was destroyed as a result. */
			bool				ReleaseClientReference();

	/** @brief Marks the beginning of a state-change recording block. */
			void				EnterStateChange();

	/** @brief Marks the end of a state-change recording block. */
			void				ExitStateChange();

	/** @brief Synchronises the current drawing state from a Canvas into this picture.
	 *  @param canvas Source canvas whose state is to be recorded. */
			void				SyncState(Canvas* canvas);

	/** @brief Writes font state flags into the picture data stream.
	 *  @param font Font whose properties to record.
	 *  @param mask Bitmask of font properties to write. */
			void				WriteFontState(const ServerFont& font,
									uint16 mask);

	/** @brief Replays all recorded drawing commands onto the given canvas.
	 *  @param target Canvas that receives the drawing commands. */
			void				Play(Canvas* target);

	/** @brief Pushes a picture onto the nested picture stack.
	 *  @param picture Picture to push. */
			void 				PushPicture(ServerPicture* picture);

	/** @brief Pops and returns the top picture from the nested picture stack.
	 *  @return The previously pushed ServerPicture, or NULL if the stack is empty. */
			ServerPicture*		PopPicture();

	/** @brief Appends a child picture to this picture's sub-picture list.
	 *  @param picture Child picture to append. */
			void				AppendPicture(ServerPicture* picture);

	/** @brief Adds a picture to the nested list and returns its index.
	 *  @param picture Picture to nest.
	 *  @return Index of the newly added picture. */
			int32				NestPicture(ServerPicture* picture);

	/** @brief Returns the current byte length of the recorded picture data.
	 *  @return Data length in bytes. */
			off_t				DataLength() const;

	/** @brief Reads picture data from the link receiver.
	 *  @param link Source link to read data from.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			ImportData(BPrivate::LinkReceiver& link);

	/** @brief Writes picture data to the port link.
	 *  @param link Destination link to write data to.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			ExportData(BPrivate::PortLink& link);

private:
	friend class PictureBoundingBoxPlayer;

			typedef BObjectList<ServerPicture> PictureList;

			int32				fToken;
			ObjectDeleter<BFile>
								fFile;
			ObjectDeleter<BPositionIO>
								fData;
			ObjectDeleter<PictureList>
								fPictures;
			BReference<ServerPicture>
								fPushed;
			ServerApp*			fOwner;
};


#endif	// SERVER_PICTURE_H
