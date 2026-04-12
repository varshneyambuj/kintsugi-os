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
 *   Copyright 2011-2015, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Tappe <zooey@hirschkaefer.de>
 *       Rene Gollent <rene@gollent.com>
 */


/**
 * @file Job.cpp
 * @brief Package-kit base job class that binds a BSupportKit::BJob to a BContext.
 *
 * BJob extends BSupportKit::BJob by holding a reference to a BContext so that
 * concrete job subclasses can access the decision provider, the job-state
 * listener, and the temporary-file manager without passing them as separate
 * parameters to every method.
 *
 * @see BContext, FetchFileJob, ActivateRepositoryCacheJob
 */


#include <package/Job.h>


namespace BPackageKit {


/**
 * @brief Construct a job associated with the given context.
 *
 * @param context  The package-kit context providing shared services.
 * @param title    Human-readable job title forwarded to BSupportKit::BJob.
 */
BJob::BJob(const BContext& context, const BString& title)
	:
	BSupportKit::BJob(title),
	fContext(context)
{
}


/**
 * @brief Destructor.
 */
BJob::~BJob()
{
}


}	// namespace BPackageKit
