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
 *   Copyright 2017, Jérôme Duval.
 *   Copyright 2014, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ZstdCompressionAlgorithm.cpp
 * @brief Implementation of the Zstandard-based compression and decompression
 *        algorithm for Kintsugi OS.
 *
 * This file provides BZstdCompressionAlgorithm together with its parameter
 * classes (BZstdCompressionParameters, BZstdDecompressionParameters) and
 * internal Stream template wrappers.  It wraps the libzstd streaming API
 * (ZSTD_CStream / ZSTD_DStream) to deliver streaming compression/decompression
 * as BDataIO objects and single-shot buffer operations via CompressBuffer() /
 * DecompressBuffer().
 *
 * Both compression and decompression require the ZSTD_ENABLED define to be set
 * at build time; compression additionally requires the build to be a userland
 * (non-kernel, non-boot) target (B_ZSTD_COMPRESSION_SUPPORT).
 *
 * @see BZstdCompressionParameters, BZstdDecompressionParameters,
 *      BZstdCompressionAlgorithm
 */


#include <ZstdCompressionAlgorithm.h>

#include <errno.h>
#include <string.h>

#include <algorithm>
#include <new>

#ifdef ZSTD_ENABLED
  #include <zstd.h>
  #include <zstd_errors.h>
#endif

#include <AutoDeleter.h>
#include <DataIO.h>


// build compression support only for userland
#if defined(ZSTD_ENABLED) && !defined(_KERNEL_MODE) && !defined(_BOOT_MODE)
#	define B_ZSTD_COMPRESSION_SUPPORT 1
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


// #pragma mark - BZstdCompressionParameters


/**
 * @brief Construct compression parameters with the given compression level.
 *
 * Initialises the buffer size to the default (4 KiB).
 *
 * @param compressionLevel zstd compression level.  Use
 *        B_ZSTD_COMPRESSION_DEFAULT for the library default, or a value in
 *        the range supported by the installed libzstd build.
 */
BZstdCompressionParameters::BZstdCompressionParameters(
	int compressionLevel)
	:
	BCompressionParameters(),
	fCompressionLevel(compressionLevel),
	fBufferSize(kDefaultBufferSize)
{
}


BZstdCompressionParameters::~BZstdCompressionParameters()
{
}


/**
 * @brief Return the configured zstd compression level.
 * @return The current compression level value.
 */
int32
BZstdCompressionParameters::CompressionLevel() const
{
	return fCompressionLevel;
}


/**
 * @brief Set the zstd compression level.
 * @param level New compression level.
 */
void
BZstdCompressionParameters::SetCompressionLevel(int32 level)
{
	fCompressionLevel = level;
}


/**
 * @brief Return the I/O buffer size used during streaming compression.
 * @return Buffer size in bytes.
 */
size_t
BZstdCompressionParameters::BufferSize() const
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
BZstdCompressionParameters::SetBufferSize(size_t size)
{
	fBufferSize = sanitize_buffer_size(size);
}


// #pragma mark - BZstdDecompressionParameters


/**
 * @brief Construct decompression parameters with the default buffer size.
 */
BZstdDecompressionParameters::BZstdDecompressionParameters()
	:
	BDecompressionParameters(),
	fBufferSize(kDefaultBufferSize)
{
}


BZstdDecompressionParameters::~BZstdDecompressionParameters()
{
}


/**
 * @brief Return the I/O buffer size used during streaming decompression.
 * @return Buffer size in bytes.
 */
size_t
BZstdDecompressionParameters::BufferSize() const
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
BZstdDecompressionParameters::SetBufferSize(size_t size)
{
	fBufferSize = sanitize_buffer_size(size);
}


// #pragma mark - CompressionStrategy


#ifdef B_ZSTD_COMPRESSION_SUPPORT


struct BZstdCompressionAlgorithm::CompressionStrategy {
	typedef BZstdCompressionParameters Parameters;

	static const bool kNeedsFinalFlush = true;

	static size_t Init(ZSTD_CStream **stream,
		const BZstdCompressionParameters* parameters)
	{
		int32 compressionLevel = B_ZSTD_COMPRESSION_DEFAULT;
		if (parameters != NULL) {
			compressionLevel = parameters->CompressionLevel();
		}

		*stream = ZSTD_createCStream();
		return ZSTD_initCStream(*stream, compressionLevel);
	}

	static void Uninit(ZSTD_CStream *stream)
	{
		ZSTD_freeCStream(stream);
	}

	static size_t Process(ZSTD_CStream *stream, ZSTD_inBuffer *input,
		ZSTD_outBuffer *output, bool flush)
	{
		if (flush)
			return ZSTD_flushStream(stream, output);
		else
			return ZSTD_compressStream(stream, output, input);
	}
};


#endif	// B_ZSTD_COMPRESSION_SUPPORT


// #pragma mark - DecompressionStrategy


#ifdef ZSTD_ENABLED


struct BZstdCompressionAlgorithm::DecompressionStrategy {
	typedef BZstdDecompressionParameters Parameters;

	static const bool kNeedsFinalFlush = false;

	static size_t Init(ZSTD_DStream **stream,
		const BZstdDecompressionParameters* /*parameters*/)
	{
		*stream = ZSTD_createDStream();
		return ZSTD_initDStream(*stream);
	}

	static void Uninit(ZSTD_DStream *stream)
	{
		ZSTD_freeDStream(stream);
	}

	static size_t Process(ZSTD_DStream *stream, ZSTD_inBuffer *input,
		ZSTD_outBuffer *output, bool flush)
	{
		return ZSTD_decompressStream(stream, output, input);
	}

};


// #pragma mark - Stream


/**
 * @brief Internal streaming wrapper that couples a ZSTD_CStream or
 *        ZSTD_DStream to a BAbstractInputStream or BAbstractOutputStream.
 *
 * Stream is a template parameterised on BaseClass (input or output adapter),
 * Strategy (CompressionStrategy or DecompressionStrategy), and StreamType
 * (ZSTD_CStream or ZSTD_DStream).  It is never instantiated directly by
 * application code; use the factory methods on BZstdCompressionAlgorithm.
 */
template<typename BaseClass, typename Strategy, typename StreamType>
struct BZstdCompressionAlgorithm::Stream : BaseClass {
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
	 * @brief Flush pending output (compression only) and free the zstd stream.
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
	 * @brief Initialise the underlying I/O buffer and zstd stream context.
	 *
	 * @param parameters Strategy-specific parameters providing the buffer size
	 *        and, for compression, the compression level.
	 * @return B_OK on success, or a translated zstd error code.
	 */
	status_t Init(const typename Strategy::Parameters* parameters)
	{
		status_t error = this->BaseClass::Init(
			parameters != NULL ? parameters->BufferSize() : kDefaultBufferSize);
		if (error != B_OK)
			return error;

		size_t zstdError = Strategy::Init(&fStream, parameters);
		if (ZSTD_getErrorCode(zstdError) != ZSTD_error_no_error)
			return _TranslateZstdError(zstdError);

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
		inBuffer.src = input;
		inBuffer.pos = 0;
		inBuffer.size = inputSize;
		outBuffer.dst = output;
		outBuffer.pos = 0;
		outBuffer.size = outputSize;

		size_t zstdError = Strategy::Process(fStream, &inBuffer, &outBuffer, flush);
		if (ZSTD_getErrorCode(zstdError) != ZSTD_error_no_error)
			return _TranslateZstdError(zstdError);

		bytesConsumed = inBuffer.pos;
		bytesProduced = outBuffer.pos;
		return B_OK;
	}

private:
	bool		fStreamInitialized;
	StreamType	*fStream;
	ZSTD_inBuffer inBuffer;
	ZSTD_outBuffer outBuffer;
};


#endif	// ZSTD_ENABLED


// #pragma mark - BZstdCompressionAlgorithm


/**
 * @brief Construct a BZstdCompressionAlgorithm instance.
 */
BZstdCompressionAlgorithm::BZstdCompressionAlgorithm()
	:
	BCompressionAlgorithm()
{
}


/**
 * @brief Destroy the BZstdCompressionAlgorithm instance.
 */
BZstdCompressionAlgorithm::~BZstdCompressionAlgorithm()
{
}


/**
 * @brief Create a compressing input stream that reads uncompressed data from
 *        \a input and exposes compressed data.
 *
 * Requires both ZSTD_ENABLED and a userland build (B_ZSTD_COMPRESSION_SUPPORT).
 *
 * @param input      Source of uncompressed data.
 * @param parameters Optional BZstdCompressionParameters.  May be NULL.
 * @param _stream    On success, receives a heap-allocated BDataIO that the
 *                   caller must delete.
 * @return B_OK, B_NOT_SUPPORTED, or B_NO_MEMORY.
 */
status_t
BZstdCompressionAlgorithm::CreateCompressingInputStream(BDataIO* input,
	const BCompressionParameters* parameters, BDataIO*& _stream)
{
#ifdef B_ZSTD_COMPRESSION_SUPPORT
	return Stream<BAbstractInputStream, CompressionStrategy, ZSTD_CStream>::Create(
		input, parameters, _stream);
#else
	return B_NOT_SUPPORTED;
#endif
}


/**
 * @brief Create a compressing output stream that accepts uncompressed data and
 *        writes compressed data to \a output.
 *
 * Requires both ZSTD_ENABLED and a userland build.
 *
 * @param output     Destination for compressed data.
 * @param parameters Optional BZstdCompressionParameters.  May be NULL.
 * @param _stream    On success, receives a heap-allocated BDataIO.
 * @return B_OK, B_NOT_SUPPORTED, or B_NO_MEMORY.
 */
status_t
BZstdCompressionAlgorithm::CreateCompressingOutputStream(BDataIO* output,
	const BCompressionParameters* parameters, BDataIO*& _stream)
{
#ifdef B_ZSTD_COMPRESSION_SUPPORT
	return Stream<BAbstractOutputStream, CompressionStrategy, ZSTD_CStream>::Create(
		output, parameters, _stream);
#else
	return B_NOT_SUPPORTED;
#endif
}


/**
 * @brief Create a decompressing input stream that reads compressed data from
 *        \a input and exposes decompressed data.
 *
 * Requires ZSTD_ENABLED.
 *
 * @param input      Source of compressed data.
 * @param parameters Optional BZstdDecompressionParameters.  May be NULL.
 * @param _stream    On success, receives a heap-allocated BDataIO.
 * @return B_OK, B_NOT_SUPPORTED, or B_NO_MEMORY.
 */
status_t
BZstdCompressionAlgorithm::CreateDecompressingInputStream(BDataIO* input,
	const BDecompressionParameters* parameters, BDataIO*& _stream)
{
#ifdef ZSTD_ENABLED
	return Stream<BAbstractInputStream, DecompressionStrategy, ZSTD_DStream>::Create(
		input, parameters, _stream);
#else
	return B_NOT_SUPPORTED;
#endif
}


/**
 * @brief Create a decompressing output stream that accepts compressed data and
 *        writes decompressed data to \a output.
 *
 * Requires ZSTD_ENABLED.
 *
 * @param output     Destination for decompressed data.
 * @param parameters Optional BZstdDecompressionParameters.  May be NULL.
 * @param _stream    On success, receives a heap-allocated BDataIO.
 * @return B_OK, B_NOT_SUPPORTED, or B_NO_MEMORY.
 */
status_t
BZstdCompressionAlgorithm::CreateDecompressingOutputStream(BDataIO* output,
	const BDecompressionParameters* parameters, BDataIO*& _stream)
{
#ifdef ZSTD_ENABLED
	return Stream<BAbstractOutputStream, DecompressionStrategy, ZSTD_DStream>::Create(
		output, parameters, _stream);
#else
	return B_NOT_SUPPORTED;
#endif
}


/**
 * @brief Compress an entire buffer in a single call using ZSTD_compress().
 *
 * Requires both ZSTD_ENABLED and a userland build.
 *
 * @param input      Source buffer descriptor.
 * @param output     Destination buffer.  On success, iov_len is updated to the
 *                   number of compressed bytes written.
 * @param parameters Optional BZstdCompressionParameters for the level.  May
 *                   be NULL (uses B_ZSTD_COMPRESSION_DEFAULT).
 * @param scratch    Unused; reserved for future use.
 * @return B_OK, B_NOT_SUPPORTED, or a translated zstd error code.
 * @note The scratch buffer is not yet used; it is reserved for a future
 *       ZSTD_initStaticCCtx() optimisation path.
 */
status_t
BZstdCompressionAlgorithm::CompressBuffer(const iovec& input, iovec& output,
	const BCompressionParameters* parameters, iovec* scratch)
{
#ifdef B_ZSTD_COMPRESSION_SUPPORT
	// TODO: Make use of scratch buffer (if available.)
	const BZstdCompressionParameters* zstdParameters
		= dynamic_cast<const BZstdCompressionParameters*>(parameters);
	int compressionLevel = zstdParameters != NULL
		? zstdParameters->CompressionLevel()
		: B_ZSTD_COMPRESSION_DEFAULT;

	size_t zstdError = ZSTD_compress(output.iov_base, output.iov_len,
		input.iov_base, input.iov_len, compressionLevel);
	if (ZSTD_isError(zstdError))
		return _TranslateZstdError(zstdError);

	output.iov_len = zstdError;
	return B_OK;
#else
	return B_NOT_SUPPORTED;
#endif
}


/**
 * @brief Decompress an entire buffer in a single call using ZSTD_decompressDCtx().
 *
 * Requires ZSTD_ENABLED.  When ZSTD_STATIC_LINKING_ONLY is defined and a
 * \a scratch buffer is provided, a static decompression context is initialised
 * from it to avoid a heap allocation.
 *
 * @param input      Source buffer of compressed data.
 * @param output     Destination buffer.  On success, iov_len is updated to the
 *                   number of decompressed bytes written.
 * @param parameters Ignored.  May be NULL.
 * @param scratch    Optional buffer used for a static ZSTD_DCtx (when
 *                   ZSTD_STATIC_LINKING_ONLY is defined).  May be NULL.
 * @return B_OK, B_NOT_SUPPORTED, or a translated zstd error code.
 */
status_t
BZstdCompressionAlgorithm::DecompressBuffer(const iovec& input, iovec& output,
	const BDecompressionParameters* parameters, iovec* scratch)
{
#ifdef ZSTD_ENABLED
	ZSTD_DCtx* dctx;
	CObjectDeleter<ZSTD_DCtx, size_t, ZSTD_freeDCtx> dctxDeleter;
#if defined(ZSTD_STATIC_LINKING_ONLY)
	if (scratch != NULL)
		dctx = ZSTD_initStaticDCtx(scratch->iov_base, scratch->iov_len);
	else
#endif
		dctxDeleter.SetTo(dctx = ZSTD_createDCtx());

	size_t zstdError = ZSTD_decompressDCtx(dctx,
		output.iov_base, output.iov_len,
		input.iov_base, input.iov_len);
	if (ZSTD_isError(zstdError))
		return _TranslateZstdError(zstdError);

	output.iov_len = zstdError;
	return B_OK;
#else
	return B_NOT_SUPPORTED;
#endif
}


/*static*/ status_t
BZstdCompressionAlgorithm::_TranslateZstdError(size_t error)
{
#ifdef ZSTD_ENABLED
	switch (ZSTD_getErrorCode(error)) {
		case ZSTD_error_no_error:
			return B_OK;
		case ZSTD_error_seekableIO:
			return B_BAD_VALUE;
		case ZSTD_error_corruption_detected:
		case ZSTD_error_checksum_wrong:
			return B_BAD_DATA;
		case ZSTD_error_version_unsupported:
			return B_BAD_VALUE;
		case ZSTD_error_memory_allocation:
			return B_NO_MEMORY;
		case ZSTD_error_dstSize_tooSmall:
			return B_BUFFER_OVERFLOW;
		default:
			return B_ERROR;
	}
#else
	return B_NOT_SUPPORTED;
#endif
}
