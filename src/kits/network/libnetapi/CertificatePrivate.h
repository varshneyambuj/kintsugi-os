/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2014, Haiku Inc.
 */

/** @file CertificatePrivate.h
    @brief Private wrapper around an OpenSSL X509 handle used as the
           pimpl-style backing store for BCertificate. */

#ifndef _CERTIFICATE_PRIVATE_H
#define _CERTIFICATE_PRIVATE_H


#ifdef OPENSSL_ENABLED
#	include <openssl/ssl.h>


/** @brief Owns the X509 certificate handle behind a BCertificate facade. */
class BCertificate::Private {
public:
	/** @brief Construct from an existing OpenSSL @a data handle (takes ownership). */
	Private(X509* data);
	/** @brief Frees the wrapped X509 handle. */
	~Private();

public:
	X509* fX509;
};
#endif


#endif
