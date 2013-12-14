/* scanimage -- command line scanning utility
   Uses the SANE library.
   Copyright (C) 1996, 1997, 1998 Andreas Beck and David Mosberger
   
   Copyright (C) 1999 - 2009 by the SANE Project -- See AUTHORS and ChangeLog
   for details.

   For questions and comments contact the sane-devel mailinglist (see
   http://www.sane-project.org/mailing-lists.html).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#ifdef _AIX
# include "../include/lalloca.h"                /* MUST come first for AIX! */
#endif

#include "../include/sane/config.h"
#include "../include/lalloca.h"

#include <assert.h>
#include "lgetopt.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "../include/_stdint.h"

#include "../include/sane/sane.h"
#include "../include/sane/sanei.h"
#include "../include/sane/saneopts.h"

#include "stiff.h"

#include "../include/md5.h"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#ifndef HAVE_ATEXIT
# define atexit(func)	on_exit(func, 0)	/* works for SunOS, at least */
#endif

typedef struct
{
  uint8_t *data;
  int width;    /*WARNING: this is in bytes, get pixel width from param*/
  int height;
  int x;
  int y;
}
Image;

#define OPTION_FORMAT   1001
#define OPTION_MD5	1002
#define OPTION_BATCH_COUNT	1003
#define OPTION_BATCH_START_AT	1004
#define OPTION_BATCH_DOUBLE	1005
#define OPTION_BATCH_INCREMENT	1006
#define OPTION_BATCH_PROMPT    1007

#define BATCH_COUNT_UNLIMITED -1

static struct option basic_options[] = {
  {"device-name", required_argument, NULL, 'd'},
  {"list-devices", no_argument, NULL, 'L'},
  {"formatted-device-list", required_argument, NULL, 'f'},
  {"help", no_argument, NULL, 'h'},
  {"verbose", no_argument, NULL, 'v'},
  {"progress", no_argument, NULL, 'p'},
  {"test", no_argument, NULL, 'T'},
  {"all-options", no_argument, NULL, 'A'},
  {"version", no_argument, NULL, 'V'},
  {"buffer-size", optional_argument, NULL, 'B'},
  {"batch", optional_argument, NULL, 'b'},
  {"batch-count", required_argument, NULL, OPTION_BATCH_COUNT},
  {"batch-start", required_argument, NULL, OPTION_BATCH_START_AT},
  {"batch-double", no_argument, NULL, OPTION_BATCH_DOUBLE},
  {"batch-increment", required_argument, NULL, OPTION_BATCH_INCREMENT},
  {"batch-prompt", no_argument, NULL, OPTION_BATCH_PROMPT},
  {"format", required_argument, NULL, OPTION_FORMAT},
  {"accept-md5-only", no_argument, NULL, OPTION_MD5},
  {"icc-profile", required_argument, NULL, 'i'},
  {"dont-scan", no_argument, NULL, 'n'},
  {0, 0, NULL, 0}
};

#define OUTPUT_PNM      0
#define OUTPUT_TIFF     1

#define BASE_OPTSTRING	"d:hi:Lf:B::nvVTAbp"
#define STRIP_HEIGHT	256	/* # lines we increment image height */

static struct option *all_options;
static int option_number_len;
static int *option_number;
static SANE_Handle device;
static int verbose;
static int progress = 0;
static int test;
static int all;
static int output_format = OUTPUT_PNM;
static int help;
static int dont_scan = 0;
static const char *prog_name;
static int resolution_optind = -1, resolution_value = 0;

/* window (area) related options */
static SANE_Option_Descriptor window_option[4]; /*updated descs for x,y,l,t*/
static int window[4]; /*index into backend options for x,y,l,t*/
static SANE_Word window_val[2]; /*the value for x,y options*/
static int window_val_user[2];	/* is x,y user-specified? */

static int accept_only_md5_auth = 0;
static const char *icc_profile = NULL;

static void fetch_options (SANE_Device * device);
static void scanimage_exit (void);

static SANE_Word tl_x = 0;
static SANE_Word tl_y = 0;
static SANE_Word br_x = 0;
static SANE_Word br_y = 0;
static SANE_Byte *buffer;
static size_t buffer_size;


static void
auth_callback (SANE_String_Const resource,
	       SANE_Char * username, SANE_Char * password)
{
  char tmp[3 + 128 + SANE_MAX_USERNAME_LEN + SANE_MAX_PASSWORD_LEN], *wipe;
  unsigned char md5digest[16];
  int md5mode = 0, len, query_user = 1;
  FILE *pass_file;
  struct stat stat_buf;

  *tmp = 0;

  if (getenv ("HOME") != NULL)
    {
      if (strlen (getenv ("HOME")) < 500)
	{
	  sprintf (tmp, "%s/.sane/pass", getenv ("HOME"));
	}
    }

  if ((strlen (tmp) > 0) && (stat (tmp, &stat_buf) == 0))
    {

      if ((stat_buf.st_mode & 63) != 0)
	{
	  fprintf (stderr, "%s has wrong permissions (use at least 0600)\n",
		   tmp);
	}
      else
	{

	  if ((pass_file = fopen (tmp, "r")) != NULL)
	    {

	      if (strstr (resource, "$MD5$") != NULL)
		len = (strstr (resource, "$MD5$") - resource);
	      else
		len = strlen (resource);

	      while (fgets (tmp, sizeof(tmp), pass_file))
		{

		  if ((strlen (tmp) > 0) && (tmp[strlen (tmp) - 1] == '\n'))
		    tmp[strlen (tmp) - 1] = 0;
		  if ((strlen (tmp) > 0) && (tmp[strlen (tmp) - 1] == '\r'))
		    tmp[strlen (tmp) - 1] = 0;

		  if (strchr (tmp, ':') != NULL)
		    {

		      if (strchr (strchr (tmp, ':') + 1, ':') != NULL)
			{

			  if ((strncmp
			       (strchr (strchr (tmp, ':') + 1, ':') + 1,
				resource, len) == 0)
			      &&
			      ((int) strlen
			       (strchr (strchr (tmp, ':') + 1, ':') + 1) ==
			       len))
			    {

			      if ((strchr (tmp, ':') - tmp) <
				  SANE_MAX_USERNAME_LEN)
				{

				  if ((strchr (strchr (tmp, ':') + 1, ':') -
				       (strchr (tmp, ':') + 1)) <
				      SANE_MAX_PASSWORD_LEN)
				    {

				      strncpy (username, tmp,
					       strchr (tmp, ':') - tmp);

				      username[strchr (tmp, ':') - tmp] = 0;

				      strncpy (password,
					       strchr (tmp, ':') + 1,
					       strchr (strchr (tmp, ':') + 1,
						       ':') -
					       (strchr (tmp, ':') + 1));
				      password[strchr
					       (strchr (tmp, ':') + 1,
						':') - (strchr (tmp,
								':') + 1)] =
					0;

				      query_user = 0;
				      break;
				    }
				}

			    }
			}
		    }
		}

	      fclose (pass_file);
	    }
	}
    }

  if (strstr (resource, "$MD5$") != NULL)
    {
      md5mode = 1;
      len = (strstr (resource, "$MD5$") - resource);
      if (query_user == 1)
	fprintf (stderr, "Authentification required for resource %*.*s. "
		 "Enter username: ", len, len, resource);
    }
  else
    {

      if (accept_only_md5_auth != 0)
	{
	  fprintf (stderr, "ERROR: backend requested plain-text password\n");
	  return;
	}
      else
	{
	  fprintf (stderr,
		   "WARNING: backend requested plain-text password\n");
	  query_user = 1;
	}

      if (query_user == 1)
	fprintf (stderr,
		 "Authentification required for resource %s. Enter username: ",
		 resource);
    }

  if (query_user == 1)
    fgets (username, SANE_MAX_USERNAME_LEN, stdin);

  if ((strlen (username)) && (username[strlen (username) - 1] == '\n'))
    username[strlen (username) - 1] = 0;

  if (query_user == 1)
    {
#ifdef HAVE_GETPASS
      strcpy (password, (wipe = getpass ("Enter password: ")));
      memset (wipe, 0, strlen (password));
#else
      printf("OS has no getpass().  User Queries will not work\n");
#endif
    }

  if (md5mode)
    {

      sprintf (tmp, "%.128s%.*s", (strstr (resource, "$MD5$")) + 5,
	       SANE_MAX_PASSWORD_LEN - 1, password);

      md5_buffer (tmp, strlen (tmp), md5digest);

      memset (password, 0, SANE_MAX_PASSWORD_LEN);

      sprintf (password, "$MD5$%02x%02x%02x%02x%02x%02x%02x%02x"
	       "%02x%02x%02x%02x%02x%02x%02x%02x",
	       md5digest[0], md5digest[1],
	       md5digest[2], md5digest[3],
	       md5digest[4], md5digest[5],
	       md5digest[6], md5digest[7],
	       md5digest[8], md5digest[9],
	       md5digest[10], md5digest[11],
	       md5digest[12], md5digest[13], md5digest[14], md5digest[15]);
    }
}

static RETSIGTYPE
sighandler (int signum)
{
  static SANE_Bool first_time = SANE_TRUE;

  if (device)
    {
      fprintf (stderr, "%s: received signal %d\n", prog_name, signum);
      if (first_time)
	{
	  first_time = SANE_FALSE;
	  fprintf (stderr, "%s: trying to stop scanner\n", prog_name);
	  sane_cancel (device);
	}
      else
	{
	  fprintf (stderr, "%s: aborting\n", prog_name);
	  _exit (0);
	}
    }
}

static void
print_unit (SANE_Unit unit)
{
  switch (unit)
    {
    case SANE_UNIT_NONE:
      break;
    case SANE_UNIT_PIXEL:
      fputs ("pel", stdout);
      break;
    case SANE_UNIT_BIT:
      fputs ("bit", stdout);
      break;
    case SANE_UNIT_MM:
      fputs ("mm", stdout);
      break;
    case SANE_UNIT_DPI:
      fputs ("dpi", stdout);
      break;
    case SANE_UNIT_PERCENT:
      fputc ('%', stdout);
      break;
    case SANE_UNIT_MICROSECOND:
      fputs ("us", stdout);
      break;
    }
}

static void
print_option (SANE_Device * device, int opt_num, const SANE_Option_Descriptor *opt)
{
  const char *str, *last_break, *start;
  SANE_Bool not_first = SANE_FALSE;
  int i, column;

  if (opt->type == SANE_TYPE_GROUP){
    printf ("  %s:\n", opt->title);
    return;
  }

  /* if both of these are set, option is invalid */
  if(opt->cap & SANE_CAP_SOFT_SELECT && opt->cap & SANE_CAP_HARD_SELECT){
    fprintf (stderr, "%s: invalid option caps, SS+HS\n", prog_name);
    return;
  }

  /* invalid to select but not detect */
  if(opt->cap & SANE_CAP_SOFT_SELECT && !(opt->cap & SANE_CAP_SOFT_DETECT)){
    fprintf (stderr, "%s: invalid option caps, SS!SD\n", prog_name);
    return;
  }
  /* standard allows this, though it makes little sense
  if(opt->cap & SANE_CAP_HARD_SELECT && !(opt->cap & SANE_CAP_SOFT_DETECT)){
    fprintf (stderr, "%s: invalid option caps, HS!SD\n", prog_name);
    return;
  }*/

  /* if one of these three is not set, option is useless, skip it */
  if(!(opt->cap & 
   (SANE_CAP_SOFT_SELECT | SANE_CAP_HARD_SELECT | SANE_CAP_SOFT_DETECT)
  )){
    return;
  }

  /* print the option */
  if ( !strcmp (opt->name, "x")
    || !strcmp (opt->name, "y")
    || !strcmp (opt->name, "t")
    || !strcmp (opt->name, "l"))
      printf ("    -%s", opt->name);
  else
    printf ("    --%s", opt->name);

  /* print the option choices */
  if (opt->type == SANE_TYPE_BOOL)
    {
      fputs ("[=(", stdout);
      if (opt->cap & SANE_CAP_AUTOMATIC)
	fputs ("auto|", stdout);
      fputs ("yes|no)]", stdout);
    }
  else if (opt->type != SANE_TYPE_BUTTON)
    {
      fputc (' ', stdout);
      if (opt->cap & SANE_CAP_AUTOMATIC)
	{
	  fputs ("auto|", stdout);
	  not_first = SANE_TRUE;
	}
      switch (opt->constraint_type)
	{
	case SANE_CONSTRAINT_NONE:
	  switch (opt->type)
	    {
	    case SANE_TYPE_INT:
	      fputs ("<int>", stdout);
	      break;
	    case SANE_TYPE_FIXED:
	      fputs ("<float>", stdout);
	      break;
	    case SANE_TYPE_STRING:
	      fputs ("<string>", stdout);
	      break;
	    default:
	      break;
	    }
	  if (opt->type != SANE_TYPE_STRING
           && opt->size > (SANE_Int) sizeof (SANE_Word))
	    fputs (",...", stdout);
	  break;

	case SANE_CONSTRAINT_RANGE:
	  if (opt->type == SANE_TYPE_INT)
	    {
	      if (!strcmp (opt->name, "x"))
		{
		  printf ("%d..%d",
                          opt->constraint.range->min, 
                          opt->constraint.range->max - tl_x);
		}
	      else if (!strcmp (opt->name, "y"))
		{
		  printf ("%d..%d",
                          opt->constraint.range->min,
                          opt->constraint.range->max - tl_y);
		}
	      else
		{
		  printf ("%d..%d",
			  opt->constraint.range->min,
			  opt->constraint.range->max);
		}
	      print_unit (opt->unit);
	      if (opt->size > (SANE_Int) sizeof (SANE_Word))
		fputs (",...", stdout);
	      if (opt->constraint.range->quant)
		printf (" (in steps of %d)", opt->constraint.range->quant);
	    }
	  else
	    {
	      if (!strcmp (opt->name, "x"))
		{
		  printf ("%g..%g",
			  SANE_UNFIX (opt->constraint.range->min),
			  SANE_UNFIX (opt->constraint.range->max - tl_x));
		}
	      else if (!strcmp (opt->name, "y"))
		{
		  printf ("%g..%g",
			  SANE_UNFIX (opt->constraint.range->min),
			  SANE_UNFIX (opt->constraint.range->max - tl_y));
		}
	      else
		{
		  printf ("%g..%g",
			  SANE_UNFIX (opt->constraint.range->min),
			  SANE_UNFIX (opt->constraint.range->max));
		}
	      print_unit (opt->unit);
	      if (opt->size > (SANE_Int) sizeof (SANE_Word))
		fputs (",...", stdout);
	      if (opt->constraint.range->quant)
		printf (" (in steps of %g)",
			SANE_UNFIX (opt->constraint.range->quant));
	    }
	  break;

	case SANE_CONSTRAINT_WORD_LIST:
	  for (i = 0; i < opt->constraint.word_list[0]; ++i)
	    {
	      if (not_first)
		fputc ('|', stdout);

	      not_first = SANE_TRUE;

	      if (opt->type == SANE_TYPE_INT)
		printf ("%d", opt->constraint.word_list[i + 1]);
	      else
		printf ("%g", SANE_UNFIX (opt->constraint.word_list[i + 1]));
	    }
	  print_unit (opt->unit);
	  if (opt->size > (SANE_Int) sizeof (SANE_Word))
	    fputs (",...", stdout);
	  break;

	case SANE_CONSTRAINT_STRING_LIST:
	  for (i = 0; opt->constraint.string_list[i]; ++i)
	    {
	      if (i > 0)
		fputc ('|', stdout);

	      fputs (opt->constraint.string_list[i], stdout);
	    }
	  break;
	}
    }

  /* print current option value */
  if (opt->type == SANE_TYPE_STRING || opt->size == sizeof (SANE_Word))
    {
      if (SANE_OPTION_IS_ACTIVE (opt->cap))
	{
	  void *val = alloca (opt->size);
	  sane_control_option (device, opt_num, SANE_ACTION_GET_VALUE, val,
			       0);
	  fputs (" [", stdout);
	  switch (opt->type)
	    {
	    case SANE_TYPE_BOOL:
	      fputs (*(SANE_Bool *) val ? "yes" : "no", stdout);
	      break;

	    case SANE_TYPE_INT:
	      if (strcmp (opt->name, "l") == 0)
		{
		  tl_x = (*(SANE_Fixed *) val);
		  printf ("%d", tl_x);
		}
	      else if (strcmp (opt->name, "t") == 0)
		{
		  tl_y = (*(SANE_Fixed *) val);
		  printf ("%d", tl_y);
		}
	      else if (strcmp (opt->name, "x") == 0)
		{
		  br_x = (*(SANE_Fixed *) val);
		  printf ("%d", br_x - tl_x);
		}
	      else if (strcmp (opt->name, "y") == 0)
		{
		  br_y = (*(SANE_Fixed *) val);
		  printf ("%d", br_y - tl_y);
		}
	      else
		printf ("%d", *(SANE_Int *) val);
	      break;

	    case SANE_TYPE_FIXED:

	      if (strcmp (opt->name, "l") == 0)
		{
		  tl_x = (*(SANE_Fixed *) val);
		  printf ("%g", SANE_UNFIX (tl_x));
		}
	      else if (strcmp (opt->name, "t") == 0)
		{
		  tl_y = (*(SANE_Fixed *) val);
		  printf ("%g", SANE_UNFIX (tl_y));
		}
	      else if (strcmp (opt->name, "x") == 0)
		{
		  br_x = (*(SANE_Fixed *) val);
		  printf ("%g", SANE_UNFIX (br_x - tl_x));
		}
	      else if (strcmp (opt->name, "y") == 0)
		{
		  br_y = (*(SANE_Fixed *) val);
		  printf ("%g", SANE_UNFIX (br_y - tl_y));
		}
	      else
		printf ("%g", SANE_UNFIX (*(SANE_Fixed *) val));

	      break;

	    case SANE_TYPE_STRING:
	      fputs ((char *) val, stdout);
	      break;

	    default:
	      break;
	    }
	  fputc (']', stdout);
	}
    }

  if (!SANE_OPTION_IS_ACTIVE (opt->cap))
    fputs (" [inactive]", stdout);

  else if(opt->cap & SANE_CAP_HARD_SELECT)
    fputs (" [hardware]", stdout);

  else if(!(opt->cap & SANE_CAP_SOFT_SELECT) && opt->cap & SANE_CAP_SOFT_DETECT)
    fputs (" [read-only]", stdout);

  fputs ("\n        ", stdout);

  column = 8;
  last_break = 0;
  start = opt->desc;
  for (str = opt->desc; *str; ++str)
    {
      ++column;
      if (*str == ' ')
        last_break = str;
      else if (*str == '\n'){
        column=80;
        last_break = str;
      }
      if (column >= 79 && last_break)
        {
          while (start < last_break)
            fputc (*start++, stdout);
          start = last_break + 1;	/* skip blank */
          fputs ("\n        ", stdout);
          column = 8 + (str - start);
        }
    }
  while (*start)
    fputc (*start++, stdout);
  fputc ('\n', stdout);
}

/* A scalar has the following syntax:

     V [ U ]

   V is the value of the scalar.  It is either an integer or a
   floating point number, depending on the option type.

   U is an optional unit.  If not specified, the default unit is used.
   The following table lists which units are supported depending on
   what the option's default unit is:

     Option's unit:	Allowed units:

     SANE_UNIT_NONE:
     SANE_UNIT_PIXEL:	pel
     SANE_UNIT_BIT:	b (bit), B (byte)
     SANE_UNIT_MM:	mm (millimeter), cm (centimeter), in or " (inches),
     SANE_UNIT_DPI:	dpi
     SANE_UNIT_PERCENT:	%
     SANE_UNIT_PERCENT:	us
 */
static const char *
parse_scalar (const SANE_Option_Descriptor * opt, const char *str,
	      SANE_Word * value)
{
  char *end;
  double v;

  if (opt->type == SANE_TYPE_FIXED)
    v = strtod (str, &end) * (1 << SANE_FIXED_SCALE_SHIFT);
  else
    v = strtol (str, &end, 10);

  if (str == end)
    {
      fprintf (stderr,
	       "%s: option --%s: bad option value (rest of option: %s)\n",
	       prog_name, opt->name, str);
      exit (1);
    }
  str = end;

  switch (opt->unit)
    {
    case SANE_UNIT_NONE:
    case SANE_UNIT_PIXEL:
      break;

    case SANE_UNIT_BIT:
      if (*str == 'b' || *str == 'B')
	{
	  if (*str++ == 'B')
	    v *= 8;
	}
      break;

    case SANE_UNIT_MM:
      if (str[0] == '\0')
	v *= 1.0;		/* default to mm */
      else if (strcmp (str, "mm") == 0)
	str += sizeof ("mm") - 1;
      else if (strcmp (str, "cm") == 0)
	{
	  str += sizeof ("cm") - 1;
	  v *= 10.0;
	}
      else if (strcmp (str, "in") == 0 || *str == '"')
	{
	  if (*str++ != '"')
	    ++str;
	  v *= 25.4;		/* 25.4 mm/inch */
	}
      else
	{
	  fprintf (stderr,
		   "%s: option --%s: illegal unit (rest of option: %s)\n",
		   prog_name, opt->name, str);
	  return 0;
	}
      break;

    case SANE_UNIT_DPI:
      if (strcmp (str, "dpi") == 0)
	str += sizeof ("dpi") - 1;
      break;

    case SANE_UNIT_PERCENT:
      if (*str == '%')
	++str;
      break;

    case SANE_UNIT_MICROSECOND:
      if (strcmp (str, "us") == 0)
	str += sizeof ("us") - 1;
      break;
    }

  if(v < 0){
    *value = v - 0.5;
  }
  else{
    *value = v + 0.5;
  }

  return str;
}

/* A vector has the following syntax:

     [ '[' I ']' ] S { [','|'-'] [ '[' I ']' S }

   The number in brackets (I), if present, determines the index of the
   vector element to be set next.  If I is not present, the value of
   last index used plus 1 is used.  The first index value used is 0
   unless I is present.

   S is a scalar value as defined by parse_scalar().

   If two consecutive value specs are separated by a comma (,) their
   values are set independently.  If they are separated by a dash (-),
   they define the endpoints of a line and all vector values between
   the two endpoints are set according to the value of the
   interpolated line.  For example, [0]15-[255]15 defines a vector of
   256 elements whose value is 15.  Similarly, [0]0-[255]255 defines a
   vector of 256 elements whose value starts at 0 and increases to
   255.  */
static void
parse_vector (const SANE_Option_Descriptor * opt, const char *str,
	      SANE_Word * vector, size_t vector_length)
{
  SANE_Word value, prev_value = 0;
  int index = -1, prev_index = 0;
  char *end, separator = '\0';

  /* initialize vector to all zeroes: */
  memset (vector, 0, vector_length * sizeof (SANE_Word));

  do
    {
      if (*str == '[')
	{
	  /* read index */
	  index = strtol (++str, &end, 10);
	  if (str == end || *end != ']')
	    {
	      fprintf (stderr, "%s: option --%s: closing bracket missing "
		       "(rest of option: %s)\n", prog_name, opt->name, str);
	      exit (1);
	    }
	  str = end + 1;
	}
      else
	++index;

      if (index < 0 || index >= (int) vector_length)
	{
	  fprintf (stderr,
		   "%s: option --%s: index %d out of range [0..%ld]\n",
		   prog_name, opt->name, index, (long) vector_length - 1);
	  exit (1);
	}

      /* read value */
      str = parse_scalar (opt, str, &value);
      if (!str)
	exit (1);

      if (*str && *str != '-' && *str != ',')
	{
	  fprintf (stderr,
		   "%s: option --%s: illegal separator (rest of option: %s)\n",
		   prog_name, opt->name, str);
	  exit (1);
	}

      /* store value: */
      vector[index] = value;
      if (separator == '-')
	{
	  /* interpolate */
	  double v, slope;
	  int i;

	  v = (double) prev_value;
	  slope = ((double) value - v) / (index - prev_index);

	  for (i = prev_index + 1; i < index; ++i)
	    {
	      v += slope;
	      vector[i] = (SANE_Word) v;
	    }
	}

      prev_index = index;
      prev_value = value;
      separator = *str++;
    }
  while (separator == ',' || separator == '-');

  if (verbose > 2)
    {
      int i;

      fprintf (stderr, "%s: value for --%s is: ", prog_name, opt->name);
      for (i = 0; i < (int) vector_length; ++i)
	if (opt->type == SANE_TYPE_FIXED)
	  fprintf (stderr, "%g ", SANE_UNFIX (vector[i]));
	else
	  fprintf (stderr, "%d ", vector[i]);
      fputc ('\n', stderr);
    }
}

static void
fetch_options (SANE_Device * device)
{
  const SANE_Option_Descriptor *opt;
  SANE_Int num_dev_options;
  int i, option_count;
  SANE_Status status;

  opt = sane_get_option_descriptor (device, 0);
  if (opt == NULL)
    {
      fprintf (stderr, "Could not get option descriptor for option 0\n");
      exit (1);
    }

  status = sane_control_option (device, 0, SANE_ACTION_GET_VALUE,
                                &num_dev_options, 0);
  if (status != SANE_STATUS_GOOD)
    {
      fprintf (stderr, "Could not get value for option 0: %s\n",
               sane_strstatus (status));
      exit (1);
    }

  /* build the full table of long options */
  option_count = 0;
  for (i = 1; i < num_dev_options; ++i)
    {
      opt = sane_get_option_descriptor (device, i);
      if (opt == NULL)
	{
	  fprintf (stderr, "Could not get option descriptor for option %d\n",i);
	  exit (1);
	}

      /* create command line option only for settable options */
      if (!SANE_OPTION_IS_SETTABLE (opt->cap) || opt->type == SANE_TYPE_GROUP)
	continue;

      option_number[option_count] = i;

      all_options[option_count].name = (const char *) opt->name;
      all_options[option_count].flag = 0;
      all_options[option_count].val = 0;

      if (opt->type == SANE_TYPE_BOOL)
	all_options[option_count].has_arg = optional_argument;
      else if (opt->type == SANE_TYPE_BUTTON)
	all_options[option_count].has_arg = no_argument;
      else
	all_options[option_count].has_arg = required_argument;

      /* Look for scan resolution */
      if ((opt->type == SANE_TYPE_FIXED || opt->type == SANE_TYPE_INT)
	  && opt->size == sizeof (SANE_Int)
	  && (opt->unit == SANE_UNIT_DPI)
	  && (strcmp (opt->name, SANE_NAME_SCAN_RESOLUTION) == 0))
	resolution_optind = i;

      /* Keep track of top-left corner options (if they exist at
         all) and replace the bottom-right corner options by a
         width/height option (if they exist at all).  */
      if ((opt->type == SANE_TYPE_FIXED || opt->type == SANE_TYPE_INT)
	  && opt->size == sizeof (SANE_Int)
	  && (opt->unit == SANE_UNIT_MM || opt->unit == SANE_UNIT_PIXEL))
	{
	  if (strcmp (opt->name, SANE_NAME_SCAN_BR_X) == 0)
	    {
	      window[0] = i;
	      all_options[option_count].name = "width";
	      all_options[option_count].val = 'x';
	      window_option[0] = *opt;
	      window_option[0].title = "Scan width";
	      window_option[0].desc = "Width of scan-area.";
	      window_option[0].name = "x";
	    }
	  else if (strcmp (opt->name, SANE_NAME_SCAN_BR_Y) == 0)
	    {
	      window[1] = i;
	      all_options[option_count].name = "height";
	      all_options[option_count].val = 'y';
	      window_option[1] = *opt;
	      window_option[1].title = "Scan height";
	      window_option[1].desc = "Height of scan-area.";
	      window_option[1].name = "y";
	    }
	  else if (strcmp (opt->name, SANE_NAME_SCAN_TL_X) == 0)
	    {
	      window[2] = i;
	      all_options[option_count].val = 'l';
	      window_option[2] = *opt;
	      window_option[2].name = "l";
	    }
	  else if (strcmp (opt->name, SANE_NAME_SCAN_TL_Y) == 0)
	    {
	      window[3] = i;
	      all_options[option_count].val = 't';
	      window_option[3] = *opt;
	      window_option[3].name = "t";
	    }
	}
      ++option_count;
    }
  memcpy (all_options + option_count, basic_options, sizeof (basic_options));
  option_count += NELEMS (basic_options);
  memset (all_options + option_count, 0, sizeof (all_options[0]));

  /* Initialize width & height options based on backend default
     values for top-left x/y and bottom-right x/y: */
  for (i = 0; i < 2; ++i)
    {
      if (window[i] && !window_val_user[i])
	{
	  sane_control_option (device, window[i],
                                SANE_ACTION_GET_VALUE, &window_val[i], 0);
          if (window[i + 2]){
	    SANE_Word pos;
	    sane_control_option (device, window[i + 2],
			       SANE_ACTION_GET_VALUE, &pos, 0);
	    window_val[i] -= pos;
          }
	}
    }
}

static void
set_option (SANE_Handle device, int optnum, void *valuep)
{
  const SANE_Option_Descriptor *opt;
  SANE_Status status;
  SANE_Word orig = 0;
  SANE_Int info = 0;

  opt = sane_get_option_descriptor (device, optnum);
  if (opt && (!SANE_OPTION_IS_ACTIVE (opt->cap)))
    {
      if (verbose > 0)
	fprintf (stderr, "%s: ignored request to set inactive option %s\n",
		 prog_name, opt->name);
      return;
    }
    
  if (opt->size == sizeof (SANE_Word) && opt->type != SANE_TYPE_STRING)
    orig = *(SANE_Word *) valuep;

  status = sane_control_option (device, optnum, SANE_ACTION_SET_VALUE,
				valuep, &info);
  if (status != SANE_STATUS_GOOD)
    {
      fprintf (stderr, "%s: setting of option --%s failed (%s)\n",
	       prog_name, opt->name, sane_strstatus (status));
      exit (1);
    }

  if ((info & SANE_INFO_INEXACT) && opt->size == sizeof (SANE_Word))
    {
      if (opt->type == SANE_TYPE_INT)
	fprintf (stderr, "%s: rounded value of %s from %d to %d\n",
		 prog_name, opt->name, orig, *(SANE_Word *) valuep);
      else if (opt->type == SANE_TYPE_FIXED)
	fprintf (stderr, "%s: rounded value of %s from %g to %g\n",
		 prog_name, opt->name,
		 SANE_UNFIX (orig), SANE_UNFIX (*(SANE_Word *) valuep));
    }

  if (info & SANE_INFO_RELOAD_OPTIONS)
    fetch_options (device);
}

static void
process_backend_option (SANE_Handle device, int optnum, const char *optarg)
{
  static SANE_Word *vector = 0;
  static size_t vector_size = 0;
  const SANE_Option_Descriptor *opt;
  size_t vector_length;
  SANE_Status status;
  SANE_Word value;
  void *valuep;

  opt = sane_get_option_descriptor (device, optnum);

  if (!SANE_OPTION_IS_ACTIVE (opt->cap))
    {
      fprintf (stderr, "%s: attempted to set inactive option %s\n",
	       prog_name, opt->name);
      exit (1);
    }

  if ((opt->cap & SANE_CAP_AUTOMATIC) && optarg &&
      strncasecmp (optarg, "auto", 4) == 0)
    {
      status = sane_control_option (device, optnum, SANE_ACTION_SET_AUTO,
				    0, 0);
      if (status != SANE_STATUS_GOOD)
	{
	  fprintf (stderr,
		   "%s: failed to set option --%s to automatic (%s)\n",
		   prog_name, opt->name, sane_strstatus (status));
	  exit (1);
	}
      return;
    }

  valuep = &value;
  switch (opt->type)
    {
    case SANE_TYPE_BOOL:
      value = 1;		/* no argument means option is set */
      if (optarg)
	{
	  if (strncasecmp (optarg, "yes", strlen (optarg)) == 0)
	    value = 1;
	  else if (strncasecmp (optarg, "no", strlen (optarg)) == 0)
	    value = 0;
	  else
	    {
	      fprintf (stderr, "%s: option --%s: bad option value `%s'\n",
		       prog_name, opt->name, optarg);
	      exit (1);
	    }
	}
      break;

    case SANE_TYPE_INT:
    case SANE_TYPE_FIXED:
      /* ensure vector is long enough: */
      vector_length = opt->size / sizeof (SANE_Word);
      if (vector_size < vector_length)
	{
	  vector_size = vector_length;
	  vector = realloc (vector, vector_length * sizeof (SANE_Word));
	  if (!vector)
	    {
	      fprintf (stderr, "%s: out of memory\n", prog_name);
	      exit (1);
	    }
	}
      parse_vector (opt, optarg, vector, vector_length);
      valuep = vector;
      break;

    case SANE_TYPE_STRING:
      valuep = malloc (opt->size);
      if (!valuep)
	{
	  fprintf (stderr, "%s: out of memory\n", prog_name);
	  exit (1);
	}
      strncpy (valuep, optarg, opt->size);
      ((char *) valuep)[opt->size - 1] = 0;
      break;

    case SANE_TYPE_BUTTON:
      value = 0;		/* value doesn't matter */
      break;

    default:
      fprintf (stderr, "%s: duh, got unknown option type %d\n",
	       prog_name, opt->type);
      return;
    }
  set_option (device, optnum, valuep);
}

static void
write_pnm_header (SANE_Frame format, int width, int height, int depth)
{
  /* The netpbm-package does not define raw image data with maxval > 255. */
  /* But writing maxval 65535 for 16bit data gives at least a chance */
  /* to read the image. */
  switch (format)
    {
    case SANE_FRAME_RED:
    case SANE_FRAME_GREEN:
    case SANE_FRAME_BLUE:
    case SANE_FRAME_RGB:
      printf ("P6\n# SANE data follows\n%d %d\n%d\n", width, height,
	      (depth <= 8) ? 255 : 65535);
      break;

    default:
      if (depth == 1)
	printf ("P4\n# SANE data follows\n%d %d\n", width, height);
      else
	printf ("P5\n# SANE data follows\n%d %d\n%d\n", width, height,
		(depth <= 8) ? 255 : 65535);
      break;
    }
#ifdef __EMX__			/* OS2 - write in binary mode. */
  _fsetmode (stdout, "b");
#endif
}

static void *
advance (Image * image)
{
  if (++image->x >= image->width)
    {
      image->x = 0;
      if (++image->y >= image->height || !image->data)
	{
	  size_t old_size = 0, new_size;

	  if (image->data)
	    old_size = image->height * image->width;

	  image->height += STRIP_HEIGHT;
	  new_size = image->height * image->width;

	  if (image->data)
	    image->data = realloc (image->data, new_size);
	  else
	    image->data = malloc (new_size);
	  if (image->data)
	    memset (image->data + old_size, 0, new_size - old_size);
	}
    }
  if (!image->data)
    fprintf (stderr, "%s: can't allocate image buffer (%dx%d)\n",
	     prog_name, image->width, image->height);
  return image->data;
}

static SANE_Status
scan_it (void)
{
  int i, len, first_frame = 1, offset = 0, must_buffer = 0, hundred_percent;
  SANE_Byte min = 0xff, max = 0;
  SANE_Parameters parm;
  SANE_Status status;
  Image image = { 0, 0, 0, 0, 0 };
  static const char *format_name[] = {
    "gray", "RGB", "red", "green", "blue"
  };
  SANE_Word total_bytes = 0, expected_bytes;
  SANE_Int hang_over = -1;

  do
    {
      if (!first_frame)
	{
#ifdef SANE_STATUS_WARMING_UP
          do
	    {
	      status = sane_start (device);
	    }
	  while(status == SANE_STATUS_WARMING_UP);
#else
	  status = sane_start (device);
#endif
	  if (status != SANE_STATUS_GOOD)
	    {
	      fprintf (stderr, "%s: sane_start: %s\n",
		       prog_name, sane_strstatus (status));
	      goto cleanup;
	    }
	}

      status = sane_get_parameters (device, &parm);
      if (status != SANE_STATUS_GOOD)
	{
	  fprintf (stderr, "%s: sane_get_parameters: %s\n",
		   prog_name, sane_strstatus (status));
	  goto cleanup;
	}

      if (verbose)
	{
	  if (first_frame)
	    {
	      if (parm.lines >= 0)
		fprintf (stderr, "%s: scanning image of size %dx%d pixels at "
			 "%d bits/pixel\n",
			 prog_name, parm.pixels_per_line, parm.lines,
			 8 * parm.bytes_per_line / parm.pixels_per_line);
	      else
		fprintf (stderr, "%s: scanning image %d pixels wide and "
			 "variable height at %d bits/pixel\n",
			 prog_name, parm.pixels_per_line,
			 8 * parm.bytes_per_line / parm.pixels_per_line);
	    }

	  fprintf (stderr, "%s: acquiring %s frame\n", prog_name,
	   parm.format <= SANE_FRAME_BLUE ? format_name[parm.format]:"Unknown");
	}

      if (first_frame)
	{
	  switch (parm.format)
	    {
	    case SANE_FRAME_RED:
	    case SANE_FRAME_GREEN:
	    case SANE_FRAME_BLUE:
	      assert (parm.depth == 8);
	      must_buffer = 1;
	      offset = parm.format - SANE_FRAME_RED;
	      break;

	    case SANE_FRAME_RGB:
	      assert ((parm.depth == 8) || (parm.depth == 16));
	    case SANE_FRAME_GRAY:
	      assert ((parm.depth == 1) || (parm.depth == 8)
		      || (parm.depth == 16));
	      if (parm.lines < 0)
		{
		  must_buffer = 1;
		  offset = 0;
		}
	      else
		{
		  if (output_format == OUTPUT_TIFF)
		    sanei_write_tiff_header (parm.format,
					     parm.pixels_per_line, parm.lines,
					     parm.depth, resolution_value,
					     icc_profile);
		  else
		    write_pnm_header (parm.format, parm.pixels_per_line,
				      parm.lines, parm.depth);
		}
	      break;

            default:
	      break;
	    }

	  if (must_buffer)
	    {
	      /* We're either scanning a multi-frame image or the
		 scanner doesn't know what the eventual image height
		 will be (common for hand-held scanners).  In either
		 case, we need to buffer all data before we can write
		 the image.  */
	      image.width = parm.bytes_per_line;

	      if (parm.lines >= 0)
		/* See advance(); we allocate one extra line so we
		   don't end up realloc'ing in when the image has been
		   filled in.  */
		image.height = parm.lines - STRIP_HEIGHT + 1;
	      else
		image.height = 0;

	      image.x = image.width - 1;
	      image.y = -1;
	      if (!advance (&image))
		{
		  status = SANE_STATUS_NO_MEM;
		  goto cleanup;
		}
	    }
	}
      else
	{
	  assert (parm.format >= SANE_FRAME_RED
		  && parm.format <= SANE_FRAME_BLUE);
	  offset = parm.format - SANE_FRAME_RED;
	  image.x = image.y = 0;
	}
      hundred_percent = parm.bytes_per_line * parm.lines 
	* ((parm.format == SANE_FRAME_RGB || parm.format == SANE_FRAME_GRAY) ? 1:3);

      while (1)
	{
	  double progr;
	  status = sane_read (device, buffer, buffer_size, &len);
	  total_bytes += (SANE_Word) len;
          progr = ((total_bytes * 100.) / (double) hundred_percent);
          if (progr > 100.)
	    progr = 100.;
          if (progress)
	    fprintf (stderr, "Progress: %3.1f%%\r", progr);

	  if (status != SANE_STATUS_GOOD)
	    {
	      if (verbose && parm.depth == 8)
		fprintf (stderr, "%s: min/max graylevel value = %d/%d\n",
			 prog_name, min, max);
	      if (status != SANE_STATUS_EOF)
		{
		  fprintf (stderr, "%s: sane_read: %s\n",
			   prog_name, sane_strstatus (status));
		  return status;
		}
	      break;
	    }

	  if (must_buffer)
	    {
	      switch (parm.format)
		{
		case SANE_FRAME_RED:
		case SANE_FRAME_GREEN:
		case SANE_FRAME_BLUE:
		  for (i = 0; i < len; ++i)
		    {
		      image.data[offset + 3 * i] = buffer[i];
		      if (!advance (&image))
			{
			  status = SANE_STATUS_NO_MEM;
			  goto cleanup;
			}
		    }
		  offset += 3 * len;
		  break;

		case SANE_FRAME_RGB:
		  for (i = 0; i < len; ++i)
		    {
		      image.data[offset + i] = buffer[i];
		      if (!advance (&image))
			  {
			    status = SANE_STATUS_NO_MEM;
			    goto cleanup;
			  }
		    }
		  offset += len;
		  break;

		case SANE_FRAME_GRAY:
		  for (i = 0; i < len; ++i)
		    {
		      image.data[offset + i] = buffer[i];
		      if (!advance (&image))
			  {
			    status = SANE_STATUS_NO_MEM;
			    goto cleanup;
			  }
		    }
		  offset += len;
		  break;

                default:
		  break;
		}
	    }
	  else			/* ! must_buffer */
	    {
	      if ((output_format == OUTPUT_TIFF) || (parm.depth != 16))
		fwrite (buffer, 1, len, stdout);
	      else
		{
#if !defined(WORDS_BIGENDIAN)
		  int i, start = 0;

		  /* check if we have saved one byte from the last sane_read */
		  if (hang_over > -1)
		    {
		      if (len > 0)
			{
			  fwrite (buffer, 1, 1, stdout);
			  buffer[0] = (SANE_Byte) hang_over;
			  hang_over = -1;
			  start = 1;
			}
		    }
		  /* now do the byte-swapping */
		  for (i = start; i < (len - 1); i += 2)
		    {
		      unsigned char LSB;
		      LSB = buffer[i];
		      buffer[i] = buffer[i + 1];
		      buffer[i + 1] = LSB;
		    }
		  /* check if we have an odd number of bytes */
		  if (((len - start) % 2) != 0)
		    {
		      hang_over = buffer[len - 1];
		      len--;
		    }
#endif
		  fwrite (buffer, 1, len, stdout);
		}
	    }

	  if (verbose && parm.depth == 8)
	    {
	      for (i = 0; i < len; ++i)
		if (buffer[i] >= max)
		  max = buffer[i];
		else if (buffer[i] < min)
		  min = buffer[i];
	    }
	}
      first_frame = 0;
    }
  while (!parm.last_frame);

  if (must_buffer)
    {
      image.height = image.y;

      if (output_format == OUTPUT_TIFF)
	sanei_write_tiff_header (parm.format, parm.pixels_per_line,
				 image.height, parm.depth, resolution_value,
				 icc_profile);
      else
	write_pnm_header (parm.format, parm.pixels_per_line,
                          image.height, parm.depth);

#if !defined(WORDS_BIGENDIAN)
      /* multibyte pnm file may need byte swap to LE */
      /* FIXME: other bit depths? */
      if (output_format != OUTPUT_TIFF && parm.depth == 16)
	{
	  int i;
	  for (i = 0; i < image.height * image.width; i += 2)
	    {
	      unsigned char LSB;
	      LSB = image.data[i];
	      image.data[i] = image.data[i + 1];
	      image.data[i + 1] = LSB;
	    }
	}
#endif

	fwrite (image.data, 1, image.height * image.width, stdout);
    }

  /* flush the output buffer */
  fflush( stdout );

cleanup:
  if (image.data)
    free (image.data);


  expected_bytes = parm.bytes_per_line * parm.lines *
    ((parm.format == SANE_FRAME_RGB
      || parm.format == SANE_FRAME_GRAY) ? 1 : 3);
  if (parm.lines < 0)
    expected_bytes = 0;
  if (total_bytes > expected_bytes && expected_bytes != 0)
    {
      fprintf (stderr,
	       "%s: WARNING: read more data than announced by backend "
	       "(%u/%u)\n", prog_name, total_bytes, expected_bytes);
    }
  else if (verbose)
    fprintf (stderr, "%s: read %u bytes in total\n", prog_name, total_bytes);

  return status;
}

#define clean_buffer(buf,size)	memset ((buf), 0x23, size)

static void
pass_fail (int max, int len, SANE_Byte * buffer, SANE_Status status)
{
  if (status != SANE_STATUS_GOOD)
    fprintf (stderr, "FAIL Error: %s\n", sane_strstatus (status));
  else if (buffer[len] != 0x23)
    {
      while (len <= max && buffer[len] != 0x23)
	++len;
      fprintf (stderr, "FAIL Cheat: %d bytes\n", len);
    }
  else if (len > max)
    fprintf (stderr, "FAIL Overflow: %d bytes\n", len);
  else if (len == 0)
    fprintf (stderr, "FAIL No data\n");
  else
    fprintf (stderr, "PASS\n");
}

static SANE_Status
test_it (void)
{
  int i, len;
  SANE_Parameters parm;
  SANE_Status status;
  Image image = { 0, 0, 0, 0, 0 };
  static const char *format_name[] =
    { "gray", "RGB", "red", "green", "blue" };

#ifdef SANE_STATUS_WARMING_UP
  do
    {
      status = sane_start (device);
    }
  while(status == SANE_STATUS_WARMING_UP);
#else
  status = sane_start (device);
#endif

  if (status != SANE_STATUS_GOOD)
    {
      fprintf (stderr, "%s: sane_start: %s\n",
	       prog_name, sane_strstatus (status));
      goto cleanup;
    }

  status = sane_get_parameters (device, &parm);
  if (status != SANE_STATUS_GOOD)
    {
      fprintf (stderr, "%s: sane_get_parameters: %s\n",
	       prog_name, sane_strstatus (status));
      goto cleanup;
    }

  if (parm.lines >= 0)
    fprintf (stderr, "%s: scanning image of size %dx%d pixels at "
	     "%d bits/pixel\n", prog_name, parm.pixels_per_line, parm.lines,
	     8 * parm.bytes_per_line / parm.pixels_per_line);
  else
    fprintf (stderr, "%s: scanning image %d pixels wide and "
	     "variable height at %d bits/pixel\n",
	     prog_name, parm.pixels_per_line,
	     8 * parm.bytes_per_line / parm.pixels_per_line);
  fprintf (stderr, "%s: acquiring %s frame, %d bits/sample\n", prog_name,
	   parm.format <= SANE_FRAME_BLUE ? format_name[parm.format]:"Unknown",
           parm.depth);

  image.data = malloc (parm.bytes_per_line * 2);

  clean_buffer (image.data, parm.bytes_per_line * 2);
  fprintf (stderr, "%s: reading one scanline, %d bytes...\t", prog_name,
	   parm.bytes_per_line);
  status = sane_read (device, image.data, parm.bytes_per_line, &len);
  pass_fail (parm.bytes_per_line, len, image.data, status);
  if (status != SANE_STATUS_GOOD)
    goto cleanup;

  clean_buffer (image.data, parm.bytes_per_line * 2);
  fprintf (stderr, "%s: reading one byte...\t\t", prog_name);
  status = sane_read (device, image.data, 1, &len);
  pass_fail (1, len, image.data, status);
  if (status != SANE_STATUS_GOOD)
    goto cleanup;

  for (i = 2; i < parm.bytes_per_line * 2; i *= 2)
    {
      clean_buffer (image.data, parm.bytes_per_line * 2);
      fprintf (stderr, "%s: stepped read, %d bytes... \t", prog_name, i);
      status = sane_read (device, image.data, i, &len);
      pass_fail (i, len, image.data, status);
      if (status != SANE_STATUS_GOOD)
	goto cleanup;
    }

  for (i /= 2; i > 2; i /= 2)
    {
      clean_buffer (image.data, parm.bytes_per_line * 2);
      fprintf (stderr, "%s: stepped read, %d bytes... \t", prog_name, i - 1);
      status = sane_read (device, image.data, i - 1, &len);
      pass_fail (i - 1, len, image.data, status);
      if (status != SANE_STATUS_GOOD)
	goto cleanup;
    }

cleanup:
  sane_cancel (device);
  if (image.data)
    free (image.data);
  return status;
}


static int
get_resolution (void)
{
  const SANE_Option_Descriptor *resopt;
  int resol = 0;
  void *val;

  if (resolution_optind < 0)
    return 0;
  resopt = sane_get_option_descriptor (device, resolution_optind);
  if (!resopt)
    return 0;

  val = alloca (resopt->size);
  if (!val)
    return 0;

  sane_control_option (device, resolution_optind, SANE_ACTION_GET_VALUE, val,
		       0);
  if (resopt->type == SANE_TYPE_INT)
    resol = *(SANE_Int *) val;
  else
    resol = (int) (SANE_UNFIX (*(SANE_Fixed *) val) + 0.5);

  return resol;
}

static void
scanimage_exit (void)
{
  if (device)
    {
      if (verbose > 1)
	fprintf (stderr, "Closing device\n");
      sane_close (device);
    }
  if (verbose > 1)
    fprintf (stderr, "Calling sane_exit\n");
  sane_exit ();

  if (all_options)
    free (all_options);
  if (option_number)
    free (option_number);
  if (verbose > 1)
    fprintf (stderr, "scanimage: finished\n");
}

/** @brief print device options to stdout
 *
 * @param device struct of the opened device to describe
 * @param num_dev_options number of device options
 * @param ro SANE_TRUE to print read-only options
 */
static void print_options(SANE_Device * device, SANE_Int num_dev_options, SANE_Bool ro)
{
  int i, j;
  const SANE_Option_Descriptor *opt;

  for (i = 1; i < num_dev_options; ++i)
    {
      opt = 0;

      /* scan area uses modified option struct */
      for (j = 0; j < 4; ++j)
	if (i == window[j])
	  opt = window_option + j;

      if (!opt)
	opt = sane_get_option_descriptor (device, i);

      if (ro || SANE_OPTION_IS_SETTABLE (opt->cap)
	  || opt->type == SANE_TYPE_GROUP)
	print_option (device, i, opt);
    }
  if (num_dev_options)
    fputc ('\n', stdout);
}

int
main (int argc, char **argv)
{
  int ch, i, index, all_options_len;
  const SANE_Device **device_list;
  SANE_Int num_dev_options = 0;
  const char *devname = 0;
  const char *defdevname = 0;
  const char *format = 0;
  char readbuf[2];
  char *readbuf2;
  int batch = 0;
  int batch_prompt = 0;
  int batch_count = BATCH_COUNT_UNLIMITED;
  int batch_start_at = 1;
  int batch_increment = 1;
  SANE_Status status;
  char *full_optstring;
  SANE_Int version_code;

  atexit (scanimage_exit);

  buffer_size = (32 * 1024);	/* default size */

  prog_name = strrchr (argv[0], '/');
  if (prog_name)
    ++prog_name;
  else
    prog_name = argv[0];

  defdevname = getenv ("SANE_DEFAULT_DEVICE");

  sane_init (&version_code, auth_callback);

  /* make a first pass through the options with error printing and argument
     permutation disabled: */
  opterr = 0;
  while ((ch = getopt_long (argc, argv, "-" BASE_OPTSTRING, basic_options,
			    &index)) != EOF)
    {
      switch (ch)
	{
	case ':':
	case '?':
	  break;		/* may be an option that we'll parse later on */
	case 'd':
	  devname = optarg;
	  break;
	case 'b':
	  /* This may have already been set by the batch-count flag */
	  batch = 1;
	  format = optarg;
	  break;
	case 'h':
	  help = 1;
	  break;
	case 'i':		/* icc profile */
	  icc_profile = optarg;
	  break;
	case 'v':
	  ++verbose;
	  break;
	case 'p':
          progress = 1;
	  break;
	case 'B':
          if (optarg)
	    buffer_size = 1024 * atoi(optarg);
          else
	    buffer_size = (1024 * 1024);
	  break;
	case 'T':
	  test = 1;
	  break;
	case 'A':
	  all = 1;
	  break;
	case 'n':
	  dont_scan = 1;
	  break;
	case OPTION_BATCH_PROMPT:
	  batch_prompt = 1;
	  break;
	case OPTION_BATCH_INCREMENT:
	  batch_increment = atoi (optarg);
	  break;
	case OPTION_BATCH_START_AT:
	  batch_start_at = atoi (optarg);
	  break;
	case OPTION_BATCH_DOUBLE:
	  batch_increment = 2;
	  break;
	case OPTION_BATCH_COUNT:
	  batch_count = atoi (optarg);
	  batch = 1;
	  break;
	case OPTION_FORMAT:
	  if (strcmp (optarg, "tiff") == 0)
	    output_format = OUTPUT_TIFF;
	  else
	    output_format = OUTPUT_PNM;
	  break;
	case OPTION_MD5:
	  accept_only_md5_auth = 1;
	  break;
	case 'L':
	case 'f':
	  {
	    int i = 0;

	    status = sane_get_devices (&device_list, SANE_FALSE);
	    if (status != SANE_STATUS_GOOD)
	      {
		fprintf (stderr, "%s: sane_get_devices() failed: %s\n",
			 prog_name, sane_strstatus (status));
		exit (1);
	      }

	    if (ch == 'L')
	      {
		for (i = 0; device_list[i]; ++i)
		  {
		    printf ("device `%s' is a %s %s %s\n",
			    device_list[i]->name, device_list[i]->vendor,
			    device_list[i]->model, device_list[i]->type);
		  }
	      }
	    else
	      {
		int i = 0, int_arg = 0;
		char *percent, *start, *fmt;
		const char *text_arg = 0;
		char cc, ftype;

		fmt = malloc (strlen (optarg) + 1);
		if (fmt == 0)
		  {
		    fprintf (stderr, "%s: not enough memory\n", prog_name);
		    exit (1);
		  }

		for (i = 0; device_list[i]; ++i)
		  {
		    strcpy (fmt, optarg);
		    start = fmt;
		    while (*start && (percent = strchr (start, '%')))
		      {
			percent++;
			if (*percent)
			  {
			    switch (*percent)
			      {
			      case 'd':
				text_arg = device_list[i]->name;
				ftype = *percent = 's';
				break;
			      case 'v':
				text_arg = device_list[i]->vendor;
				ftype = *percent = 's';
				break;
			      case 'm':
				text_arg = device_list[i]->model;
				ftype = *percent = 's';
				break;
			      case 't':
				text_arg = device_list[i]->type;
				ftype = *percent = 's';
				break;
			      case 'i':
				int_arg = i;
				ftype = 'i';
				break;
			      case 'n':
				text_arg = "\n";
				ftype = *percent = 's';
				break;
			      case '%':
				ftype = 0;
				break;
			      default:
				fprintf (stderr,
					 "%s: unknown format specifier %%%c\n",
					 prog_name, *percent);
				*percent = '%';
				ftype = 0;
			      }
			    percent++;
			    cc = *percent;
			    *percent = 0;
			    switch (ftype)
			      {
			      case 's':
				printf (start, text_arg);
				break;
			      case 'i':
				printf (start, int_arg);
				break;
			      case 0:
				printf (start);
				break;
			      }
			    *percent = cc;
			    start = percent;
			  }
			else
			  {
			    /* last char of the string is a '%', suppress it */
			    *start = 0;
			    break;
			  }
		      }
		    if (*start)
		      printf (start);
		  }
	      }
	    if (i == 0 && ch != 'f')
	      printf ("\nNo scanners were identified. If you were expecting "
                "something different,\ncheck that the scanner is plugged "
		"in, turned on and detected by the\nsane-find-scanner tool "
		"(if appropriate). Please read the documentation\nwhich came "
		"with this software (README, FAQ, manpages).\n");

	    if (defdevname)
	      printf ("default device is `%s'\n", defdevname);
	    exit (0);
	  }

	case 'V':
	  printf ("scanimage (%s) %s; backend version %d.%d.%d\n", PACKAGE,
		  VERSION, SANE_VERSION_MAJOR (version_code),
		  SANE_VERSION_MINOR (version_code),
		  SANE_VERSION_BUILD (version_code));
	  exit (0);

	default:
	  break;		/* ignore device specific options for now */
	}
    }

  if (help)
    {
      printf ("Usage: %s [OPTION]...\n\
\n\
Start image acquisition on a scanner device and write image data to\n\
standard output.\n\
\n\
Parameters are separated by a blank from single-character options (e.g.\n\
-d epson) and by a \"=\" from multi-character options (e.g. --device-name=epson).\n\
-d, --device-name=DEVICE   use a given scanner device (e.g. hp:/dev/scanner)\n\
    --format=pnm|tiff      file format of output file\n\
-i, --icc-profile=PROFILE  include this ICC profile into TIFF file\n", prog_name);
      printf ("\
-L, --list-devices         show available scanner devices\n\
-f, --formatted-device-list=FORMAT similar to -L, but the FORMAT of the output\n\
                           can be specified: %%d (device name), %%v (vendor),\n\
                           %%m (model), %%t (type), %%i (index number), and\n\
                           %%n (newline)\n\
-b, --batch[=FORMAT]       working in batch mode, FORMAT is `out%%d.pnm' or\n\
                           `out%%d.tif' by default depending on --format\n");
      printf ("\
    --batch-start=#        page number to start naming files with\n\
    --batch-count=#        how many pages to scan in batch mode\n\
    --batch-increment=#    increase page number in filename by #\n\
    --batch-double         increment page number by two, same as\n\
                           --batch-increment=2\n\
    --batch-prompt         ask for pressing a key before scanning a page\n\
    --accept-md5-only      only accept authorization requests using md5\n");
      printf ("\
-p, --progress             print progress messages\n\
-n, --dont-scan            only set options, don't actually scan\n\
-T, --test                 test backend thoroughly\n\
-A, --all-options          list all available backend options\n\
-h, --help                 display this help message and exit\n\
-v, --verbose              give even more status messages\n\
-B, --buffer-size=#        change input buffer size (in kB, default 32)\n\
-V, --version              print version information\n");
    }

  if (!devname)
    {
      /* If no device name was specified explicitly, we look at the
         environment variable SANE_DEFAULT_DEVICE.  If this variable
         is not set, we open the first device we find (if any): */
      devname = defdevname;
      if (!devname)
	{
	  status = sane_get_devices (&device_list, SANE_FALSE);
	  if (status != SANE_STATUS_GOOD)
	    {
	      fprintf (stderr, "%s: sane_get_devices() failed: %s\n",
		       prog_name, sane_strstatus (status));
	      exit (1);
	    }
	  if (!device_list[0])
	    {
	      fprintf (stderr, "%s: no SANE devices found\n", prog_name);
	      exit (1);
	    }
	  devname = device_list[0]->name;
	}
    }

  status = sane_open (devname, &device);
  if (status != SANE_STATUS_GOOD)
    {
      fprintf (stderr, "%s: open of device %s failed: %s\n",
	       prog_name, devname, sane_strstatus (status));
      if (devname[0] == '/')
	fprintf (stderr, "\nYou seem to have specified a UNIX device name, "
		 "or filename instead of selecting\nthe SANE scanner or "
		 "image acquisition device you want to use. As an example,\n"
		 "you might want \"epson:/dev/sg0\" or "
		 "\"hp:/dev/usbscanner0\". If any supported\ndevices are "
		 "installed in your system, you should be able to see a "
		 "list with\n\"scanimage --list-devices\".\n");
      if (help)
	device = 0;
      else
	exit (1);
    }

  if (device)
    {
      const SANE_Option_Descriptor * desc_ptr;

      /* Good form to always get the descriptor once before value */
      desc_ptr = sane_get_option_descriptor(device, 0);
      if (!desc_ptr)
	{
	  fprintf (stderr, "%s: unable to get option count descriptor\n",
		   prog_name);
	  exit (1);
	}

      /* We got a device, find out how many options it has */
      status = sane_control_option (device, 0, SANE_ACTION_GET_VALUE,
				    &num_dev_options, 0);
      if (status != SANE_STATUS_GOOD)
	{
	  fprintf (stderr, "%s: unable to determine option count\n",
		   prog_name);
	  exit (1);
	}

      /* malloc global option lists */
      all_options_len = num_dev_options + NELEMS (basic_options) + 1;
      all_options = malloc (all_options_len * sizeof (all_options[0]));
      option_number_len = num_dev_options;
      option_number = malloc (option_number_len * sizeof (option_number[0]));
      if (!all_options || !option_number)
	{
	  fprintf (stderr, "%s: out of memory in main()\n",
		   prog_name);
	  exit (1);
	}

      /* load global option lists */
      fetch_options (device);

      {
	char *larg, *targ, *xarg, *yarg;
	larg = targ = xarg = yarg = "";

	/* Maybe accept t, l, x, and y options. */
	if (window[0])
	  xarg = "x:";

	if (window[1])
	  yarg = "y:";

	if (window[2])
	  larg = "l:";

	if (window[3])
	  targ = "t:";

	/* Now allocate the full option list. */
	full_optstring = malloc (strlen (BASE_OPTSTRING)
				 + strlen (larg) + strlen (targ)
				 + strlen (xarg) + strlen (yarg) + 1);

	if (!full_optstring)
	  {
	    fprintf (stderr, "%s: out of memory\n", prog_name);
	    exit (1);
	  }

	strcpy (full_optstring, BASE_OPTSTRING);
	strcat (full_optstring, larg);
	strcat (full_optstring, targ);
	strcat (full_optstring, xarg);
	strcat (full_optstring, yarg);
      }

      /* re-run argument processing with backend-specific options included
       * this time, enable error printing and arg permutation */
      optind = 0;
      opterr = 1;
      while ((ch = getopt_long (argc, argv, full_optstring, all_options,
				&index)) != EOF)
	{
	  switch (ch)
	    {
	    case ':':
	    case '?':
	      exit (1);		/* error message is printed by getopt_long() */

	    case 'd':
	    case 'h':
	    case 'p':
	    case 'v':
	    case 'V':
	    case 'T':
	    case 'B':
	      /* previously handled options */
	      break;

	    case 'x':
	      window_val_user[0] = 1;
	      parse_vector (&window_option[0], optarg, &window_val[0], 1);
	      break;

	    case 'y':
	      window_val_user[1] = 1;
	      parse_vector (&window_option[1], optarg, &window_val[1], 1);
	      break;

	    case 'l':		/* tl-x */
	      process_backend_option (device, window[2], optarg);
	      break;

	    case 't':		/* tl-y */
	      process_backend_option (device, window[3], optarg);
	      break;

	    case 0:
	      process_backend_option (device, option_number[index], optarg);
	      break;
	    }
	}
      if (optind < argc)
	{
	  fprintf (stderr, "%s: argument without option: `%s'; ", prog_name,
		   argv[argc - 1]);
	  fprintf (stderr, "try %s --help\n", prog_name);
	  exit (1);
	}

      free (full_optstring);

      /* convert x/y to br_x/br_y */
      for (index = 0; index < 2; ++index)
	if (window[index])
	  {
            SANE_Word pos = 0;
	    SANE_Word val = window_val[index];

	    if (window[index + 2])
	      {
		sane_control_option (device, window[index + 2],
				     SANE_ACTION_GET_VALUE, &pos, 0);
		val += pos;
	      }
	    set_option (device, window[index], &val);
	  }

      /* output device-specific help */
      if (help)
	{
	  printf ("\nOptions specific to device `%s':\n", devname);
	  print_options(device, num_dev_options, SANE_FALSE);
	}

      /*  list all device-specific options */
      if (all)
	{
	  printf ("\nAll options specific to device `%s':\n", devname);
	  print_options(device, num_dev_options, SANE_TRUE);
          exit (0);
	}
    }

  /* output device list */
  if (help)
    {
      printf ("\
Type ``%s --help -d DEVICE'' to get list of all options for DEVICE.\n\
\n\
List of available devices:", prog_name);
      status = sane_get_devices (&device_list, SANE_FALSE);
      if (status == SANE_STATUS_GOOD)
	{
	  int column = 80;

	  for (i = 0; device_list[i]; ++i)
	    {
	      if (column + strlen (device_list[i]->name) + 1 >= 80)
		{
		  printf ("\n    ");
		  column = 4;
		}
	      if (column > 4)
		{
		  fputc (' ', stdout);
		  column += 1;
		}
	      fputs (device_list[i]->name, stdout);
	      column += strlen (device_list[i]->name);
	    }
	}
      fputc ('\n', stdout);
      exit (0);
    }

  if (dont_scan)
    exit (0);

  if (output_format != OUTPUT_PNM)
    resolution_value = get_resolution ();

#ifdef SIGHUP
  signal (SIGHUP, sighandler);
#endif
#ifdef SIGPIPE
  signal (SIGPIPE, sighandler);
#endif
  signal (SIGINT, sighandler);
  signal (SIGTERM, sighandler);

  if (test == 0)
    {
      int n = batch_start_at;

      if (batch && NULL == format)
	{
	  if (output_format == OUTPUT_TIFF)
	    format = "out%d.tif";
	  else
	    format = "out%d.pnm";
	}

      if (batch)
	fprintf (stderr,
		 "Scanning %d pages, incrementing by %d, numbering from %d\n",
		 batch_count, batch_increment, batch_start_at);

      else if(isatty(fileno(stdout))){
	fprintf (stderr,"%s: output is not a file, exiting\n", prog_name);
        exit (1);
      }

      buffer = malloc (buffer_size);

      do
	{
	  char path[PATH_MAX];
	  char part_path[PATH_MAX];
	  if (batch)		/* format is NULL unless batch mode */
	    {
	      sprintf (path, format, n);	/* love --(C++) */
	      strcpy (part_path, path);
	      strcat (part_path, ".part");
	    }


	  if (batch)
	    {
	      if (batch_prompt)
		{
		  fprintf (stderr, "Place document no. %d on the scanner.\n",
			   n);
		  fprintf (stderr, "Press <RETURN> to continue.\n");
		  fprintf (stderr, "Press Ctrl + D to terminate.\n");
		  readbuf2 = fgets (readbuf, 2, stdin);

		  if (readbuf2 == NULL)
		    {
		      fprintf (stderr, "Batch terminated, %d pages scanned\n",
			       (n - batch_increment));
		      fclose (stdout);
		      break;	/* get out of this loop */
		    }
		}
	      fprintf (stderr, "Scanning page %d\n", n);
	    }

#ifdef SANE_STATUS_WARMING_UP
          do
	    {
	      status = sane_start (device);
	    }
	  while(status == SANE_STATUS_WARMING_UP);
#else
	  status = sane_start (device);
#endif
	  if (status != SANE_STATUS_GOOD)
	    {
	      fprintf (stderr, "%s: sane_start: %s\n",
		       prog_name, sane_strstatus (status));
	      fclose (stdout);
	      break;
	    }

	  /* write to .part file while scanning is in progress */
	  if (batch && NULL == freopen (part_path, "w", stdout))
	    {
	      fprintf (stderr, "cannot open %s\n", part_path);
	      sane_cancel (device);
	      return SANE_STATUS_ACCESS_DENIED;
	    }

	  status = scan_it ();
	  if (batch)
	    {
	      fprintf (stderr, "Scanned page %d.", n);
	      fprintf (stderr, " (scanner status = %d)\n", status);
	    }

	  switch (status)
	    {
	    case SANE_STATUS_GOOD:
	    case SANE_STATUS_EOF:
	      status = SANE_STATUS_GOOD;
	      if (batch)
		{	
		  /* close output file by redirecting, do not close
		     stdout here! */
		  if (NULL == freopen ("/dev/null", "w", stdout))
		    {
		      fprintf (stderr, "cannot open /dev/null\n");
		      sane_cancel (device);
		      return SANE_STATUS_ACCESS_DENIED;
		    }
		  else
		    {
		      /* let the fully scanned file show up */
		      if (rename (part_path, path))
			{
			  fprintf (stderr, "cannot rename %s to %s\n",
				part_path, path);
			  sane_cancel (device);
			  return SANE_STATUS_ACCESS_DENIED;
			}
		    }
		}
	      break;
	    default:
	      if (batch)
		{
		  fclose (stdout);
		  unlink (part_path);
		}
	      break;
	    }			/* switch */
	  n += batch_increment;
	}
      while ((batch
	      && (batch_count == BATCH_COUNT_UNLIMITED || --batch_count))
	     && SANE_STATUS_GOOD == status);

      sane_cancel (device);
    }
  else
    status = test_it ();

  return status;
}
