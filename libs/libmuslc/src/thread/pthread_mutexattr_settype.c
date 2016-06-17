/* @LICENSE(MUSLC_MIT) */

#include "pthread_impl.h"

int pthread_mutexattr_settype(pthread_mutexattr_t *a, int type)
{
	if ((unsigned)type > 2) return EINVAL;
	*a = (*a & ~3) | type;
	return 0;
}
