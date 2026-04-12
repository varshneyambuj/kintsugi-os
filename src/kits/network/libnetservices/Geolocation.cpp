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
 *   Copyright 2014, Haiku, Inc. All Rights Reserved.
 *   Copyright 2019, Adrien Destugues, pulkomandy@pulkomandy.tk
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Geolocation.cpp
 * @brief Implementation of BGeolocation, a WiFi-based geolocation and reverse-geocoding client.
 *
 * BGeolocation enumerates visible wireless networks using BNetworkRoster and
 * BNetworkDevice, encodes their MAC addresses and signal levels as a JSON payload,
 * and submits the data to a configurable geolocation service (default: BeaconDB).
 * It also provides a reverse-geocoding lookup to resolve latitude/longitude to
 * an ISO country code via a configurable geocoding service.
 *
 * @see BNetworkRoster, BNetworkDevice, BHttpRequest
 */


#include <Geolocation.h>

#include <HttpRequest.h>
#include <Json.h>
#include <NetworkDevice.h>
#include <NetworkInterface.h>
#include <NetworkRoster.h>
#include <String.h>
#include <UrlProtocolRoster.h>
#include <UrlRequest.h>


namespace BPrivate {

namespace Network {

/**
 * @brief Internal BUrlProtocolListener that blocks the caller until the request completes.
 *
 * Acquires a mutex when ConnectionOpened() is called so that the main thread
 * can wait on the condition variable.  Signals the condition and releases the
 * mutex in RequestCompleted().
 */
class GeolocationListener: public BUrlProtocolListener
{
	public:
		/**
		 * @brief Construct and initialise the pthread synchronisation primitives.
		 */
		GeolocationListener()
		{
			pthread_cond_init(&fCompletion, NULL);
			pthread_mutex_init(&fLock, NULL);
		}

		/**
		 * @brief Destructor — destroys the condition variable and mutex.
		 */
		virtual	~GeolocationListener() {
			pthread_cond_destroy(&fCompletion);
			pthread_mutex_destroy(&fLock);
		}

		/**
		 * @brief Called when the HTTP connection is opened; acquires the mutex.
		 *
		 * @param caller  The BUrlRequest that opened the connection.
		 */
		void ConnectionOpened(BUrlRequest* caller)
		{
			pthread_mutex_lock(&fLock);
		}

		/**
		 * @brief Called when the request completes; signals the waiting thread.
		 *
		 * @param caller   The BUrlRequest that completed.
		 * @param success  true if the request succeeded.
		 */
		void RequestCompleted(BUrlRequest* caller, bool success) {
			pthread_cond_signal(&fCompletion);
			pthread_mutex_unlock(&fLock);
		}

		/** @brief Condition variable used to signal request completion. */
		pthread_cond_t fCompletion;

		/** @brief Mutex protecting the completion condition. */
		pthread_mutex_t fLock;
};


/**
 * @brief Construct a BGeolocation using the default service endpoints.
 *
 * Uses BeaconDB for geolocation and the configured default geocoding service.
 */
BGeolocation::BGeolocation()
	: fGeolocationService(kDefaultGeolocationService, true),
	fGeocodingService(kDefaultGeocodingService, true)
{
}


/**
 * @brief Construct a BGeolocation with custom service endpoints.
 *
 * Falls back to the defaults if either URL is invalid.
 *
 * @param geolocationService  URL of the WiFi-based geolocation REST endpoint.
 * @param geocodingService    URL of the reverse-geocoding REST endpoint.
 */
BGeolocation::BGeolocation(const BUrl& geolocationService,
	const BUrl& geocodingService)
	: fGeolocationService(geolocationService),
	fGeocodingService(geocodingService)
{
	if (!fGeolocationService.IsValid())
		fGeolocationService.SetUrlString(kDefaultGeolocationService, true);
	if (!fGeocodingService.IsValid())
		fGeocodingService.SetUrlString(kDefaultGeocodingService, true);
}


/**
 * @brief Determine the device's approximate geographic position from visible WiFi networks.
 *
 * Enumerates all network interfaces, collects MAC addresses and signal/noise
 * levels for every visible wireless network, and sends a JSON POST request to
 * the geolocation service.  Requires at least two visible networks; returns
 * B_DEVICE_NOT_FOUND otherwise.
 *
 * @param latitude   Output parameter set to the estimated latitude in degrees.
 * @param longitude  Output parameter set to the estimated longitude in degrees.
 * @return B_OK on success, B_DEVICE_NOT_FOUND if fewer than two networks were found,
 *         B_BAD_DATA if the request or response parsing fails, or another error code.
 */
status_t
BGeolocation::LocateSelf(float& latitude, float& longitude)
{
	// Enumerate wifi network and build JSON message
	BNetworkRoster& roster = BNetworkRoster::Default();
	uint32 interfaceCookie = 0;
	BNetworkInterface interface;

	BString query("{\n\t\"wifiAccessPoints\": [");
	int32 count = 0;

	while (roster.GetNextInterface(&interfaceCookie, interface) == B_OK) {
		BNetworkDevice device(interface.Name());
			// TODO is that the correct way to enumerate devices?

		uint32 networksCount = 0;
		wireless_network* networks = NULL;
		device.GetNetworks(networks, networksCount);
		for (uint32 i = 0; i < networksCount; i++) {
			const wireless_network& network = networks[i];
			if (count != 0)
				query += ',';

			count++;

			query += "\n\t\t{ \"macAddress\": \"";
			query += network.address.ToString().ToUpper();
			query += "\", \"signalStrength\": ";
			query << (int)network.signal_strength;
			query += ", \"signalToNoiseRatio\": ";
			query << (int)network.noise_level;
			query += " }";
		}
		delete[] networks;
	}

	query += "\n\t]\n}\n";

	// Check that we have enough data (we need at least 2 networks)
	if (count < 2)
		return B_DEVICE_NOT_FOUND;

	GeolocationListener listener;
	BMallocIO resultBuffer;

	// Send Request (POST JSON message)
	BUrlRequest* request = BUrlProtocolRoster::MakeRequest(fGeolocationService,
		&resultBuffer, &listener);
	if (request == NULL)
		return B_BAD_DATA;

	BHttpRequest* http = dynamic_cast<BHttpRequest*>(request);
	if (http == NULL) {
		delete request;
		return B_BAD_DATA;
	}

	// There are no API keys for BeaconDB, instead they ask to set a user agent identifying the
	// software. Let's also include a contact address in case something goes wrong.
	http->SetUserAgent("Haiku/R1 haiku-development@freelists.org");

	http->SetMethod(B_HTTP_POST);

	BHttpHeaders headers;
	headers.AddHeader("Content-Type", "application/json");
	http->SetHeaders(headers);

	BMemoryIO* io = new BMemoryIO(query.String(), query.Length());
	http->AdoptInputData(io, query.Length());

	status_t result = http->Run();
	if (result < 0) {
		delete http;
		return result;
	}

	pthread_mutex_lock(&listener.fLock);
	while (http->IsRunning())
		pthread_cond_wait(&listener.fCompletion, &listener.fLock);
	pthread_mutex_unlock(&listener.fLock);

	// Parse reply
	const BHttpResult& reply = (const BHttpResult&)http->Result();
	if (reply.StatusCode() != 200) {
		delete http;
		return B_ERROR;
	}

	BMessage data;
	result = BJson::Parse((char*)resultBuffer.Buffer(), data);
	delete http;
	if (result != B_OK) {
		return result;
	}

	BMessage location;
	result = data.FindMessage("location", &location);
	if (result != B_OK)
		return result;

	double lat, lon;
	result = location.FindDouble("lat", &lat);
	if (result != B_OK)
		return result;
	result = location.FindDouble("lng", &lon);
	if (result != B_OK)
		return result;

	latitude = lat;
	longitude = lon;

	return result;
}


/**
 * @brief Resolve a latitude/longitude coordinate to an ISO country code.
 *
 * Appends the coordinates as query parameters to the geocoding service URL,
 * issues a GET request, and parses the response body as a country code string.
 *
 * @param latitude   The latitude in degrees to look up.
 * @param longitude  The longitude in degrees to look up.
 * @param country    Output BCountry object set to the resolved country on success.
 * @return B_OK on success, B_BAD_DATA if the HTTP request cannot be created,
 *         B_ERROR on a non-200 HTTP response, or another error code on failure.
 */
status_t
BGeolocation::Country(const float latitude, const float longitude,
	BCountry& country)
{
	// Prepare the request URL
	BUrl url(fGeocodingService);
	BString requestString;
	requestString.SetToFormat("%s&lat=%f&lng=%f", url.Request().String(), latitude,
		longitude);
	url.SetPath("/countryCode");
	url.SetRequest(requestString);

	GeolocationListener listener;
	BMallocIO resultBuffer;
	BUrlRequest* request = BUrlProtocolRoster::MakeRequest(url,
		&resultBuffer, &listener);
	if (request == NULL)
		return B_BAD_DATA;

	BHttpRequest* http = dynamic_cast<BHttpRequest*>(request);
	if (http == NULL) {
		delete request;
		return B_BAD_DATA;
	}

	status_t result = http->Run();
	if (result < 0) {
		delete http;
		return result;
	}

	pthread_mutex_lock(&listener.fLock);
	while (http->IsRunning()) {
		pthread_cond_wait(&listener.fCompletion, &listener.fLock);
	}
	pthread_mutex_unlock(&listener.fLock);

	// Parse reply
	const BHttpResult& reply = (const BHttpResult&)http->Result();
	if (reply.StatusCode() != 200) {
		delete http;
		return B_ERROR;
	}

	off_t length = 0;
	resultBuffer.GetSize(&length);
	length -= 2; // Remove \r\n from response
	BString countryCode((char*)resultBuffer.Buffer(), (int32)length);
	return country.SetTo(countryCode);
}


/** @brief Default geolocation service endpoint (BeaconDB). */
const char* BGeolocation::kDefaultGeolocationService = "https://api.beacondb.net/v1/geolocate";

#ifdef HAVE_DEFAULT_GEOLOCATION_SERVICE_KEY

#include "DefaultGeolocationServiceKey.h"

/** @brief Default geocoding service endpoint with embedded API key. */
const char* BGeolocation::kDefaultGeocodingService
	= "https://secure.geonames.org/?username="
		DEFAULT_GEOCODING_SERVICE_KEY;

#else

/** @brief Default geocoding service endpoint (empty when no key is configured). */
const char* BGeolocation::kDefaultGeocodingService = "";

#endif

}	// namespace Network

}	// namespace BPrivate
