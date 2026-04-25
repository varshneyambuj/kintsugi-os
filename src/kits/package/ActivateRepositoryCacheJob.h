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

/** @file ActivateRepositoryCacheJob.h
    @brief BJob that promotes a fetched repository cache file to the active cache slot. */

#ifndef _PACKAGE__PRIVATE__ACTIVATE_REPOSITORY_CACHE_JOB_H_
#define _PACKAGE__PRIVATE__ACTIVATE_REPOSITORY_CACHE_JOB_H_


#include <Directory.h>
#include <Entry.h>
#include <String.h>

#include <package/Job.h>


namespace BPackageKit {

namespace BPrivate {


/**
 * @brief Job that moves a freshly fetched repository cache file into its
 *        permanent location, replacing any prior cache for the repository.
 */
class ActivateRepositoryCacheJob : public BJob {
	typedef	BJob				inherited;

public:
								ActivateRepositoryCacheJob(
									const BContext& context,
									const BString& title,
									const BEntry& fetchedRepoCacheEntry,
									const BString& repositoryName,
									const BDirectory& targetDirectory);
	virtual						~ActivateRepositoryCacheJob();

protected:
	virtual	status_t			Execute();

private:
			BEntry				fFetchedRepoCacheEntry;
			BString				fRepositoryName;
			BDirectory			fTargetDirectory;
};


}	// namespace BPrivate

}	// namespace BPackageKit


#endif // _PACKAGE__PRIVATE__ACTIVATE_REPOSITORY_CACHE_JOB_H_
