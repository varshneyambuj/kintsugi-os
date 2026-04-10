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
 *   Copyright 2008, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file fifo.h
 *  @brief VFS hooks that wrap a vnode in named-pipe (FIFO) semantics. */

#ifndef _VFS_FIFO_H
#define _VFS_FIFO_H

#include <fs_interface.h>


/** @brief Wraps an FS vnode so its read/write paths implement FIFO semantics.
 *  @param superVolume The volume that owns the vnode.
 *  @param vnode       The vnode to convert into a FIFO endpoint.
 *  @return B_OK on success, or an error code on failure. */
status_t	create_fifo_vnode(fs_volume* superVolume, fs_vnode* vnode);
/** @brief Initialises the FIFO subsystem during VFS bring-up. */
void		fifo_init();


#endif	// _VFS_FIFO_H
