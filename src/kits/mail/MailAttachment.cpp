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
 *   Copyright 2001 Dr. Zoidberg Enterprises. All rights reserved.
 */


/**
 * @file MailAttachment.cpp
 * @brief Mail attachment classes for encoding and decoding MIME body parts.
 *
 * Implements BSimpleMailAttachment, which handles binary data with base64,
 * quoted-printable, UU-encode, or 7/8-bit transfer encodings, and
 * BAttributedMailAttachment, which wraps a file together with its BFS
 * extended attributes in a multipart/x-bfile container so that attributes
 * survive round-tripping through e-mail. Callers add attachment instances
 * to BEmailMessage via BEmailMessage::AddComponent() or Attach().
 *
 * @see BEmailMessage, BMailComponent, BMailContainer
 */


#include <MailAttachment.h>

#include <stdlib.h>
#include <stdio.h>

#include <ByteOrder.h>
#include <DataIO.h>
#include <Entry.h>
#include <File.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <String.h>

#include <AutoDeleter.h>

#include <mail_encoding.h>
#include <NodeMessage.h>


/**
 * @brief Default constructor — creates an empty attachment with no data source.
 *
 * Initialises the attachment to use base64 transfer encoding and sets the
 * Content-Disposition to "BMailAttachment". fStatus is set to B_NO_INIT
 * until a data source is provided via one of the SetTo() or SetDecodedData()
 * overloads.
 */
BSimpleMailAttachment::BSimpleMailAttachment()
	:
	fStatus(B_NO_INIT),
	_data(NULL),
	_raw_data(NULL),
	_we_own_data(false)
{
	Initialize(base64);
}


/**
 * @brief Constructs an attachment wrapping an existing BPositionIO stream.
 *
 * @param data      Pointer to the data stream; must remain valid for the
 *                  lifetime of this object unless ownership is transferred.
 * @param encoding  Transfer encoding to use when rendering (e.g. base64).
 */
BSimpleMailAttachment::BSimpleMailAttachment(BPositionIO *data,
	mail_encoding encoding)
	:
	_data(data),
	_raw_data(NULL),
	_we_own_data(false)
{
	fStatus = data == NULL ? B_BAD_VALUE : B_OK;

	Initialize(encoding);
}


/**
 * @brief Constructs an attachment from a raw memory buffer.
 *
 * Copies the supplied data into an internal BMemoryIO and takes ownership
 * of the allocation.
 *
 * @param data      Pointer to the source bytes.
 * @param length    Number of bytes to copy.
 * @param encoding  Transfer encoding to use when rendering.
 */
BSimpleMailAttachment::BSimpleMailAttachment(const void *data, size_t length,
	mail_encoding encoding)
	:
	_data(new BMemoryIO(data,length)),
	_raw_data(NULL),
	_we_own_data(true)
{
	fStatus = data == NULL ? B_BAD_VALUE : B_OK;

	Initialize(encoding);
}


/**
 * @brief Constructs an attachment from an open BFile.
 *
 * @param file             Pointer to an open BFile to read data from.
 * @param deleteWhenDone   If true, the attachment takes ownership of \a file
 *                         and deletes it when it is no longer needed.
 */
BSimpleMailAttachment::BSimpleMailAttachment(BFile *file, bool deleteWhenDone)
	:
	_data(NULL),
	_raw_data(NULL),
	_we_own_data(false)
{
	Initialize(base64);
	SetTo(file, deleteWhenDone);
}


/**
 * @brief Constructs an attachment from an entry_ref, opening the file read-only.
 *
 * @param ref  entry_ref identifying the file to attach.
 */
BSimpleMailAttachment::BSimpleMailAttachment(entry_ref *ref)
	:
	_data(NULL),
	_raw_data(NULL),
	_we_own_data(false)
{
	Initialize(base64);
	SetTo(ref);
}


/**
 * @brief Destroys the attachment, freeing data if owned.
 */
BSimpleMailAttachment::~BSimpleMailAttachment()
{
	if (_we_own_data)
		delete _data;
}


/**
 * @brief Sets default headers for a new attachment.
 *
 * Configures the transfer encoding header and sets the Content-Disposition
 * to "BMailAttachment". Called from constructors.
 *
 * @param encoding  Transfer encoding to apply (e.g. base64).
 */
void
BSimpleMailAttachment::Initialize(mail_encoding encoding)
{
	SetEncoding(encoding);
	SetHeaderField("Content-Disposition","BMailAttachment");
}


/**
 * @brief Assigns an open BFile as the attachment's data source.
 *
 * Reads the MIME type from the file's node info and sets the Content-Type
 * header accordingly. Ownership of the BFile is transferred if
 * \a deleteFileWhenDone is true.
 *
 * @param file               Pointer to the open BFile.
 * @param deleteFileWhenDone If true, the attachment takes ownership of \a file.
 * @return B_OK on success.
 */
status_t
BSimpleMailAttachment::SetTo(BFile *file, bool deleteFileWhenDone)
{
	char type[B_MIME_TYPE_LENGTH] = "application/octet-stream";

	BNodeInfo nodeInfo(file);
	if (nodeInfo.InitCheck() == B_OK)
		nodeInfo.GetType(type);

	SetHeaderField("Content-Type", type);
	// TODO: No way to get file name (see SetTo(entry_ref *))
	//SetFileName(ref->name);

	if (deleteFileWhenDone)
		SetDecodedDataAndDeleteWhenDone(file);
	else
		SetDecodedData(file);

	return fStatus = B_OK;
}


/**
 * @brief Assigns a file identified by entry_ref as the attachment's data source.
 *
 * Opens the file in read-only mode, delegates to SetTo(BFile*,bool), and
 * additionally sets the filename from the ref's name component.
 *
 * @param ref  entry_ref of the file to attach.
 * @return B_OK on success, or an error code if the file cannot be opened.
 */
status_t
BSimpleMailAttachment::SetTo(entry_ref *ref)
{
	BFile *file = new BFile(ref, B_READ_ONLY);
	if ((fStatus = file->InitCheck()) < B_OK) {
		delete file;
		return fStatus;
	}

	if (SetTo(file, true) != B_OK)
		return fStatus;

	SetFileName(ref->name);
	return fStatus = B_OK;
}


/**
 * @brief Returns the initialisation status of this attachment.
 *
 * @return B_OK if the attachment is valid, B_NO_INIT if not yet associated
 *         with any data, or B_BAD_VALUE if constructed with a NULL source.
 */
status_t
BSimpleMailAttachment::InitCheck()
{
	return fStatus;
}


/**
 * @brief Retrieves the attachment filename from the Content-Type or related headers.
 *
 * Searches Content-Type, Content-Disposition, and Content-Location headers
 * in that order for a "name" or "filename" parameter.
 *
 * @param text  Output buffer of at least B_FILE_NAME_LENGTH bytes to receive
 *              the null-terminated filename.
 * @return B_OK if a filename was found, B_NAME_NOT_FOUND otherwise.
 */
status_t
BSimpleMailAttachment::FileName(char *text)
{
	BMessage contentType;
	HeaderField("Content-Type", &contentType);

	const char *fileName = contentType.FindString("name");
	if (!fileName)
		fileName = contentType.FindString("filename");
	if (!fileName) {
		contentType.MakeEmpty();
		HeaderField("Content-Disposition", &contentType);
		fileName = contentType.FindString("name");
	}
	if (!fileName)
		fileName = contentType.FindString("filename");
	if (!fileName) {
		contentType.MakeEmpty();
		HeaderField("Content-Location", &contentType);
		fileName = contentType.FindString("unlabeled");
	}
	if (!fileName)
		return B_NAME_NOT_FOUND;

	strncpy(text, fileName, B_FILE_NAME_LENGTH);
	return B_OK;
}


/**
 * @brief Sets the filename parameter in the Content-Type header.
 *
 * Also requests UTF-8 encoding for the name parameter so that non-ASCII
 * filenames are handled correctly by rfc2047 encoding.
 *
 * @param name  Null-terminated filename string to store.
 */
void
BSimpleMailAttachment::SetFileName(const char *name)
{
	BMessage contentType;
	HeaderField("Content-Type", &contentType);

	if (contentType.ReplaceString("name", name) != B_OK)
		contentType.AddString("name", name);

	// Request that the file name header be encoded in UTF-8 if it has weird
	// characters.  If it is just a plain name, the header will appear normal.
	if (contentType.ReplaceInt32(kHeaderCharsetString, B_MAIL_UTF8_CONVERSION)
			!= B_OK)
		contentType.AddInt32(kHeaderCharsetString, B_MAIL_UTF8_CONVERSION);

	SetHeaderField ("Content-Type", &contentType);
}


/**
 * @brief Copies the decoded attachment data into \a data.
 *
 * Forces any pending lazy parse, then streams the decoded content from the
 * internal buffer into the supplied BPositionIO.
 *
 * @param data  Destination stream to receive the decoded bytes.
 * @return B_OK on success, B_IO_ERROR if no decoded data is available,
 *         B_BAD_VALUE if \a data is NULL.
 */
status_t
BSimpleMailAttachment::GetDecodedData(BPositionIO *data)
{
	ParseNow();

	if (!_data)
		return B_IO_ERROR;
	if (data == NULL)
		return B_BAD_VALUE;

	char buffer[256];
	ssize_t length;
	_data->Seek(0,SEEK_SET);

	while ((length = _data->Read(buffer, sizeof(buffer))) > 0)
		data->Write(buffer, length);

	return B_OK;
}


/**
 * @brief Returns a direct pointer to the decoded data stream.
 *
 * Forces any pending lazy parse and returns the internal BPositionIO.
 *
 * @return Pointer to the decoded data stream, or NULL if unavailable.
 */
BPositionIO *
BSimpleMailAttachment::GetDecodedData()
{
	ParseNow();
	return _data;
}


/**
 * @brief Assigns a new data source and transfers ownership.
 *
 * The previous data stream is deleted if owned. The attachment will delete
 * \a data when it is no longer needed.
 *
 * @param data  New data source; ownership is transferred to this attachment.
 * @return B_OK always.
 */
status_t
BSimpleMailAttachment::SetDecodedDataAndDeleteWhenDone(BPositionIO *data)
{
	_raw_data = NULL;

	if (_we_own_data)
		delete _data;

	_data = data;
	_we_own_data = true;

	return B_OK;
}


/**
 * @brief Assigns a new data source without taking ownership.
 *
 * The previous data stream is deleted if owned. The caller retains
 * responsibility for \a data's lifetime.
 *
 * @param data  New data source; ownership is NOT transferred.
 * @return B_OK always.
 */
status_t
BSimpleMailAttachment::SetDecodedData(BPositionIO *data)
{
	_raw_data = NULL;

	if (_we_own_data)
		delete _data;

	_data = data;
	_we_own_data = false;

	return B_OK;
}


/**
 * @brief Assigns decoded data from a raw memory buffer.
 *
 * Wraps the buffer in a BMemoryIO that is owned by this attachment.
 *
 * @param data    Pointer to the decoded data bytes.
 * @param length  Number of bytes in \a data.
 * @return B_OK always.
 */
status_t
BSimpleMailAttachment::SetDecodedData(const void *data, size_t length)
{
	_raw_data = NULL;

	if (_we_own_data)
		delete _data;

	_data = new BMemoryIO(data,length);
	_we_own_data = true;

	return B_OK;
}


/**
 * @brief Sets the Content-Transfer-Encoding and updates the corresponding header.
 *
 * Translates the mail_encoding constant into the appropriate RFC string
 * (e.g. base64, quoted-printable, 7bit, 8bit) and sets the header field.
 *
 * @param encoding  Transfer encoding constant from mail_encoding.h.
 */
void
BSimpleMailAttachment::SetEncoding(mail_encoding encoding)
{
	_encoding = encoding;

	const char *cte = NULL; //--Content Transfer Encoding
	switch (_encoding) {
		case base64:
			cte = "base64";
			break;
		case seven_bit:
		case no_encoding:
			cte = "7bit";
			break;
		case eight_bit:
			cte = "8bit";
			break;
		case uuencode:
			cte = "uuencode";
			break;
		case quoted_printable:
			cte = "quoted-printable";
			break;
		default:
			cte = "bug-not-implemented";
			break;
	}

	SetHeaderField("Content-Transfer-Encoding", cte);
}


/**
 * @brief Returns the current transfer encoding of this attachment.
 *
 * @return The mail_encoding constant in use.
 */
mail_encoding
BSimpleMailAttachment::Encoding()
{
	return _encoding;
}


/**
 * @brief Initialises the attachment from an RFC 822 stream at the current position.
 *
 * Reads and parses MIME headers, then records the byte range of the encoded
 * payload for lazy decoding. If \a parseNow is true, the payload is
 * immediately decoded into memory.
 *
 * @param data       Stream positioned at the start of this MIME part.
 * @param length     Number of bytes available for this part.
 * @param parseNow   If true, decode the payload immediately.
 * @return B_OK on success, B_ERROR if the payload overruns the given length.
 */
status_t
BSimpleMailAttachment::SetToRFC822(BPositionIO *data, size_t length,
	bool parseNow)
{
	//---------Massive memory squandering!---ALERT!----------
	if (_we_own_data)
		delete _data;

	off_t position = data->Position();
	BMailComponent::SetToRFC822(data, length, parseNow);

	// this actually happens...
	if (data->Position() - position > (off_t)length)
		return B_ERROR;

	length -= (data->Position() - position);

	_raw_data = data;
	_raw_length = length;
	_raw_offset = data->Position();

	BString encoding = HeaderField("Content-Transfer-Encoding");
	if (encoding.IFindFirst("base64") >= 0)
		_encoding = base64;
	else if (encoding.IFindFirst("quoted-printable") >= 0)
		_encoding = quoted_printable;
	else if (encoding.IFindFirst("uuencode") >= 0)
		_encoding = uuencode;
	else if (encoding.IFindFirst("7bit") >= 0)
		_encoding = seven_bit;
	else if (encoding.IFindFirst("8bit") >= 0)
		_encoding = eight_bit;
	else
		_encoding = no_encoding;

	if (parseNow)
		ParseNow();

	return B_OK;
}


/**
 * @brief Decodes the raw MIME payload into an in-memory buffer.
 *
 * Reads the raw encoded bytes from the source stream, decodes them using
 * the detected transfer encoding, and stores the result in a new BMallocIO.
 * After this call _raw_data is set to NULL indicating the data is decoded.
 */
void
BSimpleMailAttachment::ParseNow()
{
	if (_raw_data == NULL || _raw_length == 0)
		return;

	_raw_data->Seek(_raw_offset, SEEK_SET);

	char *src = (char *)malloc(_raw_length);
	if (src == NULL)
		return;

	size_t size = _raw_length;

	size = _raw_data->Read(src, _raw_length);

	BMallocIO *buffer = new BMallocIO;
	buffer->SetSize(size);
		// 8bit is *always* more efficient than an encoding, so the buffer
		// will *never* be larger than before

	size = decode(_encoding,(char *)(buffer->Buffer()),src,size,0);
	free(src);

	buffer->SetSize(size);

	_data = buffer;
	_we_own_data = true;

	_raw_data = NULL;

	return;
}


/**
 * @brief Encodes the attachment data and writes the complete MIME part to a stream.
 *
 * Forces lazy parse, then encodes the decoded data using the configured
 * transfer encoding and writes it to \a renderTo following the MIME headers.
 *
 * @param renderTo  Output stream to write the encoded MIME part to.
 * @return B_OK on success, B_NO_MEMORY if allocation fails, or an IO error code.
 */
status_t
BSimpleMailAttachment::RenderToRFC822(BPositionIO *renderTo)
{
	ParseNow();
	BMailComponent::RenderToRFC822(renderTo);
	//---------Massive memory squandering!---ALERT!----------

	_data->Seek(0, SEEK_END);
	off_t size = _data->Position();
	char *src = (char *)malloc(size);
	if (src == NULL)
		return B_NO_MEMORY;

	MemoryDeleter sourceDeleter(src);

	_data->Seek(0, SEEK_SET);

	ssize_t read = _data->Read(src, size);
	if (read < B_OK)
		return read;

	// The encoded text will never be more than twice as large with any
	// conceivable encoding.  But just in case, there's a function call which
	// will tell us how much space is needed.
	ssize_t destSize = max_encoded_length(_encoding, read);
	if (destSize < B_OK) // Invalid encodings like uuencode rejected here.
		return destSize;
	char *dest = (char *)malloc(destSize);
	if (dest == NULL)
		return B_NO_MEMORY;

	MemoryDeleter destinationDeleter(dest);

	destSize = encode (_encoding, dest, src, read, false /* headerMode */);
	if (destSize < B_OK)
		return destSize;

	if (destSize > 0)
		read = renderTo->Write(dest, destSize);

	return read > 0 ? B_OK : read;
}


//	#pragma mark -


/**
 * @brief Default constructor — creates an empty attributed attachment.
 *
 * The container and sub-attachments are not initialised until Initialize()
 * or one of the SetTo() overloads is called.
 */
BAttributedMailAttachment::BAttributedMailAttachment()
	:
	fContainer(NULL),
	fStatus(B_NO_INIT),
	_data(NULL),
	_attributes_attach(NULL)
{
}


/**
 * @brief Constructs an attributed attachment from an open BFile.
 *
 * @param file             Open BFile; BFS attributes are read from it.
 * @param deleteWhenDone   If true, takes ownership of \a file.
 */
BAttributedMailAttachment::BAttributedMailAttachment(BFile *file,
	bool deleteWhenDone)
	:
	fContainer(NULL),
	_data(NULL),
	_attributes_attach(NULL)
{
	SetTo(file, deleteWhenDone);
}


/**
 * @brief Constructs an attributed attachment from an entry_ref.
 *
 * @param ref  entry_ref of the file whose data and BFS attributes to include.
 */
BAttributedMailAttachment::BAttributedMailAttachment(entry_ref *ref)
	:
	fContainer(NULL),
	_data(NULL),
	_attributes_attach(NULL)
{
	SetTo(ref);
}


/**
 * @brief Destroys the attributed attachment and its container.
 *
 * The container deletes _data and _attributes_attach.
 */
BAttributedMailAttachment::~BAttributedMailAttachment()
{
	// Our SimpleAttachments are deleted by fContainer
	delete fContainer;
}


/**
 * @brief Sets up the multipart/x-bfile container structure.
 *
 * Creates a BMIMEMultipartMailContainer with two sub-parts: one for the file
 * data and one for the serialised BFS attributes. Also sets the appropriate
 * Content-Type and Content-Disposition headers on the container.
 *
 * @return B_OK on success, or an error code if allocation fails.
 */
status_t
BAttributedMailAttachment::Initialize()
{
	// _data & _attributes_attach will be deleted by the container
	delete fContainer;

	fContainer = new BMIMEMultipartMailContainer("++++++BFile++++++");

	_data = new BSimpleMailAttachment();
	fContainer->AddComponent(_data);

	_attributes_attach = new BSimpleMailAttachment();
	_attributes.MakeEmpty();
	_attributes_attach->SetHeaderField("Content-Type",
		"application/x-be_attribute; name=\"BeOS Attributes\"");
	fContainer->AddComponent(_attributes_attach);

	fContainer->SetHeaderField("Content-Type", "multipart/x-bfile");
	fContainer->SetHeaderField("Content-Disposition", "BMailAttachment");

	// also set the header fields of this component, in case someone asks
	SetHeaderField("Content-Type", "multipart/x-bfile");
	SetHeaderField("Content-Disposition", "BMailAttachment");

	return B_OK;
}


/**
 * @brief Assigns an open BFile as this attachment's data source.
 *
 * Reads the file's BFS attributes into _attributes, delegates file data to
 * _data, and generates a random MIME boundary string.
 *
 * @param file             Open BFile to read data and attributes from.
 * @param deleteFileWhenDone  If true, takes ownership of \a file.
 * @return B_OK on success, or B_BAD_VALUE if \a file is NULL.
 */
status_t
BAttributedMailAttachment::SetTo(BFile *file, bool deleteFileWhenDone)
{
	if (file == NULL)
		return fStatus = B_BAD_VALUE;

	if ((fStatus = Initialize()) < B_OK)
		return fStatus;

	_attributes << *file;

	if ((fStatus = _data->SetTo(file, deleteFileWhenDone)) < B_OK)
		return fStatus;

	// Set boundary

	// Also, we have the make up the boundary out of whole cloth
	// This is likely to give a completely random string
	BString boundary;
	boundary << "BFile--" << ((long)file ^ time(NULL)) << "-"
		<< ~((long)file ^ (long)&fStatus ^ (long)&_attributes) << "--";
	fContainer->SetBoundary(boundary.String());

	return fStatus = B_OK;
}


/**
 * @brief Assigns a file identified by entry_ref as this attachment's source.
 *
 * Reads BFS attributes from the node, opens the file via _data->SetTo(),
 * and constructs a random boundary incorporating the filename.
 *
 * @param ref  entry_ref of the file to attach.
 * @return B_OK on success, or B_BAD_VALUE if \a ref is NULL.
 */
status_t
BAttributedMailAttachment::SetTo(entry_ref *ref)
{
	if (ref == NULL)
		return fStatus = B_BAD_VALUE;

	if ((fStatus = Initialize()) < B_OK)
		return fStatus;

	BNode node(ref);
	if ((fStatus = node.InitCheck()) < B_OK)
		return fStatus;

	_attributes << node;

	if ((fStatus = _data->SetTo(ref)) < B_OK)
		return fStatus;

	// Set boundary

	// This is likely to give a completely random string
	BString boundary;
	char buffer[512];
	strcpy(buffer, ref->name);
	for (int32 i = strlen(buffer); i-- > 0;) {
		if (buffer[i] & 0x80)
			buffer[i] = 'x';
		else if (buffer[i] == ' ' || buffer[i] == ':')
			buffer[i] = '_';
	}
	buffer[32] = '\0';
	boundary << "BFile-" << buffer << "--" << ((long)_data ^ time(NULL))
		<< "-" << ~((long)_data ^ (long)&buffer ^ (long)&_attributes)
		<< "--";
	fContainer->SetBoundary(boundary.String());

	return fStatus = B_OK;
}


/**
 * @brief Returns the initialisation status of this attributed attachment.
 *
 * @return B_OK if valid, B_NO_INIT if not yet initialised.
 */
status_t
BAttributedMailAttachment::InitCheck()
{
	return fStatus;
}


/**
 * @brief Saves the attachment file and its BFS attributes to /tmp.
 *
 * Reconstructs the file under /tmp using the embedded filename, writes the
 * BFS attributes, and sets \a entry to point at the new file.
 *
 * @param entry  Output BEntry set to the path of the saved file.
 */
void
BAttributedMailAttachment::SaveToDisk(BEntry *entry)
{
	BString path = "/tmp/";
	char name[B_FILE_NAME_LENGTH] = "";
	_data->FileName(name);
	path << name;

	BFile file(path.String(), B_READ_WRITE | B_CREATE_FILE);
	(BNode&)file << _attributes;
	_data->GetDecodedData(&file);
	file.Sync();

	entry->SetTo(path.String());
}


/**
 * @brief Sets the transfer encoding for both the data and attributes sub-parts.
 *
 * @param encoding  Transfer encoding to apply to both sub-attachments.
 */
void
BAttributedMailAttachment::SetEncoding(mail_encoding encoding)
{
	_data->SetEncoding(encoding);
	if (_attributes_attach != NULL)
		_attributes_attach->SetEncoding(encoding);
}


/**
 * @brief Returns the transfer encoding of the data sub-attachment.
 *
 * @return The mail_encoding used by the data component.
 */
mail_encoding
BAttributedMailAttachment::Encoding()
{
	return _data->Encoding();
}


/**
 * @brief Retrieves the filename of this attachment from the data sub-part.
 *
 * @param name  Output buffer of at least B_FILE_NAME_LENGTH bytes.
 * @return B_OK if found, B_NAME_NOT_FOUND otherwise.
 */
status_t
BAttributedMailAttachment::FileName(char *name)
{
	return _data->FileName(name);
}


/**
 * @brief Sets the filename on the data sub-attachment.
 *
 * @param name  Null-terminated filename string.
 */
void
BAttributedMailAttachment::SetFileName(const char *name)
{
	_data->SetFileName(name);
}


/**
 * @brief Retrieves the decoded file data and restores BFS attributes.
 *
 * If \a data is a BNode, the cached BFS attributes are written back to it.
 * The raw file bytes are then read from the data sub-attachment.
 *
 * @param data  Destination stream or BNode to receive decoded data and attributes.
 * @return B_OK always.
 */
status_t
BAttributedMailAttachment::GetDecodedData(BPositionIO *data)
{
	BNode *node = dynamic_cast<BNode *>(data);
	if (node != NULL)
		*node << _attributes;

	_data->GetDecodedData(data);
	return B_OK;
}


/**
 * @brief Assigns decoded data and, if \a data is a BNode, captures its attributes.
 *
 * @param data  Data source; BFS attributes are captured if it is a BNode.
 * @return B_OK always.
 */
status_t
BAttributedMailAttachment::SetDecodedData(BPositionIO *data)
{
	BNode *node = dynamic_cast<BNode *>(data);
	if (node != NULL)
		_attributes << *node;

	_data->SetDecodedData(data);
	return B_OK;
}


/**
 * @brief Parses a multipart/x-bfile MIME stream to restore data and attributes.
 *
 * Initialises the container, parses the RFC 822 stream, validates the MIME
 * type, extracts the data and attribute sub-parts, and deserialises the
 * big-endian attribute binary into the internal _attributes BMessage.
 *
 * @param data       RFC 822 stream positioned at the start of this part.
 * @param length     Number of bytes available for this part.
 * @param parseNow   If true, forces immediate decoding of sub-part data.
 * @return B_OK on success, or an error code if the part is malformed.
 */
status_t
BAttributedMailAttachment::SetToRFC822(BPositionIO *data, size_t length,
	bool parseNow)
{
	status_t err = Initialize();
	if (err < B_OK)
		return err;

	err = fContainer->SetToRFC822(data, length, parseNow);
	if (err < B_OK)
		return err;

	BMimeType type;
	fContainer->MIMEType(&type);
	if (strcmp(type.Type(), "multipart/x-bfile") != 0)
		return B_BAD_TYPE;

	// get data and attributes
	if ((_data = dynamic_cast<BSimpleMailAttachment *>(
			fContainer->GetComponent(0))) == NULL)
		return B_BAD_VALUE;

	if (parseNow) {
		// Force it to make a copy of the data. Needed for forwarding
		// messages hack.
		_data->GetDecodedData();
	}

	if ((_attributes_attach = dynamic_cast<BSimpleMailAttachment *>(
				fContainer->GetComponent(1))) == NULL
		|| _attributes_attach->GetDecodedData() == NULL)
		return B_OK;

	// Convert the attribute binary attachment into a convenient easy to use
	// BMessage.

	int32 len
		= ((BMallocIO *)(_attributes_attach->GetDecodedData()))->BufferLength();
	char *start = (char *)malloc(len);
	if (start == NULL)
		return B_NO_MEMORY;

	MemoryDeleter deleter(start);

	if (_attributes_attach->GetDecodedData()->ReadAt(0, start, len) < len)
		return B_IO_ERROR;

	int32 index = 0;
	while (index < len) {
		char *name = &start[index];
		index += strlen(name) + 1;

		type_code code;
		memcpy(&code, &start[index], sizeof(type_code));
		code = B_BENDIAN_TO_HOST_INT32(code);
		index += sizeof(type_code);

		int64 buf_length;
		memcpy(&buf_length, &start[index], sizeof(buf_length));
		buf_length = B_BENDIAN_TO_HOST_INT64(buf_length);
		index += sizeof(buf_length);

		swap_data(code, &start[index], buf_length, B_SWAP_BENDIAN_TO_HOST);
		_attributes.AddData(name, code, &start[index], buf_length);
		index += buf_length;
	}

	return B_OK;
}


/**
 * @brief Serialises the BFS attributes and renders the full multipart/x-bfile part.
 *
 * Serialises all entries from the internal _attributes BMessage into
 * big-endian binary, stores the result in _attributes_attach, and delegates
 * rendering to the container.
 *
 * @param renderTo  Output stream to write the encoded MIME structure to.
 * @return B_OK on success, B_NO_MEMORY if allocation fails, or an IO error code.
 */
status_t
BAttributedMailAttachment::RenderToRFC822(BPositionIO *renderTo)
{
	BMallocIO *io = new BMallocIO;

#if defined(HAIKU_TARGET_PLATFORM_DANO)
	const
#endif
	char *name;
	type_code type;
	for (int32 i = 0; _attributes.GetInfo(B_ANY_TYPE, i, &name, &type) == B_OK;
			i++) {
		const void *data;
		ssize_t dataLen;
		_attributes.FindData(name, type, &data, &dataLen);
		io->Write(name, strlen(name) + 1);

		type_code swappedType = B_HOST_TO_BENDIAN_INT32(type);
		io->Write(&swappedType, sizeof(type_code));

		int64 length, swapped;
		length = dataLen;
		swapped = B_HOST_TO_BENDIAN_INT64(length);
		io->Write(&swapped,sizeof(int64));

		void *buffer = malloc(dataLen);
		if (buffer == NULL) {
			delete io;
			return B_NO_MEMORY;
		}
		memcpy(buffer, data, dataLen);
		swap_data(type, buffer, dataLen, B_SWAP_HOST_TO_BENDIAN);
		io->Write(buffer, dataLen);
		free(buffer);
	}
	if (_attributes_attach == NULL)
		_attributes_attach = new BSimpleMailAttachment;

	_attributes_attach->SetDecodedDataAndDeleteWhenDone(io);

	return fContainer->RenderToRFC822(renderTo);
}


/**
 * @brief Returns the MIME type of the data sub-attachment.
 *
 * @param mime  Output BMimeType to receive the type.
 * @return B_OK on success.
 */
status_t
BAttributedMailAttachment::MIMEType(BMimeType *mime)
{
	return _data->MIMEType(mime);
}


// #pragma mark - The reserved function stubs


void BMailAttachment::_ReservedAttachment1() {}
void BMailAttachment::_ReservedAttachment2() {}
void BMailAttachment::_ReservedAttachment3() {}
void BMailAttachment::_ReservedAttachment4() {}

void BSimpleMailAttachment::_ReservedSimple1() {}
void BSimpleMailAttachment::_ReservedSimple2() {}
void BSimpleMailAttachment::_ReservedSimple3() {}

void BAttributedMailAttachment::_ReservedAttributed1() {}
void BAttributedMailAttachment::_ReservedAttributed2() {}
void BAttributedMailAttachment::_ReservedAttributed3() {}
