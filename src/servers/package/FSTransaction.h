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
 *
 * Copyright 2013, Ingo Weinhold, ingo_weinhold@gmx.de.
  * Distributed under the terms of the MIT License.
 */

/** @file FSTransaction.h
 *  @brief Tracks filesystem operations for atomic rollback of package changes */

#ifndef FS_TRANSACTION_H
#define FS_TRANSACTION_H


#include <string>
#include <vector>

#include "FSUtils.h"


/** @brief Records filesystem create, remove, and move operations so they can be atomically rolled back */
class FSTransaction {
public:
			typedef FSUtils::Entry Entry;

			class Operation;
			class CreateOperation;
			class RemoveOperation;
			class MoveOperation;

public:
	/** @brief Construct an empty transaction */
								FSTransaction();
	/** @brief Destructor */
								~FSTransaction();

	/** @brief Undo all recorded operations in reverse order */
			void				RollBack();

	/** @brief Record that an entry was created and return its operation index */
			int32				CreateEntry(const Entry& entry,
									int32 modifiedOperation = -1);
	/** @brief Record that an entry was removed (with backup) and return its operation index */
			int32				RemoveEntry(const Entry& entry,
									const Entry& backupEntry,
									int32 modifiedOperation = -1);
	/** @brief Record that an entry was moved and return its operation index */
			int32				MoveEntry(const Entry& fromEntry,
									const Entry& toEntry,
									int32 modifiedOperation = -1);

	/** @brief Unregister the operation at the given index so it will not be rolled back */
			void				RemoveOperationAt(int32 index);

private:
			struct OperationInfo;
			typedef std::vector<OperationInfo> OperationList;

private:
	static	std::string			_GetPath(const Entry& entry);

private:
			OperationList		fOperations; /**< Ordered list of recorded operations */
};


/** @brief RAII guard for a single filesystem operation within a transaction */
class FSTransaction::Operation {
public:
	Operation(FSTransaction* transaction, int32 operation)
		:
		fTransaction(transaction),
		fOperation(operation),
		fIsFinished(false)
	{
	}

	~Operation()
	{
		if (fTransaction != NULL && fOperation >= 0 && !fIsFinished)
			fTransaction->RemoveOperationAt(fOperation);
	}

	/*!	Arms the operation rollback, i.e. rolling back the transaction will
		revert this operation.
	*/
	void Finished()
	{
		fIsFinished = true;
	}

	/*!	Unregisters the operation rollback, i.e. rolling back the transaction
		will not revert this operation.
	*/
	void Unregister()
	{
		if (fTransaction != NULL && fOperation >= 0) {
			fTransaction->RemoveOperationAt(fOperation);
			fIsFinished = false;
			fTransaction = NULL;
			fOperation = -1;
		}
	}

private:
	FSTransaction*	fTransaction;
	int32			fOperation;
	bool			fIsFinished;
};


/** @brief Convenience wrapper that records a create operation on construction */
class FSTransaction::CreateOperation : public FSTransaction::Operation {
public:
	CreateOperation(FSTransaction* transaction, const Entry& entry,
		int32 modifiedOperation = -1)
		:
		Operation(transaction,
			transaction->CreateEntry(entry, modifiedOperation))
	{
	}
};


/** @brief Convenience wrapper that records a remove operation on construction */
class FSTransaction::RemoveOperation : public FSTransaction::Operation {
public:
	RemoveOperation(FSTransaction* transaction, const Entry& entry,
		const Entry& backupEntry, int32 modifiedOperation = -1)
		:
		Operation(transaction,
			transaction->RemoveEntry(entry, backupEntry, modifiedOperation))
	{
	}
};


/** @brief Convenience wrapper that records a move operation on construction */
class FSTransaction::MoveOperation : public FSTransaction::Operation {
public:
	MoveOperation(FSTransaction* transaction, const Entry& fromEntry,
		const Entry& toEntry, int32 modifiedOperation = -1)
		:
		Operation(transaction,
			transaction->MoveEntry(fromEntry, toEntry, modifiedOperation))
	{
	}
};


#endif	// FS_TRANSACTION_H
