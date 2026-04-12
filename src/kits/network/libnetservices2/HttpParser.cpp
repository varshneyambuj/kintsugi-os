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
 *   Copyright 2022 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Niels Sascha Reedijk, niels.reedijk@gmail.com
 */


/**
 * @file HttpParser.cpp
 * @brief HTTP response parser: status line, header fields, and body variants.
 *
 * HttpParser drives incremental parsing of an HTTP/1.1 response from an
 * HttpBuffer.  It handles the status line, header fields, and three body
 * transfer modes: fixed-length, variable-length (connection-close), and
 * chunked.  An optional decompression layer wraps the raw parsers when a
 * Content-Encoding header is present.
 *
 * @see HttpBuffer, BHttpSession
 */


#include "HttpParser.h"

#include <stdexcept>
#include <string>

#include <HttpFields.h>
#include <NetServicesDefs.h>
#include <ZlibCompressionAlgorithm.h>

using namespace std::literals;
using namespace BPrivate::Network;


// #pragma mark -- HttpParser


/**
 * @brief Mark the response as having no body content.
 *
 * Must be called before body parsing begins (i.e. while still in the Fields
 * parse state).  Used for HEAD responses and similar cases where the server
 * sends headers only.
 */
void
HttpParser::SetNoContent() noexcept
{
	if (fStreamState > HttpInputStreamState::Fields)
		debugger("Cannot set the parser to no content after parsing of the body has started");
	fBodyType = HttpBodyType::NoContent;
};


/**
 * @brief Parse the HTTP status line from \a buffer and store it in \a status.
 *
 * Reads one CRLF-terminated line, extracts the three-digit status code and
 * the full status text, then advances the internal state to Fields.
 *
 * @param buffer  HttpBuffer containing incoming response data.
 * @param status  Output structure populated with the parsed status code and text.
 * @return true if the status line was successfully parsed; false if more data is needed.
 */
bool
HttpParser::ParseStatus(HttpBuffer& buffer, BHttpStatus& status)
{
	if (fStreamState != HttpInputStreamState::StatusLine)
		debugger("The Status line has already been parsed");

	auto statusLine = buffer.GetNextLine();
	if (!statusLine)
		return false;

	auto codeStart = statusLine->FindFirst(' ') + 1;
	if (codeStart < 0)
		throw BNetworkRequestError(__PRETTY_FUNCTION__, BNetworkRequestError::ProtocolError);

	auto codeEnd = statusLine->FindFirst(' ', codeStart);

	if (codeEnd < 0 || (codeEnd - codeStart) != 3)
		throw BNetworkRequestError(__PRETTY_FUNCTION__, BNetworkRequestError::ProtocolError);

	std::string statusCodeString(statusLine->String() + codeStart, 3);

	// build the output
	try {
		status.code = std::stol(statusCodeString);
	} catch (...) {
		throw BNetworkRequestError(__PRETTY_FUNCTION__, BNetworkRequestError::ProtocolError);
	}

	status.text = std::move(statusLine.value());
	fStatus.code = status.code; // cache the status code
	fStreamState = HttpInputStreamState::Fields;
	return true;
}


/**
 * @brief Incrementally parse HTTP header fields from \a buffer into \a fields.
 *
 * Reads complete CRLF-terminated field lines until the blank line terminating
 * the header section is encountered.  After all fields are parsed, determines
 * the body type (NoContent, FixedSize, Chunked, or VariableSize) and
 * optionally wraps the body parser in a decompression layer.
 *
 * @param buffer  HttpBuffer containing incoming response data.
 * @param fields  Output BHttpFields populated with parsed header fields.
 * @return true if all header fields were parsed; false if more data is needed.
 */
bool
HttpParser::ParseFields(HttpBuffer& buffer, BHttpFields& fields)
{
	if (fStreamState != HttpInputStreamState::Fields)
		debugger("The parser is not expecting header fields at this point");

	auto fieldLine = buffer.GetNextLine();

	while (fieldLine && !fieldLine.value().IsEmpty()) {
		// Parse next header line
		fields.AddField(fieldLine.value());
		fieldLine = buffer.GetNextLine();
	}

	if (!fieldLine || (fieldLine && !fieldLine.value().IsEmpty())) {
		// there is more to parse
		return false;
	}

	// Determine the properties for the body
	// RFC 7230 section 3.3.3 has a prioritized list of 7 rules around determining the body:
	std::optional<off_t> bodyBytesTotal = std::nullopt;
	if (fBodyType == HttpBodyType::NoContent || fStatus.StatusCode() == BHttpStatusCode::NoContent
		|| fStatus.StatusCode() == BHttpStatusCode::NotModified) {
		// [1] In case of HEAD (set previously), status codes 1xx (TODO!), status code 204 or 304,
		// no content [2] NOT SUPPORTED: when doing a CONNECT request, no content
		fBodyType = HttpBodyType::NoContent;
		fStreamState = HttpInputStreamState::Done;
	} else if (auto header = fields.FindField("Transfer-Encoding"sv);
			   header != fields.end() && header->Value() == "chunked"sv) {
		// [3] If there is a Transfer-Encoding heading set to 'chunked'
		// TODO: support the more advanced rules in the RFC around the meaning of this field
		fBodyType = HttpBodyType::Chunked;
		fStreamState = HttpInputStreamState::Body;
	} else if (fields.CountFields("Content-Length"sv) > 0) {
		// [4] When there is no Transfer-Encoding, then look for Content-Encoding:
		//	- If there are more than one, the values must match
		//	- The value must be a valid number
		// [5] If there is a valid value, then that is the expected size of the body
		try {
			auto contentLength = std::string();
			for (const auto& field: fields) {
				if (field.Name() == "Content-Length"sv) {
					if (contentLength.size() == 0)
						contentLength = field.Value();
					else if (contentLength != field.Value()) {
						throw BNetworkRequestError(__PRETTY_FUNCTION__,
							BNetworkRequestError::ProtocolError,
							"Multiple Content-Length fields with differing values");
					}
				}
			}
			bodyBytesTotal = std::stol(contentLength);
			if (*bodyBytesTotal == 0) {
				fBodyType = HttpBodyType::NoContent;
				fStreamState = HttpInputStreamState::Done;
			} else {
				fBodyType = HttpBodyType::FixedSize;
				fStreamState = HttpInputStreamState::Body;
			}
		} catch (const std::logic_error& e) {
			throw BNetworkRequestError(__PRETTY_FUNCTION__, BNetworkRequestError::ProtocolError,
				"Cannot parse Content-Length field value (logic_error)");
		}
	} else {
		// [6] Applies to request messages only (this is a response)
		// [7] If nothing else then the received message is all data until connection close
		// (this is the default)
		fStreamState = HttpInputStreamState::Body;
	}

	// Set up the body parser based on the logic above.
	switch (fBodyType) {
		case HttpBodyType::VariableSize:
			fBodyParser = std::make_unique<HttpRawBodyParser>();
			break;
		case HttpBodyType::FixedSize:
			fBodyParser = std::make_unique<HttpRawBodyParser>(*bodyBytesTotal);
			break;
		case HttpBodyType::Chunked:
			fBodyParser = std::make_unique<HttpChunkedBodyParser>();
			break;
		case HttpBodyType::NoContent:
		default:
			return true;
	}

	// Check Content-Encoding for compression
	auto header = fields.FindField("Content-Encoding"sv);
	if (header != fields.end() && (header->Value() == "gzip" || header->Value() == "deflate")) {
		fBodyParser = std::make_unique<HttpBodyDecompression>(std::move(fBodyParser));
	}

	return true;
}


/**
 * @brief Parse body bytes from \a buffer using the configured body parser.
 *
 * Delegates to the active HttpBodyParser and advances to Done state when
 * the parser reports completion.
 *
 * @param buffer       HttpBuffer containing incoming response data.
 * @param writeToBody  Function called with each chunk of parsed body bytes.
 * @param readEnd      true if \a buffer contains the last bytes of this response.
 * @return Number of bytes consumed from \a buffer during this call.
 */
size_t
HttpParser::ParseBody(HttpBuffer& buffer, HttpTransferFunction writeToBody, bool readEnd)
{
	if (fStreamState < HttpInputStreamState::Body || fStreamState == HttpInputStreamState::Done)
		debugger("The parser is not in the correct state to parse a body");

	auto parseResult = fBodyParser->ParseBody(buffer, writeToBody, readEnd);

	if (parseResult.complete)
		fStreamState = HttpInputStreamState::Done;

	return parseResult.bytesParsed;
}


/**
 * @brief Return whether the response is expected to contain body content.
 *
 * May change after ParseFields() determines that the body type is NoContent.
 *
 * @return true if the response should have a body.
 */
bool
HttpParser::HasContent() const noexcept
{
	return fBodyType != HttpBodyType::NoContent;
}


/**
 * @brief Return the total body size if known from Content-Length.
 *
 * @return Optional off_t byte count, or std::nullopt for chunked/variable bodies.
 */
std::optional<off_t>
HttpParser::BodyBytesTotal() const noexcept
{
	if (fBodyParser)
		return fBodyParser->TotalBodySize();
	return std::nullopt;
}


/**
 * @brief Return the number of body bytes transferred so far.
 *
 * @return Byte count of body data written to the target function.
 */
off_t
HttpParser::BodyBytesTransferred() const noexcept
{
	if (fBodyParser)
		return fBodyParser->TransferredBodySize();
	return 0;
}


/**
 * @brief Return whether the parser has finished processing the response.
 *
 * @return true when the internal state machine has reached Done.
 */
bool
HttpParser::Complete() const noexcept
{
	return fStreamState == HttpInputStreamState::Done;
}


// #pragma mark -- HttpBodyParser


/**
 * @brief Default implementation that returns std::nullopt (unknown body size).
 *
 * @return std::nullopt.
 */
std::optional<off_t>
HttpBodyParser::TotalBodySize() const noexcept
{
	return std::nullopt;
}


/**
 * @brief Return the number of body bytes transferred from the stream so far.
 *
 * For chunked transfers this excludes chunk headers and other framing bytes.
 *
 * @return Number of body bytes written to the target.
 */
off_t
HttpBodyParser::TransferredBodySize() const noexcept
{
	return fTransferredBodySize;
}


// #pragma mark -- HttpRawBodyParser

/**
 * @brief Construct an HttpRawBodyParser with an unknown total body size.
 */
HttpRawBodyParser::HttpRawBodyParser()
{
}


/**
 * @brief Construct an HttpRawBodyParser expecting exactly \a bodyBytesTotal bytes.
 *
 * @param bodyBytesTotal  Expected total body size in bytes as from Content-Length.
 */
HttpRawBodyParser::HttpRawBodyParser(off_t bodyBytesTotal)
	:
	fBodyBytesTotal(bodyBytesTotal)
{
}


/**
 * @brief Parse a raw (non-chunked) body from \a buffer.
 *
 * Copies up to the expected remaining bytes (or all available bytes if the
 * total is unknown) to \a writeToBody.  If \a readEnd is true and fewer bytes
 * than expected have arrived, a ProtocolError is raised.  Partial writes by
 * \a writeToBody are treated as fatal SystemError.
 *
 * @param buffer       HttpBuffer containing incoming response data.
 * @param writeToBody  Function called with each chunk of parsed body bytes.
 * @param readEnd      true if the buffer contains the last bytes of this response.
 * @return BodyParseResult with byte counts and completion flag.
 */
BodyParseResult
HttpRawBodyParser::ParseBody(HttpBuffer& buffer, HttpTransferFunction writeToBody, bool readEnd)
{
	auto bytesToRead = buffer.RemainingBytes();
	if (fBodyBytesTotal) {
		auto expectedRemainingBytes = *fBodyBytesTotal - fTransferredBodySize;
		if (expectedRemainingBytes < static_cast<off_t>(buffer.RemainingBytes()))
			bytesToRead = expectedRemainingBytes;
		else if (readEnd && expectedRemainingBytes > static_cast<off_t>(buffer.RemainingBytes())) {
			throw BNetworkRequestError(__PRETTY_FUNCTION__, BNetworkRequestError::ProtocolError,
				"Message body is incomplete; less data received than expected");
		}
	}

	// Copy the data
	auto bytesRead = buffer.WriteTo(writeToBody, bytesToRead);
	fTransferredBodySize += bytesRead;

	if (bytesRead != bytesToRead) {
		// Fail if not all expected bytes are written.
		throw BNetworkRequestError(__PRETTY_FUNCTION__, BNetworkRequestError::SystemError,
			"Could not write all available body bytes to the target.");
	}

	if (fBodyBytesTotal) {
		if (*fBodyBytesTotal == fTransferredBodySize)
			return {bytesRead, bytesRead, true};
		else
			return {bytesRead, bytesRead, false};
	} else
		return {bytesRead, bytesRead, readEnd};
}


/**
 * @brief Return the known total body size, or std::nullopt if unknown.
 *
 * @return Optional off_t as set by the Content-Length constructor.
 */
std::optional<off_t>
HttpRawBodyParser::TotalBodySize() const noexcept
{
	return fBodyBytesTotal;
}


// #pragma mark -- HttpChunkedBodyParser

/**
 * @brief Parse a chunked-transfer-encoded body from \a buffer.
 *
 * Processes one or more chunks from the buffer, copying each chunk's data to
 * \a writeToBody.  Handles the chunk-size header, chunk data, chunk-end CRLF,
 * and trailers.  Waits for more data if the current chunk header or terminator
 * is incomplete in the buffer.
 *
 * @param buffer       HttpBuffer containing incoming response data.
 * @param writeToBody  Function called with each chunk of parsed body bytes.
 * @param readEnd      true if the buffer contains the last bytes of this response.
 * @return BodyParseResult with byte counts and completion flag.
 */
BodyParseResult
HttpChunkedBodyParser::ParseBody(HttpBuffer& buffer, HttpTransferFunction writeToBody, bool readEnd)
{
	size_t totalBytesRead = 0;
	while (buffer.RemainingBytes() > 0) {
		switch (fChunkParserState) {
			case ChunkSize:
			{
				// Read the next chunk size from the buffer; if unsuccesful wait for more data
				auto chunkSizeString = buffer.GetNextLine();
				if (!chunkSizeString)
					return {totalBytesRead, totalBytesRead, false};
				auto chunkSizeStr = std::string(chunkSizeString.value().String());
				try {
					size_t pos = 0;
					fRemainingChunkSize = std::stoll(chunkSizeStr, &pos, 16);
					if (pos < chunkSizeStr.size() && chunkSizeStr[pos] != ';') {
						throw BNetworkRequestError(
							__PRETTY_FUNCTION__, BNetworkRequestError::ProtocolError);
					}
				} catch (const std::invalid_argument&) {
					throw BNetworkRequestError(
						__PRETTY_FUNCTION__, BNetworkRequestError::ProtocolError);
				} catch (const std::out_of_range&) {
					throw BNetworkRequestError(
						__PRETTY_FUNCTION__, BNetworkRequestError::ProtocolError);
				}

				if (fRemainingChunkSize > 0)
					fChunkParserState = Chunk;
				else
					fChunkParserState = Trailers;
				break;
			}

			case Chunk:
			{
				size_t bytesToRead;
				if (fRemainingChunkSize > static_cast<off_t>(buffer.RemainingBytes()))
					bytesToRead = buffer.RemainingBytes();
				else
					bytesToRead = fRemainingChunkSize;

				auto bytesRead = buffer.WriteTo(writeToBody, bytesToRead);
				if (bytesRead != bytesToRead) {
					// Fail if not all expected bytes are written.
					throw BNetworkRequestError(__PRETTY_FUNCTION__,
						BNetworkRequestError::SystemError,
						"Could not write all available body bytes to the target.");
				}

				fTransferredBodySize += bytesRead;
				totalBytesRead += bytesRead;
				fRemainingChunkSize -= bytesRead;
				if (fRemainingChunkSize == 0)
					fChunkParserState = ChunkEnd;
				break;
			}

			case ChunkEnd:
			{
				if (buffer.RemainingBytes() < 2) {
					// not enough data in the buffer to finish the chunk
					return {totalBytesRead, totalBytesRead, false};
				}
				auto chunkEndString = buffer.GetNextLine();
				if (!chunkEndString || chunkEndString.value().Length() != 0) {
					// There should have been an empty chunk
					throw BNetworkRequestError(
						__PRETTY_FUNCTION__, BNetworkRequestError::ProtocolError);
				}

				fChunkParserState = ChunkSize;
				break;
			}

			case Trailers:
			{
				auto trailerString = buffer.GetNextLine();
				if (!trailerString) {
					// More data to come
					return {totalBytesRead, totalBytesRead, false};
				}

				if (trailerString.value().Length() > 0) {
					// Ignore empty trailers for now
					// TODO: review if the API should support trailing headers
				} else {
					fChunkParserState = Complete;
					return {totalBytesRead, totalBytesRead, true};
				}
				break;
			}

			case Complete:
				return {totalBytesRead, totalBytesRead, true};
		}
	}
	return {totalBytesRead, totalBytesRead, false};
}


// #pragma mark -- HttpBodyDecompression

/**
 * @brief Construct a decompression wrapper around an existing body parser.
 *
 * Creates a Zlib decompressing output stream that buffers decompressed bytes,
 * then wraps \a bodyParser so that all body data flows through decompression
 * before being forwarded to the write function.
 *
 * @param bodyParser  The underlying raw or chunked parser to wrap.
 */
HttpBodyDecompression::HttpBodyDecompression(std::unique_ptr<HttpBodyParser> bodyParser)
{
	fDecompressorStorage = std::make_unique<BMallocIO>();

	BDataIO* stream = nullptr;
	auto result = BZlibCompressionAlgorithm().CreateDecompressingOutputStream(
		fDecompressorStorage.get(), nullptr, stream);

	if (result != B_OK) {
		throw BNetworkRequestError("BZlibCompressionAlgorithm().CreateCompressingOutputStream",
			BNetworkRequestError::SystemError, result);
	}

	fDecompressingStream = std::unique_ptr<BDataIO>(stream);
	fBodyParser = std::move(bodyParser);
}


/**
 * @brief Decompress body bytes from \a buffer and forward them via \a writeToBody.
 *
 * Feeds the buffer through the underlying parser into the Zlib decompression
 * stream, then drains decompressed bytes to \a writeToBody.  If \a readEnd is
 * true the decompression stream is flushed to ensure all bytes are emitted.
 *
 * @param buffer       HttpBuffer containing incoming compressed response data.
 * @param writeToBody  Function called with each chunk of decompressed body bytes.
 * @param readEnd      true if the buffer contains the last bytes of this response.
 * @return BodyParseResult with compressed byte count and completion flag.
 */
BodyParseResult
HttpBodyDecompression::ParseBody(HttpBuffer& buffer, HttpTransferFunction writeToBody, bool readEnd)
{
	// Get the underlying raw or chunked parser to write data to our decompressionstream
	auto parseResults = fBodyParser->ParseBody(
		buffer,
		[this](const std::byte* buffer, size_t bufferSize) {
			auto status = fDecompressingStream->WriteExactly(buffer, bufferSize);
			if (status != B_OK) {
				throw BNetworkRequestError(
					"BDataIO::WriteExactly()", BNetworkRequestError::SystemError, status);
			}
			return bufferSize;
		},
		readEnd);
	fTransferredBodySize += parseResults.bytesParsed;

	if (readEnd || parseResults.complete) {
		// No more bytes expected so flush out the final bytes
		if (auto status = fDecompressingStream->Flush(); status != B_OK) {
			throw BNetworkRequestError(
				"BZlibDecompressionStream::Flush()", BNetworkRequestError::SystemError, status);
		}
	}

	size_t bytesWritten = 0;
	if (auto bodySize = fDecompressorStorage->Position(); bodySize > 0) {
		bytesWritten
			= writeToBody(static_cast<const std::byte*>(fDecompressorStorage->Buffer()), bodySize);
		if (static_cast<off_t>(bytesWritten) != bodySize) {
			throw BNetworkRequestError(
				__PRETTY_FUNCTION__, BNetworkRequestError::SystemError, B_PARTIAL_WRITE);
		}
		fDecompressorStorage->Seek(0, SEEK_SET);
	}
	return {parseResults.bytesParsed, bytesWritten, parseResults.complete};
}


/**
 * @brief Return the total body size from the underlying parser.
 *
 * @return Optional off_t from the wrapped parser's TotalBodySize().
 */
std::optional<off_t>
HttpBodyDecompression::TotalBodySize() const noexcept
{
	return fBodyParser->TotalBodySize();
}
