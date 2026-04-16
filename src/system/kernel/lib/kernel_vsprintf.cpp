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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2003-2008, Axel Dörfler. All rights reserved.
 *   Distributed under the terms of the MIT license.
 *
 *   Copyright 2001, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file kernel_vsprintf.cpp
 * @brief Kernel implementation of printf-family string formatters.
 *
 * Provides the kernel's self-contained vsnprintf/vsprintf/snprintf/sprintf so
 * formatting does not depend on user-space libc. Output is funnelled through
 * a small Buffer helper that tracks capacity and bytes written, allowing the
 * truncation semantics of (v)snprintf to be honoured while still reporting
 * the full would-be length. A compact floating-point path is available for
 * debug output when FLOATING_SUPPORT is defined.
 */


#include <SupportDefs.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>


#define ZEROPAD	1		/* pad with zero */
#define SIGN	2		/* unsigned/signed long */
#define PLUS	4		/* show plus */
#define SPACE	8		/* space if plus */
#define LEFT	16		/* left justified */
#define SPECIAL	32		/* 0x */
#define LARGE	64		/* use 'ABCDEF' instead of 'abcdef' */

#define FLOATING_SUPPORT


struct Buffer {
	Buffer(char* buffer, size_t size)
		:
		fCurrent(buffer),
		fSize(size),
		fBytesWritten(0)
	{
	}

	size_t BytesWritten() const
	{
		return fBytesWritten;
	}

	void PutCharacter(char c)
	{
		if (fBytesWritten < fSize) {
			*fCurrent = c;
			fCurrent++;
		}

		fBytesWritten++;
	}

	void PutPadding(int32 count)
	{
		if (count <= 0)
			return;

		if (fBytesWritten < fSize) {
			int32 toWrite = std::min(fSize - fBytesWritten, (size_t)count);
			while (--toWrite >= 0)
				*fCurrent++ = ' ';
		}

		fBytesWritten += count;
	}

	void PutString(const char *source, int32 length)
	{
		if (length <= 0)
			return;

		if (fBytesWritten < fSize) {
			int32 toWrite = std::min(fSize - fBytesWritten, (size_t)length);
			memcpy(fCurrent, source, toWrite);
			fCurrent += toWrite;
		}

		fBytesWritten += length;
	}

	void NullTerminate()
	{
		if (fBytesWritten < fSize)
			*fCurrent = '\0';
		else if (fSize > 0)
			fCurrent[-1] = '\0';
	}

private:
	char*	fCurrent;
	size_t	fSize;
	size_t	fBytesWritten;
};


/**
 * @brief Parse a run of decimal digits, advancing the cursor.
 *
 * Consumes digits from the position pointed to by @p s and converts them to
 * an int. On return, @p *s points at the first non-digit character.
 *
 * @param s In/out pointer to the format-string cursor; updated past digits.
 * @return The decoded integer value, or 0 if no digits are present.
 */
static int
skip_atoi(const char **s)
{
	int i = 0;

	while (isdigit(**s))
		i = i*10 + *((*s)++) - '0';

	return i;
}


/**
 * @brief Divide a 64-bit value by @p base in place and return the remainder.
 *
 * Used by number() to peel one digit at a time off a value being formatted.
 *
 * @param _number In/out pointer to the dividend; updated to the quotient.
 * @param base    Divisor (typically 2..36 for a printable radix).
 * @return The remainder after dividing @p *_number by @p base.
 */
static uint64
do_div(uint64 *_number, uint32 base)
{
	uint64 result = *_number % (uint64)base;
	*_number = *_number / (uint64)base;

	return result;
}


/**
 * @brief Select the leading sign character for a numeric conversion.
 *
 * Honours the printf flag characters '+' and ' ' in addition to the natural
 * '-' for negative values. Returns '\0' when no sign character should be
 * emitted (typically because the SIGN flag is not set).
 *
 * @param flags    Bitmask of PLUS / SPACE / SIGN flags.
 * @param negative True if the value to be printed is negative.
 * @return Sign character to emit, or '\0' for none.
 */
static char
sign_symbol(int flags, bool negative)
{
	if ((flags & SIGN) == 0)
		return '\0';

	if (negative)
		return '-';
	else if ((flags & PLUS) != 0)
		return '+';
	else if ((flags & SPACE) != 0)
		return ' ';

	return '\0';
}


/**
 * @brief Format an integer into @p outBuffer using printf-style options.
 *
 * Implements the core of %d, %u, %o, %x, %X and %p conversions. Handles
 * sign selection, the '0x'/'0' SPECIAL prefix, zero vs space padding, left
 * justification, and minimum precision (leading zeros independent of field
 * width). The temporary digit buffer holds at most 66 characters which
 * suffices for any 64-bit value in any base >= 2.
 *
 * @param outBuffer Destination buffer wrapper.
 * @param num       Value to render (signed interpretation if SIGN is set).
 * @param base      Numeric base, clamped to [2, 36]; higher values are ignored.
 * @param size      Minimum field width in characters.
 * @param precision Minimum number of digits (leading zeros added as needed).
 * @param flags     Bitmask of ZEROPAD, SIGN, PLUS, SPACE, LEFT, SPECIAL, LARGE.
 */
static void
number(Buffer& outBuffer, uint64 num, uint32 base, int size,
	int precision, int flags)
{
	const char *digits = "0123456789abcdefghijklmnopqrstuvwxyz";
	char c, sign, tmp[66];
	int i;

	if (flags & LARGE)
		digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	if (flags & LEFT)
		flags &= ~ZEROPAD;
	if (base < 2 || base > 36)
		return;

	c = (flags & ZEROPAD) ? '0' : ' ';

	if (flags & SIGN) {
		sign = sign_symbol(flags, (int64)num < 0);
		if ((int64)num < 0)
			num = -(int64)num;
		if (sign)
			size--;
	} else
		sign = 0;

	if ((flags & SPECIAL) != 0) {
		if (base == 16)
			size -= 2;
		else if (base == 8)
			size--;
	}

	i = 0;
	if (num == 0)
		tmp[i++] = '0';
	else while (num != 0)
		tmp[i++] = digits[do_div(&num, base)];

	if (i > precision)
		precision = i;
	size -= precision;

	if (!(flags & (ZEROPAD + LEFT))) {
		outBuffer.PutPadding(size);
		size = 0;
	}
	if (sign)
		outBuffer.PutCharacter(sign);

	if ((flags & SPECIAL) != 0) {
		if (base == 8)
			outBuffer.PutCharacter('0');
		else if (base == 16) {
			outBuffer.PutCharacter('0');
			outBuffer.PutCharacter(digits[33]);
		}
	}

	if (!(flags & LEFT)) {
		while (size-- > 0)
			outBuffer.PutCharacter(c);
	}
	while (i < precision--)
		outBuffer.PutCharacter('0');
	while (i-- > 0)
		outBuffer.PutCharacter(tmp[i]);

	outBuffer.PutPadding(size);
}


#ifdef FLOATING_SUPPORT
/**
 * @brief Minimal double-to-string conversion for kernel debug output.
 *
 * Emits at most three fractional digits and ignores the usual precision
 * argument; the implementation is deliberately small so it can be compiled
 * into the kernel when FLOATING_SUPPORT is enabled. Intended for tracing and
 * printk-style debugging rather than general-purpose formatting.
 *
 * @param outBuffer  Destination buffer wrapper.
 * @param value      Value to print.
 * @param fieldWidth Minimum total width; padding is added with spaces.
 * @param flags      Bitmask with LEFT, SIGN, PLUS, SPACE honoured.
 */
static void
floating(Buffer& outBuffer, double value, int fieldWidth, int flags)
{
	char buffer[66];
	uint64 fraction;
	uint64 integer;
	int32 length = 0;
	char sign;

	sign = sign_symbol(flags, value < 0.0);
	if (value < 0.0)
		value = -value;

	fraction = (uint64)(value * 1000) % 1000;
	integer = (uint64)value;

	// put fraction part, if any

	if (fraction != 0) {
		bool first = true;
		for (int zeroes = 0; zeroes < 3; zeroes++) {
			int digit = do_div(&fraction, 10);
			if (!first || digit > 0) {
				buffer[length++] = '0' + digit;
				first = false;
			}
		}

		buffer[length++] = '.';
	}

	// put integer part

	if (integer == 0) {
		buffer[length++] = '0';
	} else {
		while (integer != 0)
			buffer[length++] = '0' + do_div(&integer, 10);
	}

	// write back to string

	if (!(flags & LEFT))
		outBuffer.PutPadding(fieldWidth);

	if (sign)
		outBuffer.PutCharacter(sign);

	while (length-- > 0)
		outBuffer.PutCharacter(buffer[length]);

	if ((flags & LEFT) != 0)
		outBuffer.PutPadding(fieldWidth);
}
#endif	// FLOATING_SUPPORT


/**
 * @brief Format a va_list into a bounded character buffer.
 *
 * Kernel equivalent of C99 vsnprintf(): walks @p format, interpreting the
 * usual %-conversions (d, i, u, o, x, X, c, s, p, n, %, and when
 * FLOATING_SUPPORT is defined f/F/g/G), pulling arguments from @p args,
 * width/precision modifiers, and length qualifiers h, l, ll/L and z. Writes
 * at most @p bufferSize - 1 characters plus a NUL terminator, and always
 * returns the would-be length so callers can detect truncation.
 *
 * @param buffer     Destination character array; may be NULL only if @p bufferSize is 0.
 * @param bufferSize Capacity of @p buffer including space for the terminator.
 * @param format     printf-style format string.
 * @param args       Argument list positioned at the first conversion.
 * @return Number of bytes that would have been written ignoring truncation.
 */
int
vsnprintf(char *buffer, size_t bufferSize, const char *format, va_list args)
{
	uint64 num;
	int base;
	int flags;			/* flags to number() */
	int fieldWidth;	/* width of output field */
	int precision;
		/* min. # of digits for integers; max number of chars for from string */
	int qualifier;		/* 'h', 'l', or 'L' for integer fields */

	Buffer outBuffer(buffer, bufferSize);

	for (; format[0]; format++) {
		if (format[0] != '%') {
			outBuffer.PutCharacter(format[0]);
			continue;
		}

		/* process flags */

		flags = 0;

	repeat:
		format++;
			/* this also skips first '%' */
		switch (format[0]) {
			case '-': flags |= LEFT; goto repeat;
			case '+': flags |= PLUS; goto repeat;
			case ' ': flags |= SPACE; goto repeat;
			case '#': flags |= SPECIAL; goto repeat;
			case '0': flags |= ZEROPAD; goto repeat;

			case '%':
				outBuffer.PutCharacter(format[0]);
				continue;
		}

		/* get field width */

		fieldWidth = -1;
		if (isdigit(*format))
			fieldWidth = skip_atoi(&format);
		else if (format[0] == '*') {
			format++;
			/* it's the next argument */
			fieldWidth = va_arg(args, int);
			if (fieldWidth < 0) {
				fieldWidth = -fieldWidth;
				flags |= LEFT;
			}
		}

		/* get the precision */

		precision = -1;
		if (format[0] == '.') {
			format++;
			if (isdigit(*format))
				precision = skip_atoi(&format);
			else if (format[0] == '*') {
				format++;
				/* it's the next argument */
				precision = va_arg(args, int);
			}
			if (precision < 0)
				precision = 0;
		}

		/* get the conversion qualifier */

		qualifier = -1;
		if (format[0] == 'h' || format[0] == 'L' || format[0] == 'z') {
			qualifier = *format++;
		} else if (format[0] == 'l')  {
			format++;
			if (format[0] == 'l') {
				qualifier = 'L';
				format++;
			} else
				qualifier = 'l';
		}

		/* default base */
		base = 10;

		switch (format[0]) {
			case 'c':
				if (!(flags & LEFT))
					outBuffer.PutPadding(fieldWidth - 1);

				outBuffer.PutCharacter((char)va_arg(args, int));

				if ((flags & LEFT) != 0)
					outBuffer.PutPadding(fieldWidth - 1);
				continue;

			case 's':
			{
				const char *argument = va_arg(args, char *);
				int32 length;

				if (argument == NULL)
					argument = "<NULL>";

				length = strnlen(argument, precision);
				fieldWidth -= length;

				if (!(flags & LEFT))
					outBuffer.PutPadding(fieldWidth);

				outBuffer.PutString(argument, length);

				if ((flags & LEFT) != 0)
					outBuffer.PutPadding(fieldWidth);
				continue;
			}

#ifdef FLOATING_SUPPORT
			case 'f':
			case 'F':
			case 'g':
			case 'G':
			{
				double value = va_arg(args, double);
				floating(outBuffer, value, fieldWidth, flags | SIGN);
				continue;
			}
#endif	// FLOATING_SUPPORT

			case 'p':
				if (fieldWidth == -1) {
					fieldWidth = 2*sizeof(void *);
					flags |= ZEROPAD;
				}

				outBuffer.PutString("0x", 2);
				number(outBuffer, (addr_t)va_arg(args, void *), 16, fieldWidth,
					precision, flags);
				continue;

			case 'n':
				if (qualifier == 'l') {
					long *ip = va_arg(args, long *);
					*ip = outBuffer.BytesWritten();
				} else {
					int *ip = va_arg(args, int *);
					*ip = outBuffer.BytesWritten();
				}
				continue;

			/* integer number formats - set up the flags and "break" */
			case 'o':
				base = 8;
				break;

			case 'X':
				flags |= LARGE;
			case 'x':
				base = 16;
				break;

			case 'd':
			case 'i':
				flags |= SIGN;
			case 'u':
				break;

			default:
				if (format[0] != '%')
					outBuffer.PutCharacter('%');

				if (!format[0])
					goto out;

				outBuffer.PutCharacter(format[0]);
				continue;
		}

		if (qualifier == 'L')
			num = va_arg(args, unsigned long long);
		else if (qualifier == 'l') {
			num = va_arg(args, unsigned long);
			if ((flags & SIGN) != 0)
				num = (long)num;
		} else if (qualifier == 'z') {
			num = va_arg(args, size_t);
			if ((flags & SIGN) != 0)
				num = (long)num;
		} else if (qualifier == 'h') {
			num = (unsigned short)va_arg(args, int);
			if ((flags & SIGN) != 0)
				num = (short)num;
		} else if ((flags & SIGN) != 0)
			num = va_arg(args, int);
		else
			num = va_arg(args, unsigned int);

		number(outBuffer, num, base, fieldWidth, precision, flags);
	}

out:
	outBuffer.NullTerminate();
	return outBuffer.BytesWritten();
}


/**
 * @brief Format a va_list into an unbounded character buffer.
 *
 * Thin wrapper around vsnprintf() using the largest possible size, mirroring
 * the unsafe semantics of C vsprintf(). Callers are responsible for ensuring
 * @p buffer is large enough for the expansion.
 *
 * @param buffer Destination character array sized by the caller.
 * @param format printf-style format string.
 * @param args   Argument list for the conversions in @p format.
 * @return Number of bytes written (excluding the NUL terminator).
 */
int
vsprintf(char *buffer, const char *format, va_list args)
{
	return vsnprintf(buffer, ~0UL, format, args);
}


/**
 * @brief Variadic front-end for vsnprintf().
 *
 * Assembles a va_list from its trailing arguments and delegates to
 * vsnprintf() with the caller-supplied capacity.
 *
 * @param buffer     Destination character array.
 * @param bufferSize Capacity of @p buffer including the NUL terminator.
 * @param format     printf-style format string.
 * @return Number of bytes that would have been written ignoring truncation.
 */
int
snprintf(char *buffer, size_t bufferSize, const char *format, ...)
{
	va_list args;
	int i;

	va_start(args, format);
	i = vsnprintf(buffer, bufferSize, format, args);
	va_end(args);

	return i;
}


/**
 * @brief Variadic front-end for vsprintf() with no bounds checking.
 *
 * Builds a va_list from trailing arguments and defers to vsnprintf() with an
 * effectively unlimited destination size. The caller must guarantee that
 * @p buffer is large enough.
 *
 * @param buffer Destination character array sized by the caller.
 * @param format printf-style format string.
 * @return Number of bytes written (excluding the NUL terminator).
 */
int
sprintf(char *buffer, const char *format, ...)
{
	va_list args;
	int i;

	va_start(args, format);
	i = vsnprintf(buffer, ~0UL, format, args);
	va_end(args);

	return i;
}

