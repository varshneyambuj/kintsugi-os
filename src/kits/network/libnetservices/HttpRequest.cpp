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
 *   Copyright 2010-2021 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christophe Huriaux, c.huriaux@gmail.com
 *       Niels Sascha Reedijk, niels.reedijk@gmail.com
 *       Adrien Destugues, pulkomandy@pulkomandy.tk
 *       Stephan Aßmus, superstippi@gmx.de
 */


/**
 * @file HttpRequest.cpp
 * @brief Implementation of BHttpRequest, the HTTP/HTTPS protocol handler.
 *
 * Manages the full HTTP request/response lifecycle including connection
 * setup (plain or TLS), request serialisation, chunked transfer decoding,
 * gzip/deflate decompression, cookie handling, HTTP authentication, and
 * redirect following. Both HTTP/1.0 and HTTP/1.1 are supported.
 *
 * @see BNetworkRequest, BHttpAuthentication, BHttpForm, BHttpHeaders
 */


#include <HttpRequest.h>

#include <arpa/inet.h>
#include <stdio.h>

#include <cstdlib>
#include <deque>
#include <new>

#include <AutoDeleter.h>
#include <Certificate.h>
#include <Debug.h>
#include <DynamicBuffer.h>
#include <File.h>
#include <ProxySecureSocket.h>
#include <Socket.h>
#include <SecureSocket.h>
#include <StackOrHeapArray.h>
#include <ZlibCompressionAlgorithm.h>

using namespace BPrivate::Network;


static const int32 kHttpBufferSize = 4096;


namespace BPrivate {

	class CheckedSecureSocket: public BSecureSocket
	{
		public:
			CheckedSecureSocket(BHttpRequest* request);

			bool			CertificateVerificationFailed(BCertificate& certificate,
					const char* message);

		private:
			BHttpRequest*	fRequest;
	};


	/**
	 * @brief Construct a CheckedSecureSocket bound to the given BHttpRequest.
	 *
	 * @param request  The owning BHttpRequest whose certificate callback will
	 *                 be invoked on verification failure.
	 */
	CheckedSecureSocket::CheckedSecureSocket(BHttpRequest* request)
		:
		BSecureSocket(),
		fRequest(request)
	{
	}


	/**
	 * @brief Delegate certificate verification failure to the owning request.
	 *
	 * @param certificate  The certificate that failed verification.
	 * @param message      A human-readable description of the failure.
	 * @return true if the connection should continue despite the failure,
	 *         false to abort.
	 */
	bool
	CheckedSecureSocket::CertificateVerificationFailed(BCertificate& certificate,
		const char* message)
	{
		return fRequest->_CertificateVerificationFailed(certificate, message);
	}


	class CheckedProxySecureSocket: public BProxySecureSocket
	{
		public:
			CheckedProxySecureSocket(const BNetworkAddress& proxy, BHttpRequest* request);

			bool			CertificateVerificationFailed(BCertificate& certificate,
					const char* message);

		private:
			BHttpRequest*	fRequest;
	};


	/**
	 * @brief Construct a CheckedProxySecureSocket for a TLS-over-proxy connection.
	 *
	 * @param proxy    The proxy server address to tunnel through.
	 * @param request  The owning BHttpRequest whose certificate callback will
	 *                 be invoked on verification failure.
	 */
	CheckedProxySecureSocket::CheckedProxySecureSocket(const BNetworkAddress& proxy,
		BHttpRequest* request)
		:
		BProxySecureSocket(proxy),
		fRequest(request)
	{
	}


	/**
	 * @brief Delegate certificate verification failure to the owning request.
	 *
	 * @param certificate  The certificate that failed verification.
	 * @param message      A human-readable description of the failure.
	 * @return true if the connection should continue despite the failure,
	 *         false to abort.
	 */
	bool
	CheckedProxySecureSocket::CertificateVerificationFailed(BCertificate& certificate,
		const char* message)
	{
		return fRequest->_CertificateVerificationFailed(certificate, message);
	}
};


/**
 * @brief Construct a BHttpRequest for the given URL.
 *
 * Initialises all request options to their defaults via _ResetOptions().
 *
 * @param url           The HTTP or HTTPS URL to request.
 * @param output        BDataIO sink that receives the response body.
 * @param ssl           true for HTTPS, false for plain HTTP.
 * @param protocolName  Logical protocol name string (e.g. "HTTP" or "HTTPS").
 * @param listener      Lifecycle callback listener, or NULL.
 * @param context       URL context providing cookies and proxy config, or NULL.
 */
BHttpRequest::BHttpRequest(const BUrl& url, BDataIO* output, bool ssl,
	const char* protocolName, BUrlProtocolListener* listener,
	BUrlContext* context)
	:
	BNetworkRequest(url, output, listener, context, "BUrlProtocol.HTTP",
		protocolName),
	fSSL(ssl),
	fRequestMethod(B_HTTP_GET),
	fHttpVersion(B_HTTP_11),
	fResult(url),
	fRequestStatus(kRequestInitialState),
	fOptHeaders(NULL),
	fOptPostFields(NULL),
	fOptInputData(NULL),
	fOptInputDataSize(-1),
	fOptRangeStart(-1),
	fOptRangeEnd(-1),
	fOptFollowLocation(true)
{
	_ResetOptions();
	fSocket = NULL;
}


/**
 * @brief Copy constructor — creates a new request with the same URL and options.
 *
 * The copy starts in the initial state regardless of the source object's
 * current state. Some options (e.g. post fields) are not copied.
 *
 * @param other  The source BHttpRequest to copy settings from.
 */
BHttpRequest::BHttpRequest(const BHttpRequest& other)
	:
	BNetworkRequest(other.Url(), other.Output(), other.fListener,
		other.fContext, "BUrlProtocol.HTTP", other.fSSL ? "HTTPS" : "HTTP"),
	fSSL(other.fSSL),
	fRequestMethod(other.fRequestMethod),
	fHttpVersion(other.fHttpVersion),
	fResult(other.fUrl),
	fRequestStatus(kRequestInitialState),
	fOptHeaders(NULL),
	fOptPostFields(NULL),
	fOptInputData(NULL),
	fOptInputDataSize(-1),
	fOptRangeStart(other.fOptRangeStart),
	fOptRangeEnd(other.fOptRangeEnd),
	fOptFollowLocation(other.fOptFollowLocation)
{
	_ResetOptions();
		// FIXME some options may be copied from other instead.
	fSocket = NULL;
}


/**
 * @brief Destructor — stops the request and frees all owned resources.
 */
BHttpRequest::~BHttpRequest()
{
	Stop();

	delete fSocket;

	delete fOptInputData;
	delete fOptHeaders;
	delete fOptPostFields;
}


/**
 * @brief Set the HTTP method for the request (e.g. "GET", "POST", "PUT").
 *
 * @param method  The HTTP method string.
 */
void
BHttpRequest::SetMethod(const char* const method)
{
	fRequestMethod = method;
}


/**
 * @brief Enable or disable automatic redirect following.
 *
 * @param follow  true to follow Location redirects, false to stop at them.
 */
void
BHttpRequest::SetFollowLocation(bool follow)
{
	fOptFollowLocation = follow;
}


/**
 * @brief Set the maximum number of redirects to follow before giving up.
 *
 * @param redirections  The maximum redirect count (default: 8).
 */
void
BHttpRequest::SetMaxRedirections(int8 redirections)
{
	fOptMaxRedirs = redirections;
}


/**
 * @brief Set the Referer header value.
 *
 * @param referrer  The referrer URL string to send.
 */
void
BHttpRequest::SetReferrer(const BString& referrer)
{
	fOptReferer = referrer;
}


/**
 * @brief Set the User-Agent header value.
 *
 * @param agent  The user agent string to send.
 */
void
BHttpRequest::SetUserAgent(const BString& agent)
{
	fOptUserAgent = agent;
}


/**
 * @brief Configure whether the response body should be discarded.
 *
 * @param discard  true to discard received data without writing to output.
 */
void
BHttpRequest::SetDiscardData(bool discard)
{
	fOptDiscardData = discard;
}


/**
 * @brief Suppress all listener callbacks for this request.
 *
 * @param disable  true to suppress callbacks, false to restore them.
 */
void
BHttpRequest::SetDisableListener(bool disable)
{
	fOptDisableListener = disable;
}


/**
 * @brief Enable or disable automatic Referer header generation on redirects.
 *
 * @param enable  true to set the Referer to the previous URL on redirect.
 */
void
BHttpRequest::SetAutoReferrer(bool enable)
{
	fOptAutoReferer = enable;
}


/**
 * @brief Abort the request on 4xx or 5xx HTTP status codes.
 *
 * @param stop  true to stop as soon as an error status is received.
 */
void
BHttpRequest::SetStopOnError(bool stop)
{
	fOptStopOnError = stop;
}


/**
 * @brief Set the username for HTTP authentication.
 *
 * @param name  The username string.
 */
void
BHttpRequest::SetUserName(const BString& name)
{
	fOptUsername = name;
}


/**
 * @brief Set the password for HTTP authentication.
 *
 * @param password  The password string.
 */
void
BHttpRequest::SetPassword(const BString& password)
{
	fOptPassword = password;
}


/**
 * @brief Set the byte-range start position for a ranged request.
 *
 * Only takes effect when called before the request has been dispatched.
 *
 * @param position  The first byte offset to request.
 */
void
BHttpRequest::SetRangeStart(off_t position)
{
	// This field is used within the transfer loop, so only
	// allow setting it before sending the request.
	if (fRequestStatus == kRequestInitialState)
		fOptRangeStart = position;
}


/**
 * @brief Set the byte-range end position for a ranged request.
 *
 * Only takes effect when called before the request has been dispatched.
 *
 * @param position  The last byte offset to request (inclusive).
 */
void
BHttpRequest::SetRangeEnd(off_t position)
{
	// This field could be used in the transfer loop, so only
	// allow setting it before sending the request.
	if (fRequestStatus == kRequestInitialState)
		fOptRangeEnd = position;
}


/**
 * @brief Set the POST form data by copying the provided BHttpForm.
 *
 * Implicitly switches the request method to POST.
 *
 * @param fields  The BHttpForm to copy and use as the POST body.
 */
void
BHttpRequest::SetPostFields(const BHttpForm& fields)
{
	AdoptPostFields(new(std::nothrow) BHttpForm(fields));
}


/**
 * @brief Set the custom request headers by copying the provided BHttpHeaders.
 *
 * @param headers  The BHttpHeaders to copy and merge into the outgoing request.
 */
void
BHttpRequest::SetHeaders(const BHttpHeaders& headers)
{
	AdoptHeaders(new(std::nothrow) BHttpHeaders(headers));
}


/**
 * @brief Take ownership of a heap-allocated BHttpForm for the POST body.
 *
 * Deletes any previously set post fields. Implicitly switches the method to POST.
 *
 * @param fields  A heap-allocated BHttpForm. The request takes ownership.
 */
void
BHttpRequest::AdoptPostFields(BHttpForm* const fields)
{
	delete fOptPostFields;
	fOptPostFields = fields;

	if (fOptPostFields != NULL)
		fRequestMethod = B_HTTP_POST;
}


/**
 * @brief Take ownership of a BDataIO for streaming the request body.
 *
 * Used for POST or PUT requests where the body data comes from an arbitrary
 * stream rather than a BHttpForm. If \a size is negative, chunked transfer
 * encoding is used.
 *
 * @param data  A heap-allocated BDataIO. The request takes ownership.
 * @param size  Total byte count of the stream, or -1 for unknown (chunked).
 */
void
BHttpRequest::AdoptInputData(BDataIO* const data, const ssize_t size)
{
	delete fOptInputData;
	fOptInputData = data;
	fOptInputDataSize = size;
}


/**
 * @brief Take ownership of a heap-allocated BHttpHeaders for custom headers.
 *
 * Deletes any previously set custom headers.
 *
 * @param headers  A heap-allocated BHttpHeaders. The request takes ownership.
 */
void
BHttpRequest::AdoptHeaders(BHttpHeaders* const headers)
{
	delete fOptHeaders;
	fOptHeaders = headers;
}


/**
 * @brief Test whether an HTTP status code is in the 1xx informational range.
 *
 * @param code  The HTTP status code to test.
 * @return true if \a code is between 100 and 199 inclusive, false otherwise.
 */
/*static*/ bool
BHttpRequest::IsInformationalStatusCode(int16 code)
{
	return (code >= B_HTTP_STATUS__INFORMATIONAL_BASE)
		&& (code <  B_HTTP_STATUS__INFORMATIONAL_END);
}


/**
 * @brief Test whether an HTTP status code is in the 2xx success range.
 *
 * @param code  The HTTP status code to test.
 * @return true if \a code is between 200 and 299 inclusive, false otherwise.
 */
/*static*/ bool
BHttpRequest::IsSuccessStatusCode(int16 code)
{
	return (code >= B_HTTP_STATUS__SUCCESS_BASE)
		&& (code <  B_HTTP_STATUS__SUCCESS_END);
}


/**
 * @brief Test whether an HTTP status code is in the 3xx redirection range.
 *
 * @param code  The HTTP status code to test.
 * @return true if \a code is between 300 and 399 inclusive, false otherwise.
 */
/*static*/ bool
BHttpRequest::IsRedirectionStatusCode(int16 code)
{
	return (code >= B_HTTP_STATUS__REDIRECTION_BASE)
		&& (code <  B_HTTP_STATUS__REDIRECTION_END);
}


/**
 * @brief Test whether an HTTP status code is in the 4xx client-error range.
 *
 * @param code  The HTTP status code to test.
 * @return true if \a code is between 400 and 499 inclusive, false otherwise.
 */
/*static*/ bool
BHttpRequest::IsClientErrorStatusCode(int16 code)
{
	return (code >= B_HTTP_STATUS__CLIENT_ERROR_BASE)
		&& (code <  B_HTTP_STATUS__CLIENT_ERROR_END);
}


/**
 * @brief Test whether an HTTP status code is in the 5xx server-error range.
 *
 * @param code  The HTTP status code to test.
 * @return true if \a code is between 500 and 599 inclusive, false otherwise.
 */
/*static*/ bool
BHttpRequest::IsServerErrorStatusCode(int16 code)
{
	return (code >= B_HTTP_STATUS__SERVER_ERROR_BASE)
		&& (code <  B_HTTP_STATUS__SERVER_ERROR_END);
}


/**
 * @brief Return the broad status class for an HTTP status code.
 *
 * @param code  The HTTP status code to classify.
 * @return One of B_HTTP_STATUS_CLASS_INFORMATIONAL,
 *         B_HTTP_STATUS_CLASS_SUCCESS, B_HTTP_STATUS_CLASS_REDIRECTION,
 *         B_HTTP_STATUS_CLASS_CLIENT_ERROR, B_HTTP_STATUS_CLASS_SERVER_ERROR,
 *         or B_HTTP_STATUS_CLASS_INVALID.
 */
/*static*/ int16
BHttpRequest::StatusCodeClass(int16 code)
{
	if (BHttpRequest::IsInformationalStatusCode(code))
		return B_HTTP_STATUS_CLASS_INFORMATIONAL;
	else if (BHttpRequest::IsSuccessStatusCode(code))
		return B_HTTP_STATUS_CLASS_SUCCESS;
	else if (BHttpRequest::IsRedirectionStatusCode(code))
		return B_HTTP_STATUS_CLASS_REDIRECTION;
	else if (BHttpRequest::IsClientErrorStatusCode(code))
		return B_HTTP_STATUS_CLASS_CLIENT_ERROR;
	else if (BHttpRequest::IsServerErrorStatusCode(code))
		return B_HTTP_STATUS_CLASS_SERVER_ERROR;

	return B_HTTP_STATUS_CLASS_INVALID;
}


/**
 * @brief Return the result object from the most recently completed request.
 *
 * @return A const reference to the internal BUrlResult (cast to BHttpResult)
 *         containing status code, headers, content type, and length.
 */
const BUrlResult&
BHttpRequest::Result() const
{
	return fResult;
}


/**
 * @brief Disconnect the socket and stop the request thread.
 *
 * @return B_OK on success, or an error code if the thread join fails.
 */
status_t
BHttpRequest::Stop()
{

	if (fSocket != NULL) {
		fSocket->Disconnect();
			// Unlock any pending connect, read or write operation.
	}
	return BNetworkRequest::Stop();
}


/**
 * @brief Reset all optional request parameters to their default values.
 *
 * Deletes any previously set post fields and custom headers, then
 * restores all option fields to their initial defaults.
 */
void
BHttpRequest::_ResetOptions()
{
	delete fOptPostFields;
	delete fOptHeaders;

	fOptFollowLocation = true;
	fOptMaxRedirs = 8;
	fOptReferer = "";
	fOptUserAgent = "Services Kit (Haiku)";
	fOptUsername = "";
	fOptPassword = "";
	fOptAuthMethods = B_HTTP_AUTHENTICATION_BASIC | B_HTTP_AUTHENTICATION_DIGEST
		| B_HTTP_AUTHENTICATION_IE_DIGEST;
	fOptHeaders = NULL;
	fOptPostFields = NULL;
	fOptSetCookies = true;
	fOptDiscardData = false;
	fOptDisableListener = false;
	fOptAutoReferer = true;
}


/**
 * @brief Run the HTTP protocol loop, handling redirects and authentication.
 *
 * Resolves the host name, calls _MakeRequest() in a loop to send the request
 * and receive the response, then handles 3xx redirects (updating the URL and
 * possibly switching between HTTP and HTTPS) and 401 authentication challenges
 * (setting credentials and retrying) until a terminal response is received
 * or the redirect limit is exhausted.
 *
 * @return B_OK on success, B_RESOURCE_NOT_FOUND for 404, or a network/IO
 *         error code on failure.
 */
status_t
BHttpRequest::_ProtocolLoop()
{
	// Initialize the request redirection loop
	int8 maxRedirs = fOptMaxRedirs;
	bool newRequest;

	do {
		newRequest = false;

		// Result reset
		fHeaders.Clear();
		_ResultHeaders().Clear();

		BString host = fUrl.Host();
		int port = fSSL ? 443 : 80;

		if (fUrl.HasPort())
			port = fUrl.Port();

		if (fContext->UseProxy()) {
			host = fContext->GetProxyHost();
			port = fContext->GetProxyPort();
		}

		status_t result = fInputBuffer.InitCheck();
		if (result != B_OK)
			return result;

		if (!_ResolveHostName(host, port)) {
			_EmitDebug(B_URL_PROTOCOL_DEBUG_ERROR,
				"Unable to resolve hostname (%s), aborting.",
					fUrl.Host().String());
			return B_SERVER_NOT_FOUND;
		}

		status_t requestStatus = _MakeRequest();
		if (requestStatus != B_OK)
			return requestStatus;

		// Prepare the referer for the next request if needed
		if (fOptAutoReferer)
			fOptReferer = fUrl.UrlString();

		switch (StatusCodeClass(fResult.StatusCode())) {
			case B_HTTP_STATUS_CLASS_INFORMATIONAL:
				// Header 100:continue should have been
				// handled in the _MakeRequest read loop
				break;

			case B_HTTP_STATUS_CLASS_SUCCESS:
				break;

			case B_HTTP_STATUS_CLASS_REDIRECTION:
			{
				// Redirection has been explicitly disabled
				if (!fOptFollowLocation)
					break;

				int code = fResult.StatusCode();
				if (code == B_HTTP_STATUS_MOVED_PERMANENTLY
					|| code == B_HTTP_STATUS_FOUND
					|| code == B_HTTP_STATUS_SEE_OTHER
					|| code == B_HTTP_STATUS_TEMPORARY_REDIRECT) {
					BString locationUrl = fHeaders["Location"];

					fUrl = BUrl(fUrl, locationUrl);

					// 302 and 303 redirections also convert POST requests to GET
					// (and remove the posted form data)
					if ((code == B_HTTP_STATUS_FOUND
						|| code == B_HTTP_STATUS_SEE_OTHER)
						&& fRequestMethod == B_HTTP_POST) {
						SetMethod(B_HTTP_GET);
						delete fOptPostFields;
						fOptPostFields = NULL;
						delete fOptInputData;
						fOptInputData = NULL;
						fOptInputDataSize = 0;
					}

					if (--maxRedirs > 0) {
						newRequest = true;

						// Redirections may need a switch from http to https.
						if (fUrl.Protocol() == "https")
							fSSL = true;
						else if (fUrl.Protocol() == "http")
							fSSL = false;

						_EmitDebug(B_URL_PROTOCOL_DEBUG_TEXT,
							"Following: %s\n",
							fUrl.UrlString().String());
					}
				}
				break;
			}

			case B_HTTP_STATUS_CLASS_CLIENT_ERROR:
				if (fResult.StatusCode() == B_HTTP_STATUS_UNAUTHORIZED) {
					BHttpAuthentication* authentication
						= &fContext->GetAuthentication(fUrl);
					status_t status = B_OK;

					if (authentication->Method() == B_HTTP_AUTHENTICATION_NONE) {
						// There is no authentication context for this
						// url yet, so let's create one.
						BHttpAuthentication newAuth;
						newAuth.Initialize(fHeaders["WWW-Authenticate"]);
						fContext->AddAuthentication(fUrl, newAuth);

						// Get the copy of the authentication we just added.
						// That copy is owned by the BUrlContext and won't be
						// deleted (unlike the temporary object above)
						authentication = &fContext->GetAuthentication(fUrl);
					}

					newRequest = false;
					if (fOptUsername.Length() > 0 && status == B_OK) {
						// If we received an username and password, add them
						// to the request. This will either change the
						// credentials for an existing request, or set them
						// for a new one we created just above.
						//
						// If this request handles HTTP redirections, it will
						// also automatically retry connecting and send the
						// login information.
						authentication->SetUserName(fOptUsername);
						authentication->SetPassword(fOptPassword);
						newRequest = true;
					}
				}
				break;

			case B_HTTP_STATUS_CLASS_SERVER_ERROR:
				break;

			default:
			case B_HTTP_STATUS_CLASS_INVALID:
				break;
		}
	} while (newRequest);

	_EmitDebug(B_URL_PROTOCOL_DEBUG_TEXT,
		"%" B_PRId32 " headers and %" B_PRIuSIZE " bytes of data remaining",
		fHeaders.CountHeaders(), fInputBuffer.Size());

	if (fResult.StatusCode() == 404)
		return B_RESOURCE_NOT_FOUND;

	return B_OK;
}


/**
 * @brief Open a socket, send the request, and receive the full response.
 *
 * Creates a BSocket (plain or TLS, direct or via proxy), connects it,
 * sends the serialised request line and headers, transmits any POST body,
 * then runs a receive loop that handles chunked transfer encoding and
 * optional gzip/deflate decompression, writing decoded body data to fOutput.
 *
 * @return B_OK on success, or a socket/IO error code on failure.
 */
status_t
BHttpRequest::_MakeRequest()
{
	delete fSocket;

	if (fSSL) {
		if (fContext->UseProxy()) {
			BNetworkAddress proxy(fContext->GetProxyHost(), fContext->GetProxyPort());
			fSocket = new(std::nothrow) BPrivate::CheckedProxySecureSocket(proxy, this);
		} else
			fSocket = new(std::nothrow) BPrivate::CheckedSecureSocket(this);
	} else
		fSocket = new(std::nothrow) BSocket();

	if (fSocket == NULL)
		return B_NO_MEMORY;

	_EmitDebug(B_URL_PROTOCOL_DEBUG_TEXT, "Connection to %s on port %d.",
		fUrl.Authority().String(), fRemoteAddr.Port());
	status_t connectError = fSocket->Connect(fRemoteAddr);

	if (connectError != B_OK) {
		_EmitDebug(B_URL_PROTOCOL_DEBUG_ERROR, "Socket connection error %s",
			strerror(connectError));
		return connectError;
	}

	//! ProtocolHook:ConnectionOpened
	if (fListener != NULL)
		fListener->ConnectionOpened(this);

	_EmitDebug(B_URL_PROTOCOL_DEBUG_TEXT,
		"Connection opened, sending request.");

	BString requestHeaders;
	requestHeaders.Append(_SerializeRequest());
	requestHeaders.Append(_SerializeHeaders());
	requestHeaders.Append("\r\n");
	fSocket->Write(requestHeaders.String(), requestHeaders.Length());
	_EmitDebug(B_URL_PROTOCOL_DEBUG_TEXT, "Request sent.");

	_SendPostData();
	fRequestStatus = kRequestInitialState;


	// Receive loop
	bool disableListener = false;
	bool receiveEnd = false;
	bool parseEnd = false;
	bool readByChunks = false;
	bool decompress = false;
	status_t readError = B_OK;
	ssize_t bytesRead = 0;
	off_t bytesReceived = 0;
	off_t bytesTotal = 0;
	size_t previousBufferSize = 0;
	off_t bytesUnpacked = 0;
	char* inputTempBuffer = new(std::nothrow) char[kHttpBufferSize];
	ArrayDeleter<char> inputTempBufferDeleter(inputTempBuffer);
	ssize_t inputTempSize = kHttpBufferSize;
	ssize_t chunkSize = -1;
	DynamicBuffer decompressorStorage;
	BDataIO* decompressingStream;
	ObjectDeleter<BDataIO> decompressingStreamDeleter;

	while (!fQuit && !(receiveEnd && parseEnd)) {
		if ((!receiveEnd) && (fInputBuffer.Size() == previousBufferSize)) {
			BStackOrHeapArray<char, 4096> chunk(kHttpBufferSize);
			bytesRead = fSocket->Read(chunk, kHttpBufferSize);

			if (bytesRead < 0) {
				readError = bytesRead;
				break;
			} else if (bytesRead == 0) {
				// Check if we got the expected number of bytes.
				// Exceptions:
				// - If the content-length is not known (bytesTotal is 0), for
				//   example in the case of a chunked transfer, we can't know
				// - If the request method is "HEAD" which explicitly asks the
				//   server to not send any data (only the headers)
				if (bytesTotal > 0 && bytesReceived != bytesTotal) {
					readError = B_IO_ERROR;
					break;
				}
				receiveEnd = true;
			}

			fInputBuffer.AppendData(chunk, bytesRead);
		} else
			bytesRead = 0;

		previousBufferSize = fInputBuffer.Size();

		if (fRequestStatus < kRequestStatusReceived) {
			_ParseStatus();

			if (fOptFollowLocation
					&& IsRedirectionStatusCode(fResult.StatusCode()))
				disableListener = true;

			if (fOptStopOnError
					&& fResult.StatusCode() >= B_HTTP_STATUS_CLASS_CLIENT_ERROR)
			{
				fQuit = true;
				break;
			}

			//! ProtocolHook:ResponseStarted
			if (fRequestStatus >= kRequestStatusReceived && fListener != NULL
					&& !disableListener)
				fListener->ResponseStarted(this);
		}

		if (fRequestStatus < kRequestHeadersReceived) {
			_ParseHeaders();

			if (fRequestStatus >= kRequestHeadersReceived) {
				_ResultHeaders() = fHeaders;

				// Parse received cookies
				if (fContext != NULL) {
					for (int32 i = 0;  i < fHeaders.CountHeaders(); i++) {
						if (fHeaders.HeaderAt(i).NameIs("Set-Cookie")) {
							fContext->GetCookieJar().AddCookie(
								fHeaders.HeaderAt(i).Value(), fUrl);
						}
					}
				}

				//! ProtocolHook:HeadersReceived
				if (fListener != NULL && !disableListener)
					fListener->HeadersReceived(this);


				if (BString(fHeaders["Transfer-Encoding"]) == "chunked")
					readByChunks = true;

				BString contentEncoding(fHeaders["Content-Encoding"]);
				// We don't advertise "deflate" support (see above), but we
				// still try to decompress it, if a server ever sends a deflate
				// stream despite it not being in our Accept-Encoding list.
				if (contentEncoding == "gzip"
						|| contentEncoding == "deflate") {
					decompress = true;
					readError = BZlibCompressionAlgorithm()
						.CreateDecompressingOutputStream(&decompressorStorage,
							NULL, decompressingStream);
					if (readError != B_OK)
						break;

					decompressingStreamDeleter.SetTo(decompressingStream);
				}

				int32 index = fHeaders.HasHeader("Content-Length");
				if (index != B_ERROR)
					bytesTotal = atoll(fHeaders.HeaderAt(index).Value());
				else
					bytesTotal = -1;

				if (fRequestMethod == B_HTTP_HEAD
					|| fResult.StatusCode() == 204) {
					// In the case of a HEAD request or if the server replies
					// 204 ("no content"), we don't expect to receive anything
					// more, and the socket will be closed.
					receiveEnd = true;
				}
			}
		}

		if (fRequestStatus >= kRequestHeadersReceived) {
			// If Transfer-Encoding is chunked, we should read a complete
			// chunk in buffer before handling it
			if (readByChunks) {
				if (chunkSize >= 0) {
					if ((ssize_t)fInputBuffer.Size() >= chunkSize + 2) {
							// 2 more bytes to handle the closing CR+LF
						bytesRead = chunkSize;
						if (inputTempSize < chunkSize + 2) {
							inputTempSize = chunkSize + 2;
							inputTempBuffer
								= new(std::nothrow) char[inputTempSize];
							inputTempBufferDeleter.SetTo(inputTempBuffer);
						}

						if (inputTempBuffer == NULL) {
							readError = B_NO_MEMORY;
							break;
						}

						fInputBuffer.RemoveData(inputTempBuffer,
							chunkSize + 2);
						chunkSize = -1;
					} else {
						// Not enough data, try again later
						bytesRead = -1;
					}
				} else {
					BString chunkHeader;
					if (_GetLine(chunkHeader) == B_ERROR) {
						chunkSize = -1;
						bytesRead = -1;
					} else {
						// Format of a chunk header:
						// <chunk size in hex>[; optional data]
						int32 semiColonIndex = chunkHeader.FindFirst(';', 0);

						// Cut-off optional data if present
						if (semiColonIndex != -1) {
							chunkHeader.Remove(semiColonIndex,
								chunkHeader.Length() - semiColonIndex);
						}

						chunkSize = strtol(chunkHeader.String(), NULL, 16);
						if (chunkSize == 0)
							fRequestStatus = kRequestContentReceived;

						bytesRead = -1;
					}
				}

				// A chunk of 0 bytes indicates the end of the chunked transfer
				if (bytesRead == 0)
					receiveEnd = true;
			} else {
				bytesRead = fInputBuffer.Size();

				if (bytesRead > 0) {
					if (inputTempSize < bytesRead) {
						inputTempSize = bytesRead;
						inputTempBuffer = new(std::nothrow) char[bytesRead];
						inputTempBufferDeleter.SetTo(inputTempBuffer);
					}

					if (inputTempBuffer == NULL) {
						readError = B_NO_MEMORY;
						break;
					}
					fInputBuffer.RemoveData(inputTempBuffer, bytesRead);
				}
			}

			if (bytesRead >= 0) {
				bytesReceived += bytesRead;

				if (fOutput != NULL && !disableListener) {
					if (decompress) {
						readError = decompressingStream->WriteExactly(
							inputTempBuffer, bytesRead);
						if (readError != B_OK)
							break;

						ssize_t size = decompressorStorage.Size();
						BStackOrHeapArray<char, 4096> buffer(size);
						size = decompressorStorage.Read(buffer, size);
						if (size > 0) {
							size_t written = 0;
							readError = fOutput->WriteExactly(buffer,
								size, &written);
							if (fListener != NULL && written > 0)
								fListener->BytesWritten(this, written);
							if (readError != B_OK)
								break;
							bytesUnpacked += size;
						}
					} else if (bytesRead > 0) {
						size_t written = 0;
						readError = fOutput->WriteExactly(inputTempBuffer,
							bytesRead, &written);
						if (fListener != NULL && written > 0)
							fListener->BytesWritten(this, written);
						if (readError != B_OK)
							break;
					}
				}

				if (fListener != NULL && !disableListener)
					fListener->DownloadProgress(this, bytesReceived,
						std::max((off_t)0, bytesTotal));

				if (bytesTotal >= 0 && bytesReceived >= bytesTotal)
					receiveEnd = true;

				if (decompress && receiveEnd && !disableListener) {
					readError = decompressingStream->Flush();

					if (readError == B_BUFFER_OVERFLOW)
						readError = B_OK;

					if (readError != B_OK)
						break;

					ssize_t size = decompressorStorage.Size();
					BStackOrHeapArray<char, 4096> buffer(size);
					size = decompressorStorage.Read(buffer, size);
					if (fOutput != NULL && size > 0 && !disableListener) {
						size_t written = 0;
						readError = fOutput->WriteExactly(buffer, size,
							&written);
						if (fListener != NULL && written > 0)
							fListener->BytesWritten(this, written);
						if (readError != B_OK)
							break;
						bytesUnpacked += size;
					}
				}
			}
		}

		parseEnd = (fInputBuffer.Size() == 0);
	}

	fSocket->Disconnect();

	if (readError != B_OK)
		return readError;

	return fQuit ? B_INTERRUPTED : B_OK;
}


/**
 * @brief Parse the HTTP status line from the input buffer.
 *
 * Reads one line from fInputBuffer, validates that it begins with an HTTP
 * version prefix and contains a three-digit status code, then stores the
 * code and reason phrase in the result object.
 */
void
BHttpRequest::_ParseStatus()
{
	// Status line should be formatted like: HTTP/M.m SSS ...
	// With:   M = Major version of the protocol
	//         m = Minor version of the protocol
	//       SSS = three-digit status code of the response
	//       ... = additional text info
	BString statusLine;
	if (_GetLine(statusLine) == B_ERROR)
		return;

	if (statusLine.CountChars() < 12)
		return;

	fRequestStatus = kRequestStatusReceived;

	BString statusCodeStr;
	BString statusText;
	statusLine.CopyInto(statusCodeStr, 9, 3);
	_SetResultStatusCode(atoi(statusCodeStr.String()));

	statusLine.CopyInto(_ResultStatusText(), 13, statusLine.Length() - 13);

	_EmitDebug(B_URL_PROTOCOL_DEBUG_TEXT, "Status line received: Code %d (%s)",
		atoi(statusCodeStr.String()), _ResultStatusText().String());
}


/**
 * @brief Read header lines from the input buffer until the blank separator.
 *
 * Calls _GetLine() in a loop, adding each line to fHeaders. An empty line
 * signals the end of the header section and advances fRequestStatus to
 * kRequestHeadersReceived.
 */
void
BHttpRequest::_ParseHeaders()
{
	BString currentHeader;
	while (_GetLine(currentHeader) != B_ERROR) {
		// An empty line means the end of the header section
		if (currentHeader.Length() == 0) {
			fRequestStatus = kRequestHeadersReceived;
			return;
		}

		_EmitDebug(B_URL_PROTOCOL_DEBUG_HEADER_IN, "%s",
			currentHeader.String());
		fHeaders.AddHeader(currentHeader.String());
	}
}


/**
 * @brief Build the HTTP request line (method, path, version).
 *
 * Constructs the first line of the HTTP request, including an absolute URI
 * when a proxy is in use, and appends a query string if present in the URL.
 *
 * @return The formatted request line as a BString (without the trailing CRLF).
 */
BString
BHttpRequest::_SerializeRequest()
{
	BString request(fRequestMethod);
	request << ' ';

	if (fContext->UseProxy()) {
		// When there is a proxy, the request must include the host and port so
		// the proxy knows where to send the request.
		request << Url().Protocol() << "://" << Url().Host();
		if (Url().HasPort())
			request << ':' << Url().Port();
	}

	if (Url().HasPath() && Url().Path().Length() > 0)
		request << Url().Path();
	else
		request << '/';

	if (Url().HasRequest())
		request << '?' << Url().Request();

	switch (fHttpVersion) {
		case B_HTTP_11:
			request << " HTTP/1.1\r\n";
			break;

		default:
		case B_HTTP_10:
			request << " HTTP/1.0\r\n";
			break;
	}

	_EmitDebug(B_URL_PROTOCOL_DEBUG_HEADER_OUT, "%s", request.String());

	return request;
}


/**
 * @brief Build the full set of HTTP request headers as a CRLF-terminated string.
 *
 * Assembles standard headers (Host, Accept, Connection), optional headers
 * (User-Agent, Referer, Range), authentication, POST content headers, user-
 * specified custom headers, and cookie headers. All are returned as a single
 * string ready to be written to the socket.
 *
 * @return A BString containing all headers, each terminated by "\r\n".
 */
BString
BHttpRequest::_SerializeHeaders()
{
	BHttpHeaders outputHeaders;

	// HTTP 1.1 additional headers
	if (fHttpVersion == B_HTTP_11) {
		BString host = Url().Host();
		if (Url().HasPort() && !_IsDefaultPort())
			host << ':' << Url().Port();

		outputHeaders.AddHeader("Host", host);

		outputHeaders.AddHeader("Accept", "*/*");
		outputHeaders.AddHeader("Accept-Encoding", "gzip");
			// Allows the server to compress data using the "gzip" format.
			// "deflate" is not supported, because there are two interpretations
			// of what it means (the RFC and Microsoft products), and we don't
			// want to handle this. Very few websites support only deflate,
			// and most of them will send gzip, or at worst, uncompressed data.

		outputHeaders.AddHeader("Connection", "close");
			// Let the remote server close the connection after response since
			// we don't handle multiple request on a single connection
	}

	// Classic HTTP headers
	if (fOptUserAgent.CountChars() > 0)
		outputHeaders.AddHeader("User-Agent", fOptUserAgent.String());

	if (fOptReferer.CountChars() > 0)
		outputHeaders.AddHeader("Referer", fOptReferer.String());

	// Optional range requests headers
	if (fOptRangeStart != -1 || fOptRangeEnd != -1) {
		if (fOptRangeStart == -1)
			fOptRangeStart = 0;
		BString range;
		if (fOptRangeEnd != -1) {
			range.SetToFormat("bytes=%" B_PRIdOFF "-%" B_PRIdOFF,
				fOptRangeStart, fOptRangeEnd);
		} else {
			range.SetToFormat("bytes=%" B_PRIdOFF "-", fOptRangeStart);
		}
		outputHeaders.AddHeader("Range", range.String());
	}

	// Authentication
	if (fContext != NULL) {
		BHttpAuthentication& authentication = fContext->GetAuthentication(fUrl);
		if (authentication.Method() != B_HTTP_AUTHENTICATION_NONE) {
			if (fOptUsername.Length() > 0) {
				authentication.SetUserName(fOptUsername);
				authentication.SetPassword(fOptPassword);
			}

			BString request(fRequestMethod);
			outputHeaders.AddHeader("Authorization",
				authentication.Authorization(fUrl, request));
		}
	}

	// Required headers for POST data
	if (fOptPostFields != NULL && fRequestMethod == B_HTTP_POST) {
		BString contentType;

		switch (fOptPostFields->GetFormType()) {
			case B_HTTP_FORM_MULTIPART:
				contentType << "multipart/form-data; boundary="
					<< fOptPostFields->GetMultipartBoundary() << "";
				break;

			case B_HTTP_FORM_URL_ENCODED:
				contentType << "application/x-www-form-urlencoded";
				break;
		}

		outputHeaders.AddHeader("Content-Type", contentType);
		outputHeaders.AddHeader("Content-Length",
			fOptPostFields->ContentLength());
	} else if (fOptInputData != NULL
			&& (fRequestMethod == B_HTTP_POST
			|| fRequestMethod == B_HTTP_PUT)) {
		if (fOptInputDataSize >= 0)
			outputHeaders.AddHeader("Content-Length", fOptInputDataSize);
		else
			outputHeaders.AddHeader("Transfer-Encoding", "chunked");
	}

	// Optional headers specified by the user
	if (fOptHeaders != NULL) {
		for (int32 headerIndex = 0; headerIndex < fOptHeaders->CountHeaders();
				headerIndex++) {
			BHttpHeader& optHeader = (*fOptHeaders)[headerIndex];
			int32 replaceIndex = outputHeaders.HasHeader(optHeader.Name());

			// Add or replace the current option header to the
			// output header list
			if (replaceIndex == -1)
				outputHeaders.AddHeader(optHeader.Name(), optHeader.Value());
			else
				outputHeaders[replaceIndex].SetValue(optHeader.Value());
		}
	}

	// Context cookies
	if (fOptSetCookies && fContext != NULL) {
		BString cookieString;

		BNetworkCookieJar::UrlIterator iterator
			= fContext->GetCookieJar().GetUrlIterator(fUrl);
		const BNetworkCookie* cookie = iterator.Next();
		if (cookie != NULL) {
			while (true) {
				cookieString << cookie->RawCookie(false);
				cookie = iterator.Next();
				if (cookie == NULL)
					break;
				cookieString << "; ";
			}

			outputHeaders.AddHeader("Cookie", cookieString);
		}
	}

	// Write output headers to output stream
	BString headerData;

	for (int32 headerIndex = 0; headerIndex < outputHeaders.CountHeaders();
			headerIndex++) {
		const char* header = outputHeaders.HeaderAt(headerIndex).Header();

		headerData << header;
		headerData << "\r\n";

		_EmitDebug(B_URL_PROTOCOL_DEBUG_HEADER_OUT, "%s", header);
	}

	return headerData;
}


/**
 * @brief Send the request body (POST fields or raw input data) over the socket.
 *
 * For multipart form data, iterates the BHttpForm and streams each part.
 * For BDataIO input data with unknown size, uses chunked transfer encoding.
 * Does nothing for GET or HEAD requests.
 */
void
BHttpRequest::_SendPostData()
{
	if (fRequestMethod == B_HTTP_POST && fOptPostFields != NULL) {
		if (fOptPostFields->GetFormType() != B_HTTP_FORM_MULTIPART) {
			BString outputBuffer = fOptPostFields->RawData();
			_EmitDebug(B_URL_PROTOCOL_DEBUG_TRANSFER_OUT,
				"%s", outputBuffer.String());
			fSocket->Write(outputBuffer.String(), outputBuffer.Length());
		} else {
			for (BHttpForm::Iterator it = fOptPostFields->GetIterator();
				const BHttpFormData* currentField = it.Next();
				) {
				_EmitDebug(B_URL_PROTOCOL_DEBUG_TRANSFER_OUT,
					it.MultipartHeader().String());
				fSocket->Write(it.MultipartHeader().String(),
					it.MultipartHeader().Length());

				switch (currentField->Type()) {
					default:
					case B_HTTPFORM_UNKNOWN:
						ASSERT(0);
						break;

					case B_HTTPFORM_STRING:
						fSocket->Write(currentField->String().String(),
							currentField->String().Length());
						break;

					case B_HTTPFORM_FILE:
						{
							BFile upFile(currentField->File().Path(),
								B_READ_ONLY);
							char readBuffer[kHttpBufferSize];
							ssize_t readSize;
							off_t totalSize;

							if (upFile.GetSize(&totalSize) != B_OK)
								ASSERT(0);

							readSize = upFile.Read(readBuffer,
								sizeof(readBuffer));
							while (readSize > 0) {
								fSocket->Write(readBuffer, readSize);
								readSize = upFile.Read(readBuffer,
									sizeof(readBuffer));
								fListener->UploadProgress(this, readSize,
									std::max((off_t)0, totalSize));
							}

							break;
						}
					case B_HTTPFORM_BUFFER:
						fSocket->Write(currentField->Buffer(),
							currentField->BufferSize());
						break;
				}

				fSocket->Write("\r\n", 2);
			}

			BString footer = fOptPostFields->GetMultipartFooter();
			fSocket->Write(footer.String(), footer.Length());
		}
	} else if ((fRequestMethod == B_HTTP_POST || fRequestMethod == B_HTTP_PUT)
		&& fOptInputData != NULL) {

		// If the input data is seekable, we rewind it for each new request.
		BPositionIO* seekableData
			= dynamic_cast<BPositionIO*>(fOptInputData);
		if (seekableData)
			seekableData->Seek(0, SEEK_SET);

		for (;;) {
			char outputTempBuffer[kHttpBufferSize];
			ssize_t read = fOptInputData->Read(outputTempBuffer,
				sizeof(outputTempBuffer));

			if (read <= 0)
				break;

			if (fOptInputDataSize < 0) {
				// Input data size unknown, so we have to use chunked transfer
				char hexSize[18];
				// The string does not need to be NULL terminated.
				size_t hexLength = snprintf(hexSize, sizeof(hexSize), "%lx\r\n",
					read);

				fSocket->Write(hexSize, hexLength);
				fSocket->Write(outputTempBuffer, read);
				fSocket->Write("\r\n", 2);
			} else {
				fSocket->Write(outputTempBuffer, read);
			}
		}

		if (fOptInputDataSize < 0) {
			// Chunked transfer terminating sequence
			fSocket->Write("0\r\n\r\n", 5);
		}
	}

}


/**
 * @brief Return a mutable reference to the result's header collection.
 *
 * @return A reference to fResult.fHeaders for use by internal parsing code.
 */
BHttpHeaders&
BHttpRequest::_ResultHeaders()
{
	return fResult.fHeaders;
}


/**
 * @brief Store an HTTP status code into the result object.
 *
 * @param statusCode  The three-digit HTTP status code to store.
 */
void
BHttpRequest::_SetResultStatusCode(int32 statusCode)
{
	fResult.fStatusCode = statusCode;
}


/**
 * @brief Return a mutable reference to the result's status text string.
 *
 * @return A reference to fResult.fStatusString for use by _ParseStatus().
 */
BString&
BHttpRequest::_ResultStatusText()
{
	return fResult.fStatusString;
}


/**
 * @brief Handle a TLS certificate verification failure.
 *
 * Checks the context's certificate exception list first; if no exception is
 * found, asks the listener for permission to continue. If the listener
 * approves, a temporary exception is added for this session.
 *
 * @param certificate  The certificate that failed verification.
 * @param message      Human-readable description of the failure.
 * @return true if the connection should proceed, false to abort.
 */
bool
BHttpRequest::_CertificateVerificationFailed(BCertificate& certificate,
	const char* message)
{
	if (fContext->HasCertificateException(certificate))
		return true;

	if (fListener != NULL
		&& fListener->CertificateVerificationFailed(this, certificate, message)) {
		// User asked us to continue anyway, let's add a temporary exception for this certificate
		fContext->AddCertificateException(certificate);
		return true;
	}

	return false;
}


/**
 * @brief Return whether the URL's port is the default port for this scheme.
 *
 * Used to suppress the port number from the Host header when it equals the
 * scheme's well-known default (80 for HTTP, 443 for HTTPS).
 *
 * @return true if the port is the default for the current scheme, false otherwise.
 */
bool
BHttpRequest::_IsDefaultPort()
{
	if (fSSL && Url().Port() == 443)
		return true;
	if (!fSSL && Url().Port() == 80)
		return true;
	return false;
}
