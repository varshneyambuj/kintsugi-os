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

/** @file ActivateRepositoryConfigJob.h
    @brief BJob that installs a fetched repository config archive into the active set. */

#ifndef _PACKAGE__PRIVATE__ACTIVATE_REPOSITORY_CONFIG_JOB_H_
#define _PACKAGE__PRIVATE__ACTIVATE_REPOSITORY_CONFIG_JOB_H_


#include <Directory.h>
#include <Entry.h>
#include <String.h>

#include <package/Job.h>


namespace BPackageKit {

namespace BPrivate {


/**
 * @brief Job that copies a downloaded repository-info archive into the
 *        configured target directory and records its base URL.
 */
class ActivateRepositoryConfigJob : public BJob {
	typedef	BJob				inherited;

public:
								ActivateRepositoryConfigJob(
									const BContext& context,
									const BString& title,
									const BEntry& archivedRepoInfoEntry,
									const BString& repositoryBaseURL,
									const BDirectory& targetDirectory);
	virtual						~ActivateRepositoryConfigJob();

			const BString&		RepositoryName() const;

protected:
	virtual	status_t			Execute();
	virtual	void				Cleanup(status_t jobResult);

private:
			BEntry				fArchivedRepoInfoEntry;
			BString				fRepositoryBaseURL;
			BDirectory			fTargetDirectory;
			BEntry				fTargetEntry;

			BString				fRepositoryName;
};


}	// namespace BPrivate

}	// namespace BPackageKit


#endif // _PACKAGE__PRIVATE__ACTIVATE_REPOSITORY_CONFIG_JOB_H_
