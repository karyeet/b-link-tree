#include "memory.h"
#include "lock.h"
#include "node.h"
#include <assert.h>
#include <string.h>


static lock_t local_readlock = LOCK_INIT;


Node mem_read(bptr_t address, Node const *memory) {
	assert(address < MEM_SIZE);
	return memory[address];
}

//! The HLS interface to HBM does not support atomic test-and-set operations.
//! Atomic test-and-set operations within the FPGA fabric are allowed.
//! Serializing test-and-set operations in memory with mutual exclusion ensures
//! that race conditions do not lead to unexpected concurrent modification.
//!
//! @todo Set up multiple locks for specific regions of memory, such as by
//! address ranges or hashes to allow higher write bandwidth.
Node mem_read_lock(bptr_t address, Node *memory) {
#ifdef OPTIMISTIC_LOCK
	return memory[address];
#else
	Node tmp;

	assert(address < MEM_SIZE);
	// Read the given address from main memory until its lock is released
	// Then grab the lock
	do {
		lock_p(&local_readlock);
		tmp = memory[address];
		if (test_and_set(&tmp.lock) == 0) {
			break;
		} else {
			lock_v(&local_readlock);
		}
	} while(true);
	// Write back the locked value to main memory
	memory[address] = tmp;
	// Release the local lock for future writers
	lock_v(&local_readlock);
	return tmp;
#endif
}

Node mem_read_trylock(bptr_t address, Node *memory, bool *success) {
#ifdef OPTIMISTIC_LOCK
	return memory[address];
#else
	Node tmp;

	assert(address < MEM_SIZE);
	assert(success != NULL);
	// Read the given address from main memory until its lock is released
	// Then grab the lock
	lock_p(&local_readlock);
	tmp = memory[address];
	*success = (test_and_set(&tmp.lock) == 0);
	if (*success) {
		// Write back the locked value to main memory
		memory[address] = tmp;
	}
	// Release the local lock for future writers
	lock_v(&local_readlock);
	return tmp;
#endif
}

#ifdef OPTIMISTIC_LOCK
bool mem_write_unlock(AddrNode *node, Node *memory) {
	assert(node->addr < MEM_SIZE);
	Node tmp = memory[node->addr];
	if (tmp.lock != node->node.lock) return false;
	node->node.lock++;
	memory[node->addr] = node->node;
	return true;
}
#else
void mem_write_unlock(AddrNode *node, Node *memory) {
	assert(node->addr < MEM_SIZE);
	lock_v(&node->node.lock);
	memory[node->addr] = node->node;
}
#endif

void mem_unlock(bptr_t address, Node *memory) {
#ifndef OPTIMISTIC_LOCK
	assert(address < MEM_SIZE);
	// Cast byte pointer to lock_t pointer to ensure write is of correct size
	*((lock_t*) (
		// Byte pointer
		&(
			(uint8_t*) memory
		)[
			// Address of the lock field of the node who starts at address
			(address+1)*sizeof(Node)-sizeof(lock_t)
		]
	)) = LOCK_INIT;
#endif
}

void mem_reset_all(Node *memory) {
	memset(memory, INVALID, MEM_SIZE*sizeof(Node));
	for (bptr_t i = 0; i < MEM_SIZE; i++) {
		memory[i].lock = LOCK_INIT;
	}
}
