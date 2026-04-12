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
 *   BMailMessage - compatibility wrapper to our mail message class
 *   Copyright 2001 Dr. Zoidberg Enterprises. All rights reserved.
 */


/**
 * @file b_mail_message.cpp
 * @brief Compatibility wrapper implementing the legacy BMailMessage API.
 *
 * BMailMessage is a thin shim that delegates to BEmailMessage, preserving
 * source-level compatibility with applications written against the original
 * BeOS mail kit. All fFields storage is reinterpreted as a BEmailMessage
 * pointer cast through BList* to avoid ABI changes.
 *
 * @see BEmailMessage, BMailContainer
 */


//------This entire document is a horrible, horrible hack. I apologize.
#include <Entry.h>

class _EXPORT BMailMessage;

#include <E-mail.h>

#include <MailAttachment.h>
#include <MailMessage.h>

#include <stdio.h>
#include <strings.h>

struct CharsetConversionEntry
{
	const char *charset;
	uint32 flavor;
};

extern const CharsetConversionEntry mail_charsets[];


/**
 * @brief Constructs a new BMailMessage backed by a BEmailMessage.
 *
 * Allocates a BEmailMessage and stores its pointer in fFields, which is
 * declared as BList* in the legacy header to avoid exposing the new type.
 */
BMailMessage::BMailMessage(void)
	:	fFields((BList *)(new BEmailMessage()))
{
}


/**
 * @brief Destroys the BMailMessage and its underlying BEmailMessage.
 */
BMailMessage::~BMailMessage(void)
{
	delete ((BEmailMessage *)(fFields));
}


/**
 * @brief Adds a plain-text body component using a numeric encoding constant.
 *
 * Creates a BTextMailComponent from the supplied buffer, configures it with
 * quoted-printable transfer encoding and the given character-set flavor, then
 * appends it to the underlying BEmailMessage.
 *
 * @param text      Pointer to the raw text data.
 * @param length    Number of bytes in \a text.
 * @param encoding  BeOS character-set conversion constant (e.g. B_ISO1_CONVERSION).
 * @param clobber   Ignored; present for API compatibility only.
 * @return B_OK on success.
 */
status_t BMailMessage::AddContent(const char *text, int32 length,
	uint32 encoding, bool /*clobber*/)
{
	BTextMailComponent *comp = new BTextMailComponent;
	BMemoryIO io(text,length);
	comp->SetDecodedData(&io);

	comp->SetEncoding(quoted_printable,encoding);

	//if (clobber)
	((BEmailMessage *)(fFields))->AddComponent(comp);

	return B_OK;
}


/**
 * @brief Adds a plain-text body component using a charset name string.
 *
 * Looks up the RFC charset name in the global mail_charsets table to find
 * the corresponding BeOS conversion constant. Falls back to B_ISO1_CONVERSION
 * if the name is unrecognised or NULL.
 *
 * @param text      Pointer to the raw text data.
 * @param length    Number of bytes in \a text.
 * @param encoding  RFC charset name (e.g. "utf-8"), or NULL for ISO-8859-1.
 * @param clobber   Ignored; present for API compatibility only.
 * @return B_OK on success.
 */
status_t BMailMessage::AddContent(const char *text, int32 length,
	const char *encoding, bool /*clobber*/)
{
	BTextMailComponent *comp = new BTextMailComponent();
	BMemoryIO io(text,length);
	comp->SetDecodedData(&io);

	uint32 encode = B_ISO1_CONVERSION;
	//-----I'm assuming that encoding is one of the RFC charsets
	//-----there are no docs. Am I right?
	if (encoding != NULL) {
		for (int32 i = 0; mail_charsets[i].charset != NULL; i++) {
			if (strcasecmp(encoding,mail_charsets[i].charset) == 0) {
				encode = mail_charsets[i].flavor;
				break;
			}
		}
	}

	comp->SetEncoding(quoted_printable,encode);

	//if (clobber)
	((BEmailMessage *)(fFields))->AddComponent(comp);

	return B_OK;
}


/**
 * @brief Attaches a file to the message using an entry_ref.
 *
 * @param ref     entry_ref identifying the file to attach.
 * @param clobber Ignored; present for API compatibility only.
 * @return B_OK on success.
 */
status_t BMailMessage::AddEnclosure(entry_ref *ref, bool /*clobber*/)
{
	((BEmailMessage *)(fFields))->Attach(ref);
	return B_OK;
}


/**
 * @brief Attaches a file to the message by filesystem path.
 *
 * Converts the path string to an entry_ref, then delegates to
 * BEmailMessage::Attach().
 *
 * @param path    Null-terminated path to the file to attach.
 * @param clobber Ignored; present for API compatibility only.
 * @return B_OK on success, or a filesystem error code if the path is invalid.
 */
status_t BMailMessage::AddEnclosure(const char *path, bool /*clobber*/)
{
	BEntry entry(path);
	status_t status;
	if ((status = entry.InitCheck()) < B_OK)
		return status;

	entry_ref ref;
	if ((status = entry.GetRef(&ref)) < B_OK)
		return status;

	((BEmailMessage *)(fFields))->Attach(&ref);
	return B_OK;
}


/**
 * @brief Attaches raw in-memory data as a MIME part with the given type.
 *
 * Wraps the supplied buffer in a BSimpleMailAttachment, sets the
 * Content-Type header to \a MIME_type, and appends the part to the message.
 *
 * @param MIME_type  MIME type string for the attachment (e.g. "image/png").
 * @param data       Pointer to the raw attachment data.
 * @param len        Number of bytes in \a data.
 * @param clobber    Ignored; present for API compatibility only.
 * @return B_OK on success.
 */
status_t BMailMessage::AddEnclosure(const char *MIME_type, void *data, int32 len,
	bool /*clobber*/)
{
	BSimpleMailAttachment *attach = new BSimpleMailAttachment;
	attach->SetDecodedData(data,len);
	attach->SetHeaderField("Content-Type",MIME_type);

	((BEmailMessage *)(fFields))->AddComponent(attach);
	return B_OK;
}


/**
 * @brief Sets a message header field using a numeric encoding constant.
 *
 * Strips the trailing ": " that the legacy API appends to field names before
 * forwarding to BEmailMessage::SetHeaderField().
 *
 * @param encoding    Ignored; present for API compatibility only.
 * @param field_name  Header name including the trailing ": " suffix.
 * @param str         Header value string.
 * @param clobber     Ignored; present for API compatibility only.
 * @return B_OK on success.
 */
status_t BMailMessage::AddHeaderField(uint32 /*encoding*/, const char *field_name, const char *str,
	bool /*clobber*/)
{
	//printf("First AddHeaderField. Args are %s%s\n",field_name,str);

	BString string = field_name;
	string.Truncate(string.Length() - 2); //----BMailMessage includes the ": "
	((BEmailMessage *)(fFields))->SetHeaderField(string.String(),str);
	return B_OK;
}


/**
 * @brief Sets a message header field by name (no encoding argument).
 *
 * Strips the trailing ": " that the legacy API appends to field names before
 * forwarding to BEmailMessage::SetHeaderField().
 *
 * @param field_name  Header name including the trailing ": " suffix.
 * @param str         Header value string.
 * @param clobber     Ignored; present for API compatibility only.
 * @return B_OK on success.
 */
status_t BMailMessage::AddHeaderField(const char *field_name, const char *str,
	bool /*clobber*/)
{
	//printf("Second AddHeaderField. Args are %s%s\n",field_name,str);
	BString string = field_name;
	string.Truncate(string.Length() - 2); //----BMailMessage includes the ": "
	((BEmailMessage *)(fFields))->SetHeaderField(string.String(),str);
	return B_OK;
}


/**
 * @brief Sends the message, optionally triggering immediate delivery.
 *
 * @param send_now  If true, notifies the mail daemon to deliver the message
 *                  immediately; if false the message is only queued.
 * @return B_OK on success, or an error code from BEmailMessage::Send().
 */
status_t BMailMessage::Send(bool send_now,
	 bool /*remove_when_I_have_completed_sending_this_message_to_your_preferred_SMTP_server*/)
{
 	return ((BEmailMessage *)(fFields))->Send(send_now);
}
