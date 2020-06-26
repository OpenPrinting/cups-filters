/*
 * Threading primitives for CUPS.
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
 * '_cupsMutexInit()' - Initialize a mutex.
 */

void
_cupsMutexInit(_cups_mutex_t *mutex)	/* I - Mutex */
{
  pthread_mutex_init(mutex, NULL);
}


/*
 * '_cupsMutexLock()' - Lock a mutex.
 */

void
_cupsMutexLock(_cups_mutex_t *mutex)	/* I - Mutex */
{
  pthread_mutex_lock(mutex);
}


/*
 * '_cupsMutexUnlock()' - Unlock a mutex.
 */

void
_cupsMutexUnlock(_cups_mutex_t *mutex)	/* I - Mutex */
{
  pthread_mutex_unlock(mutex);
}


#elif defined(_WIN32)
#  include <process.h>


/*
 * '_cupsMutexInit()' - Initialize a mutex.
 */

void
_cupsMutexInit(_cups_mutex_t *mutex)	/* I - Mutex */
{
  InitializeCriticalSection(&mutex->m_criticalSection);
  mutex->m_init = 1;
}


/*
 * '_cupsMutexLock()' - Lock a mutex.
 */

void
_cupsMutexLock(_cups_mutex_t *mutex)	/* I - Mutex */
{
  if (!mutex->m_init)
  {
    _cupsGlobalLock();

    if (!mutex->m_init)
    {
      InitializeCriticalSection(&mutex->m_criticalSection);
      mutex->m_init = 1;
    }

    _cupsGlobalUnlock();
  }

  EnterCriticalSection(&mutex->m_criticalSection);
}


/*
 * '_cupsMutexUnlock()' - Unlock a mutex.
 */

void
_cupsMutexUnlock(_cups_mutex_t *mutex)	/* I - Mutex */
{
  LeaveCriticalSection(&mutex->m_criticalSection);
}


#else /* No threading */


/*
 * '_cupsMutexInit()' - Initialize a mutex.
 */

void
_cupsMutexInit(_cups_mutex_t *mutex)	/* I - Mutex */
{
  (void)mutex;
}


/*
 * '_cupsMutexLock()' - Lock a mutex.
 */

void
_cupsMutexLock(_cups_mutex_t *mutex)	/* I - Mutex */
{
  (void)mutex;
}


/*
 * '_cupsMutexUnlock()' - Unlock a mutex.
 */

void
_cupsMutexUnlock(_cups_mutex_t *mutex)	/* I - Mutex */
{
  (void)mutex;
}


#endif /* HAVE_PTHREAD_H */
