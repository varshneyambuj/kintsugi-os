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
 */

/**
 * @file KPartitionVisitor.cpp
 * @brief Abstract visitor interface for traversing the kernel partition tree.
 *
 * KPartitionVisitor is the base class for tree-walks over the KPartition
 * hierarchy that the disk device manager maintains (a root disk with nested
 * partitions/sub-partitions). A subclass overrides VisitPre() and/or
 * VisitPost() to act on each node during a depth-first traversal. Returning
 * true from either hook stops the walk early and propagates that verdict back
 * up through the partition tree's traversal driver.
 *
 * The default implementations here simply return false so that a subclass may
 * override only the callback it cares about and let the other one no-op.
 */

#include "KPartitionVisitor.h"
#include <util/kernel_cpp.h>


/**
 * @brief Construct a no-op visitor.
 *
 * Provided so derived classes have a well-defined base constructor; the base
 * class itself carries no state.
 */
KPartitionVisitor::KPartitionVisitor()
{
}


/**
 * @brief Virtual destructor for polymorphic visitors.
 *
 * Allows derived visitors to be destroyed through a KPartitionVisitor* without
 * slicing. The base class owns no resources.
 */
KPartitionVisitor::~KPartitionVisitor()
{
}


/**
 * @brief Pre-order callback invoked before descending into @a partition's
 *        children.
 *
 * Subclasses override this to inspect a partition on the way down the tree.
 * The default implementation performs no work and allows traversal to
 * continue.
 *
 * @param partition The partition node the walker is about to descend into.
 * @return false to continue traversal; true to stop the walk immediately.
 */
bool
KPartitionVisitor::VisitPre(KPartition *partition)
{
	return false;
}


/**
 * @brief Post-order callback invoked after all of @a partition's children have
 *        been visited.
 *
 * Subclasses override this to act on a partition on the way back up the tree,
 * which is useful for aggregating information gathered from sub-partitions.
 * The default implementation performs no work and allows traversal to
 * continue.
 *
 * @param partition The partition node whose subtree has just been visited.
 * @return false to continue traversal; true to stop the walk immediately.
 */
bool
KPartitionVisitor::VisitPost(KPartition *partition)
{
	return false;
}
