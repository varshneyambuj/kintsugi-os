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
 *       Adrien Destugues, pulkomandy@pulkomandy.tk
 */


/**
 * @file UrlProtocolRoster.cpp
 * @brief Implementation of BUrlProtocolRoster, the URL request factory.
 *
 * Provides a single static factory method that inspects the URL scheme and
 * instantiates the appropriate BUrlRequest subclass (BHttpRequest,
 * BFileRequest, BDataRequest, or BGopherRequest). In the future this may be
 * extended to use a plug-in add-on interface.
 *
 * @see BUrlRequest, BHttpRequest, BGopherRequest
 */

#include <UrlProtocolRoster.h>

#include <new>

#include <DataRequest.h>
#include <Debug.h>
#include <FileRequest.h>
#include <GopherRequest.h>
#include <HttpRequest.h>
#include <UrlRequest.h>

using namespace BPrivate::Network;


/**
 * @brief Instantiate the correct BUrlRequest subclass for the given URL scheme.
 *
 * Supports "http", "https", "file", "data", and "gopher" schemes. The caller
 * takes ownership of the returned object.
 *
 * @param url       The URL to request; its protocol field selects the handler.
 * @param output    BDataIO sink for the response body.
 * @param listener  Lifecycle callback listener, or NULL.
 * @param context   URL context supplying cookies and proxy settings, or NULL.
 * @return A heap-allocated BUrlRequest subclass appropriate for \a url, or
 *         NULL if the scheme is not supported or allocation fails.
 */
/* static */ BUrlRequest*
BUrlProtocolRoster::MakeRequest(const BUrl& url, BDataIO* output,
	BUrlProtocolListener* listener, BUrlContext* context)
{
	// TODO: instanciate the correct BUrlProtocol using add-on interface
	if (url.Protocol() == "http") {
		return new(std::nothrow) BHttpRequest(url, output, false, "HTTP",
			listener, context);
	} else if (url.Protocol() == "https") {
		return new(std::nothrow) BHttpRequest(url, output, true, "HTTPS",
			listener, context);
	} else if (url.Protocol() == "file") {
		return new(std::nothrow) BFileRequest(url, output, listener, context);
	} else if (url.Protocol() == "data") {
		return new(std::nothrow) BDataRequest(url, output, listener, context);
	} else if (url.Protocol() == "gopher") {
		return new(std::nothrow) BGopherRequest(url, output, listener, context);
	}

	return NULL;
}
