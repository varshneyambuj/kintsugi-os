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
 *   Copyright 2008, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ithamar R. Adema
 */

/** @file Transport.h
 *  @brief Transport add-on representation for the print server. */

#ifndef TRANSPORT_H
#define TRANSPORT_H

class Transport;

#include <FindDirectory.h>
#include <Handler.h>
#include <String.h>
#include <Path.h>

#include <ObjectList.h>

/** @brief Represents a print transport add-on (e.g., USB, network, file). */
class Transport : public BHandler
{
	typedef BHandler Inherited;
public:
	/** @brief Construct a transport from its add-on path. */
	Transport(const BPath& path);
	/** @brief Destructor removing this transport from the global list. */
	~Transport();

	/** @brief Return the transport name derived from the add-on filename. */
	BString Name() const { return fPath.Leaf(); }

	/** @brief Query the add-on for currently available ports or devices. */
	status_t ListAvailablePorts(BMessage* msg);

	/** @brief Scan a directory for transport add-ons and register them. */
	static status_t Scan(directory_which which);

	/** @brief Find a transport by name in the global list. */
	static Transport* Find(const BString& name);
	/** @brief Remove a transport from the global list. */
	static void Remove(Transport* transport);
	/** @brief Return the transport at the given index. */
	static Transport* At(int32 idx);
	/** @brief Return the total number of registered transports. */
	static int32 CountTransports();

	/** @brief Handle incoming messages including scripting commands. */
	void MessageReceived(BMessage* msg);

		// Scripting support, see Transport.Scripting.cpp
	/** @brief Return the scripting suites supported by this transport. */
	status_t GetSupportedSuites(BMessage* msg);
	/** @brief Handle a scripting command for this transport. */
	void HandleScriptingCommand(BMessage* msg);
	/** @brief Resolve a scripting specifier to the appropriate handler. */
	BHandler* ResolveSpecifier(BMessage* msg, int32 index, BMessage* spec,
								int32 form, const char* prop);

private:
	BPath fPath; /**< Filesystem path to the transport add-on */
	long fImageID; /**< Image ID if the add-on is kept loaded */
	int fFeatures; /**< Transport feature flags (e.g., hotplug support) */

	static BObjectList<Transport> sTransports; /**< Global list of registered transports */
};

#endif
