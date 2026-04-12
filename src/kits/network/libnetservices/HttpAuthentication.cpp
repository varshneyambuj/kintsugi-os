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
 *   Copyright 2010-2023 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christophe Huriaux, c.huriaux@gmail.com
 */


/**
 * @file HttpAuthentication.cpp
 * @brief Implementation of BHttpAuthentication for HTTP auth schemes.
 *
 * Supports Basic, Digest (MD5 and MD5-sess), and Bearer token authentication
 * as defined by RFC 7617, RFC 7616, and RFC 6750 respectively. Parses the
 * WWW-Authenticate challenge header and produces the corresponding
 * Authorization header value for subsequent requests.
 *
 * @see BHttpRequest, BUrlContext
 */


#include <HttpAuthentication.h>

#include <stdlib.h>
#include <stdio.h>

#include <AutoLocker.h>

using namespace BPrivate::Network;


#if DEBUG > 0
#define PRINT(x) printf x
#else
#define PRINT(x)
#endif

#ifdef OPENSSL_ENABLED
extern "C" {
#include <openssl/md5.h>
};
#else
#include "md5.h"
#endif

#ifndef MD5_DIGEST_LENGTH
#define MD5_DIGEST_LENGTH 16
#endif

static const char* kBase64Symbols
	= "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";


/**
 * @brief Default constructor — sets the method to B_HTTP_AUTHENTICATION_NONE.
 */
BHttpAuthentication::BHttpAuthentication()
	:
	fAuthenticationMethod(B_HTTP_AUTHENTICATION_NONE)
{
}


/**
 * @brief Construct with explicit username and password credentials.
 *
 * The authentication method is still B_HTTP_AUTHENTICATION_NONE until
 * Initialize() is called to parse a WWW-Authenticate challenge.
 *
 * @param username  The username to store.
 * @param password  The password to store.
 */
BHttpAuthentication::BHttpAuthentication(const BString& username, const BString& password)
	:
	fAuthenticationMethod(B_HTTP_AUTHENTICATION_NONE),
	fUserName(username),
	fPassword(password)
{
}


/**
 * @brief Copy constructor — deep-copies all authentication state.
 *
 * @param other  The source BHttpAuthentication object to copy from.
 */
BHttpAuthentication::BHttpAuthentication(const BHttpAuthentication& other)
	:
	fAuthenticationMethod(other.fAuthenticationMethod),
	fUserName(other.fUserName),
	fPassword(other.fPassword),
	fToken(other.fToken),
	fRealm(other.fRealm),
	fDigestNonce(other.fDigestNonce),
	fDigestCnonce(other.fDigestCnonce),
	fDigestNc(other.fDigestNc),
	fDigestOpaque(other.fDigestOpaque),
	fDigestStale(other.fDigestStale),
	fDigestAlgorithm(other.fDigestAlgorithm),
	fDigestQop(other.fDigestQop),
	fAuthorizationString(other.fAuthorizationString)
{
}


/**
 * @brief Assignment operator — replaces all fields with those from \a other.
 *
 * @param other  The source BHttpAuthentication object to copy from.
 * @return A reference to this object.
 */
BHttpAuthentication& BHttpAuthentication::operator=(
	const BHttpAuthentication& other)
{
	fAuthenticationMethod = other.fAuthenticationMethod;
	fUserName = other.fUserName;
	fPassword = other.fPassword;
	fToken = other.fToken;
	fRealm = other.fRealm;
	fDigestNonce = other.fDigestNonce;
	fDigestCnonce = other.fDigestCnonce;
	fDigestNc = other.fDigestNc;
	fDigestOpaque = other.fDigestOpaque;
	fDigestStale = other.fDigestStale;
	fDigestAlgorithm = other.fDigestAlgorithm;
	fDigestQop = other.fDigestQop;
	fAuthorizationString = other.fAuthorizationString;
	return *this;
}


// #pragma mark Field modification


/**
 * @brief Set the username credential under the internal lock.
 *
 * @param username  The new username string to store.
 */
void
BHttpAuthentication::SetUserName(const BString& username)
{
	fLock.Lock();
	fUserName = username;
	fLock.Unlock();
}


/**
 * @brief Set the password credential under the internal lock.
 *
 * @param password  The new password string to store.
 */
void
BHttpAuthentication::SetPassword(const BString& password)
{
	fLock.Lock();
	fPassword = password;
	fLock.Unlock();
}


/**
 * @brief Set the Bearer token under the internal lock.
 *
 * @param token  The Bearer token string to store.
 */
void
BHttpAuthentication::SetToken(const BString& token)
{
	fLock.Lock();
	fToken = token;
	fLock.Unlock();
}


/**
 * @brief Explicitly override the active authentication method.
 *
 * @param method  One of the BHttpAuthenticationMethod constants.
 */
void
BHttpAuthentication::SetMethod(BHttpAuthenticationMethod method)
{
	fLock.Lock();
	fAuthenticationMethod = method;
	fLock.Unlock();
}


/**
 * @brief Parse a WWW-Authenticate header value and initialise the object.
 *
 * Detects whether the challenge requests Basic, Digest, or Bearer
 * authentication and extracts the relevant parameters (realm, nonce, opaque,
 * algorithm, qop) from the comma-separated attribute list.
 *
 * @param wwwAuthenticate  The full value of the WWW-Authenticate header.
 * @return B_OK if a supported and complete challenge was parsed, B_BAD_VALUE
 *         if the header is empty, or B_ERROR for unsupported or incomplete
 *         challenges.
 */
status_t
BHttpAuthentication::Initialize(const BString& wwwAuthenticate)
{
	BPrivate::AutoLocker<BLocker> lock(fLock);

	fAuthenticationMethod = B_HTTP_AUTHENTICATION_NONE;
	fDigestQop = B_HTTP_QOP_NONE;

	if (wwwAuthenticate.Length() == 0)
		return B_BAD_VALUE;

	BString authRequired;
	BString additionalData;
	int32 firstSpace = wwwAuthenticate.FindFirst(' ');

	if (firstSpace == -1)
		wwwAuthenticate.CopyInto(authRequired, 0, wwwAuthenticate.Length());
	else {
		wwwAuthenticate.CopyInto(authRequired, 0, firstSpace);
		wwwAuthenticate.CopyInto(additionalData, firstSpace + 1,
			wwwAuthenticate.Length() - (firstSpace + 1));
	}

	authRequired.ToLower();
	if (authRequired == "basic")
		fAuthenticationMethod = B_HTTP_AUTHENTICATION_BASIC;
	else if (authRequired == "digest") {
		fAuthenticationMethod = B_HTTP_AUTHENTICATION_DIGEST;
		fDigestAlgorithm = B_HTTP_AUTHENTICATION_ALGORITHM_MD5;
	} else if (authRequired == "bearer")
		fAuthenticationMethod = B_HTTP_AUTHENTICATION_BEARER;
	else
		return B_ERROR;


	while (additionalData.Length()) {
		int32 firstComma = additionalData.FindFirst(',');
		if (firstComma == -1)
			firstComma = additionalData.Length();

		BString value;
		additionalData.MoveInto(value, 0, firstComma);
		additionalData.Remove(0, 1);
		additionalData.Trim();

		int32 equal = value.FindFirst('=');
		if (equal <= 0)
			continue;

		BString name;
		value.MoveInto(name, 0, equal);
		value.Remove(0, 1);
		name.ToLower();

		if (value.Length() > 0 && value[0] == '"') {
			value.Remove(0, 1);
			value.Remove(value.Length() - 1, 1);
		}

		PRINT(("HttpAuth: name=%s, value=%s\n", name.String(),
			value.String()));

		if (name == "realm")
			fRealm = value;
		else if (name == "nonce")
			fDigestNonce = value;
		else if (name == "opaque")
			fDigestOpaque = value;
		else if (name == "stale") {
			value.ToLower();
			fDigestStale = (value == "true");
		} else if (name == "algorithm") {
			value.ToLower();

			if (value == "md5")
				fDigestAlgorithm = B_HTTP_AUTHENTICATION_ALGORITHM_MD5;
			else if (value == "md5-sess")
				fDigestAlgorithm = B_HTTP_AUTHENTICATION_ALGORITHM_MD5_SESS;
			else
				fDigestAlgorithm = B_HTTP_AUTHENTICATION_ALGORITHM_NONE;
		} else if (name == "qop")
			fDigestQop = B_HTTP_QOP_AUTH;
	}

	if (fAuthenticationMethod == B_HTTP_AUTHENTICATION_BASIC)
		return B_OK;
	else if (fAuthenticationMethod == B_HTTP_AUTHENTICATION_BEARER)
		return B_OK;
	else if (fAuthenticationMethod == B_HTTP_AUTHENTICATION_DIGEST
			&& fDigestNonce.Length() > 0
			&& fDigestAlgorithm != B_HTTP_AUTHENTICATION_ALGORITHM_NONE) {
		return B_OK;
	} else
		return B_ERROR;
}


// #pragma mark Field access


/**
 * @brief Return the stored username under the internal lock.
 *
 * @return A const reference to the username BString.
 */
const BString&
BHttpAuthentication::UserName() const
{
	BPrivate::AutoLocker<BLocker> lock(fLock);
	return fUserName;
}


/**
 * @brief Return the stored password under the internal lock.
 *
 * @return A const reference to the password BString.
 */
const BString&
BHttpAuthentication::Password() const
{
	BPrivate::AutoLocker<BLocker> lock(fLock);
	return fPassword;
}


/**
 * @brief Return the active authentication method under the internal lock.
 *
 * @return The current BHttpAuthenticationMethod value.
 */
BHttpAuthenticationMethod
BHttpAuthentication::Method() const
{
	BPrivate::AutoLocker<BLocker> lock(fLock);
	return fAuthenticationMethod;
}


/**
 * @brief Build the Authorization header value for the given request.
 *
 * Selects the appropriate encoding scheme (Basic base64, Bearer token, or
 * Digest response) based on the stored authentication method and returns the
 * complete header value string ready to be added to an outgoing request.
 *
 * @param url     The URL being requested, used for the Digest URI field.
 * @param method  The HTTP method string (e.g. "GET"), used for Digest A2 hash.
 * @return The full Authorization header value string.
 */
BString
BHttpAuthentication::Authorization(const BUrl& url, const BString& method) const
{
	BPrivate::AutoLocker<BLocker> lock(fLock);
	BString authorizationString;

	switch (fAuthenticationMethod) {
		case B_HTTP_AUTHENTICATION_NONE:
			break;

		case B_HTTP_AUTHENTICATION_BASIC:
		{
			BString basicEncode;
			basicEncode << fUserName << ':' << fPassword;
			authorizationString << "Basic " << Base64Encode(basicEncode);
			break;
		}

		case B_HTTP_AUTHENTICATION_BEARER:
			authorizationString << "Bearer " << fToken;
			break;

		case B_HTTP_AUTHENTICATION_DIGEST:
		case B_HTTP_AUTHENTICATION_IE_DIGEST:
			authorizationString << "Digest " << "username=\"" << fUserName
				<< "\", realm=\"" << fRealm << "\", nonce=\"" << fDigestNonce
				<< "\", algorithm=";

			if (fDigestAlgorithm == B_HTTP_AUTHENTICATION_ALGORITHM_MD5)
				authorizationString << "MD5";
			else
				authorizationString << "MD5-sess";

			if (fDigestOpaque.Length() > 0)
				authorizationString << ", opaque=\"" << fDigestOpaque << "\"";

			if (fDigestQop != B_HTTP_QOP_NONE) {
				if (fDigestCnonce.Length() == 0) {
					fDigestCnonce = _H(fDigestOpaque);
					//fDigestCnonce = "03c6790a055cbbac";
					fDigestNc = 0;
				}

				authorizationString << ", uri=\"" << url.Path() << "\"";
				authorizationString << ", qop=auth, cnonce=\"" << fDigestCnonce
					<< "\"";

				char strNc[9];
				snprintf(strNc, 9, "%08x", ++fDigestNc);
				authorizationString << ", nc=" << strNc;

			}

			authorizationString << ", response=\""
				<< _DigestResponse(url.Path(), method) << "\"";
			break;
	}

	return authorizationString;
}


// #pragma mark Base64 encoding


/**
 * @brief Encode a string using the URL-safe Base64 alphabet.
 *
 * Processes the input three bytes at a time, emitting four Base64 characters
 * per group and padding the output with '=' characters when the input length
 * is not a multiple of three.
 *
 * @param string  The binary or text string to encode.
 * @return The Base64-encoded representation of \a string.
 */
/*static*/ BString
BHttpAuthentication::Base64Encode(const BString& string)
{
	BString result;
	BString tmpString = string;

	while (tmpString.Length()) {
		char in[3] = { 0, 0, 0 };
		char out[4] = { 0, 0, 0, 0 };
		int8 remaining = tmpString.Length();

		tmpString.MoveInto(in, 0, 3);

		out[0] = (in[0] & 0xFC) >> 2;
		out[1] = ((in[0] & 0x03) << 4) | ((in[1] & 0xF0) >> 4);
		out[2] = ((in[1] & 0x0F) << 2) | ((in[2] & 0xC0) >> 6);
		out[3] = in[2] & 0x3F;

		for (int i = 0; i < 4; i++)
			out[i] = kBase64Symbols[(int)out[i]];

		//  Add padding if the input length is not a multiple
		// of 3
		switch (remaining) {
			case 1:
				out[2] = '=';
				// Fall through
			case 2:
				out[3] = '=';
				break;
		}

		result.Append(out, 4);
	}

	return result;
}


/**
 * @brief Decode a URL-safe Base64-encoded string back to its original bytes.
 *
 * Validates that the input length is a multiple of four, then processes each
 * group of four Base64 characters into three output bytes, stripping any '='
 * padding characters.
 *
 * @param string  The Base64-encoded input string.
 * @return The decoded binary string, or an empty string on invalid input.
 */
/*static*/ BString
BHttpAuthentication::Base64Decode(const BString& string)
{
	BString result;

	// Check for invalid input
	if (string.Length() % 4 != 0)
		return result;

	BString base64Reverse(kBase64Symbols);

	BString tmpString(string);
	while (tmpString.Length()) {
		char in[4] = { 0, 0, 0, 0 };
		char out[3] = { 0, 0, 0 };

		tmpString.MoveInto(in, 0, 4);

		for (int i = 0; i < 4; i++) {
			if (in[i] == '=')
				in[i] = 0;
			else
				in[i] = base64Reverse.FindFirst(in[i], 0);
		}

		out[0] = (in[0] << 2) | ((in[1] & 0x30) >> 4);
		out[1] = ((in[1] & 0x0F) << 4) | ((in[2] & 0x3C) >> 2);
		out[2] = ((in[2] & 0x03) << 6) | in[3];

		result.Append(out, 3);
	}

	return result;
}


/**
 * @brief Compute the Digest authentication response string.
 *
 * Implements RFC 7616 by computing H(A1), H(A2), and the final KD-based
 * response hash using MD5 or MD5-sess as indicated by fDigestAlgorithm.
 * If a quality-of-protection (qop) directive is active, the client nonce
 * count and client nonce are incorporated.
 *
 * @param uri     The request URI path used in the A2 computation.
 * @param method  The HTTP method string used in the A2 computation.
 * @return The hex-encoded MD5 response string.
 */
BString
BHttpAuthentication::_DigestResponse(const BString& uri, const BString& method) const
{
	PRINT(("HttpAuth: Computing digest response: \n"));
	PRINT(("HttpAuth: > username  = %s\n", fUserName.String()));
	PRINT(("HttpAuth: > password  = %s\n", fPassword.String()));
	PRINT(("HttpAuth: > token     = %s\n", fToken.String()));
	PRINT(("HttpAuth: > realm     = %s\n", fRealm.String()));
	PRINT(("HttpAuth: > nonce     = %s\n", fDigestNonce.String()));
	PRINT(("HttpAuth: > cnonce    = %s\n", fDigestCnonce.String()));
	PRINT(("HttpAuth: > nc        = %08x\n", fDigestNc));
	PRINT(("HttpAuth: > uri       = %s\n", uri.String()));
	PRINT(("HttpAuth: > method    = %s\n", method.String()));
	PRINT(("HttpAuth: > algorithm = %d (MD5:%d, MD5-sess:%d)\n",
		fDigestAlgorithm, B_HTTP_AUTHENTICATION_ALGORITHM_MD5,
		B_HTTP_AUTHENTICATION_ALGORITHM_MD5_SESS));

	BString A1;
	A1 << fUserName << ':' << fRealm << ':' << fPassword;

	if (fDigestAlgorithm == B_HTTP_AUTHENTICATION_ALGORITHM_MD5_SESS) {
		A1 = _H(A1);
		A1 << ':' << fDigestNonce << ':' << fDigestCnonce;
	}


	BString A2;
	A2 << method << ':' << uri;

	PRINT(("HttpAuth: > A1        = %s\n", A1.String()));
	PRINT(("HttpAuth: > A2        = %s\n", A2.String()));
	PRINT(("HttpAuth: > H(A1)     = %s\n", _H(A1).String()));
	PRINT(("HttpAuth: > H(A2)     = %s\n", _H(A2).String()));

	char strNc[9];
	snprintf(strNc, 9, "%08x", fDigestNc);

	BString secretResp;
	secretResp << fDigestNonce << ':' << strNc << ':' << fDigestCnonce
		<< ":auth:" << _H(A2);

	PRINT(("HttpAuth: > R2        = %s\n", secretResp.String()));

	BString response = _KD(_H(A1), secretResp);
	PRINT(("HttpAuth: > response  = %s\n", response.String()));

	return response;
}


/**
 * @brief Compute the lowercase hex MD5 hash of a string (the H() function).
 *
 * @param value  The input string to hash.
 * @return The 32-character lowercase hex representation of the MD5 digest.
 */
BString
BHttpAuthentication::_H(const BString& value) const
{
	MD5_CTX context;
	uchar hashResult[MD5_DIGEST_LENGTH];
	MD5_Init(&context);
	MD5_Update(&context, (void *)(value.String()), value.Length());
	MD5_Final(hashResult, &context);

	BString result;
	// Preallocate the string
	char* resultChar = result.LockBuffer(MD5_DIGEST_LENGTH * 2);
	if (resultChar == NULL)
		return BString();

	for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
		char c = ((hashResult[i] & 0xF0) >> 4);
		c += (c > 9) ? 'a' - 10 : '0';
		resultChar[0] = c;
		resultChar++;

		c = hashResult[i] & 0x0F;
		c += (c > 9) ? 'a' - 10 : '0';
		resultChar[0] = c;
		resultChar++;
	}
	result.UnlockBuffer(MD5_DIGEST_LENGTH * 2);

	return result;
}


/**
 * @brief Compute the Digest KD() keyed-digest function.
 *
 * Concatenates \a secret, ':', and \a data, then returns H() of the result.
 *
 * @param secret  The H(A1) secret value.
 * @param data    The colon-separated data string containing nonce, nc, cnonce,
 *                qop, and H(A2).
 * @return The hex-encoded MD5 of the concatenated input.
 */
BString
BHttpAuthentication::_KD(const BString& secret, const BString& data) const
{
	BString encode;
	encode << secret << ':' << data;

	return _H(encode);
}
