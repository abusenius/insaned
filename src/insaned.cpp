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
#include <syslog.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <cstring>
#include <cstdio>

#include "InsaneDaemon.h"
#include "InsaneException.h"

int main(int argc, char ** argv)
{
    // defaults
    const std::string LOGFILE       = "/var/log/" + InsaneDaemon::NAME + ".log";
    const std::string EVENTS_DIR    = "/etc/" + InsaneDaemon::NAME + "/events";
    const int SLEEP_MS              = 500;
    const int SLEEP_MIN             = 50;
    const int SLEEP_MAX             = 5000;
    const int VERBOSITY             = 0;
    const bool DO_FORK              = true;
    const bool SUSPEND_AFTER_EVENT  = false;

    // command line options
    const char * BASE_OPTSTRING = "d:hvVf:e:s:nLwp:";
    option basic_options[] = {
        {"device-name", required_argument, nullptr, 'd'},
        {"help", no_argument, nullptr, 'h'},
        {"verbose", no_argument, nullptr, 'v'},
        {"version", no_argument, nullptr, 'V'},
        {"log-file", required_argument, nullptr, 'f'},
        {"events-dir", required_argument, nullptr, 'e'},
        {"sleep-ms", required_argument, nullptr, 's'},
        {"dont-fork", no_argument, nullptr, 'n'},
        {"list-sensors", no_argument, nullptr, 'L'},
        {"suspend-after-event", no_argument, nullptr, 'w'},
        {"pid-file", required_argument, nullptr, 'p'},
        {0, 0, nullptr, 0}
    };

    std::string prog_name;
    prog_name = std::string(argv[0]);
    size_t n = prog_name.rfind('/');
    if (n != std::string::npos) {
        prog_name = prog_name.substr(n + 1);
    }

    bool help = false;
    bool list = false;
    bool suspend = SUSPEND_AFTER_EVENT;
    int verbose = VERBOSITY;
    bool do_fork = DO_FORK;
    int sleep_ms = SLEEP_MS;
    std::string devname = "";
    std::string pidfile = "";
    std::string logfile = LOGFILE;
    std::string events_dir = EVENTS_DIR;

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
        case 'L':
            list = true;
            break;
        case 'v':
            verbose++;
            break;
        case 'V':
            std::cout << InsaneDaemon::NAME << " " << VERSION
                      << ". SANE backend version " << daemon.get_sane_version() << std::endl;
            return 0;
        case 'f':
            logfile = optarg;
            break;
        case 'e':
            events_dir = optarg;
            break;
        case 'p':
            pidfile = optarg;
            break;
        case 's':
            try {
                sleep_ms = std::stoi(std::string(optarg));
                if (sleep_ms < SLEEP_MIN || SLEEP_MAX < sleep_ms) {
                    throw std::out_of_range("The value must be in range " + std::to_string(SLEEP_MIN) + ".." + std::to_string(SLEEP_MAX));
                }
            } catch (std::exception & e) {
                std::cerr << "Invalid value of --sleep-ms (" << optarg << "): " << e.what() << std::endl;
                return 1;
            }
            break;
        case 'n':
            do_fork = false;
            break;
        case 'w':
            suspend = true;
            break;
        default:
            std::cerr << "Unknown option: " << static_cast<char>(ch) << std::endl;
            return 1;
            break;
        }
    }

    daemon.init(devname, events_dir, sleep_ms, verbose, do_fork && !(help || list), suspend);

    /* print help and device list */
    if (help) {
        std::cout << "Usage: " << prog_name << " [OPTIONS]...\n"
            << "\n"
            << "Start polling sensors of a scanner device and run the corresponding callback\n"
            << "script when a button is pressed.\n"
            << "\n"
            << "Parameters are separated by a blank from single-character options (e.g.\n"
            << "-d epson) and by a \"=\" from multi-character options (e.g. --device-name=epson).\n"
            << " -d, --device-name=DEVICE   use the given scanner device instead of the first\n"
            << "                            available device\n"
            << " -f, --log-file=FILE        use the given log file instead of default\n"
            << "                            (" << LOGFILE << ")\n"
            << " -e, --events-dir=DIR       execute event scripts from the given directory\n"
            << "                            instead of the default (" << EVENTS_DIR << ")\n"
            << " -s, --sleep-ms=NUMBER      sleep for the given amount of ms between polling the\n"
            << "                            sensors instead of default (" << SLEEP_MS << " ms), must be in\n"
            << "                            range " << SLEEP_MIN << ".." << SLEEP_MAX << "\n"
            << " -n, --dont-fork            do not fork into background\n"
            << " -L, --list-sensors         list sensors that will be monitored along with their\n"
            << "                            current state and exit. See also --device-name\n"
            << " -w, --suspend-after-event  suspend sensor polling for 15 seconds after an event\n"
            << "                            handler script was triggered. Use this if insaned\n"
            << "                            tends to interfere with your handlers.\n"
            << " -p, --pid-file=FILE        if this option is present, the daemon will create\n"
            << "                            this file and write its PID into it after fork\n"
            << " -v, --verbose              give even more status messages\n"
            << " -h, --help                 display this help message and exit\n"
            << " -V, --version              print version information and exit" << std::endl;

        std::cout << "\nList of available devices:\n";
        try {
            for (auto & device : daemon.get_devices()) {
                std::cout << "    " << device << std::endl;
            }
        } catch (InsaneException & e) {
            std::cerr << "\n" << InsaneDaemon::NAME << ": " << e.what() << std::endl;
            return 1;
        }
        if (!list) {
            return 0;
        }
        std::cout << std::endl;
    }

    if (list) {
        try {
            auto sensors = daemon.get_sensors(); // updates current device if it was not set
            std::cout << "List of sensors for device '" << daemon.current_device() << "':" << std::endl;
            for (auto & pair : sensors) {
                std::cout << "    " << pair.first << "\t" << (pair.second ? "[yes]" : "[no]") << std::endl;
            }
        } catch (InsaneException & e) {
            std::cerr << "\n" << InsaneDaemon::NAME << ": " << e.what() << std::endl;
            return 1;
        }
        return 0;
    }

    try {
        // TODO su to another UID
        // TODO drop permissions
        if (do_fork) {
            // fork
            if (pid_t pid = fork()) {
                if (pid > 0) {
                    // parent process
                    return 0;
                } else {
                    syslog(LOG_ERR | LOG_USER, "First fork failed: %s", strerror(errno));
                    return 1;
                }
            }

            // detach
            setsid();

            // some settings
            if (chdir("/") < 0) {
                syslog(LOG_ERR | LOG_USER, "Unable to chdir to /: %s", strerror(errno));
                return 1;
            }
            umask(0);

            // second fork ensures the process cannot acquire a controlling terminal.
            if (pid_t pid = fork()) {
                if (pid > 0) {
                    // parent
                    return 0;
                } else {
                    syslog(LOG_ERR | LOG_USER, "Second fork failed: %s", strerror(errno));
                    return 1;
                }
            }

            // close streams
            close(0);
            close(1);
            close(2);

            // disable standard input.
            if (open("/dev/null", O_RDONLY) < 0) {
                syslog(LOG_ERR | LOG_USER, "Unable to open /dev/null: %s", strerror(errno));
                return 1;
            }

            // send standard output to a log file
            const int flags = O_WRONLY | O_CREAT | O_APPEND;
            const mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
            if (open(logfile.c_str(), flags, mode) < 0) {
                syslog(LOG_ERR | LOG_USER, "Unable to open log file %s: %s", logfile.c_str(), strerror(errno));
                return 1;
            }

            // send standard error to the same log file
            if (dup(1) < 0) {
                syslog(LOG_ERR | LOG_USER, "Unable to dup output descriptor: %s", strerror(errno));
                return 1;
            }

            if (!pidfile.empty()) {
                int fid = open(pidfile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
                if (fid < 0) {
                    syslog(LOG_ERR | LOG_USER, "Unable to open pid file %s: %s", pidfile.c_str(), strerror(errno));
                    return 1;
                }
                FILE * f = fdopen(fid, "w");
                if (f) {
                    fprintf(f, "%d\n", getpid());
                    fclose(f);
                }
                close(fid);
                f = nullptr;
            }

            syslog(LOG_INFO | LOG_USER, "Daemon started");
        }

        // run daemon
        daemon.run();

        if (do_fork) {
            syslog(LOG_INFO | LOG_USER, "Daemon stopped");
        }

        return 0;
    } catch (InsaneException & e) {
        syslog(LOG_ERR | LOG_USER, "Exception: %s", e.what());
        std::cerr << "\n" << InsaneDaemon::NAME << ": Exception: " << e.what() << std::endl;
    } catch (std::exception & e) {
        syslog(LOG_ERR | LOG_USER, "Exception: %s", e.what());
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 1;
}

