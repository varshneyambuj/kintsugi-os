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
 *   Copyright 2010-2015 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christophe Huriaux, c.huriaux@gmail.com
 *       Adrien Destugues, pulkomandy@pulkomandy.tk
 */


/**
 * @file UrlContext.cpp
 * @brief Implementation of BUrlContext, the per-session URL request state container.
 *
 * Holds a cookie jar, a domain-keyed map of HTTP authentication objects, a
 * list of accepted certificate exceptions, and optional proxy host/port
 * settings. A default empty authentication entry (keyed on the empty string)
 * ensures that GetAuthentication() always returns a valid reference.
 *
 * @see BHttpRequest, BNetworkCookieJar, BHttpAuthentication
 */


#include <UrlContext.h>

#include <stdio.h>

#include <HashMap.h>
#include <HashString.h>

using namespace BPrivate::Network;


class BUrlContext::BHttpAuthenticationMap : public
	SynchronizedHashMap<BPrivate::HashString, BHttpAuthentication*> {};


/**
 * @brief Construct a default BUrlContext with an empty cookie jar and no proxy.
 *
 * Allocates the authentication map and inserts a default empty authentication
 * object keyed on the empty string so that GetAuthentication() always succeeds.
 */
BUrlContext::BUrlContext()
	:
	fCookieJar(),
	fAuthenticationMap(NULL),
	fCertificates(20),
	fProxyHost(),
	fProxyPort(0)
{
	fAuthenticationMap = new(std::nothrow) BHttpAuthenticationMap();

	// This is the default authentication, used when nothing else is found.
	// The empty string used as a key will match all the domain strings, once
	// we have removed all components.
	fAuthenticationMap->Put(HashString("", 0), new BHttpAuthentication());
}


/**
 * @brief Destructor — deletes all authentication objects and the map.
 */
BUrlContext::~BUrlContext()
{
	BHttpAuthenticationMap::Iterator iterator
		= fAuthenticationMap->GetIterator();
	while (iterator.HasNext())
		delete iterator.Next().value;

	delete fAuthenticationMap;
}


// #pragma mark Context modifiers


/**
 * @brief Replace the context's cookie jar with a copy of \a cookieJar.
 *
 * @param cookieJar  The new BNetworkCookieJar to copy into this context.
 */
void
BUrlContext::SetCookieJar(const BNetworkCookieJar& cookieJar)
{
	fCookieJar = cookieJar;
}


/**
 * @brief Store or update the HTTP authentication for a URL.
 *
 * The key is formed from the host and path of \a url. If an entry already
 * exists for this key it is updated in-place; otherwise a heap-allocated copy
 * of \a authentication is inserted.
 *
 * @param url             The URL whose host+path forms the authentication key.
 * @param authentication  The BHttpAuthentication to store.
 */
void
BUrlContext::AddAuthentication(const BUrl& url,
	const BHttpAuthentication& authentication)
{
	BString domain = url.Host();
	domain += url.Path();
	BPrivate::HashString hostHash(domain.String(), domain.Length());

	fAuthenticationMap->Lock();

	BHttpAuthentication* previous = fAuthenticationMap->Get(hostHash);

	if (previous)
		*previous = authentication;
	else {
		BHttpAuthentication* copy
			= new(std::nothrow) BHttpAuthentication(authentication);
		fAuthenticationMap->Put(hostHash, copy);
	}

	fAuthenticationMap->Unlock();
}


/**
 * @brief Set the HTTP proxy host and port for all requests using this context.
 *
 * @param host  The proxy hostname or IP address.
 * @param port  The proxy TCP port number.
 */
void
BUrlContext::SetProxy(BString host, uint16 port)
{
	fProxyHost = host;
	fProxyPort = port;
}


/**
 * @brief Add a TLS certificate exception to this context.
 *
 * The certificate is copied so the caller may release their reference after
 * this call. The exception persists for the lifetime of the context.
 *
 * @param certificate  The BCertificate to accept despite verification failure.
 */
void
BUrlContext::AddCertificateException(const BCertificate& certificate)
{
	BCertificate* copy = new(std::nothrow) BCertificate(certificate);
	if (copy != NULL) {
		fCertificates.AddItem(copy);
	}
}


// #pragma mark Context accessors


/**
 * @brief Return a mutable reference to the context's cookie jar.
 *
 * @return A reference to the internal BNetworkCookieJar.
 */
BNetworkCookieJar&
BUrlContext::GetCookieJar()
{
	return fCookieJar;
}


/**
 * @brief Retrieve the authentication object for the given URL.
 *
 * Progressively strips path components from the URL until an entry is found in
 * the authentication map. The default empty-string entry guarantees a match.
 *
 * @param url  The URL whose authentication credentials are needed.
 * @return A reference to the matching BHttpAuthentication object.
 */
BHttpAuthentication&
BUrlContext::GetAuthentication(const BUrl& url)
{
	BString domain = url.Host();
	domain += url.Path();

	BHttpAuthentication* authentication = NULL;

	do {
		authentication = fAuthenticationMap->Get( HashString(domain.String(),
			domain.Length()));

		domain.Truncate(domain.FindLast('/'));

	} while (authentication == NULL);

	return *authentication;
}


/**
 * @brief Return whether a proxy has been configured for this context.
 *
 * @return true if fProxyHost is non-empty, false otherwise.
 */
bool
BUrlContext::UseProxy()
{
	return !fProxyHost.IsEmpty();
}


/**
 * @brief Return the configured proxy hostname.
 *
 * @return The proxy host BString, empty if no proxy is configured.
 */
BString
BUrlContext::GetProxyHost()
{
	return fProxyHost;
}


/**
 * @brief Return the configured proxy port number.
 *
 * @return The proxy port, or 0 if no proxy is configured.
 */
uint16
BUrlContext::GetProxyPort()
{
	return fProxyPort;
}


/**
 * @brief Test whether the given certificate is in the exception list.
 *
 * Uses the BCertificate::operator==() predicate via a UnaryPredicate functor
 * to scan the fCertificates list.
 *
 * @param certificate  The certificate to look up.
 * @return true if the certificate has a stored exception, false otherwise.
 */
bool
BUrlContext::HasCertificateException(const BCertificate& certificate)
{
	struct Equals: public UnaryPredicate<const BCertificate> {
		Equals(const BCertificate& itemToMatch)
			:
			fItemToMatch(itemToMatch)
		{
		}

		int operator()(const BCertificate* item) const
		{
			/* Must return 0 if there is a match! */
			return !(*item == fItemToMatch);
		}

		const BCertificate& fItemToMatch;
	} comparator(certificate);

	return fCertificates.FindIf(comparator) != NULL;
}
