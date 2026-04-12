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
 * @file UrlProtocolListener.cpp
 * @brief Default no-op implementations of BUrlProtocolListener callbacks.
 *
 * All virtual methods provide empty default bodies so that subclasses only
 * need to override the events they care about. The DebugMessage() default
 * implementation prints to stderr in debug builds.
 *
 * @see BUrlProtocolDispatchingListener, BUrlProtocolAsynchronousListener
 */

#include <cstdio>

#include <UrlRequest.h>
#include <UrlProtocolListener.h>

using namespace std;
using namespace BPrivate::Network;


/**
 * @brief Default no-op: called when a connection to the remote server is opened.
 *
 * @param  (unused) The BUrlRequest that opened the connection.
 */
void
BUrlProtocolListener::ConnectionOpened(BUrlRequest*)
{
}


/**
 * @brief Default no-op: called when the remote hostname has been resolved.
 *
 * @param  (unused) The BUrlRequest that resolved the hostname.
 * @param  (unused) The resolved IP address string.
 */
void
BUrlProtocolListener::HostnameResolved(BUrlRequest*, const char*)
{
}


/**
 * @brief Default handler: reject all certificate verification failures.
 *
 * Subclasses should override this to display the certificate to the user and
 * return true if they wish to proceed despite the failure.
 *
 * @param caller       The BUrlRequest that encountered the failure.
 * @param certificate  The BCertificate that failed verification.
 * @param message      A human-readable description of the failure.
 * @return false always in the default implementation.
 */
bool
BUrlProtocolListener::CertificateVerificationFailed(BUrlRequest* caller,
	BCertificate& certificate, const char* message)
{
	return false;
}


/**
 * @brief Default no-op: called when the server begins sending a response.
 *
 * @param  (unused) The BUrlRequest that received the response start.
 */
void
BUrlProtocolListener::ResponseStarted(BUrlRequest*)
{
}



/**
 * @brief Default no-op: called when all response headers have been received.
 *
 * @param  (unused) The BUrlRequest that received the headers.
 */
void
BUrlProtocolListener::HeadersReceived(BUrlRequest*)
{
}


/**
 * @brief Default no-op: called each time a chunk of data is written to output.
 *
 * @param  (unused) The BUrlRequest writing data.
 * @param  (unused) The number of bytes written in this chunk.
 */
void
BUrlProtocolListener::BytesWritten(BUrlRequest*, size_t)
{
}


/**
 * @brief Default no-op: called periodically as the download progresses.
 *
 * @param  (unused) The BUrlRequest making progress.
 * @param  (unused) Total bytes received so far.
 * @param  (unused) Total expected bytes, or 0 if unknown.
 */
void
BUrlProtocolListener::DownloadProgress(BUrlRequest*, off_t, off_t)
{
}


/**
 * @brief Default no-op: called periodically as the upload progresses.
 *
 * @param  (unused) The BUrlRequest making upload progress.
 * @param  (unused) Total bytes sent so far.
 * @param  (unused) Total expected bytes to send.
 */
void
BUrlProtocolListener::UploadProgress(BUrlRequest*, off_t, off_t)
{
}


/**
 * @brief Default no-op: called when the request thread has finished.
 *
 * @param  (unused) The BUrlRequest that completed.
 * @param  (unused) true if the request completed without error.
 */
void
BUrlProtocolListener::RequestCompleted(BUrlRequest*, bool)
{
}


/**
 * @brief Default debug message handler — prints to stderr in debug builds.
 *
 * Prepends a directional arrow based on \a type and prints the protocol name
 * and \a text to standard error. In non-debug builds this is a complete no-op.
 *
 * @param caller  The BUrlRequest emitting the message.
 * @param type    The BUrlProtocolDebugMessage category.
 * @param text    The debug message text.
 */
void
BUrlProtocolListener::DebugMessage(BUrlRequest* caller,
	BUrlProtocolDebugMessage type, const char* text)
{
#ifdef DEBUG
	switch (type) {
		case B_URL_PROTOCOL_DEBUG_TEXT:
			fprintf(stderr, "   ");
			break;

		case B_URL_PROTOCOL_DEBUG_ERROR:
			fprintf(stderr, "!!!");
			break;

		case B_URL_PROTOCOL_DEBUG_TRANSFER_IN:
		case B_URL_PROTOCOL_DEBUG_HEADER_IN:
			fprintf(stderr, "<--");
			break;

		case B_URL_PROTOCOL_DEBUG_TRANSFER_OUT:
		case B_URL_PROTOCOL_DEBUG_HEADER_OUT:
			fprintf(stderr, "-->");
			break;
	}

	fprintf(stderr, " %s: %s\n", caller->Protocol().String(), text);
#endif
}
