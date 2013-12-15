/* insaned -- simple daemon polling button presses using the SANE library
   Heavily based on scanimage utility from SANE distribution
   Copyright (C) 2013 - 2014 Alex Busenius

   scanimage -- command line scanning utility
   Uses the SANE library.
   Copyright (C) 1996, 1997, 1998 Andreas Beck and David Mosberger
   
   Copyright (C) 1999 - 2009 by the SANE Project

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

#include "config.h"

#include <alloca.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <getopt.h>

#include <sane/sane.h>

static struct option basic_options[] = {
    {"device-name", required_argument, NULL, 'd'},
    {"help", no_argument, NULL, 'h'},
    {"verbose", no_argument, NULL, 'v'},
    {"all-options", no_argument, NULL, 'A'},
    {"version", no_argument, NULL, 'V'},
    {0, 0, NULL, 0}
};

#define BASE_OPTSTRING  "d:hvVA"

static int option_number_len;
static int *option_number;
static SANE_Handle device;
static int verbose;
static int all;
static int help;
static const char *prog_name;

static void fetch_options (SANE_Device * device);
static void scanimage_exit (void);

static void
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
print_option (SANE_Device * device, int opt_num, const SANE_Option_Descriptor *opt)
{
    if (!opt || opt->type == SANE_TYPE_GROUP) {
        return;
    }
    /* if both of these are set, option is invalid */
    if (opt->cap & SANE_CAP_SOFT_SELECT && opt->cap & SANE_CAP_HARD_SELECT) {
        return;
    }
    /* invalid to select but not detect */
    if (opt->cap & SANE_CAP_SOFT_SELECT && !(opt->cap & SANE_CAP_SOFT_DETECT)) {
        return;
    }
    /* if one of these three is not set, option is useless, skip it */
    if (!(opt->cap & (SANE_CAP_SOFT_SELECT | SANE_CAP_HARD_SELECT | SANE_CAP_SOFT_DETECT))) {
        return;
    }
    /* skip inactive options */
    if (! SANE_OPTION_IS_ACTIVE (opt->cap)) {
        return;
    }

    /* print the sensor option */
    if (opt->type == SANE_TYPE_BOOL
            && opt->cap & SANE_CAP_HARD_SELECT
            && !(opt->cap & SANE_CAP_SOFT_SELECT)
            && opt->cap & SANE_CAP_SOFT_DETECT) {
        /* name */
        printf ("    %s ", opt->name);
        /* print current option value */
        if (opt->size == sizeof (SANE_Word) && SANE_OPTION_IS_ACTIVE (opt->cap)) {
            void * val = alloca (opt->size);
            sane_control_option (device, opt_num, SANE_ACTION_GET_VALUE, val, 0);
            switch (opt->type)
            {
                case SANE_TYPE_BOOL:
                        fputs (*(SANE_Bool *) val ? "[yes]" : "[no]", stdout);
                        break;
                default:
                        break;
            }
        }
        fputc ('\n', stdout);
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

        ++option_count;
    }
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
    int i;
    const SANE_Option_Descriptor *opt;

    for (i = 1; i < num_dev_options; ++i)
    {
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
    int ch, i, index;
    const SANE_Device **device_list;
    SANE_Int num_dev_options = 0;
    const char *devname = 0;
    const char *defdevname = 0;
    SANE_Status status;
    SANE_Int version_code;

    atexit (scanimage_exit);

    prog_name = strrchr (argv[0], '/');
    if (prog_name)
        ++prog_name;
    else
        prog_name = argv[0];

    defdevname = getenv ("SANE_DEFAULT_DEVICE");

    sane_init (&version_code, NULL);

    /* make a pass through the options with error printing */
    opterr = 1;
    while ((ch = getopt_long (argc, argv, BASE_OPTSTRING, basic_options,
                    &index)) != EOF)
    {
        switch (ch)
        {
            case ':':
            case '?':
                    exit(1);
                    break;
            case 'd':
                    devname = optarg;
                    break;
            case 'h':
                    help = 1;
                    break;
            case 'v':
                    ++verbose;
                    break;
            case 'A':
                    all = 1;
                    break;
            case 'V':
                    printf ("%s %s; backend version %d.%d.%d\n", PACKAGE,
                            VERSION, SANE_VERSION_MAJOR (version_code),
                            SANE_VERSION_MINOR (version_code),
                            SANE_VERSION_BUILD (version_code));
                    exit (0);

            default:
                    fprintf(stderr, "Unknown option: %c\n", ch);
                    exit(1);
                    break;
        }
    }

    if (help)
    {
        printf ("Usage: %s [OPTION]...\n\
\n\
Start polling sensors (buttons) of a scanner device and run callback scripts\n\
when a button is pressed.\n\
\n\
Parameters are separated by a blank from single-character options (e.g.\n\
-d epson) and by a \"=\" from multi-character options (e.g. --device-name=epson).\n\
 -d, --device-name=DEVICE   use a given scanner device (e.g. hp:/dev/scanner)\n", prog_name);
        printf ("\
 -A, --all-options          list all available backend options\n\
 -h, --help                 display this help message and exit\n\
 -v, --verbose              give even more status messages\n\
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
            fprintf(stderr, "\nYou seem to have specified a UNIX device name, "
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
        option_number_len = num_dev_options;
        option_number = malloc (option_number_len * sizeof (option_number[0]));
        if (!option_number)
        {
            fprintf (stderr, "%s: out of memory in main()\n",
                    prog_name);
            exit (1);
        }

        /* load global option lists */
        fetch_options (device);

        /*  list all device-specific options */
        if (all)
        {
            printf ("\nSensors for device `%s':\n", devname);
            print_options(device, num_dev_options, SANE_TRUE);
            exit (0);
        }
    }

    /* output device list */
    if (help)
    {
        printf ("\nList of available devices:");
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

#ifdef SIGHUP
    signal (SIGHUP, sighandler);
#endif
#ifdef SIGPIPE
    signal (SIGPIPE, sighandler);
#endif
    signal (SIGINT, sighandler);
    signal (SIGTERM, sighandler);

    exit (status);
    return status;
}

