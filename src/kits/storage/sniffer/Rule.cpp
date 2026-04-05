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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file Rule.cpp
 * @brief MIME sniffer rule implementation.
 *
 * Implements the Rule class, the top-level structure of a parsed MIME sniffer
 * rule. A Rule holds a priority value in [0.0, 1.0] and a conjunction list of
 * DisjList objects; all DisjLists in the conjunction must match the data stream
 * for the rule as a whole to match. Rules are populated by Parser::Parse().
 *
 * @see BPrivate::Storage::Sniffer::Parser
 * @see BPrivate::Storage::Sniffer::DisjList
 */

#include <sniffer/Err.h>
#include <sniffer/DisjList.h>
#include <sniffer/Rule.h>
#include <DataIO.h>
#include <stdio.h>

using namespace BPrivate::Storage::Sniffer;

/**
 * @brief Creates an uninitialized Rule object.
 *
 * To initialize the rule, pass a pointer to this object to Sniffer::parse().
 */
Rule::Rule()
	: fPriority(0.0)
	, fConjList(NULL)
{
}

/**
 * @brief Destroys the Rule and releases the conjunction list it owns.
 */
Rule::~Rule() {
	Unset();
}

/**
 * @brief Returns the initialization status of this Rule.
 *
 * @return B_OK if the conjunction list has been set, B_NO_INIT otherwise.
 */
status_t
Rule::InitCheck() const {
	return fConjList ? B_OK : B_NO_INIT;
}

/**
 * @brief Returns the priority of the rule.
 *
 * @return A value in the range [0.0, 1.0] representing the rule's match priority.
 */
double
Rule::Priority() const {
	return fPriority;
}

/**
 * @brief Sniffs the given data stream.
 *
 * Evaluates every DisjList in the conjunction list against the stream; all must
 * match for the rule to succeed.
 *
 * @param data The data stream to sniff.
 * @return true if every DisjList in the conjunction matches, false otherwise.
 */
bool
Rule::Sniff(BPositionIO *data) const {
	if (InitCheck() != B_OK)
		return false;
	else {
		bool result = true;
		std::vector<DisjList*>::const_iterator i;
		for (i = fConjList->begin(); i != fConjList->end(); i++) {
			if (*i)
				result &= (*i)->Sniff(data);
		}
		return result;
	}
}

/**
 * @brief Returns the number of bytes needed for this rule to perform a complete sniff.
 *
 * Returns the largest BytesNeeded() value among all DisjLists in the conjunction,
 * since all must be evaluated and the stream must be long enough for each.
 *
 * @return The maximum bytes needed across all DisjLists, or a negative error code.
 */
ssize_t
Rule::BytesNeeded() const
{
	ssize_t result = InitCheck();

	// Tally up the BytesNeeded() values for all the DisjLists and return the largest.
	if (result == B_OK) {
		result = 0; // Just to be safe...
		std::vector<DisjList*>::const_iterator i;
		for (i = fConjList->begin(); i != fConjList->end(); i++) {
			if (*i) {
				ssize_t bytes = (*i)->BytesNeeded();
				if (bytes >= 0) {
					if (bytes > result)
						result = bytes;
				} else {
					result = bytes;
					break;
				}
			}
		}
	}
	return result;
}

/**
 * @brief Resets the Rule to an uninitialized state, freeing the conjunction list.
 */
void
Rule::Unset() {
	if (fConjList){
		delete fConjList;
		fConjList = NULL;
	}
}

/**
 * @brief Initializes the rule with a priority and a conjunction list.
 *
 * Called by Parser::Parse() after successfully parsing a sniffer rule.
 * Throws a Sniffer::Err if @p priority is outside [0.0, 1.0].
 *
 * @param priority A value in [0.0, 1.0] representing how strongly the rule matches.
 * @param list     Heap-allocated vector of DisjList pointers; ownership is transferred.
 */
void
Rule::SetTo(double priority, std::vector<DisjList*>* list) {
	Unset();
	if (0.0 <= priority && priority <= 1.0)
		fPriority = priority;
	else
		throw new Err("Sniffer pattern error: invalid priority", -1);
	fConjList = list;
}
