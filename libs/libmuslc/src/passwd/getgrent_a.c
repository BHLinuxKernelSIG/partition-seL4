/* @LICENSE(MUSLC_MIT) */

#include "pwf.h"
#include <pthread.h>

struct group *__getgrent_a(FILE *f, struct group *gr, char **line, size_t *size, char ***mem, size_t *nmem)
{
	ssize_t l;
	char *s, *mems;
	size_t i;
	int cs;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);
	for (;;) {
		if ((l=getline(line, size, f)) < 0) {
			free(*line);
			*line = 0;
			gr = 0;
			goto end;
		}
		line[0][l-1] = 0;

		s = line[0];
		gr->gr_name = s++;
		if (!(s = strchr(s, ':'))) continue;

		*s++ = 0; gr->gr_passwd = s;
		if (!(s = strchr(s, ':'))) continue;

		*s++ = 0; gr->gr_gid = atoi(s);
		if (!(s = strchr(s, ':'))) continue;

		*s++ = 0; mems = s;
		break;
	}

	for (*nmem=!!*s; *s; s++)
		if (*s==',') ++*nmem;
	free(*mem);
	*mem = calloc(sizeof(char *), *nmem+1);
	if (!*mem) {
		free(*line);
		*line = 0;
		return 0;
	}
	if (*mems) {
		mem[0][0] = mems;
		for (s=mems, i=0; *s; s++)
			if (*s==',') *s++ = 0, mem[0][++i] = s;
		mem[0][++i] = 0;
	} else {
		mem[0][0] = 0;
	}
	gr->gr_mem = *mem;
end:
	pthread_setcancelstate(cs, 0);
	return gr;
}
