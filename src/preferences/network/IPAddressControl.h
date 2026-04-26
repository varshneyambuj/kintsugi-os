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
 * MIT License. Copyright 2015, Haiku, Inc.
 * Original authors: Axel Dörfler.
 */

/** @file IPAddressControl.h
    @brief BTextControl subclass that validates the entered string as an
           IPv4 or IPv6 address and visually marks invalid input. */

#ifndef IP_ADDRESS_CONTROL_H
#define IP_ADDRESS_CONTROL_H


#include <TextControl.h>


/**
 * @brief Self-validating IP address text control.
 *
 * Parses input on every modification and toggles MarkAsInvalid() so the
 * field flashes red when the user enters something the network kit cannot
 * resolve as a numeric address. Also configurable to treat empty input as
 * valid or invalid.
 */
class IPAddressControl : public BTextControl {
public:
								IPAddressControl(int family, const char* label,
									const char* name);
	virtual						~IPAddressControl();

			bool				AllowEmpty() const;
			void				SetAllowEmpty(bool empty);

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_UpdateMark();

private:
			int					fFamily;
			bool				fAllowEmpty;
};


#endif // IP_ADDRESS_CONTROL_H
