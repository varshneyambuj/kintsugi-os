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
 *   Copyright 2010-2017 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christophe Huriaux, c.huriaux@gmail.com
 *       Adrien Destugues, pulkomandy@pulkomandy.tk
 */


/**
 * @file UrlProtocolDispatchingListener.cpp
 * @brief Implementation of BUrlProtocolDispatchingListener.
 *
 * Implements the synchronous BUrlProtocolListener interface by converting
 * each callback into a B_URL_PROTOCOL_NOTIFICATION BMessage and posting it
 * to a target BHandler or BMessenger. Certificate verification callbacks use
 * a synchronous reply to return the user's decision back to the protocol
 * thread.
 *
 * @see BUrlProtocolAsynchronousListener, BUrlProtocolListener
 */


#include <UrlProtocolDispatchingListener.h>

#include <Debug.h>
#include <UrlResult.h>

#include <assert.h>

using namespace BPrivate::Network;


const char* kUrlProtocolMessageType = "be:urlProtocolMessageType";
const char* kUrlProtocolCaller = "be:urlProtocolCaller";


/**
 * @brief Construct a dispatching listener that targets a BHandler.
 *
 * @param handler  The BHandler to which notification messages will be sent.
 */
BUrlProtocolDispatchingListener::BUrlProtocolDispatchingListener
	(BHandler* handler)
	:
	fMessenger(handler)
{
}


/**
 * @brief Construct a dispatching listener that targets a BMessenger.
 *
 * @param messenger  The BMessenger to which notification messages will be sent.
 */
BUrlProtocolDispatchingListener::BUrlProtocolDispatchingListener
	(const BMessenger& messenger)
	:
	fMessenger(messenger)
{
}


/**
 * @brief Destructor.
 */
BUrlProtocolDispatchingListener::~BUrlProtocolDispatchingListener()
{
}


/**
 * @brief Dispatch a ConnectionOpened notification message.
 *
 * @param caller  The BUrlRequest that opened the connection.
 */
void
BUrlProtocolDispatchingListener::ConnectionOpened(BUrlRequest* caller)
{
	BMessage message(B_URL_PROTOCOL_NOTIFICATION);
	_SendMessage(&message, B_URL_PROTOCOL_CONNECTION_OPENED, caller);
}


/**
 * @brief Dispatch a HostnameResolved notification message.
 *
 * @param caller  The BUrlRequest that resolved the hostname.
 * @param ip      The resolved IP address string.
 */
void
BUrlProtocolDispatchingListener::HostnameResolved(BUrlRequest* caller,
	const char* ip)
{
	BMessage message(B_URL_PROTOCOL_NOTIFICATION);
	message.AddString("url:hostIp", ip);

	_SendMessage(&message, B_URL_PROTOCOL_HOSTNAME_RESOLVED, caller);
}


/**
 * @brief Dispatch a ResponseStarted notification message.
 *
 * @param caller  The BUrlRequest that started receiving the response.
 */
void
BUrlProtocolDispatchingListener::ResponseStarted(BUrlRequest* caller)
{
	BMessage message(B_URL_PROTOCOL_NOTIFICATION);
	_SendMessage(&message, B_URL_PROTOCOL_RESPONSE_STARTED, caller);
}


/**
 * @brief Dispatch a HeadersReceived notification message.
 *
 * @param caller  The BUrlRequest that finished receiving headers.
 */
void
BUrlProtocolDispatchingListener::HeadersReceived(BUrlRequest* caller)
{
	BMessage message(B_URL_PROTOCOL_NOTIFICATION);
	_SendMessage(&message, B_URL_PROTOCOL_HEADERS_RECEIVED, caller);
}


/**
 * @brief Dispatch a BytesWritten notification message.
 *
 * @param caller        The BUrlRequest that wrote data to its output.
 * @param bytesWritten  The number of bytes written.
 */
void
BUrlProtocolDispatchingListener::BytesWritten(BUrlRequest* caller,
	size_t bytesWritten)
{
	BMessage message(B_URL_PROTOCOL_NOTIFICATION);
	message.AddInt32("url:bytesWritten", bytesWritten);

	_SendMessage(&message, B_URL_PROTOCOL_BYTES_WRITTEN, caller);
}


/**
 * @brief Dispatch a DownloadProgress notification message.
 *
 * @param caller         The BUrlRequest making download progress.
 * @param bytesReceived  Total bytes received so far.
 * @param bytesTotal     Total expected bytes, or 0 if unknown.
 */
void
BUrlProtocolDispatchingListener::DownloadProgress(BUrlRequest* caller,
	off_t bytesReceived, off_t bytesTotal)
{
	BMessage message(B_URL_PROTOCOL_NOTIFICATION);
	message.AddInt64("url:bytesReceived", bytesReceived);
	message.AddInt64("url:bytesTotal", bytesTotal);

	_SendMessage(&message, B_URL_PROTOCOL_DOWNLOAD_PROGRESS, caller);
}


/**
 * @brief Dispatch an UploadProgress notification message.
 *
 * @param caller      The BUrlRequest making upload progress.
 * @param bytesSent   Total bytes sent so far.
 * @param bytesTotal  Total expected bytes to send.
 */
void
BUrlProtocolDispatchingListener::UploadProgress(BUrlRequest* caller,
	off_t bytesSent, off_t bytesTotal)
{
	BMessage message(B_URL_PROTOCOL_NOTIFICATION);
	message.AddInt64("url:bytesSent", bytesSent);
	message.AddInt64("url:bytesTotal", bytesTotal);

	_SendMessage(&message, B_URL_PROTOCOL_UPLOAD_PROGRESS, caller);
}


/**
 * @brief Dispatch a RequestCompleted notification message.
 *
 * @param caller   The BUrlRequest that completed.
 * @param success  true if the request completed without error.
 */
void
BUrlProtocolDispatchingListener::RequestCompleted(BUrlRequest* caller,
	bool success)
{
	BMessage message(B_URL_PROTOCOL_NOTIFICATION);
	message.AddBool("url:success", success);

	_SendMessage(&message, B_URL_PROTOCOL_REQUEST_COMPLETED, caller);
}


/**
 * @brief Dispatch a DebugMessage notification message.
 *
 * @param caller  The BUrlRequest emitting the debug message.
 * @param type    The BUrlProtocolDebugMessage category.
 * @param text    The debug message text.
 */
void
BUrlProtocolDispatchingListener::DebugMessage(BUrlRequest* caller,
	BUrlProtocolDebugMessage type, const char* text)
{
	BMessage message(B_URL_PROTOCOL_NOTIFICATION);
	message.AddInt32("url:type", type);
	message.AddString("url:text", text);

	_SendMessage(&message, B_URL_PROTOCOL_DEBUG_MESSAGE, caller);
}


/**
 * @brief Send a certificate verification failure message and wait for a reply.
 *
 * This is a synchronous call — the protocol thread blocks until the handler
 * replies. The certificate pointer is embedded in the message so the handler
 * can display it to the user.
 *
 * @param caller       The BUrlRequest encountering the certificate failure.
 * @param certificate  The BCertificate that failed verification.
 * @param error        A human-readable description of the failure.
 * @return The "url:continue" boolean from the handler's reply: true to
 *         proceed despite the failure, false to abort.
 */
bool
BUrlProtocolDispatchingListener::CertificateVerificationFailed(
	BUrlRequest* caller, BCertificate& certificate, const char* error)
{
	BMessage message(B_URL_PROTOCOL_NOTIFICATION);
	message.AddString("url:error", error);
	message.AddPointer("url:certificate", &certificate);
	message.AddInt8(kUrlProtocolMessageType,
		B_URL_PROTOCOL_CERTIFICATE_VERIFICATION_FAILED);
	message.AddPointer(kUrlProtocolCaller, caller);

	// Warning: synchronous reply
	BMessage reply;
	fMessenger.SendMessage(&message, &reply);

	return reply.FindBool("url:continue");
}


/**
 * @brief Internal helper that stamps and sends a notification message.
 *
 * Adds the caller pointer and notification type to \a message and sends it
 * via the stored BMessenger. In debug builds, asserts that the send succeeds.
 *
 * @param message       The partially-constructed BMessage to send.
 * @param notification  The B_URL_PROTOCOL_* notification constant.
 * @param caller        The BUrlRequest to embed as the caller pointer.
 */
void
BUrlProtocolDispatchingListener::_SendMessage(BMessage* message,
	int8 notification, BUrlRequest* caller)
{
	ASSERT(message != NULL);

	message->AddPointer(kUrlProtocolCaller, caller);
	message->AddInt8(kUrlProtocolMessageType, notification);

#ifdef DEBUG
	status_t result = fMessenger.SendMessage(message);
	ASSERT(result == B_OK);
#else
	fMessenger.SendMessage(message);
#endif
}
