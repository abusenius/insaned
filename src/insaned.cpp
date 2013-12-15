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

#include <iostream>
#include <string>
#include <getopt.h>

#include "InsaneDaemon.h"
#include "InsaneException.h"

int main(int argc, char ** argv)
{
    const char * BASE_OPTSTRING = "d:hvV";
    option basic_options[] = {
        {"device-name", required_argument, NULL, 'd'},
        {"help", no_argument, NULL, 'h'},
        {"verbose", no_argument, NULL, 'v'},
        {"version", no_argument, NULL, 'V'},
        {0, 0, NULL, 0}
    };

    std::string prog_name;
    prog_name = std::string(argv[0]);
    size_t n = prog_name.rfind('/');
    if (n != std::string::npos) {
        prog_name = prog_name.substr(n + 1);
    }

    bool help = false;
    int verbose = 0;
    std::string devname;

    // get dameon instance
    InsaneDaemon & daemon = InsaneDaemon::instance();

    /* make a pass through the options with error printing */
    int ch = 0;
    int index = 0;
    opterr = 1;
    while ((ch = getopt_long(argc, argv, BASE_OPTSTRING, basic_options, &index)) != EOF) {
        switch (ch) {
        case ':':
        case '?':
            return 1; // error is printed by getopt_long
            break;
        case 'd':
            devname = optarg;
            break;
        case 'h':
            help = true;
            break;
        case 'v':
            ++verbose;
            break;
        case 'V':
            std::cout << InsaneDaemon::NAME << " " << VERSION
                      << ". SANE backend version " << daemon.get_sane_version() << std::endl;
            return 0;
        default:
            std::cerr << "Unknown option: " << static_cast<char>(ch) << std::endl;
            return 1;
            break;
        }
    }
    daemon.init(verbose);

    /* print help and device list */
    if (help) {
        std::cout << "Usage: " << prog_name << " [OPTION]...\n"
            << "\n"
            << "Start polling sensors (buttons) of a scanner device and run callback scripts\n"
            << "when a button is pressed.\n"
            << "\n"
            << "Parameters are separated by a blank from single-character options (e.g.\n"
            << "-d epson) and by a \"=\" from multi-character options (e.g. --device-name=epson).\n"
            << " -d, --device-name=DEVICE   use a given scanner device (e.g. hp:/dev/scanner)\n"
            << " -h, --help                 display this help message and exit\n"
            << " -v, --verbose              give even more status messages\n"
            << " -V, --version              print version information" << std::endl;

        std::cout << "\nList of available devices:" << std::endl;
        try {
            for (auto & device : daemon.get_devices()) {
                std::cout << "\n    " << device;
            }
            std::cout << std::endl;
        } catch (InsaneException & e) {
            std::cerr << "\n" << InsaneDaemon::NAME << ": " << e.what() << std::endl;
            return 1;
        }
        return 0;
    }

    try {
        // TODO fork
        daemon.open(devname);
        daemon.run();
    } catch (InsaneException & e) {
        std::cerr << "\n" << InsaneDaemon::NAME << ": " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

