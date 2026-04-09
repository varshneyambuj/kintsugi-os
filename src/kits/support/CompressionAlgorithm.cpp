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
 *   Copyright 2014, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file CompressionAlgorithm.cpp
 * @brief Base-class implementations for the compression algorithm framework.
 *
 * This file provides default (stub) implementations of BCompressionAlgorithm,
 * BCompressionParameters, and BDecompressionParameters, as well as the
 * concrete streaming infrastructure BAbstractStream, BAbstractInputStream, and
 * BAbstractOutputStream.  Derived classes (e.g. ZlibCompressionAlgorithm)
 * override the virtual methods to provide algorithm-specific logic.
 *
 * The stream classes implement an iconv-style loop: compressed or uncompressed
 * data is read from a source BDataIO into an internal ring buffer, then
 * ProcessData() (implemented by the subclass) converts it into the output
 * buffer; FlushPendingData() is called at end-of-stream to drain any codec
 * state.
 *
 * @see BCompressionAlgorithm, BAbstractInputStream, BAbstractOutputStream
 */


#include <CompressionAlgorithm.h>

#include <stdlib.h>
#include <string.h>

#include <Errors.h>


// #pragma mark - BCompressionParameters


/**
 * @brief Default constructor — no algorithm-specific parameters.
 */
BCompressionParameters::BCompressionParameters()
{
}


/**
 * @brief Destructor — no-op base implementation.
 */
BCompressionParameters::~BCompressionParameters()
{
}


// #pragma mark - BDecompressionParameters


/**
 * @brief Default constructor — no algorithm-specific parameters.
 */
BDecompressionParameters::BDecompressionParameters()
{
}


/**
 * @brief Destructor — no-op base implementation.
 */
BDecompressionParameters::~BDecompressionParameters()
{
}


// #pragma mark - BCompressionAlgorithm


/**
 * @brief Default constructor — no algorithm state at this level.
 */
BCompressionAlgorithm::BCompressionAlgorithm()
{
}


/**
 * @brief Destructor — no-op base implementation.
 */
BCompressionAlgorithm::~BCompressionAlgorithm()
{
}


/**
 * @brief Create a BDataIO stream that compresses data read from \a input.
 *
 * The default implementation returns B_NOT_SUPPORTED.  Subclasses that
 * support streaming compression must override this method and return a
 * heap-allocated stream in \a _stream.
 *
 * @param input       The source BDataIO to read uncompressed data from.
 * @param parameters  Algorithm-specific compression parameters, or NULL for
 *                    defaults.
 * @param _stream     Receives the created BDataIO on success.
 * @return B_OK on success, B_NOT_SUPPORTED if not implemented, or another
 *         error code.
 * @see CreateCompressingOutputStream()
 */
status_t
BCompressionAlgorithm::CreateCompressingInputStream(BDataIO* input,
	const BCompressionParameters* parameters, BDataIO*& _stream)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Create a BDataIO stream that compresses data written to \a output.
 *
 * The default implementation returns B_NOT_SUPPORTED.  Subclasses that
 * support streaming compression must override this method and return a
 * heap-allocated stream in \a _stream.
 *
 * @param output      The sink BDataIO to write compressed data to.
 * @param parameters  Algorithm-specific compression parameters, or NULL for
 *                    defaults.
 * @param _stream     Receives the created BDataIO on success.
 * @return B_OK on success, B_NOT_SUPPORTED if not implemented, or another
 *         error code.
 * @see CreateCompressingInputStream()
 */
status_t
BCompressionAlgorithm::CreateCompressingOutputStream(BDataIO* output,
	const BCompressionParameters* parameters, BDataIO*& _stream)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Create a BDataIO stream that decompresses data read from \a input.
 *
 * The default implementation returns B_NOT_SUPPORTED.  Subclasses that
 * support streaming decompression must override this method and return a
 * heap-allocated stream in \a _stream.
 *
 * @param input       The source BDataIO to read compressed data from.
 * @param parameters  Algorithm-specific decompression parameters, or NULL for
 *                    defaults.
 * @param _stream     Receives the created BDataIO on success.
 * @return B_OK on success, B_NOT_SUPPORTED if not implemented, or another
 *         error code.
 * @see CreateDecompressingOutputStream()
 */
status_t
BCompressionAlgorithm::CreateDecompressingInputStream(BDataIO* input,
	const BDecompressionParameters* parameters, BDataIO*& _stream)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Create a BDataIO stream that decompresses data written to \a output.
 *
 * The default implementation returns B_NOT_SUPPORTED.  Subclasses that
 * support streaming decompression must override this method and return a
 * heap-allocated stream in \a _stream.
 *
 * @param output      The sink BDataIO to write decompressed data to.
 * @param parameters  Algorithm-specific decompression parameters, or NULL for
 *                    defaults.
 * @param _stream     Receives the created BDataIO on success.
 * @return B_OK on success, B_NOT_SUPPORTED if not implemented, or another
 *         error code.
 * @see CreateDecompressingInputStream()
 */
status_t
BCompressionAlgorithm::CreateDecompressingOutputStream(BDataIO* output,
	const BDecompressionParameters* parameters, BDataIO*& _stream)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Compress the data described by \a input into \a output in a single
 *        call.
 *
 * The default implementation returns B_NOT_SUPPORTED.  Subclasses that
 * support one-shot buffer compression must override this method.
 *
 * @param input       An iovec describing the uncompressed input data.
 * @param output      An iovec describing the output buffer.  The iov_len
 *                    field is updated to the number of bytes written on
 *                    success.
 * @param parameters  Algorithm-specific compression parameters, or NULL.
 * @param scratch     Optional scratch buffer iovec, or NULL.
 * @return B_OK on success, B_NOT_SUPPORTED if not implemented, or another
 *         error code.
 * @see DecompressBuffer()
 */
status_t
BCompressionAlgorithm::CompressBuffer(const iovec& input, iovec& output,
	const BCompressionParameters* parameters, iovec* scratch)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Decompress the data described by \a input into \a output in a single
 *        call.
 *
 * The default implementation returns B_NOT_SUPPORTED.  Subclasses that
 * support one-shot buffer decompression must override this method.
 *
 * @param input       An iovec describing the compressed input data.
 * @param output      An iovec describing the output buffer.  The iov_len
 *                    field is updated to the number of bytes written on
 *                    success.
 * @param parameters  Algorithm-specific decompression parameters, or NULL.
 * @param scratch     Optional scratch buffer iovec, or NULL.
 * @return B_OK on success, B_NOT_SUPPORTED if not implemented, or another
 *         error code.
 * @see CompressBuffer()
 */
status_t
BCompressionAlgorithm::DecompressBuffer(const iovec& input, iovec& output,
	const BDecompressionParameters* parameters, iovec* scratch)
{
	return B_NOT_SUPPORTED;
}


// #pragma mark - BAbstractStream


/**
 * @brief Construct an uninitialised BAbstractStream.
 *
 * The internal buffer is not allocated until Init() is called.
 */
BCompressionAlgorithm::BAbstractStream::BAbstractStream()
	:
	BDataIO(),
	fBuffer(NULL),
	fBufferCapacity(0),
	fBufferOffset(0),
	fBufferSize(0)
{
}


/**
 * @brief Destructor — frees the internal intermediate buffer.
 */
BCompressionAlgorithm::BAbstractStream::~BAbstractStream()
{
	free(fBuffer);
}


/**
 * @brief Allocate the internal intermediate buffer.
 *
 * Must be called once before the stream is used for I/O.
 *
 * @param bufferSize  The number of bytes to allocate for the internal buffer.
 * @return B_OK on success, B_NO_MEMORY if allocation fails.
 */
status_t
BCompressionAlgorithm::BAbstractStream::Init(size_t bufferSize)
{
	fBuffer = (uint8*)malloc(bufferSize);
	fBufferCapacity = bufferSize;

	return fBuffer != NULL ? B_OK : B_NO_MEMORY;
}


// #pragma mark - BAbstractInputStream


/**
 * @brief Construct a BAbstractInputStream that reads from \a input.
 *
 * @param input  The source BDataIO supplying raw (compressed or uncompressed)
 *               data.  Not owned by the stream.
 */
BCompressionAlgorithm::BAbstractInputStream::BAbstractInputStream(
		BDataIO* input)
	:
	BAbstractStream(),
	fInput(input),
	fEndOfInput(false),
	fNoMorePendingData(false)
{
}


/**
 * @brief Destructor — no-op; the source BDataIO is not owned by this object.
 */
BCompressionAlgorithm::BAbstractInputStream::~BAbstractInputStream()
{
}


/**
 * @brief Read up to \a size converted bytes into \a buffer.
 *
 * Implements the iconv-style conversion loop:
 * -# If the internal buffer contains data, call ProcessData() to convert as
 *    much as possible into the caller's output buffer.
 * -# If the internal buffer is empty and end-of-input has not been reached,
 *    read more data from fInput into the buffer and repeat from step 1.
 * -# Once fInput signals end-of-stream (Read() returns 0) and there is still
 *    room in the caller's buffer, call FlushPendingData() once to drain any
 *    remaining codec state.
 *
 * @param buffer  Destination for converted data.
 * @param size    Maximum number of bytes to produce.
 * @return The number of bytes written to \a buffer, 0 at end-of-stream, or a
 *         negative error code on failure.
 * @see ProcessData(), FlushPendingData()
 */
ssize_t
BCompressionAlgorithm::BAbstractInputStream::Read(void* buffer, size_t size)
{
	if (size == 0)
		return 0;

	size_t bytesRemaining = size;
	uint8* output = (uint8*)buffer;

	while (bytesRemaining > 0) {
		// process the data still in the input buffer
		if (fBufferSize > 0) {
			size_t bytesConsumed;
			size_t bytesProduced;
			status_t error = ProcessData(fBuffer + fBufferOffset, fBufferSize,
				output, bytesRemaining, bytesConsumed, bytesProduced);
			if (error != B_OK)
				return error;

			fBufferOffset += bytesConsumed;
			fBufferSize -= bytesConsumed;
			output += bytesProduced;
			bytesRemaining -= bytesProduced;
			continue;
		}

		// We couldn't process anything, because we don't have any or not enough
		// bytes in the input buffer.

		if (fEndOfInput)
			break;

		// Move any remaining data to the start of the buffer.
		if (fBufferSize > 0) {
			if (fBufferSize == fBufferCapacity)
				return B_ERROR;

			if (fBufferOffset > 0)
				memmove(fBuffer, fBuffer + fBufferOffset, fBufferSize);
		}

		fBufferOffset = 0;

		// read from the source
		ssize_t bytesRead = fInput->Read(fBuffer + fBufferSize,
			fBufferCapacity - fBufferSize);
		if (bytesRead < 0)
			return bytesRead;
		if (bytesRead == 0) {
			fEndOfInput = true;
			break;
		}

		fBufferSize += bytesRead;
	}

	// If we've reached the end of the input and still have room in the output
	// buffer, we have consumed all input data and want to flush all pending
	// data, now.
	if (fEndOfInput && bytesRemaining > 0 && !fNoMorePendingData) {
		size_t bytesProduced;
		status_t error = FlushPendingData(output, bytesRemaining,
			bytesProduced);
		if (error != B_OK)
			return error;

		if (bytesProduced < bytesRemaining)
			fNoMorePendingData = true;

		output += bytesProduced;
		bytesRemaining -= bytesProduced;
	}

	return size - bytesRemaining;
}


// #pragma mark - BAbstractOutputStream


/**
 * @brief Construct a BAbstractOutputStream that writes converted data to
 *        \a output.
 *
 * @param output  The sink BDataIO that receives converted data.  Not owned by
 *                this stream.
 */
BCompressionAlgorithm::BAbstractOutputStream::BAbstractOutputStream(
		BDataIO* output)
	:
	BAbstractStream(),
	fOutput(output)
{
}


/**
 * @brief Destructor — no-op; the sink BDataIO is not owned by this object.
 */
BCompressionAlgorithm::BAbstractOutputStream::~BAbstractOutputStream()
{
}


/**
 * @brief Write \a size bytes from \a buffer through the conversion layer to
 *        the underlying output stream.
 *
 * Implements the iconv-style production loop:
 * -# Call ProcessData() to convert data from \a buffer into the internal
 *    output buffer.
 * -# When the internal buffer is full (ProcessData() produces nothing), flush
 *    it to fOutput via Write() and continue.
 *
 * @param buffer  Source data to convert and write.
 * @param size    Number of bytes in \a buffer.
 * @return The number of bytes consumed from \a buffer, or a negative error
 *         code on failure.
 * @see Flush(), ProcessData()
 */
ssize_t
BCompressionAlgorithm::BAbstractOutputStream::Write(const void* buffer,
	size_t size)
{
	if (size == 0)
		return 0;

	size_t bytesRemaining = size;
	uint8* input = (uint8*)buffer;

	while (bytesRemaining > 0) {
		// try to process more data
		if (fBufferSize < fBufferCapacity) {
			size_t bytesConsumed;
			size_t bytesProduced;
			status_t error = ProcessData(input, bytesRemaining,
				fBuffer + fBufferSize, fBufferCapacity - fBufferSize,
				bytesConsumed, bytesProduced);
			if (error != B_OK)
				return error;

			input += bytesConsumed;
			bytesRemaining -= bytesConsumed;
			fBufferSize += bytesProduced;
			continue;
		}

		// We couldn't process anything, because we don't have any or not enough
		// room in the output buffer.

		if (fBufferSize == 0)
			return B_ERROR;

		// write to the target
		ssize_t bytesWritten = fOutput->Write(fBuffer, fBufferSize);
		if (bytesWritten < 0)
			return bytesWritten;
		if (bytesWritten == 0)
			break;

		// Move any remaining data to the start of the buffer.
		fBufferSize -= bytesWritten;
		if (fBufferSize > 0)
			memmove(fBuffer, fBuffer + bytesWritten, fBufferSize);
	}

	return size - bytesRemaining;
}


/**
 * @brief Flush all pending codec state and any buffered output to the
 *        underlying sink.
 *
 * Repeatedly calls FlushPendingData() until the subclass signals that there is
 * nothing more to produce, writing each batch of produced bytes to fOutput via
 * WriteExactly().  Finally calls fOutput->Flush() to ensure the sink is fully
 * flushed.
 *
 * @return B_OK on success, or an error from FlushPendingData() or
 *         WriteExactly().
 * @see Write(), FlushPendingData()
 */
status_t
BCompressionAlgorithm::BAbstractOutputStream::Flush()
{
	bool noMorePendingData = false;

	for (;;) {
		// let the derived class flush all pending data
		if (fBufferSize < fBufferCapacity && !noMorePendingData) {
			size_t bytesProduced;
			status_t error = FlushPendingData(fBuffer + fBufferSize,
				fBufferCapacity - fBufferSize, bytesProduced);
			if (error != B_OK)
				return error;

			noMorePendingData = bytesProduced < fBufferCapacity - fBufferSize;

			fBufferSize += bytesProduced;
		}

		// write buffered data to output
		if (fBufferSize == 0)
			break;

		status_t error = fOutput->WriteExactly(fBuffer, fBufferSize);
		if (error != B_OK)
			return error;

		fBufferSize = 0;
	}

	return fOutput->Flush();
}
