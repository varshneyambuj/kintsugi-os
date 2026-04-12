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
 *   (Text)Component - message component base class and plain text
 *   Copyright 2001 Dr. Zoidberg Enterprises. All rights reserved.
 */


/**
 * @file MailComponent.cpp
 * @brief Base MIME component class and plain-text body component for the mail kit.
 *
 * BMailComponent is the abstract base for all MIME body parts: it stores and
 * serialises RFC 822 headers, computes MIME types, and provides the lazy-parse
 * interface used by attachment and container subclasses. BTextMailComponent
 * extends it to handle text/plain (and other text/*) parts with quoted-printable
 * or base64 encoding and charset conversion to/from UTF-8.
 *
 * @see BSimpleMailAttachment, BMIMEMultipartMailContainer, BEmailMessage
 */


#include <String.h>
#include <Mime.h>

#include <ctype.h>
#include <stdlib.h>
#include <strings.h>

class _EXPORT BMailComponent;
class _EXPORT BTextMailComponent;

#include <MailComponent.h>
#include <MailAttachment.h>
#include <MailContainer.h>
#include <mail_util.h>

#include <CharacterSet.h>
#include <CharacterSetRoster.h>

using namespace BPrivate ;

struct CharsetConversionEntry
{
	const char* charset;
	uint32 flavor;
};

extern const CharsetConversionEntry mail_charsets[];


/** @brief BMessage key for the character-set integer stored in header messages. */
const char* kHeaderCharsetString = "header-charset";

/** @brief BMessage key for the encoding integer stored in header messages. */
const char* kHeaderEncodingString = "header-encoding";
// Special field names in the headers which specify the character set (int32)
// and encoding (int8) to use when converting the headers from UTF-8 to the
// output e-mail format (rfc2047).  Since they are numbers, not strings, the
// extra fields won't be output.


/**
 * @brief Constructs a BMailComponent with the given default character set.
 *
 * @param defaultCharSet  BeOS character-set conversion constant used when the
 *                        message does not specify an explicit charset.
 */
BMailComponent::BMailComponent(uint32 defaultCharSet)
	: _charSetForTextDecoding (defaultCharSet)
{
}


/**
 * @brief Destroys the BMailComponent.
 */
BMailComponent::~BMailComponent()
{
}


/**
 * @brief Determines the high-level component type of this MIME part.
 *
 * Checks first for B_MAIL_ATTRIBUTED_ATTACHMENT via dynamic_cast, then
 * inspects the MIME supertype. Returns one of the B_MAIL_* constants defined
 * in MailComponent.h.
 *
 * @return B_MAIL_ATTRIBUTED_ATTACHMENT, B_MAIL_MULTIPART_CONTAINER,
 *         B_MAIL_PLAIN_TEXT_BODY, or B_MAIL_SIMPLE_ATTACHMENT.
 */
uint32
BMailComponent::ComponentType()
{
	if (NULL != dynamic_cast<BAttributedMailAttachment*> (this))
		return B_MAIL_ATTRIBUTED_ATTACHMENT;

	BMimeType type, super;
	MIMEType(&type);
	type.GetSupertype(&super);

	//---------ATT-This code *desperately* needs to be improved
	if (super == "multipart") {
		if (type == "multipart/x-bfile") // Not likely, they have the MIME
			return B_MAIL_ATTRIBUTED_ATTACHMENT; // of their data contents.
		else
			return B_MAIL_MULTIPART_CONTAINER;
	} else if (!IsAttachment() && (super == "text" || type.Type() == NULL))
		return B_MAIL_PLAIN_TEXT_BODY;
	else
		return B_MAIL_SIMPLE_ATTACHMENT;
}


/**
 * @brief Allocates a concrete subclass instance matching this component's type.
 *
 * Uses ComponentType() to determine the correct subclass, then allocates and
 * returns a new instance of that type with the same default character set.
 *
 * @return Newly allocated BMailComponent subclass; the caller takes ownership.
 */
BMailComponent*
BMailComponent::WhatIsThis()
{
	switch (ComponentType()) {
		case B_MAIL_SIMPLE_ATTACHMENT:
			return new BSimpleMailAttachment;
		case B_MAIL_ATTRIBUTED_ATTACHMENT:
			return new BAttributedMailAttachment;
		case B_MAIL_MULTIPART_CONTAINER:
			return new BMIMEMultipartMailContainer (NULL, NULL, _charSetForTextDecoding);
		case B_MAIL_PLAIN_TEXT_BODY:
		default:
			return new BTextMailComponent (NULL, _charSetForTextDecoding);
	}
}


/**
 * @brief Returns true if this component should be treated as an attachment.
 *
 * Checks the Content-Disposition header for "attachment", the Content-Type
 * header for a "name" parameter, and whether a Content-Location header is
 * present.
 *
 * @return true if this is an attachment, false if it is an inline body part.
 */
bool
BMailComponent::IsAttachment()
{
	const char* disposition = HeaderField("Content-Disposition");
	if ((disposition != NULL)
		&& (strncasecmp(disposition, "Attachment", strlen("Attachment")) == 0))
		return true;

	BMessage header;
	HeaderField("Content-Type", &header);
	if (header.HasString("name"))
		return true;

	if (HeaderField("Content-Location", &header) == B_OK)
		return true;

	BMimeType type;
	MIMEType(&type);
	if (type == "multipart/x-bfile")
		return true;

	return false;
}


/**
 * @brief Sets a string-valued header field, optionally with character-set encoding hints.
 *
 * Removes any existing values for \a key if \a replace_existing is true, then
 * adds the new value. Also records \a charset and \a encoding globally for use
 * during RenderToRFC822().
 *
 * @param key               RFC 822 header field name.
 * @param value             New field value; NULL or empty string removes the field.
 * @param charset           Character-set conversion constant, or B_MAIL_NULL_CONVERSION
 *                          to leave unchanged.
 * @param encoding          Transfer encoding, or null_encoding to leave unchanged.
 * @param replace_existing  If true, replaces any previous values for \a key.
 */
void
BMailComponent::SetHeaderField(const char* key, const char* value,
	uint32 charset, mail_encoding encoding, bool replace_existing)
{
	if (replace_existing)
		headers.RemoveName(key);
	if (value != NULL && value[0] != 0) // Empty or NULL strings mean delete header.
		headers.AddString(key, value);

	// Latest setting of the character set and encoding to use when outputting
	// the headers is the one which affects all the headers.  There used to be
	// separate settings for each item in the headers, but it never actually
	// worked (can't store multiple items of different types in a BMessage).
	if (charset != B_MAIL_NULL_CONVERSION
		&& headers.ReplaceInt32 (kHeaderCharsetString, charset) != B_OK)
		headers.AddInt32(kHeaderCharsetString, charset);
	if (encoding != null_encoding
		&& headers.ReplaceInt8 (kHeaderEncodingString, encoding) != B_OK)
		headers.AddInt8(kHeaderEncodingString, encoding);
}


/**
 * @brief Sets a structured header field from a BMessage of key/value sub-parameters.
 *
 * Serialises the BMessage's string fields into a semicolon-separated header
 * value (e.g. for Content-Type: text/plain; charset="utf-8") and calls the
 * string-based overload.
 *
 * @param key               RFC 822 header field name.
 * @param structure         BMessage whose string fields become semicolon-separated
 *                          parameters. The "unlabeled" key becomes the primary value.
 * @param replace_existing  If true, replaces any previous values for \a key.
 */
void
BMailComponent::SetHeaderField(const char* key, BMessage* structure,
	bool replace_existing)
{
	int32 charset = B_MAIL_NULL_CONVERSION;
	int8 encoding = null_encoding;
	const char* unlabeled = "unlabeled";

	if (replace_existing)
		headers.RemoveName(key);

	BString value;
	if (structure->HasString(unlabeled))
		value << structure->FindString(unlabeled) << "; ";

	const char* name;
	const char* sub_val;
	type_code type;
	for (int32 i = 0; structure->GetInfo(B_STRING_TYPE, i,
#if !defined(HAIKU_TARGET_PLATFORM_DANO)
		(char**)
#endif
		&name, &type) == B_OK; i++) {

		if (strcasecmp(name, unlabeled) == 0)
			continue;

		structure->FindString(name, &sub_val);
		value << name << '=';
		if (BString(sub_val).FindFirst(' ') > 0)
			value << '\"' << sub_val << "\"; ";
		else
			value << sub_val << "; ";
	}

	value.Truncate(value.Length() - 2); //-----Remove the last "; "

	if (structure->HasInt32(kHeaderCharsetString))
		structure->FindInt32(kHeaderCharsetString, &charset);
	if (structure->HasInt8(kHeaderEncodingString))
		structure->FindInt8(kHeaderEncodingString, &encoding);

	SetHeaderField(key, value.String(), (uint32) charset, (mail_encoding) encoding);
}


/**
 * @brief Returns the raw string value of a header field by name and index.
 *
 * @param key    RFC 822 header field name (case-insensitive BMessage lookup).
 * @param index  Zero-based index for fields that appear multiple times.
 * @return Pointer to the field value string, or NULL if not found.
 */
const char*
BMailComponent::HeaderField(const char* key, int32 index) const
{
	const char* string = NULL;

	headers.FindString(key, index, &string);
	return string;
}


/**
 * @brief Parses a structured header field into a BMessage of sub-parameters.
 *
 * Splits the field value at semicolons, trims whitespace, handles quoted
 * strings, and fills \a structure with named sub-values. The primary
 * (unlabeled) value is stored under the key "unlabeled".
 *
 * @param key       RFC 822 header field name.
 * @param structure Output BMessage to receive the parsed sub-parameters.
 * @param index     Zero-based index for fields that appear multiple times.
 * @return B_OK on success, B_NAME_NOT_FOUND if the field is absent.
 */
status_t
BMailComponent::HeaderField(const char* key, BMessage* structure,
	int32 index) const
{
	BString string = HeaderField(key, index);
	if (string == "")
		return B_NAME_NOT_FOUND;

	BString sub_cat;
	BString end_piece;
	int32 i = 0;
	int32 end = 0;

	// Break the header into parts, they're separated by semicolons, like this:
	// Content-Type: multipart/mixed;boundary= "----=_NextPart_000_00AA_354DB459.5977A1CA"
	// There's also white space and quotes to be removed, and even comments in
	// parenthesis like this, which can appear anywhere white space is: (header comment)

	while (end < string.Length()) {
		end = string.FindFirst(';', i);
		if (end < 0)
			end = string.Length();

		string.CopyInto(sub_cat, i, end - i);
		i = end + 1;

		//-------Trim spaces off of beginning and end of text
		for (int32 h = 0; h < sub_cat.Length(); h++) {
			if (!isspace(sub_cat.ByteAt(h))) {
				sub_cat.Remove(0, h);
				break;
			}
		}
		for (int32 h = sub_cat.Length() - 1; h >= 0; h--) {
			if (!isspace(sub_cat.ByteAt(h))) {
				sub_cat.Truncate(h + 1);
				break;
			}
		}
		//--------Split along '='
		int32 first_equal = sub_cat.FindFirst('=');
		if (first_equal >= 0) {
			sub_cat.CopyInto(end_piece, first_equal + 1, sub_cat.Length() - first_equal - 1);
			sub_cat.Truncate(first_equal);
			// Remove leading spaces from part after the equals sign.
			while (isspace (end_piece.ByteAt(0)))
				end_piece.Remove (0 /* index */, 1 /* number of chars */);
			// Remove quote marks.
			if (end_piece.ByteAt(0) == '\"') {
				end_piece.Remove(0, 1);
				end_piece.Truncate(end_piece.Length() - 1);
			}
			sub_cat.ToLower();
			structure->AddString(sub_cat.String(), end_piece.String());
		} else {
			structure->AddString("unlabeled", sub_cat.String());
		}
	}

	return B_OK;
}


/**
 * @brief Removes all stored values for a header field by name.
 *
 * @param key  RFC 822 header field name.
 * @return B_OK on success, or B_NAME_NOT_FOUND if the field is absent.
 */
status_t
BMailComponent::RemoveHeader(const char* key)
{
	return headers.RemoveName(key);
}


/**
 * @brief Returns the name of the header field at the given index.
 *
 * Iterates over the internal headers BMessage to enumerate field names.
 * Useful for copying all headers from one component to another.
 *
 * @param index  Zero-based index across all header fields.
 * @return Pointer to the field name string, or NULL if index is out of range.
 */
const char*
BMailComponent::HeaderAt(int32 index) const
{
#if defined(HAIKU_TARGET_PLATFORM_DANO)
	const
#endif
	char* name = NULL;
	type_code type;

	headers.GetInfo(B_STRING_TYPE, index, &name, &type);
	return name;
}


/**
 * @brief Default implementation of GetDecodedData; does nothing.
 *
 * @return B_OK always.
 */
status_t
BMailComponent::GetDecodedData(BPositionIO*)
{
	return B_OK;
}


/**
 * @brief Default implementation of SetDecodedData; does nothing.
 *
 * @return B_OK always.
 */
status_t
BMailComponent::SetDecodedData(BPositionIO*)
{
	return B_OK;
}


/**
 * @brief Parses the RFC 822 headers from the stream, ignoring the body.
 *
 * Clears any previously stored headers and refills them from the stream
 * by calling parse_header().
 *
 * @param data       Readable stream positioned at the start of the headers.
 * @param length     Ignored; parse_header reads until the blank line.
 * @param parse_now  Ignored at this level.
 * @return B_OK on success, or an error code from parse_header().
 */
status_t
BMailComponent::SetToRFC822(BPositionIO* data, size_t /*length*/, bool /*parse_now*/)
{
	headers.MakeEmpty();

	// Only parse the header here
	return parse_header(headers, *data);
}


/**
 * @brief Serialises all stored headers to an RFC 822 output stream.
 *
 * Iterates through all string-valued entries in the headers BMessage,
 * encodes each value using rfc2047 (with the globally configured charset
 * and encoding), folds long lines, and writes them followed by CRLF pairs
 * and a blank line separating headers from body.
 *
 * @param render_to  Output stream to write the formatted headers to.
 * @return B_OK on success, or a negative IO error code if writing fails.
 */
status_t
BMailComponent::RenderToRFC822(BPositionIO* render_to)
{
	int32 charset = B_ISO15_CONVERSION;
	int8 encoding = quoted_printable;
	const char* key;
	const char* value;
	char* allocd;
	ssize_t amountWritten;
	BString concat;
	type_code stupidity_personified = B_STRING_TYPE;
	int32 count = 0;

	if (headers.HasInt32(kHeaderCharsetString))
		headers.FindInt32(kHeaderCharsetString, &charset);
	if (headers.HasInt8(kHeaderEncodingString))
		headers.FindInt8(kHeaderEncodingString, &encoding);

	for (int32 index = 0; headers.GetInfo(B_STRING_TYPE, index,
#if !defined(HAIKU_TARGET_PLATFORM_DANO)
			(char**)
#endif
			&key, &stupidity_personified, &count) == B_OK; index++) {
		for (int32 g = 0; g < count; g++) {
			headers.FindString(key, g, (const char**)&value);
			allocd = (char*)malloc(strlen(value) + 1);
			strcpy(allocd, value);

			concat << key << ": ";
			concat.CapitalizeEachWord();

			concat.Append(allocd, utf8_to_rfc2047(&allocd, strlen(value),
				charset, encoding));
			free(allocd);
			FoldLineAtWhiteSpaceAndAddCRLF(concat);

			amountWritten = render_to->Write(concat.String(), concat.Length());
			if (amountWritten < 0)
				return amountWritten; // IO error happened, usually disk full.
			concat = "";
		}
	}

	render_to->Write("\r\n", 2);

	return B_OK;
}


/**
 * @brief Determines the MIME type of this component from its Content-Type header.
 *
 * Prefers non-multipart Content-Type values; falls back to multipart types
 * only if a valid boundary string is present. Normalises the type to lower
 * case and handles the legacy "TEXT" value.
 *
 * @param mime  Output BMimeType to receive the determined type.
 * @return B_OK always.
 */
status_t
BMailComponent::MIMEType(BMimeType* mime)
{
	bool foundBestHeader;
	const char* boundaryString;
	unsigned int i;
	BMessage msg;
	const char* typeAsString = NULL;
	char typeAsLowerCaseString[B_MIME_TYPE_LENGTH];

	// Find the best Content-Type header to use.  There should really be just
	// one, but evil spammers sneakily insert one for multipart (with no
	// boundary string), then one for text/plain.  We'll scan through them and
	// only use the multipart one if there are no others, and it has a
	// boundary.

	foundBestHeader = false;
	for (i = 0; msg.MakeEmpty(), HeaderField("Content-Type", &msg, i) == B_OK; i++) {
		typeAsString = msg.FindString("unlabeled");
		if (typeAsString != NULL && strncasecmp(typeAsString, "multipart", 9) != 0) {
			foundBestHeader = true;
			break;
		}
	}
	if (!foundBestHeader) {
		for (i = 0; msg.MakeEmpty(), HeaderField("Content-Type", &msg, i) == B_OK; i++) {
			typeAsString = msg.FindString("unlabeled");
			if (typeAsString != NULL && strncasecmp(typeAsString, "multipart", 9) == 0) {
				boundaryString = msg.FindString("boundary");
				if (boundaryString != NULL && strlen(boundaryString) > 0) {
					foundBestHeader = true;
					break;
				}
			}
		}
	}
	// At this point we have the good MIME type in typeAsString, but only if
	// foundBestHeader is true.

	if (!foundBestHeader) {
		strcpy(typeAsLowerCaseString, "text/plain"); // Hope this is an OK default.
	} else {
		// Some extra processing to convert mixed or upper case MIME types into
		// lower case, since the BeOS R5 BMimeType is case sensitive (but Haiku
		// isn't).  Also truncate the string if it is too long.
		for (i = 0; i < sizeof(typeAsLowerCaseString) - 1
			&& typeAsString[i] != 0; i++)
			typeAsLowerCaseString[i] = tolower(typeAsString[i]);
		typeAsLowerCaseString[i] = 0;

		// Some old e-mail programs saved the type as just "TEXT", which we need to
		// convert to "text/plain" since the rest of the code looks for that.
		if (strcmp(typeAsLowerCaseString, "text") == 0)
			strcpy(typeAsLowerCaseString, "text/plain");
	}
	mime->SetTo(typeAsLowerCaseString);
	return B_OK;
}


void BMailComponent::_ReservedComponent1() {}
void BMailComponent::_ReservedComponent2() {}
void BMailComponent::_ReservedComponent3() {}
void BMailComponent::_ReservedComponent4() {}
void BMailComponent::_ReservedComponent5() {}


//-------------------------------------------------------------------------
//	#pragma mark -


/**
 * @brief Constructs a BTextMailComponent with optional initial text.
 *
 * Initialises the component to use quoted-printable encoding and ISO-8859-15.
 * If \a text is non-NULL, calls SetText() to store the initial content.
 * Also sets the MIME-Version header to "1.0".
 *
 * @param text            Initial body text as a UTF-8 string, or NULL.
 * @param defaultCharSet  Default character-set conversion constant.
 */
BTextMailComponent::BTextMailComponent(const char* text, uint32 defaultCharSet)
	: BMailComponent(defaultCharSet),
	encoding(quoted_printable),
	charset(B_ISO15_CONVERSION),
	raw_data(NULL)
{
	if (text != NULL)
		SetText(text);

	SetHeaderField("MIME-Version", "1.0");
}


/**
 * @brief Destroys the BTextMailComponent.
 */
BTextMailComponent::~BTextMailComponent()
{
}


/**
 * @brief Sets the transfer encoding and character set for rendering.
 *
 * @param encoding  Transfer encoding to use (e.g. quoted_printable, base64).
 * @param charset   BeOS character-set conversion constant for the output charset.
 */
void
BTextMailComponent::SetEncoding(mail_encoding encoding, int32 charset)
{
	this->encoding = encoding;
	this->charset = charset;
}


/**
 * @brief Sets the UTF-8 body text, replacing any previously stored content.
 *
 * Clears the raw-data pointer so that the lazy parser is not invoked.
 *
 * @param text  Null-terminated UTF-8 string to store as the body.
 */
void
BTextMailComponent::SetText(const char* text)
{
	this->text.SetTo(text);

	raw_data = NULL;
}


/**
 * @brief Appends text to the end of the current body content.
 *
 * Forces lazy parse before appending so that raw RFC 822 data is decoded first.
 *
 * @param text  Null-terminated UTF-8 string to append.
 */
void
BTextMailComponent::AppendText(const char* text)
{
	ParseRaw();

	this->text << text;
}


/**
 * @brief Returns the body text as a UTF-8 C string.
 *
 * Forces lazy parse if needed.
 *
 * @return Pointer to the null-terminated UTF-8 text; valid for the object's lifetime.
 */
const char*
BTextMailComponent::Text()
{
	ParseRaw();

	return text.String();
}


/**
 * @brief Returns a pointer to the internal BString holding the body text.
 *
 * Forces lazy parse if needed. Useful when the caller needs to manipulate
 * the text via BString operations directly.
 *
 * @return Pointer to the internal text BString.
 */
BString*
BTextMailComponent::BStringText()
{
	ParseRaw();

	return &text;
}


/**
 * @brief Prepends a quoted copy of \a message and prefixes each existing line.
 *
 * Inserts \a message followed by a newline at the beginning of the current
 * text, then prepends each subsequent line with \a quote_style.
 *
 * @param message     Text to insert as the quotation attribution line.
 * @param quote_style Per-line prefix string (e.g. "> ").
 */
void
BTextMailComponent::Quote(const char* message, const char* quote_style)
{
	ParseRaw();

	BString string;
	string << '\n' << quote_style;
	text.ReplaceAll("\n",string.String());

	string = message;
	string << '\n';
	text.Prepend(string.String());
}


/**
 * @brief Writes the decoded body text (or binary decoded data) to \a data.
 *
 * For text/* MIME types the UTF-8 converted text string is written. For
 * other types the raw decoded (but not charset-converted) bytes are written.
 *
 * @param data  Destination stream to write the body content to.
 * @return B_OK on success, B_IO_ERROR if \a data is NULL, or an IO error code.
 */
status_t
BTextMailComponent::GetDecodedData(BPositionIO* data)
{
	ParseRaw();

	if (data == NULL)
		return B_IO_ERROR;

	BMimeType type;
	BMimeType textAny("text");
	ssize_t written;
	if (MIMEType(&type) == B_OK && textAny.Contains(&type))
		// Write out the string which has been both decoded from quoted
		// printable or base64 etc, and then converted to UTF-8 from whatever
		// character set the message specified.  Do it for text/html,
		// text/plain and all other text datatypes.  Of course, if the message
		// is HTML and specifies a META tag for a character set, it will now be
		// wrong.  But then we don't display HTML in BeMail, yet.
		written = data->Write(text.String(), text.Length());
	else
		// Just write out whatever the binary contents are, only decoded from
		// the quoted printable etc format.
		written = data->Write(decoded.String(), decoded.Length());

	return written >= 0 ? B_OK : written;
}


/**
 * @brief Reads decoded body text from a stream into the internal text buffer.
 *
 * Reads all available bytes from \a data in 254-byte chunks, appending each
 * chunk to the text string.
 *
 * @param data  Source stream to read from.
 * @return B_OK always.
 */
status_t
BTextMailComponent::SetDecodedData(BPositionIO* data)
{
	char buffer[255];
	size_t buf_len;

	while ((buf_len = data->Read(buffer, 254)) > 0) {
		buffer[buf_len] = 0;
		this->text << buffer;
	}

	raw_data = NULL;

	return B_OK;
}


/**
 * @brief Records the body byte range for lazy decoding from an RFC 822 stream.
 *
 * Parses headers via the base class, then records the stream position and
 * remaining byte count for the body. If \a parseNow is true, calls ParseRaw()
 * immediately to decode and convert the body.
 *
 * @param data      Stream positioned at the start of this text part.
 * @param length    Number of bytes available for this part.
 * @param parseNow  If true, decode and charset-convert immediately.
 * @return B_OK on success, or an error code from ParseRaw().
 */
status_t
BTextMailComponent::SetToRFC822(BPositionIO* data, size_t length, bool parseNow)
{
	off_t position = data->Position();
	BMailComponent::SetToRFC822(data, length);

	// Some malformed MIME headers can have the header running into the
	// boundary of the next MIME chunk, resulting in a negative length.
	length -= data->Position() - position;
	if ((ssize_t) length < 0)
	  length = 0;

	raw_data = data;
	raw_length = length;
	raw_offset = data->Position();

	if (parseNow) {
		// copies the data stream and sets the raw_data variable to NULL
		return ParseRaw();
	}

	return B_OK;
}


/**
 * @brief Decodes and charset-converts the raw body data into the text buffer.
 *
 * Reads the raw bytes from the recorded position, decodes them using the
 * Content-Transfer-Encoding, strips CR from CRLF line endings, attempts to
 * detect the character set from the Content-Type header or by heuristic, and
 * converts to UTF-8 via mail_convert_to_utf8(). Sets raw_data to NULL on
 * completion so this method is a no-op on subsequent calls.
 *
 * @return B_OK on success, B_NO_MEMORY if a buffer allocation fails,
 *         B_IO_ERROR if the stream cannot be read.
 */
status_t
BTextMailComponent::ParseRaw()
{
	if (raw_data == NULL)
		return B_OK;

	raw_data->Seek(raw_offset, SEEK_SET);

	BMessage content_type;
	HeaderField("Content-Type", &content_type);

	charset = _charSetForTextDecoding;
	if (charset == B_MAIL_NULL_CONVERSION && content_type.HasString("charset")) {
		const char* charset_string = content_type.FindString("charset");
		if (strcasecmp(charset_string, "us-ascii") == 0) {
			charset = B_MAIL_US_ASCII_CONVERSION;
		} else if (strcasecmp(charset_string, "utf-8") == 0) {
			charset = B_MAIL_UTF8_CONVERSION;
		} else {
			const BCharacterSet* cs = BCharacterSetRoster::FindCharacterSetByName(charset_string);
			if (cs != NULL) {
				charset = cs->GetConversionID();
			}
		}
	}

	encoding = encoding_for_cte(HeaderField("Content-Transfer-Encoding"));

	char* buffer = (char*)malloc(raw_length + 1);
	if (buffer == NULL)
		return B_NO_MEMORY;

	int32 bytes;
	if ((bytes = raw_data->Read(buffer, raw_length)) < 0)
		return B_IO_ERROR;

	char* string = decoded.LockBuffer(bytes + 1);
	bytes = decode(encoding, string, buffer, bytes, 0);
	free(buffer);
	buffer = NULL;

	// Change line ends from \r\n to just \n.  Though this won't work properly
	// for UTF-16 because \r takes up two bytes rather than one.
	char* dest;
	char* src;
	char* end = string + bytes;
	for (dest = src = string; src < end; src++) {
	 	if (*src != '\r')
	 		*dest++ = *src;
	}
	decoded.UnlockBuffer(dest - string);
	bytes = decoded.Length(); // Might have shrunk a bit.

	// If the character set wasn't specified, try to guess.  ISO-2022-JP
	// contains the escape sequences ESC $ B or ESC $ @ to turn on 2 byte
	// Japanese, and ESC ( J to switch to Roman, or sometimes ESC ( B for
	// ASCII.  We'll just try looking for the two switch to Japanese sequences.

	if (charset == B_MAIL_NULL_CONVERSION) {
		if (decoded.FindFirst ("\e$B") >= 0 || decoded.FindFirst ("\e$@") >= 0)
			charset = B_JIS_CONVERSION;
		else // Just assume the usual Latin-9 character set.
			charset = B_ISO15_CONVERSION;
	}

	int32 state = 0;
	int32 destLength = bytes * 3 /* in case it grows */ + 1 /* +1 so it isn't zero which crashes */;
	string = text.LockBuffer(destLength);
	mail_convert_to_utf8(charset, decoded.String(), &bytes, string,
		&destLength, &state);
	if (destLength > 0)
		text.UnlockBuffer(destLength);
	else {
		text.UnlockBuffer(0);
		text.SetTo(decoded);
	}

	raw_data = NULL;
	return B_OK;
}


/**
 * @brief Charset-converts and transfer-encodes the body text, then writes to a stream.
 *
 * Forces lazy parse, updates the Content-Type (with charset parameter) and
 * Content-Transfer-Encoding headers, calls the base class to render headers,
 * converts the UTF-8 body to the target charset, applies the transfer encoding,
 * and writes the encoded body followed by a trailing CRLF.
 *
 * @param render_to  Output stream to write the complete MIME text part to.
 * @return B_OK on success, or a negative error code on failure.
 */
status_t
BTextMailComponent::RenderToRFC822(BPositionIO* render_to)
{
	status_t status = ParseRaw();
	if (status < B_OK)
		return status;

	BMimeType type;
	MIMEType(&type);
	BString content_type;
	content_type << type.Type(); // Preserve MIME type (e.g. text/html

	for (uint32 i = 0; mail_charsets[i].charset != NULL; i++) {
		if (mail_charsets[i].flavor == charset) {
			content_type << "; charset=\"" << mail_charsets[i].charset << "\"";
			break;
		}
	}

	SetHeaderField("Content-Type", content_type.String());

	const char* transfer_encoding = NULL;
	switch (encoding) {
		case base64:
			transfer_encoding = "base64";
			break;
		case quoted_printable:
			transfer_encoding = "quoted-printable";
			break;
		case eight_bit:
			transfer_encoding = "8bit";
			break;
		case seven_bit:
		default:
			transfer_encoding = "7bit";
			break;
	}

	SetHeaderField("Content-Transfer-Encoding", transfer_encoding);

	BMailComponent::RenderToRFC822(render_to);

	BString modified = this->text;
	BString alt;

	int32 len = this->text.Length();
	if (len > 0) {
		int32 dest_len = len * 5;
		// Shift-JIS can have a 3 byte escape sequence and a 2 byte code for
		// each character (which could just be 2 bytes in UTF-8, or even 1 byte
		// if it's regular ASCII), so it can get quite a bit larger than the
		// original text.  Multiplying by 5 should make more than enough space.
		char* raw = alt.LockBuffer(dest_len);
		int32 state = 0;
		mail_convert_from_utf8(charset, this->text.String(), &len, raw,
			&dest_len, &state);
		alt.UnlockBuffer(dest_len);

		raw = modified.LockBuffer((alt.Length() * 3) + 1);
		switch (encoding) {
			case base64:
				len = encode_base64(raw, alt.String(), alt.Length(), false);
				raw[len] = 0;
				break;
			case quoted_printable:
				len = encode_qp(raw, alt.String(), alt.Length(), false);
				raw[len] = 0;
				break;
			case eight_bit:
			case seven_bit:
			default:
				len = alt.Length();
				strcpy(raw, alt.String());
		}
		modified.UnlockBuffer(len);

		if (encoding != base64) // encode_base64 already does CRLF line endings.
			modified.ReplaceAll("\n","\r\n");

		// There seem to be a possibility of NULL bytes in the text, so lets
		// filter them out, shouldn't be any after the encoding stage.

		char* string = modified.LockBuffer(modified.Length());
		for (int32 i = modified.Length(); i-- > 0;) {
			if (string[i] != '\0')
				continue;

			puts("BTextMailComponent::RenderToRFC822: NULL byte in text!!");
			string[i] = ' ';
		}
		modified.UnlockBuffer();

		// word wrapping is already done by BeMail (user-configurable)
		// and it does it *MUCH* nicer.

//		//------Desperate bid to wrap lines
//		int32 curr_line_length = 0;
//		int32 last_space = 0;
//
//		for (int32 i = 0; i < modified.Length(); i++) {
//			if (isspace(modified.ByteAt(i)))
//				last_space = i;
//
//			if ((modified.ByteAt(i) == '\r') && (modified.ByteAt(i+1) == '\n'))
//				curr_line_length = 0;
//			else
//				curr_line_length++;
//
//			if (curr_line_length > 80) {
//				if (last_space >= 0) {
//					modified.Insert("\r\n",last_space);
//					last_space = -1;
//					curr_line_length = 0;
//				}
//			}
//		}
	}
	modified << "\r\n";

	render_to->Write(modified.String(), modified.Length());

	return B_OK;
}


void BTextMailComponent::_ReservedText1() {}
void BTextMailComponent::_ReservedText2() {}
