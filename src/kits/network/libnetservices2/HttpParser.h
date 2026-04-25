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
 * MIT License. Copyright 2022, Haiku Inc.
 */

/** @file HttpParser.h
    @brief Stateful HTTP/1.1 response parser plus the polymorphic body
           sub-parsers it switches between (raw, chunked, and decompressing). */

#ifndef _B_HTTP_PARSER_H_
#define _B_HTTP_PARSER_H_


#include <functional>
#include <optional>

#include <HttpResult.h>

#include "HttpBuffer.h"

class BMallocIO;

namespace BPrivate {

namespace Network {

/** @brief Callback that copies parsed body bytes into the consumer's sink. */
using HttpTransferFunction = std::function<size_t(const std::byte*, size_t)>;


/** @brief Phases of the inbound response parser. */
enum class HttpInputStreamState { StatusLine, Fields, Body, Done };


/** @brief Selects which HttpBodyParser subclass handles the response body. */
enum class HttpBodyType { NoContent, Chunked, FixedSize, VariableSize };


/** @brief Outcome of one ParseBody() call: bytes consumed, bytes emitted,
           and whether the body is complete. */
struct BodyParseResult {
			size_t		bytesParsed;
			size_t		bytesWritten;
			bool		complete;
};


class HttpBodyParser;


/** @brief Top-level HTTP/1.1 response parser driving the status line, header
           field, and body phases over an HttpBuffer feed. */
class HttpParser
{
public:
	/** @brief Construct a parser positioned at the start of the status line. */
								HttpParser(){};

	// Explicitly mark request as having no content
			void				SetNoContent() noexcept;

	// Parse data from response
			bool				ParseStatus(HttpBuffer& buffer, BHttpStatus& status);
			bool				ParseFields(HttpBuffer& buffer, BHttpFields& fields);
			size_t				ParseBody(HttpBuffer& buffer, HttpTransferFunction writeToBody,
									bool readEnd);
			/** @brief Current position within the response stream. */
			HttpInputStreamState State() const noexcept { return fStreamState; }

	// Details on the body status
			bool				HasContent() const noexcept;
			std::optional<off_t> BodyBytesTotal() const noexcept;
			off_t				BodyBytesTransferred() const noexcept;
			bool				Complete() const noexcept;

private:
			off_t				fHeaderBytes = 0;
			BHttpStatus			fStatus;
			HttpInputStreamState fStreamState = HttpInputStreamState::StatusLine;

	// Body
			HttpBodyType		fBodyType = HttpBodyType::VariableSize;
			std::unique_ptr<HttpBodyParser> fBodyParser = nullptr;
};


/** @brief Abstract base for body-parsing strategies (raw, chunked,
           decompressing). */
class HttpBodyParser
{
public:
	/** @brief Consume bytes from @a buffer, emit decoded bytes through
	    @a writeToBody, honouring @a readEnd to flag end-of-stream. */
	virtual						BodyParseResult ParseBody(HttpBuffer& buffer,
									HttpTransferFunction writeToBody, bool readEnd) = 0;

	virtual	std::optional<off_t> TotalBodySize() const noexcept;

			off_t				TransferredBodySize() const noexcept;

protected:
			off_t				fTransferredBodySize = 0;
};


/** @brief Body parser for unencoded responses, with optional fixed length
           supplied by Content-Length. */
class HttpRawBodyParser : public HttpBodyParser
{
public:
	/** @brief Construct a variable-length raw parser (read until close). */
								HttpRawBodyParser();
	/** @brief Construct a raw parser that expects exactly @a bodyBytesTotal bytes. */
								HttpRawBodyParser(off_t bodyBytesTotal);
	virtual	BodyParseResult		ParseBody(HttpBuffer& buffer, HttpTransferFunction writeToBody,
									bool readEnd) override;
	virtual	std::optional<off_t> TotalBodySize() const noexcept override;

private:
			std::optional<off_t> fBodyBytesTotal;
};


/** @brief Body parser for `Transfer-Encoding: chunked` responses. */
class HttpChunkedBodyParser : public HttpBodyParser
{
public:
	virtual BodyParseResult ParseBody(
		HttpBuffer& buffer, HttpTransferFunction writeToBody, bool readEnd) override;

private:
	enum { ChunkSize, ChunkEnd, Chunk, Trailers, Complete } fChunkParserState = ChunkSize;
	off_t fRemainingChunkSize = 0;
	bool fLastChunk = false;
};


/** @brief Decorator body parser that decompresses gzip/deflate output of a
           wrapped parser before forwarding bytes downstream. */
class HttpBodyDecompression : public HttpBodyParser
{
public:
	/** @brief Wrap @a bodyParser with a streaming decompressor. */
								HttpBodyDecompression(std::unique_ptr<HttpBodyParser> bodyParser);
	virtual	BodyParseResult		ParseBody(HttpBuffer& buffer, HttpTransferFunction writeToBody,
									bool readEnd) override;

	virtual	std::optional<off_t> TotalBodySize() const noexcept;

private:
			std::unique_ptr<HttpBodyParser> fBodyParser;
			std::unique_ptr<BMallocIO> fDecompressorStorage;
			std::unique_ptr<BDataIO> fDecompressingStream;
};


} // namespace Network

} // namespace BPrivate

#endif // _B_HTTP_PARSER_H_
