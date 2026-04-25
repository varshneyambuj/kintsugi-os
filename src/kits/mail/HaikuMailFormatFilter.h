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
 * MIT License. Copyright 2011-2013, Haiku, Inc.
 * Original author: Clemens Zeidler <haiku@clemens-zeidler.de>.
 */

/** @file HaikuMailFormatFilter.h
    @brief Built-in mail filter that writes received messages in Haiku's BFS attribute layout. */

#ifndef IMAP_LISTENER_H
#define IMAP_LISTENER_H


#include <MailFilter.h>
#include <String.h>


/** @brief Mail filter that maps fetched headers to BFS attributes and assigns
           outbound message names following the platform's mail conventions. */
class HaikuMailFormatFilter : public BMailFilter {
public:
								HaikuMailFormatFilter(BMailProtocol& protocol,
									const BMailAccountSettings& settings);

	virtual BString				DescriptiveName() const;

			BMailFilterAction	HeaderFetched(entry_ref& ref, BFile& file,
									BMessage& attributes);
			void				BodyFetched(const entry_ref& ref, BFile& file,
									BMessage& attributes);

			void				MessageSent(const entry_ref& ref, BFile& file);

private:
			void				_RemoveExtraWhitespace(BString& name);
			void				_RemoveLeadingDots(BString& name);
			BString				_ExtractName(const BString& from);
			status_t			_SetType(BMessage& attributes,
									const char* mimeType);

private:
			int32				fAccountID;
			BString				fAccountName;
			BString				fOutboundDirectory;
};


#endif // IMAP_LISTENER_H
