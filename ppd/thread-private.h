/*
 * Private threading definitions for libppd.
 *
 * Copyright 2009-2017 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more information.
 */

#ifndef _PPD_THREAD_PRIVATE_H_
#  define _PPD_THREAD_PRIVATE_H_

/*
 * Include necessary headers...
 */

#  include "config.h"


/*
 * C++ magic...
 */

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


#  ifdef HAVE_PTHREAD_H			/* POSIX threading */
#    include <pthread.h>
typedef pthread_mutex_t _ppd_mutex_t;
typedef pthread_key_t	_ppd_threadkey_t;
#    define _PPD_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#    define _PPD_THREADKEY_INITIALIZER 0
#    define _ppdThreadGetData(k) pthread_getspecific(k)
#    define _ppdThreadSetData(k,p) pthread_setspecific(k,p)

#  elif defined(_WIN32)			/* Windows threading */
#    include <winsock2.h>
#    include <windows.h>
typedef struct _ppd_mutex_s
{
  int			m_init;		/* Flag for on-demand initialization */
  CRITICAL_SECTION	m_criticalSection;
					/* Win32 Critical Section */
} _ppd_mutex_t;
typedef DWORD	_ppd_threadkey_t;
#    define _PPD_MUTEX_INITIALIZER { 0, 0 }
#    define _PPD_THREADKEY_INITIALIZER 0
#    define _ppdThreadGetData(k) TlsGetValue(k)
#    define _ppdThreadSetData(k,p) TlsSetValue(k,p)

#  else					/* No threading */
typedef char	_ppd_mutex_t;
typedef void	*_ppd_threadkey_t;
#    define _PPD_MUTEX_INITIALIZER 0
#    define _PPD_THREADKEY_INITIALIZER (void *)0
#    define _ppdThreadGetData(k) k
#    define _ppdThreadSetData(k,p) k=p
#  endif /* HAVE_PTHREAD_H */


/*
 * Functions...
 */

extern void	_ppdMutexInit(_ppd_mutex_t *mutex);
extern void	_ppdMutexLock(_ppd_mutex_t *mutex);
extern void	_ppdMutexUnlock(_ppd_mutex_t *mutex);

#  ifdef __cplusplus
}
#  endif /* __cplusplus */
#endif /* !_PPD_THREAD_PRIVATE_H_ */
