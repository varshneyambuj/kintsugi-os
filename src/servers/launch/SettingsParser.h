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
 *   Copyright 2015, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file SettingsParser.h
 *  @brief Parses launch_daemon configuration files into a tree-shaped BMessage. */

#ifndef SETTINGS_PARSER_H
#define SETTINGS_PARSER_H


#include <Message.h>


/** @brief Reads launch_daemon settings files and converts them into BMessage form. */
class SettingsParser {
public:
								SettingsParser();

	/** @brief Parses the configuration file at @p path into @p settings. */
			status_t			ParseFile(const char* path, BMessage& settings);

#ifdef TEST_HAIKU
	/** @brief Test-only entry point that parses an in-memory configuration string. */
			status_t			Parse(const char* text, BMessage& settings);
#endif
};


#endif // SETTINGS_PARSER_H
