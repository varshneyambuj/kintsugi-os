# Kintsugi OS — Coding Convention (Comments & File Headers)

This document captures the commenting, attribution, and Doxygen conventions
used in the Kintsugi OS source tree. It is derived from the reference
quality already in place in:

- `src/kits/interface/` (e.g. `Font.cpp`, `Button.cpp`)
- `src/kits/bluetooth/` (e.g. `CommandManager.cpp`, `LocalDevice.cpp`)
- `src/servers/app/` (e.g. `AppServer.cpp`, `Desktop.cpp`, `Desktop.h`)

When in doubt, match the local convention of the file you are editing
rather than rewriting the surrounding code to a different style.


## 1. License Header (top of every source file)

Every `.cpp`, `.c`, and `.h` file begins with an Apache 2.0 license header,
immediately followed (after a blank line) by a Doxygen `@file` block, then
the includes.

### 1.1 Canonical author line

- Use `Ambuj Varshney, ambuj@kintsugi-os.org`.
- Do **not** use the older `varshney@ambuj.se`. When you touch a file that
  still carries it, update it in the same edit.

### 1.2 Banner variants

Two banner forms coexist in the tree. Both are valid; pick the one that
matches the directory's local convention:

**Form A — "Kintsugi OS Project" (used in most `.cpp` files in `kits/` and
`servers/app/`):**

```cpp
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
 *   Copyright 2001-2015, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Stephan Aßmus <superstippi@gmx.de>
 */
```

**Form B — "Kintsugi OS Contributors" (used in many `.h` files in
`servers/app/`):**

```cpp
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
 * MIT License. Copyright 2001-2020, Haiku.
 * Original authors: Adrian Oanca, Stephan Aßmus, Axel Dörfler, ...
 */
```

Differences between the two forms:

| Aspect           | Form A (Project)          | Form B (Contributors)    |
| ---------------- | ------------------------- | ------------------------ |
| Copyright line   | `Kintsugi OS Project`     | `Kintsugi OS Contributors` |
| Author block     | `Authors:` (plural list)  | `Author:` (single line)  |
| Upstream block   | "incorporates work covered by the following copyright and permission notice:" then verbatim original notice indented two spaces | Compact "Incorporates work from..." paragraph |

### 1.3 Preserving upstream attribution

When a file derives from upstream (Haiku, NewOS, MIT/BSD/ISC sources):

- Keep the original copyright lines and license terms verbatim.
- In Form A, place the upstream notice indented two spaces inside the
  "incorporates work covered by..." block, including the original Authors list.
- Never delete the upstream notice. Updating it for clarity is fine; rewriting
  it to drop attribution is not.


## 2. File-level Doxygen Block

Immediately after the license header (with one blank line separating them),
every source file gets an `@file` block. Two equivalent forms are in use:

**Multi-line (preferred for non-trivial files):**

```cpp
/**
 * @file Font.cpp
 * @brief Implementation of BFont, the font description and metrics class
 *
 * BFont encapsulates a font family, style, and size, along with rendering
 * attributes such as shear, rotation, and spacing. It communicates with the
 * app_server to query font metrics and available font families.
 *
 * @see BView, BString
 */
```

**Compact (acceptable for simple files):**

```cpp
/** @file AppServer.cpp
    @brief Entry point and top-level manager for the Kintsugi OS application server. */
```

Rules:
- The first line after `@file` is `@brief` — one short sentence stating
  what the file implements.
- Optional longer paragraph explaining intent, scope, or major collaborators.
- Optional `@see` listing one or two closely related types.
- Do **not** restate what the includes already make obvious.


## 3. Per-Function Doxygen

Every non-trivial free function, member function, and static helper gets a
Doxygen block immediately above its definition. The minimum is `@brief`;
add tags as the function warrants.

### 3.1 Standard tag set and order

```cpp
/**
 * @brief One-line summary in imperative or descriptive voice.
 *
 * Optional longer description: what the function does, why, important
 * preconditions, locking, ownership, threading. Keep to a few sentences.
 *
 * @param name  Description of each parameter, aligned for readability.
 * @param other Description.
 * @return     Plain-language description of the return value.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When @a name is out of range.
 * @note  Caller frees the returned buffer with free().
 * @see   RelatedFunction()
 * @todo  Replace heuristic with a real lookup.
 */
```

Tag order in practice:
`@brief` → optional body → `@param`s in declaration order → `@return` →
`@retval`s → `@note` / `@warning` → `@see` → `@todo` / `@deprecated`.

### 3.2 When to use which tag

- **`@brief`** — required. One sentence, ends with a period.
- **`@param`** — one per parameter, in declaration order. Use the parameter
  name verbatim. State whether output parameters may be `NULL`.
- **`@return`** — required for non-`void` functions. Describe the meaning,
  not just the type. For status codes, use `@retval` for each documented
  value.
- **`@retval`** — preferred when a function returns one of a small set of
  discrete codes (e.g. `B_OK`, `B_BAD_VALUE`, `B_NAME_NOT_FOUND`).
- **`@note`** — invariants, ownership, threading, or surprising behavior
  that a caller must respect. Examples: "Caller must free with free()",
  "No bounds checking is performed", "The face value is heuristic".
- **`@warning`** — destructive or footgun behavior.
- **`@see`** — one or two closely related symbols. Don't list everything.
- **`@todo`** — known limitations worth tracking. Inline `// TODO:` is also
  acceptable for code-line-specific notes.
- **`@deprecated`** — mark legacy APIs that survive for binary compat.

### 3.3 Referring to parameters in prose

Both Doxygen forms are accepted; pick one and stay consistent within a file.

- `kits/interface/` style: `@a paramName`
- `kits/bluetooth/` style: `\a paramName`

Example (bluetooth style):
```
 * Allocates a contiguous memory block large enough to hold the
 * hci_command_header followed by \a psize bytes of parameter data.
```

### 3.4 What the `@brief` should *not* be

- Do not paraphrase the function signature ("Returns the count of
  families."). State intent: "Returns the number of font families in the
  cached list."
- Do not reference the current PR, ticket, or session. That belongs in the
  commit message.


## 4. Variable, Constant, and Member Doxygen

For globals, file-scope constants, and members whose purpose is not obvious
from their name, use a single-line `@brief`:

```cpp
/** @brief Sentinel value stored in fHeight.ascent when the cached height is invalid. */
const float kUninitializedAscent = INFINITY;

/** @brief Pointer to the system plain font; equivalent to B_PLAIN_FONT. */
const BFont* be_plain_font = &sPlainFont;
```

For grouped enum values, place a one-line `@brief` above each value (or
above the `enum` and rely on the name when values are self-explanatory):

```cpp
/** @brief Bit flag indicating this button is the window's default button. */
/** @brief Bit flag indicating flat (borderless) rendering style. */
enum {
    FLAG_DEFAULT = 0x01,
    FLAG_FLAT    = 0x02,
};
```


## 5. Section Markers

Section markers separate logical regions of a `.cpp` file. Both forms used
in the tree are preserved:

```cpp
//	#pragma mark -
```

and

```cpp
#if 0
#pragma mark - LINK CONTROL -
#endif
```

Keep them when present; do not invent new ones unless splitting a long file
genuinely helps navigation.


## 6. Inline Comments

- Default to writing no inline comment when the code is self-explanatory.
- Use `// TODO:` and `// FIXME:` for known issues. Keep them short and
  specific. Don't restate what `@todo` already covers in the function block.
- A short inline comment before a non-obvious block is acceptable when
  it explains *why*, not *what*. Examples seen in the tree:
  `// Resize to minimum height if needed`, `// Check if the mode has
  actually changed`. These explain intent the code alone does not show.


## 7. Style Quick Reference

- Indent inside `/** ... */` blocks with ` * ` (space-star-space).
- One blank line between the license header and the `@file` block.
- One blank line between the `@file` block and the first `#include`.
- One blank line between consecutive function definitions and their
  Doxygen blocks above the next function.
- Keep lines under ~80 characters where practical; existing files wrap
  Doxygen prose at column 80.
- No trailing whitespace.


## 8. Coverage Expectations

A file is considered "deep" when:
- Its license header is present, current, and uses the canonical author
  line (`ambuj@kintsugi-os.org`).
- An `@file` block follows the header.
- Every function (free, member, static helper) has at minimum a `@brief`,
  with `@param`/`@return`/`@retval` added wherever they apply.
- File-scope constants and globals worth documenting carry a `@brief`.

Reference-quality examples to consult:
- `src/kits/interface/Font.cpp`
- `src/kits/bluetooth/CommandManager.cpp`
- `src/servers/app/Desktop.cpp`

When sweeping a file, run `grep -c '@brief' <file>` for a coarse signal —
≥ 10 typically indicates deep coverage on a normally sized implementation
file; 1–9 usually means only the file banner is annotated.
