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
 *   Originally distributed under permissive terms by the Haiku project.
 *   See RFC 1035 for the protocol implemented here.
 */


/**
 * @file DNSQuery.cpp
 * @brief Implements BRawNetBuffer, DNSTools, and DNSQuery for the
 *        Mail auto-config wizard.
 *
 * Provides just enough of the RFC 1035 protocol to send a single MX query
 * over UDP, decode the response, and surface the highest-priority mail
 * exchange for a domain.
 */


#include "DNSQuery.h"

#include <errno.h>
#include <stdio.h>

#include <ByteOrder.h>
#include <FindDirectory.h>
#include <NetAddress.h>
#include <NetEndpoint.h>
#include <Path.h>

// #define DEBUG 1

#undef PRINT
#ifdef DEBUG
#define PRINT(a...) printf(a)
#else
#define PRINT(a...)
#endif


/** @brief Monotonically increasing process-wide DNS query identifier. */
static int32 gID = 1;


/** @brief Constructs an empty buffer with both read and write cursors at
           offset zero. */
BRawNetBuffer::BRawNetBuffer()
{
	_Init(NULL, 0);
}


/**
 * @brief Constructs an empty buffer pre-allocated to @a size bytes.
 *
 * @param size  Initial allocation in bytes.
 */
BRawNetBuffer::BRawNetBuffer(off_t size)
{
	_Init(NULL, 0);
	fBuffer.SetSize(size);
}


/**
 * @brief Constructs a buffer initialised from @a buf.
 *
 * @param buf   Source bytes copied into the buffer; may be @c NULL.
 * @param size  Number of bytes to copy from @a buf.
 */
BRawNetBuffer::BRawNetBuffer(const void* buf, size_t size)
{
	_Init(buf, size);
}


/**
 * @brief Appends @a value to the buffer in big-endian (network) byte order.
 *
 * @param value  16-bit integer to append.
 * @retval B_OK         Bytes written and the write cursor advanced.
 * @retval B_NO_MEMORY  Underlying BMallocIO could not grow.
 */
status_t
BRawNetBuffer::AppendUint16(uint16 value)
{
	uint16 netVal = B_HOST_TO_BENDIAN_INT16(value);
	ssize_t sizeW = fBuffer.WriteAt(fWritePosition, &netVal, sizeof(uint16));
	if (sizeW == B_NO_MEMORY)
		return B_NO_MEMORY;
	fWritePosition += sizeof(uint16);
	return B_OK;
}


/**
 * @brief Appends a NUL-terminated C string verbatim including the
 *        terminator.
 *
 * @param string  String to append; must not be @c NULL.
 * @retval B_OK         Bytes written and the write cursor advanced.
 * @retval B_NO_MEMORY  Underlying BMallocIO could not grow.
 */
status_t
BRawNetBuffer::AppendString(const char* string)
{
	size_t length = strlen(string) + 1;
	ssize_t sizeW = fBuffer.WriteAt(fWritePosition, string, length);
	if (sizeW == B_NO_MEMORY)
		return B_NO_MEMORY;
	fWritePosition += length;
	return B_OK;
}


/**
 * @brief Reads a big-endian 16-bit integer at the current read cursor and
 *        advances it.
 *
 * @param value  Output: host-byte-order integer; only valid on @c B_OK.
 * @retval B_OK     Two bytes consumed.
 * @retval B_ERROR  Buffer was already at EOF.
 */
status_t
BRawNetBuffer::ReadUint16(uint16& value)
{
	uint16 netVal;
	ssize_t sizeW = fBuffer.ReadAt(fReadPosition, &netVal, sizeof(uint16));
	if (sizeW == 0)
		return B_ERROR;
	value= B_BENDIAN_TO_HOST_INT16(netVal);
	fReadPosition += sizeof(uint16);
	return B_OK;
}


/**
 * @brief Reads a big-endian 32-bit integer at the current read cursor and
 *        advances it.
 *
 * @param value  Output: host-byte-order integer; only valid on @c B_OK.
 * @retval B_OK     Four bytes consumed.
 * @retval B_ERROR  Buffer was already at EOF.
 */
status_t
BRawNetBuffer::ReadUint32(uint32& value)
{
	uint32 netVal;
	ssize_t sizeW = fBuffer.ReadAt(fReadPosition, &netVal, sizeof(uint32));
	if (sizeW == 0)
		return B_ERROR;
	value= B_BENDIAN_TO_HOST_INT32(netVal);
	fReadPosition += sizeof(uint32);
	return B_OK;
}


/**
 * @brief Reads a length-prefixed (and possibly compressed) DNS name out of
 *        the buffer at the current read cursor.
 *
 * Honours the RFC 1035 message-compression scheme: a leading byte of @c
 * 192 signals a back-pointer to an earlier name in the same packet.
 *
 * @param string  Output: decoded ASCII name.
 * @retval B_OK     Name read and the cursor advanced past the on-wire
 *                  bytes.
 * @retval B_ERROR  Buffer ended before the name terminated.
 */
status_t
BRawNetBuffer::ReadString(BString& string)
{
	string = "";
	ssize_t bytesRead = _ReadStringAt(string, fReadPosition);
	if (bytesRead < 0)
		return B_ERROR;
	fReadPosition += bytesRead;
	return B_OK;
}


/**
 * @brief Advances the read cursor by @a skip bytes without consuming them
 *        into a value.
 *
 * @param skip  Number of bytes to skip.
 * @retval B_OK     Cursor advanced.
 * @retval B_ERROR  The skip would run past the end of the buffer.
 */
status_t
BRawNetBuffer::SkipReading(off_t skip)
{
	if (fReadPosition + skip > (off_t)fBuffer.BufferLength())
		return B_ERROR;
	fReadPosition += skip;
	return B_OK;
}


/**
 * @brief Common constructor helper that resets cursors and seeds the
 *        underlying BMallocIO with @a buf.
 *
 * @param buf   Initial bytes; may be @c NULL when @a size is zero.
 * @param size  Number of bytes to copy from @a buf.
 */
void
BRawNetBuffer::_Init(const void* buf, size_t size)
{
	fWritePosition = 0;
	fReadPosition = 0;
	fBuffer.WriteAt(fWritePosition, buf, size);
}


/**
 * @brief Recursive helper that decodes a DNS name starting at absolute
 *        offset @a pos, following compression pointers.
 *
 * @param string  Accumulator for the decoded characters.
 * @param pos     Absolute offset into the buffer where the name starts.
 * @return Number of bytes consumed by the name in the on-wire stream
 *         (including the terminator or the compression pointer), or @c -1
 *         when @a pos is past the end of the buffer.
 */
ssize_t
BRawNetBuffer::_ReadStringAt(BString& string, off_t pos)
{
	if (pos >= (off_t)fBuffer.BufferLength())
		return -1;

	ssize_t bytesRead = 0;
	char* buffer = (char*)fBuffer.Buffer();
	buffer = &buffer[pos];
	// if the string is compressed we have to follow the links to the
	// sub strings
	while (pos < (off_t)fBuffer.BufferLength() && *buffer != 0) {
		if (uint8(*buffer) == 192) {
			// found a pointer mark
			buffer++;
			bytesRead++;
			off_t subPos = uint8(*buffer);
			_ReadStringAt(string, subPos);
			break;
		}
		string.Append(buffer, 1);
		buffer++;
		bytesRead++;
	}
	bytesRead++;
	return bytesRead;
}


// #pragma mark - DNSTools


/**
 * @brief Parses @c /system/settings/network/resolv.conf and copies up to
 *        two nameserver addresses into @a serverList.
 *
 * Comments (lines starting with @c ';' or @c '#') are skipped and only
 * the first two @c "nameserver" lines are kept.
 *
 * @param serverList  Owning list filled with newly allocated BString
 *                    addresses (caller frees the entries via the list's
 *                    own ownership semantics).
 * @retval B_OK              File was opened and parsed; the list may still
 *                           be empty if no nameservers were configured.
 * @retval B_ENTRY_NOT_FOUND The settings directory or @c resolv.conf
 *                           could not be located/opened.
 * @todo Stop hand-parsing @c resolv.conf once a public DNS-list API
 *       exists.
 */
status_t
DNSTools::GetDNSServers(BObjectList<BString, true>* serverList)
{
	// TODO: reading resolv.conf ourselves shouldn't be needed.
	// we should have some function to retrieve the dns list
#define	MATCH(line, name) \
	(!strncmp(line, name, sizeof(name) - 1) && \
	(line[sizeof(name) - 1] == ' ' || \
	 line[sizeof(name) - 1] == '\t'))

	BPath path;
	if (find_directory(B_SYSTEM_SETTINGS_DIRECTORY, &path) != B_OK)
		return B_ENTRY_NOT_FOUND;

	path.Append("network/resolv.conf");

	FILE* fp = fopen(path.Path(), "r");
	if (fp == NULL) {
		fprintf(stderr, "failed to open '%s' to read nameservers: %s\n",
			path.Path(), strerror(errno));
		return B_ENTRY_NOT_FOUND;
	}

	int nserv = 0;
	char buf[1024];
	char *cp; //, **pp;
	int MAXNS = 2;

	// read the config file
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		// skip comments
		if (*buf == ';' || *buf == '#')
			continue;

		// read nameservers to query
		if (MATCH(buf, "nameserver") && nserv < MAXNS) {
//			char sbuf[2];
			cp = buf + sizeof("nameserver") - 1;
			while (*cp == ' ' || *cp == '\t')
				cp++;
			cp[strcspn(cp, ";# \t\n")] = '\0';
			if ((*cp != '\0') && (*cp != '\n')) {
				serverList->AddItem(new BString(cp));
				nserv++;
			}
		}
		continue;
	}

	fclose(fp);
	
	return B_OK;
}


/**
 * @brief Converts a dotted DNS name into the length-prefixed wire form.
 *
 * "www.example.com" becomes "3www7example3com" with each dot replaced by
 * the byte count of the following label.
 *
 * @param string  Dotted source name.
 * @return Length-prefixed encoding suitable for the question section of a
 *         DNS request.
 */
BString
DNSTools::ConvertToDNSName(const BString& string)
{
	BString outString = string;
	int32 dot, lastDot, diff;

	dot = string.FindFirst(".");
	if (dot != B_ERROR) {
		outString.Prepend((char*)&dot, 1);
		// because we prepend a char add 1 more
		lastDot = dot + 1;

		while (true) {
			dot = outString.FindFirst(".", lastDot + 1);
			if (dot == B_ERROR)
				break;

			// set a counts to the dot
			diff =  dot - 1 - lastDot;
			outString.SetByteAt(lastDot, (char)diff);
			lastDot = dot;
		}
	} else
		lastDot = 0;

	diff = outString.CountChars() - 1 - lastDot;
	outString.SetByteAt(lastDot, (char)diff);

	return outString;
}


/**
 * @brief Inverse of ConvertToDNSName(): turns a length-prefixed wire-form
 *        name back into dotted notation.
 *
 * @param string  Length-prefixed source name (without compression
 *                pointers).
 * @return Dotted human-readable name; the same value if @a string is
 *         empty.
 */
BString
DNSTools::ConvertFromDNSName(const BString& string)
{
	if (string.Length() == 0)
		return string;

	BString outString = string;
	int32 dot = string[0];
	int32 nextDot = dot;
	outString.Remove(0, sizeof(char));
	while (true) {
		if (nextDot >= outString.Length())
			break;
		dot = outString[nextDot];
		if (dot == 0)
			break;
		// set a "."
		outString.SetByteAt(nextDot, '.');
		nextDot+= dot + 1;
	}
	return outString;
}


// #pragma mark - DNSQuery
// see http://tools.ietf.org/html/rfc1035 for more information about DNS


/** @brief Constructs an idle DNSQuery; no socket is opened until
           GetMXRecords() is called. */
DNSQuery::DNSQuery()
{
}


/** @brief Trivial destructor; sockets are owned by the call-scoped
           BNetEndpoint inside GetMXRecords(). */
DNSQuery::~DNSQuery()
{
}


/**
 * @brief Looks up the first nameserver from resolv.conf and decodes it
 *        into @a add.
 *
 * @param add  Output: numeric IPv4 address of the first nameserver. Only
 *             valid on @c B_OK.
 * @retval B_OK     A nameserver was read and decoded.
 * @retval (other)  Whatever DNSTools::GetDNSServers() returned, or
 *                  @c B_ERROR when the address failed to parse.
 */
status_t
DNSQuery::ReadDNSServer(in_addr* add)
{
	// list owns the items
	BObjectList<BString, true> dnsServerList(5);
	status_t status = DNSTools::GetDNSServers(&dnsServerList);
	if (status != B_OK)
		return status;
		
	BString* firstDNS = dnsServerList.ItemAt(0);
	if (firstDNS == NULL || inet_aton(firstDNS->String(), add) != 1)
		return B_ERROR;

	PRINT("dns server found: %s \n", firstDNS->String());
	return B_OK;
}


/**
 * @brief Sends a single MX query for @a serverName over UDP and decodes
 *        the answer into @a mxList.
 *
 * Constructs a recursion-desired query, sends it to the first system
 * nameserver, then walks the answer section keeping only records whose
 * type is @c MX_RECORD.
 *
 * @param serverName  Domain whose MX records are wanted.
 * @param mxList      Owning list that receives newly-allocated mx_record
 *                    entries; the highest-priority entry is at index 0
 *                    when more than one is returned in priority order.
 * @param timeout     Receive-timeout in microseconds; defaults to
 *                    500 ms.
 * @retval B_OK     At least one MX record was found and added to
 *                  @a mxList.
 * @retval B_ERROR  No nameserver, no socket, no answer, or no MX record
 *                  in the answer.
 */
status_t
DNSQuery::GetMXRecords(const BString&  serverName,
	BObjectList<mx_record, true>* mxList, bigtime_t timeout)
{
	// get the DNS server to ask for the mx record
	in_addr dnsAddress;
	if (ReadDNSServer(&dnsAddress) != B_OK)
		return B_ERROR;

	// create dns query package
	BRawNetBuffer buffer;
	dns_header header;
	_SetMXHeader(&header);
	_AppendQueryHeader(buffer, &header);

	BString serverNameConv = DNSTools::ConvertToDNSName(serverName);
	buffer.AppendString(serverNameConv);
	buffer.AppendUint16(uint16(MX_RECORD));
	buffer.AppendUint16(uint16(1));

	// send the buffer
	PRINT("send buffer\n");
	BNetAddress netAddress(dnsAddress, 53);
	BNetEndpoint netEndpoint(SOCK_DGRAM);
	if (netEndpoint.InitCheck() != B_OK)
		return B_ERROR;

	if (netEndpoint.Connect(netAddress) != B_OK)
		return B_ERROR;
	PRINT("Connected\n");

	int32 bytesSend = netEndpoint.Send(buffer.Data(), buffer.Size());
	if (bytesSend == B_ERROR)
		return B_ERROR;
	PRINT("bytes send %i\n", int(bytesSend));

	// receive buffer
	BRawNetBuffer receiBuffer(512);
	netEndpoint.SetTimeout(timeout);

	int32 bytesRecei = netEndpoint.ReceiveFrom(receiBuffer.Data(), 512,
		netAddress);
	if (bytesRecei == B_ERROR)
		return B_ERROR;
	PRINT("bytes received %i\n", int(bytesRecei));

	dns_header receiHeader;

	_ReadQueryHeader(receiBuffer, &receiHeader);
	PRINT("Package contains :");
	PRINT("%d Questions, ", receiHeader.q_count);
	PRINT("%d Answers, ", receiHeader.ans_count);
	PRINT("%d Authoritative Servers, ", receiHeader.auth_count);
	PRINT("%d Additional records\n", receiHeader.add_count);

	// remove name and Question
	BString dummyS;
	uint16 dummy;
	receiBuffer.ReadString(dummyS);
	receiBuffer.ReadUint16(dummy);
	receiBuffer.ReadUint16(dummy);

	bool mxRecordFound = false;
	for (int i = 0; i < receiHeader.ans_count; i++) {
		resource_record_head rrHead;
		_ReadResourceRecord(receiBuffer, &rrHead);
		if (rrHead.type == MX_RECORD) {
			mx_record* mxRec = new mx_record;
			_ReadMXRecord(receiBuffer, mxRec);
			PRINT("MX record found pri %i, name %s\n",
				mxRec->priority, mxRec->serverName.String());
			// Add mx record to the list
			mxList->AddItem(mxRec);
			mxRecordFound = true;
		} else {
			buffer.SkipReading(rrHead.dataLength);
		}
	}

	if (!mxRecordFound)
		return B_ERROR;

	return B_OK;
}


/**
 * @brief Atomically allocates a fresh DNS query identifier.
 *
 * Wraps back to zero before approaching the 16-bit ceiling so a long-
 * running session does not collide with the upper-reserved range.
 *
 * @return New 16-bit query ID.
 */
uint16
DNSQuery::_GetUniqueID()
{
	int32 nextId= atomic_add(&gID, 1);
	// just to be sure
	if (nextId > 65529)
		nextId = 0;
	return nextId;
}


/**
 * @brief Fills @a header with the flag set used by the wizard's MX
 *        lookups: standard query, recursion desired, one question, no
 *        answers.
 *
 * @param header  dns_header to populate; must not be @c NULL.
 */
void
DNSQuery::_SetMXHeader(dns_header* header)
{
	header->id = _GetUniqueID();
	header->qr = 0;      //This is a query
	header->opcode = 0;  //This is a standard query
	header->aa = 0;      //Not Authoritative
	header->tc = 0;      //This message is not truncated
	header->rd = 1;      //Recursion Desired
	header->ra = 0;      //Recursion not available! hey we dont have it (lol)
	header->z  = 0;
	header->rcode = 0;
	header->q_count = 1;   //we have only 1 question
	header->ans_count  = 0;
	header->auth_count = 0;
	header->add_count  = 0;
}


/**
 * @brief Serialises @a header to @a buffer in the wire layout expected by
 *        the resolver.
 *
 * Packs the flag bitfields into a single 16-bit word in the order defined
 * by RFC 1035 section 4.1.1.
 *
 * @param buffer  Destination raw buffer; the write cursor advances past
 *                the header.
 * @param header  Source header; must not be @c NULL.
 */
void
DNSQuery::_AppendQueryHeader(BRawNetBuffer& buffer, const dns_header* header)
{
	buffer.AppendUint16(header->id);
	uint16 data = 0;
	data |= header->rcode;
	data |= header->z << 4;
	data |= header->ra << 7;
	data |= header->rd << 8;
	data |= header->tc << 9;
	data |= header->aa << 10;
	data |= header->opcode << 11;
	data |= header->qr << 15;
	buffer.AppendUint16(data);
	buffer.AppendUint16(header->q_count);
	buffer.AppendUint16(header->ans_count);
	buffer.AppendUint16(header->auth_count);
	buffer.AppendUint16(header->add_count);
}


/**
 * @brief Inverse of _AppendQueryHeader(): unpacks a wire-format DNS header
 *        out of @a buffer into @a header.
 *
 * @param buffer  Source raw buffer; the read cursor advances past the
 *                header.
 * @param header  Destination header; must not be @c NULL.
 */
void
DNSQuery::_ReadQueryHeader(BRawNetBuffer& buffer, dns_header* header)
{
	buffer.ReadUint16(header->id);
	uint16 data = 0;
	buffer.ReadUint16(data);
	header->rcode = data & 0x0F;
	header->z = (data >> 4) & 0x07;
	header->ra = (data >> 7) & 0x01;
	header->rd = (data >> 8) & 0x01;
	header->tc = (data >> 9) & 0x01;
	header->aa = (data >> 10) & 0x01;
	header->opcode = (data >> 11) & 0x0F;
	header->qr = (data >> 15) & 0x01;
	buffer.ReadUint16(header->q_count);
	buffer.ReadUint16(header->ans_count);
	buffer.ReadUint16(header->auth_count);
	buffer.ReadUint16(header->add_count);
}


/**
 * @brief Decodes a single MX answer body (preference + exchange name) out
 *        of @a buffer into @a mxRecord.
 *
 * @param buffer    Source raw buffer; the read cursor advances past the
 *                  record body.
 * @param mxRecord  Destination record; must not be @c NULL.
 */
void
DNSQuery::_ReadMXRecord(BRawNetBuffer& buffer, mx_record* mxRecord)
{
	buffer.ReadUint16(mxRecord->priority);
	buffer.ReadString(mxRecord->serverName);
	mxRecord->serverName = DNSTools::ConvertFromDNSName(mxRecord->serverName);
}


/**
 * @brief Decodes the fixed-size header preceding a resource record's
 *        type-specific data.
 *
 * @param buffer  Source raw buffer; the read cursor advances past the RR
 *                header.
 * @param rrHead  Destination structure; must not be @c NULL.
 */
void
DNSQuery::_ReadResourceRecord(BRawNetBuffer& buffer,
	resource_record_head *rrHead)
{
	buffer.ReadString(rrHead->name);
	buffer.ReadUint16(rrHead->type);
	buffer.ReadUint16(rrHead->dataClass);
	buffer.ReadUint32(rrHead->ttl);
	buffer.ReadUint16(rrHead->dataLength);
}
