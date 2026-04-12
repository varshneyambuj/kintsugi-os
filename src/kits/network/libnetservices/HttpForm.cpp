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
 *   Copyright 2010-2013 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christophe Huriaux, c.huriaux@gmail.com
 */


/**
 * @file HttpForm.cpp
 * @brief Implementation of BHttpForm and BHttpFormData for HTTP form encoding.
 *
 * BHttpForm manages an ordered collection of BHttpFormData fields and can
 * serialise them either as URL-encoded (application/x-www-form-urlencoded) or
 * multipart/form-data, which is required when file uploads are included. The
 * BHttpForm::Iterator class provides sequential access to fields for streaming
 * multipart uploads without buffering the entire body in memory.
 *
 * @see BHttpRequest
 */


#include <HttpForm.h>

#include <cstdlib>
#include <cstring>
#include <ctime>

#include <File.h>
#include <NodeInfo.h>
#include <TypeConstants.h>
#include <Url.h>


static int32 kBoundaryRandomSize = 16;

using namespace std;
using namespace BPrivate::Network;


// #pragma mark - BHttpFormData


/**
 * @brief Default constructor — creates an empty string-typed form field.
 */
BHttpFormData::BHttpFormData()
	:
	fDataType(B_HTTPFORM_STRING),
	fCopiedBuffer(false),
	fFileMark(false),
	fBufferValue(NULL),
	fBufferSize(0)
{
}


/**
 * @brief Construct a named string field with the given value.
 *
 * @param name   The form field name.
 * @param value  The string value for this field.
 */
BHttpFormData::BHttpFormData(const BString& name, const BString& value)
	:
	fDataType(B_HTTPFORM_STRING),
	fCopiedBuffer(false),
	fFileMark(false),
	fName(name),
	fStringValue(value),
	fBufferValue(NULL),
	fBufferSize(0)
{
}


/**
 * @brief Construct a named file field pointing to a filesystem path.
 *
 * The content type is determined at serialisation time from the file's
 * node info; the file is not opened at construction.
 *
 * @param name  The form field name.
 * @param file  The BPath of the file to upload.
 */
BHttpFormData::BHttpFormData(const BString& name, const BPath& file)
	:
	fDataType(B_HTTPFORM_FILE),
	fCopiedBuffer(false),
	fFileMark(false),
	fName(name),
	fPathValue(file),
	fBufferValue(NULL),
	fBufferSize(0)
{
}


/**
 * @brief Construct a named buffer field referencing an existing memory region.
 *
 * The caller retains ownership of \a buffer unless CopyBuffer() is later
 * called to transfer ownership to this object.
 *
 * @param name    The form field name.
 * @param buffer  Pointer to the raw data buffer.
 * @param size    Number of bytes in the buffer.
 */
BHttpFormData::BHttpFormData(const BString& name, const void* buffer,
	ssize_t size)
	:
	fDataType(B_HTTPFORM_BUFFER),
	fCopiedBuffer(false),
	fFileMark(false),
	fName(name),
	fBufferValue(buffer),
	fBufferSize(size)
{
}


/**
 * @brief Copy constructor — delegates to the assignment operator.
 *
 * @param other  The source BHttpFormData to copy.
 */
BHttpFormData::BHttpFormData(const BHttpFormData& other)
	:
	fCopiedBuffer(false),
	fFileMark(false),
	fBufferValue(NULL),
	fBufferSize(0)
{
	*this = other;
}


/**
 * @brief Destructor — frees the buffer if it was internally copied.
 */
BHttpFormData::~BHttpFormData()
{
	if (fCopiedBuffer)
		delete[] reinterpret_cast<const char*>(fBufferValue);
}


// #pragma mark - Retrieve data informations


/**
 * @brief Return whether the field is properly initialised.
 *
 * Buffer fields are valid only when the buffer pointer is non-NULL; all other
 * field types are always considered valid.
 *
 * @return true if the field is ready for use, false otherwise.
 */
bool
BHttpFormData::InitCheck() const
{
	if (fDataType == B_HTTPFORM_BUFFER)
		return fBufferValue != NULL;

	return true;
}


/**
 * @brief Return the field name.
 *
 * @return A const reference to the field name BString.
 */
const BString&
BHttpFormData::Name() const
{
	return fName;
}


/**
 * @brief Return the string value (valid only for B_HTTPFORM_STRING fields).
 *
 * @return A const reference to the string value BString.
 */
const BString&
BHttpFormData::String() const
{
	return fStringValue;
}


/**
 * @brief Return the file path (valid only for B_HTTPFORM_FILE fields).
 *
 * @return A const reference to the BPath representing the file.
 */
const BPath&
BHttpFormData::File() const
{
	return fPathValue;
}


/**
 * @brief Return a pointer to the raw buffer (valid only for B_HTTPFORM_BUFFER fields).
 *
 * @return A const void pointer to the buffer data, or NULL if not a buffer field.
 */
const void*
BHttpFormData::Buffer() const
{
	return fBufferValue;
}


/**
 * @brief Return the size of the raw buffer in bytes.
 *
 * @return The buffer size, or 0 if the field is not a buffer field.
 */
ssize_t
BHttpFormData::BufferSize() const
{
	return fBufferSize;
}


/**
 * @brief Return whether this field is marked as a file upload.
 *
 * @return true if MarkAsFile() has been called on this field, false otherwise.
 */
bool
BHttpFormData::IsFile() const
{
	return fFileMark;
}


/**
 * @brief Return the file name override set by MarkAsFile().
 *
 * @return A const reference to the filename BString, empty if not marked.
 */
const BString&
BHttpFormData::Filename() const
{
	return fFilename;
}


/**
 * @brief Return the MIME type override set by MarkAsFile().
 *
 * @return A const reference to the MIME type BString, empty if not marked.
 */
const BString&
BHttpFormData::MimeType() const
{
	return fMimeType;
}


/**
 * @brief Return the data type of this field.
 *
 * @return One of the form_content_type constants: B_HTTPFORM_STRING,
 *         B_HTTPFORM_FILE, B_HTTPFORM_BUFFER, or B_HTTPFORM_UNKNOWN.
 */
form_content_type
BHttpFormData::Type() const
{
	return fDataType;
}


// #pragma mark - Change behavior


/**
 * @brief Mark a string or buffer field as representing a file upload.
 *
 * This causes the field to be encoded with a Content-Disposition filename
 * parameter in multipart/form-data output. The form type is automatically
 * switched to multipart when this is called via BHttpForm::MarkAsFile().
 *
 * @param filename  The filename to advertise in the Content-Disposition header.
 * @param mimeType  The MIME content type for the file data.
 * @return B_OK on success, or B_ERROR if the field type is FILE or UNKNOWN.
 */
status_t
BHttpFormData::MarkAsFile(const BString& filename, const BString& mimeType)
{
	if (fDataType == B_HTTPFORM_UNKNOWN || fDataType == B_HTTPFORM_FILE)
		return B_ERROR;

	fFilename = filename;
	fMimeType = mimeType;
	fFileMark = true;

	return B_OK;
}


/**
 * @brief Remove the file-upload mark from this field.
 *
 * Clears the filename and MIME type overrides and resets the IsFile() flag.
 */
void
BHttpFormData::UnmarkAsFile()
{
	fFilename.Truncate(0, true);
	fMimeType.Truncate(0, true);
	fFileMark = false;
}


/**
 * @brief Make an internal copy of the referenced buffer data.
 *
 * After this call the object owns the buffer and will free it in the
 * destructor, so the original caller may release their copy.
 *
 * @return B_OK on success, B_ERROR if this is not a buffer field, or
 *         B_NO_MEMORY if the allocation fails.
 */
status_t
BHttpFormData::CopyBuffer()
{
	if (fDataType != B_HTTPFORM_BUFFER)
		return B_ERROR;

	char* copiedBuffer = new(std::nothrow) char[fBufferSize];
	if (copiedBuffer == NULL)
		return B_NO_MEMORY;

	memcpy(copiedBuffer, fBufferValue, fBufferSize);
	fBufferValue = copiedBuffer;
	fCopiedBuffer = true;

	return B_OK;
}


/**
 * @brief Assignment operator — copies all fields from \a other.
 *
 * If \a other had an internally copied buffer, CopyBuffer() is called on
 * this object to ensure independent ownership.
 *
 * @param other  The source BHttpFormData to copy from.
 * @return A reference to this object.
 */
BHttpFormData&
BHttpFormData::operator=(const BHttpFormData& other)
{
	fDataType = other.fDataType;
	fCopiedBuffer = false;
	fFileMark = other.fFileMark;
	fName = other.fName;
	fStringValue = other.fStringValue;
	fPathValue = other.fPathValue;
	fBufferValue = other.fBufferValue;
	fBufferSize = other.fBufferSize;
	fFilename = other.fFilename;
	fMimeType = other.fMimeType;

	if (other.fCopiedBuffer)
		CopyBuffer();

	return *this;
}


// #pragma mark - BHttpForm


/**
 * @brief Default constructor — creates an empty URL-encoded form.
 */
BHttpForm::BHttpForm()
	:
	fType(B_HTTP_FORM_URL_ENCODED)
{
}


/**
 * @brief Copy constructor — copies all fields and the form type.
 *
 * @param other  The source BHttpForm to copy.
 */
BHttpForm::BHttpForm(const BHttpForm& other)
	:
	fFields(other.fFields),
	fType(other.fType),
	fMultipartBoundary(other.fMultipartBoundary)
{
}


/**
 * @brief Construct a BHttpForm by parsing a URL-encoded query string.
 *
 * @param formString  The URL-encoded name=value pairs to parse.
 */
BHttpForm::BHttpForm(const BString& formString)
	:
	fType(B_HTTP_FORM_URL_ENCODED)
{
	ParseString(formString);
}


/**
 * @brief Destructor — clears all stored form fields.
 */
BHttpForm::~BHttpForm()
{
	Clear();
}


// #pragma mark - Form string parsing


/**
 * @brief Parse a URL-encoded query string and add fields to this form.
 *
 * Repeatedly calls _ExtractNameValuePair() to process each "name=value"
 * segment delimited by '&' characters.
 *
 * @param formString  The URL-encoded form data string to parse.
 */
void
BHttpForm::ParseString(const BString& formString)
{
	int32 index = 0;

	while (index < formString.Length())
		_ExtractNameValuePair(formString, &index);
}


/**
 * @brief Serialise the form to its wire-format string representation.
 *
 * For URL-encoded forms, returns a percent-encoded "name=value&..." string.
 * For multipart forms, returns the complete multipart body including part
 * headers, field data, and the final boundary terminator. File fields are
 * read entirely into memory at this point.
 *
 * @return The serialised form body as a BString.
 */
BString
BHttpForm::RawData() const
{
	BString result;

	if (fType == B_HTTP_FORM_URL_ENCODED) {
		for (FormStorage::const_iterator it = fFields.begin();
			it != fFields.end(); it++) {
			const BHttpFormData* currentField = &it->second;

			switch (currentField->Type()) {
				case B_HTTPFORM_UNKNOWN:
					break;

				case B_HTTPFORM_STRING:
					result << '&' << BUrl::UrlEncode(currentField->Name())
						<< '=' << BUrl::UrlEncode(currentField->String());
					break;

				case B_HTTPFORM_FILE:
					break;

				case B_HTTPFORM_BUFFER:
					// Send the buffer only if its not marked as a file
					if (!currentField->IsFile()) {
						result << '&' << BUrl::UrlEncode(currentField->Name())
							<< '=';
						result.Append(
							reinterpret_cast<const char*>(currentField->Buffer()),
							currentField->BufferSize());
					}
					break;
			}
		}

		result.Remove(0, 1);
	} else if (fType == B_HTTP_FORM_MULTIPART) {
		//  Very slow and memory consuming method since we're caching the
		// file content, this should be preferably handled by the protocol
		for (FormStorage::const_iterator it = fFields.begin();
			it != fFields.end(); it++) {
			const BHttpFormData* currentField = &it->second;
			result << _GetMultipartHeader(currentField);

			switch (currentField->Type()) {
				case B_HTTPFORM_UNKNOWN:
					break;

				case B_HTTPFORM_STRING:
					result << currentField->String();
					break;

				case B_HTTPFORM_FILE:
				{
					BFile upFile(currentField->File().Path(), B_READ_ONLY);
					char readBuffer[1024];
					ssize_t readSize;

					readSize = upFile.Read(readBuffer, 1024);

					while (readSize > 0) {
						result.Append(readBuffer, readSize);
						readSize = upFile.Read(readBuffer, 1024);
					}
					break;
				}

				case B_HTTPFORM_BUFFER:
					result.Append(
						reinterpret_cast<const char*>(currentField->Buffer()),
						currentField->BufferSize());
					break;
			}

			result << "\r\n";
		}

		result << "--" << fMultipartBoundary << "--\r\n";
	}

	return result;
}


// #pragma mark - Form add


/**
 * @brief Add a named string field to the form.
 *
 * @param fieldName  The name of the form field.
 * @param value      The string value to store.
 * @return B_OK on success, or B_ERROR if field initialisation fails.
 */
status_t
BHttpForm::AddString(const BString& fieldName, const BString& value)
{
	BHttpFormData formData(fieldName, value);
	if (!formData.InitCheck())
		return B_ERROR;

	fFields.insert(pair<BString, BHttpFormData>(fieldName, formData));
	return B_OK;
}


/**
 * @brief Add a named integer field by converting the value to a string.
 *
 * @param fieldName  The name of the form field.
 * @param value      The integer value to encode.
 * @return B_OK on success, or an error code from AddString().
 */
status_t
BHttpForm::AddInt(const BString& fieldName, int32 value)
{
	BString strValue;
	strValue << value;

	return AddString(fieldName, strValue);
}


/**
 * @brief Add a named file field and switch the form to multipart encoding.
 *
 * The file is not opened immediately; it will be read when RawData() or the
 * iterator serialises the field. The form type is automatically changed to
 * B_HTTP_FORM_MULTIPART if it is not already.
 *
 * @param fieldName  The name of the form field.
 * @param file       The BPath of the file to upload.
 * @return B_OK on success, or B_ERROR if field initialisation fails.
 */
status_t
BHttpForm::AddFile(const BString& fieldName, const BPath& file)
{
	BHttpFormData formData(fieldName, file);
	if (!formData.InitCheck())
		return B_ERROR;

	fFields.insert(pair<BString, BHttpFormData>(fieldName, formData));

	if (fType != B_HTTP_FORM_MULTIPART)
		SetFormType(B_HTTP_FORM_MULTIPART);
	return B_OK;
}


/**
 * @brief Add a named raw-buffer field referencing external memory.
 *
 * The caller retains ownership of \a buffer. Use AddBufferCopy() if the
 * lifetime of the form may exceed the lifetime of the buffer.
 *
 * @param fieldName  The name of the form field.
 * @param buffer     Pointer to the data to send.
 * @param size       Number of bytes in the buffer.
 * @return B_OK on success, or B_ERROR if field initialisation fails.
 */
status_t
BHttpForm::AddBuffer(const BString& fieldName, const void* buffer,
	ssize_t size)
{
	BHttpFormData formData(fieldName, buffer, size);
	if (!formData.InitCheck())
		return B_ERROR;

	fFields.insert(pair<BString, BHttpFormData>(fieldName, formData));
	return B_OK;
}


/**
 * @brief Add a named raw-buffer field and copy the buffer data internally.
 *
 * Unlike AddBuffer(), the form takes ownership of an internal copy of the
 * data so the caller may free \a buffer immediately after this call.
 *
 * @param fieldName  The name of the form field.
 * @param buffer     Pointer to the data to copy and send.
 * @param size       Number of bytes in the buffer.
 * @return B_OK on success, B_ERROR if field initialisation fails, or
 *         B_NO_MEMORY if the internal buffer copy allocation fails.
 */
status_t
BHttpForm::AddBufferCopy(const BString& fieldName, const void* buffer,
	ssize_t size)
{
	BHttpFormData formData(fieldName, buffer, size);
	if (!formData.InitCheck())
		return B_ERROR;

	// Copy the buffer of the inserted form data copy to
	// avoid an unneeded copy of the buffer upon insertion
	pair<FormStorage::iterator, bool> insertResult
		= fFields.insert(pair<BString, BHttpFormData>(fieldName, formData));

	return insertResult.first->second.CopyBuffer();
}


// #pragma mark - Mark a field as a filename


/**
 * @brief Mark an existing field as a file-upload field with a MIME type.
 *
 * The form type is automatically changed to multipart if necessary.
 *
 * @param fieldName  The name of the field to mark.
 * @param filename   The filename to advertise in the Content-Disposition header.
 * @param mimeType   The MIME content type for the field data.
 */
void
BHttpForm::MarkAsFile(const BString& fieldName, const BString& filename,
	const BString& mimeType)
{
	FormStorage::iterator it = fFields.find(fieldName);

	if (it == fFields.end())
		return;

	it->second.MarkAsFile(filename, mimeType);
	if (fType != B_HTTP_FORM_MULTIPART)
		SetFormType(B_HTTP_FORM_MULTIPART);
}


/**
 * @brief Mark an existing field as a file-upload field without a MIME type.
 *
 * @param fieldName  The name of the field to mark.
 * @param filename   The filename to advertise in the Content-Disposition header.
 */
void
BHttpForm::MarkAsFile(const BString& fieldName, const BString& filename)
{
	MarkAsFile(fieldName, filename, "");
}


/**
 * @brief Remove the file-upload mark from a named field.
 *
 * @param fieldName  The name of the field to unmark.
 */
void
BHttpForm::UnmarkAsFile(const BString& fieldName)
{
	FormStorage::iterator it = fFields.find(fieldName);

	if (it == fFields.end())
		return;

	it->second.UnmarkAsFile();
}


// #pragma mark - Change form type


/**
 * @brief Set the encoding type of the form.
 *
 * Switching to B_HTTP_FORM_MULTIPART generates a random boundary string.
 *
 * @param type  The desired form encoding: B_HTTP_FORM_URL_ENCODED or
 *              B_HTTP_FORM_MULTIPART.
 */
void
BHttpForm::SetFormType(form_type type)
{
	fType = type;

	if (fType == B_HTTP_FORM_MULTIPART)
		_GenerateMultipartBoundary();
}


// #pragma mark - Form test


/**
 * @brief Test whether a field with the given name exists in the form.
 *
 * @param name  The field name to look up.
 * @return true if the field exists, false otherwise.
 */
bool
BHttpForm::HasField(const BString& name) const
{
	return (fFields.find(name) != fFields.end());
}


// #pragma mark - Form retrieve


/**
 * @brief Build and return the multipart header for a specific named field.
 *
 * @param fieldName  The name of the field whose header is needed.
 * @return The complete Content-Disposition (and optional Content-Type) header
 *         block for the field, or an empty string if the field does not exist.
 */
BString
BHttpForm::GetMultipartHeader(const BString& fieldName) const
{
	FormStorage::const_iterator it = fFields.find(fieldName);

	if (it == fFields.end())
		return BString("");

	return _GetMultipartHeader(&it->second);
}


/**
 * @brief Return the current form encoding type.
 *
 * @return B_HTTP_FORM_URL_ENCODED or B_HTTP_FORM_MULTIPART.
 */
form_type
BHttpForm::GetFormType() const
{
	return fType;
}


/**
 * @brief Return the multipart boundary string.
 *
 * @return A const reference to the boundary BString, empty for URL-encoded forms.
 */
const BString&
BHttpForm::GetMultipartBoundary() const
{
	return fMultipartBoundary;
}


/**
 * @brief Return the multipart closing boundary line.
 *
 * @return A BString of the form "--<boundary>--\r\n".
 */
BString
BHttpForm::GetMultipartFooter() const
{
	BString result = "--";
	result << fMultipartBoundary << "--\r\n";
	return result;
}


/**
 * @brief Compute the total Content-Length for the serialised form body.
 *
 * For URL-encoded forms, this is the length of RawData(). For multipart
 * forms, each field's header, data, CRLF separator, and the closing boundary
 * are summed; file sizes are obtained by seeking to the end of the file.
 *
 * @return The total byte length of the serialised form body.
 */
ssize_t
BHttpForm::ContentLength() const
{
	if (fType == B_HTTP_FORM_URL_ENCODED)
		return RawData().Length();

	ssize_t contentLength = 0;

	for (FormStorage::const_iterator it = fFields.begin();
		it != fFields.end(); it++) {
		const BHttpFormData* c = &it->second;
		contentLength += _GetMultipartHeader(c).Length();

		switch (c->Type()) {
			case B_HTTPFORM_UNKNOWN:
				break;

			case B_HTTPFORM_STRING:
				contentLength += c->String().Length();
				break;

			case B_HTTPFORM_FILE:
			{
				BFile upFile(c->File().Path(), B_READ_ONLY);
				upFile.Seek(0, SEEK_END);
				contentLength += upFile.Position();
				break;
			}

			case B_HTTPFORM_BUFFER:
				contentLength += c->BufferSize();
				break;
		}

		contentLength += 2;
	}

	contentLength += fMultipartBoundary.Length() + 6;

	return contentLength;
}


// #pragma mark Form iterator


/**
 * @brief Return an iterator positioned at the first field.
 *
 * @return A BHttpForm::Iterator for traversing and streaming this form.
 */
BHttpForm::Iterator
BHttpForm::GetIterator()
{
	return BHttpForm::Iterator(this);
}


// #pragma mark - Form clear


/**
 * @brief Remove all fields from the form.
 */
void
BHttpForm::Clear()
{
	fFields.clear();
}


// #pragma mark - Overloaded operators


/**
 * @brief Access or create a string field by name using the subscript operator.
 *
 * If no field with \a name exists, an empty string field is created. The
 * returned reference may be used to read or modify the field's value.
 *
 * @param name  The field name to look up or create.
 * @return A reference to the corresponding BHttpFormData.
 */
BHttpFormData&
BHttpForm::operator[](const BString& name)
{
	if (!HasField(name))
		AddString(name, "");

	return fFields[name];
}


/**
 * @brief Extract a single "name=value" pair from a URL-encoded form string.
 *
 * Advances \a index past the consumed characters (up to and including the
 * next '&' delimiter) and calls AddString() to store the extracted pair.
 *
 * @param formString  The full URL-encoded form string.
 * @param index       In/out pointer to the current parse position.
 */
void
BHttpForm::_ExtractNameValuePair(const BString& formString, int32* index)
{
	// Look for a name=value pair
	int16 firstAmpersand = formString.FindFirst("&", *index);
	int16 firstEqual = formString.FindFirst("=", *index);

	BString name;
	BString value;

	if (firstAmpersand == -1) {
		if (firstEqual != -1) {
			formString.CopyInto(name, *index, firstEqual - *index);
			formString.CopyInto(value, firstEqual + 1,
				formString.Length() - firstEqual - 1);
		} else
			formString.CopyInto(value, *index,
				formString.Length() - *index);

		*index = formString.Length() + 1;
	} else {
		if (firstEqual != -1 && firstEqual < firstAmpersand) {
			formString.CopyInto(name, *index, firstEqual - *index);
			formString.CopyInto(value, firstEqual + 1,
				firstAmpersand - firstEqual - 1);
		} else
			formString.CopyInto(value, *index, firstAmpersand - *index);

		*index = firstAmpersand + 1;
	}

	AddString(name, value);
}


/**
 * @brief Generate a random multipart boundary string.
 *
 * Produces a 28-character fixed prefix followed by kBoundaryRandomSize
 * random decimal digits.
 */
void
BHttpForm::_GenerateMultipartBoundary()
{
	fMultipartBoundary = "----------------------------";

	srand(time(NULL));
		// TODO: Maybe a more robust way to seed the random number
		// generator is needed?

	for (int32 i = 0; i < kBoundaryRandomSize; i++)
		fMultipartBoundary << (char)(rand() % 10 + '0');
}


// #pragma mark - Field information access by std iterator


/**
 * @brief Build the multipart MIME part header for a form field.
 *
 * Constructs the "--boundary\r\nContent-Disposition: form-data; name=..."
 * block, appending a filename and Content-Type line for file-upload fields.
 *
 * @param element  Pointer to the BHttpFormData field to generate the header for.
 * @return The complete MIME part header string (not including the data or
 *         trailing CRLF after the data).
 */
BString
BHttpForm::_GetMultipartHeader(const BHttpFormData* element) const
{
	BString result;
	result << "--" << fMultipartBoundary << "\r\n";
	result << "Content-Disposition: form-data; name=\"" << element->Name()
		<< '"';

	switch (element->Type()) {
		case B_HTTPFORM_UNKNOWN:
			break;

		case B_HTTPFORM_FILE:
		{
			result << "; filename=\"" << element->File().Leaf() << '"';

			BNode fileNode(element->File().Path());
			BNodeInfo fileInfo(&fileNode);

			result << "\r\nContent-Type: ";
			char tempMime[128];
			if (fileInfo.GetType(tempMime) == B_OK)
				result << tempMime;
			else
				result << "application/octet-stream";

			break;
		}

		case B_HTTPFORM_STRING:
		case B_HTTPFORM_BUFFER:
			if (element->IsFile()) {
				result << "; filename=\"" << element->Filename() << '"';

				if (element->MimeType().Length() > 0)
					result << "\r\nContent-Type: " << element->MimeType();
				else
					result << "\r\nContent-Type: text/plain";
			}
			break;
	}

	result << "\r\n\r\n";

	return result;
}


// #pragma mark - Iterator


/**
 * @brief Construct an iterator positioned at the first field of \a form.
 *
 * @param form  The BHttpForm to iterate over.
 */
BHttpForm::Iterator::Iterator(BHttpForm* form)
	:
	fElement(NULL)
{
	fForm = form;
	fStdIterator = form->fFields.begin();
	_FindNext();
}


/**
 * @brief Copy constructor — duplicates the iterator's current position.
 *
 * @param other  The source iterator to copy.
 */
BHttpForm::Iterator::Iterator(const Iterator& other)
{
	*this = other;
}


/**
 * @brief Return whether more fields remain to be visited.
 *
 * @return true if Next() will return a valid field, false at end-of-form.
 */
bool
BHttpForm::Iterator::HasNext() const
{
	return fStdIterator != fForm->fFields.end();
}


/**
 * @brief Advance the iterator and return the current field.
 *
 * @return A pointer to the current BHttpFormData, or NULL at end-of-form.
 */
BHttpFormData*
BHttpForm::Iterator::Next()
{
	BHttpFormData* element = fElement;
	_FindNext();
	return element;
}


/**
 * @brief Remove the last field returned by Next() from the form.
 *
 * After calling this method, fElement is set to NULL and the internal
 * std::map iterator is invalidated for the removed element.
 */
void
BHttpForm::Iterator::Remove()
{
	fForm->fFields.erase(fStdIterator);
	fElement = NULL;
}


/**
 * @brief Return the multipart header for the most recently visited field.
 *
 * @return The Content-Disposition header block for the previous element.
 */
BString
BHttpForm::Iterator::MultipartHeader()
{
	return fForm->_GetMultipartHeader(fPrevElement);
}


/**
 * @brief Assignment operator — copies all iterator state from \a other.
 *
 * @param other  The source iterator to copy.
 * @return A reference to this iterator.
 */
BHttpForm::Iterator&
BHttpForm::Iterator::operator=(const Iterator& other)
{
	fForm = other.fForm;
	fStdIterator = other.fStdIterator;
	fElement = other.fElement;
	fPrevElement = other.fPrevElement;

	return *this;
}


/**
 * @brief Advance the std iterator and cache the next element pointer.
 *
 * Saves the current element as fPrevElement, then advances fStdIterator and
 * updates fElement to point to the next BHttpFormData, or NULL at end.
 */
void
BHttpForm::Iterator::_FindNext()
{
	fPrevElement = fElement;

	if (fStdIterator != fForm->fFields.end()) {
		fElement = &fStdIterator->second;
		fStdIterator++;
	} else
		fElement = NULL;
}
