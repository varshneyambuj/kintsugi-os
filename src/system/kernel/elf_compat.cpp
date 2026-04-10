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

/** @file elf_compat.cpp
 *  @brief 32-bit ELF loader built by re-including elf.cpp under renamed symbols. */


//#define TRACE_ELF
#define ELF32_COMPAT 1


#include <elf.h>


#define elf_load_user_image elf32_load_user_image
#define elf_resolve_symbol elf32_resolve_symbol
#define elf_find_symbol elf32_find_symbol
#define elf_parse_dynamic_section elf32_parse_dynamic_section
#define elf_relocate elf32_relocate

#define arch_elf_relocate_rel arch_elf32_relocate_rel
#define arch_elf_relocate_rela arch_elf32_relocate_rela


#include "elf.cpp"
