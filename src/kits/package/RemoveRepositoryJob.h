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

/** @file RemoveRepositoryJob.h
    @brief BJob that removes a repository configuration and its cached data. */

#ifndef _PACKAGE__PRIVATE__REMOVE_REPOSITORY_JOB_H_
#define _PACKAGE__PRIVATE__REMOVE_REPOSITORY_JOB_H_


#include <String.h>

#include <package/Job.h>


namespace BPackageKit {

namespace BPrivate {


/**
 * @brief Job that deletes the on-disk repository configuration and cache files
 *        for the named repository.
 */
class RemoveRepositoryJob : public BJob {
	typedef	BJob				inherited;

public:
								RemoveRepositoryJob(
									const BContext& context,
									const BString& title,
									const BString& repositoryName);
	virtual						~RemoveRepositoryJob();

protected:
	virtual	status_t			Execute();

private:
			BString				fRepositoryName;
};


}	// namespace BPrivate

}	// namespace BPackageKit


#endif // _PACKAGE__PRIVATE__REMOVE_REPOSITORY_JOB_H_
