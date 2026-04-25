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
 *   Copyright 2018, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file TypeHandlerRoster.cpp
 * @brief Implementation of TypeHandlerRoster, the singleton registry of TypeHandler instances.
 *
 * The roster keeps the list of generic and specialised handlers (the
 * BasicTypeHandler templates plus CString/BMessage/BList) and selects the
 * best-supporting handler for a given DWARF Type. CreateValueNode() is the
 * public entry point used by the variables view to obtain the correct
 * ValueNode subclass for a child.
 *
 * @see TypeHandler, ValueNode
 */


#include "TypeHandlerRoster.h"

#include <new>

#include <AutoDeleter.h>
#include <AutoLocker.h>

#include "AddressValueNode.h"
#include "ArrayValueNode.h"
#include "CompoundValueNode.h"
#include "BListTypeHandler.h"
#include "BMessageTypeHandler.h"
#include "CStringTypeHandler.h"
#include "EnumerationValueNode.h"
#include "PointerToMemberValueNode.h"
#include "PrimitiveValueNode.h"
#include "Type.h"
#include "TypeHandler.h"


/**
 * @brief Comparator for sorting handlers by descending support score.
 *
 * Used by FindTypeHandlers() so that the UI presents the handler with the
 * highest SupportsType() score for @a state first.
 *
 * @param a      First handler.
 * @param b      Second handler.
 * @param state  Cast to a Type pointer; the type to score against.
 * @return Positive when @a a is a worse match than @a b, negative otherwise.
 */
static int CompareTypeHandlers(const TypeHandler* a, const TypeHandler* b,
	void* state)
{
	Type* type = (Type*)state;
	return a->SupportsType(type) > b->SupportsType(type) ? 1 : -1;
}


// #pragma mark - BasicTypeHandler


namespace {


/**
 * @brief Generic TypeHandler that pairs a DWARF Type subclass with a ValueNode subclass.
 *
 * Used to register the catch-all handlers (Address, Array, Compound,
 * Enumeration, PointerToMember, Primitive). It returns a 0.5 support score so
 * a more specific specialised handler can outscore it.
 */
template<typename TypeClass, typename NodeClass>
class BasicTypeHandler : public TypeHandler {
public:
	/**
	 * @brief Returns the user-visible handler name.
	 *
	 * @return The literal "Raw".
	 */
	virtual const char* Name() const
	{
		return "Raw";
	}

	/**
	 * @brief Reports support score for @a type.
	 *
	 * @param type  Type to test.
	 * @return 0.5 if @a type is a TypeClass, 0 otherwise.
	 */
	virtual float SupportsType(Type* type) const
	{
		return dynamic_cast<TypeClass*>(type) != NULL ? 0.5f : 0;
	}

	/**
	 * @brief Allocates the matching NodeClass for @a nodeChild and @a type.
	 *
	 * @param nodeChild  Child the node will render for.
	 * @param type       Resolved DWARF type.
	 * @param _node      Set to the freshly allocated node on success.
	 * @retval B_OK         On success.
	 * @retval B_BAD_VALUE  When @a type is not a TypeClass.
	 * @retval B_NO_MEMORY  On allocation failure.
	 */
	virtual status_t CreateValueNode(ValueNodeChild* nodeChild,
		Type* type, ValueNode*& _node)
	{
		TypeClass* supportedType = dynamic_cast<TypeClass*>(type);
		if (supportedType == NULL)
			return B_BAD_VALUE;

		ValueNode* node = new(std::nothrow) NodeClass(nodeChild, supportedType);
		if (node == NULL)
			return B_NO_MEMORY;

		_node = node;
		return B_OK;
	}
};


}	// unnamed namespace


// #pragma mark - TypeHandlerRoster


/** @brief Process-wide singleton instance, set by CreateDefault(). */
/*static*/ TypeHandlerRoster* TypeHandlerRoster::sDefaultInstance = NULL;


/**
 * @brief Constructs an empty roster with its own lock.
 */
TypeHandlerRoster::TypeHandlerRoster()
	:
	fLock("type handler roster")
{
}


/**
 * @brief Releases references on every registered handler.
 */
TypeHandlerRoster::~TypeHandlerRoster()
{
	for (int32 i = 0; TypeHandler* handler = fTypeHandlers.ItemAt(i); i++)
		handler->ReleaseReference();
}


/**
 * @brief Returns the process-wide singleton instance.
 *
 * @return The default roster, or NULL if CreateDefault() has not run.
 */
/*static*/ TypeHandlerRoster*
TypeHandlerRoster::Default()
{
	return sDefaultInstance;
}


/**
 * @brief Initialises and registers the default handler set into the singleton.
 *
 * Idempotent -- a second call is a no-op once the singleton is set.
 *
 * @retval B_OK         On success or when already initialised.
 * @retval B_NO_MEMORY  On allocation failure.
 */
/*static*/ status_t
TypeHandlerRoster::CreateDefault()
{
	if (sDefaultInstance != NULL)
		return B_OK;

	TypeHandlerRoster* roster = new(std::nothrow) TypeHandlerRoster;
	if (roster == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<TypeHandlerRoster> rosterDeleter(roster);

	status_t error = roster->Init();
	if (error != B_OK)
		return error;

	error = roster->RegisterDefaultHandlers();
	if (error != B_OK)
		return error;

	sDefaultInstance = rosterDeleter.Detach();
	return B_OK;
}


/**
 * @brief Tears down the singleton, releasing all registered handlers.
 */
/*static*/ void
TypeHandlerRoster::DeleteDefault()
{
	TypeHandlerRoster* roster = sDefaultInstance;
	sDefaultInstance = NULL;
	delete roster;
}


/**
 * @brief Verifies that the embedded lock initialised correctly.
 *
 * @return Status of fLock.InitCheck().
 */
status_t
TypeHandlerRoster::Init()
{
	return fLock.InitCheck();
}


/**
 * @brief Registers the canonical generic and specialised handlers.
 *
 * Generic handlers cover Address, Array, Compound, Enumeration,
 * PointerToMember, and Primitive types. Specialised handlers cover CString,
 * BMessage, and BList rendering.
 *
 * @retval B_OK         All handlers were registered.
 * @retval B_NO_MEMORY  When any allocation or insertion failed.
 */
status_t
TypeHandlerRoster::RegisterDefaultHandlers()
{
	TypeHandler* handler;
	BReference<TypeHandler> handlerReference;

	#undef REGISTER_BASIC_HANDLER
	#define REGISTER_BASIC_HANDLER(name)						\
		handler = new(std::nothrow)								\
			BasicTypeHandler<name##Type, name##ValueNode>();	\
		handlerReference.SetTo(handler, true);					\
		if (handler == NULL || !RegisterHandler(handler))		\
			return B_NO_MEMORY;

	REGISTER_BASIC_HANDLER(Address);
	REGISTER_BASIC_HANDLER(Array);
	REGISTER_BASIC_HANDLER(Compound);
	REGISTER_BASIC_HANDLER(Enumeration);
	REGISTER_BASIC_HANDLER(PointerToMember);
	REGISTER_BASIC_HANDLER(Primitive);

	#undef REGISTER_SPECIALIZED_HANDLER
	#define REGISTER_SPECIALIZED_HANDLER(name)					\
		handler = new(std::nothrow)								\
			name##TypeHandler();								\
		handlerReference.SetTo(handler, true);					\
		if (handler == NULL || !RegisterHandler(handler))		\
			return B_NO_MEMORY;

	REGISTER_SPECIALIZED_HANDLER(CString);
	REGISTER_SPECIALIZED_HANDLER(BMessage);
	REGISTER_SPECIALIZED_HANDLER(BList);

	return B_OK;
}


/**
 * @brief Counts how many registered handlers claim positive support for @a type.
 *
 * @param type  Type to score.
 * @return Number of handlers with SupportsType() > 0.
 */
int32
TypeHandlerRoster::CountTypeHandlers(Type* type)
{
	AutoLocker<BLocker>  locker(fLock);

	int32 count = 0;
	for (int32 i = 0; TypeHandler* handler = fTypeHandlers.ItemAt(i); i++) {
		if (handler->SupportsType(type) > 0)
			++count;
	}

	return count;
}


/**
 * @brief Returns the registered handler with the highest support score for @a type.
 *
 * @param nodeChild  Child the handler will eventually render for; reserved for
 *                   future per-child filtering.
 * @param type       Type to score.
 * @param _handler   Set to a referenced handler on success.
 * @retval B_OK              A best handler was found.
 * @retval B_ENTRY_NOT_FOUND No handler returned a positive score.
 */
status_t
TypeHandlerRoster::FindBestTypeHandler(ValueNodeChild* nodeChild, Type* type,
	TypeHandler*& _handler)
{
	// find the best-supporting handler
	AutoLocker<BLocker> locker(fLock);

	TypeHandler* bestHandler = NULL;
	float bestSupport = 0;

	for (int32 i = 0; TypeHandler* handler = fTypeHandlers.ItemAt(i); i++) {
		float support = handler->SupportsType(type);
		if (support > 0 && support > bestSupport) {
			bestHandler = handler;
			bestSupport = support;
		}
	}

	if (bestHandler == NULL)
		return B_ENTRY_NOT_FOUND;

	bestHandler->AcquireReference();
	_handler = bestHandler;
	return B_OK;
}


/**
 * @brief Builds a sorted list of every handler that supports @a type.
 *
 * The returned list is sorted by descending support score. Each entry has its
 * reference acquired before being added, so the caller owns one reference per
 * handler in the list.
 *
 * @param nodeChild  Child the handlers will render for; reserved for future
 *                   per-child filtering.
 * @param type       Type to score.
 * @param _handlers  Set to the freshly allocated list on success.
 * @retval B_OK              At least one handler matched.
 * @retval B_ENTRY_NOT_FOUND No handler returned a positive score.
 * @retval B_NO_MEMORY       On allocation failure.
 * @note Caller deletes the returned list and releases each handler reference.
 */
status_t
TypeHandlerRoster::FindTypeHandlers(ValueNodeChild* nodeChild, Type* type,
	TypeHandlerList*& _handlers)
{
	// find the best-supporting handler
	AutoLocker<BLocker> locker(fLock);

	TypeHandlerList* handlers = new(std::nothrow) TypeHandlerList(10);
	ObjectDeleter<TypeHandlerList> listDeleter(handlers);
	if (handlers == NULL)
		return B_NO_MEMORY;

	for (int32 i = 0; TypeHandler* handler = fTypeHandlers.ItemAt(i); i++) {
		if (handler->SupportsType(type) > 0) {
			if (!handlers->AddItem(handler))
				return B_NO_MEMORY;
		}
	}

	if (handlers->CountItems() == 0)
		return B_ENTRY_NOT_FOUND;

	for (int32 i = 0; TypeHandler* handler = handlers->ItemAt(i); i++)
		handler->AcquireReference();

	handlers->SortItems(CompareTypeHandlers, type);

	_handlers = handlers;
	listDeleter.Detach();

	return B_OK;
}


/**
 * @brief Allocates the appropriate ValueNode for @a nodeChild and @a type.
 *
 * If @a handler is NULL the roster picks the best match itself, walking up
 * through typedef/modifier chains via Type::ResolveRawType() until a handler
 * is found or the chain is exhausted.
 *
 * @param nodeChild  Child the node will render for.
 * @param type       Resolved DWARF type.
 * @param handler    Caller-chosen handler, or NULL to auto-select.
 * @param _node      Set to the freshly allocated node on success.
 * @retval B_OK           On success.
 * @retval B_UNSUPPORTED  When no handler matches even after type resolution.
 * @return Other handler-specific errors propagated from CreateValueNode().
 */
status_t
TypeHandlerRoster::CreateValueNode(ValueNodeChild* nodeChild, Type* type,
	TypeHandler* handler, ValueNode*& _node)
{
	BReference<TypeHandler> handlerReference;

	// if the caller doesn't supply us with a handler to use, try to find
	// the best match.
	if (handler == NULL) {
		// find the best-supporting handler
		while (true) {
			status_t error = FindBestTypeHandler(nodeChild, type, handler);
			if (error == B_OK) {
				handlerReference.SetTo(handler, true);
				break;
			}

			// not found yet -- try to strip a modifier/typedef from the type
			Type* nextType = type->ResolveRawType(true);
			if (nextType == NULL || nextType == type)
				return B_UNSUPPORTED;

			type = nextType;
		}
	}

	return handler->CreateValueNode(nodeChild, type, _node);
}


/**
 * @brief Adds @a handler to the registry, taking a reference.
 *
 * @param handler  Handler to register.
 * @return true on success, false on allocation failure.
 */
bool
TypeHandlerRoster::RegisterHandler(TypeHandler* handler)
{
	if (!fTypeHandlers.AddItem(handler))
		return false;

	handler->AcquireReference();
	return true;
}


/**
 * @brief Removes @a handler from the registry, releasing the reference taken on registration.
 *
 * @param handler  Handler to deregister.
 */
void
TypeHandlerRoster::UnregisterHandler(TypeHandler* handler)
{
	if (fTypeHandlers.RemoveItem(handler))
		handler->ReleaseReference();
}
