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
 *   Copyright 2013 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Adrien Destugues, pulkomandy@pulkomandy.tk
 */


/**
 * @file DataRequest.cpp
 * @brief Implementation of BDataRequest, the data: URI scheme handler.
 *
 * BDataRequest parses RFC 2397 data: URIs, extracting the optional MIME type,
 * charset, and base64 flag from the meta-information section, decoding any
 * base64-encoded payload, and delivering the raw bytes to the configured output.
 *
 * @see BUrlRequest, BUrlProtocolRoster
 */


#include "DataRequest.h"

#include <AutoDeleter.h>
#include <HttpAuthentication.h>
#include <mail_encoding.h>
#include <stdio.h>

using namespace BPrivate::Network;


/**
 * @brief Construct a BDataRequest for a data: URI.
 *
 * @param url       The data: URI to parse and deliver.
 * @param output    BDataIO that receives the decoded payload bytes.
 * @param listener  Optional BUrlProtocolListener for progress callbacks.
 * @param context   BUrlContext providing shared session state.
 */
BDataRequest::BDataRequest(const BUrl& url, BDataIO* output,
	BUrlProtocolListener* listener,
	BUrlContext* context)
	:
	BUrlRequest(url, output, listener, context, "data URL parser", "data"),
	fResult()
{
	fResult.SetContentType("text/plain");
}


/**
 * @brief Return the result object for this request.
 *
 * @return Const reference to the BUrlResult populated after execution.
 */
const BUrlResult&
BDataRequest::Result() const
{
	return fResult;
}


/**
 * @brief Parse and deliver the data: URI payload.
 *
 * Extracts the base64 flag, MIME type, and charset from the meta section,
 * decodes a base64 payload if required, and writes the decoded bytes to the
 * output BDataIO.  Notifies the listener at connection-open, headers-received,
 * bytes-written, and download-progress milestones.
 *
 * @return B_OK on success, B_BAD_DATA on malformed or incorrectly padded base64,
 *         or an error code from BDataIO::WriteExactly.
 */
status_t
BDataRequest::_ProtocolLoop()
{
	BString mimeType;
	BString charset;
	const char* payload;
	ssize_t length;
	bool isBase64 = false;

	// The RFC says this uses a nonstandard scheme, so the path, query and
	// fragment are a bit nonsensical. It would be nice to handle them, but
	// some software (eg. WebKit) relies on data URIs with embedded "#" char
	// in the data...
	BString data = fUrl.UrlString();
	data.Remove(0, 5); // remove "data:"

	int separatorPosition = data.FindFirst(',');

	if (fListener != NULL)
		fListener->ConnectionOpened(this);

	if (separatorPosition >= 0) {
		BString meta = data;
		meta.Truncate(separatorPosition);
		data.Remove(0, separatorPosition + 1);

		int pos = 0;
		while (meta.Length() > 0) {
			// Extract next parameter
			pos = meta.FindFirst(';', pos);

			BString parameter = meta;
			if (pos >= 0) {
				parameter.Truncate(pos);
				meta.Remove(0, pos+1);
			} else
				meta.Truncate(0);

			// Interpret the parameter
			if (parameter == "base64") {
				isBase64 = true;
			} else if (parameter.FindFirst("charset=") == 0) {
				charset = parameter;
			} else {
				// Must be the MIME type
				mimeType = parameter;
			}
		}

		if (charset.Length() > 0)
			mimeType << ";" << charset;
		fResult.SetContentType(mimeType);

	}

	ArrayDeleter<char> buffer;
	if (isBase64) {
		// Check that the base64 data is properly padded (we process characters
		// by groups of 4 and there must not be stray chars at the end as
		// Base64 specifies padding.
		if (data.Length() & 3)
			return B_BAD_DATA;

		buffer.SetTo(new char[data.Length() * 3 / 4]);
		payload = buffer.Get();
			// payload must be a const char* so we can assign data.String() to
			// it below, but decode_64 modifies buffer.
		length = decode_base64(buffer.Get(), data.String(), data.Length());

		// There may be some padding at the end of the base64 stream. This
		// prevents us from computing the exact length we should get, so allow
		// for some error margin.
		if (length > data.Length() * 3 / 4
			|| length < data.Length() * 3 / 4 - 3) {
			return B_BAD_DATA;
		}
	} else {
		payload = data.String();
		length = data.Length();
	}

	fResult.SetLength(length);

	if (fListener != NULL)
		fListener->HeadersReceived(this);
	if (length > 0) {
		if (fOutput != NULL) {
			size_t written = 0;
			status_t err = fOutput->WriteExactly(payload, length, &written);
			if (fListener != NULL && written > 0)
				fListener->BytesWritten(this, written);
			if (err != B_OK)
				return err;
			if (fListener != NULL)
				fListener->DownloadProgress(this, written, written);
		}
	}

	return B_OK;
}
