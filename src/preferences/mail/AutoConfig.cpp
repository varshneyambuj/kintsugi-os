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
 *   Copyright 2007-2011, Haiku, Inc. All rights reserved.
 *   Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file AutoConfig.cpp
 * @brief Implements AutoConfig, the provider lookup helper used by the
 *        Mail preferences auto-configuration wizard.
 *
 * Resolution proceeds in three stages: a local on-disk provider database
 * (under @c B_USER_SETTINGS_DIRECTORY/Mail/ProviderInfo), an MX-record
 * DNS lookup via DNSQuery, and finally a name heuristic that prepends
 * "mail." to the domain.
 */


#include "AutoConfig.h"
#include "DNSQuery.h"

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <stdio.h>


/**
 * @brief Populates @a info for the given e-mail address using the layered
 *        lookup strategy.
 *
 * The local provider database is consulted first. If that misses, an MX
 * record query is attempted; if that fails as well, the server names are
 * guessed from the domain part. The function only returns @c B_OK when the
 * local database matched; otherwise it returns @c B_ENTRY_NOT_FOUND even
 * though @a info has been filled with a best-effort guess.
 *
 * @param email  Full e-mail address whose provider should be resolved.
 * @param info   Destination structure populated with server names, auth
 *               types, and SSL flags. Must not be @c NULL.
 * @retval B_OK              The provider database supplied an exact match.
 * @retval B_ENTRY_NOT_FOUND No database hit; @a info contains MX or guessed
 *                           values.
 */
status_t
AutoConfig::GetInfoFromMailAddress(const char* email, provider_info *info)
{
	BString provider = ExtractProvider(email);

	// first check the database
	if (LoadProviderInfo(provider, info) == B_OK)
		return B_OK;

	// fallback try to read MX record
	if (GetMXRecord(provider.String(), info) == B_OK) 
		return B_ENTRY_NOT_FOUND;

	// if no MX record received guess a name
	GuessServerName(provider.String(), info);
	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Resolves provider hostnames from the highest-priority MX record of
 *        @a provider.
 *
 * The first MX record's serverName is used for IMAP, POP, and SMTP entries.
 * Auth and username pattern fields are reset to their default zero values.
 *
 * @param provider  Bare domain (the part after the @c '@' in an address).
 * @param info      Destination provider_info; must not be @c NULL.
 * @retval B_OK     One or more MX records were retrieved successfully.
 * @retval B_ERROR  No DNS server reachable or no MX records returned.
 */
status_t
AutoConfig::GetMXRecord(const char* provider, provider_info *info)
{
	BObjectList<mx_record, true> mxList(5);
	DNSQuery dnsQuery;
	if (dnsQuery.GetMXRecords(provider, &mxList) != B_OK)
		return B_ERROR;

	mx_record *mxRec = mxList.ItemAt(0);
	if (mxRec == NULL)
		return B_ERROR;

	info->imap_server = mxRec->serverName;
	info->pop_server =  mxRec->serverName;
	info->smtp_server =  mxRec->serverName;

	info->authentification_pop = 0;
	info->authentification_smtp = 0;
	info->username_pattern = 0;
	return B_OK;

}


/**
 * @brief Last-ditch heuristic that fills the server fields with
 *        @c "mail.<provider>".
 *
 * @param provider  Bare domain to prefix with @c "mail.".
 * @param info      Destination provider_info; must not be @c NULL.
 * @return Always @c B_OK.
 */
status_t
AutoConfig::GuessServerName(const char* provider, provider_info* info)
{
	info->imap_server = "mail.";
	info->imap_server += provider;
	info->pop_server = "mail.";
	info->pop_server +=  provider;
	info->smtp_server = "mail.";
	info->smtp_server +=  provider;

	info->authentification_pop = 0;
	info->authentification_smtp = 0;
	info->username_pattern = 0;
	return B_OK;
}


/**
 * @brief Debug helper that dumps the contents of @a pInfo to stdout.
 *
 * @param pInfo  Provider record to print; must not be @c NULL.
 */
void
AutoConfig::PrintProviderInfo(provider_info* pInfo)
{
	printf("Provider: %s:\n", pInfo->provider.String());
	printf("pop_mail_host: %s\n", pInfo->pop_server.String());
	printf("imap_mail_host: %s\n", pInfo->imap_server.String());
	printf("smtp_host: %s\n", pInfo->smtp_server.String());
	printf("pop authentication: %i\n", int(pInfo->authentification_pop));
	printf("smtp authentication: %i\n",
			int(pInfo->authentification_smtp));
	printf("username_pattern: %i\n",
			int(pInfo->username_pattern));
}


/**
 * @brief Extracts the domain portion that follows the last @c '@' in
 *        @a email.
 *
 * @param email  Full e-mail address.
 * @return The domain substring; an empty BString if no @c '@' is present.
 */
BString
AutoConfig::ExtractProvider(const char* email)
{
	BString emailS(email);
	BString provider;
	int32 at = emailS.FindLast("@");
	emailS.CopyInto(provider, at + 1, emailS.Length() - at);
	return provider;
}



/**
 * @brief Looks up @a provider as a file in the user-settings provider
 *        database and parses its BFS attributes into @a info.
 *
 * @param provider  Bare domain used as the filename in
 *                  @c B_USER_SETTINGS_DIRECTORY/Mail/ProviderInfo.
 * @param info      Destination provider_info; must not be @c NULL.
 * @retval B_OK              File present and at least one attribute read.
 * @retval B_ENTRY_NOT_FOUND No matching provider file exists.
 * @retval B_ERROR           File present but no recognisable attributes.
 */
status_t
AutoConfig::LoadProviderInfo(const BString &provider, provider_info* info)
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	path.Append(INFO_DIR);
	BDirectory infoDir(path.Path());
	
	BFile infoFile(&infoDir, provider.String(), B_READ_ONLY);
	if (infoFile.InitCheck() != B_OK)
		return B_ENTRY_NOT_FOUND;

	info->provider = provider;
	if (ReadProviderInfo(&infoFile, info) == true)
		return B_OK;
	
	return B_ERROR;
}


/**
 * @brief Reads the per-provider BFS attributes off @a node and copies them
 *        into @a info.
 *
 * Each attribute is optional; the function records whether at least one
 * recognisable value was found.
 *
 * @param node  Open BNode for the provider file.
 * @param info  Destination provider_info; must not be @c NULL.
 * @return @c true if any attribute was successfully read, @c false if the
 *         node carried no provider attributes.
 */
bool
AutoConfig::ReadProviderInfo(BNode *node, provider_info* info)
{
	bool infoFound = false;
	char buffer[255];

	// server
	ssize_t size;
	size = node->ReadAttr(ATTR_NAME_POPSERVER, B_STRING_TYPE, 0, &buffer, 255);
	if (size > 0) {
		info->pop_server = buffer;
		infoFound = true;
	}
	size = node->ReadAttr(ATTR_NAME_IMAPSERVER, B_STRING_TYPE, 0, &buffer, 255);
	if (size > 0) {
		info->imap_server = buffer;
		infoFound = true;
	}
	size = node->ReadAttr(ATTR_NAME_SMTPSERVER, B_STRING_TYPE, 0, &buffer, 255);
	if (size > 0) {
		info->smtp_server = buffer;
		infoFound = true;
	}

	// authentication type
	int32 authType;
	size = node->ReadAttr(ATTR_NAME_AUTHPOP, B_INT32_TYPE, 0, &authType,
							sizeof(int32));
	if (size == sizeof(int32)) {
		info->authentification_pop = authType;
		infoFound = true;
	}
	size = node->ReadAttr(ATTR_NAME_AUTHSMTP, B_INT32_TYPE, 0, &authType,
							sizeof(int32));
	if (size == sizeof(int32)) {
		info->authentification_smtp = authType;
		infoFound = true;
	}

	// ssl
	int32 ssl;
	size = node->ReadAttr(ATTR_NAME_POPSSL, B_INT32_TYPE, 0, &ssl,
							sizeof(int32));
	if (size == sizeof(int32)) {
		info->ssl_pop = ssl;
		infoFound = true;
	}
	size = node->ReadAttr(ATTR_NAME_IMAPSSL, B_INT32_TYPE, 0, &ssl,
							sizeof(int32));
	if (size == sizeof(int32)) {
		info->ssl_imap = ssl;
		infoFound = true;
	}
	size = node->ReadAttr(ATTR_NAME_SMTPSSL, B_INT32_TYPE, 0, &ssl,
							sizeof(int32));
	if (size == sizeof(int32)) {
		info->ssl_smtp = ssl;
		infoFound = true;
	}

	// username pattern
	int32 pattern;
	size = node->ReadAttr(ATTR_NAME_USERNAME, B_INT32_TYPE, 0, &pattern,
							sizeof(int32));
	if (size == sizeof(int32)) {
		info->username_pattern = pattern;
		infoFound = true;
	}
	
	return infoFound;
}

