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
 * Copyright 2009, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT license.
 */

/** @file ParameterWeb.h
 *  @brief Defines BParameterWeb, BParameterGroup, BParameter, and parameter subclasses.
 */

#ifndef _CONTROL_WEB_H
#define _CONTROL_WEB_H


#include <Flattenable.h>
#include <MediaDefs.h>
#include <MediaNode.h>
#include <TypeConstants.h>


// Parameter Kinds

/** @brief Generic parameter kind string. */
extern const char* const B_GENERIC;

// slider controls
/** @brief Master gain parameter kind. */
extern const char* const B_MASTER_GAIN;
/** @brief Gain parameter kind. */
extern const char* const B_GAIN;
/** @brief Balance parameter kind. */
extern const char* const B_BALANCE;
/** @brief Frequency parameter kind. */
extern const char* const B_FREQUENCY;
/** @brief Level parameter kind. */
extern const char* const B_LEVEL;
/** @brief Shuttle speed parameter kind. */
extern const char* const B_SHUTTLE_SPEED;
/** @brief Crossfade parameter kind (0-100, first to second). */
extern const char* const B_CROSSFADE;
/** @brief Equalization parameter kind (in dB). */
extern const char* const B_EQUALIZATION;

// compression controls
/** @brief Compression parameter kind. */
extern const char* const B_COMPRESSION;
/** @brief Quality parameter kind. */
extern const char* const B_QUALITY;
/** @brief Bit rate parameter kind (bits/s). */
extern const char* const B_BITRATE;
/** @brief GOP size parameter kind. */
extern const char* const B_GOP_SIZE;

// selector controls
/** @brief Mute parameter kind. */
extern const char* const B_MUTE;
/** @brief Enable parameter kind. */
extern const char* const B_ENABLE;
/** @brief Input multiplexer parameter kind. */
extern const char* const B_INPUT_MUX;
/** @brief Output multiplexer parameter kind. */
extern const char* const B_OUTPUT_MUX;
/** @brief TV tuner channel parameter kind. */
extern const char* const B_TUNER_CHANNEL;
/** @brief Track selector parameter kind. */
extern const char* const B_TRACK;
/** @brief Recording state parameter kind. */
extern const char* const B_RECSTATE;
/** @brief Shuttle mode parameter kind. */
extern const char* const B_SHUTTLE_MODE;
/** @brief Resolution parameter kind. */
extern const char* const B_RESOLUTION;
/** @brief Color space parameter kind. */
extern const char* const B_COLOR_SPACE;
/** @brief Frame rate parameter kind. */
extern const char* const B_FRAME_RATE;
/** @brief Video format parameter kind (1=NTSC-M, 2=NTSC-J, 3=PAL-BDGHI, etc.). */
extern const char* const B_VIDEO_FORMAT;

// junction controls
/** @brief Physical audio/video input junction kind. */
extern const char* const B_WEB_PHYSICAL_INPUT;
/** @brief Physical audio/video output junction kind. */
extern const char* const B_WEB_PHYSICAL_OUTPUT;
/** @brief Analog-to-digital converter junction kind. */
extern const char* const B_WEB_ADC_CONVERTER;
/** @brief Digital-to-analog converter junction kind. */
extern const char* const B_WEB_DAC_CONVERTER;
/** @brief Logical input junction kind. */
extern const char* const B_WEB_LOGICAL_INPUT;
/** @brief Logical output junction kind. */
extern const char* const B_WEB_LOGICAL_OUTPUT;
/** @brief Logical bus junction kind. */
extern const char* const B_WEB_LOGICAL_BUS;
/** @brief Buffer input junction kind. */
extern const char* const B_WEB_BUFFER_INPUT;
/** @brief Buffer output junction kind. */
extern const char* const B_WEB_BUFFER_OUTPUT;

// transport control
/** @brief Simple transport parameter kind (0=rewind, 1=stop, 2=play, 3=pause, 4=fast-forward). */
extern const char* const B_SIMPLE_TRANSPORT;

class BContinuousParameter;
class BDiscreteParameter;
class BList;
class BNullParameter;
class BParameter;
class BParameterGroup;
class BTextParameter;

/** @brief Flags influencing how a parameter is displayed by a BMediaTheme. */
enum media_parameter_flags {
	B_HIDDEN_PARAMETER		= 1,  /**< Do not show this parameter in control panels. */
	B_ADVANCED_PARAMETER	= 2   /**< Show only in advanced/expert views. */
};

/** @brief Describes the complete set of controllable parameters exported by a media node.
 *
 *  BParameterWeb is a flattenable tree of BParameterGroup objects that organize
 *  BParameter instances.  Obtain it from BControllable::Web() or
 *  BMediaRoster::GetParameterWebFor().
 */
class BParameterWeb : public BFlattenable {
public:
	/** @brief Constructs an empty parameter web. */
								BParameterWeb();

	/** @brief Destroys the parameter web and all owned groups and parameters. */
								~BParameterWeb();

	/** @brief Returns the node this web belongs to.
	 *  @return The media_node of the owning BControllable.
	 */
			media_node			Node();

	/** @brief Creates a new top-level parameter group.
	 *  @param name Human-readable group name.
	 *  @return Pointer to the new BParameterGroup.
	 */
			BParameterGroup*	MakeGroup(const char* name);

	/** @brief Returns the number of top-level groups in this web.
	 *  @return Group count.
	 */
			int32				CountGroups();

	/** @brief Returns the top-level group at the given index.
	 *  @param index Zero-based group index.
	 *  @return Pointer to the BParameterGroup.
	 */
			BParameterGroup*	GroupAt(int32 index);

	/** @brief Returns the total number of BParameter objects across all groups.
	 *  @return Parameter count.
	 */
			int32				CountParameters();

	/** @brief Returns the BParameter at the given flat index.
	 *  @param index Zero-based parameter index.
	 *  @return Pointer to the BParameter.
	 */
			BParameter*			ParameterAt(int32 index);

	// BFlattenable implementation
	/** @brief Returns false (variable-size object). */
	virtual	bool				IsFixedSize() const;
	/** @brief Returns the type code used when flattening. */
	virtual type_code			TypeCode() const;
	/** @brief Returns the number of bytes needed to flatten this web. */
	virtual	ssize_t				FlattenedSize() const;
	/** @brief Serializes the web into a flat byte buffer.
	 *  @param buffer Destination buffer.
	 *  @param size Capacity in bytes.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			Flatten(void* buffer, ssize_t size) const;
	/** @brief Returns true if the given type code is acceptable for Unflatten().
	 *  @param code The type code to test.
	 */
	virtual	bool				AllowsTypeCode(type_code code) const;
	/** @brief Restores the web from a flat byte buffer.
	 *  @param code Type code of the flattened data.
	 *  @param buffer Source buffer.
	 *  @param size Length in bytes.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			Unflatten(type_code code, const void* buffer,
									ssize_t size);

private:
	friend class BParameterGroup;
	friend class BControllable;

								BParameterWeb(const BParameterWeb& other);
			BParameterWeb&		operator=(const BParameterWeb& other);

	// reserved
	virtual	status_t			_Reserved_ControlWeb_0(void*);
	virtual	status_t			_Reserved_ControlWeb_1(void*);
	virtual	status_t			_Reserved_ControlWeb_2(void*);
	virtual	status_t			_Reserved_ControlWeb_3(void*);
	virtual	status_t			_Reserved_ControlWeb_4(void*);
	virtual	status_t			_Reserved_ControlWeb_5(void*);
	virtual	status_t			_Reserved_ControlWeb_6(void*);
	virtual	status_t			_Reserved_ControlWeb_7(void*);

			void				AddRefFix(void* oldItem, void* newItem);

private:
			BList*				fGroups;
			media_node			fNode;
			uint32				_reserved[8];
			BList*				fOldRefs;
			BList*				fNewRefs;
};


/** @brief A named collection of BParameter objects within a BParameterWeb.
 *
 *  BParameterGroup organizes related parameters and sub-groups into a logical
 *  hierarchy displayed as sections or tabs in a control panel.
 */
class BParameterGroup : public BFlattenable {
private:
	/** @brief Constructs a group; use BParameterWeb::MakeGroup() instead.
	 *  @param web The owning web.
	 *  @param name Human-readable group name.
	 */
								BParameterGroup(BParameterWeb* web,
									const char* name);
	virtual						~BParameterGroup();

public:
	/** @brief Returns the owning BParameterWeb.
	 *  @return Pointer to the BParameterWeb.
	 */
			BParameterWeb*		Web() const;

	/** @brief Returns the human-readable name of this group.
	 *  @return The group name string.
	 */
			const char*			Name() const;

	/** @brief Sets display flags for this group.
	 *  @param flags Combination of media_parameter_flags values.
	 */
			void				SetFlags(uint32 flags);

	/** @brief Returns the display flags for this group.
	 *  @return Flags bitmask.
	 */
			uint32				Flags() const;

	/** @brief Creates a null (label-only) parameter in this group.
	 *  @param id Unique parameter ID.
	 *  @param type The media type this parameter applies to.
	 *  @param name Human-readable name.
	 *  @param kind One of the B_* parameter kind strings.
	 *  @return Pointer to the new BNullParameter.
	 */
			BNullParameter*		MakeNullParameter(int32 id, media_type type,
									const char* name, const char* kind);

	/** @brief Creates a continuous (slider/knob) parameter in this group.
	 *  @param id Unique parameter ID.
	 *  @param type The media type.
	 *  @param name Human-readable name.
	 *  @param kind One of the B_* parameter kind strings.
	 *  @param unit Unit label string (e.g. "dB").
	 *  @param min Minimum value.
	 *  @param max Maximum value.
	 *  @param step Step size.
	 *  @return Pointer to the new BContinuousParameter.
	 */
			BContinuousParameter* MakeContinuousParameter(int32 id,
									media_type type, const char* name,
									const char* kind, const char* unit,
									float min, float max, float step);

	/** @brief Creates a discrete (selector/checkbox) parameter in this group.
	 *  @param id Unique parameter ID.
	 *  @param type The media type.
	 *  @param name Human-readable name.
	 *  @param kind One of the B_* parameter kind strings.
	 *  @return Pointer to the new BDiscreteParameter.
	 */
			BDiscreteParameter*	MakeDiscreteParameter(int32 id, media_type type,
									const char* name, const char* kind);

	/** @brief Creates a text-entry parameter in this group.
	 *  @param id Unique parameter ID.
	 *  @param type The media type.
	 *  @param name Human-readable name.
	 *  @param kind One of the B_* parameter kind strings.
	 *  @param maxBytes Maximum string length in bytes.
	 *  @return Pointer to the new BTextParameter.
	 */
			BTextParameter*		MakeTextParameter(int32 id, media_type type,
									const char* name, const char* kind,
									size_t maxBytes);

	/** @brief Creates a child group within this group.
	 *  @param name Human-readable name for the sub-group.
	 *  @return Pointer to the new BParameterGroup.
	 */
			BParameterGroup*	MakeGroup(const char* name);

	/** @brief Returns the number of parameters directly in this group.
	 *  @return Parameter count.
	 */
			int32				CountParameters();

	/** @brief Returns the parameter at the given index.
	 *  @param index Zero-based parameter index.
	 *  @return Pointer to the BParameter.
	 */
			BParameter*			ParameterAt(int32 index);

	/** @brief Returns the number of child groups.
	 *  @return Child group count.
	 */
			int32				CountGroups();

	/** @brief Returns the child group at the given index.
	 *  @param index Zero-based child group index.
	 *  @return Pointer to the child BParameterGroup.
	 */
			BParameterGroup*	GroupAt(int32 index);

	// BFlattenable implementation
	/** @brief Returns false (variable-size object). */
	virtual	bool				IsFixedSize() const;
	/** @brief Returns the type code used when flattening. */
	virtual type_code			TypeCode() const;
	/** @brief Returns the number of bytes needed to flatten this group. */
	virtual	ssize_t				FlattenedSize() const;
	/** @brief Serializes this group into a flat byte buffer. */
	virtual	status_t			Flatten(void* buffer, ssize_t size) const;
	/** @brief Returns true if the given type code is acceptable. */
	virtual	bool				AllowsTypeCode(type_code code) const;
	/** @brief Restores this group from a flat byte buffer. */
	virtual	status_t			Unflatten(type_code code, const void* buffer,
									ssize_t size);

private:
	friend class BParameterWeb;

								BParameterGroup();
								BParameterGroup(const BParameterGroup& other);
			BParameterGroup&	operator=(const BParameterGroup& other);

			BParameter*			MakeControl(int32 type);

	// reserved
	virtual	status_t			_Reserved_ControlGroup_0(void*);
	virtual	status_t			_Reserved_ControlGroup_1(void*);
	virtual	status_t			_Reserved_ControlGroup_2(void*);
	virtual	status_t			_Reserved_ControlGroup_3(void*);
	virtual	status_t			_Reserved_ControlGroup_4(void*);
	virtual	status_t			_Reserved_ControlGroup_5(void*);
	virtual	status_t			_Reserved_ControlGroup_6(void*);
	virtual	status_t			_Reserved_ControlGroup_7(void*);

private:
			BParameterWeb*		fWeb;
			BList*				fControls;
			BList*				fGroups;
			char*				fName;
			uint32				fFlags;
			uint32				_reserved[7];
};


/** @brief Abstract base class representing one controllable parameter in a BParameterWeb.
 *
 *  BParameter provides a type-safe value interface used by control panels and
 *  the BControllable message protocol.  Concrete subclasses are
 *  BContinuousParameter, BDiscreteParameter, BTextParameter, and BNullParameter.
 */
class BParameter : public BFlattenable {
public:
	/** @brief Identifies the concrete subclass of a BParameter. */
	enum media_parameter_type {
		B_NULL_PARAMETER,
		B_DISCRETE_PARAMETER,
		B_CONTINUOUS_PARAMETER,
		B_TEXT_PARAMETER
	};

	/** @brief Returns the concrete parameter type.
	 *  @return One of the media_parameter_type values.
	 */
			media_parameter_type Type() const;

	/** @brief Returns the owning BParameterWeb.
	 *  @return Pointer to the BParameterWeb.
	 */
			BParameterWeb*		Web() const;

	/** @brief Returns the BParameterGroup this parameter belongs to.
	 *  @return Pointer to the BParameterGroup.
	 */
			BParameterGroup*	Group() const;

	/** @brief Returns the human-readable name.
	 *  @return The name string.
	 */
			const char*			Name() const;

	/** @brief Returns the parameter kind string (e.g. B_GAIN).
	 *  @return The kind string.
	 */
			const char*			Kind() const;

	/** @brief Returns the unit label string.
	 *  @return The unit string.
	 */
			const char*			Unit() const;

	/** @brief Returns the unique parameter ID.
	 *  @return The parameter ID.
	 */
			int32				ID() const;

	/** @brief Sets display flags for this parameter.
	 *  @param flags Combination of media_parameter_flags values.
	 */
			void				SetFlags(uint32 flags);

	/** @brief Returns the display flags for this parameter.
	 *  @return Flags bitmask.
	 */
			uint32				Flags() const;

	/** @brief Returns the type code of the value stored by this parameter. */
	virtual	type_code			ValueType() = 0;

	/** @brief Reads the current value of this parameter from the owning node.
	 *  @param buffer Buffer that receives the current value.
	 *  @param _size In: buffer capacity; Out: bytes written.
	 *  @param _when On return, the time of the last value change.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetValue(void* buffer, size_t* _size,
									bigtime_t* _when);

	/** @brief Sends a new value to the owning node.
	 *  @param buffer Pointer to the new value data.
	 *  @param size Size of the value in bytes.
	 *  @param when Performance time at which the change should take effect.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetValue(const void* buffer, size_t size,
									bigtime_t when);

	/** @brief Returns the number of value channels for this parameter.
	 *  @return Channel count.
	 */
			int32				CountChannels();

	/** @brief Sets the number of value channels for this parameter.
	 *  @param count The new channel count.
	 */
			void				SetChannelCount(int32 count);

	/** @brief Returns the media type associated with this parameter.
	 *  @return The media_type value.
	 */
			media_type			MediaType();

	/** @brief Sets the media type associated with this parameter.
	 *  @param type The new media_type.
	 */
			void				SetMediaType(media_type type);

	/** @brief Returns the number of upstream input connections to this parameter.
	 *  @return Input count.
	 */
			int32				CountInputs();

	/** @brief Returns the input parameter at the given index.
	 *  @param index Zero-based index.
	 *  @return Pointer to the BParameter.
	 */
			BParameter*			InputAt(int32 index);

	/** @brief Adds an upstream input parameter connection.
	 *  @param input The upstream BParameter.
	 */
			void				AddInput(BParameter* input);

	/** @brief Returns the number of downstream output connections from this parameter.
	 *  @return Output count.
	 */
			int32				CountOutputs();

	/** @brief Returns the output parameter at the given index.
	 *  @param index Zero-based index.
	 *  @return Pointer to the BParameter.
	 */
			BParameter*			OutputAt(int32 index);

	/** @brief Adds a downstream output parameter connection.
	 *  @param output The downstream BParameter.
	 */
			void				AddOutput(BParameter* output);

	// BFlattenable implementation
	/** @brief Returns false (variable-size object). */
	virtual	bool				IsFixedSize() const;
	/** @brief Returns the type code used when flattening. */
	virtual type_code			TypeCode() const;
	/** @brief Returns the number of bytes needed to flatten this parameter. */
	virtual	ssize_t				FlattenedSize() const;
	/** @brief Serializes this parameter into a flat byte buffer. */
	virtual	status_t			Flatten(void* buffer, ssize_t size) const;
	/** @brief Returns true if the given type code is acceptable. */
	virtual	bool				AllowsTypeCode(type_code code) const;
	/** @brief Restores this parameter from a flat byte buffer. */
	virtual	status_t			Unflatten(type_code code, const void* buffer,
									ssize_t size);

private:
	friend class BNullParameter;
	friend class BContinuousParameter;
	friend class BDiscreteParameter;
	friend class BTextParameter;
	friend class BParameterGroup;
	friend class BParameterWeb;

								BParameter(int32 id, media_type mediaType,
									media_parameter_type type,
									BParameterWeb* web, const char* name,
									const char* kind, const char* unit);
								~BParameter();

	// reserved
	virtual	status_t			_Reserved_Control_0(void*);
	virtual	status_t			_Reserved_Control_1(void*);
	virtual	status_t			_Reserved_Control_2(void*);
	virtual	status_t			_Reserved_Control_3(void*);
	virtual	status_t			_Reserved_Control_4(void*);
	virtual	status_t			_Reserved_Control_5(void*);
	virtual	status_t			_Reserved_Control_6(void*);
	virtual	status_t			_Reserved_Control_7(void*);

			bool				SwapOnUnflatten() { return fSwapDetected; }
	virtual	void				FixRefs(BList& old, BList& updated);

private:
			int32				fID;
			media_parameter_type fType;
			BParameterWeb*		fWeb;
			BParameterGroup*	fGroup;
			char*				fName;
			char*				fKind;
			char*				fUnit;
			BList*				fInputs;
			BList*				fOutputs;
			bool				fSwapDetected;
			media_type			fMediaType;
			int32				fChannels;
			uint32				fFlags;

			uint32				_reserved[7];
};


/** @brief A BParameter with a continuous floating-point value range.
 *
 *  BContinuousParameter is used for sliders, knobs, and similar controls.
 *  The response curve can be linear, polynomial, exponential, or logarithmic.
 */
class BContinuousParameter : public BParameter {
public:
	/** @brief Response curve types for BContinuousParameter. */
	enum response {
		B_UNKNOWN = 0,
		B_LINEAR,
		B_POLYNOMIAL,
		B_EXPONENTIAL,
		B_LOGARITHMIC
	};

	/** @brief Returns B_FLOAT_TYPE, the value type for continuous parameters. */
	virtual	type_code			ValueType();

	/** @brief Returns the minimum allowed value.
	 *  @return Minimum value.
	 */
			float				MinValue();

	/** @brief Returns the maximum allowed value.
	 *  @return Maximum value.
	 */
			float				MaxValue();

	/** @brief Returns the step size between discrete value positions.
	 *  @return Step size.
	 */
			float				ValueStep();

	/** @brief Sets the response curve for this parameter.
	 *  @param response One of the response enum values.
	 *  @param factor Curve factor.
	 *  @param offset Curve offset.
	 */
			void				SetResponse(int response, float factor,
									float offset);

	/** @brief Retrieves the response curve settings.
	 *  @param _response On return, the response type.
	 *  @param factor On return, the curve factor.
	 *  @param offset On return, the curve offset.
	 */
			void				GetResponse(int* _response, float* factor,
									float* offset);

	/** @brief Returns the number of bytes needed to flatten this parameter. */
	virtual	ssize_t				FlattenedSize() const;
	/** @brief Serializes this parameter into a flat byte buffer. */
	virtual	status_t			Flatten(void* buffer, ssize_t size) const;
	/** @brief Restores this parameter from a flat byte buffer. */
	virtual	status_t			Unflatten(type_code code, const void* buffer,
									ssize_t size);

private:
	friend class BParameterGroup;

								BContinuousParameter(int32 id,
									media_type mediaType,
									BParameterWeb* web, const char* name,
									const char* kind, const char* unit,
									float min, float max, float step);
								~BContinuousParameter();

	// reserved
	virtual	status_t			_Reserved_ContinuousParameter_0(void*);
	virtual	status_t			_Reserved_ContinuousParameter_1(void*);
	virtual	status_t			_Reserved_ContinuousParameter_2(void*);
	virtual	status_t			_Reserved_ContinuousParameter_3(void*);
	virtual	status_t			_Reserved_ContinuousParameter_4(void*);
	virtual	status_t			_Reserved_ContinuousParameter_5(void*);
	virtual	status_t			_Reserved_ContinuousParameter_6(void*);
	virtual	status_t			_Reserved_ContinuousParameter_7(void*);

private:
			float				fMinimum;
			float				fMaximum;
			float				fStepping;
			response			fResponse;
			float				fFactor;
			float				fOffset;

			uint32				_reserved[8];
};


/** @brief A BParameter with a discrete integer value and labeled items.
 *
 *  BDiscreteParameter is used for selectors, checkboxes, and similar controls.
 *  Each possible value has an associated name string.
 */
class BDiscreteParameter : public BParameter {
public:
	/** @brief Returns B_INT32_TYPE, the value type for discrete parameters. */
	virtual	type_code			ValueType();

	/** @brief Returns the number of selectable items.
	 *  @return Item count.
	 */
			int32				CountItems();

	/** @brief Returns the name of the item at the given index.
	 *  @param index Zero-based item index.
	 *  @return The item name string.
	 */
			const char*			ItemNameAt(int32 index);

	/** @brief Returns the integer value of the item at the given index.
	 *  @param index Zero-based item index.
	 *  @return The item's integer value.
	 */
			int32				ItemValueAt(int32 index);

	/** @brief Adds a new named item to this parameter.
	 *  @param value The integer value for this item.
	 *  @param name Human-readable item name.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			AddItem(int32 value, const char* name);

	/** @brief Populates items from the connected input parameters.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			MakeItemsFromInputs();

	/** @brief Populates items from the connected output parameters.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			MakeItemsFromOutputs();

	/** @brief Removes all items from this parameter. */
			void				MakeEmpty();

	/** @brief Returns the number of bytes needed to flatten this parameter. */
	virtual	ssize_t				FlattenedSize() const;
	/** @brief Serializes this parameter into a flat byte buffer. */
	virtual	status_t			Flatten(void* buffer, ssize_t size) const;
	/** @brief Restores this parameter from a flat byte buffer. */
	virtual	status_t			Unflatten(type_code code, const void* buffer,
									ssize_t size);

private:
	friend class BParameterGroup;

								BDiscreteParameter(int32 id,
									media_type mediaType,
									BParameterWeb* web, const char* name,
									const char* kind);
								~BDiscreteParameter();

	// reserved
	virtual	status_t			_Reserved_DiscreteParameter_0(void*);
	virtual	status_t			_Reserved_DiscreteParameter_1(void*);
	virtual	status_t			_Reserved_DiscreteParameter_2(void*);
	virtual	status_t			_Reserved_DiscreteParameter_3(void*);
	virtual	status_t			_Reserved_DiscreteParameter_4(void*);
	virtual	status_t			_Reserved_DiscreteParameter_5(void*);
	virtual	status_t			_Reserved_DiscreteParameter_6(void*);
	virtual	status_t			_Reserved_DiscreteParameter_7(void*);

private:
			BList*				fSelections;
			BList*				fValues;

			uint32				_reserved[8];
};


/** @brief A BParameter whose value is a short string.
 *
 *  BTextParameter is used for editable text fields in control panels.
 *  The maximum string length is set at construction time.
 */
class BTextParameter : public BParameter {
public:
	/** @brief Returns B_STRING_TYPE, the value type for text parameters. */
	virtual	type_code			ValueType();

	/** @brief Returns the maximum number of bytes the string value may occupy.
	 *  @return Maximum byte count.
	 */
			size_t				MaxBytes() const;

	/** @brief Returns the number of bytes needed to flatten this parameter. */
	virtual	ssize_t				FlattenedSize() const;
	/** @brief Serializes this parameter into a flat byte buffer. */
	virtual	status_t			Flatten(void* buffer, ssize_t size) const;
	/** @brief Restores this parameter from a flat byte buffer. */
	virtual	status_t			Unflatten(type_code code, const void* buffer,
									ssize_t size);

private:
	friend class BParameterGroup;

								BTextParameter(int32 id,
									media_type mediaType,
									BParameterWeb* web, const char* name,
									const char* kind, size_t maxBytes);
								~BTextParameter();

	// reserved
	virtual	status_t			_Reserved_TextParameter_0(void*);
	virtual	status_t			_Reserved_TextParameter_1(void*);
	virtual	status_t			_Reserved_TextParameter_2(void*);
	virtual	status_t			_Reserved_TextParameter_3(void*);
	virtual	status_t			_Reserved_TextParameter_4(void*);
	virtual	status_t			_Reserved_TextParameter_5(void*);
	virtual	status_t			_Reserved_TextParameter_6(void*);
	virtual	status_t			_Reserved_TextParameter_7(void*);

private:
			uint32				fMaxBytes;

			uint32				_reserved[8];
};


/** @brief A BParameter with no value; used purely as a label or junction connector.
 *
 *  BNullParameter acts as a named anchor point in the parameter web graph,
 *  representing junctions between parameter connections (e.g. B_WEB_BUFFER_INPUT).
 */
class BNullParameter : public BParameter {
public:
	/** @brief Returns B_RAW_TYPE (no meaningful value). */
	virtual	type_code			ValueType();

	/** @brief Returns the number of bytes needed to flatten this parameter. */
	virtual	ssize_t				FlattenedSize() const;
	/** @brief Serializes this parameter into a flat byte buffer. */
	virtual	status_t			Flatten(void* buffer, ssize_t size) const;
	/** @brief Restores this parameter from a flat byte buffer. */
	virtual	status_t			Unflatten(type_code code, const void* buffer,
									ssize_t size);

private:
	friend class BParameterGroup;

								BNullParameter(int32 id,
									media_type mediaType,
									BParameterWeb* web, const char* name,
									const char* kind);
								~BNullParameter();

	// reserved
	virtual	status_t			_Reserved_NullParameter_0(void*);
	virtual	status_t			_Reserved_NullParameter_1(void*);
	virtual	status_t			_Reserved_NullParameter_2(void*);
	virtual	status_t			_Reserved_NullParameter_3(void*);
	virtual	status_t			_Reserved_NullParameter_4(void*);
	virtual	status_t			_Reserved_NullParameter_5(void*);
	virtual	status_t			_Reserved_NullParameter_6(void*);
	virtual	status_t			_Reserved_NullParameter_7(void*);

private:
			uint32				_reserved[8];
};


#endif	// _CONTROL_WEB_H
