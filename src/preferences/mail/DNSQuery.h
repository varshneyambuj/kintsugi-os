/*
 * Copyright 2026, Kintsugi OS Contributors. All rights reserved.
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
 * Incorporates work originally distributed under permissive terms by the
 * Haiku project. See RFC 1035 for the wire format implemented here.
 */

/** @file DNSQuery.h
    @brief Minimal DNS resolver used by the Mail auto-config wizard to
           look up MX records when no provider database entry exists. */

#ifndef DNS_QUERY_H
#define DNS_QUERY_H


#include <DataIO.h>
#include <NetBuffer.h>
#include <ObjectList.h>
#include <String.h>


#include <arpa/inet.h>

/** @brief DNS resource-record type code for MX (mail exchange) records. */
#define MX_RECORD		15

/**
 * @brief Decoded MX record consisting of its preference value and the
 *        target mail server's domain name.
 */
struct mx_record {
	uint16	priority;
	BString	serverName;
};


/**
 * @brief Light-weight byte buffer with big-endian integer and DNS-style
 *        string accessors used by DNSQuery.
 *
 * Like BNetBuffer but without per-field type or size headers, so the bytes
 * laid down on the wire match the RFC 1035 layout exactly.
 */
class BRawNetBuffer {
public:
						BRawNetBuffer();
						BRawNetBuffer(off_t size);
						BRawNetBuffer(const void* buf, size_t size);

		// functions like in BNetBuffer but no type and size info is writen.
		// functions return B_NO_MEMORY or B_OK
		status_t		AppendUint16(uint16 value);
		status_t		AppendString(const char* string);

		status_t		ReadUint16(uint16& value);
		status_t		ReadUint32(uint32& value);
		status_t		ReadString(BString& string);

		status_t		SkipReading(off_t off);
		
		/** @brief Returns a raw pointer to the underlying byte storage. */
		void			*Data(void) const { return (void*)fBuffer.Buffer(); }
		/** @brief Returns the current allocated buffer size in bytes. */
		size_t			Size(void) const { return fBuffer.BufferLength(); }
		/** @brief Resizes the underlying buffer to @a size bytes. */
		size_t			SetSize(off_t size) { return fBuffer.SetSize(size); }
		/** @brief Manually rewinds or advances the next-write offset to
		           @a pos. */
		void			SetWritePosition(off_t pos) { fWritePosition = pos; }

private:
		void			_Init(const void* buf, size_t size);
		ssize_t			_ReadStringAt(BString& string, off_t pos);

		off_t 			fWritePosition;
		off_t 			fReadPosition;
		BMallocIO		fBuffer;
};


/**
 * @brief Static helpers for parsing the system resolver configuration and
 *        translating between dotted and length-prefixed DNS name formats.
 */
class DNSTools {
public:
		static status_t	GetDNSServers(BObjectList<BString, true>* serverList);
		static BString	ConvertToDNSName(const BString& string);
		static BString	ConvertFromDNSName(const BString& string);
};

// see also http://prasshhant.blogspot.com/2007/03/dns-query.html
/**
 * @brief Wire-format DNS message header (RFC 1035 section 4.1.1) used as
 *        both the request and response top frame.
 */
struct dns_header {
	dns_header()
	{
		q_count = 0;
		ans_count  = 0;
		auth_count = 0;
		add_count  = 0;
	}

	uint16 id;						// A 16 bit identifier
	
	unsigned	char qr     :1;		// query (0), or a response (1)
	unsigned	char opcode :4;	    // A four bit field
	unsigned	char aa     :1;		// Authoritative Answer
	unsigned	char tc     :1;		// Truncated Message
	unsigned	char rd     :1;		// Recursion Desired	
	unsigned	char ra     :1;		// Recursion Available
	unsigned	char z      :3;		// Reserved for future use
	unsigned	char rcode  :4;	    // Response code

	uint16		q_count;			// number of question entries
	uint16		ans_count;			// number of answer entries
	uint16		auth_count;			// number of authority entries
	uint16		add_count;			// number of resource entries
};

// resource record without resource data
/**
 * @brief Header portion of a DNS resource record, decoded from the
 *        response stream before the type-specific payload is read.
 */
struct resource_record_head {
	BString	name;
	uint16	type;
	uint16	dataClass;
	uint32	ttl;
	uint16	dataLength;
};


/**
 * @brief Issues a single MX-record DNS query and parses the response.
 *
 * Uses the first nameserver from @c /system/settings/network/resolv.conf
 * and a UDP datagram socket. Designed for one-shot use from the auto-
 * configuration wizard rather than a long-running resolver.
 */
class DNSQuery {
public:
						DNSQuery();
						~DNSQuery();
		status_t		ReadDNSServer(in_addr* add);
		status_t		GetMXRecords(const BString& serverName,
							BObjectList<mx_record, true>* mxList,
							bigtime_t timeout = 500000);
				  
private:
		uint16			_GetUniqueID();
		void			_SetMXHeader(dns_header* header);
		void			_AppendQueryHeader(BRawNetBuffer& buffer,
							const dns_header* header);
		void			_ReadQueryHeader(BRawNetBuffer& buffer,
							dns_header* header);
		void			_ReadMXRecord(BRawNetBuffer& buffer,
							mx_record* mxRecord);

		void			_ReadResourceRecord(BRawNetBuffer& buffer,
							resource_record_head* rrHead);
};


#endif // DNS_QUERY_H
