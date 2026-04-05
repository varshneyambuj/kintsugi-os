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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2005, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Lotz <mmlr@mlotz.ch>
 */

/** @file TypeConstants.cpp
 *  @brief MIME type constant definitions for URL protocols.
 *
 *  Defines the MIME type strings for various URL schemes (HTTP, HTTPS, FTP,
 *  etc.) that are exported by libbe for compatibility with BeOS R5.
 */

//! These type constants are exported by libbe under R5.

/** @brief MIME type for HTTP URLs. */
const char *B_URL_HTTP = "application/x-vnd.Be.URL.http";
/** @brief MIME type for HTTPS URLs. */
const char *B_URL_HTTPS = "application/x-vnd.Be.URL.https";
/** @brief MIME type for FTP URLs. */
const char *B_URL_FTP = "application/x-vnd.Be.URL.ftp";
/** @brief MIME type for Gopher URLs. */
const char *B_URL_GOPHER = "application/x-vnd.Be.URL.gopher";
/** @brief MIME type for mailto URLs. */
const char *B_URL_MAILTO = "application/x-vnd.Be.URL.mailto";
/** @brief MIME type for news URLs. */
const char *B_URL_NEWS = "application/x-vnd.Be.URL.news";
/** @brief MIME type for NNTP URLs. */
const char *B_URL_NNTP = "application/x-vnd.Be.URL.nntp";
/** @brief MIME type for Telnet URLs. */
const char *B_URL_TELNET = "application/x-vnd.Be.URL.telnet";
/** @brief MIME type for rlogin URLs. */
const char *B_URL_RLOGIN = "application/x-vnd.Be.URL.rlogin";
/** @brief MIME type for TN3270 URLs. */
const char *B_URL_TN3270 = "application/x-vnd.Be.URL.tn3270";
/** @brief MIME type for WAIS URLs. */
const char *B_URL_WAIS = "application/x-vnd.Be.URL.wais";
/** @brief MIME type for file URLs. */
const char *B_URL_FILE = "application/x-vnd.Be.URL.file";
