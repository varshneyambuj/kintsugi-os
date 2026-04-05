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
 *     Ambuj Varshney, varshney@ambuj.se
 */

/**
 * @file KPartitionVisitor.cpp
 * @brief Abstract visitor interface for traversing the kernel partition tree.
 *
 * Provides default implementations for the visitor callbacks used when
 * walking the kernel partition hierarchy. VisitPre() and VisitPost() return
 * false by default, meaning traversal continues without early termination.
 */

#include "KPartitionVisitor.h"
#include <util/kernel_cpp.h>

/** @brief Default constructor. */
KPartitionVisitor::KPartitionVisitor()
{
}

/** @brief Destructor. */
KPartitionVisitor::~KPartitionVisitor()
{
}

/**
 * @brief Called before visiting the children of @a partition (pre-order).
 * @param partition The partition being visited.
 * @return false to continue traversal; true to stop early.
 */
bool
KPartitionVisitor::VisitPre(KPartition *partition)
{
	return false;
}

/**
 * @brief Called after visiting the children of @a partition (post-order).
 * @param partition The partition being visited.
 * @return false to continue traversal; true to stop early.
 */
bool
KPartitionVisitor::VisitPost(KPartition *partition)
{
	return false;
}
