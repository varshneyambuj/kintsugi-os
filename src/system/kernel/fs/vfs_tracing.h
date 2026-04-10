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
 *   Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2010, Michael Lotz, mmlr@mlotz.ch.
 *   Distributed under the terms of the MIT License.
 */

/** @file vfs_tracing.h
 *  @brief Tracing entry classes for file-descriptor and io_context lifecycle events.
 *
 * The classes in the FileDescriptorTracing and IOContextTracing namespaces
 * are TraceEntry subclasses produced by the kernel tracing macros @c TFD()
 * and @c TIOC(). They capture allocation, retention, duplication, removal,
 * and inheritance events for file descriptors and per-team I/O contexts so
 * that the kernel debugger can replay descriptor history when debugging
 * lifetime bugs. */

#ifndef VFS_TRACING_H
#define VFS_TRACING_H


#include <fs/fd.h>
#include <tracing.h>
#include <vfs.h>


// #pragma mark - File Descriptor Tracing


#if FILE_DESCRIPTOR_TRACING

namespace FileDescriptorTracing {


/** @brief Base class capturing the descriptor and reference count for any FD trace entry. */
class FDTraceEntry
	: public TRACE_ENTRY_SELECTOR(FILE_DESCRIPTOR_TRACING_STACK_TRACE) {
public:
	FDTraceEntry(file_descriptor* descriptor)
		:
		TraceEntryBase(FILE_DESCRIPTOR_TRACING_STACK_TRACE, 0, true),
		fDescriptor(descriptor),
		fReferenceCount(descriptor->ref_count)
	{
	}

protected:
	file_descriptor*		fDescriptor;     /**< File descriptor the entry refers to. */
	int32					fReferenceCount; /**< Reference count snapshot at the moment of capture. */
};


class NewFD : public FDTraceEntry {
public:
	NewFD(io_context* context, int fd, file_descriptor* descriptor)
		:
		FDTraceEntry(descriptor),
		fContext(context),
		fFD(fd)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("fd new:     descriptor: %p (%" B_PRId32 "), context: %p, "
			"fd: %d", fDescriptor, fReferenceCount, fContext, fFD);
	}

private:
	io_context*	fContext;
	int			fFD;
};


class PutFD : public FDTraceEntry {
public:
	PutFD(file_descriptor* descriptor)
		:
		FDTraceEntry(descriptor)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("fd put:     descriptor: %p (%" B_PRId32 ")", fDescriptor,
			fReferenceCount);
	}
};


class GetFD : public FDTraceEntry {
public:
	GetFD(io_context* context, int fd, file_descriptor* descriptor)
		:
		FDTraceEntry(descriptor),
		fContext(context),
		fFD(fd)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("fd get:     descriptor: %p (%" B_PRId32 "), context: %p, "
			"fd: %d", fDescriptor, fReferenceCount, fContext, fFD);
	}

private:
	io_context*	fContext;
	int			fFD;
};


class RemoveFD : public FDTraceEntry {
public:
	RemoveFD(io_context* context, int fd, file_descriptor* descriptor)
		:
		FDTraceEntry(descriptor),
		fContext(context),
		fFD(fd)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("fd remove:  descriptor: %p (%" B_PRId32 "), context: %p, "
			"fd: %d", fDescriptor, fReferenceCount, fContext, fFD);
	}

private:
	io_context*	fContext;
	int			fFD;
};


class Dup2FD : public FDTraceEntry {
public:
	Dup2FD(io_context* context, int oldFD, int newFD)
		:
		FDTraceEntry(context->fds[oldFD]),
		fContext(context),
		fEvictedDescriptor(context->fds[newFD]),
		fEvictedReferenceCount(
			fEvictedDescriptor != NULL ? fEvictedDescriptor->ref_count : 0),
		fOldFD(oldFD),
		fNewFD(newFD)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("fd dup2:    descriptor: %p (%" B_PRId32 "), context: %p, "
			"fd: %d -> %d, evicted: %p (%" B_PRId32 ")", fDescriptor,
			fReferenceCount, fContext, fOldFD, fNewFD, fEvictedDescriptor,
			fEvictedReferenceCount);
	}

private:
	io_context*			fContext;
	file_descriptor*	fEvictedDescriptor;
	int32				fEvictedReferenceCount;
	int					fOldFD;
	int					fNewFD;
};


class InheritFD : public FDTraceEntry {
public:
	InheritFD(io_context* context, int fd, file_descriptor* descriptor,
		io_context* parentContext)
		:
		FDTraceEntry(descriptor),
		fContext(context),
		fParentContext(parentContext),
		fFD(fd)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("fd inherit: descriptor: %p (%" B_PRId32 "), context: %p, "
			"fd: %d, parentContext: %p", fDescriptor, fReferenceCount, fContext,
			fFD, fParentContext);
	}

private:
	io_context*	fContext;
	io_context*	fParentContext;
	int			fFD;
};


}	// namespace FileDescriptorTracing

#	define TFD(x)	new(std::nothrow) FileDescriptorTracing::x

#else
#	define TFD(x)
#endif	// FILE_DESCRIPTOR_TRACING


// #pragma mark - IO Context Tracing



#if IO_CONTEXT_TRACING

namespace IOContextTracing {


/** @brief Base class capturing the io_context (and optionally a stack trace) for any IO context trace entry. */
class IOContextTraceEntry : public AbstractTraceEntry {
public:
	IOContextTraceEntry(io_context* context)
		:
		fContext(context)
	{
#if IO_CONTEXT_TRACING_STACK_TRACE
		fStackTrace = capture_tracing_stack_trace(
			IO_CONTEXT_TRACING_STACK_TRACE, 0, false);
#endif
	}

#if IO_CONTEXT_TRACING_STACK_TRACE
	virtual void DumpStackTrace(TraceOutput& out)
	{
		out.PrintStackTrace(fStackTrace);
	}
#endif

protected:
	io_context*				fContext;
#if IO_CONTEXT_TRACING_STACK_TRACE
	tracing_stack_trace*	fStackTrace;
#endif
};


class NewIOContext : public IOContextTraceEntry {
public:
	NewIOContext(io_context* context, io_context* parentContext)
		:
		IOContextTraceEntry(context),
		fParentContext(parentContext)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("iocontext new:    context: %p, parent: %p", fContext,
			fParentContext);
	}

private:
	io_context*	fParentContext;
};


class FreeIOContext : public IOContextTraceEntry {
public:
	FreeIOContext(io_context* context)
		:
		IOContextTraceEntry(context)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("iocontext free:   context: %p", fContext);
	}
};


class ResizeIOContext : public IOContextTraceEntry {
public:
	ResizeIOContext(io_context* context, uint32 newTableSize)
		:
		IOContextTraceEntry(context),
		fOldTableSize(context->table_size),
		fNewTableSize(newTableSize)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		out.Print("iocontext resize: context: %p, size: %" B_PRIu32	" -> %"
			B_PRIu32, fContext, fOldTableSize, fNewTableSize);
	}

private:
	uint32	fOldTableSize;
	uint32	fNewTableSize;
};


}	// namespace IOContextTracing

#	define TIOC(x)	new(std::nothrow) IOContextTracing::x

#else
#	define TIOC(x)
#endif	// IO_CONTEXT_TRACING


#endif // VFS_TRACING_H
