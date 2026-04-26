/*
 * Copyright 2026, Kintsugi OS Contributors. All rights reserved.
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
 * MIT License. Copyright 2007-2011, Haiku, Inc. and Copyright 2011, Clemens
 * Zeidler <haiku@clemens-zeidler.de>.
 */

/** @file AutoConfig.h
    @brief Provider auto-configuration helper for the Mail preferences app:
           looks up known mail providers from a settings database, falls
           back to MX-record DNS lookups, and finally guesses common
           hostnames when the address is unknown. */

#ifndef AUTO_CONFIG_H
#define AUTO_CONFIG_H


#include <List.h>
#include <Node.h>
#include <String.h>

/** @brief Subdirectory under the user settings directory where per-provider
           info files live. */
#define INFO_DIR "Mail/ProviderInfo"

/** @brief BFS attribute name on a provider_info node holding the POP server
           hostname. */
#define ATTR_NAME_POPSERVER "POP Server"
/** @brief BFS attribute name on a provider_info node holding the IMAP
           server hostname. */
#define ATTR_NAME_IMAPSERVER "IMAP Server"
/** @brief BFS attribute name on a provider_info node holding the SMTP
           server hostname. */
#define ATTR_NAME_SMTPSERVER "SMTP Server"
/** @brief BFS attribute encoding the POP authentication scheme (0 = plain
           text, 1 = APOP). */
#define ATTR_NAME_AUTHPOP "POP Authentication"
/** @brief BFS attribute encoding the SMTP authentication scheme (0 = none,
           1 = ESMTP, 2 = POP3 before SMTP). */
#define ATTR_NAME_AUTHSMTP "SMTP Authentication"
/** @brief BFS attribute encoding the POP SSL/TLS mode index. */
#define ATTR_NAME_POPSSL "POP SSL"
/** @brief BFS attribute encoding the IMAP SSL/TLS mode index. */
#define ATTR_NAME_IMAPSSL "IMAP SSL"
/** @brief BFS attribute encoding the SMTP SSL/TLS mode index. */
#define ATTR_NAME_SMTPSSL "SMTP SSL"
/** @brief BFS attribute encoding the proposed username pattern (0 = full
           e-mail address, 1 = local-part only, 2 = no proposal). */
#define ATTR_NAME_USERNAME "Username Pattern"


/*
ATTR_NAME_AUTHPOP:
	0	plain text
	1	APOP

ATTR_NAME_AUTHSMTP:
	0	none
	1	ESMTP
	2	POP3 before SMTP

ATTR_NAME_USERNAME:
	0	username is the email address (default)
	1	username is the local-part of the email address local-part@domain.net
	2	no username is proposed
*/



/**
 * @brief Aggregated server, authentication, and encryption settings inferred
 *        for a single mail provider.
 *
 * Populated by AutoConfig either from the on-disk provider database, an MX
 * lookup, or a hostname guess. Consumed by the auto-config wizard to pre-fill
 * the account form.
 */
struct provider_info
{
	BString provider;

	BString pop_server;
	BString imap_server;
	BString smtp_server;

	int32 authentification_pop;
	int32 authentification_smtp;

	int32 ssl_pop;
	int32 ssl_imap;
	int32 ssl_smtp;

	int32 username_pattern;
};


/**
 * @brief Resolves provider_info for an e-mail address using a layered
 *        strategy: local provider database, MX-record query, then hostname
 *        guess.
 */
class AutoConfig
{
	public:
		status_t		GetInfoFromMailAddress(const char* email,
												provider_info *info);

		// for debug
		void			PrintProviderInfo(provider_info* pInfo);

	private:
		status_t		GetMXRecord(const char* provider, provider_info *info);
		status_t		GuessServerName(const char* provider,
											provider_info *info);

		BString			ExtractProvider(const char* email);
		status_t		LoadProviderInfo(const BString &provider, provider_info* info);
		bool			ReadProviderInfo(BNode *node, provider_info* info);

};



#endif
