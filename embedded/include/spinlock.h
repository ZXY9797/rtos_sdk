#ifndef MSDK_INCLUDE_SPINLOCK_H_
#define MSDK_INCLUDE_SPINLOCK_H_
#include <stdbool.h>
#include <toolchain.h>
#include <arch/cpu.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Spinlock APIs
 * @defgroup spinlock_apis Spinlock APIs
 * @ingroup kernel_apis
 * @{
 */

struct z_spinlock_key {
	int key;
};

/**
 * @brief Kernel Spin Lock
 *
 * This struct defines a spin lock record on which CPUs can wait with
 * k_spin_lock().  Any number of spinlocks may be defined in
 * application code.
 */
struct k_spinlock {
/**
 * @cond INTERNAL_HIDDEN
 */
#ifdef CONFIG_SMP
#ifdef CONFIG_TICKET_SPINLOCKS
	/*
	 * Ticket spinlocks are conceptually two atomic variables,
	 * one indicating the current FIFO head (spinlock owner),
	 * and the other indicating the current FIFO tail.
	 * Spinlock is acquired in the following manner:
	 * - current FIFO tail value is atomically incremented while it's
	 *   original value is saved as a "ticket"
	 * - we spin until the FIFO head becomes equal to the ticket value
	 *
	 * Spinlock is released by atomic increment of the FIFO head
	 */
	atomic_t owner;
	atomic_t tail;
#else
	atomic_t locked;
#endif /* CONFIG_TICKET_SPINLOCKS */
#endif /* CONFIG_SMP */

#ifdef CONFIG_SPIN_VALIDATE
	/* Stores the thread that holds the lock with the locking CPU
	 * ID in the bottom two bits.
	 */
	uintptr_t thread_cpu;
#ifdef CONFIG_SPIN_LOCK_TIME_LIMIT
	/* Stores the time (in cycles) when a lock was taken
	 */
	uint32_t lock_time;
#endif /* CONFIG_SPIN_LOCK_TIME_LIMIT */
#endif /* CONFIG_SPIN_VALIDATE */

#if defined(CONFIG_CPP) && !defined(CONFIG_SMP) && \
	!defined(CONFIG_SPIN_VALIDATE)
	/* If CONFIG_SMP and CONFIG_SPIN_VALIDATE are both not defined
	 * the k_spinlock struct will have no members. The result
	 * is that in C sizeof(k_spinlock) is 0 and in C++ it is 1.
	 *
	 * This size difference causes problems when the k_spinlock
	 * is embedded into another struct like k_msgq, because C and
	 * C++ will have different ideas on the offsets of the members
	 * that come after the k_spinlock member.
	 *
	 * To prevent this we add a 1 byte dummy member to k_spinlock
	 * when the user selects C++ support and k_spinlock would
	 * otherwise be empty.
	 */
	char dummy;
#endif
/**
 * INTERNAL_HIDDEN @endcond
 */
};

/**
 * @brief Spinlock key type
 *
 * This type defines a "key" value used by a spinlock implementation
 * to store the system interrupt state at the time of a call to
 * k_spin_lock().  It is expected to be passed to a matching
 * k_spin_unlock().
 *
 * This type is opaque and should not be inspected by application
 * code.
 */
typedef struct z_spinlock_key k_spinlock_key_t;

static ALWAYS_INLINE k_spinlock_key_t k_spin_lock(struct k_spinlock *l)
{
	ARG_UNUSED(l);
	k_spinlock_key_t k;

	/* Note that we need to use the underlying arch-specific lock
	 * implementation.  The "irq_lock()" API in SMP context is
	 * actually a wrapper for a global spinlock!
	 */
	k.key = arch_irq_lock();

	// z_spinlock_validate_pre(l);
#ifdef CONFIG_SMP
#ifdef CONFIG_TICKET_SPINLOCKS
	/*
	 * Enqueue ourselves to the end of a spinlock waiters queue
	 * receiving a ticket
	 */
	atomic_val_t ticket = atomic_inc(&l->tail);
	/* Spin until our ticket is served */
	while (atomic_get(&l->owner) != ticket) {
		arch_spin_relax();
	}
#else
	while (!atomic_cas(&l->locked, 0, 1)) {
		arch_spin_relax();
	}
#endif /* CONFIG_TICKET_SPINLOCKS */
#endif /* CONFIG_SMP */
	// z_spinlock_validate_post(l);

	return k;
}

/**
 * @brief Unlock a spin lock
 *
 * This releases a lock acquired by k_spin_lock().  After this
 * function is called, any CPU will be able to acquire the lock.  If
 * other CPUs are currently spinning inside k_spin_lock() waiting for
 * this lock, exactly one of them will return synchronously with the
 * lock held.
 *
 * Spin locks must be properly nested.  A call to k_spin_unlock() must
 * be made on the lock object most recently locked using
 * k_spin_lock(), using the key value that it returned.  Attempts to
 * unlock mis-nested locks, or to unlock locks that are not held, or
 * to passing a key parameter other than the one returned from
 * k_spin_lock(), are illegal.  When CONFIG_SPIN_VALIDATE is set, some
 * of these errors can be detected by the framework.
 *
 * @param l A pointer to the spinlock to release
 * @param key The value returned from k_spin_lock() when this lock was
 *        acquired
 */
static ALWAYS_INLINE void k_spin_unlock(struct k_spinlock *l,
					k_spinlock_key_t key)
{
	ARG_UNUSED(l);
#ifdef CONFIG_SPIN_VALIDATE
	__ASSERT(z_spin_unlock_valid(l), "Not my spinlock %p", l);

#if defined(CONFIG_SPIN_LOCK_TIME_LIMIT) && (CONFIG_SPIN_LOCK_TIME_LIMIT != 0)
	uint32_t delta = sys_clock_cycle_get_32() - l->lock_time;

	__ASSERT(delta < CONFIG_SPIN_LOCK_TIME_LIMIT,
		 "Spin lock %p held %u cycles, longer than limit of %u cycles",
		 l, delta, CONFIG_SPIN_LOCK_TIME_LIMIT);
#endif /* CONFIG_SPIN_LOCK_TIME_LIMIT */
#endif /* CONFIG_SPIN_VALIDATE */

#ifdef CONFIG_SMP
#ifdef CONFIG_TICKET_SPINLOCKS
	/* Give the spinlock to the next CPU in a FIFO */
	(void)atomic_inc(&l->owner);
#else
	/* Strictly we don't need atomic_clear() here (which is an
	 * exchange operation that returns the old value).  We are always
	 * setting a zero and (because we hold the lock) know the existing
	 * state won't change due to a race.  But some architectures need
	 * a memory barrier when used like this, and we don't have a
	 * Zephyr framework for that.
	 */
	(void)atomic_clear(&l->locked);
#endif /* CONFIG_TICKET_SPINLOCKS */
#endif /* CONFIG_SMP */
	arch_irq_unlock(key.key);
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SPINLOCK_H_ */
