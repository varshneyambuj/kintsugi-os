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
 *   Copyright 2018, Jérôme Duval, jerome.duval@gmail.com.
 *   Distributed under the terms of the MIT License.
 */

/** @file commpage_compat.cpp
 *  @brief 32-bit compatibility commpage built by re-including commpage.cpp under
 *         renamed symbols. Provides the user-visible commpage to legacy 32-bit
 *         processes running on a 64-bit kernel. */


#define sCommPageArea sCommPageCompatArea
#define sCommPageAddress sCommPageCompatAddress
#define sFreeCommPageSpace sFreeCommPageCompatSpace
#define sCommPageImage sCommPageCompatImage

#define allocate_commpage_entry allocate_commpage_compat_entry
#define fill_commpage_entry fill_commpage_compat_entry
#define get_commpage_image get_commpage_compat_image
#define clone_commpage_area clone_commpage_compat_area
#define commpage_init commpage_compat_init
#define commpage_init_post_cpus commpage_compat_init_post_cpus
#define arch_commpage_init arch_commpage_compat_init
#define arch_commpage_init_post_cpus arch_commpage_compat_init_post_cpus

#define ADDRESS_TYPE uint32
#define COMMPAGE_COMPAT

#include "commpage.cpp"
