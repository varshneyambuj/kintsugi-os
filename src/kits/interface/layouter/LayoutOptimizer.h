/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
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
 * MIT License. Copyright 2007, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 */

/** @file LayoutOptimizer.h
    @brief Quadratic-programming solver used by ComplexLayouter to find a
           feasible size assignment that satisfies linear inequality
           constraints between layout variables. */

#ifndef LAYOUT_OPTIMIZER_H
#define LAYOUT_OPTIMIZER_H

#include <List.h>
#include <math.h>

/** @brief Tolerance used when comparing doubles for equality in the solver. */
static const double kEqualsEpsilon = 0.000001;


namespace BPrivate {
namespace Layout {

/**
 * @brief Constrained quadratic-programming solver for layout sizing.
 *
 * Holds a set of linear inequality (and equality) constraints between integer
 * variables and a Solve() routine that finds the variable assignment closest
 * to a desired vector while satisfying all constraints. Used as a fallback
 * inside ComplexLayouter when greedy assignment cannot honour multi-element
 * constraints.
 */
class LayoutOptimizer {
public:
								LayoutOptimizer(int32 variableCount);
								~LayoutOptimizer();

			status_t			InitCheck() const;

			LayoutOptimizer*	Clone() const;

			bool				AddConstraint(int32 left, int32 right,
									double value, bool equality);
			bool				AddConstraintsFrom(
									const LayoutOptimizer* solver);
			void				RemoveAllConstraints();

			bool				Solve(const double* desired, double size,
									double* values);

private:
			bool				_Solve(const double* desired, double* values);
			bool				_SolveSubProblem(const double* d, int am,
									double* p);
			void				_SetResult(const double* x, double* values);


			struct Constraint;

			int32				fVariableCount;
			BList				fConstraints;
			double*				fVariables;
			double**			fTemp1;
			double**			fTemp2;
			double**			fZtrans;
			double**			fQ;
			double**			fActiveMatrix;
			double**			fActiveMatrixTemp;
};

}	// namespace Layout
}	// namespace BPrivate

using BPrivate::Layout::LayoutOptimizer;


/**
 * @brief Returns whether two doubles are within @c kEqualsEpsilon of each other.
 *
 * @param a First value.
 * @param b Second value.
 * @return @c true when @a a and @a b differ by at most @c kEqualsEpsilon.
 */
inline bool
fuzzy_equals(double a, double b)
{
	return fabs(a - b) < kEqualsEpsilon;
}


#endif	// LAYOUT_OPTIMIZER_H
