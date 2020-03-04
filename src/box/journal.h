#ifndef TARANTOOL_JOURNAL_H_INCLUDED
#define TARANTOOL_JOURNAL_H_INCLUDED
/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <stdint.h>
#include <stdbool.h>
#include "salad/stailq.h"
#include "fiber.h"
#include "txn.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct xrow_header;
struct journal_entry;

/** Journal entry finalization callback typedef. */
typedef void (*journal_entry_complete_cb)(struct journal_entry *entry, void *data);

/**
 * An entry for an abstract journal.
 * Simply put, a write ahead log request.
 *
 * In case of synchronous replication, this request will travel
 * first to a Raft leader before going to the local WAL.
 */
struct journal_entry {
	/** A helper to include requests into a FIFO queue. */
	struct stailq_entry fifo;
	/**
	 * On success, contains vclock signature of
	 * the committed transaction, on error is -1
	 */
	int64_t res;
	/**
	 * A journal entry finalization callback which is going to be called
	 * after the entry processing was finished in both cases: success
	 * or fail. Entry->res is set to a result value before the callback
	 * is fired.
	 */
	journal_entry_complete_cb on_complete_cb;
	/**
	 * A journal entry completion callback argument.
	 */
	void *on_complete_cb_data;
	/**
	 * Approximate size of this request when encoded.
	 */
	size_t approx_len;
	/**
	 * The number of rows in the request.
	 */
	int n_rows;
	/**
	 * The rows.
	 */
	struct xrow_header *rows[];
};

struct region;

/**
 * Create a new journal entry.
 *
 * @return NULL if out of memory, fiber diagnostics area is set
 */
struct journal_entry *
journal_entry_new(size_t n_rows, struct region *region);

/**
 * Finalize a single entry.
 */
static inline void
journal_entry_complete(struct journal_entry *entry)
{
	entry->on_complete_cb(entry, entry->on_complete_cb_data);
}

/**
 * An API for an abstract journal for all transactions of this
 * instance, as well as for multiple instances in case of
 * synchronous replication.
 */
struct journal {
	int (*write_async)(struct journal *journal,
			   struct journal_entry *entry,
			   journal_entry_complete_cb on_complete_cb,
			   void *on_complete_cb_data);
	int (*write)(struct journal *journal,
		     struct journal_entry *req);
	void (*destroy)(struct journal *journal);
};

/**
 * Depending on the step of recovery and instance configuration
 * points at a concrete implementation of the journal.
 */
extern struct journal *current_journal;

static inline int
journal_write(struct journal_entry *entry)
{
	assert(in_txn() != NULL);
	return current_journal->write(current_journal, entry);
}

static inline int
journal_write_async(struct journal_entry *entry,
		    journal_entry_complete_cb on_complete_cb,
		    void *on_complete_cb_data)
{
	assert(in_txn() != NULL);
	return current_journal->write_async(current_journal, entry,
					    on_complete_cb,
					    on_complete_cb_data);
}

/**
 * Change the current implementation of the journaling API.
 * Happens during life cycle of an instance:
 *
 * 1. When recovering a snapshot, the log sequence numbers
 *    don't matter and are not used, transactions
 *    can be recovered in any order. A stub API simply
 *    returns 0 for every write request.
 *
 * 2. When recovering from the local write ahead
 * log, the LSN of each entry is already known. In this case,
 * the journal API should simply return the existing
 * log sequence numbers of records and do nothing else.
 *
 * 2. After recovery, in wal_mode = NONE, the implementation
 * fakes a WAL by using a simple counter to provide
 * log sequence numbers.
 *
 * 3. If the write ahead log is on, the WAL thread
 * is issuing the log sequence numbers.
 */
static inline void
journal_set(struct journal *new_journal)
{
	if (current_journal && current_journal->destroy)
		current_journal->destroy(current_journal);
	current_journal = new_journal;
}

static inline void
journal_create(struct journal *journal,
	       int (*write_async)(struct journal *journal,
				  struct journal_entry *entry,
				  journal_entry_complete_cb on_complete_cb,
				  void *on_complete_cb_data),
	       int (*write)(struct journal *, struct journal_entry *),
	       void (*destroy)(struct journal *))
{
	journal->write_async	= write_async,
	journal->write		= write;
	journal->destroy	= destroy;
}

extern int
journal_no_write_async(struct journal *journal,
		       struct journal_entry *entry,
		       journal_entry_complete_cb on_complete_cb,
		       void *on_complete_cb_data);

static inline bool
journal_is_initialized(struct journal *journal)
{
	return journal->write != NULL &&
		journal->write_async != NULL;
}

#if defined(__cplusplus)
} /* extern "C" */

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_JOURNAL_H_INCLUDED */
