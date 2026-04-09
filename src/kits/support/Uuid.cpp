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
 *   Copyright 2013 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Uuid.cpp
 * @brief Implementation of BUuid, a RFC 4122 universally unique identifier.
 *
 * BUuid generates and represents 128-bit UUIDs. Random (version 4) UUIDs are
 * produced by reading from /dev/urandom or /dev/random when available; a
 * time- and address-seeded PRNG fallback is used when neither device can be
 * opened. The variant and version fields are set in accordance with RFC 4122
 * section 4.4.
 *
 * The class lives in the BPrivate namespace and is exported via a using
 * declaration at file scope.
 */


#include <Uuid.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


static const char* const kHexChars = "0123456789abcdef";

/// Byte index within fValue at which the version nibble is stored (bits 4-7).
static const size_t kVersionByteIndex = 6;
/// Byte index within fValue at which the variant bits are stored (bits 6-7).
static const size_t kVariantByteIndex = 8;


/**
 * @brief Seed the PRNG used by the random fallback path.
 *
 * Combines the current wall-clock time (seconds and nanoseconds) with a
 * stack address to produce a seed. With address-space layout randomisation
 * the stack address contributes a few extra bits of entropy.
 *
 * This function is called at most once (via a static local inside
 * _SetToRandomFallback()) so the seed is established lazily on first use.
 *
 * @return Always returns \c true (used only as a static initialiser guard).
 */
static bool
init_random_seed()
{
	// set a time-based seed
	timespec time;
	clock_gettime(CLOCK_REALTIME, &time);
	uint32 seed = (uint32)time.tv_sec ^ (uint32)time.tv_nsec;

	// factor in a stack address -- with address space layout randomization
	// that adds a bit of additional randomness
	seed ^= (uint32)(addr_t)&time;

	srandom(seed);

	return true;
}


namespace BPrivate {

/**
 * @brief Construct a nil UUID (all 128 bits set to zero).
 */
BUuid::BUuid()
{
	memset(fValue, 0, sizeof(fValue));
}


/**
 * @brief Copy-construct a BUuid from \a other.
 *
 * @param other The UUID whose value is to be copied.
 */
BUuid::BUuid(const BUuid& other)
{
	memcpy(fValue, other.fValue, sizeof(fValue));
}


BUuid::~BUuid()
{
}


/**
 * @brief Test whether this UUID is the nil UUID (all bits zero).
 *
 * @return \c true if every byte of fValue is 0, \c false otherwise.
 */
bool
BUuid::IsNil() const
{
	for (size_t i = 0; i < sizeof(fValue); i++) {
		if (fValue[i] != 0)
			return false;
	}

	return true;
}


/**
 * @brief Fill this UUID with random bytes and set the version 4 / variant 1
 *        fields as required by RFC 4122 section 4.4.
 *
 * Random bytes are sourced from /dev/urandom (preferred), /dev/random, or the
 * PRNG fallback (see _SetToDevRandom() and _SetToRandomFallback()). After
 * filling the raw bytes the variant field (byte 8, bits 6-7) is set to
 * binary 10 and the version field (byte 6, bits 4-7) is set to 0100 (4).
 *
 * @return A reference to this object, allowing chained assignment.
 */
BUuid&
BUuid::SetToRandom()
{
	if (!BUuid::_SetToDevRandom())
		BUuid::_SetToRandomFallback();

	// set variant and version
	fValue[kVariantByteIndex] &= 0x3f;
	fValue[kVariantByteIndex] |= 0x80;
	fValue[kVersionByteIndex] &= 0x0f;
	fValue[kVersionByteIndex] |= 4 << 4;
		// version 4

	return *this;
}


/**
 * @brief Format the UUID as the canonical lowercase hyphenated string.
 *
 * Produces a string in the form
 * "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" (8-4-4-4-12 hex digits).
 *
 * @return A BString containing the formatted UUID.
 */
BString
BUuid::ToString() const
{
	char buffer[32];
	for (size_t i = 0; i < 16; i++) {
		buffer[2 * i] = kHexChars[fValue[i] >> 4];
		buffer[2 * i + 1] = kHexChars[fValue[i] & 0xf];
	}

	return BString().SetToFormat("%.8s-%.4s-%.4s-%.4s-%.12s",
		buffer, buffer + 8, buffer + 12, buffer + 16, buffer + 20);
}


/**
 * @brief Perform a bytewise comparison with another UUID.
 *
 * The comparison is equivalent to memcmp() on the 16-byte raw values,
 * which yields a lexicographic ordering of the binary representation.
 *
 * @param other The UUID to compare against.
 * @return A negative value if this < other, zero if equal, a positive value
 *         if this > other.
 */
int
BUuid::Compare(const BUuid& other) const
{
	return memcmp(fValue, other.fValue, sizeof(fValue));
}


/**
 * @brief Copy-assign \a other to this UUID.
 *
 * @param other The UUID whose value is to be copied.
 * @return A reference to this object.
 */
BUuid&
BUuid::operator=(const BUuid& other)
{
	memcpy(fValue, other.fValue, sizeof(fValue));

	return *this;
}


/**
 * @brief Attempt to fill the UUID bytes from a kernel random device.
 *
 * Tries /dev/urandom first, then /dev/random. Reads exactly sizeof(fValue)
 * bytes. The version/variant fields are NOT set here; the caller
 * (SetToRandom()) applies them after this function returns.
 *
 * @return \c true if exactly sizeof(fValue) bytes were read successfully,
 *         \c false if neither device could be opened or the read was short.
 */
bool
BUuid::_SetToDevRandom()
{
	// open device
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) {
		fd = open("/dev/random", O_RDONLY);
		if (fd < 0)
			return false;
	}

	// read bytes
	ssize_t bytesRead = read(fd, fValue, sizeof(fValue));
	close(fd);

	return bytesRead == (ssize_t)sizeof(fValue);
}


/**
 * @brief Fill the UUID bytes using the C standard PRNG as a fallback.
 *
 * Calls init_random_seed() exactly once (via a static local) to seed the
 * PRNG, then calls random() four times to produce 16 bytes. Because
 * random() returns only 31-bit values the high bit of each 32-bit word
 * is zero; a few of these bits are redistributed to the bytes whose high
 * bit would otherwise be overwritten by the version field in SetToRandom().
 *
 * This function is intended to be called only when _SetToDevRandom() fails.
 */
void
BUuid::_SetToRandomFallback()
{
	static bool sSeedInitialized = init_random_seed();
	(void)sSeedInitialized;

	for (int32 i = 0; i < 4; i++) {
		uint32 value = random();
		fValue[4 * i + 0] = uint8(value >> 24);
		fValue[4 * i + 1] = uint8(value >> 16);
		fValue[4 * i + 2] = uint8(value >> 8);
		fValue[4 * i + 3] = uint8(value);
	}

	// random() returns 31 bit numbers only, so we move a few bits from where
	// we overwrite them with the version anyway.
	uint8 bitsToMove = fValue[kVersionByteIndex];
	for (int32 i = 0; i < 4; i++)
		fValue[4 * i] |= (bitsToMove << i) & 0x80;
}

}	// namespace BPrivate


using BPrivate::BUuid;
