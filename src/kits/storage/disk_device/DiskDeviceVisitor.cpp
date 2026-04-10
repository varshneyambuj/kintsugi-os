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
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file DiskDeviceVisitor.cpp
 * @brief Implementation of the BDiskDeviceVisitor base class.
 *
 * BDiskDeviceVisitor provides the base interface for the visitor pattern used
 * when iterating over BDiskDevice and BPartition objects. Derived classes
 * override the Visit() methods to perform custom operations on each visited
 * node. The return value of Visit() controls whether iteration continues.
 *
 * @see BDiskDevice
 * @see BPartition
 */

#include <DiskDeviceVisitor.h>

/**
 * @brief Base class of visitors used for BDiskDevice and BPartition iteration.
 *
 * BDiskDeviceRoster and BDiskDevice provide iteration methods that work
 * together with an instance of a derived class of BDiskDeviceVisitor. For
 * each encountered BDiskDevice and BPartition the respective Visit() method
 * is invoked. The return value of that method specifies whether the iteration
 * shall be terminated at that point.
 */

/**
 * @brief Creates a new disk device visitor.
 */
BDiskDeviceVisitor::BDiskDeviceVisitor()
{
}

/**
 * @brief Frees all resources associated with this object.
 *
 * Does nothing.
 */
BDiskDeviceVisitor::~BDiskDeviceVisitor()
{
}

/**
 * @brief Invoked when a BDiskDevice is visited.
 *
 * If the method returns \c true, the iteration is terminated at this point,
 * on \c false continued. Overridden by derived classes.
 * This class' version does nothing and returns \c false.
 *
 * @param device The visited disk device.
 * @return \c true if the iteration shall be terminated at this point,
 *         \c false otherwise.
 */
bool
BDiskDeviceVisitor::Visit(BDiskDevice *device)
{
	return false;
}

/**
 * @brief Invoked when a BPartition is visited.
 *
 * If the method returns \c true, the iteration is terminated at this point,
 * on \c false continued. Overridden by derived classes.
 * This class' version does nothing and returns \c false.
 *
 * @param partition The visited partition.
 * @param level The level of the partition in the partition tree.
 * @return \c true if the iteration shall be terminated at this point,
 *         \c false otherwise.
 */
bool
BDiskDeviceVisitor::Visit(BPartition *partition, int32 level)
{
	return false;
}
