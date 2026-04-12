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
 *   Copyright 2003, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2010-2011, Oliver Tappe, zooey@hirschkaefer.de.
 *   Copyright 2012, John Scipione, jscipione@gmail.com
 *   Copyright 2017, Adrien Destugues, pulkomandy@pulkomandy.tk
 *   Copyright 2021, Andrew Lindesay, apl@lindesay.co.nz
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file NumberFormat.cpp
 * @brief Implementation of BNumberFormat for locale-aware number formatting.
 *
 * BNumberFormat wraps ICU NumberFormat via the private BNumberFormatImpl helper
 * class. Separate ICU formatter instances are maintained for integers, floats,
 * currency, and percentages, and are lazily created on first use. The class
 * supports formatting to C strings and BStrings, monetary and percent variants,
 * decimal precision control, and reverse parsing of locale-formatted numbers.
 *
 * @see BFormat, BFormattingConventions
 */


#include <unicode/uversion.h>
#include <NumberFormat.h>

#include <AutoDeleter.h>
#include <Autolock.h>
#include <FormattingConventionsPrivate.h>

#include <ICUWrapper.h>

#include <unicode/dcfmtsym.h>
#include <unicode/decimfmt.h>
#include <unicode/numfmt.h>


U_NAMESPACE_USE


/**
 * @brief Private implementation class that holds lazily-created ICU formatters.
 *
 * Each formatter type (integer, float, currency, percent) is created on first
 * use and cached for subsequent calls.
 */
class BNumberFormatImpl {
public:
					BNumberFormatImpl();
					~BNumberFormatImpl();

	NumberFormat*	GetInteger(BFormattingConventions* convention);
	NumberFormat*	GetFloat(BFormattingConventions* convention);
	NumberFormat*	GetCurrency(BFormattingConventions* convention);
	NumberFormat*	GetPercent(BFormattingConventions* convention);

	ssize_t			ApplyFormatter(NumberFormat* formatter, char* string,
						size_t maxSize, const double value);
	status_t		ApplyFormatter(NumberFormat* formatter, BString& string,
						const double value);

private:
	NumberFormat*	fIntegerFormat;
	NumberFormat*	fFloatFormat;
	NumberFormat*	fCurrencyFormat;
	NumberFormat*	fPercentFormat;
};


/**
 * @brief Construct a BNumberFormatImpl with all formatters uninitialized.
 */
BNumberFormatImpl::BNumberFormatImpl()
{
	// They are initialized lazily as needed
	fIntegerFormat = NULL;
	fFloatFormat = NULL;
	fCurrencyFormat = NULL;
	fPercentFormat = NULL;
}


/**
 * @brief Destroy the BNumberFormatImpl and free all cached ICU formatters.
 */
BNumberFormatImpl::~BNumberFormatImpl()
{
	delete fIntegerFormat;
	delete fFloatFormat;
	delete fCurrencyFormat;
	delete fPercentFormat;
}


/**
 * @brief Return (or lazily create) the decimal integer formatter.
 *
 * @param convention  Formatting conventions providing the ICU locale.
 * @return The ICU NumberFormat, or NULL on allocation/ICU failure.
 */
NumberFormat*
BNumberFormatImpl::GetInteger(BFormattingConventions* convention)
{
	if (fIntegerFormat == NULL) {
		UErrorCode err = U_ZERO_ERROR;
		fIntegerFormat = NumberFormat::createInstance(
			*BFormattingConventions::Private(convention).ICULocale(),
			UNUM_DECIMAL, err);

		if (fIntegerFormat == NULL)
			return NULL;
		if (U_FAILURE(err)) {
			delete fIntegerFormat;
			fIntegerFormat = NULL;
			return NULL;
		}
	}

	return fIntegerFormat;
}


/**
 * @brief Return (or lazily create) the decimal float formatter.
 *
 * @param convention  Formatting conventions providing the ICU locale.
 * @return The ICU NumberFormat, or NULL on allocation/ICU failure.
 */
NumberFormat*
BNumberFormatImpl::GetFloat(BFormattingConventions* convention)
{
	if (fFloatFormat == NULL) {
		UErrorCode err = U_ZERO_ERROR;
		fFloatFormat = NumberFormat::createInstance(
			*BFormattingConventions::Private(convention).ICULocale(),
			UNUM_DECIMAL, err);

		if (fFloatFormat == NULL)
			return NULL;
		if (U_FAILURE(err)) {
			delete fFloatFormat;
			fFloatFormat = NULL;
			return NULL;
		}
	}

	return fFloatFormat;
}


/**
 * @brief Return (or lazily create) the currency formatter.
 *
 * @param convention  Formatting conventions providing the ICU locale.
 * @return The ICU NumberFormat, or NULL on allocation/ICU failure.
 */
NumberFormat*
BNumberFormatImpl::GetCurrency(BFormattingConventions* convention)
{
	if (fCurrencyFormat == NULL) {
		UErrorCode err = U_ZERO_ERROR;
		fCurrencyFormat = NumberFormat::createCurrencyInstance(
			*BFormattingConventions::Private(convention).ICULocale(),
			err);

		if (fCurrencyFormat == NULL)
			return NULL;
		if (U_FAILURE(err)) {
			delete fCurrencyFormat;
			fCurrencyFormat = NULL;
			return NULL;
		}
	}

	return fCurrencyFormat;
}


/**
 * @brief Return (or lazily create) the percent formatter.
 *
 * @param convention  Formatting conventions providing the ICU locale.
 * @return The ICU NumberFormat, or NULL on allocation/ICU failure.
 */
NumberFormat*
BNumberFormatImpl::GetPercent(BFormattingConventions* convention)
{
	if (fPercentFormat == NULL) {
		UErrorCode err = U_ZERO_ERROR;
		fPercentFormat = NumberFormat::createInstance(
			*BFormattingConventions::Private(convention).ICULocale(),
			UNUM_PERCENT, err);

		if (fPercentFormat == NULL)
			return NULL;
		if (U_FAILURE(err)) {
			delete fPercentFormat;
			fPercentFormat = NULL;
			return NULL;
		}
	}

	return fPercentFormat;
}


/**
 * @brief Apply a formatter to a double and write the result into a C buffer.
 *
 * @param formatter  ICU NumberFormat to use.
 * @param string     Destination character buffer.
 * @param maxSize    Buffer size in bytes.
 * @param value      The double value to format.
 * @return Number of bytes written, or a negative error code.
 */
ssize_t
BNumberFormatImpl::ApplyFormatter(NumberFormat* formatter, char* string,
	size_t maxSize, const double value)
{
	BString fullString;
	status_t status = ApplyFormatter(formatter, fullString, value);
	if (status != B_OK)
		return status;

	return strlcpy(string, fullString.String(), maxSize);
}


/**
 * @brief Apply a formatter to a double and return the result in a BString.
 *
 * @param formatter  ICU NumberFormat to use.
 * @param string     Output BString.
 * @param value      The double value to format.
 * @return B_OK on success, B_NO_MEMORY if formatter is NULL.
 */
status_t
BNumberFormatImpl::ApplyFormatter(NumberFormat* formatter, BString& string,
	const double value)
{
	if (formatter == NULL)
		return B_NO_MEMORY;

	UnicodeString icuString;
	formatter->format(value, icuString);

	string.Truncate(0);
	BStringByteSink stringConverter(&string);
	icuString.toUTF8(stringConverter);

	return B_OK;
}


/**
 * @brief Construct a BNumberFormat using the system default locale.
 */
BNumberFormat::BNumberFormat()
	: BFormat()
{
	fPrivateData = new BNumberFormatImpl();
}


/**
 * @brief Construct a BNumberFormat for the given BLocale.
 *
 * @param locale  Locale to use, or NULL for the system default.
 */
BNumberFormat::BNumberFormat(const BLocale* locale)
	: BFormat(locale)
{
	fPrivateData = new BNumberFormatImpl();
}


/**
 * @brief Copy-construct a BNumberFormat.
 *
 * @param other  Source BNumberFormat to copy.
 */
BNumberFormat::BNumberFormat(const BNumberFormat &other)
	: BFormat(other)
{
	fPrivateData = new BNumberFormatImpl(*other.fPrivateData);
}


/**
 * @brief Destroy the BNumberFormat and free the private implementation.
 */
BNumberFormat::~BNumberFormat()
{
	delete fPrivateData;
}


// #pragma mark - Formatting


/**
 * @brief Format a double into a C string buffer using decimal notation.
 *
 * @param string   Destination character buffer.
 * @param maxSize  Buffer size in bytes.
 * @param value    The double to format.
 * @return Number of bytes written, or a negative error code.
 */
ssize_t
BNumberFormat::Format(char* string, size_t maxSize, const double value)
{
	BString fullString;
	status_t status = Format(fullString, value);
	if (status != B_OK)
		return status;

	return strlcpy(string, fullString.String(), maxSize);
}


/**
 * @brief Format a double into a BString using decimal notation.
 *
 * @param string  Output BString.
 * @param value   The double to format.
 * @return B_OK on success, B_NO_MEMORY if the formatter could not be created.
 */
status_t
BNumberFormat::Format(BString& string, const double value)
{
	NumberFormat* formatter = fPrivateData->GetFloat(&fConventions);

	if (formatter == NULL)
		return B_NO_MEMORY;

	UnicodeString icuString;
	formatter->format(value, icuString);

	string.Truncate(0);
	BStringByteSink stringConverter(&string);
	icuString.toUTF8(stringConverter);

	return B_OK;
}


/**
 * @brief Format an int32 into a C string buffer.
 *
 * @param string   Destination character buffer.
 * @param maxSize  Buffer size in bytes.
 * @param value    The int32 to format.
 * @return Number of bytes written, or a negative error code.
 */
ssize_t
BNumberFormat::Format(char* string, size_t maxSize, const int32 value)
{
	BString fullString;
	status_t status = Format(fullString, value);
	if (status != B_OK)
		return status;

	return strlcpy(string, fullString.String(), maxSize);
}


/**
 * @brief Format an int32 into a BString.
 *
 * @param string  Output BString.
 * @param value   The int32 to format.
 * @return B_OK on success, B_NO_MEMORY if the formatter could not be created.
 */
status_t
BNumberFormat::Format(BString& string, const int32 value)
{
	NumberFormat* formatter = fPrivateData->GetInteger(&fConventions);

	if (formatter == NULL)
		return B_NO_MEMORY;

	UnicodeString icuString;
	formatter->format((int32_t)value, icuString);

	string.Truncate(0);
	BStringByteSink stringConverter(&string);
	icuString.toUTF8(stringConverter);

	return B_OK;
}


/**
 * @brief Set the number of fraction digits for float, currency, and percent formatters.
 *
 * Both minimum and maximum fraction digits are set to \a precision.
 *
 * @param precision  Number of decimal places.
 * @return B_OK on success, B_ERROR if any formatter could not be obtained.
 */
status_t
BNumberFormat::SetPrecision(int precision)
{
	NumberFormat* decimalFormatter = fPrivateData->GetFloat(&fConventions);
	NumberFormat* currencyFormatter = fPrivateData->GetCurrency(&fConventions);
	NumberFormat* percentFormatter = fPrivateData->GetPercent(&fConventions);

	if ((decimalFormatter == NULL) || (currencyFormatter == NULL) || (percentFormatter == NULL))
		return B_ERROR;

	decimalFormatter->setMinimumFractionDigits(precision);
	decimalFormatter->setMaximumFractionDigits(precision);

	currencyFormatter->setMinimumFractionDigits(precision);
	currencyFormatter->setMaximumFractionDigits(precision);

	percentFormatter->setMinimumFractionDigits(precision);
	percentFormatter->setMaximumFractionDigits(precision);

	return B_OK;
}


/**
 * @brief Format a monetary value into a C string buffer.
 *
 * @param string   Destination character buffer.
 * @param maxSize  Buffer size in bytes.
 * @param value    Monetary value as a double.
 * @return Number of bytes written, or a negative error code.
 */
ssize_t
BNumberFormat::FormatMonetary(char* string, size_t maxSize, const double value)
{
	return fPrivateData->ApplyFormatter(
		fPrivateData->GetCurrency(&fConventions), string, maxSize, value);
}


/**
 * @brief Format a monetary value into a BString.
 *
 * @param string  Output BString.
 * @param value   Monetary value as a double.
 * @return B_OK on success, B_NO_MEMORY if the formatter could not be created.
 */
status_t
BNumberFormat::FormatMonetary(BString& string, const double value)
{
	return fPrivateData->ApplyFormatter(
		fPrivateData->GetCurrency(&fConventions), string, value);
}


/**
 * @brief Format a percentage value into a C string buffer.
 *
 * @param string   Destination character buffer.
 * @param maxSize  Buffer size in bytes.
 * @param value    Fraction value (e.g. 0.5 for 50%).
 * @return Number of bytes written, or a negative error code.
 */
ssize_t
BNumberFormat::FormatPercent(char* string, size_t maxSize, const double value)
{
	return fPrivateData->ApplyFormatter(
		fPrivateData->GetPercent(&fConventions), string, maxSize, value);
}


/**
 * @brief Format a percentage value into a BString.
 *
 * @param string  Output BString.
 * @param value   Fraction value (e.g. 0.5 for 50%).
 * @return B_OK on success, B_NO_MEMORY if the formatter could not be created.
 */
status_t
BNumberFormat::FormatPercent(BString& string, const double value)
{
	return fPrivateData->ApplyFormatter(
		fPrivateData->GetPercent(&fConventions), string, value);
}


/**
 * @brief Parse a locale-formatted number string into a double.
 *
 * @param string  Input BString containing a locale-formatted number.
 * @param value   Output double populated with the parsed value.
 * @return B_OK on success, B_NO_MEMORY if the formatter is unavailable,
 *         B_BAD_DATA if the string cannot be parsed.
 */
status_t
BNumberFormat::Parse(const BString& string, double& value)
{
	NumberFormat* parser = fPrivateData->GetFloat(&fConventions);

	if (parser == NULL)
		return B_NO_MEMORY;

	UnicodeString unicode(string.String());
	Formattable result(0);
	UErrorCode err = U_ZERO_ERROR;

	parser->parse(unicode, result, err);

	if (err != U_ZERO_ERROR)
		return B_BAD_DATA;

	value = result.getDouble();

	return B_OK;
}


/**
 * @brief Return the locale-specific decimal or grouping separator symbol.
 *
 * @param element  B_DECIMAL_SEPARATOR or B_GROUPING_SEPARATOR.
 * @return BString containing the separator character, or an empty string on
 *         failure.
 */
BString
BNumberFormat::GetSeparator(BNumberElement element)
{
	DecimalFormatSymbols::ENumberFormatSymbol symbol;
	BString result;

	switch(element) {
		case B_DECIMAL_SEPARATOR:
			symbol = DecimalFormatSymbols::kDecimalSeparatorSymbol;
			break;
		case B_GROUPING_SEPARATOR:
			symbol = DecimalFormatSymbols::kGroupingSeparatorSymbol;
			break;

		default:
			return result;
	}

	NumberFormat* format = fPrivateData->GetFloat(&fConventions);
	DecimalFormat* decimal = dynamic_cast<DecimalFormat*>(format);

	if (decimal == NULL)
		return result;

	const DecimalFormatSymbols* symbols = decimal->getDecimalFormatSymbols();
	UnicodeString string = symbols->getSymbol(symbol);

	BStringByteSink stringConverter(&result);
	string.toUTF8(stringConverter);

	return result;
}
