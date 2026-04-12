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
 *   This file contains library initialization code.
 *   The required mimetypes and attribute-indices are created here.
 */


/**
 * @file InitLocaleKit.cpp
 * @brief Library initialization for the Locale Kit.
 *
 * Provides __initialize_locale_kit(), which is called once at liblocale.so
 * startup. It ensures that the filesystem attribute indices required for
 * catalog lookup exist on the boot volume, installs the catalog MIME type
 * with the correct attribute metadata and file extension, and loads the
 * system-wide translation catalog used by libbe itself.
 *
 * @see DefaultCatalog, MutableLocaleRoster
 */


#include <fs_attr.h>
#include <fs_index.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <DefaultCatalog.h>
#include <MutableLocaleRoster.h>
#include <SystemCatalog.h>


namespace BPrivate {

/** @brief The global system catalog used by libbe for its own translated strings. */
BCatalog gSystemCatalog;

}


using BPrivate::DefaultCatalog;
using BPrivate::MutableLocaleRoster;
using BPrivate::gSystemCatalog;


/**
 * @brief Create the named filesystem attribute index on the boot volume if absent.
 *
 * Queries the boot volume for an existing index with \a attrName; if none
 * exists the index is created as a B_STRING_TYPE index.
 *
 * @param attrName  Name of the attribute index to ensure exists.
 */
static void EnsureIndexExists(const char *attrName)
{
	BVolume bootVol;
	BVolumeRoster volRoster;
	if (volRoster.GetBootVolume(&bootVol) != B_OK)
		return;
	struct index_info idxInfo;
	if (fs_stat_index(bootVol.Device(), attrName, &idxInfo) != 0)
		fs_create_index(bootVol.Device(), attrName, B_STRING_TYPE, 0);
}


/*
 * prepares the system for use by the Locale Kit catalogs,
 * it makes sure that the required indices and mimetype exist:
 */
/**
 * @brief Install the catalog MIME type and required filesystem indices.
 *
 * Ensures the attribute indices for catalog language, signature, and
 * fingerprint exist on the boot volume, then installs the
 * DefaultCatalog::kCatMimeType MIME type with the appropriate attribute
 * info, ".catalog" file extension, and descriptive strings if it is not
 * already installed.
 */
static void
SetupCatalogBasics()
{
	// make sure the indices required for catalog-traversal are there:
	EnsureIndexExists(BLocaleRoster::kCatLangAttr);
	EnsureIndexExists(BLocaleRoster::kCatSigAttr);

	// install mimetype for default-catalog:
	BMimeType mt;
	status_t res = mt.SetTo(DefaultCatalog::kCatMimeType);
	if (res == B_OK && !mt.IsInstalled()) {
		// install supertype, if it isn't available
		BMimeType supertype;
		res = mt.GetSupertype(&supertype);
		if (res == B_OK && !supertype.IsInstalled()) {
			res = supertype.Install();
		}

		if (res == B_OK) {
			// info about the attributes of a catalog...
			BMessage attrMsg;
			// ...the catalog signature...
			attrMsg.AddString("attr:public_name", "Signature");
			attrMsg.AddString("attr:name", BLocaleRoster::kCatSigAttr);
			attrMsg.AddInt32("attr:type", B_STRING_TYPE);
			attrMsg.AddBool("attr:editable", false);
			attrMsg.AddBool("attr:viewable", true);
			attrMsg.AddBool("attr:extra", false);
			attrMsg.AddInt32("attr:alignment", 0);
			attrMsg.AddInt32("attr:width", 140);
			// ...the catalog language...
			attrMsg.AddString("attr:public_name", "Language");
			attrMsg.AddString("attr:name", BLocaleRoster::kCatLangAttr);
			attrMsg.AddInt32("attr:type", B_STRING_TYPE);
			attrMsg.AddBool("attr:editable", false);
			attrMsg.AddBool("attr:viewable", true);
			attrMsg.AddBool("attr:extra", false);
			attrMsg.AddInt32("attr:alignment", 0);
			attrMsg.AddInt32("attr:width", 60);
			// ...and the catalog fingerprint...
			attrMsg.AddString("attr:public_name", "Fingerprint");
			attrMsg.AddString("attr:name", BLocaleRoster::kCatFingerprintAttr);
			attrMsg.AddInt32("attr:type", B_INT32_TYPE);
			attrMsg.AddBool("attr:editable", false);
			attrMsg.AddBool("attr:viewable", true);
			attrMsg.AddBool("attr:extra", false);
			attrMsg.AddInt32("attr:alignment", 0);
			attrMsg.AddInt32("attr:width", 70);
			res = mt.SetAttrInfo(&attrMsg);
		}

		if (res == B_OK) {
			// file extensions (.catalog):
			BMessage extMsg;
			extMsg.AddString("extensions", "catalog");
			res = mt.SetFileExtensions(&extMsg);
		}

		if (res == B_OK) {
			// short and long descriptions:
			mt.SetShortDescription("Translation Catalog");
			res = mt.SetLongDescription("Catalog with translated application resources");
		}

		if (res == B_OK)
			res = mt.Install();
	}
}


/**
 * @brief Library entry point: initialize the Locale Kit at load time.
 *
 * Calls SetupCatalogBasics() to install MIME types and indices, then asks
 * MutableLocaleRoster to load the system catalog for libbe's own strings.
 */
void
__initialize_locale_kit()
{
	SetupCatalogBasics();

	MutableLocaleRoster::Default()->LoadSystemCatalog(&gSystemCatalog);
}
