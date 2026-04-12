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
 * @file UrlProtocolAsynchronousListener.cpp
 * @brief Implementation of BUrlProtocolAsynchronousListener.
 *
 * Registers itself as a BHandler on be_app so that protocol notifications
 * dispatched as BMessages from the protocol thread are delivered on the
 * application's message loop. In transparent mode a
 * BUrlProtocolDispatchingListener is automatically created and exposed as
 * SynchronousListener() so protocol threads can post to this handler.
 *
 * @see BUrlProtocolDispatchingListener, BUrlProtocolListener
 */


#include <UrlProtocolAsynchronousListener.h>

#include <new>

#include <AppKit.h>
#include <Archivable.h>
#include <Debug.h>
#include <String.h>
#include <UrlResult.h>

using namespace BPrivate::Network;


extern const char* kUrlProtocolMessageType;
extern const char* kUrlProtocolCaller;


/**
 * @brief Construct a BUrlProtocolAsynchronousListener and register with be_app.
 *
 * If \a transparent is true, an internal BUrlProtocolDispatchingListener is
 * created that posts messages back to this handler; the caller can retrieve
 * it via SynchronousListener() and pass it to BUrlRequest.
 *
 * @param transparent  true to create and expose an internal dispatching
 *                     listener, false for standalone use.
 */
BUrlProtocolAsynchronousListener::BUrlProtocolAsynchronousListener(
	bool transparent)
	:
	BHandler("UrlProtocolAsynchronousListener"),
	BUrlProtocolListener(),
	fSynchronousListener(NULL)
{
	if (be_app->Lock()) {
		be_app->AddHandler(this);
		be_app->Unlock();
	} else
		PRINT(("Cannot lock be_app\n"));

	if (transparent) {
		fSynchronousListener
			= new(std::nothrow) BUrlProtocolDispatchingListener(this);
	}
}


/**
 * @brief Destructor — unregisters from be_app and deletes the internal listener.
 */
BUrlProtocolAsynchronousListener::~BUrlProtocolAsynchronousListener()
{
	if (be_app->Lock()) {
		be_app->RemoveHandler(this);
		be_app->Unlock();
	}
	delete fSynchronousListener;
}


// #pragma mark Synchronous listener access


/**
 * @brief Return the internal dispatching listener, or NULL in non-transparent mode.
 *
 * The returned listener bridges from the synchronous BUrlProtocolListener
 * interface (called on the protocol thread) to this handler's MessageReceived()
 * (called on the application thread).
 *
 * @return A pointer to the internal BUrlProtocolDispatchingListener, or NULL.
 */
BUrlProtocolListener*
BUrlProtocolAsynchronousListener::SynchronousListener()
{
	return fSynchronousListener;
}


/**
 * @brief Decode a B_URL_PROTOCOL_NOTIFICATION message and invoke the matching callback.
 *
 * Extracts the caller pointer and notification type from the message, then
 * dispatches to the appropriate BUrlProtocolListener virtual method. Unknown
 * notifications are logged in debug builds. Non-notification messages are
 * forwarded to BHandler::MessageReceived().
 *
 * @param message  The BMessage received by this handler.
 */
void
BUrlProtocolAsynchronousListener::MessageReceived(BMessage* message)
{
	if (message->what != B_URL_PROTOCOL_NOTIFICATION) {
		BHandler::MessageReceived(message);
		return;
	}

	BUrlRequest* caller;
	if (message->FindPointer(kUrlProtocolCaller, (void**)&caller) != B_OK)
		return;

	int8 notification;
	if (message->FindInt8(kUrlProtocolMessageType, &notification) != B_OK)
		return;

	switch (notification) {
		case B_URL_PROTOCOL_CONNECTION_OPENED:
			ConnectionOpened(caller);
			break;

		case B_URL_PROTOCOL_HOSTNAME_RESOLVED:
			{
				const char* ip;
				message->FindString("url:ip", &ip);

				HostnameResolved(caller, ip);
			}
			break;

		case B_URL_PROTOCOL_RESPONSE_STARTED:
			ResponseStarted(caller);
			break;

		case B_URL_PROTOCOL_HEADERS_RECEIVED:
			HeadersReceived(caller);
			break;

		case B_URL_PROTOCOL_BYTES_WRITTEN:
			{
				size_t bytesWritten = message->FindInt32("url:bytesWritten");

				BytesWritten(caller, bytesWritten);
			}
			break;

		case B_URL_PROTOCOL_DOWNLOAD_PROGRESS:
			{
				off_t bytesReceived;
				off_t bytesTotal;
				message->FindInt64("url:bytesReceived", &bytesReceived);
				message->FindInt64("url:bytesTotal", &bytesTotal);

				DownloadProgress(caller, bytesReceived, bytesTotal);
			}
			break;

		case B_URL_PROTOCOL_UPLOAD_PROGRESS:
			{
				off_t bytesSent;
				off_t bytesTotal;
				message->FindInt64("url:bytesSent", &bytesSent);
				message->FindInt64("url:bytesTotal", &bytesTotal);

				UploadProgress(caller, bytesSent, bytesTotal);
			}
			break;

		case B_URL_PROTOCOL_REQUEST_COMPLETED:
			{
				bool success;
				message->FindBool("url:success", &success);

				RequestCompleted(caller, success);
			}
			break;

		case B_URL_PROTOCOL_DEBUG_MESSAGE:
			{
				BUrlProtocolDebugMessage type
					= (BUrlProtocolDebugMessage)message->FindInt32("url:type");
				BString text = message->FindString("url:text");

				DebugMessage(caller, type, text);
			}
			break;

		case B_URL_PROTOCOL_CERTIFICATE_VERIFICATION_FAILED:
			{
				const char* error = message->FindString("url:error");
				BCertificate* certificate;
				message->FindPointer("url:certificate", (void**)&certificate);
				bool result = CertificateVerificationFailed(caller,
					*certificate, error);

				BMessage reply;
				reply.AddBool("url:continue", result);
				message->SendReply(&reply);
			}
			break;

		default:
			PRINT(("BUrlProtocolAsynchronousListener: Unknown notification %d\n",
				notification));
			break;
	}
}
