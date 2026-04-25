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
 * MIT License. Copyright 2011-2015, Haiku.
 * Original authors: Oliver Tappe <zooey@hirschkaefer.de>,
 *                   Rene Gollent <rene@gollent.com>,
 *                   Axel Dörfler <axeld@pinc-software.de>.
 */

/** @file FetchFileJob.h
    @brief BJob that downloads a remote package or repository artefact to a local entry. */

#ifndef _PACKAGE__PRIVATE__FETCH_FILE_JOB_H_
#define _PACKAGE__PRIVATE__FETCH_FILE_JOB_H_


#include <Entry.h>
#include <File.h>
#include <String.h>

#ifdef HAIKU_TARGET_PLATFORM_HAIKU
#	include <UrlProtocolListener.h>
#endif

#include <package/Job.h>

#ifdef HAIKU_TARGET_PLATFORM_HAIKU
using BPrivate::Network::BUrlProtocolListener;
using BPrivate::Network::BUrlRequest;
#endif


namespace BPackageKit {

namespace BPrivate {


/**
 * @brief Job that downloads a file from a URL to a target BEntry, reporting
 *        progress via BUrlProtocolListener callbacks on Haiku targets.
 */
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
class FetchFileJob : public BJob, public BUrlProtocolListener {
#else // HAIKU_TARGET_PLATFORM_HAIKU
class FetchFileJob : public BJob {
#endif // HAIKU_TARGET_PLATFORM_HAIKU

	typedef	BJob				inherited;

public:
								FetchFileJob(const BContext& context,
									const BString& title,
									const BString& fileURL,
									const BEntry& targetEntry);
	virtual						~FetchFileJob();

			float				DownloadProgress() const;
			const char*			DownloadURL() const;
			const char*			DownloadFileName() const;
			off_t				DownloadBytes() const;
			off_t				DownloadTotalBytes() const;

#ifdef HAIKU_TARGET_PLATFORM_HAIKU
	virtual void	DownloadProgress(BUrlRequest*,
						off_t bytesReceived, off_t bytesTotal);
	virtual void 	RequestCompleted(BUrlRequest* request,
						bool success);
#endif

protected:
	virtual	status_t			Execute();
	virtual	void				Cleanup(status_t jobResult);

private:
			BString				fFileURL;
			BEntry				fTargetEntry;
			BFile				fTargetFile;
			status_t			fError;
			float				fDownloadProgress;
			off_t				fBytes;
			off_t				fTotalBytes;
};


}	// namespace BPrivate

}	// namespace BPackageKit


#endif // _PACKAGE__PRIVATE__FETCH_FILE_JOB_H_
