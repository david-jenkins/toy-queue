
#include "mutex.h"

#ifndef POSIXMUTEX
// optimised mutexes and conditions
int mutex_init_v2(mutex_t *m, const pthread_mutexattr_t *a)
{
	m->m.u = 0;
	m->id = rand();
	/* Default to process private */
	m->flags = FUTEX_PRIVATE_FLAG;
	if (a)
	{
		/* Check for a shared mutex */
		int shared = 0;
		pthread_mutexattr_getpshared(a, &shared);
		if (shared) m->flags = 0;
	}
	return 0;
}

int mutex_destroy_v2(mutex_t *m)
{
	/* Do nothing */
	(void) m;
	return 0;
}

// int mutex_lock_v2(mutex_t *m)
// {
// 	int i;

// 	/* Try to grab lock */
// 	for (i = 0; i < 100; i++)
// 	{
// 		if (!xchg_8(&m->m.b.locked, 1)) return 0;

// 		cpu_relax();
// 	}

// 	/* Have to sleep */
// 	while (xchg_32(&m->m.u, 257) & 1)
// 	{
// 		sys_futex(&m->m, FUTEX_WAIT | m->flags, 257, NULL, NULL, 0);
// 	}

// 	return 0;
// }

// int mutex_timedlock_v2(mutex_t *m, const struct timespec *to)
// {
// 	int i;

// 	/* Try to grab lock */
// 	for (i = 0; i < 100; i++)
// 	{
// 		if (!xchg_8(&m->m.b.locked, 1)) return 0;

// 		cpu_relax();
// 	}
// 	int retval = 0;
// 	/* Have to sleep */
// 	while ((xchg_32(&m->m.u, 257) & 1) && retval==0)
// 	{
// 		retval = sys_futex(&m->m, FUTEX_WAIT | m->flags, 257, to, NULL, 0);
// 	}
// 	if (retval == -1 && errno == ETIMEDOUT)
// 		retval = ETIMEDOUT;

// 	return retval;
// }

// int mutex_unlock_v2(mutex_t *m)
// {
// 	int i;

// 	/* Locked and not contended */
// 	if ((m->m.u == 1) && (cmpxchg(&m->m.u, 1, 0) == 1)) return 0;

// 	/* Unlock */
// 	m->m.b.locked = 0;

// 	barrier();

// 	/* Spin and hope someone takes the lock */
// 	for (i = 0; i < 200; i++)
// 	{
// 		if (m->m.b.locked) return 0;

// 		cpu_relax();
// 	}

// 	/* We need to wake someone up */
// 	m->m.b.contended = 0;

// 	sys_futex(&m->m, FUTEX_WAKE | m->flags, 1, NULL, NULL, 0);

// 	return 0;
// }

// int mutex_trylock_v2(mutex_t *m)
// {
// 	/* Try to take the lock, if is currently unlocked */
// 	unsigned c = xchg_8(&m->m.b.locked, 1);
// 	if (!c) return 0;
// 	return EBUSY;
// }

int cond_init_v2(cv_t *c, pthread_condattr_t *a)
{
	c->bcast = 0;
	c->mid = -1;

	/* Sequence variable doesn't actually matter, but keep valgrind happy */
	c->seq = 0;

	/* Default to process private */
	c->flags = FUTEX_PRIVATE_FLAG;
	if (a)
	{
		/* Check for a shared cond */
		int shared = 0;
		pthread_condattr_getpshared(a, &shared);
		if (shared) c->flags = 0;
	}
	return 0;
}

int cond_destroy_v2(cv_t *c)
{
	/* No need to do anything */
	(void) c;
	return 0;
}

// int cond_timedwait_v2(cv_t *c, mutex_t *m, const struct timespec *to)
// {
// 	int seq = c->seq;
// 	int retval = 0;
// 	if (c->mid != m->id)
// 	{
// 		if (c->flags!=m->flags) return EINVAL;
// 		if (c->mid!=-1) return EINVAL;
// 		/* Atomically set mutex inside cv */
// 		cmpxchg(&c->mid, -1, m->id);
// 		if (c->mid != m->id) return EINVAL;
// 	}

// 	mutex_unlock(m);

// 	retval = sys_futex(&c->seq, FUTEX_WAIT | c->flags, seq, to, NULL, 0);
// 	// if (retval == -1 && errno == ETIMEDOUT) {
// 	// 	retval = ETIMEDOUT;
// 	// 	printf("TIMEDOUT in cond_timedwait\n");
// 	// }

// 	while (xchg_32(&m->m.b.locked, 257) & 1)
// 	{
// 		retval = sys_futex(&m->m, FUTEX_WAIT | m->flags, 257, NULL, NULL, 0);
// 		// if (retval==-1) {
// 		// 	perror("ERROR in cond_timedwait1: ");
// 		// }
// 	}

// 	if (retval==0 && xchg_32(&c->bcast,0) == 1) {
// 		int seq = c->seq;
// 		retval = sys_futex(&c->seq, FUTEX_CMP_REQUEUE | c->flags, 0, (void *) INT_MAX, &m->m, seq);
// 		// if (retval==-1) {
// 		// 	perror("ERROR in cond_timedwait2: ");
// 		// }
// 		c->mid = -1;
// 	}
	
// 	if (retval == -1 && errno == ETIMEDOUT)
// 		retval = ETIMEDOUT;

// 	return retval;
// }

// original mutexes and conditions
int mutex_init_v1(mutex_t *m, const pthread_mutexattr_t *a)
{
	m->m.u = 0;
	m->id = rand();
	/* Default to process private */
	m->flags = FUTEX_PRIVATE_FLAG;
	if (a)
	{
		/* Check for a shared mutex */
		int shared = 0;
		pthread_mutexattr_getpshared(a, &shared);
		if (shared) m->flags = 0;
	}
	return 0;
}

int mutex_destroy_v1(mutex_t *m)
{
	/* Do nothing */
	(void) m;
	return 0;
}



int cond_init_v1(cv_t *c, pthread_condattr_t *a)
{
	c->bcast = 0;
	c->mid = -1;

	/* Sequence variable doesn't actually matter, but keep valgrind happy */
	c->seq = 0;

	/* Default to process private */
	c->flags = FUTEX_PRIVATE_FLAG;
	if (a)
	{
		/* Check for a shared cond */
		int shared = 0;
		pthread_condattr_getpshared(a, &shared);
		if (shared) c->flags = 0;
	}
	return 0;
}

int cond_destroy_v1(cv_t *c)
{
	/* No need to do anything */
	(void) c;
	return 0;
}

#endif

#ifdef POSIXCONDWAIT

int darc_condwait_init_tovalue(darc_condwait_t *s, unsigned value)
{
	pthread_mutex_init(&s->m,NULL);
	pthread_cond_init(&s->c,NULL);
	s->value = value;
	return 0;
}

int darc_condwait_init(darc_condwait_t *s)
{
	return darc_condwait_init_tovalue(s, 0);
}

int darc_condwait_init_shared_tovalue(darc_condwait_t *s, unsigned value)
{
	pthread_mutexattr_t mutexattr;
	pthread_condattr_t condattr;

	pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_setpshared(&mutexattr,PTHREAD_PROCESS_SHARED);

    pthread_condattr_init(&condattr);
    pthread_condattr_setpshared(&condattr,PTHREAD_PROCESS_SHARED);

	pthread_mutex_init(&s->m,&mutexattr);
	pthread_cond_init(&s->c,&condattr);

	pthread_mutexattr_destroy(&mutexattr);
	pthread_condattr_destroy(&condattr);

	s->value = value;

	return 0;
}

int darc_condwait_init_shared(darc_condwait_t *s)
{
	return darc_condwait_init_shared_tovalue(s, 0);
}

int darc_condwait_destroy(darc_condwait_t *s)
{
	pthread_mutex_destroy(&s->m);
	pthread_cond_destroy(&s->c);
	return 0;
}

#elif defined(MUTEXCONDWAIT)

int darc_condwait_init_tovalue(darc_condwait_t *s, unsigned value)
{
	mutex_init(&s->m,NULL);
	cond_init(&s->c,NULL);
	s->value = value;
	return 0;
}

int darc_condwait_init(darc_condwait_t *s)
{
	return darc_condwait_init_tovalue(s, 0);
}

int darc_condwait_init_shared_tovalue(darc_condwait_t *s, unsigned value)
{
	pthread_mutexattr_t mutexattr;
	pthread_condattr_t condattr;

	pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_setpshared(&mutexattr,PTHREAD_PROCESS_SHARED);

    pthread_condattr_init(&condattr);
    pthread_condattr_setpshared(&condattr,PTHREAD_PROCESS_SHARED);

	mutex_init(&s->m,&mutexattr);
	cond_init(&s->c,&condattr);

	pthread_mutexattr_destroy(&mutexattr);
	pthread_condattr_destroy(&condattr);

	s->value = value;

	return 0;
}

int darc_condwait_init_shared(darc_condwait_t *s)
{
	return darc_condwait_init_shared_tovalue(s, 0);
}

int darc_condwait_destroy(darc_condwait_t *s)
{
	mutex_destroy(&s->m);
	cond_destroy(&s->c);
	return 0;
}


#else

int darc_condwait_init_tovalue(darc_condwait_t *s, unsigned value)
{
	s->value = value;
	s->flags = FUTEX_PRIVATE_FLAG;
	return 0;
}

int darc_condwait_init(darc_condwait_t *s)
{
	return darc_condwait_init_tovalue(s, 0);
}

int darc_condwait_init_shared_tovalue(darc_condwait_t *s, unsigned value)
{
	s->value = value;
	s->flags = 0;
	return 0;
}

int darc_condwait_init_shared(darc_condwait_t *s)
{
	return darc_condwait_init_shared_tovalue(s, 0);
}

int darc_condwait_destroy(darc_condwait_t *s)
{
	(void) s;
	return 0;
}


#endif

int pool_barrier_init(pool_barrier_t *b, pthread_barrierattr_t *a, unsigned count)
{
	b->seq = 0;
	b->count = 0;
	b->refcount = 1;
	/* Total of waiting threads */
	b->total = count - 1;

		/* Default to process private */
	b->flags = FUTEX_PRIVATE_FLAG;
	if (a)
	{
		/* Check for a shared barrier */
		int shared = 0;
		pthread_barrierattr_getpshared(a, &shared);
		if (shared) b->flags = 0;
	}
	return 0;
}

int pool_barrier_destroy(pool_barrier_t *b)
{
	/* Trigger futex wake */
	atomic_add(&b->refcount, -1);

	/* Wait until all references to the barrier are dropped */
	while (1)
	{
		unsigned ref = atomic_read(b->refcount);

		if (!ref) return 0;

		sys_futex(&b->refcount, FUTEX_WAIT | b->flags, ref, NULL, NULL, 0);
	}
	return 0;
}

int fast_barrier_init(fast_barrier_t *b, pthread_barrierattr_t *a, unsigned count)
{
	b->seq = 0;
	b->count = 0;
	b->refcount = 1;
	/* Total of waiting threads */
	b->total = count - 1;

	if (count < sysconf(_SC_NPROCESSORS_ONLN))
	{
		b->spins = 1000;
	}
	else
	{
		b->spins = 1;
	}

	/* Default to process private */
	b->flags = FUTEX_PRIVATE_FLAG;
	if (a)
	{
		/* Check for a shared barrier */
		int shared = 0;
		pthread_barrierattr_getpshared(a, &shared);
		if (shared) b->flags = 0;
	}
	return 0;
}

int fast_barrier_destroy(fast_barrier_t *b)
{
	/* Trigger futex wake */
	atomic_add(&b->refcount, -1);

	/* Wait until all references to the barrier are dropped */
	while (1)
	{
		unsigned ref = atomic_read(b->refcount);

		if (!ref) return 0;

		sys_futex(&b->refcount, FUTEX_WAIT | b->flags, ref, NULL, NULL, 0);
	}
	return 0;
}


int ticket_barrier_init(ticket_barrier_t *b, pthread_barrierattr_t *a, unsigned count)
{
	b->count_in = 0;
	b->count_out = 0;
	b->count_next = -1;
	b->total = count;

	/* Default to process private */
	b->flags = FUTEX_PRIVATE_FLAG;
	if (a)
	{
		/* Check for a shared barrier */
		int shared = 0;
		pthread_barrierattr_getpshared(a, &shared);
		if (shared) b->flags = 0;
	}
	return 0;
}

int ticket_barrier_destroy(ticket_barrier_t *b)
{
	/* Alter the refcount so we trigger futex wake */
	atomic_add(&b->count_in, -1);

	/* However, threads can be leaving... so wait for them */
	while (1)
	{
		unsigned count_out = atomic_read(b->count_out);

		/* Make sure everything is synchronized */
		barrier();

		if (count_out == atomic_read(b->count_in) + 1) return 0;

		sys_futex(&b->count_out, FUTEX_WAIT | b->flags, count_out, NULL, NULL, 0);
	}
	return 0;
}



int spincond_init(spincond_t *c)
{
	c->sig = 0;
	c->bcast = 0;
	return 0;
}

int spincond_destroy(spincond_t *c)
{
	(void) c;
	return 0;
}