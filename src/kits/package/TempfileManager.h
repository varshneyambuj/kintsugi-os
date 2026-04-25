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
 * MIT License. Copyright 2011, Haiku.
 * Original author: Oliver Tappe <zooey@hirschkaefer.de>.
 */

/** @file TempfileManager.h
    @brief Helper for allocating uniquely-named scratch files within a base directory. */

#ifndef _PACKAGE__PRIVATE__TEMPFILE_MANAGER_H_
#define _PACKAGE__PRIVATE__TEMPFILE_MANAGER_H_


#include <Directory.h>
#include <Entry.h>
#include <String.h>
#include <SupportDefs.h>


namespace BPackageKit {

namespace BPrivate {


/**
 * @brief Allocates uniquely-named temporary files inside a configurable base
 *        directory and removes them when the manager is destroyed.
 */
class TempfileManager {
public:
								TempfileManager();
								~TempfileManager();

			void				SetBaseDirectory(const BDirectory& baseDir);

			BEntry				Create(const BString& baseName = kDefaultName);

private:
	static	const BString		kDefaultName;

private:
			BDirectory			fBaseDirectory;
			int32				fNextNumber;
};


}	// namespace BPrivate

}	// namespace BPackageKit


#endif // _PACKAGE__PRIVATE__TEMPFILE_MANAGER_H_
