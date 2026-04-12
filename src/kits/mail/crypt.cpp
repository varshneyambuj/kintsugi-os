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
 *   crypt - simple encryption algorithm used for passwords
 *   Copyright 2001 Dr. Zoidberg Enterprises. All rights reserved.
 */


/**
 * @file crypt.cpp
 * @brief Simple XOR-based password obfuscation for the mail kit.
 *
 * Provides get_passwd(), set_passwd(), and passwd_crypt() to store and
 * retrieve account passwords in BMessage settings archives. Passwords are
 * obfuscated with a fixed-length XOR key so that they are not stored in
 * plain text in settings files, though this is not a cryptographically strong
 * protection.
 *
 * @see BMailAccountSettings, MailSettings.cpp
 */


#include <Message.h>

#include <string.h>
#include <crypt.h>


/** @brief Fixed XOR key used by passwd_crypt(); length matches PASSWORD_LENGTH. */
static const char key[PASSWORD_LENGTH + 1] = "Dr. Zoidberg Enterprises, BeMail";


/**
 * @brief Retrieves and decrypts a password stored in a BMessage.
 *
 * Reads the raw (obfuscated) bytes stored under \a name in \a msg, decrypts
 * them with passwd_crypt(), and returns the result as a newly allocated
 * null-terminated string.
 *
 * @param msg   BMessage containing the encrypted password field.
 * @param name  Name of the B_RAW_TYPE field holding the obfuscated bytes.
 * @return Heap-allocated plaintext password string, or NULL if the field is
 *         absent or empty. The caller must delete[] the returned buffer.
 */
_EXPORT char *get_passwd(const BMessage *msg,const char *name)
{
	char *encryptedPassword;
	ssize_t length;
	if (msg->FindData(name,B_RAW_TYPE,(const void **)&encryptedPassword,&length) < B_OK || !encryptedPassword || length == 0)
		return NULL;

	char *buffer = new char[length];
	passwd_crypt(encryptedPassword,buffer,length);

	return buffer;
}


/**
 * @brief Encrypts and stores a password in a BMessage.
 *
 * XOR-encrypts \a password with the internal key and stores the result as
 * B_RAW_TYPE data under \a name, replacing any previously stored value.
 *
 * @param msg       BMessage to receive the encrypted password field.
 * @param name      Field name under which to store the obfuscated bytes.
 * @param password  Null-terminated plaintext password, or NULL (returns false).
 * @return true on success, false if \a password is NULL or storage fails.
 */
_EXPORT bool set_passwd(BMessage *msg,const char *name,const char *password)
{
	if (!password)
		return false;

	ssize_t length = strlen(password) + 1;
	char *buffer = new char[length];
	passwd_crypt((char *)password,buffer,length);

	msg->RemoveName(name);
	status_t status = msg->AddData(name,B_RAW_TYPE,buffer,length,false);

	delete [] buffer;
	return (status >= B_OK);
}


/**
 * @brief Applies XOR obfuscation to a password buffer in-place or between buffers.
 *
 * Each byte of \a in is XORed with the corresponding byte of the fixed key
 * and written to \a out. At most PASSWORD_LENGTH bytes are XORed; bytes
 * beyond that are copied without modification (via the prior memcpy).
 *
 * @param in      Source buffer containing the text to transform.
 * @param out     Destination buffer to receive the transformed bytes.
 * @param length  Number of bytes to process from \a in.
 * @note \a in and \a out may be different buffers but must not overlap in an
 *       undefined way. Calling twice on the same data reverses the operation.
 */
_EXPORT void passwd_crypt(char *in,char *out,int length)
{
	int i;

	memcpy(out,in,length);
	if (length > PASSWORD_LENGTH)
		length = PASSWORD_LENGTH;

	for (i = 0;i < length;i++)
		out[i] ^= key[i];
}
