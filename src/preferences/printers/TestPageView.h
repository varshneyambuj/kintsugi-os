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
 * MIT License. Copyright 2011, Haiku.
 * Original authors: Philippe Houdoin.
 */

/** @file TestPageView.h
    @brief Composite BView that renders a printer test page. */

#ifndef _PRINTERS_TESTPAGEVIEW_H
#define _PRINTERS_TESTPAGEVIEW_H


#include <View.h>

class PrinterItem;

/**
 * @brief Top-level view used by TestPageWindow to render a single test
 *        page.
 */
class TestPageView : public BView {
public:
							TestPageView(BRect rect, PrinterItem* printer);

		void				AttachedToWindow();
		void				DrawAfterChildren(BRect rect);

private:
		PrinterItem*		fPrinter;
};

#endif
