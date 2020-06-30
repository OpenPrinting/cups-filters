/*
 * Threading primitives for libppd.
 *
 * Copyright © 2009-2018 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "thread-private.h"


#if defined(HAVE_PTHREAD_H)
/*
 * '_ppdMutexInit()' - Initialize a mutex.
 */

void
_ppdMutexInit(_ppd_mutex_t *mutex)	/* I - Mutex */
{
  pthread_mutex_init(mutex, NULL);
}


/*
 * '_ppdMutexLock()' - Lock a mutex.
 */

void
_ppdMutexLock(_ppd_mutex_t *mutex)	/* I - Mutex */
{
  pthread_mutex_lock(mutex);
}


/*
 * '_ppdMutexUnlock()' - Unlock a mutex.
 */

void
_ppdMutexUnlock(_ppd_mutex_t *mutex)	/* I - Mutex */
{
  pthread_mutex_unlock(mutex);
}


#elif defined(_WIN32)
#  include <process.h>

static _ppd_mutex_t    ppd_global_mutex = _CUPS_MUTEX_INITIALIZER;
                                        /* Global critical section */


/*
 * '_ppdMutexInit()' - Initialize a mutex.
 */

void
_ppdMutexInit(_ppd_mutex_t *mutex)	/* I - Mutex */
{
  InitializeCriticalSection(&mutex->m_criticalSection);
  mutex->m_init = 1;
}


/*
 * '_ppdMutexLock()' - Lock a mutex.
 */

void
_ppdMutexLock(_ppd_mutex_t *mutex)	/* I - Mutex */
{
  if (!mutex->m_init)
  {
    EnterCriticalSection(&ppd_global_mutex.m_criticalSection);

    if (!mutex->m_init)
    {
      InitializeCriticalSection(&mutex->m_criticalSection);
      mutex->m_init = 1;
    }

    LeaveCriticalSection(&ppd_global_mutex.m_criticalSection);
  }

  EnterCriticalSection(&mutex->m_criticalSection);
}


/*
 * '_ppdMutexUnlock()' - Unlock a mutex.
 */

void
_ppdMutexUnlock(_ppd_mutex_t *mutex)	/* I - Mutex */
{
  LeaveCriticalSection(&mutex->m_criticalSection);
}


#else /* No threading */


/*
 * '_ppdMutexInit()' - Initialize a mutex.
 */

void
_ppdMutexInit(_ppd_mutex_t *mutex)	/* I - Mutex */
{
  (void)mutex;
}


/*
 * '_ppdMutexLock()' - Lock a mutex.
 */

void
_ppdMutexLock(_ppd_mutex_t *mutex)	/* I - Mutex */
{
  (void)mutex;
}


/*
 * '_ppdMutexUnlock()' - Unlock a mutex.
 */

void
_ppdMutexUnlock(_ppd_mutex_t *mutex)	/* I - Mutex */
{
  (void)mutex;
}


#endif /* HAVE_PTHREAD_H */
