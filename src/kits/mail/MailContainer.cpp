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
 *   Container - message part container class
 *   Copyright 2001 Dr. Zoidberg Enterprises. All rights reserved.
 */


/**
 * @file MailContainer.cpp
 * @brief MIME multipart container class for the mail kit.
 *
 * BMIMEMultipartMailContainer parses and renders multipart/* MIME structures.
 * It lazily decodes sub-parts: at parse time only the byte ranges of each
 * sub-part within the source stream are recorded; sub-parts are decoded into
 * concrete BMailComponent instances on demand when GetComponent() is called.
 * Used directly by BEmailMessage and BAttributedMailAttachment.
 *
 * @see BMailComponent, BEmailMessage, BAttributedMailAttachment
 */


#include <String.h>
#include <List.h>
#include <Mime.h>

#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

class _EXPORT BMIMEMultipartMailContainer;

#include <MailContainer.h>
#include <MailAttachment.h>

/**
 * @brief Internal structure recording the byte range of one MIME sub-part.
 *
 * Both start and end are absolute offsets into the BPositionIO source stream.
 * The CRLF that precedes the next boundary delimiter is not included in the
 * range, which can cause end < start + header_size for malformed messages.
 */
typedef struct message_part {
	message_part(off_t start, off_t end) { this->start = start; this->end = end; }

	// Offset where the part starts (includes MIME sub-headers but not the
	// boundary line) in the message file.
	int32 start;

	// Offset just past the last byte of data, so total length == end - start.
	// Note that the CRLF that starts the next boundary isn't included in the
	// data, the end points at the start of the next CRLF+Boundary.  This can
	// lead to weird things like the blank line ending the subheader being the
	// same as the boundary starting CRLF.  So if you have something malformed
	// like this:
	// ------=_NextPart_005_0040_ENBYSXVW.VACTSCVC
    // Content-Type: text/plain; charset="ISO-8859-1"
    //
    // ------=_NextPart_005_0040_ENBYSXVW.VACTSCVC
    // If you subtract the header length (which includes the blank line) from
    // the MIME part total length (which doesn't include the blank line - it's
    // part of the next boundary), you get -2.
	int32 end;
} message_part;


/**
 * @brief Constructs a multipart container with an optional boundary string.
 *
 * Sets the MIME-Version and Content-Type headers to "1.0" and
 * "multipart/mixed" respectively, then installs the boundary via SetBoundary().
 *
 * @param boundary                     Boundary string for this container, or NULL.
 * @param this_is_an_MIME_message_text Warning text written before the first
 *                                     boundary for non-MIME-aware clients, or NULL.
 * @param defaultCharSet               Default charset used when creating sub-components.
 */
BMIMEMultipartMailContainer::BMIMEMultipartMailContainer(
	const char *boundary,
	const char *this_is_an_MIME_message_text,
	uint32 defaultCharSet)
	:
	BMailContainer (defaultCharSet),
	_boundary(NULL),
	_MIME_message_warning(this_is_an_MIME_message_text),
	_io_data(NULL)
{
	// Definition of the MIME version in the mail header should be enough
	SetHeaderField("MIME-Version","1.0");
	SetHeaderField("Content-Type","multipart/mixed");
	SetBoundary(boundary);
}

/*BMIMEMultipartMailContainer::BMIMEMultipartMailContainer(BMIMEMultipartMailContainer &copy) :
	BMailComponent(copy),
	_boundary(copy._boundary),
	_MIME_message_warning(copy._MIME_message_warning),
	_io_data(copy._io_data) {
		AddHeaderField("MIME-Version","1.0");
		AddHeaderField("Content-Type","multipart/mixed");
		SetBoundary(boundary);
	}*/


/**
 * @brief Destroys the container, freeing all sub-part records and components.
 */
BMIMEMultipartMailContainer::~BMIMEMultipartMailContainer() {
	for (int32 i = 0; i < _components_in_raw.CountItems(); i++)
		delete (message_part *)_components_in_raw.ItemAt(i);

	for (int32 i = 0; i < _components_in_code.CountItems(); i++)
		delete (BMailComponent *)_components_in_code.ItemAt(i);

	free((void *)_boundary);
}


/**
 * @brief Sets the MIME boundary string and updates the Content-Type header.
 *
 * Frees any previous boundary, duplicates the new one, and updates the
 * "boundary" parameter in the Content-Type header accordingly.
 *
 * @param boundary  New boundary string, or NULL to remove it.
 */
void BMIMEMultipartMailContainer::SetBoundary(const char *boundary) {
	free ((void *) _boundary);
	_boundary = NULL;
	if (boundary != NULL)
		_boundary = strdup(boundary);

	BMessage structured;
	HeaderField("Content-Type",&structured);

	if (_boundary == NULL)
		structured.RemoveName("boundary");
	else if (structured.ReplaceString("boundary",_boundary) != B_OK)
		structured.AddString("boundary",_boundary);

	SetHeaderField("Content-Type",&structured);
}


/**
 * @brief Sets the preamble text shown before the first boundary for legacy clients.
 *
 * @param text  Warning string, or NULL to suppress the preamble.
 */
void BMIMEMultipartMailContainer::SetThisIsAnMIMEMessageText(const char *text) {
	_MIME_message_warning = text;
}


/**
 * @brief Appends a new sub-component to this container.
 *
 * Adds \a component to both the code list and a corresponding NULL entry in
 * the raw list to keep the two lists parallel.
 *
 * @param component  Sub-component to add; the container does not take ownership.
 * @return B_OK on success, B_ERROR if either list add fails.
 */
status_t BMIMEMultipartMailContainer::AddComponent(BMailComponent *component) {
	if (!_components_in_code.AddItem(component))
		return B_ERROR;
	if (_components_in_raw.AddItem(NULL))
		return B_OK;

	_components_in_code.RemoveItem(component);
	return B_ERROR;
}


/**
 * @brief Retrieves a sub-component by index, lazily parsing from the source stream.
 *
 * If the component has already been parsed and cached in _components_in_code,
 * it is returned directly. Otherwise the corresponding byte range is read from
 * the source stream, the component type is determined, and a full parse is
 * performed before caching the result.
 *
 * @param index      Zero-based component index.
 * @param parse_now  If true, immediately decodes the sub-part body.
 * @return Pointer to the BMailComponent, or NULL if index is out of range or
 *         parsing fails.
 */
BMailComponent *BMIMEMultipartMailContainer::GetComponent(int32 index, bool parse_now) {
	if (index >= CountComponents())
		return NULL;

	if (BMailComponent *component = (BMailComponent *)_components_in_code.ItemAt(index))
		return component;	//--- Handle easy case

	message_part *part = (message_part *)(_components_in_raw.ItemAt(index));
	if (part == NULL)
		return NULL;

	_io_data->Seek(part->start,SEEK_SET);

	BMailComponent component (_charSetForTextDecoding);
	if (component.SetToRFC822(_io_data,part->end - part->start) < B_OK)
		return NULL;

	BMailComponent *piece = component.WhatIsThis();

	/* Debug code
	_io_data->Seek(part->start,SEEK_SET);
	char *data = new char[part->end - part->start + 1];
	_io_data->Read(data,part->end - part->start);
	data[part->end - part->start] = 0;
	puts((char *)(data));
	printf("Instantiating from %d to %d (%d octets)\n",part->start, part->end, part->end - part->start);
	*/
	_io_data->Seek(part->start,SEEK_SET);
	if (piece->SetToRFC822(_io_data,part->end - part->start, parse_now) < B_OK)
	{
		delete piece;
		return NULL;
	}
	_components_in_code.ReplaceItem(index,piece);

	return piece;
}


/**
 * @brief Returns the number of sub-components in this container.
 *
 * @return Count of items in the component lists.
 */
int32
BMIMEMultipartMailContainer::CountComponents() const
{
	return _components_in_code.CountItems();
}


/**
 * @brief Removes a specific sub-component by pointer.
 *
 * @param component  Pointer to the component to remove.
 * @return B_OK on success, B_BAD_VALUE if \a component is NULL,
 *         B_ENTRY_NOT_FOUND if it is not in this container.
 */
status_t
BMIMEMultipartMailContainer::RemoveComponent(BMailComponent *component)
{
	if (component == NULL)
		return B_BAD_VALUE;

	int32 index = _components_in_code.IndexOf(component);
	if (component == NULL)
		return B_ENTRY_NOT_FOUND;

	delete (BMailComponent *)_components_in_code.RemoveItem(index);
	delete (message_part *)_components_in_raw.RemoveItem(index);

	return B_OK;
}


/**
 * @brief Removes a sub-component by index.
 *
 * @param index  Zero-based index of the component to remove.
 * @return B_OK on success, B_BAD_INDEX if \a index is out of range.
 */
status_t
BMIMEMultipartMailContainer::RemoveComponent(int32 index)
{
	if (index >= CountComponents())
		return B_BAD_INDEX;

	delete (BMailComponent *)_components_in_code.RemoveItem(index);
	delete (message_part *)_components_in_raw.RemoveItem(index);

	return B_OK;
}


/**
 * @brief Not supported; returns B_BAD_TYPE.
 *
 * @return B_BAD_TYPE always — containers have no single decoded data stream.
 */
status_t BMIMEMultipartMailContainer::GetDecodedData(BPositionIO *)
{
	return B_BAD_TYPE; //------We don't play dat
}


/**
 * @brief Not supported; returns B_BAD_TYPE.
 *
 * @return B_BAD_TYPE always — containers have no single decoded data stream.
 */
status_t BMIMEMultipartMailContainer::SetDecodedData(BPositionIO *) {
	return B_BAD_TYPE; //------We don't play dat
}


/**
 * @brief Parses a multipart/* RFC 822 stream and records sub-part byte ranges.
 *
 * Reads the container headers, extracts the boundary string, then scans the
 * source stream byte-by-byte for boundary delimiters. For each part found,
 * a message_part record is appended to _components_in_raw. If \a copy_data
 * is true, all sub-parts are immediately parsed via GetComponent().
 *
 * @param data       Readable source stream positioned at the start of this part.
 * @param length     Number of bytes available for this container.
 * @param copy_data  If true, immediately parse all sub-component bodies.
 * @return B_OK on success, B_BAD_TYPE if the Content-Type is not multipart or
 *         lacks a boundary, or a negative IO error code.
 */
status_t BMIMEMultipartMailContainer::SetToRFC822(BPositionIO *data, size_t length, bool copy_data)
{
	typedef enum LookingForEnum {
		FIRST_NEWLINE,
		INITIAL_DASHES,
		BOUNDARY_BODY,
		LAST_NEWLINE,
		MAX_LOOKING_STATES
	} LookingFor;

	ssize_t     amountRead;
	ssize_t     amountToRead;
	ssize_t     boundaryLength;
	char        buffer [4096];
	ssize_t     bufferIndex;
	off_t       bufferOffset;
	ssize_t     bufferSize;
	BMessage    content_type;
	const char *content_type_string;
	bool        finalBoundary = false;
	bool        finalComponentCompleted = false;
	int         i;
	off_t       lastBoundaryOffset;
	LookingFor  state;
	off_t       startOfBoundaryOffset;
	off_t       topLevelEnd;
	off_t       topLevelStart;

	// Clear out old components.  Maybe make a MakeEmpty method?

	for (i = _components_in_code.CountItems(); i-- > 0;)
		delete (BMailComponent *)_components_in_code.RemoveItem(i);

	for (i = _components_in_raw.CountItems(); i-- > 0;)
		delete (message_part *)_components_in_raw.RemoveItem(i);

	// Start by reading the headers and getting the boundary string.

	_io_data = data;
	topLevelStart = data->Position();
	topLevelEnd = topLevelStart + length;

	BMailComponent::SetToRFC822(data,length);

	HeaderField("Content-Type",&content_type);
	content_type_string = content_type.FindString("unlabeled");
	if (content_type_string == NULL ||
		strncasecmp(content_type_string,"multipart",9) != 0)
		return B_BAD_TYPE;

	if (!content_type.HasString("boundary"))
		return B_BAD_TYPE;
	free ((void *) _boundary);
	_boundary = strdup(content_type.FindString("boundary"));
	boundaryLength = strlen(_boundary);
	if (boundaryLength > (ssize_t) sizeof (buffer) / 2)
		return B_BAD_TYPE; // Boundary is way too long, should be max 70 chars.

	//	Find container parts by scanning through the given portion of the file
	//	for the boundary marker lines.  The stuff between the header and the
	//	first boundary is ignored, the same as the stuff after the last
	//	boundary.  The rest get stored away as our sub-components.  See RFC2046
	//	section 5.1 for details.

	bufferOffset = data->Position(); // File offset of the start of the buffer.
	bufferIndex = 0; // Current position we are examining in the buffer.
	bufferSize = 0; // Amount of data actually in the buffer, not including NUL.
	startOfBoundaryOffset = -1;
	lastBoundaryOffset = -1;
	state = INITIAL_DASHES; // Starting just after a new line so don't search for it.
	while (((bufferOffset + bufferIndex < topLevelEnd)
		|| (state == LAST_NEWLINE /* No EOF test in LAST_NEWLINE state */))
		&& !finalComponentCompleted)
	{
		// Refill the buffer if the remaining amount of data is less than a
		// boundary's worth, plus four dashes and two CRLFs.
		if (bufferSize - bufferIndex < boundaryLength + 8)
		{
			// Shuffle the remaining bit of data in the buffer over to the front.
			if (bufferSize - bufferIndex > 0)
				memmove (buffer, buffer + bufferIndex, bufferSize - bufferIndex);
			bufferOffset += bufferIndex;
			bufferSize = bufferSize - bufferIndex;
			bufferIndex = 0;

			// Fill up the rest of the buffer with more data.  Also leave space
			// for a NUL byte just past the last data in the buffer so that
			// simple string searches won't go off past the end of the data.
			amountToRead = topLevelEnd - (bufferOffset + bufferSize);
			if (amountToRead > (ssize_t) sizeof (buffer) - 1 - bufferSize)
				amountToRead = sizeof (buffer) - 1 - bufferSize;
			if (amountToRead > 0) {
				amountRead = data->Read (buffer + bufferSize, amountToRead);
				if (amountRead < 0)
					return amountRead;
				bufferSize += amountRead;
			}
			buffer [bufferSize] = 0; // Add an end of string NUL byte.
		}

		// Search for whatever parts of the boundary we are currently looking
		// for in the buffer.  It starts with a newline (officially CRLF but we
		// also accept just LF for off-line e-mail files), followed by two
		// hyphens or dashes "--", followed by the unique boundary string
		// specified earlier in the header, followed by two dashes "--" for the
		// final boundary (or zero dashes for intermediate boundaries),
		// followed by white space (possibly including header style comments in
		// brackets), and then a newline.

		switch (state) {
			case FIRST_NEWLINE:
				// The newline before the boundary is considered to be owned by
				// the boundary, not part of the previous MIME component.
				startOfBoundaryOffset = bufferOffset + bufferIndex;
				if (buffer[bufferIndex] == '\r' && buffer[bufferIndex + 1] == '\n') {
					bufferIndex += 2;
					state = INITIAL_DASHES;
				} else if (buffer[bufferIndex] == '\n') {
					bufferIndex += 1;
					state = INITIAL_DASHES;
				} else
					bufferIndex++;
				break;

			case INITIAL_DASHES:
				if (buffer[bufferIndex] == '-' && buffer[bufferIndex + 1] == '-') {
					bufferIndex += 2;
					state = BOUNDARY_BODY;
				} else
					state = FIRST_NEWLINE;
				break;

			case BOUNDARY_BODY:
				if (strncmp (buffer + bufferIndex, _boundary, boundaryLength) != 0) {
					state = FIRST_NEWLINE;
					break;
				}
				bufferIndex += boundaryLength;
				finalBoundary = false;
				if (buffer[bufferIndex] == '-' && buffer[bufferIndex + 1] == '-') {
					bufferIndex += 2;
					finalBoundary = true;
				}
				state = LAST_NEWLINE;
				break;

			case LAST_NEWLINE:
				// Just keep on scanning until the next new line or end of file.
				if (buffer[bufferIndex] == '\r' && buffer[bufferIndex + 1] == '\n')
					bufferIndex += 2;
				else if (buffer[bufferIndex] == '\n')
					bufferIndex += 1;
				else if (buffer[bufferIndex] != 0 /* End of file is like a newline */) {
					// Not a new line or end of file, just skip over
					// everything.  White space or not, we don't really care.
					bufferIndex += 1;
					break;
				}
				// Got to the end of the boundary line and maybe now have
				// another component to add.
				if (lastBoundaryOffset >= 0) {
					_components_in_raw.AddItem (new message_part (lastBoundaryOffset, startOfBoundaryOffset));
					_components_in_code.AddItem (NULL);
				}
				// Next component's header starts just after the boundary line.
				lastBoundaryOffset = bufferOffset + bufferIndex;
				if (finalBoundary)
					finalComponentCompleted = true;
				state = FIRST_NEWLINE;
				break;

			default: // Should not happen.
				state = FIRST_NEWLINE;
		}
	}

	// Some bad MIME encodings (usually spam, or damaged files) don't put on
	// the trailing boundary.  Dump whatever is remaining into a final
	// component if there wasn't a trailing boundary and there is some data
	// remaining.

	if (!finalComponentCompleted
		&& lastBoundaryOffset >= 0 && lastBoundaryOffset < topLevelEnd) {
		_components_in_raw.AddItem (new message_part (lastBoundaryOffset, topLevelEnd));
		_components_in_code.AddItem (NULL);
	}

	// If requested, actually read the data inside each component, otherwise
	// only the positions in the BPositionIO are recorded.

	if (copy_data) {
		for (i = 0; GetComponent(i, true /* parse_now */) != NULL; i++) {}
	}

	data->Seek (topLevelEnd, SEEK_SET);
	return B_OK;
}


/**
 * @brief Renders the container and all sub-components to an RFC 822 output stream.
 *
 * Writes the container headers (via the base class), then iterates over all
 * sub-components, writing the boundary delimiter before each. Sub-components
 * that have been parsed in code are rendered recursively; those still in raw
 * form are copied verbatim from the source stream.
 *
 * @param render_to  Output stream to write the complete multipart MIME structure to.
 * @return B_OK on success, or a negative IO error code if writing fails.
 */
status_t BMIMEMultipartMailContainer::RenderToRFC822(BPositionIO *render_to) {
	BMailComponent::RenderToRFC822(render_to);

	BString delimiter;
	delimiter << "\r\n--" << _boundary << "\r\n";

	if (_MIME_message_warning != NULL) {
		render_to->Write(_MIME_message_warning,strlen(_MIME_message_warning));
		render_to->Write("\r\n",2);
	}

	for (int32 i = 0; i < _components_in_code.CountItems() /* both have equal length, so pick one at random */; i++) {
		render_to->Write(delimiter.String(),delimiter.Length());
		if (_components_in_code.ItemAt(i) != NULL) { //---- _components_in_code has precedence

			BMailComponent *code = (BMailComponent *)_components_in_code.ItemAt(i);
			status_t status = code->RenderToRFC822(render_to); //----Easy enough
			if (status < B_OK)
				return status;
		} else {
			// copy message contents

			uint8 buffer[1024];
			ssize_t amountWritten, length;
			message_part *part = (message_part *)_components_in_raw.ItemAt(i);

			for (off_t begin = part->start; begin < part->end;
				begin += sizeof(buffer)) {
				length = (((off_t)part->end - begin) >= (off_t)sizeof(buffer))
					? sizeof(buffer) : (part->end - begin);

				_io_data->ReadAt(begin,buffer,length);
				amountWritten = render_to->Write(buffer,length);
				if (amountWritten < 0)
					return amountWritten; // IO error of some sort.
			}
		}
	}

	render_to->Write(delimiter.String(),delimiter.Length() - 2);	// strip CRLF
	render_to->Write("--\r\n",4);

	return B_OK;
}

void BMIMEMultipartMailContainer::_ReservedMultipart1() {}
void BMIMEMultipartMailContainer::_ReservedMultipart2() {}
void BMIMEMultipartMailContainer::_ReservedMultipart3() {}

void BMailContainer::_ReservedContainer1() {}
void BMailContainer::_ReservedContainer2() {}
void BMailContainer::_ReservedContainer3() {}
void BMailContainer::_ReservedContainer4() {}
