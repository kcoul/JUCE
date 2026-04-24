#ifndef JUCE_QNX_COMPAT_PTHREAD_H
#define JUCE_QNX_COMPAT_PTHREAD_H

#ifndef __EXT_QNX
#define __EXT_QNX 1
#endif

#ifndef __EXT_UNIX_MISC
#define __EXT_UNIX_MISC 1
#endif

#ifndef __EXT_POSIX1_200112
#define __EXT_POSIX1_200112 1
#endif

#include_next <pthread.h>

#ifndef PTHREAD_MUTEX_RECURSIVE
#define PTHREAD_MUTEX_RECURSIVE 2
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(pthread_mutexattr_settype)
int pthread_mutexattr_settype(pthread_mutexattr_t* __attr, int __type);
#endif

#ifdef __cplusplus
}
#endif

#endif
