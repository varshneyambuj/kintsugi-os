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
 * Incorporates work from Haiku, Inc. covered by:
 * Copyright (c) 2008 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the the MIT licence.
 */

/** @file MediaRoster.h
 *  @brief Defines BMediaRoster, the main API entry point for the Media Kit.
 */

#ifndef _MEDIA_ROSTER_H
#define _MEDIA_ROSTER_H

#include <MediaDefs.h>
#include <MediaNode.h>


/** @brief Hardware bus types used when querying recommended audio buffer sizes. */
typedef enum {
	B_ISA_BUS,
	B_PCI_BUS,
	B_PCMCIA_BUS,
	B_UNKNOWN_BUS = 0x80
} bus_type;


class BBufferGroup;
class BMediaAddOn;
class BMimeType;
class BParameterWeb;
class BString;

struct dormant_flavor_info;
struct entry_ref;

namespace BPrivate { namespace media {
	class DefaultDeleter;
	class BMediaRosterEx;
} } // BPrivate::media


/** @brief The central broker for Media Kit services: node management, connections, and routing.
 *
 *  BMediaRoster is a BLooper subclass that provides the primary interface for
 *  discovering, connecting, and controlling media nodes.  Obtain the singleton
 *  instance via Roster() or CurrentRoster().
 */
class BMediaRoster : public BLooper {
public:

	/** @brief Returns the global BMediaRoster instance, creating it if necessary.
	 *  @param _error If non-NULL, receives the creation error code.
	 *  @return Pointer to the singleton BMediaRoster.
	 */
	static	BMediaRoster*		Roster(status_t* _error = NULL);

	/** @brief Returns the global BMediaRoster instance without creating one.
	 *  @return Pointer to the existing instance, or NULL if none exists.
	 */
	static	BMediaRoster*		CurrentRoster();

	/** @brief Returns true if the media server is currently running. */
	static	bool				IsRunning();

	/** @brief Retrieves the default video input node.
	 *  @param _node On return, the video input node.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetVideoInput(media_node* _node);

	/** @brief Retrieves the default audio input node.
	 *  @param _node On return, the audio input node.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetAudioInput(media_node* _node);

	/** @brief Retrieves the default video output node.
	 *  @param _node On return, the video output node.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetVideoOutput(media_node* _node);

	/** @brief Retrieves the default audio mixer node.
	 *  @param _node On return, the audio mixer node.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetAudioMixer(media_node* _node);

	/** @brief Retrieves the default audio output node (use the mixer in common cases).
	 *  @param _node On return, the audio output node.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetAudioOutput(media_node* _node);

	/** @brief Retrieves the default audio output node along with its input details.
	 *  @param _node On return, the audio output node.
	 *  @param _inputId On return, the input ID on the output node.
	 *  @param _inputName On return, the input's name string.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetAudioOutput(media_node* _node,
									int32* _inputId, BString* _inputName);

	/** @brief Retrieves the system time source node.
	 *  @param _node On return, the time source node.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetTimeSource(media_node* _node);

	/** @brief Sets the default video input to a live node.
	 *  @param producer The node to use as the default video input.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetVideoInput(const media_node& producer);

	/** @brief Sets the default video input to a dormant node.
	 *  @param producer Dormant node info identifying the desired input.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetVideoInput(
									const dormant_node_info& producer);

	/** @brief Sets the default audio input to a live node.
	 *  @param producer The node to use as the default audio input.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetAudioInput(const media_node& producer);

	/** @brief Sets the default audio input to a dormant node.
	 *  @param producer Dormant node info identifying the desired input.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetAudioInput(
									const dormant_node_info& producer);

	/** @brief Sets the default video output to a live node.
	 *  @param consumer The node to use as the default video output.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetVideoOutput(const media_node& consumer);

	/** @brief Sets the default video output to a dormant node.
	 *  @param consumer Dormant node info identifying the desired output.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetVideoOutput(
									const dormant_node_info& consumer);

	/** @brief Sets the default audio output to a live node.
	 *  @param consumer The node to use as the default audio output.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetAudioOutput(
									const media_node& consumer);

	/** @brief Sets the default audio output to a specific input on a live node.
	 *  @param inputToOutput The media_input describing the target input.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetAudioOutput(
									const media_input& inputToOutput);

	/** @brief Sets the default audio output to a dormant node.
	 *  @param consumer Dormant node info identifying the desired output.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetAudioOutput(
									const dormant_node_info& consumer);

	/** @brief Obtains a usable clone of a node identified by its ID.
	 *  @param node The media_node_id of the desired node.
	 *  @param clone On return, a media_node reference to the node.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetNodeFor(media_node_id node,
									media_node* clone);

	/** @brief Obtains a clone of the system time source node.
	 *  @param clone On return, the system time source node.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetSystemTimeSource(media_node* clone);

	/** @brief Releases a previously obtained node clone; may destroy the node.
	 *  @param node The node to release.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			ReleaseNode(const media_node& node);

	/** @brief Creates a BTimeSource proxy for the given node's time source.
	 *  @param for_node The node whose time source is desired.
	 *  @return A BTimeSource pointer; call Release() when done.
	 */
			BTimeSource*		MakeTimeSourceFor(const media_node& for_node);

	/** @brief Connects a producer source to a consumer destination.
	 *  @param from The source to connect from.
	 *  @param to The destination to connect to.
	 *  @param _inOutFormat In/out: the negotiated format.
	 *  @param _output On return, the actual output used.
	 *  @param _input On return, the actual input used.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			Connect(const media_source& from,
									const media_destination& to,
									media_format* _inOutFormat,
									media_output* _output,
									media_input* _input);

	/** @brief Flags for Connect(). */
			enum connect_flags {
				B_CONNECT_MUTED = 0x1  /**< Start the connection muted. */
			};

	/** @brief Connects a source to a destination with additional flags.
	 *  @param from The source to connect from.
	 *  @param to The destination to connect to.
	 *  @param _inOutFormat In/out: the negotiated format.
	 *  @param _output On return, the actual output used.
	 *  @param _input On return, the actual input used.
	 *  @param flags Combination of connect_flags values.
	 *  @param _reserved Reserved; pass NULL.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			Connect(const media_source& from,
									const media_destination& to,
									media_format* _inOutFormat,
									media_output* _output,
									media_input* _input,
									uint32 flags, void* _reserved = NULL);

	/** @brief Disconnects a source/destination pair identified by node IDs.
	 *  @param sourceNode Node ID of the producer.
	 *  @param source The source endpoint.
	 *  @param destinationNode Node ID of the consumer.
	 *  @param destination The destination endpoint.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			Disconnect(media_node_id sourceNode,
									const media_source& source,
									media_node_id destinationNode,
									const media_destination& destination);

	/** @brief Disconnects a connection described by its output and input (Haiku extension).
	 *  @param output The output side of the connection.
	 *  @param input The input side of the connection.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			Disconnect(const media_output& output,
									const media_input& input);

	/** @brief Starts a node at the given performance time.
	 *  @param node The node to start.
	 *  @param atPerformanceTime When to start.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			StartNode(const media_node& node,
									bigtime_t atPerformanceTime);

	/** @brief Stops a node at the given performance time.
	 *  @param node The node to stop.
	 *  @param atPerformanceTime When to stop.
	 *  @param immediate If true, ignore atPerformanceTime and stop now.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			StopNode(const media_node& node,
									bigtime_t atPerformanceTime,
									bool immediate = false);

	/** @brief Seeks a running node to a new media time.
	 *  @param node The node to seek (must be running).
	 *  @param toMediaTime The target media time.
	 *  @param atPerformanceTime When to apply the seek.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SeekNode(const media_node& node,
									bigtime_t toMediaTime,
									bigtime_t atPerformanceTime = 0);

	/** @brief Starts a time source at the given real time.
	 *  @param node The time source node to start.
	 *  @param atRealTime Real time at which to start.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			StartTimeSource(const media_node& node,
									bigtime_t atRealTime);

	/** @brief Stops a time source at the given real time.
	 *  @param node The time source node to stop.
	 *  @param atRealTime Real time at which to stop.
	 *  @param immediate If true, stop immediately.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			StopTimeSource(const media_node& node,
									bigtime_t atRealTime,
									bool immediate = false);

	/** @brief Seeks a time source to a new performance time.
	 *  @param node The time source to seek.
	 *  @param toPerformanceTime Target performance time.
	 *  @param atRealTime Real time at which to apply the seek.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SeekTimeSource(const media_node& node,
									bigtime_t toPerformanceTime,
									bigtime_t atRealTime);

	/** @brief Synchronously waits until a node reaches the given time.
	 *  @param node The node to wait for.
	 *  @param atTime The performance time to sync to.
	 *  @param timeout Maximum wait time in microseconds.
	 *  @return B_OK when synced, or an error code.
	 */
			status_t			SyncToNode(const media_node& node,
									bigtime_t atTime,
									bigtime_t timeout = B_INFINITE_TIMEOUT);

	/** @brief Sets the run mode for a node.
	 *  @param node The node to configure.
	 *  @param mode The desired run_mode.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetRunModeNode(const media_node& node,
									BMediaNode::run_mode mode);

	/** @brief Synchronously prerolls a node (pre-buffers data for immediate playback).
	 *  @param node The node to preroll.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			PrerollNode(const media_node& node);

	/** @brief Starts a node and schedules it to stop at a later time.
	 *  @param node The node to roll.
	 *  @param startPerformance Start performance time.
	 *  @param stopPerformance Stop performance time.
	 *  @param atMediaTime Media time offset.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			RollNode(const media_node& node,
									bigtime_t startPerformance,
									bigtime_t stopPerformance,
									bigtime_t atMediaTime
										= -B_INFINITE_TIMEOUT);

	/** @brief Sets the additional latency a producer adds when in B_RECORDING mode.
	 *  @param node The producer node.
	 *  @param delay The extra delay in microseconds.
	 *  @param mode The run_mode this delay applies to (should be B_RECORDING).
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetProducerRunModeDelay(const media_node& node,
									bigtime_t delay,
									BMediaNode::run_mode mode
										= BMediaNode::B_RECORDING);

	/** @brief Sets the playback rate for a producer node.
	 *  @param producer The producer node.
	 *  @param numer Numerator of the rate fraction.
	 *  @param denom Denominator of the rate fraction.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetProducerRate(const media_node& producer,
									int32 numer, int32 denom);

	/** @brief Retrieves detailed information about a live node.
	 *  @param node The node to query.
	 *  @param _liveInfo On return, the live_node_info.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetLiveNodeInfo(const media_node& node,
									live_node_info* _liveInfo);

	/** @brief Retrieves a list of currently running nodes, optionally filtered.
	 *  @param _liveNodes Array to receive live_node_info entries.
	 *  @param inOutTotalCount In: array capacity; Out: actual count.
	 *  @param hasInput Optional input format filter.
	 *  @param hasOutput Optional output format filter.
	 *  @param name Optional name substring filter.
	 *  @param nodeKinds Optional kind-flag filter (e.g. B_BUFFER_PRODUCER).
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetLiveNodes(live_node_info* _liveNodes,
									int32* inOutTotalCount,
									const media_format* hasInput = NULL,
									const media_format* hasOutput = NULL,
									const char* name = NULL,
									uint64 nodeKinds = 0);

	/** @brief Retrieves free (unconnected) inputs of a node.
	 *  @param node The node to query.
	 *  @param _freeInputsBuffer Array to receive media_input entries.
	 *  @param bufferCapacity Capacity of the array.
	 *  @param _foundCount On return, the number of entries written.
	 *  @param filterType Optional media_type filter.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetFreeInputsFor(const media_node& node,
									media_input* _freeInputsBuffer,
									int32 bufferCapacity,
									int32* _foundCount,
									media_type filterType
										= B_MEDIA_UNKNOWN_TYPE);

	/** @brief Retrieves connected inputs of a node.
	 *  @param node The node to query.
	 *  @param _activeInputsBuffer Array to receive media_input entries.
	 *  @param bufferCapacity Capacity of the array.
	 *  @param _foundCount On return, the number of entries written.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetConnectedInputsFor(const media_node& node,
									media_input* _activeInputsBuffer,
									int32 bufferCapacity,
									int32* _foundCount);

	/** @brief Retrieves all inputs (free and connected) of a node.
	 *  @param node The node to query.
	 *  @param _inputsBuffer Array to receive media_input entries.
	 *  @param bufferCapacity Capacity of the array.
	 *  @param _foundCount On return, the number of entries written.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetAllInputsFor(const media_node& node,
									media_input* _inputsBuffer,
									int32 bufferCapacity,
									int32* _foundCount);

	/** @brief Retrieves free (unconnected) outputs of a node.
	 *  @param node The node to query.
	 *  @param _freeOutputsBuffer Array to receive media_output entries.
	 *  @param bufferCapacity Capacity of the array.
	 *  @param _foundCount On return, the number of entries written.
	 *  @param filterType Optional media_type filter.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetFreeOutputsFor(const media_node& node,
									media_output* _freeOutputsBuffer,
									int32 bufferCapacity,
									int32* _foundCount,
									media_type filterType
										= B_MEDIA_UNKNOWN_TYPE);

	/** @brief Retrieves connected outputs of a node.
	 *  @param node The node to query.
	 *  @param _activeOutputsBuffer Array to receive media_output entries.
	 *  @param bufferCapacity Capacity of the array.
	 *  @param _foundCount On return, the number of entries written.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetConnectedOutputsFor(const media_node& node,
									media_output* _activeOutputsBuffer,
									int32 bufferCapacity,
									int32* _foundCount);

	/** @brief Retrieves all outputs (free and connected) of a node.
	 *  @param node The node to query.
	 *  @param _outputsBuffer Array to receive media_output entries.
	 *  @param bufferCapacity Capacity of the array.
	 *  @param _foundCount On return, the number of entries written.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetAllOutputsFor(const media_node& node,
									media_output* _outputsBuffer,
									int32 bufferCapacity,
									int32* _foundCount);

	/** @brief Subscribes a BMessenger to all media notifications.
	 *  @param target The BMessenger to receive notifications.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			StartWatching(const BMessenger& target);

	/** @brief Subscribes a BMessenger to a specific notification type.
	 *  @param target The BMessenger to receive notifications.
	 *  @param notificationType The notification 'what' code to watch.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			StartWatching(const BMessenger& target,
									int32 notificationType);

	/** @brief Subscribes a BMessenger to notifications from a specific node.
	 *  @param target The BMessenger to receive notifications.
	 *  @param node The node to watch.
	 *  @param notificationType The notification 'what' code to watch.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			StartWatching(const BMessenger& target,
									const media_node& node,
									int32 notificationType);

	/** @brief Unsubscribes a BMessenger from all media notifications.
	 *  @param target The BMessenger to stop.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			StopWatching(const BMessenger& target);

	/** @brief Unsubscribes a BMessenger from a specific notification type.
	 *  @param target The BMessenger to stop.
	 *  @param notificationType The notification type to stop watching.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			StopWatching(const BMessenger& target,
									int32 notificationType);

	/** @brief Unsubscribes a BMessenger from notifications for a specific node.
	 *  @param target The BMessenger to stop.
	 *  @param node The node to stop watching.
	 *  @param notificationType The notification type to stop watching.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			StopWatching(const BMessenger& target,
									const media_node& node,
									int32 notificationType);

	/** @brief Registers a node with the media server.
	 *  @param node The node to register.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			RegisterNode(BMediaNode* node);

	/** @brief Unregisters a node from the media server.
	 *  @param node The node to unregister.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			UnregisterNode(BMediaNode* node);

	/** @brief Sets the time source for a specific node.
	 *  @param node The node whose time source should be set.
	 *  @param timeSource The time source node to use.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetTimeSourceFor(media_node_id node,
									media_node_id timeSource);

	/** @brief Retrieves a copy of the parameter web for the given node.
	 *  @param node The node to query.
	 *  @param _web On return, a pointer to the BParameterWeb (caller owns it).
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetParameterWebFor(const media_node& node,
									BParameterWeb** _web);

	/** @brief Launches the control panel for a node.
	 *  @param node The node whose control panel to open.
	 *  @param _messenger If non-NULL, receives a BMessenger for the panel.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			StartControlPanel(const media_node& node,
									BMessenger* _messenger = NULL);

	/** @brief Retrieves a list of dormant (not yet instantiated) nodes matching filters.
	 *  @param _info Array to receive dormant_node_info entries.
	 *  @param _inOutCount In: array capacity; Out: actual count.
	 *  @param _hasInput Optional input format filter.
	 *  @param _hasOutput Optional output format filter.
	 *  @param name Optional name substring filter.
	 *  @param requireKinds Required node kind flags.
	 *  @param denyKinds Excluded node kind flags.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetDormantNodes(dormant_node_info* _info,
									int32* _inOutCount,
									const media_format* _hasInput = NULL,
									const media_format* _hasOutput = NULL,
									const char* name = NULL,
									uint64 requireKinds = 0,
									uint64 denyKinds = 0);

	/** @brief Instantiates a dormant node with explicit placement flags.
	 *  @param info The dormant node to instantiate.
	 *  @param _node On return, the instantiated node.
	 *  @param flags B_FLAVOR_IS_GLOBAL or B_FLAVOR_IS_LOCAL.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			InstantiateDormantNode(
									const dormant_node_info& info,
									media_node* _node,
									uint32 flags);

	/** @brief Instantiates a dormant node using default placement.
	 *  @param info The dormant node to instantiate.
	 *  @param _node On return, the instantiated node.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			InstantiateDormantNode(
									const dormant_node_info& info,
									media_node* _node);

	/** @brief Finds the dormant node info for a live node.
	 *  @param node The live node to query.
	 *  @param _info On return, the dormant_node_info.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetDormantNodeFor(const media_node& node,
									dormant_node_info* _info);

	/** @brief Retrieves detailed flavor information for a dormant node.
	 *  @param info The dormant node.
	 *  @param _flavor On return, the dormant_flavor_info.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetDormantFlavorInfoFor(
									const dormant_node_info& info,
									dormant_flavor_info* _flavor);

	/** @brief Queries the total downstream latency of a producer.
	 *  @param producer The producer node.
	 *  @param _latency On return, the latency in microseconds.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetLatencyFor(const media_node& producer,
									bigtime_t* _latency);

	/** @brief Queries the start-up latency of a producer.
	 *  @param producer The producer node.
	 *  @param _latency On return, the initial latency in microseconds.
	 *  @param _flags Optional; on return, associated flags.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetInitialLatencyFor(
									const media_node& producer,
									bigtime_t* _latency,
									uint32* _flags = NULL);

	/** @brief Queries the start latency of a time source node.
	 *  @param timeSource The time source node.
	 *  @param _latency On return, the start latency in microseconds.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetStartLatencyFor(
									const media_node& timeSource,
									bigtime_t* _latency);

	/** @brief Returns the file formats a file-interface node supports.
	 *  @param fileInterface The file-interface node.
	 *  @param _formatsBuffer Array to receive media_file_format entries.
	 *  @param _inOutNumInfos In: capacity; Out: actual count.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetFileFormatsFor(
									const media_node& fileInterface,
									media_file_format* _formatsBuffer,
									int32* _inOutNumInfos);

	/** @brief Binds a file-interface node to a file.
	 *  @param fileInterface The node to bind.
	 *  @param file The file to open.
	 *  @param createAndTruncate True to create or truncate the file.
	 *  @param _length On return, the file duration in microseconds (if not creating).
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetRefFor(const media_node& fileInterface,
									const entry_ref& file,
									bool createAndTruncate,
									bigtime_t* _length);

	/** @brief Retrieves the file currently bound to a file-interface node.
	 *  @param node The file-interface node.
	 *  @param _ref On return, the entry_ref of the current file.
	 *  @param mimeType If non-NULL, receives the MIME type string.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetRefFor(const media_node& node,
									entry_ref* _ref,
									BMimeType* mimeType = NULL);

	/** @brief Asks a file-interface node to probe a file for compatibility.
	 *  @param fileInterface The node to query.
	 *  @param ref The file to probe.
	 *  @param _mimeType On return, the detected MIME type.
	 *  @param _capability On return, a 0.0-1.0 confidence score.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SniffRefFor(const media_node& fileInterface,
									const entry_ref& ref, BMimeType* _mimeType,
									float* _capability);

	/** @brief Finds a dormant node that can play the given file.
	 *  @param ref The file to find a player for.
	 *  @param requireNodeKinds Required node kind flags.
	 *  @param _node On return, the best dormant node for the file.
	 *  @param _mimeType If non-NULL, receives the detected MIME type.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SniffRef(const entry_ref& ref,
									uint64 requireNodeKinds,
									dormant_node_info* _node,
									BMimeType* _mimeType = NULL);

	/** @brief Finds a dormant node matching a MIME type and node kind flags.
	 *  @param type The MIME type to look up.
	 *  @param requireNodeKinds Required node kind flags.
	 *  @param _info On return, the matching dormant_node_info.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetDormantNodeForType(const BMimeType& type,
									uint64 requireNodeKinds,
									dormant_node_info* _info);

	/** @brief Lists the readable file formats supported by a dormant node.
	 *  @param node The dormant node to query.
	 *  @param _readFormatsBuffer Array to receive formats.
	 *  @param bufferCapacity Capacity of the array.
	 *  @param _foundCount On return, the number of formats found.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetReadFileFormatsFor(
									const dormant_node_info& node,
									media_file_format* _readFormatsBuffer,
									int32 bufferCapacity, int32* _foundCount);

	/** @brief Lists the writable file formats supported by a dormant node.
	 *  @param node The dormant node to query.
	 *  @param _writeFormatsBuffer Array to receive formats.
	 *  @param bufferCapacity Capacity of the array.
	 *  @param _foundCount On return, the number of formats found.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetWriteFileFormatsFor(
									const dormant_node_info& node,
									media_file_format* _writeFormatsBuffer,
									int32 bufferCapacity, int32* _foundCount);

	/** @brief Retrieves the current format of a connected output.
	 *  @param output The output to query.
	 *  @param _inOutFormat On return, the current format.
	 *  @param flags Reserved; pass 0.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetFormatFor(const media_output& output,
									media_format* _inOutFormat,
									uint32 flags = 0);

	/** @brief Retrieves the current format of a connected input.
	 *  @param input The input to query.
	 *  @param _inOutFormat On return, the current format.
	 *  @param flags Reserved; pass 0.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetFormatFor(const media_input& input,
									media_format* _inOutFormat,
									uint32 flags = 0);

	/** @brief Retrieves the preferred format of a node.
	 *  @param node The node to query.
	 *  @param _inOutFormat On return, the node's preferred format.
	 *  @param quality Quality hint (0.0-1.0).
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetFormatFor(const media_node& node,
									media_format* _inOutFormat,
									float quality = B_MEDIA_ANY_QUALITY);

	/** @brief Retrieves attribute data for a node.
	 *  @param node The node to query.
	 *  @param outArray Array to receive media_node_attribute entries.
	 *  @param inMaxCount Capacity of the array.
	 *  @return The number of attributes written, or a negative error code.
	 */
			ssize_t				GetNodeAttributesFor(const media_node& node,
									media_node_attribute* outArray,
									size_t inMaxCount);

	/** @brief Finds the node ID that owns the given source or destination port.
	 *  @param sourceOrDestinationPort The port to look up.
	 *  @return The media_node_id, or a negative error code.
	 */
			media_node_id		NodeIDFor(port_id sourceOrDestinationPort);

	/** @brief Retrieves the live instances of a specific add-on flavor.
	 *  @param addon The add-on ID.
	 *  @param flavor The flavor ID within the add-on.
	 *  @param _id Array to receive media_node_id values.
	 *  @param _inOutCount In: capacity; Out: actual count (defaults to 1).
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetInstancesFor(media_addon_id addon,
									int32 flavor,media_node_id* _id,
									int32* _inOutCount = NULL);

	/** @brief Returns the recommended audio buffer size for the given parameters.
	 *  @param channelCount Number of audio channels.
	 *  @param sampleFormat The raw audio sample format.
	 *  @param frameRate Sample rate in Hz.
	 *  @param busType The hardware bus type.
	 *  @return Recommended buffer size in bytes, or a negative error code.
	 */
			ssize_t				AudioBufferSizeFor(int32 channelCount,
									uint32 sampleFormat, float frameRate,
									bus_type busType = B_UNKNOWN_BUS);

	/** @brief Queries the Media Kit for a specific capability flag.
	 *  @param cap The capability to query.
	 *  @param buffer Buffer to receive capability data.
	 *  @param maxSize Capacity of the buffer.
	 *  @return Size of data written, 0 if present without data, or negative if absent.
	 */
	static	ssize_t				MediaFlags(media_flags cap, void* buffer,
									size_t maxSize);

	// BLooper overrides
	/** @brief Processes incoming BMessages on the roster's looper thread. */
	virtual	void				MessageReceived(BMessage* message);
	/** @brief Returns true to allow the looper to quit. */
	virtual	bool				QuitRequested();

	/** @brief BHandler scripting support. */
	virtual	BHandler*			ResolveSpecifier(BMessage* message,
									int32 index, BMessage* specifier,
									int32 form, const char* property);
	/** @brief BHandler scripting support. */
	virtual	status_t			GetSupportedSuites(BMessage* data);

	virtual						~BMediaRoster();

private:

	// Reserving virtual function slots.
	virtual	status_t			_Reserved_MediaRoster_0(void*);
	virtual	status_t			_Reserved_MediaRoster_1(void*);
	virtual	status_t			_Reserved_MediaRoster_2(void*);
	virtual	status_t			_Reserved_MediaRoster_3(void*);
	virtual	status_t			_Reserved_MediaRoster_4(void*);
	virtual	status_t			_Reserved_MediaRoster_5(void*);
	virtual	status_t			_Reserved_MediaRoster_6(void*);
	virtual	status_t			_Reserved_MediaRoster_7(void*);

	friend class BPrivate::media::DefaultDeleter;
	friend class BPrivate::media::BMediaRosterEx;

	// Constructor is private, since you are supposed to use
	// Roster() or CurrentRoster().
								BMediaRoster();

	// Those methods are deprecated or considered useless
	// NOTE: planned to be removed once we break the API.
			status_t			SetOutputBuffersFor(const media_source& output,
									BBufferGroup* group,
									bool willReclaim = false);

			status_t			SetRealtimeFlags(uint32 enabledFlags);
			status_t			GetRealtimeFlags(uint32* _enabledFlags);

	static	status_t			ParseCommand(BMessage& reply);

			status_t			GetDefaultInfo(media_node_id forDefault,
									BMessage& _config);
			status_t			SetRunningDefault(media_node_id forDefault,
									const media_node& node);
	// End of deprecated methods

private:
			uint32				_reserved_media_roster_[67];

	static	BMediaRoster*		sDefaultInstance;
};


#endif // _MEDIA_ROSTER_H

