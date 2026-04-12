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
 * @file LibsolvSolver.cpp
 * @brief Concrete BSolver implementation backed by the libsolv dependency-resolution library.
 *
 * LibsolvSolver translates between the Kintsugi package-kit API (BSolverRepository,
 * BSolverPackage, BSolverPackageSpecifierList, etc.) and the libsolv Pool/Solver
 * data structures. It maintains a mapping between libsolv solvable IDs and
 * BSolverPackage pointers, reconstructs BSolverProblem / BSolverProblemSolution
 * objects from the solver's conflict data, and produces BSolverResult objects
 * describing the ordered install/uninstall transaction.
 *
 * @see BSolver, BSolverRepository, BPackageManager
 */


#include "LibsolvSolver.h"

#include <errno.h>
#include <sys/utsname.h>

#include <new>

#include <solv/policy.h>
#include <solv/poolarch.h>
#include <solv/repo.h>
#include <solv/repo_haiku.h>
#include <solv/selection.h>
#include <solv/solverdebug.h>

#include <package/PackageResolvableExpression.h>
#include <package/RepositoryCache.h>
#include <package/solver/SolverPackage.h>
#include <package/solver/SolverPackageSpecifier.h>
#include <package/solver/SolverPackageSpecifierList.h>
#include <package/solver/SolverProblem.h>
#include <package/solver/SolverRepository.h>
#include <package/solver/SolverResult.h>

#include <AutoDeleter.h>
#include <ObjectList.h>


// TODO: libsolv doesn't have any helpful out-of-memory handling. It just just
// abort()s. Obviously that isn't good behavior for a library.


/**
 * @brief Add-on entry point: allocate and return a new LibsolvSolver.
 *
 * Called by BSolver::Create() after loading this add-on via dlopen().
 *
 * @return A new LibsolvSolver on success, or NULL on allocation failure.
 */
BSolver*
BPackageKit::create_solver()
{
	return new(std::nothrow) LibsolvSolver;
}


struct LibsolvSolver::SolvQueue : Queue {
	SolvQueue()
	{
		queue_init(this);
	}

	~SolvQueue()
	{
		queue_free(this);
	}
};


struct LibsolvSolver::SolvDataIterator : Dataiterator {
	SolvDataIterator(Pool* pool, Repo* repo, Id solvableId, Id keyname,
		const char* match, int flags)
	{
		dataiterator_init(this, pool, repo, solvableId, keyname, match, flags);
	}

	~SolvDataIterator()
	{
		dataiterator_free(this);
	}
};


struct LibsolvSolver::RepositoryInfo {
	RepositoryInfo(BSolverRepository* repository)
		:
		fRepository(repository),
		fSolvRepo(NULL),
		fChangeCount(repository->ChangeCount())
	{
	}

	BSolverRepository* Repository() const
	{
		return fRepository;
	}

	Repo* SolvRepo()
	{
		return fSolvRepo;
	}

	void SetSolvRepo(Repo* repo)
	{
		fSolvRepo = repo;
	}

	bool HasChanged() const
	{
		return fChangeCount != fRepository->ChangeCount() || fSolvRepo == NULL;
	}

	void SetUnchanged()
	{
		fChangeCount = fRepository->ChangeCount();
	}

private:
	BSolverRepository*	fRepository;
	Repo*				fSolvRepo;
	uint64				fChangeCount;
};


struct LibsolvSolver::Problem : public BSolverProblem {
	Problem(::Id id, BType type, BSolverPackage* sourcePackage,
		BSolverPackage* targetPackage,
		const BPackageResolvableExpression& dependency)
		:
		BSolverProblem(type, sourcePackage, targetPackage, dependency),
		fId(id),
		fSelectedSolution(NULL)
	{
	}

	::Id Id() const
	{
		return fId;
	}

	const Solution* SelectedSolution() const
	{
		return fSelectedSolution;
	}

	void SetSelectedSolution(const Solution* solution)
	{
		fSelectedSolution = solution;
	}

private:
	::Id			fId;
	const Solution*	fSelectedSolution;
};


struct LibsolvSolver::Solution : public BSolverProblemSolution {
	Solution(::Id id, LibsolvSolver::Problem* problem)
		:
		BSolverProblemSolution(),
		fId(id),
		fProblem(problem)
	{
	}

	::Id Id() const
	{
		return fId;
	}

	LibsolvSolver::Problem* Problem() const
	{
		return fProblem;
	}

private:
	::Id					fId;
	LibsolvSolver::Problem*	fProblem;
};


// #pragma mark - LibsolvSolver


/**
 * @brief Default-construct a LibsolvSolver with a NULL pool and solver.
 *
 * All libsolv resources are allocated lazily by Init() and _InitPool().
 */
LibsolvSolver::LibsolvSolver()
	:
	fPool(NULL),
	fSolver(NULL),
	fJobs(NULL),
	fRepositoryInfos(10),
	fInstalledRepository(NULL),
	fSolvablePackages(),
	fPackageSolvables(),
	fProblems(10),
	fDebugLevel(0)
{
}


/**
 * @brief Destroy the solver and release all libsolv resources.
 */
LibsolvSolver::~LibsolvSolver()
{
	_Cleanup();
}


/**
 * @brief Initialise the solver, cleaning up any previous state.
 *
 * All libsolv objects are allocated lazily on first use; this method only
 * performs a _Cleanup() to reset existing state.
 *
 * @return B_OK always.
 */
status_t
LibsolvSolver::Init()
{
	_Cleanup();

	// We do all initialization lazily.
	return B_OK;
}


/**
 * @brief Set the libsolv debug verbosity level.
 *
 * @param level  Debug level passed to pool_setdebuglevel(); 0 disables output.
 */
void
LibsolvSolver::SetDebugLevel(int32 level)
{
	fDebugLevel = level;

	if (fPool != NULL)
		pool_setdebuglevel(fPool, fDebugLevel);
}


/**
 * @brief Register a BSolverRepository with this solver.
 *
 * Only one installed repository may be registered; a second call with an
 * installed repository returns B_BAD_VALUE.
 *
 * @param repository  The repository to register; must be initialised.
 * @return B_OK on success, B_BAD_VALUE if @a repository is NULL, uninitialised,
 *         or a duplicate installed repository, or B_NO_MEMORY on failure.
 */
status_t
LibsolvSolver::AddRepository(BSolverRepository* repository)
{
	if (repository == NULL || repository->InitCheck() != B_OK)
		return B_BAD_VALUE;

	// If the repository represents installed packages, check, if we already
	// have such a repository.
	if (repository->IsInstalled() && _InstalledRepository() != NULL)
		return B_BAD_VALUE;

	// add the repository info
	RepositoryInfo* info = new(std::nothrow) RepositoryInfo(repository);
	if (info == NULL)
		return B_NO_MEMORY;

	if (!fRepositoryInfos.AddItem(info)) {
		delete info;
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Search all repositories for packages matching a string.
 *
 * Searches the fields selected by @a flags (name, summary, description,
 * provides, requires) using a substring match.
 *
 * @param searchString  The substring to search for.
 * @param flags         Bitmask of B_FIND_IN_NAME, B_FIND_IN_SUMMARY, etc.
 * @param _packages     Output list populated with matching BSolverPackage pointers.
 * @return B_OK on success, or an error code on failure.
 */
status_t
LibsolvSolver::FindPackages(const char* searchString, uint32 flags,
	BObjectList<BSolverPackage>& _packages)
{
	// add repositories to pool
	status_t error = _AddRepositories();
	if (error != B_OK)
		return error;

	// create data iterator
	int iteratorFlags = SEARCH_SUBSTRING;
	if ((flags & B_FIND_CASE_INSENSITIVE) != 0)
		iteratorFlags |= SEARCH_NOCASE;

	SolvDataIterator iterator(fPool, 0, 0, 0, searchString, iteratorFlags);
	SolvQueue selection;

	// search package names
	if ((flags & B_FIND_IN_NAME) != 0) {
		dataiterator_set_keyname(&iterator, SOLVABLE_NAME);
		dataiterator_set_search(&iterator, 0, 0);

		while (dataiterator_step(&iterator))
			queue_push2(&selection, SOLVER_SOLVABLE, iterator.solvid);
	}

	// search package summaries
	if ((flags & B_FIND_IN_SUMMARY) != 0) {
		dataiterator_set_keyname(&iterator, SOLVABLE_SUMMARY);
		dataiterator_set_search(&iterator, 0, 0);

		while (dataiterator_step(&iterator))
			queue_push2(&selection, SOLVER_SOLVABLE, iterator.solvid);
	}

	// search package description
	if ((flags & B_FIND_IN_DESCRIPTION) != 0) {
		dataiterator_set_keyname(&iterator, SOLVABLE_DESCRIPTION);
		dataiterator_set_search(&iterator, 0, 0);

		while (dataiterator_step(&iterator))
			queue_push2(&selection, SOLVER_SOLVABLE, iterator.solvid);
	}

	// search package provides
	if ((flags & B_FIND_IN_PROVIDES) != 0) {
		dataiterator_set_keyname(&iterator, SOLVABLE_PROVIDES);
		dataiterator_set_search(&iterator, 0, 0);

		while (dataiterator_step(&iterator))
			queue_push2(&selection, SOLVER_SOLVABLE, iterator.solvid);
	}

	// search package requires
	if ((flags & B_FIND_IN_REQUIRES) != 0) {
		dataiterator_set_keyname(&iterator, SOLVABLE_REQUIRES);
		dataiterator_set_search(&iterator, 0, 0);

		while (dataiterator_step(&iterator))
			queue_push2(&selection, SOLVER_SOLVABLE, iterator.solvid);
	}

	return _GetFoundPackages(selection, flags, _packages);
}


/**
 * @brief Search repositories for packages matching a specifier list.
 *
 * @param packages     Specifiers identifying the packages to find.
 * @param flags        Search flags (B_FIND_INSTALLED_ONLY, etc.).
 * @param _packages    Output list populated with matching packages.
 * @param _unmatched   Optional output set to the first unmatched specifier.
 * @return B_OK on success, B_NAME_NOT_FOUND if a specifier matched nothing,
 *         or another error code on failure.
 */
status_t
LibsolvSolver::FindPackages(const BSolverPackageSpecifierList& packages,
	uint32 flags, BObjectList<BSolverPackage>& _packages,
	const BSolverPackageSpecifier** _unmatched)
{
	if (_unmatched != NULL)
		*_unmatched = NULL;

	if ((flags & B_FIND_INSTALLED_ONLY) != 0 && _InstalledRepository() == NULL)
		return B_BAD_VALUE;

	// add repositories to pool
	status_t error = _AddRepositories();
	if (error != B_OK)
		return error;

	error = _InitJobQueue();
	if (error != B_OK)
		return error;

	// add the package specifies to the job queue
	error = _AddSpecifiedPackages(packages, _unmatched,
		(flags & B_FIND_INSTALLED_ONLY) != 0 ? SELECTION_INSTALLED_ONLY : 0);
	if (error != B_OK)
		return error;

	return _GetFoundPackages(*fJobs, flags, _packages);
}


/**
 * @brief Solve for the packages that must be installed to satisfy the specifier list.
 *
 * @param packages    Specifiers identifying packages to install.
 * @param _unmatched  Optional output set to the first unmatched specifier.
 * @return B_OK on success, B_BAD_VALUE if the list is empty, or another error.
 */
status_t
LibsolvSolver::Install(const BSolverPackageSpecifierList& packages,
	const BSolverPackageSpecifier** _unmatched)
{
	if (_unmatched != NULL)
		*_unmatched = NULL;

	if (packages.IsEmpty())
		return B_BAD_VALUE;

	// add repositories to pool
	status_t error = _AddRepositories();
	if (error != B_OK)
		return error;

	// add the packages to install to the job queue
	error = _InitJobQueue();
	if (error != B_OK)
		return error;

	error = _AddSpecifiedPackages(packages, _unmatched, 0);
	if (error != B_OK)
		return error;

	// set jobs' solver mode and solve
	_SetJobsSolverMode(SOLVER_INSTALL);

	_InitSolver();
	return _Solve();
}


/**
 * @brief Solve for the packages that must be removed to uninstall the specifier list.
 *
 * @param packages    Specifiers identifying packages to uninstall.
 * @param _unmatched  Optional output set to the first unmatched specifier.
 * @return B_OK on success, B_BAD_VALUE if the list is empty or no installed
 *         repository is registered, or another error.
 */
status_t
LibsolvSolver::Uninstall(const BSolverPackageSpecifierList& packages,
	const BSolverPackageSpecifier** _unmatched)
{
	if (_unmatched != NULL)
		*_unmatched = NULL;

	if (_InstalledRepository() == NULL || packages.IsEmpty())
		return B_BAD_VALUE;

	// add repositories to pool
	status_t error = _AddRepositories();
	if (error != B_OK)
		return error;

	// add the packages to uninstall to the job queue
	error = _InitJobQueue();
	if (error != B_OK)
		return error;

	error = _AddSpecifiedPackages(packages, _unmatched,
		SELECTION_INSTALLED_ONLY);
	if (error != B_OK)
		return error;

	// set jobs' solver mode and solve
	_SetJobsSolverMode(SOLVER_ERASE);

	_InitSolver();
	solver_set_flag(fSolver, SOLVER_FLAG_ALLOW_UNINSTALL, 1);
	return _Solve();
}


/**
 * @brief Solve for the updates needed for the given package specifiers.
 *
 * When @a packages is empty all installed packages are updated. When
 * @a installNotYetInstalled is true, packages not yet installed are installed
 * instead of skipped.
 *
 * @param packages               Specifiers for packages to update; may be empty.
 * @param installNotYetInstalled If true, uninstalled packages are installed.
 * @param _unmatched             Optional output set to the first unmatched specifier.
 * @return B_OK on success, or an error code on failure.
 */
status_t
LibsolvSolver::Update(const BSolverPackageSpecifierList& packages,
	bool installNotYetInstalled, const BSolverPackageSpecifier** _unmatched)
{
	if (_unmatched != NULL)
		*_unmatched = NULL;

	// add repositories to pool
	status_t error = _AddRepositories();
	if (error != B_OK)
		return error;

	// add the packages to update to the job queue -- if none are specified,
	// update all
	error = _InitJobQueue();
	if (error != B_OK)
		return error;

	if (packages.IsEmpty()) {
		queue_push2(fJobs, SOLVER_SOLVABLE_ALL, 0);
	} else {
		error = _AddSpecifiedPackages(packages, _unmatched, 0);
		if (error != B_OK)
			return error;
	}

	// set jobs' solver mode and solve
	_SetJobsSolverMode(SOLVER_UPDATE);

	if (installNotYetInstalled) {
		for (int i = 0; i < fJobs->count; i += 2) {
			// change solver mode to SOLVER_INSTALL for empty update jobs
			if (pool_isemptyupdatejob(fPool, fJobs->elements[i],
					fJobs->elements[i + 1])) {
				fJobs->elements[i] &= ~SOLVER_JOBMASK;
				fJobs->elements[i] |= SOLVER_INSTALL;
			}
		}
	}

	_InitSolver();
	return _Solve();
}


/**
 * @brief Solve for a full distribution-upgrade of all installed packages.
 *
 * @return B_OK on success, or an error code if the solve fails.
 */
status_t
LibsolvSolver::FullSync()
{
	// add repositories to pool
	status_t error = _AddRepositories();
	if (error != B_OK)
		return error;

	// Init the job queue and specify that all packages shall be updated.
	error = _InitJobQueue();
	if (error != B_OK)
		return error;

	queue_push2(fJobs, SOLVER_SOLVABLE_ALL, 0);

	// set jobs' solver mode and solve
	_SetJobsSolverMode(SOLVER_DISTUPGRADE);

	_InitSolver();
	return _Solve();
}


/**
 * @brief Verify that the installed repository is self-consistent.
 *
 * @param flags  Verification flags; B_VERIFY_ALLOW_UNINSTALL permits removing
 *               packages to resolve conflicts.
 * @return B_OK on success, B_BAD_VALUE if no installed repository is registered.
 */
status_t
LibsolvSolver::VerifyInstallation(uint32 flags)
{
	if (_InstalledRepository() == NULL)
		return B_BAD_VALUE;

	// add repositories to pool
	status_t error = _AddRepositories();
	if (error != B_OK)
		return error;

	// add the verify job to the job queue
	error = _InitJobQueue();
	if (error != B_OK)
		return error;

	queue_push2(fJobs, SOLVER_SOLVABLE_ALL, 0);

	// set jobs' solver mode and solve
	_SetJobsSolverMode(SOLVER_VERIFY);

	_InitSolver();
	if ((flags & B_VERIFY_ALLOW_UNINSTALL) != 0)
		solver_set_flag(fSolver, SOLVER_FLAG_ALLOW_UNINSTALL, 1);
	return _Solve();
}


/**
 * @brief Select (or clear) a solution for a solver problem.
 *
 * The selected solution is applied during the next SolveAgain() call.
 *
 * @param _problem   The problem to resolve; must be non-NULL.
 * @param _solution  The solution to apply, or NULL to clear any previous selection.
 * @return B_OK on success, B_BAD_VALUE if @a _problem is NULL or the solution
 *         does not belong to @a _problem.
 */
status_t
LibsolvSolver::SelectProblemSolution(BSolverProblem* _problem,
	const BSolverProblemSolution* _solution)
{
	if (_problem == NULL)
		return B_BAD_VALUE;

	Problem* problem = static_cast<Problem*>(_problem);
	if (_solution == NULL) {
		problem->SetSelectedSolution(NULL);
		return B_OK;
	}

	const Solution* solution = static_cast<const Solution*>(_solution);
	if (solution->Problem() != problem)
		return B_BAD_VALUE;

	problem->SetSelectedSolution(solution);
	return B_OK;
}


/**
 * @brief Re-run the solver after applying selected problem solutions.
 *
 * Iterates over all outstanding problems, applies the selected solution for
 * each via solver_take_solution(), then calls _Solve() again.
 *
 * @return B_OK on success, B_BAD_VALUE if the solver or job queue is NULL.
 */
status_t
LibsolvSolver::SolveAgain()
{
	if (fSolver == NULL || fJobs == NULL)
		return B_BAD_VALUE;

	// iterate through all problems and propagate the selected solutions
	int32 problemCount = fProblems.CountItems();
	for (int32 i = 0; i < problemCount; i++) {
		Problem* problem = fProblems.ItemAt(i);
		if (const Solution* solution = problem->SelectedSolution())
			solver_take_solution(fSolver, problem->Id(), solution->Id(), fJobs);
	}

	return _Solve();
}


/**
 * @brief Return the number of unresolved problems after the last solve.
 *
 * @return Count of BSolverProblem objects in the problem list.
 */
int32
LibsolvSolver::CountProblems() const
{
	return fProblems.CountItems();
}


/**
 * @brief Return the problem at the given index.
 *
 * @param index  Zero-based index of the problem.
 * @return Pointer to the BSolverProblem, or NULL if out of range.
 */
BSolverProblem*
LibsolvSolver::ProblemAt(int32 index) const
{
	return fProblems.ItemAt(index);
}


/**
 * @brief Translate the solver transaction into a BSolverResult.
 *
 * Enumerates each step of the ordered transaction and appends a
 * B_TYPE_INSTALL or B_TYPE_UNINSTALL element for each affected package.
 *
 * @param _result  The result object to populate; cleared before use.
 * @return B_OK on success, B_BAD_VALUE if the solver is NULL or has outstanding
 *         problems, B_ERROR if a solvable cannot be mapped to a package.
 */
status_t
LibsolvSolver::GetResult(BSolverResult& _result)
{
	if (fSolver == NULL || HasProblems())
		return B_BAD_VALUE;

	_result.MakeEmpty();

	Transaction* transaction = solver_create_transaction(fSolver);
	CObjectDeleter<Transaction, void, transaction_free>
		transactionDeleter(transaction);

	if (transaction->steps.count == 0)
		return B_OK;

	transaction_order(transaction, 0);

	for (int i = 0; i < transaction->steps.count; i++) {
		Id solvableId = transaction->steps.elements[i];
		if (fPool->installed
			&& fPool->solvables[solvableId].repo == fPool->installed) {
			BSolverPackage* package = _GetPackage(solvableId);
			if (package == NULL)
				return B_ERROR;

			if (!_result.AppendElement(
					BSolverResultElement(
						BSolverResultElement::B_TYPE_UNINSTALL, package))) {
				return B_NO_MEMORY;
			}
		} else {
			BSolverPackage* package = _GetPackage(solvableId);
			if (package == NULL)
				return B_ERROR;

			if (!_result.AppendElement(
					BSolverResultElement(
						BSolverResultElement::B_TYPE_INSTALL, package))) {
				return B_NO_MEMORY;
			}
		}
	}

	return B_OK;
}


/**
 * @brief (Re-)create the libsolv Pool with the correct architecture policy.
 *
 * Detects the system architecture via uname() or compile-time macros and
 * applies it to the pool via pool_setarchpolicy().
 *
 * @return B_OK on success, or errno if uname() fails.
 */
status_t
LibsolvSolver::_InitPool()
{
	_CleanupPool();

	fPool = pool_create();

	pool_setdebuglevel(fPool, fDebugLevel);

	// Set the system architecture. We use what uname() returns unless we're on
	// x86 gcc2.
	{
		const char* arch;
		#ifdef HAIKU_TARGET_PLATFORM_HAIKU
			#ifdef __HAIKU_ARCH_X86
				#if (B_HAIKU_ABI & B_HAIKU_ABI_MAJOR) == B_HAIKU_ABI_GCC_2
					arch = "x86_gcc2";
				#else
					arch = "x86";
				#endif
			#else
				struct utsname info;
				if (uname(&info) != 0)
					return errno;
				arch = info.machine;
			#endif
		#else
			arch = HAIKU_PACKAGING_ARCH;
		#endif

		pool_setarchpolicy(fPool, arch);
	}

	return B_OK;
}


/**
 * @brief Allocate a fresh SolvQueue for the solver job list.
 *
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
LibsolvSolver::_InitJobQueue()
{
	_CleanupJobQueue();

	fJobs = new(std::nothrow) SolvQueue;
	return fJobs != NULL ? B_OK : B_NO_MEMORY;;
}


/**
 * @brief Create the libsolv Solver from the current pool with standard flags.
 *
 * Enables SOLVER_FLAG_SPLITPROVIDES and SOLVER_FLAG_BEST_OBEY_POLICY.
 */
void
LibsolvSolver::_InitSolver()
{
	_CleanupSolver();

	fSolver = solver_create(fPool);
	solver_set_flag(fSolver, SOLVER_FLAG_SPLITPROVIDES, 1);
	solver_set_flag(fSolver, SOLVER_FLAG_BEST_OBEY_POLICY, 1);
}


/**
 * @brief Release all libsolv resources and reset solver state.
 */
void
LibsolvSolver::_Cleanup()
{
	_CleanupPool();

	fInstalledRepository = NULL;
	fRepositoryInfos.MakeEmpty();

}


/**
 * @brief Free the libsolv Pool and all data structures that reference it.
 */
void
LibsolvSolver::_CleanupPool()
{
	// clean up jobs and solver data
	_CleanupJobQueue();

	// clean up our data structures that depend on/refer to libsolv pool data
	fSolvablePackages.clear();
	fPackageSolvables.clear();

	int32 repositoryCount = fRepositoryInfos.CountItems();
	for (int32 i = 0; i < repositoryCount; i++)
		fRepositoryInfos.ItemAt(i)->SetSolvRepo(NULL);

	// delete the pool
	if (fPool != NULL) {
		pool_free(fPool);
		fPool = NULL;
	}
}


/**
 * @brief Free the job queue (and implicitly the solver that depends on it).
 */
void
LibsolvSolver::_CleanupJobQueue()
{
	_CleanupSolver();

	delete fJobs;
	fJobs = NULL;
}


/**
 * @brief Free the libsolv Solver and clear the problem list.
 */
void
LibsolvSolver::_CleanupSolver()
{
	fProblems.MakeEmpty();

	if (fSolver != NULL) {
		solver_free(fSolver);
		fSolver = NULL;
	}
}


/**
 * @brief Check whether any registered repository has changed since the pool was built.
 *
 * @return True if at least one RepositoryInfo reports HasChanged().
 */
bool
LibsolvSolver::_HaveRepositoriesChanged() const
{
	int32 repositoryCount = fRepositoryInfos.CountItems();
	for (int32 i = 0; i < repositoryCount; i++) {
		RepositoryInfo* repositoryInfo = fRepositoryInfos.ItemAt(i);
		if (repositoryInfo->HasChanged())
			return true;
	}

	return false;
}


/**
 * @brief Rebuild the libsolv pool from registered repositories if necessary.
 *
 * Skips rebuilding when the pool already exists and no repository has changed.
 * Adds each repository's packages as HPKG solvables and sets the installed
 * repository on the pool.
 *
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
LibsolvSolver::_AddRepositories()
{
	if (fPool != NULL && !_HaveRepositoriesChanged())
		return B_OK;

	// something has changed -- re-create the pool
	status_t error = _InitPool();
	if (error != B_OK)
		return error;

	fInstalledRepository = NULL;

	int32 repositoryCount = fRepositoryInfos.CountItems();
	for (int32 i = 0; i < repositoryCount; i++) {
		RepositoryInfo* repositoryInfo = fRepositoryInfos.ItemAt(i);
		BSolverRepository* repository = repositoryInfo->Repository();
		Repo* repo = repo_create(fPool, repository->Name());
		repositoryInfo->SetSolvRepo(repo);

		repo->priority = -1 - repository->Priority();
		repo->appdata = (void*)repositoryInfo;

		int32 packageCount = repository->CountPackages();
		for (int32 k = 0; k < packageCount; k++) {
			BSolverPackage* package = repository->PackageAt(k);
			Id solvableId = repo_add_haiku_package_info(repo, package->Info(),
				REPO_REUSE_REPODATA | REPO_NO_INTERNALIZE);

			try {
				fSolvablePackages[solvableId] = package;
				fPackageSolvables[package] = solvableId;
			} catch (std::bad_alloc&) {
				return B_NO_MEMORY;
			}
		}

		repo_internalize(repo);

		if (repository->IsInstalled()) {
			fInstalledRepository = repositoryInfo;
			pool_set_installed(fPool, repo);
		}

		repositoryInfo->SetUnchanged();
	}

	// create "provides" lookup
	pool_createwhatprovides(fPool);

	return B_OK;
}


/**
 * @brief Return the RepositoryInfo for the registered installed repository.
 *
 * @return Pointer to the installed RepositoryInfo, or NULL if none is registered.
 */
LibsolvSolver::RepositoryInfo*
LibsolvSolver::_InstalledRepository() const
{
	int32 repositoryCount = fRepositoryInfos.CountItems();
	for (int32 i = 0; i < repositoryCount; i++) {
		RepositoryInfo* repositoryInfo = fRepositoryInfos.ItemAt(i);
		if (repositoryInfo->Repository()->IsInstalled())
			return repositoryInfo;
	}

	return NULL;
}


/**
 * @brief Look up the RepositoryInfo for the given BSolverRepository.
 *
 * @param repository  The repository to look up.
 * @return Pointer to the matching RepositoryInfo, or NULL if not found.
 */
LibsolvSolver::RepositoryInfo*
LibsolvSolver::_GetRepositoryInfo(BSolverRepository* repository) const
{
	int32 repositoryCount = fRepositoryInfos.CountItems();
	for (int32 i = 0; i < repositoryCount; i++) {
		RepositoryInfo* repositoryInfo = fRepositoryInfos.ItemAt(i);
		if (repository == repositoryInfo->Repository())
			return repositoryInfo;
	}

	return NULL;
}


/**
 * @brief Map a libsolv solvable ID to its BSolverPackage.
 *
 * @param solvableId  The libsolv solvable ID to look up.
 * @return Pointer to the matching BSolverPackage, or NULL if not found.
 */
BSolverPackage*
LibsolvSolver::_GetPackage(Id solvableId) const
{
	SolvableMap::const_iterator it = fSolvablePackages.find(solvableId);
	return it != fSolvablePackages.end() ? it->second : NULL;
}


/**
 * @brief Map a BSolverPackage to its libsolv solvable ID.
 *
 * @param package  The package to look up.
 * @return The solvable ID, or 0 if not found.
 */
Id
LibsolvSolver::_GetSolvable(BSolverPackage* package) const
{
	PackageMap::const_iterator it = fPackageSolvables.find(package);
	return it != fPackageSolvables.end() ? it->second : 0;
}


/**
 * @brief Translate a specifier list into libsolv job-queue entries.
 *
 * Direct-package specifiers are pushed as SOLVER_SOLVABLE entries; select-string
 * specifiers are resolved via selection_make() and pushed as selection entries.
 *
 * @param packages         The specifier list to process.
 * @param _unmatched       Optional output set to the first unmatched specifier.
 * @param additionalFlags  Extra selection flags (e.g. SELECTION_INSTALLED_ONLY).
 * @return B_OK on success, B_BAD_VALUE for invalid specifiers, B_NAME_NOT_FOUND
 *         if a select-string matches nothing.
 */
status_t
LibsolvSolver::_AddSpecifiedPackages(
	const BSolverPackageSpecifierList& packages,
	const BSolverPackageSpecifier** _unmatched, int additionalFlags)
{
	int32 packageCount = packages.CountSpecifiers();
	for (int32 i = 0; i < packageCount; i++) {
		const BSolverPackageSpecifier& specifier = *packages.SpecifierAt(i);
		switch (specifier.Type()) {
			case BSolverPackageSpecifier::B_UNSPECIFIED:
				return B_BAD_VALUE;

			case BSolverPackageSpecifier::B_PACKAGE:
			{
				BSolverPackage* package = specifier.Package();
				Id solvableId;
				if (package == NULL
					|| (solvableId = _GetSolvable(package)) == 0) {
					return B_BAD_VALUE;
				}

				queue_push2(fJobs, SOLVER_SOLVABLE, solvableId);
				break;
			}

			case BSolverPackageSpecifier::B_SELECT_STRING:
			{
				// find matching packages
				SolvQueue matchingPackages;

				int flags = SELECTION_NAME | SELECTION_PROVIDES | SELECTION_GLOB
					| SELECTION_CANON | SELECTION_DOTARCH | SELECTION_REL
					| additionalFlags;
				/*int matchFlags =*/ selection_make(fPool, &matchingPackages,
					specifier.SelectString().String(), flags);
				if (matchingPackages.count == 0) {
					if (_unmatched != NULL)
						*_unmatched = &specifier;
					return B_NAME_NOT_FOUND;
				}
// TODO: We might want to add support for restricting to certain repositories.
#if 0
				// restrict to the matching repository
				if (BSolverRepository* repository = specifier.Repository()) {
					RepositoryInfo* repositoryInfo
						= _GetRepositoryInfo(repository);
					if (repositoryInfo == NULL)
						return B_BAD_VALUE;

					SolvQueue repoFilter;
					queue_push2(&repoFilter,
						SOLVER_SOLVABLE_REPO
							/* | SOLVER_SETREPO | SOLVER_SETVENDOR*/,
						repositoryInfo->SolvRepo()->repoid);

					selection_filter(fPool, &matchingPackages, &repoFilter);

					if (matchingPackages.count == 0)
						return B_NAME_NOT_FOUND;
				}
#endif

				for (int j = 0; j < matchingPackages.count; j++)
					queue_push(fJobs, matchingPackages.elements[j]);
			}
		}
	}

	return B_OK;
}


/**
 * @brief Construct a BSolverProblem from a libsolv problem ID and add it to fProblems.
 *
 * Queries the solver rule info, maps IDs to BSolverPackage objects, and
 * enumerates solutions.
 *
 * @param problemId  The libsolv problem ID to analyse.
 * @return B_OK on success, B_ERROR if a solvable cannot be mapped, or B_NO_MEMORY.
 */
status_t
LibsolvSolver::_AddProblem(Id problemId)
{
	enum {
		NEED_SOURCE		= 0x1,
		NEED_TARGET		= 0x2,
		NEED_DEPENDENCY	= 0x4
	};

	Id ruleId = solver_findproblemrule(fSolver, problemId);
	Id sourceId;
	Id targetId;
	Id dependencyId;
	BSolverProblem::BType problemType = BSolverProblem::B_UNSPECIFIED;
	uint32 needed = 0;

	switch (solver_ruleinfo(fSolver, ruleId, &sourceId, &targetId,
			&dependencyId)) {
		case SOLVER_RULE_DISTUPGRADE:
			problemType = BSolverProblem::B_NOT_IN_DISTUPGRADE_REPOSITORY;
			needed = NEED_SOURCE;
			break;
		case SOLVER_RULE_INFARCH:
			problemType = BSolverProblem::B_INFERIOR_ARCHITECTURE;
			needed = NEED_SOURCE;
			break;
		case SOLVER_RULE_UPDATE:
			problemType = BSolverProblem::B_INSTALLED_PACKAGE_PROBLEM;
			needed = NEED_SOURCE;
			break;
		case SOLVER_RULE_JOB:
			problemType = BSolverProblem::B_CONFLICTING_REQUESTS;
			break;
		case SOLVER_RULE_JOB_NOTHING_PROVIDES_DEP:
			problemType = BSolverProblem::B_REQUESTED_RESOLVABLE_NOT_PROVIDED;
			needed = NEED_DEPENDENCY;
			break;
		case SOLVER_RULE_JOB_PROVIDED_BY_SYSTEM:
			problemType
				= BSolverProblem::B_REQUESTED_RESOLVABLE_PROVIDED_BY_SYSTEM;
			needed = NEED_DEPENDENCY;
			break;
		case SOLVER_RULE_RPM:
			problemType = BSolverProblem::B_DEPENDENCY_PROBLEM;
			break;
		case SOLVER_RULE_RPM_NOT_INSTALLABLE:
			problemType = BSolverProblem::B_PACKAGE_NOT_INSTALLABLE;
			needed = NEED_SOURCE;
			break;
		case SOLVER_RULE_RPM_NOTHING_PROVIDES_DEP:
			problemType = BSolverProblem::B_DEPENDENCY_NOT_PROVIDED;
			needed = NEED_SOURCE | NEED_DEPENDENCY;
			break;
		case SOLVER_RULE_RPM_SAME_NAME:
			problemType = BSolverProblem::B_PACKAGE_NAME_CLASH;
			needed = NEED_SOURCE | NEED_TARGET;
			break;
		case SOLVER_RULE_RPM_PACKAGE_CONFLICT:
			problemType = BSolverProblem::B_PACKAGE_CONFLICT;
			needed = NEED_SOURCE | NEED_TARGET | NEED_DEPENDENCY;
			break;
		case SOLVER_RULE_RPM_PACKAGE_OBSOLETES:
			problemType = BSolverProblem::B_PACKAGE_OBSOLETES_RESOLVABLE;
			needed = NEED_SOURCE | NEED_TARGET | NEED_DEPENDENCY;
			break;
		case SOLVER_RULE_RPM_INSTALLEDPKG_OBSOLETES:
			problemType
				= BSolverProblem::B_INSTALLED_PACKAGE_OBSOLETES_RESOLVABLE;
			needed = NEED_SOURCE | NEED_TARGET | NEED_DEPENDENCY;
			break;
		case SOLVER_RULE_RPM_IMPLICIT_OBSOLETES:
			problemType
				= BSolverProblem::B_PACKAGE_IMPLICITLY_OBSOLETES_RESOLVABLE;
			needed = NEED_SOURCE | NEED_TARGET | NEED_DEPENDENCY;
			break;
		case SOLVER_RULE_RPM_PACKAGE_REQUIRES:
			problemType = BSolverProblem::B_DEPENDENCY_NOT_INSTALLABLE;
			needed = NEED_SOURCE | NEED_DEPENDENCY;
			break;
		case SOLVER_RULE_RPM_SELF_CONFLICT:
			problemType = BSolverProblem::B_SELF_CONFLICT;
			needed = NEED_SOURCE | NEED_DEPENDENCY;
			break;
		case SOLVER_RULE_UNKNOWN:
		case SOLVER_RULE_FEATURE:
		case SOLVER_RULE_LEARNT:
		case SOLVER_RULE_CHOICE:
		case SOLVER_RULE_BEST:
			problemType = BSolverProblem::B_UNSPECIFIED;
			break;
	}

	BSolverPackage* sourcePackage = NULL;
	if ((needed & NEED_SOURCE) != 0) {
		sourcePackage = _GetPackage(sourceId);
		if (sourcePackage == NULL)
			return B_ERROR;
	}

	BSolverPackage* targetPackage = NULL;
	if ((needed & NEED_TARGET) != 0) {
		targetPackage = _GetPackage(targetId);
		if (targetPackage == NULL)
			return B_ERROR;
	}

	BPackageResolvableExpression dependency;
	if ((needed & NEED_DEPENDENCY) != 0) {
		status_t error = _GetResolvableExpression(dependencyId, dependency);
		if (error != B_OK)
			return error;
	}

	Problem* problem = new(std::nothrow) Problem(problemId, problemType,
		sourcePackage, targetPackage, dependency);
	if (problem == NULL || !fProblems.AddItem(problem)) {
		delete problem;
		return B_NO_MEMORY;
	}

	int solutionCount = solver_solution_count(fSolver, problemId);
	for (Id solutionId = 1; solutionId <= solutionCount; solutionId++) {
		status_t error = _AddSolution(problem, solutionId);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Build a Solution from a libsolv solution ID and attach it to @a problem.
 *
 * @param problem     The BSolverProblem that this solution resolves.
 * @param solutionId  The libsolv solution ID to enumerate elements for.
 * @return B_OK on success, or B_NO_MEMORY on allocation failure.
 */
status_t
LibsolvSolver::_AddSolution(Problem* problem, Id solutionId)
{
	Solution* solution = new(std::nothrow) Solution(solutionId, problem);
	if (solution == NULL || !problem->AppendSolution(solution)) {
		delete solution;
		return B_NO_MEMORY;
	}

	Id elementId = 0;
	for (;;) {
		Id sourceId;
		Id targetId;
		elementId = solver_next_solutionelement(fSolver, problem->Id(),
			solutionId, elementId, &sourceId, &targetId);
		if (elementId == 0)
			break;

		status_t error = _AddSolutionElement(solution, sourceId, targetId);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Translate a libsolv solution element pair into a BSolverProblemSolutionElement.
 *
 * Dispatches on the sourceId sentinel values (SOLVER_SOLUTION_JOB,
 * SOLVER_SOLUTION_INFARCH, etc.) and the policy illegal-mask to determine the
 * element type, then delegates to the typed overload.
 *
 * @param solution  The solution to append the element to.
 * @param sourceId  The libsolv source solvable ID or sentinel.
 * @param targetId  The libsolv target solvable ID.
 * @return B_OK on success, or an error code on failure.
 */
status_t
LibsolvSolver::_AddSolutionElement(Solution* solution, Id sourceId, Id targetId)
{
	typedef BSolverProblemSolutionElement Element;

	if (sourceId == SOLVER_SOLUTION_JOB
		|| sourceId == SOLVER_SOLUTION_POOLJOB) {
		// targetId is an index into the job queue
		if (sourceId == SOLVER_SOLUTION_JOB)
			targetId += fSolver->pooljobcnt;

		Id how = fSolver->job.elements[targetId - 1];
		Id what = fSolver->job.elements[targetId];
		Id select = how & SOLVER_SELECTMASK;

		switch (how & SOLVER_JOBMASK) {
			case SOLVER_INSTALL:
				if (select == SOLVER_SOLVABLE && fInstalledRepository != NULL
					&& fPool->solvables[what].repo
						== fInstalledRepository->SolvRepo()) {
					return _AddSolutionElement(solution, Element::B_DONT_KEEP,
						what, 0, NULL);
				}

				return _AddSolutionElement(solution,
					Element::B_DONT_INSTALL, 0, 0,
					solver_select2str(fPool, select, what));

			case SOLVER_ERASE:
			{
				if (select == SOLVER_SOLVABLE
					&& (fInstalledRepository == NULL
						|| fPool->solvables[what].repo
							!= fInstalledRepository->SolvRepo())) {
					return _AddSolutionElement(solution,
						Element::B_DONT_FORBID_INSTALLATION, what, 0, NULL);
				}

				Element::BType type = select == SOLVER_SOLVABLE_PROVIDES
					? Element::B_DONT_DEINSTALL_ALL : Element::B_DONT_DEINSTALL;
				return _AddSolutionElement(solution, type, 0, 0,
					solver_select2str(fPool, select, what));
			}

			case SOLVER_UPDATE:
				return _AddSolutionElement(solution,
					Element::B_DONT_INSTALL_MOST_RECENT, 0, 0,
					solver_select2str(fPool, select, what));

			case SOLVER_LOCK:
				return _AddSolutionElement(solution, Element::B_DONT_LOCK, 0, 0,
					solver_select2str(fPool, select, what));

			default:
				return _AddSolutionElement(solution, Element::B_UNSPECIFIED, 0,
					0, NULL);
		}
	}

	Solvable* target = targetId != 0 ? fPool->solvables + targetId : NULL;
	bool targetInstalled = target != NULL && fInstalledRepository
		&& target->repo == fInstalledRepository->SolvRepo();

	if (sourceId == SOLVER_SOLUTION_INFARCH) {
		return _AddSolutionElement(solution,
			targetInstalled
				? Element::B_KEEP_INFERIOR_ARCHITECTURE
				: Element::B_INSTALL_INFERIOR_ARCHITECTURE,
			targetId, 0, NULL);
	}

	if (sourceId == SOLVER_SOLUTION_DISTUPGRADE) {
		return _AddSolutionElement(solution,
			targetInstalled
				? Element::B_KEEP_EXCLUDED : Element::B_INSTALL_EXCLUDED,
			targetId, 0, NULL);
	}

	if (sourceId == SOLVER_SOLUTION_BEST) {
		return _AddSolutionElement(solution,
			targetInstalled ? Element::B_KEEP_OLD : Element::B_INSTALL_OLD,
			targetId, 0, NULL);
	}

	// replace source with target
	Solvable* source = fPool->solvables + sourceId;
	if (target == NULL) {
		return _AddSolutionElement(solution, Element::B_ALLOW_DEINSTALLATION,
			sourceId, 0, NULL);
	}

	int illegalMask = policy_is_illegal(fSolver, source, target, 0);
	if ((illegalMask & POLICY_ILLEGAL_DOWNGRADE) != 0) {
		status_t error = _AddSolutionElement(solution,
			Element::B_ALLOW_DOWNGRADE, sourceId, targetId, NULL);
		if (error != B_OK)
			return error;
	}

	if ((illegalMask & POLICY_ILLEGAL_NAMECHANGE) != 0) {
		status_t error = _AddSolutionElement(solution,
			Element::B_ALLOW_NAME_CHANGE, sourceId, targetId, NULL);
		if (error != B_OK)
			return error;
	}

	if ((illegalMask & POLICY_ILLEGAL_ARCHCHANGE) != 0) {
		status_t error = _AddSolutionElement(solution,
			Element::B_ALLOW_ARCHITECTURE_CHANGE, sourceId, targetId, NULL);
		if (error != B_OK)
			return error;
	}

	if ((illegalMask & POLICY_ILLEGAL_VENDORCHANGE) != 0) {
		status_t error = _AddSolutionElement(solution,
			Element::B_ALLOW_VENDOR_CHANGE, sourceId, targetId, NULL);
		if (error != B_OK)
			return error;
	}

	if (illegalMask == 0) {
		return _AddSolutionElement(solution, Element::B_ALLOW_REPLACEMENT,
			sourceId, targetId, NULL);
	}

	return B_OK;
}


/**
 * @brief Append a typed BSolverProblemSolutionElement to a Solution.
 *
 * Maps solvable IDs to BSolverPackage pointers and copies the optional
 * selection string, then calls solution->AppendElement().
 *
 * @param solution          The solution to append the element to.
 * @param type              The element type.
 * @param sourceSolvableId  Source solvable ID, or 0 if not applicable.
 * @param targetSolvableId  Target solvable ID, or 0 if not applicable.
 * @param selectionString   Optional selection string; may be NULL.
 * @return B_OK on success, B_ERROR if a solvable cannot be mapped, or B_NO_MEMORY.
 */
status_t
LibsolvSolver::_AddSolutionElement(Solution* solution,
	BSolverProblemSolutionElement::BType type, Id sourceSolvableId,
	Id targetSolvableId, const char* selectionString)
{
	BSolverPackage* sourcePackage = NULL;
	if (sourceSolvableId != 0) {
		sourcePackage = _GetPackage(sourceSolvableId);
		if (sourcePackage == NULL)
			return B_ERROR;
	}

	BSolverPackage* targetPackage = NULL;
	if (targetSolvableId != 0) {
		targetPackage = _GetPackage(targetSolvableId);
		if (targetPackage == NULL)
			return B_ERROR;
	}

	BString selection;
	if (selectionString != NULL && selectionString[0] != '\0') {
		selection = selectionString;
		if (selection.IsEmpty())
			return B_NO_MEMORY;
	}

	if (!solution->AppendElement(BSolverProblemSolutionElement(
			type, sourcePackage, targetPackage, selection))) {
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Translate a libsolv dependency ID back into a BPackageResolvableExpression.
 *
 * Simple (non-composite) IDs map directly to a name string. Composite Reldep
 * IDs are translated to a name + operator + version expression. IDs that
 * cannot be represented in the Kintsugi expression model return B_NOT_SUPPORTED.
 *
 * @param id           The libsolv dependency ID to translate.
 * @param _expression  Output expression filled on success.
 * @return B_OK on success, B_NOT_SUPPORTED for unsupported ID shapes, or an
 *         error from BPackageVersion::SetTo().
 */
status_t
LibsolvSolver::_GetResolvableExpression(Id id,
	BPackageResolvableExpression& _expression) const
{
	// Try to translate the libsolv ID to a resolvable expression. Generally
	// that doesn't work, since libsolv is more expressive, but all the stuff
	// we feed libsolv we should be able to translate back.

	if (!ISRELDEP(id)) {
		// just a string
		_expression.SetTo(pool_id2str(fPool, id));
		return B_OK;
	}

	// a composite -- analyze it
	Reldep* reldep = GETRELDEP(fPool, id);

	// No support for more than one level, so both name and evr must be strings.
	if (ISRELDEP(reldep->name) || ISRELDEP(reldep->evr))
		return B_NOT_SUPPORTED;

	const char* name = pool_id2str(fPool, reldep->name);
	const char* versionString = pool_id2str(fPool, reldep->evr);
	if (name == NULL || versionString == NULL)
		return B_NOT_SUPPORTED;

	// get the operator -- we don't support all libsolv supports
	BPackageResolvableOperator op;
	switch (reldep->flags) {
		case 1:
			op = B_PACKAGE_RESOLVABLE_OP_GREATER;
			break;
		case 2:
			op = B_PACKAGE_RESOLVABLE_OP_EQUAL;
			break;
		case 3:
			op = B_PACKAGE_RESOLVABLE_OP_GREATER_EQUAL;
			break;
		case 4:
			op = B_PACKAGE_RESOLVABLE_OP_LESS;
			break;
		case 5:
			op = B_PACKAGE_RESOLVABLE_OP_NOT_EQUAL;
			break;
		case 6:
			op = B_PACKAGE_RESOLVABLE_OP_LESS_EQUAL;
			break;
		default:
			return B_NOT_SUPPORTED;
	}

	// get the version (cut off the empty epoch)
	if (versionString[0] == ':')
		versionString++;

	BPackageVersion version;
	status_t error = version.SetTo(versionString, true);
	if (error != B_OK)
		return error == B_BAD_DATA ? B_NOT_SUPPORTED : error;

	_expression.SetTo(name, op, version);
	return B_OK;
}


/**
 * @brief Convert a solvable selection queue into a BSolverPackage list.
 *
 * Expands the selection into individual solvables and maps each one to a
 * BSolverPackage. When B_FIND_INSTALLED_ONLY is set, only packages in the
 * installed repository are returned.
 *
 * @param selection  The SolvQueue containing the selection to expand.
 * @param flags      Search flags forwarded from the caller.
 * @param _packages  Output list populated with matching packages.
 * @return B_OK on success, B_ERROR if a solvable cannot be mapped, or B_NO_MEMORY.
 */
status_t
LibsolvSolver::_GetFoundPackages(SolvQueue& selection, uint32 flags,
	BObjectList<BSolverPackage>& _packages)
{
	// get solvables
	SolvQueue solvables;
	selection_solvables(fPool, &selection, &solvables);

	// get packages
	for (int i = 0; i < solvables.count; i++) {
		BSolverPackage* package = _GetPackage(solvables.elements[i]);
		if (package == NULL)
			return B_ERROR;

		// TODO: Fix handling of SELECTION_INSTALLED_ONLY in libsolv. Despite
		// passing the flag, we get solvables that aren't installed.
		if ((flags & B_FIND_INSTALLED_ONLY) != 0
			&& package->Repository() != fInstalledRepository->Repository()) {
			continue;
		}

		if (!_packages.AddItem(package))
			return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Run the libsolv solver against the current job queue.
 *
 * Calls solver_solve(), then converts each problem to a BSolverProblem and
 * populates fProblems.
 *
 * @return B_OK on success (problems, if any, are stored in fProblems), or
 *         an error code if problem construction fails.
 */
status_t
LibsolvSolver::_Solve()
{
	if (fJobs == NULL || fSolver == NULL)
		return B_BAD_VALUE;

	int problemCount = solver_solve(fSolver, fJobs);

	// get the problems (if any)
	fProblems.MakeEmpty();

	for (Id problemId = 1; problemId <= problemCount; problemId++) {
		status_t error = _AddProblem(problemId);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Apply a solver mode flag to every entry in the job queue.
 *
 * ORs @a solverMode into the how-word of each job pair (SOLVER_INSTALL,
 * SOLVER_ERASE, SOLVER_UPDATE, etc.).
 *
 * @param solverMode  The libsolv solver mode constant to apply.
 */
void
LibsolvSolver::_SetJobsSolverMode(int solverMode)
{
	for (int i = 0; i < fJobs->count; i += 2)
		fJobs->elements[i] |= solverMode;
}
