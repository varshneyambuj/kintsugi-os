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
 *   Copyright 2013, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 */


/**
 * @file Solver.cpp
 * @brief BSolver base class and factory for the dependency-resolution add-on.
 *
 * BSolver is an abstract interface for package dependency resolution. The
 * concrete implementation lives in a dynamically loaded add-on
 * (libpackage-add-on-libsolv.so). BSolver::Create() loads the add-on on
 * first call using a pthread_once guard and returns a new solver instance
 * initialised via Init().
 *
 * @see BSolverRepository, BSolverResult, BPackageManager
 */


#include <package/solver/Solver.h>


typedef BPackageKit::BSolver* CreateSolverFunction();


#include <dlfcn.h>
#include <pthread.h>


/** @brief Pointer to the create_solver() symbol from the loaded add-on. */
static CreateSolverFunction* sCreateSolver = NULL;

/** @brief pthread_once guard ensuring the add-on is loaded exactly once. */
static pthread_once_t sLoadLibsolvSolverAddOnInitOnce = PTHREAD_ONCE_INIT;


/**
 * @brief Load the libsolv solver add-on and resolve the create_solver symbol.
 *
 * Called exactly once via pthread_once. If loading or symbol resolution fails,
 * sCreateSolver remains NULL and subsequent Create() calls return B_NOT_SUPPORTED.
 */
static void
load_libsolv_solver_add_on()
{
	int flags = 0;
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
	void* imageHandle = dlopen("libpackage-add-on-libsolv.so", flags);
#else
#ifdef HAIKU_HOST_PLATFORM_LINUX
	flags = RTLD_LAZY | RTLD_LOCAL;
#endif
	void* imageHandle = dlopen("libpackage-add-on-libsolv_build.so", flags);
#endif
	if (imageHandle == NULL)
		return;

	sCreateSolver = (CreateSolverFunction*)dlsym(imageHandle, "create_solver");
	if (sCreateSolver == NULL)
		dlclose(imageHandle);
}


namespace BPackageKit {


/**
 * @brief Default-construct a BSolver.
 */
BSolver::BSolver()
{
}


/**
 * @brief Destroy a BSolver.
 */
BSolver::~BSolver()
{
}


/**
 * @brief Create and initialise a BSolver instance from the loaded add-on.
 *
 * Loads the solver add-on on the first call, instantiates a solver via the
 * add-on's create_solver() entry point, and calls Init() on the result.
 *
 * @param _solver  Output pointer set to the new BSolver on success.
 * @return B_OK on success, B_NOT_SUPPORTED if the add-on could not be loaded,
 *         B_NO_MEMORY if allocation fails, or an error from Init().
 */
/*static*/ status_t
BSolver::Create(BSolver*& _solver)
{
	pthread_once(&sLoadLibsolvSolverAddOnInitOnce, &load_libsolv_solver_add_on);
	if (sCreateSolver == NULL)
		return B_NOT_SUPPORTED;

	BSolver* solver = sCreateSolver();
	if (solver == NULL)
		return B_NO_MEMORY;

	status_t error = solver->Init();
	if (error != B_OK) {
		delete solver;
		return error;
	}

	_solver = solver;
	return B_OK;
}


}	// namespace BPackageKit
