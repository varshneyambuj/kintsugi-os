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
 *   Copyright 2014 Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 */

/** @file Certificate.cpp
 *  @brief Thin BCertificate wrapper around OpenSSL X509 providing
 *         subject, issuer, validity and signature introspection. */


#include <Certificate.h>

#include <String.h>

#include "CertificatePrivate.h"


#ifdef OPENSSL_ENABLED


#include <openssl/x509v3.h>


/** @brief Parses an ASN.1 GeneralizedTime / UTCTime into a Unix time_t.
 *  @param asn1 Pointer to an OpenSSL ASN1 time structure holding a
 *              "YYMMDDHHMMSSZ" encoded timestamp.
 *  @return The corresponding time_t value, or B_BAD_DATA on parse failure. */
static time_t
parse_ASN1(ASN1_GENERALIZEDTIME *asn1)
{
	// Get the raw string data out of the ASN1 container. It looks like this:
	// "YYMMDDHHMMSSZ"
	struct tm time;

	if (sscanf((char*)asn1->data, "%2d%2d%2d%2d%2d%2d", &time.tm_year,
			&time.tm_mon, &time.tm_mday, &time.tm_hour, &time.tm_min,
			&time.tm_sec) == 6) {

		// Month is 0 based, and year is 1900-based for mktime.
		time.tm_year += 100;
		time.tm_mon -= 1;

		return mktime(&time);
	}
	return B_BAD_DATA;
}


/** @brief Decodes an X509_NAME structure into a human-readable BString.
 *  @param name OpenSSL X509 name pointer (issuer or subject).
 *  @return A BString containing the oneline textual representation. */
static BString
decode_X509_NAME(X509_NAME* name)
{
	char* buffer = X509_NAME_oneline(name, NULL, 0);

	BString result(buffer);
	OPENSSL_free(buffer);
	return result;
}


// #pragma mark - BCertificate


/** @brief Constructs a certificate from an internal private data holder.
 *  @param data Ownership-taken pointer to a BCertificate::Private. */
BCertificate::BCertificate(Private* data)
{
	fPrivate = data;
}


/** @brief Copy-constructs a certificate by duplicating the underlying X509. */
BCertificate::BCertificate(const BCertificate& other)
{
	fPrivate = new(std::nothrow) BCertificate::Private(other.fPrivate->fX509);
}


/** @brief Destructor. Frees the wrapped X509 data. */
BCertificate::~BCertificate()
{
	delete fPrivate;
}


/** @brief Returns the X.509 version number of the certificate.
 *  @return 1, 2, or 3 corresponding to X.509 v1/v2/v3. */
int
BCertificate::Version() const
{
	return X509_get_version(fPrivate->fX509) + 1;
}


/** @brief Returns the notBefore validity date of the certificate as time_t. */
time_t
BCertificate::StartDate() const
{
	return parse_ASN1(X509_getm_notBefore(fPrivate->fX509));
}


/** @brief Returns the notAfter validity date of the certificate as time_t. */
time_t
BCertificate::ExpirationDate() const
{
	return parse_ASN1(X509_getm_notAfter(fPrivate->fX509));
}


/** @brief Reports whether the certificate has the CA basic constraint set.
 *  @return true if the certificate is usable as a signing authority. */
bool
BCertificate::IsValidAuthority() const
{
	return X509_check_ca(fPrivate->fX509) > 0;
}


/** @brief Reports whether issuer and subject match (a self-signed cert).
 *  @return true if the certificate is self-signed. */
bool
BCertificate::IsSelfSigned() const
{
	return X509_check_issued(fPrivate->fX509, fPrivate->fX509) == X509_V_OK;
}


/** @brief Returns a human-readable string form of the issuer distinguished name. */
BString
BCertificate::Issuer() const
{
	X509_NAME* name = X509_get_issuer_name(fPrivate->fX509);
	return decode_X509_NAME(name);
}


/** @brief Returns a human-readable string form of the subject distinguished name. */
BString
BCertificate::Subject() const
{
	X509_NAME* name = X509_get_subject_name(fPrivate->fX509);
	return decode_X509_NAME(name);
}


/** @brief Returns the long name of the signature algorithm used on the certificate.
 *  @return Algorithm name (e.g. "sha256WithRSAEncryption"), "undefined", or "invalid". */
BString
BCertificate::SignatureAlgorithm() const
{
	int algorithmIdentifier;
	if (!X509_get_signature_info(fPrivate->fX509, NULL, &algorithmIdentifier,
			NULL, NULL)) {
		return BString("invalid");
	}

	if (algorithmIdentifier == NID_undef)
		return BString("undefined");

	const char* buffer = OBJ_nid2ln(algorithmIdentifier);
	return BString(buffer);
}


/** @brief Returns a full human-readable dump of the certificate fields.
 *  @return Multi-line BString produced by OpenSSL's X509_print_ex(). */
BString
BCertificate::String() const
{
	BIO *buffer = BIO_new(BIO_s_mem());
	X509_print_ex(buffer, fPrivate->fX509, XN_FLAG_COMPAT, X509_FLAG_COMPAT);

	char* pointer;
	long length = BIO_get_mem_data(buffer, &pointer);
	BString result(pointer, length);

	BIO_free(buffer);
	return result;
}


/** @brief Equality comparison based on the raw X509 bytes.
 *  @return true if both certificates are bitwise identical. */
bool
BCertificate::operator==(const BCertificate& other) const
{
	return X509_cmp(fPrivate->fX509, other.fPrivate->fX509) == 0;
}


// #pragma mark - BCertificate::Private


/** @brief Wraps an OpenSSL X509 pointer by duplicating it.
 *  @param data Source X509 pointer; a deep copy is stored. */
BCertificate::Private::Private(X509* data)
	: fX509(X509_dup(data))
{
}


/** @brief Frees the duplicated OpenSSL X509 object. */
BCertificate::Private::~Private()
{
	X509_free(fX509);
}


#else


BCertificate::BCertificate(const BCertificate& other)
{
}


BCertificate::BCertificate(Private* data)
{
}


BCertificate::~BCertificate()
{
}


time_t
BCertificate::StartDate() const
{
	return B_NOT_SUPPORTED;
}


time_t
BCertificate::ExpirationDate() const
{
	return B_NOT_SUPPORTED;
}


bool
BCertificate::IsValidAuthority() const
{
	return false;
}


int
BCertificate::Version() const
{
	return B_NOT_SUPPORTED;
}


BString
BCertificate::Issuer() const
{
	return BString();
}


BString
BCertificate::Subject() const
{
	return BString();
}


BString
BCertificate::SignatureAlgorithm() const
{
	return BString();
}


BString
BCertificate::String() const
{
	return BString();
}


bool
BCertificate::operator==(const BCertificate& other) const
{
	return false;
}

#endif
