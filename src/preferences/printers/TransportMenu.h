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
 * MIT License. Copyright 2010, Haiku.
 * Original authors: Michael Pfeiffer.
 */

/** @file TransportMenu.h
    @brief Lazy BMenu listing the ports of a single transport add-on. */

#ifndef _TRANSPORT_MENU_H
#define _TRANSPORT_MENU_H


#include <Menu.h>
#include <Messenger.h>
#include <String.h>


/**
 * @brief Submenu that lazily enumerates a transport's ports.
 *
 * Created by AddPrinterDialog when a transport advertises a list of
 * ports; populated the first time the menu is opened by scripting
 * print_server.
 */
class TransportMenu : public BMenu
{
public:
			TransportMenu(const char* title, uint32 what,
				const BMessenger& messenger, const BString& transportName);

	bool	AddDynamicItem(add_state s);

private:
	uint32		fWhat;
	BMessenger	fMessenger;
	BString		fTransportName;
};

#endif
