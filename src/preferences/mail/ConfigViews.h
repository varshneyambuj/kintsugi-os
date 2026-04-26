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
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2007-2012, Haiku, Inc.;
 * Copyright 2001-2002 Dr. Zoidberg Enterprises;
 * Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>.
 */

/** @file ConfigViews.h
    @brief Per-account and per-protocol settings panes hosted by
           ConfigWindow once an account is selected. */

#ifndef CONFIG_VIEWS_H
#define CONFIG_VIEWS_H


#include <Box.h>
#include <image.h>

#include <MailSettingsView.h>
#include <MailSettings.h>

#include <ProtocolConfigView.h>

#include "FilterConfigView.h"


class BTextControl;
class BListView;
class BMenuField;
class BButton;
struct entry_ref;


/**
 * @brief Settings pane displayed when the user selects the account row in
 *        the list: edits friendly name, real name, and return address on
 *        the underlying BMailAccountSettings.
 */
class AccountConfigView : public BBox {
public:
								AccountConfigView(
									BMailAccountSettings* account);

	virtual void				DetachedFromWindow();
	virtual void				AttachedToWindow();
	virtual void				MessageReceived(BMessage* message);

			void				UpdateViews();

private:
			BTextControl*		fNameControl;
			BTextControl*		fRealNameControl;
			BTextControl*		fReturnAddressControl;
			BMailAccountSettings* fAccount;
};


/**
 * @brief Container view that loads a protocol add-on, instantiates its
 *        BMailSettingsView, and persists changes back into the supplied
 *        BMailProtocolSettings on detach.
 */
class ProtocolSettingsView : public BBox {
public:
								ProtocolSettingsView(const entry_ref& ref,
									const BMailAccountSettings& accountSettings,
									BMailProtocolSettings& settings);

			void 				DetachedFromWindow();

private:
			status_t			_CreateSettingsView(const entry_ref& ref,
									const BMailAccountSettings& accountSettings,
									BMailProtocolSettings& settings);

private:
			BMailProtocolSettings& fSettings;
			BMailSettingsView*	fSettingsView;
			image_id			fImage;
};


#endif	/* CONFIG_VIEWS_H */
