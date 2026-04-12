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
 *   Copyright 2007-2015, Haiku Inc. All Rights Reserved.
 *   Copyright 2001-2004 Dr. Zoidberg Enterprises. All rights reserved.
 *
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file MailMessage.cpp
 * @brief BEmailMessage — the main general-purpose mail message class.
 *
 * BEmailMessage is the top-level RFC 822 message object. It owns the parsed
 * body tree (plain text, attachments, or a multipart container), provides
 * typed accessors for the standard envelope headers (To, From, Subject, Date,
 * etc.), and implements RenderToRFC822() / RenderTo() to compose and write
 * outgoing messages to disk. Callers then use Send() to queue or immediately
 * dispatch the rendered file via the mail daemon.
 *
 * @see BMIMEMultipartMailContainer, BTextMailComponent, BSimpleMailAttachment
 */


#include <MailMessage.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

#include <parsedate.h>

#include <Directory.h>
#include <E-mail.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <List.h>
#include <MailAttachment.h>
#include <MailDaemon.h>
#include <MailSettings.h>
#include <Messenger.h>
#include <netdb.h>
#include <NodeInfo.h>
#include <Path.h>
#include <String.h>
#include <StringList.h>

#include <MailPrivate.h>
#include <mail_util.h>


using namespace BPrivate;


//-------Change the following!----------------------
/** @brief MIME boundary string used when composing new multipart messages. */
#define mime_boundary "----------Zoidberg-BeMail-temp--------"

/** @brief Preamble text written before the first boundary for legacy clients. */
#define mime_warning "This is a multipart message in MIME format."


/**
 * @brief Constructs a BEmailMessage from an existing stream, optionally taking ownership.
 *
 * If \a file is non-NULL, calls SetToRFC822() to parse the message. The default
 * outbound account is read from BMailSettings.
 *
 * @param file  Readable stream containing an RFC 822 message, or NULL for a
 *              blank outgoing message.
 * @param own   If true, the object takes ownership of \a file and deletes it
 *              on destruction.
 * @param defaultCharSet  Default character-set conversion constant.
 */
BEmailMessage::BEmailMessage(BPositionIO* file, bool own, uint32 defaultCharSet)
	:
	BMailContainer(defaultCharSet),
	fData(NULL),
	fStatus(B_NO_ERROR),
	fBCC(NULL),
	fComponentCount(0),
	fBody(NULL),
	fTextBody(NULL)
{
	BMailSettings settings;
	fAccountID = settings.DefaultOutboundAccount();

	if (own)
		fData = file;

	if (file != NULL)
		SetToRFC822(file, ~0L);
}


/**
 * @brief Constructs a BEmailMessage by reading and parsing a mail file by ref.
 *
 * Opens the file in read-only mode and parses it with SetToRFC822().
 *
 * @param ref             entry_ref of the mail file to open.
 * @param defaultCharSet  Default character-set conversion constant.
 */
BEmailMessage::BEmailMessage(const entry_ref* ref, uint32 defaultCharSet)
	:
	BMailContainer(defaultCharSet),
	fBCC(NULL),
	fComponentCount(0),
	fBody(NULL),
	fTextBody(NULL)
{
	BMailSettings settings;
	fAccountID = settings.DefaultOutboundAccount();

	fData = new BFile();
	fStatus = static_cast<BFile*>(fData)->SetTo(ref, B_READ_ONLY);

	if (fStatus == B_OK)
		SetToRFC822(fData, ~0L);
}


/**
 * @brief Destroys the BEmailMessage and all owned resources.
 */
BEmailMessage::~BEmailMessage()
{
	free(fBCC);

	delete fBody;
	delete fData;
}


/**
 * @brief Returns the initialisation status of this message object.
 *
 * @return B_OK if the message was constructed and parsed without error.
 */
status_t
BEmailMessage::InitCheck() const
{
	return fStatus;
}


/**
 * @brief Creates a reply message pre-populated with subject, quote, and recipient.
 *
 * Depending on \a replyTo, the new message's To: field is set to the original
 * sender or to all recipients. The body is quoted using \a quoteStyle, and
 * "Re: " is prepended to the subject if not already present.
 *
 * @param replyTo         B_MAIL_REPLY_TO_SENDER or B_MAIL_REPLY_TO_ALL.
 * @param accountFromMail If true, selects the outbound account that received
 *                        the original message.
 * @param quoteStyle      Per-line prefix string for the quoted body (e.g. "> ").
 * @return Newly allocated BEmailMessage; caller takes ownership.
 */
BEmailMessage*
BEmailMessage::ReplyMessage(mail_reply_to_mode replyTo, bool accountFromMail,
	const char* quoteStyle)
{
	BEmailMessage* reply = new BEmailMessage;

	// Set ReplyTo:

	if (replyTo == B_MAIL_REPLY_TO_ALL) {
		reply->SetTo(From());

		BList list;
		get_address_list(list, CC(), extract_address);
		get_address_list(list, To(), extract_address);

		// Filter out the sender
		BMailAccounts accounts;
		BMailAccountSettings* account = accounts.AccountByID(Account());
		BString sender;
		if (account != NULL)
			sender = account->ReturnAddress();
		extract_address(sender);

		BString cc;

		for (int32 i = list.CountItems(); i-- > 0;) {
			char* address = (char*)list.RemoveItem((int32)0);

			// Add everything which is not the sender and not already in the
			// list
			if (sender.ICompare(address) && cc.FindFirst(address) < 0) {
				if (cc.Length() > 0)
					cc << ", ";

				cc << address;
			}

			free(address);
		}

		if (cc.Length() > 0)
			reply->SetCC(cc.String());
	} else if (replyTo == B_MAIL_REPLY_TO_SENDER || ReplyTo() == NULL)
		reply->SetTo(From());
	else
		reply->SetTo(ReplyTo());

	// Set special "In-Reply-To:" header (used for threading)
	const char* messageID = fBody ? fBody->HeaderField("Message-Id") : NULL;
	if (messageID != NULL)
		reply->SetHeaderField("In-Reply-To", messageID);

	// quote body text
	reply->SetBodyTextTo(BodyText());
	if (quoteStyle)
		reply->Body()->Quote(quoteStyle);

	// Set the subject (and add a "Re:" if needed)
	BString string = Subject();
	if (string.ICompare("re:", 3) != 0)
		string.Prepend("Re: ");
	reply->SetSubject(string.String());

	// set the matching outbound chain
	if (accountFromMail)
		reply->SendViaAccountFrom(this);

	return reply;
}


/**
 * @brief Creates a forward message with a quoted header summary and optional attachments.
 *
 * Builds a new message whose body contains a quoted header block (To, From,
 * Subject, Date) followed by the original body text. Appends "(fwd)" to the
 * subject if not already present. If \a includeAttachments is true, all
 * non-text components are cloned and attached.
 *
 * @param accountFromMail       If true, uses the account that received the original.
 * @param includeAttachments    If true, original attachments are forwarded too.
 * @return Newly allocated BEmailMessage; caller takes ownership.
 */
BEmailMessage*
BEmailMessage::ForwardMessage(bool accountFromMail, bool includeAttachments)
{
	BString header = "------ Forwarded Message: ------\n";
	header << "To: " << To() << '\n';
	header << "From: " << From() << '\n';
	if (CC() != NULL) {
		// Can use CC rather than "Cc" since display only.
		header << "CC: " << CC() << '\n';
	}
	header << "Subject: " << Subject() << '\n';
	header << "Date: " << HeaderField("Date") << "\n\n";
	if (fTextBody != NULL)
		header << fTextBody->Text() << '\n';
	BEmailMessage *message = new BEmailMessage();
	message->SetBodyTextTo(header.String());

	// set the subject
	BString subject = Subject();
	if (subject.IFindFirst("fwd") == B_ERROR
		&& subject.IFindFirst("forward") == B_ERROR
		&& subject.FindFirst("FW") == B_ERROR)
		subject << " (fwd)";
	message->SetSubject(subject.String());

	if (includeAttachments) {
		for (int32 i = 0; i < CountComponents(); i++) {
			BMailComponent* component = GetComponent(i);
			if (component == fTextBody || component == NULL)
				continue;

			//---I am ashamed to have the written the code between here and the next comment
			// ... and you still managed to get it wrong ;-)), axeld.
			// we should really move this stuff into copy constructors
			// or something like that

			BMallocIO io;
			component->RenderToRFC822(&io);
			BMailComponent* clone = component->WhatIsThis();
			io.Seek(0, SEEK_SET);
			clone->SetToRFC822(&io, io.BufferLength(), true);
			message->AddComponent(clone);
		}
	}
	if (accountFromMail)
		message->SendViaAccountFrom(this);

	return message;
}


/**
 * @brief Returns the To header field value.
 *
 * @return Pointer to the To string, or NULL if not set.
 */
const char*
BEmailMessage::To() const
{
	return HeaderField("To");
}


/**
 * @brief Returns the From header field value.
 *
 * @return Pointer to the From string, or NULL if not set.
 */
const char*
BEmailMessage::From() const
{
	return HeaderField("From");
}


/**
 * @brief Returns the Reply-To header field value.
 *
 * @return Pointer to the Reply-To string, or NULL if not set.
 */
const char*
BEmailMessage::ReplyTo() const
{
	return HeaderField("Reply-To");
}


/**
 * @brief Returns the Cc header field value.
 *
 * @return Pointer to the Cc string, or NULL if not set.
 */
const char*
BEmailMessage::CC() const
{
	return HeaderField("Cc");
		// Note case of CC is "Cc" in our internal headers.
}


/**
 * @brief Returns the Subject header field value.
 *
 * @return Pointer to the Subject string, or NULL if not set.
 */
const char*
BEmailMessage::Subject() const
{
	return HeaderField("Subject");
}


/**
 * @brief Returns the message date as a time_t.
 *
 * Parses the Date header using ParseDateWithTimeZone().
 *
 * @return Unix timestamp, or -1 if the Date header is absent or unparseable.
 */
time_t
BEmailMessage::Date() const
{
	const char* dateField = HeaderField("Date");
	if (dateField == NULL)
		return -1;

	return ParseDateWithTimeZone(dateField);
}


/**
 * @brief Returns the numeric message priority on a 1–5 scale.
 *
 * Checks X-Priority, Priority, and X-Msmail-Priority headers in order of
 * preference. Returns 3 (normal) if no priority header is found or if the
 * value cannot be parsed.
 *
 * @return Priority value from 1 (highest) to 5 (lowest).
 */
int
BEmailMessage::Priority() const
{
	int priorityNumber;
	const char* priorityString;

	/* The usual values are a number from 1 to 5, or one of three words:
	X-Priority: 1 and/or X-MSMail-Priority: High
	X-Priority: 3 and/or X-MSMail-Priority: Normal
	X-Priority: 5 and/or X-MSMail-Priority: Low
	Also plain Priority: is "normal", "urgent" or "non-urgent", see RFC 1327. */

	priorityString = HeaderField("Priority");
	if (priorityString == NULL)
		priorityString = HeaderField("X-Priority");
	if (priorityString == NULL)
		priorityString = HeaderField("X-Msmail-Priority");
	if (priorityString == NULL)
		return 3;
	priorityNumber = atoi (priorityString);
	if (priorityNumber != 0) {
		if (priorityNumber > 5)
			priorityNumber = 5;
		if (priorityNumber < 1)
			priorityNumber = 1;
		return priorityNumber;
	}
	if (strcasecmp (priorityString, "Low") == 0
		|| strcasecmp (priorityString, "non-urgent") == 0)
		return 5;
	if (strcasecmp (priorityString, "High") == 0
		|| strcasecmp (priorityString, "urgent") == 0)
		return 1;
	return 3;
}


/**
 * @brief Sets the Subject header with optional charset and encoding hints.
 *
 * @param subject   New subject string (UTF-8).
 * @param charset   Character-set constant for encoding, or B_MAIL_NULL_CONVERSION.
 * @param encoding  Transfer encoding, or null_encoding.
 */
void
BEmailMessage::SetSubject(const char* subject, uint32 charset,
	mail_encoding encoding)
{
	SetHeaderField("Subject", subject, charset, encoding);
}


/**
 * @brief Sets the Reply-To header with optional charset and encoding hints.
 *
 * @param replyTo   Reply-To address string.
 * @param charset   Character-set constant, or B_MAIL_NULL_CONVERSION.
 * @param encoding  Transfer encoding, or null_encoding.
 */
void
BEmailMessage::SetReplyTo(const char* replyTo, uint32 charset,
	mail_encoding encoding)
{
	SetHeaderField("Reply-To", replyTo, charset, encoding);
}


/**
 * @brief Sets the From header with optional charset and encoding hints.
 *
 * @param from      From address string.
 * @param charset   Character-set constant, or B_MAIL_NULL_CONVERSION.
 * @param encoding  Transfer encoding, or null_encoding.
 */
void
BEmailMessage::SetFrom(const char* from, uint32 charset, mail_encoding encoding)
{
	SetHeaderField("From", from, charset, encoding);
}


/**
 * @brief Sets the To header with optional charset and encoding hints.
 *
 * @param to        Recipient address string.
 * @param charset   Character-set constant, or B_MAIL_NULL_CONVERSION.
 * @param encoding  Transfer encoding, or null_encoding.
 */
void
BEmailMessage::SetTo(const char* to, uint32 charset, mail_encoding encoding)
{
	SetHeaderField("To", to, charset, encoding);
}


/**
 * @brief Sets the Cc header with optional charset and encoding hints.
 *
 * @param cc        Carbon-copy address string.
 * @param charset   Character-set constant, or B_MAIL_NULL_CONVERSION.
 * @param encoding  Transfer encoding, or null_encoding.
 */
void
BEmailMessage::SetCC(const char* cc, uint32 charset, mail_encoding encoding)
{
	// For consistency with our header names, use Cc as the name.
	SetHeaderField("Cc", cc, charset, encoding);
}


/**
 * @brief Sets the blind carbon-copy list (not written to file headers).
 *
 * The BCC list is used only when building the SMTP recipient list and is
 * never included in the message headers written to disk.
 *
 * @param bcc  Comma-separated BCC address string.
 */
void
BEmailMessage::SetBCC(const char* bcc)
{
	free(fBCC);
	fBCC = strdup(bcc);
}


/**
 * @brief Sets the message priority, writing all three priority header fields.
 *
 * Clamps \a to to the range [1, 5] and sets X-Priority, Priority, and
 * X-Msmail-Priority accordingly.
 *
 * @param to  Desired priority level from 1 (high) to 5 (low).
 */
void
BEmailMessage::SetPriority(int to)
{
	char tempString[20];

	if (to < 1)
		to = 1;
	if (to > 5)
		to = 5;
	sprintf (tempString, "%d", to);
	SetHeaderField("X-Priority", tempString);
	if (to <= 2) {
		SetHeaderField("Priority", "urgent");
		SetHeaderField("X-Msmail-Priority", "High");
	} else if (to >= 4) {
		SetHeaderField("Priority", "non-urgent");
		SetHeaderField("X-Msmail-Priority", "Low");
	} else {
		SetHeaderField("Priority", "normal");
		SetHeaderField("X-Msmail-Priority", "Normal");
	}
}


/**
 * @brief Reads the sender name from the B_MAIL_ATTR_NAME BFS attribute.
 *
 * @param name       Output buffer to receive the null-terminated name.
 * @param maxLength  Size of \a name in bytes.
 * @return B_OK on success, B_BAD_VALUE if arguments are invalid, B_ERROR if
 *         the message is not backed by a BFile.
 */
status_t
BEmailMessage::GetName(char* name, int32 maxLength) const
{
	if (name == NULL || maxLength <= 0)
		return B_BAD_VALUE;

	if (BFile* file = dynamic_cast<BFile*>(fData)) {
		status_t status = file->ReadAttr(B_MAIL_ATTR_NAME, B_STRING_TYPE, 0,
			name, maxLength);
		name[maxLength - 1] = '\0';

		return status >= 0 ? B_OK : status;
	}
	// TODO: look at From header?  But usually there is
	// a file since only the BeMail GUI calls this.
	return B_ERROR;
}


/**
 * @brief Reads the sender name from the B_MAIL_ATTR_NAME BFS attribute into a BString.
 *
 * @param name  Output BString to receive the sender name.
 * @return B_OK on success, or an error code from GetName(char*, int32).
 */
status_t
BEmailMessage::GetName(BString* name) const
{
	char* buffer = name->LockBuffer(B_FILE_NAME_LENGTH);
	status_t status = GetName(buffer, B_FILE_NAME_LENGTH);
	name->UnlockBuffer();

	return status;
}


/**
 * @brief Sets the sending account based on the account that received \a message.
 *
 * Reads the account name from \a message's BFS attributes and calls
 * SendViaAccount(const char*). Does nothing if the account name cannot be
 * determined.
 *
 * @param message  The received message whose account should be used for replies.
 */
void
BEmailMessage::SendViaAccountFrom(BEmailMessage* message)
{
	BString name;
	if (message->GetAccountName(name) < B_OK) {
		// just return the message with the default account
		return;
	}

	SendViaAccount(name);
}


/**
 * @brief Selects the outbound account by name and updates the From header.
 *
 * Looks up the account by name in BMailAccounts, then calls
 * SendViaAccount(int32) with the resolved ID.
 *
 * @param accountName  Account name string as stored in BMailAccountSettings.
 */
void
BEmailMessage::SendViaAccount(const char* accountName)
{
	BMailAccounts accounts;
	BMailAccountSettings* account = accounts.AccountByName(accountName);
	if (account != NULL)
		SendViaAccount(account->AccountID());
}


/**
 * @brief Selects the outbound account by ID and updates the From header.
 *
 * Reads the account's real name and return address and formats them into the
 * From header as "Real Name" <address>.
 *
 * @param account  Numeric account ID from BMailAccountSettings::AccountID().
 */
void
BEmailMessage::SendViaAccount(int32 account)
{
	fAccountID = account;

	BMailAccounts accounts;
	BMailAccountSettings* accountSettings = accounts.AccountByID(fAccountID);

	BString from;
	if (accountSettings) {
		from << '\"' << accountSettings->RealName() << "\" <"
			<< accountSettings->ReturnAddress() << '>';
	}
	SetFrom(from);
}


/**
 * @brief Returns the account ID of the outbound account set for this message.
 *
 * @return Numeric outbound account ID.
 */
int32
BEmailMessage::Account() const
{
	return fAccountID;
}


/**
 * @brief Reads the account name from the B_MAIL_ATTR_ACCOUNT BFS attribute.
 *
 * @param accountName  Output BString to receive the account name.
 * @return B_OK on success, B_ERROR if the message has no backing BFile or
 *         the attribute is missing.
 */
status_t
BEmailMessage::GetAccountName(BString& accountName) const
{
	BFile* file = dynamic_cast<BFile*>(fData);
	if (file == NULL)
		return B_ERROR;

	int32 accountID;
	size_t read = file->ReadAttr(B_MAIL_ATTR_ACCOUNT, B_INT32_TYPE, 0,
		&accountID, sizeof(int32));
	if (read < sizeof(int32))
		return B_ERROR;

	BMailAccounts accounts;
	BMailAccountSettings* account =  accounts.AccountByID(accountID);
	if (account != NULL)
		accountName = account->Name();
	else
		accountName = "";

	return B_OK;
}


/**
 * @brief Adds a body component (text part or attachment) to this message.
 *
 * Manages the progressive build-up of the body structure: the first component
 * becomes the sole body, the second triggers creation of a
 * BMIMEMultipartMailContainer, and subsequent components are appended to that
 * container.
 *
 * @param component  Component to add; the message takes ownership.
 * @return B_OK on success, B_MISMATCHED_VALUES if the body is not a container
 *         when more than one component is present.
 */
status_t
BEmailMessage::AddComponent(BMailComponent* component)
{
	status_t status = B_OK;

	if (fComponentCount == 0)
		fBody = component;
	else if (fComponentCount == 1) {
		BMIMEMultipartMailContainer *container
			= new BMIMEMultipartMailContainer(
				mime_boundary, mime_warning, _charSetForTextDecoding);
		status = container->AddComponent(fBody);
		if (status == B_OK)
			status = container->AddComponent(component);
		fBody = container;
	} else {
		BMIMEMultipartMailContainer* container
			= dynamic_cast<BMIMEMultipartMailContainer*>(fBody);
		if (container == NULL)
			return B_MISMATCHED_VALUES;

		status = container->AddComponent(component);
	}

	if (status == B_OK)
		fComponentCount++;
	return status;
}


/**
 * @brief Stub: component removal is not yet implemented.
 *
 * @return B_ERROR always.
 */
status_t
BEmailMessage::RemoveComponent(BMailComponent* /*component*/)
{
	// not yet implemented
	// BeMail/Enclosures.cpp:169: contains a warning about this fact
	return B_ERROR;
}


/**
 * @brief Stub: component removal by index is not yet implemented.
 *
 * @return B_ERROR always.
 */
status_t
BEmailMessage::RemoveComponent(int32 /*index*/)
{
	// not yet implemented
	return B_ERROR;
}


/**
 * @brief Returns the body component at the given index.
 *
 * If the body is a multipart container, delegates to
 * BMIMEMultipartMailContainer::GetComponent(). If the body is a single
 * component and \a i is 0, returns it directly.
 *
 * @param i         Zero-based component index.
 * @param parseNow  If true, force immediate decoding of the sub-part.
 * @return Pointer to the component, or NULL if out of range.
 */
BMailComponent*
BEmailMessage::GetComponent(int32 i, bool parseNow)
{
	if (BMIMEMultipartMailContainer* container
			= dynamic_cast<BMIMEMultipartMailContainer*>(fBody))
		return container->GetComponent(i, parseNow);

	if (i < fComponentCount)
		return fBody;

	return NULL;
}


/**
 * @brief Returns the total number of body components.
 *
 * @return Component count (1 for single-part, >1 for multipart).
 */
int32
BEmailMessage::CountComponents() const
{
	return fComponentCount;
}


/**
 * @brief Attaches a file to the message by entry_ref.
 *
 * Uses BAttributedMailAttachment if \a includeAttributes is true, otherwise
 * BSimpleMailAttachment.
 *
 * @param ref                entry_ref of the file to attach.
 * @param includeAttributes  If true, preserve BFS extended attributes.
 */
void
BEmailMessage::Attach(entry_ref* ref, bool includeAttributes)
{
	if (includeAttributes)
		AddComponent(new BAttributedMailAttachment(ref));
	else
		AddComponent(new BSimpleMailAttachment(ref));
}


/**
 * @brief Returns true if the component at \a i is an attachment.
 *
 * @param i  Zero-based component index.
 * @return true if the component reports itself as an attachment.
 */
bool
BEmailMessage::IsComponentAttachment(int32 i)
{
	if ((i >= fComponentCount) || (fComponentCount == 0))
		return false;

	if (fComponentCount == 1)
		return fBody->IsAttachment();

	BMIMEMultipartMailContainer* container
		= dynamic_cast<BMIMEMultipartMailContainer*>(fBody);
	if (container == NULL)
		return false;

	BMailComponent* component = container->GetComponent(i);
	if (component == NULL)
		return false;

	return component->IsAttachment();
}


/**
 * @brief Sets the plain-text body, creating the text component if needed.
 *
 * If no text component exists yet, a new BTextMailComponent is created and
 * added via AddComponent(). The text is then set on the component.
 *
 * @param text  UTF-8 plain-text body content.
 */
void
BEmailMessage::SetBodyTextTo(const char* text)
{
	if (fTextBody == NULL) {
		fTextBody = new BTextMailComponent;
		AddComponent(fTextBody);
	}

	fTextBody->SetText(text);
}


/**
 * @brief Returns the plain-text body component, searching the tree if necessary.
 *
 * If fTextBody is not set, walks the component tree looking for a
 * B_MAIL_PLAIN_TEXT_BODY component.
 *
 * @return Pointer to the BTextMailComponent, or NULL if none exists.
 */
BTextMailComponent*
BEmailMessage::Body()
{
	if (fTextBody == NULL)
		fTextBody = _RetrieveTextBody(fBody);

	return fTextBody;
}


/**
 * @brief Returns the body text as a UTF-8 C string.
 *
 * @return Pointer to the body text, or NULL if there is no text component.
 */
const char*
BEmailMessage::BodyText()
{
	if (Body() == NULL)
		return NULL;

	return fTextBody->Text();
}


/**
 * @brief Sets an explicit text body component, replacing the default one.
 *
 * @param body  BTextMailComponent to use as the body.
 * @return B_OK on success, B_ERROR if a text body has already been set.
 */
status_t
BEmailMessage::SetBody(BTextMailComponent* body)
{
	if (fTextBody != NULL) {
		return B_ERROR;
//	removing doesn't exist for now
//		RemoveComponent(fTextBody);
//		delete fTextBody;
	}
	fTextBody = body;
	AddComponent(fTextBody);

	return B_OK;
}


/**
 * @brief Recursively searches a component tree for the first plain-text body part.
 *
 * @param component  Root of the component tree to search.
 * @return Pointer to the first BTextMailComponent found, or NULL.
 */
BTextMailComponent*
BEmailMessage::_RetrieveTextBody(BMailComponent* component)
{
	BTextMailComponent* body = dynamic_cast<BTextMailComponent*>(component);
	if (body != NULL)
		return body;

	BMIMEMultipartMailContainer* container
		= dynamic_cast<BMIMEMultipartMailContainer*>(component);
	if (container != NULL) {
		for (int32 i = 0; i < container->CountComponents(); i++) {
			if ((component = container->GetComponent(i)) == NULL)
				continue;

			switch (component->ComponentType()) {
				case B_MAIL_PLAIN_TEXT_BODY:
					// AttributedAttachment returns the MIME type of its
					// contents, so we have to use dynamic_cast here
					body = dynamic_cast<BTextMailComponent*>(
						container->GetComponent(i));
					if (body != NULL)
						return body;
					break;

				case B_MAIL_MULTIPART_CONTAINER:
					body = _RetrieveTextBody(container->GetComponent(i));
					if (body != NULL)
						return body;
					break;
			}
		}
	}
	return NULL;
}


/**
 * @brief Parses an RFC 822 message from a stream, building the body component tree.
 *
 * Reads the account ID attribute if the stream is a BFile, parses the
 * top-level headers, instantiates the appropriate body component type, and
 * separates envelope headers (Subject, To, From, etc.) from body headers.
 *
 * @param mailFile  Readable stream containing the complete RFC 822 message.
 * @param length    Maximum bytes to parse (use ~0L to read to end).
 * @param parseNow  If true, decode body components immediately.
 * @return B_OK on success, or a negative error code on parse failure.
 */
status_t
BEmailMessage::SetToRFC822(BPositionIO* mailFile, size_t length,
	bool parseNow)
{
	if (BFile* file = dynamic_cast<BFile*>(mailFile)) {
		file->ReadAttr(B_MAIL_ATTR_ACCOUNT_ID, B_INT32_TYPE, 0, &fAccountID,
			sizeof(fAccountID));
	}

	mailFile->Seek(0, SEEK_END);
	length = mailFile->Position();
	mailFile->Seek(0, SEEK_SET);

	fStatus = BMailComponent::SetToRFC822(mailFile, length, parseNow);
	if (fStatus < B_OK)
		return fStatus;

	fBody = WhatIsThis();

	mailFile->Seek(0, SEEK_SET);
	fStatus = fBody->SetToRFC822(mailFile, length, parseNow);
	if (fStatus < B_OK)
		return fStatus;

	// Move headers that we use to us, everything else to fBody
	const char* name;
	for (int32 i = 0; (name = fBody->HeaderAt(i)) != NULL; i++) {
		if (strcasecmp(name, "Subject") != 0
			&& strcasecmp(name, "To") != 0
			&& strcasecmp(name, "From") != 0
			&& strcasecmp(name, "Reply-To") != 0
			&& strcasecmp(name, "Cc") != 0
			&& strcasecmp(name, "Priority") != 0
			&& strcasecmp(name, "X-Priority") != 0
			&& strcasecmp(name, "X-Msmail-Priority") != 0
			&& strcasecmp(name, "Date") != 0) {
			RemoveHeader(name);
		}
	}

	fBody->RemoveHeader("Subject");
	fBody->RemoveHeader("To");
	fBody->RemoveHeader("From");
	fBody->RemoveHeader("Reply-To");
	fBody->RemoveHeader("Cc");
	fBody->RemoveHeader("Priority");
	fBody->RemoveHeader("X-Priority");
	fBody->RemoveHeader("X-Msmail-Priority");
	fBody->RemoveHeader("Date");

	fComponentCount = 1;
	if (BMIMEMultipartMailContainer* container
			= dynamic_cast<BMIMEMultipartMailContainer*>(fBody))
		fComponentCount = container->CountComponents();

	return B_OK;
}


/**
 * @brief Renders this message as RFC 822 and writes it to an output stream.
 *
 * Sets the From header if not already set, builds the SMTP recipient list
 * from To/CC/BCC, generates the Date and Message-Id headers, calls the
 * base-class header renderer, then renders the body. If the output stream
 * is a BFile, writes BFS attributes (type, recipients, status, etc.) after
 * writing the message content.
 *
 * @param file  Output stream (typically a BFile in /boot/home/mail/out).
 * @return B_OK on success, B_MAIL_INVALID_MAIL if the body is NULL, or an
 *         IO error code.
 */
status_t
BEmailMessage::RenderToRFC822(BPositionIO* file)
{
	if (fBody == NULL)
		return B_MAIL_INVALID_MAIL;

	// Do real rendering

	if (From() == NULL) {
		// set the "From:" string
		SendViaAccount(fAccountID);
	}

	BList recipientList;
	get_address_list(recipientList, To(), extract_address);
	get_address_list(recipientList, CC(), extract_address);
	get_address_list(recipientList, fBCC, extract_address);

	BString recipients;
	for (int32 i = recipientList.CountItems(); i-- > 0;) {
		char *address = (char *)recipientList.RemoveItem((int32)0);

		recipients << '<' << address << '>';
		if (i)
			recipients << ',';

		free(address);
	}

	// add the date field
	time_t creationTime = time(NULL);
	{
		char date[128];
		struct tm tm;
		localtime_r(&creationTime, &tm);

		size_t length = strftime(date, sizeof(date),
			"%a, %d %b %Y %H:%M:%S", &tm);

		// GMT offsets are full hours, yes, but you never know :-)
		snprintf(date + length, sizeof(date) - length, " %+03d%02d",
			tm.tm_gmtoff / 3600, (tm.tm_gmtoff / 60) % 60);

		SetHeaderField("Date", date);
	}

	// add a message-id

	// empirical evidence indicates message id must be enclosed in
	// angle brackets and there must be an "at" symbol in it
	BString messageID;
	messageID << "<";
	messageID << system_time();
	messageID << "-BeMail@";

	char host[255];
	if (gethostname(host, sizeof(host)) < 0 || !host[0])
		strcpy(host, "zoidberg");

	messageID << host;
	messageID << ">";

	SetHeaderField("Message-Id", messageID.String());

	status_t err = BMailComponent::RenderToRFC822(file);
	if (err < B_OK)
		return err;

	file->Seek(-2, SEEK_CUR);
		// Remove division between headers

	err = fBody->RenderToRFC822(file);
	if (err < B_OK)
		return err;

	// Set the message file's attributes.  Do this after the rest of the file
	// is filled in, in case the daemon attempts to send it before it is ready
	// (since the daemon may send it when it sees the status attribute getting
	// set to "Pending").

	if (BFile* attributed = dynamic_cast <BFile*>(file)) {
		BNodeInfo(attributed).SetType(B_MAIL_TYPE);

		attributed->WriteAttrString(B_MAIL_ATTR_RECIPIENTS,&recipients);

		BString attr;

		attr = To();
		attributed->WriteAttrString(B_MAIL_ATTR_TO, &attr);
		attr = CC();
		attributed->WriteAttrString(B_MAIL_ATTR_CC, &attr);
		attr = Subject();
		if (attr.Length() <= 0)
			attr = "No Subject";
		attributed->WriteAttrString(B_MAIL_ATTR_SUBJECT, &attr);
		SubjectToThread(attr); // Extract the core subject words.
		attributed->WriteAttrString(B_MAIL_ATTR_THREAD, &attr);
		attr = ReplyTo();
		attributed->WriteAttrString(B_MAIL_ATTR_REPLY, &attr);
		attr = From();
		attributed->WriteAttrString(B_MAIL_ATTR_FROM, &attr);
		if (Priority() != 3 /* Normal is 3 */) {
			sprintf(attr.LockBuffer(40), "%d", Priority());
			attr.UnlockBuffer(-1);
			attributed->WriteAttrString(B_MAIL_ATTR_PRIORITY, &attr);
		}
		attr = "Pending";
		attributed->WriteAttrString(B_MAIL_ATTR_STATUS, &attr);
		attr = "1.0";
		attributed->WriteAttrString(B_MAIL_ATTR_MIME, &attr);

		attributed->WriteAttr(B_MAIL_ATTR_ACCOUNT, B_INT32_TYPE, 0,
			&fAccountID, sizeof(int32));

		attributed->WriteAttr(B_MAIL_ATTR_WHEN, B_TIME_TYPE, 0, &creationTime,
			sizeof(int32));
		int32 flags = B_MAIL_PENDING | B_MAIL_SAVE;
		attributed->WriteAttr(B_MAIL_ATTR_FLAGS, B_INT32_TYPE, 0, &flags,
			sizeof(int32));

		attributed->WriteAttr(B_MAIL_ATTR_ACCOUNT_ID, B_INT32_TYPE, 0,
			&fAccountID, sizeof(int32));
	}

	return B_OK;
}


/**
 * @brief Renders this message to a new file in \a dir with an auto-generated name.
 *
 * Generates a canonical filename from subject, date, and sender (same
 * algorithm as HaikuMailFormatFilter for incoming mail), creates the file in
 * \a dir, and calls RenderToRFC822() on it.
 *
 * @param dir  Directory in which to create the output mail file.
 * @param msg  Optional output BEntry set to the created file, or NULL.
 * @return B_OK on success, or an error code if file creation or rendering fails.
 */
status_t
BEmailMessage::RenderTo(BDirectory* dir, BEntry* msg)
{
	time_t currentTime;
	char numericDateString[40];
	struct tm timeFields;
	BString worker;

	// Generate a file name for the outgoing message.  See also
	// FolderFilter::ProcessMailMessage which does something similar for
	// incoming messages.

	BString name = Subject();
	SubjectToThread(name);
		// Extract the core subject words.
	if (name.Length() <= 0)
		name = "No Subject";
	if (name[0] == '.') {
		// Avoid hidden files, starting with a dot.
		name.Prepend("_");
	}

	// Convert the date into a year-month-day fixed digit width format, so that
	// sorting by file name will give all the messages with the same subject in
	// order of date.
	time (&currentTime);
	localtime_r (&currentTime, &timeFields);
	sprintf (numericDateString, "%04d%02d%02d%02d%02d%02d",
		timeFields.tm_year + 1900, timeFields.tm_mon + 1, timeFields.tm_mday,
		timeFields.tm_hour, timeFields.tm_min, timeFields.tm_sec);
	name << " " << numericDateString;

	worker = From();
	extract_address_name(worker);
	name << " " << worker;

	name.Truncate(222);	// reserve space for the uniquer

	// Get rid of annoying characters which are hard to use in the shell.
	name.ReplaceAll('/','_');
	name.ReplaceAll('\'','_');
	name.ReplaceAll('"','_');
	name.ReplaceAll('!','_');
	name.ReplaceAll('<','_');
	name.ReplaceAll('>','_');

	// Remove multiple spaces.
	while (name.FindFirst("  ") >= 0)
		name.Replace("  ", " ", 1024);

	int32 uniquer = time(NULL);
	worker = name;

	int32 tries = 30;
	bool exists;
	while ((exists = dir->Contains(worker.String())) && --tries > 0) {
		srand(rand());
		uniquer += (rand() >> 16) - 16384;

		worker = name;
		worker << ' ' << uniquer;
	}

	if (exists)
		printf("could not create mail! (should be: %s)\n", worker.String());

	BFile file;
	status_t status = dir->CreateFile(worker.String(), &file);
	if (status != B_OK)
		return status;

	if (msg != NULL)
		msg->SetTo(dir,worker.String());

	return RenderToRFC822(&file);
}


/**
 * @brief Renders and queues (or immediately sends) this message.
 *
 * Resolves the outbound account, determines the outbox directory, calls
 * RenderTo() to create the file, then optionally notifies the mail daemon
 * to send it immediately.
 *
 * @param sendNow  If true, notifies the mail daemon for immediate delivery.
 * @return B_OK on success, B_ERROR if no valid outbound account exists, or
 *         another error code from RenderTo().
 */
status_t
BEmailMessage::Send(bool sendNow)
{
	BMailAccounts accounts;
	BMailAccountSettings* account = accounts.AccountByID(fAccountID);
	if (account == NULL || !account->HasOutbound()) {
		account = accounts.AccountByID(
			BMailSettings().DefaultOutboundAccount());
		if (!account)
			return B_ERROR;
		SendViaAccount(account->AccountID());
	}

	BString path;
	if (account->OutboundSettings().FindString("path", &path) != B_OK) {
		BPath defaultMailOutPath;
		if (find_directory(B_USER_DIRECTORY, &defaultMailOutPath) != B_OK
			|| defaultMailOutPath.Append("mail/out") != B_OK)
			path = "/boot/home/mail/out";
		else
			path = defaultMailOutPath.Path();
	}

	create_directory(path.String(), 0777);
	BDirectory directory(path.String());

	BEntry message;

	status_t status = RenderTo(&directory, &message);
	if (status >= B_OK && sendNow) {
		// TODO: check whether or not the internet connection is available
		BMessenger daemon(B_MAIL_DAEMON_SIGNATURE);
		if (!daemon.IsValid())
			return B_MAIL_NO_DAEMON;

		BMessage msg(kMsgSendMessages);
		msg.AddInt32("account", fAccountID);
		BPath path;
		message.GetPath(&path);
		msg.AddString("message_path", path.Path());
		daemon.SendMessage(&msg);
	}

	return status;
}


void BEmailMessage::_ReservedMessage1() {}
void BEmailMessage::_ReservedMessage2() {}
void BEmailMessage::_ReservedMessage3() {}
