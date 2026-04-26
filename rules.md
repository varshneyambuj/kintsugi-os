# KintsugiOS Compatibility Rules

This document defines what may and may not change in KintsugiOS without breaking Haiku R1 / BeOS R5 application compatibility. **Read this before touching any file under `headers/os/`, `src/system/libbe/`, `src/system/libroot/`, `src/kits/`, or anything that defines a wire/file format.**

The single principle: **the userland ABI is sacred. Everything below the syscall interface is fair game.** That is what lets KintsugiOS diverge aggressively for sub-1W edge while still running existing Haiku/BeOS apps.

---

## 1. Sacred — do not change without explicit approval

These are the load-bearing surfaces that third-party apps depend on. Breaking any one of them silently breaks an unknown number of apps in the wild.

### 1.1 Public headers and exported symbols
- **`headers/os/**`** — every header here is part of the public API. Class layouts (member order, size, vtable), function signatures, enum values, `#define` constants, and macro semantics are frozen.
- **Exported symbols of `libbe.so`, `libroot.so`, `libnetwork.so`, `libtranslation.so`, `liblocale.so`, `libtracker.so`** — names, mangling, and behavior. Adding new symbols is fine; renaming, removing, or changing the contract of an existing one is not.
- **Symbol versioning** — if a symbol must change behavior, add a new versioned symbol, leave the old one with original semantics.

### 1.2 Wire formats
- **BMessage flat format** (`BMessage::Flatten` / `Unflatten`) — apps send these across the network and store them on disk.
- **`.rdef` / `.rsrc` resource format** — apps ship resources compiled into binaries.
- **hpkg package format** — installed apps live in these; format changes break the package set.
- **MIME database on-disk layout** — registered types/handlers/attributes.

### 1.3 Filesystem semantics
- **BFS attributes**: name, type, size limits, indexing semantics. Apps store data here.
- **Live queries**: query syntax and notification semantics.
- **Node monitoring**: `watch_node` semantics.
- **POSIX behavior** required by libroot — anything an app could reasonably observe via `stat`, `open`, `read`, `mmap`, etc.

### 1.4 IPC semantics
- **Port API** — `create_port` / `read_port` / `write_port` blocking and timeout semantics.
- **BMessenger / BLooper / BHandler** — message dispatch order, reply matching, scripting protocol.
- **BApplication signature → team lookup** — `be_roster` semantics.
- **Standard messages** (`B_QUIT_REQUESTED`, `B_ABOUT_REQUESTED`, etc., from `AppDefs.h`) — names and meaning.

### 1.5 App-visible kernel constants and ABIs
- **Thread priorities** (`B_LOW_PRIORITY` … `B_REAL_TIME_PRIORITY`) — apps pass these to `spawn_thread`. Internal scheduler may treat them however it likes, but the *constants* and their relative ordering must keep meaning.
- **Area protection flags** (`B_READ_AREA`, `B_WRITE_AREA`, etc.).
- **Semaphore semantics**.
- **`team_id`, `thread_id`, `port_id`, `area_id`, `sem_id` types** — must remain `int32`-compatible.

### 1.6 Globals that apps reach for
- `be_app`, `be_app_messenger`, `be_roster`, `be_clipboard`, `system_clipboard` — must exist with the same types and lifecycle.
- `B_TRANSLATE`, `B_TRANSLATE_CONTEXT`, etc. — locale macros.

---

## 2. Free to change — kernel internals, not visible above the syscall layer

These can be redesigned aggressively for edge constraints (sub-1W, tickless idle, minimal RAM):

- Scheduler implementation (the *constants* are sacred; the *behavior* of how priorities map to CPU time is not, as long as the relative ordering still feels right to apps).
- VM internals: page replacement, swap policy, slab allocator, area placement.
- File cache: caching policy, write-back behavior.
- VFS internal layout (as long as the syscall surface stays POSIX-compliant + BeOS extensions).
- Driver framework internals: how drivers are loaded, the bus manager design, IRQ routing.
- Network stack internals: socket buffer sizing, queue disciplines, congestion control.
- Boot loader and early init (the boot *protocol* between loader and kernel is internal).
- Power management — no existing app contract here, design from scratch.
- `headers/private/**` — by definition not stable. Change freely.
- `src/system/kernel/**` not exposed via syscall.
- Anything in `srck/` (shelved code, not built).
- Build system (`build/jam/**`).

---

## 3. Free to add — new functionality alongside the existing surface

- New syscalls: add them, never reuse old numbers.
- New BMessage `what` codes: pick a Kintsugi-namespaced range to avoid collisions.
- New libraries (`libkintsugi.so`, etc.) — apps that opt in get new features; apps that don't, don't notice.
- New BMessenger transports (e.g. tikuos-over-serial) — must be a new constructor or new class (`BPeerMessenger`), not a behavior change to existing constructors.
- New optional fields in BMessage — existing readers must ignore unknown fields gracefully (they already do).

---

## 4. Process rules for agents working in this repo

1. **Before editing anything under `headers/os/`** — state in your reply *which apps you've checked won't break*, or pause and ask.
2. **Before editing anything under `src/system/libbe/`, `src/system/libroot/`, `src/kits/`** — diff the exported symbol list before/after the change. If any symbol is removed, renamed, or changes signature, stop and ask.
3. **Before changing a syscall number or signature** — check that the libroot stub stays backward-compatible (old apps with the old stub baked in still work) or the syscall is genuinely new.
4. **Before changing BFS, MIME, hpkg, or BMessage wire formats** — stop and ask. These have on-disk and on-wire footprints that survive across reinstalls.
5. **Before deleting files in the "sacred" tree** — apply the cleanup-sweep check from `feedback_cleanup_sweep_check_refs.md` (grep build/jam + src for refs).
6. **Kernel internals are fair game** but: changes that affect timing, scheduling fairness, or memory layout can still break apps that depend on observable behavior. If a change might affect what an app *sees*, treat it as ABI.
7. **If in doubt, ask.** The cost of pausing is low; the cost of an ABI break that ships and gets installed is high.

---

## 5. How to verify compatibility for a change

In rough order of cost:

1. **Header diff**: `git diff headers/os/` — should be empty for any change that claims to be ABI-preserving. If non-empty, justify.
2. **Symbol diff**: build before and after, then
   ```
   nm -D --defined-only generated.x86_64/objects/haiku/x86_64/release/system/lib/libbe.so | sort > /tmp/libbe.after
   diff /tmp/libbe.before /tmp/libbe.after
   ```
   Same for `libroot.so` and any other public library. New symbols OK; removed/renamed symbols not OK.
3. **Build a real BeOS R5 / Haiku app** against the new headers — even just compiling a known third-party app surfaces ABI changes the diff missed.
4. **Boot the iso, run the app, click around** — type-checking and symbol diffs prove ABI shape, not behavior. A scheduler change that makes BLooper drop messages is invisible to a symbol diff.

---

## 6. The "in doubt, ask" rule

This file is necessarily incomplete — there are corners of the BeOS/Haiku ABI that aren't documented anywhere outside the source. If you are about to change something and you are not certain whether it falls under §1 (sacred) or §2 (free), the answer is §1 by default. Pause and ask the user before proceeding.

The cost of one extra clarification round is a few minutes. The cost of shipping a silent ABI break is every Haiku app on the system, which is the whole reason KintsugiOS chose Haiku as its base.
