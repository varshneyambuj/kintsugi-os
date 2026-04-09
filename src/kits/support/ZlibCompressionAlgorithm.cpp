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
 * @file ZlibCompressionAlgorithm.cpp
 * @brief Implementation of the zlib-based compression and decompression
 *        algorithm for Kintsugi OS.
 *
 * This file provides BZlibCompressionAlgorithm together with its parameter
 * classes (BZlibCompressionParameters, BZlibDecompressionParameters) and
 * internal Stream template wrappers.  It wraps the zlib deflate/inflate API
 * to deliver streaming compression/decompression as BDataIO objects and
 * single-shot buffer operations via CompressBuffer() / DecompressBuffer().
 *
 * Compression is only compiled in for userland builds
 * (B_ZLIB_COMPRESSION_SUPPORT).  Decompression (inflate) is always
 * available and auto-detects the zlib/gzip header format.
 *
 * @see BZlibCompressionParameters, BZlibDecompressionParameters,
 *      BZlibCompressionAlgorithm
 */


#include <ZlibCompressionAlgorithm.h>

#include <errno.h>
#include <string.h>

#include <algorithm>
#include <new>

#include <zlib.h>

#include <DataIO.h>


// build compression support only for userland
#if !defined(_KERNEL_MODE) && !defined(_BOOT_MODE)
#	define B_ZLIB_COMPRESSION_SUPPORT 1
#endif


static const size_t kMinBufferSize		= 1024;
static const size_t kMaxBufferSize		= 1024 * 1024;
static const size_t kDefaultBufferSize	= 4 * 1024;


/** @brief Clamps \a size to the allowed buffer-size range [kMinBufferSize, kMaxBufferSize]. */
static size_t
sanitize_buffer_size(size_t size)
{
	if (size < kMinBufferSize)
		return kMinBufferSize;
	return std::min(size, kMaxBufferSize);
}


// #pragma mark - BZlibCompressionParameters


/**
 * @brief Construct compression parameters with the given compression level.
 *
 * Initialises the buffer size to the default (4 KiB) and disables gzip
 * framing.
 *
 * @param compressionLevel zlib compression level in the range
 *        Z_NO_COMPRESSION (0) to Z_BEST_COMPRESSION (9), or
 *        B_ZLIB_COMPRESSION_DEFAULT to let zlib choose.
 */
BZlibCompressionParameters::BZlibCompressionParameters(
	int compressionLevel)
	:
	BCompressionParameters(),
	fCompressionLevel(compressionLevel),
	fBufferSize(kDefaultBufferSize),
	fGzipFormat(false)
{
}


BZlibCompressionParameters::~BZlibCompressionParameters()
{
}


/**
 * @brief Return the configured zlib compression level.
 * @return The current compression level value.
 */
int32
BZlibCompressionParameters::CompressionLevel() const
{
	return fCompressionLevel;
}


/**
 * @brief Set the zlib compression level.
 * @param level New compression level (Z_NO_COMPRESSION..Z_BEST_COMPRESSION).
 */
void
BZlibCompressionParameters::SetCompressionLevel(int32 level)
{
	fCompressionLevel = level;
}


/**
 * @brief Return the I/O buffer size used during streaming compression.
 * @return Buffer size in bytes.
 */
size_t
BZlibCompressionParameters::BufferSize() const
{
	return fBufferSize;
}


/**
 * @brief Set the I/O buffer size used during streaming compression.
 *
 * The value is clamped to [kMinBufferSize, kMaxBufferSize].
 *
 * @param size Desired buffer size in bytes.
 */
void
BZlibCompressionParameters::SetBufferSize(size_t size)
{
	fBufferSize = sanitize_buffer_size(size);
}


/**
 * @brief Return whether output should use gzip framing instead of raw zlib.
 * @return True if gzip format is enabled, false for raw zlib deflate.
 */
bool
BZlibCompressionParameters::IsGzipFormat() const
{
	return fGzipFormat;
}


/**
 * @brief Enable or disable gzip output framing.
 *
 * When enabled, the deflate stream is wrapped with a gzip header/trailer
 * (windowBits is increased by 16 internally).
 *
 * @param gzipFormat True to produce gzip output, false for raw deflate.
 */
void
BZlibCompressionParameters::SetGzipFormat(bool gzipFormat)
{
	fGzipFormat = gzipFormat;
}


// #pragma mark - BZlibDecompressionParameters


/**
 * @brief Construct decompression parameters with the default buffer size.
 *
 * The inflate stream auto-detects both raw zlib and gzip headers.
 */
BZlibDecompressionParameters::BZlibDecompressionParameters()
	:
	BDecompressionParameters(),
	fBufferSize(kDefaultBufferSize)
{
}


BZlibDecompressionParameters::~BZlibDecompressionParameters()
{
}


/**
 * @brief Return the I/O buffer size used during streaming decompression.
 * @return Buffer size in bytes.
 */
size_t
BZlibDecompressionParameters::BufferSize() const
{
	return fBufferSize;
}


/**
 * @brief Set the I/O buffer size used during streaming decompression.
 *
 * The value is clamped to [kMinBufferSize, kMaxBufferSize].
 *
 * @param size Desired buffer size in bytes.
 */
void
BZlibDecompressionParameters::SetBufferSize(size_t size)
{
	fBufferSize = sanitize_buffer_size(size);
}


// #pragma mark - CompressionStrategy


#ifdef B_ZLIB_COMPRESSION_SUPPORT


struct BZlibCompressionAlgorithm::CompressionStrategy {
	typedef BZlibCompressionParameters Parameters;

	static const bool kNeedsFinalFlush = true;

	static int Init(z_stream& stream,
		const BZlibCompressionParameters* parameters)
	{
		int32 compressionLevel = B_ZLIB_COMPRESSION_DEFAULT;
		bool gzipFormat = false;
		if (parameters != NULL) {
			compressionLevel = parameters->CompressionLevel();
			gzipFormat = parameters->IsGzipFormat();
		}

		return deflateInit2(&stream, compressionLevel,
			Z_DEFLATED,
			MAX_WBITS + (gzipFormat ? 16 : 0),
			MAX_MEM_LEVEL,
			Z_DEFAULT_STRATEGY);
	}

	static void Uninit(z_stream& stream)
	{
		deflateEnd(&stream);
	}

	static int Process(z_stream& stream, bool flush)
	{
		return deflate(&stream, flush ? Z_FINISH : 0);
	}
};


#endif	// B_ZLIB_COMPRESSION_SUPPORT


// #pragma mark - DecompressionStrategy


struct BZlibCompressionAlgorithm::DecompressionStrategy {
	typedef BZlibDecompressionParameters Parameters;

	static const bool kNeedsFinalFlush = false;

	static int Init(z_stream& stream,
		const BZlibDecompressionParameters* /*parameters*/)
	{
		// auto-detect zlib/gzip header
		return inflateInit2(&stream, 32 + MAX_WBITS);
	}

	static void Uninit(z_stream& stream)
	{
		inflateEnd(&stream);
	}

	static int Process(z_stream& stream, bool flush)
	{
		return inflate(&stream, flush ? Z_FINISH : 0);
	}
};


// #pragma mark - Stream


/**
 * @brief Internal streaming wrapper that couples a zlib z_stream to a
 *        BAbstractInputStream or BAbstractOutputStream.
 *
 * Stream is a template parameterised on BaseClass (input or output adapter)
 * and Strategy (CompressionStrategy or DecompressionStrategy).  It is
 * never instantiated directly by application code; use the factory methods
 * on BZlibCompressionAlgorithm instead.
 */
template<typename BaseClass, typename Strategy>
struct BZlibCompressionAlgorithm::Stream : BaseClass {
	/**
	 * @brief Construct a Stream wrapping the given BDataIO.
	 * @param io The underlying data source (input) or sink (output).
	 */
	Stream(BDataIO* io)
		:
		BaseClass(io),
		fStreamInitialized(false)
	{
	}

	/**
	 * @brief Flush pending output (compression only) and tear down the
	 *        zlib stream.
	 */
	~Stream()
	{
		if (fStreamInitialized) {
			if (Strategy::kNeedsFinalFlush)
				this->Flush();
			Strategy::Uninit(fStream);
		}
	}

	/**
	 * @brief Initialise the underlying I/O buffer and zlib stream.
	 *
	 * @param parameters Strategy-specific parameters supplying buffer size
	 *        and, for compression, the compression level and gzip flag.
	 * @return B_OK on success, or a translated zlib error code.
	 */
	status_t Init(const typename Strategy::Parameters* parameters)
	{
		status_t error = this->BaseClass::Init(
			parameters != NULL ? parameters->BufferSize() : kDefaultBufferSize);
		if (error != B_OK)
			return error;

		memset(&fStream, 0, sizeof(fStream));

		int zlibError = Strategy::Init(fStream, parameters);
		if (zlibError != Z_OK)
			return _TranslateZlibError(zlibError);

		fStreamInitialized = true;
		return B_OK;
	}

	virtual status_t ProcessData(const void* input, size_t inputSize,
		void* output, size_t outputSize, size_t& bytesConsumed,
		size_t& bytesProduced)
	{
		return _ProcessData(input, inputSize, output, outputSize,
			bytesConsumed, bytesProduced, false);
	}

	virtual status_t FlushPendingData(void* output, size_t outputSize,
		size_t& bytesProduced)
	{
		size_t bytesConsumed;
		return _ProcessData(NULL, 0, output, outputSize,
			bytesConsumed, bytesProduced, true);
	}

	template<typename BaseParameters>
	static status_t Create(BDataIO* io, BaseParameters* _parameters,
		BDataIO*& _stream)
	{
		const typename Strategy::Parameters* parameters
#ifdef _BOOT_MODE
			= static_cast<const typename Strategy::Parameters*>(_parameters);
#else
			= dynamic_cast<const typename Strategy::Parameters*>(_parameters);
#endif
		Stream* stream = new(std::nothrow) Stream(io);
		if (stream == NULL)
			return B_NO_MEMORY;

		status_t error = stream->Init(parameters);
		if (error != B_OK) {
			delete stream;
			return error;
		}

		_stream = stream;
		return B_OK;
	}

private:
	status_t _ProcessData(const void* input, size_t inputSize,
		void* output, size_t outputSize, size_t& bytesConsumed,
		size_t& bytesProduced, bool flush)
	{
		fStream.next_in = (Bytef*)input;
		fStream.avail_in = inputSize;
		fStream.next_out = (Bytef*)output;
		fStream.avail_out = outputSize;

		int zlibError = Strategy::Process(fStream, flush);
		if (zlibError != Z_OK) {
			if (zlibError == Z_STREAM_END) {
				if (fStream.avail_in != 0)
					return B_BAD_DATA;
			} else
				return _TranslateZlibError(zlibError);
		}

		bytesConsumed = inputSize - (size_t)fStream.avail_in;
		bytesProduced = outputSize - (size_t)fStream.avail_out;
		return B_OK;
	}

private:
	z_stream	fStream;
	bool		fStreamInitialized;
};


// #pragma mark - BZlibCompressionAlgorithm


/**
 * @brief Construct a BZlibCompressionAlgorithm instance.
 */
BZlibCompressionAlgorithm::BZlibCompressionAlgorithm()
	:
	BCompressionAlgorithm()
{
}


/**
 * @brief Destroy the BZlibCompressionAlgorithm instance.
 */
BZlibCompressionAlgorithm::~BZlibCompressionAlgorithm()
{
}


/**
 * @brief Create a compressing input stream that reads uncompressed data from
 *        \a input and exposes compressed data.
 *
 * Only available when B_ZLIB_COMPRESSION_SUPPORT is defined (userland builds).
 *
 * @param input      Source of uncompressed data.
 * @param parameters Optional BZlibCompressionParameters controlling level,
 *                   gzip format, and buffer size.  May be NULL.
 * @param _stream    On success, receives a heap-allocated BDataIO that the
 *                   caller must delete.
 * @return B_OK on success, B_NOT_SUPPORTED in kernel/boot mode,
 *         or B_NO_MEMORY on allocation failure.
 */
status_t
BZlibCompressionAlgorithm::CreateCompressingInputStream(BDataIO* input,
	const BCompressionParameters* parameters, BDataIO*& _stream)
{
#ifdef B_ZLIB_COMPRESSION_SUPPORT
	return Stream<BAbstractInputStream, CompressionStrategy>::Create(
		input, parameters, _stream);
#else
	return B_NOT_SUPPORTED;
#endif
}


/**
 * @brief Create a compressing output stream that accepts uncompressed data and
 *        writes compressed data to \a output.
 *
 * Only available when B_ZLIB_COMPRESSION_SUPPORT is defined.
 *
 * @param output     Destination for compressed data.
 * @param parameters Optional BZlibCompressionParameters.  May be NULL.
 * @param _stream    On success, receives a heap-allocated BDataIO.
 * @return B_OK, B_NOT_SUPPORTED, or B_NO_MEMORY.
 */
status_t
BZlibCompressionAlgorithm::CreateCompressingOutputStream(BDataIO* output,
	const BCompressionParameters* parameters, BDataIO*& _stream)
{
#ifdef B_ZLIB_COMPRESSION_SUPPORT
	return Stream<BAbstractOutputStream, CompressionStrategy>::Create(
		output, parameters, _stream);
#else
	return B_NOT_SUPPORTED;
#endif
}


/**
 * @brief Create a decompressing input stream that reads compressed data from
 *        \a input and exposes decompressed data.
 *
 * Auto-detects both raw zlib and gzip headers.  Available in all build
 * configurations.
 *
 * @param input      Source of compressed data.
 * @param parameters Optional BZlibDecompressionParameters.  May be NULL.
 * @param _stream    On success, receives a heap-allocated BDataIO.
 * @return B_OK or B_NO_MEMORY.
 */
status_t
BZlibCompressionAlgorithm::CreateDecompressingInputStream(BDataIO* input,
	const BDecompressionParameters* parameters, BDataIO*& _stream)
{
	return Stream<BAbstractInputStream, DecompressionStrategy>::Create(
		input, parameters, _stream);
}


/**
 * @brief Create a decompressing output stream that accepts compressed data and
 *        writes decompressed data to \a output.
 *
 * Auto-detects both raw zlib and gzip headers.
 *
 * @param output     Destination for decompressed data.
 * @param parameters Optional BZlibDecompressionParameters.  May be NULL.
 * @param _stream    On success, receives a heap-allocated BDataIO.
 * @return B_OK or B_NO_MEMORY.
 */
status_t
BZlibCompressionAlgorithm::CreateDecompressingOutputStream(BDataIO* output,
	const BDecompressionParameters* parameters, BDataIO*& _stream)
{
	return Stream<BAbstractOutputStream, DecompressionStrategy>::Create(
		output, parameters, _stream);
}


/**
 * @brief Compress an entire buffer in a single call.
 *
 * Uses zlib compress2() for one-shot deflation.  Only available when
 * B_ZLIB_COMPRESSION_SUPPORT is defined.
 *
 * @param input      Source buffer descriptor (base pointer and length).
 * @param output     Destination buffer descriptor.  On success, iov_len is
 *                   updated to reflect the number of bytes written.
 * @param parameters Optional BZlibCompressionParameters for the compression
 *                   level.  May be NULL (uses B_ZLIB_COMPRESSION_DEFAULT).
 * @param scratch    Unused; reserved for future use.
 * @return B_OK on success, B_NOT_SUPPORTED in kernel/boot mode, or a
 *         translated zlib error code.
 */
status_t
BZlibCompressionAlgorithm::CompressBuffer(const iovec& input, iovec& output,
	const BCompressionParameters* parameters, iovec* scratch)
{
#ifdef B_ZLIB_COMPRESSION_SUPPORT
	const BZlibCompressionParameters* zlibParameters
		= dynamic_cast<const BZlibCompressionParameters*>(parameters);
	int compressionLevel = zlibParameters != NULL
		? zlibParameters->CompressionLevel()
		: B_ZLIB_COMPRESSION_DEFAULT;

	uLongf bytesUsed = output.iov_len;
	int zlibError = compress2((Bytef*)output.iov_base, &bytesUsed,
		(const Bytef*)input.iov_base, (uLong)input.iov_len, compressionLevel);
	if (zlibError != Z_OK)
		return _TranslateZlibError(zlibError);

	output.iov_len = (size_t)bytesUsed;
	return B_OK;
#else
	return B_NOT_SUPPORTED;
#endif
}


/**
 * @brief Decompress an entire buffer in a single call.
 *
 * Uses zlib uncompress() for one-shot inflation.  Available in all build
 * configurations.
 *
 * @param input      Source buffer of compressed data.
 * @param output     Destination buffer.  On success, iov_len is updated to the
 *                   number of decompressed bytes written.
 * @param parameters Ignored (reserved for future use).  May be NULL.
 * @param scratch    Unused; reserved for future use.
 * @return B_OK on success or a translated zlib error code.
 */
status_t
BZlibCompressionAlgorithm::DecompressBuffer(const iovec& input, iovec& output,
	const BDecompressionParameters* parameters, iovec* scratch)
{
	uLongf bytesUsed = output.iov_len;
	int zlibError = uncompress((Bytef*)output.iov_base, &bytesUsed,
		(const Bytef*)input.iov_base, (uLong)input.iov_len);
	if (zlibError != Z_OK)
		return _TranslateZlibError(zlibError);

	output.iov_len = (size_t)bytesUsed;
	return B_OK;
}


/*static*/ status_t
BZlibCompressionAlgorithm::_TranslateZlibError(int error)
{
	switch (error) {
		case Z_OK:
			return B_OK;
		case Z_STREAM_END:
		case Z_NEED_DICT:
			// a special event (no error), but the caller doesn't seem to handle
			// it
			return B_ERROR;
		case Z_ERRNO:
			return errno;
		case Z_STREAM_ERROR:
			return B_BAD_VALUE;
		case Z_DATA_ERROR:
			return B_BAD_DATA;
		case Z_MEM_ERROR:
			return B_NO_MEMORY;
		case Z_BUF_ERROR:
			return B_BUFFER_OVERFLOW;
		case Z_VERSION_ERROR:
			return B_BAD_VALUE;
		default:
			return B_ERROR;
	}
}
