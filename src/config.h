
/* Define to the name of the distribution. */
#define PACKAGE "sane-backends"

/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif

/* Define to the version of the distribution. */
#define VERSION "1.0.24"

#define SANE_NAME_SCAN_TL_X		"tl-x"
#define SANE_NAME_SCAN_TL_Y		"tl-y"
#define SANE_NAME_SCAN_BR_X		"br-x"
#define SANE_NAME_SCAN_BR_Y		"br-y"
#define SANE_NAME_SCAN_RESOLUTION	"resolution"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define NELEMS(a)	((int)(sizeof (a) / sizeof (a[0])))

