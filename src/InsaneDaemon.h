/*
 *  InsaneDaemon.h
 *
 *  This file is part of insaned.
 *  insaned is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  insaned is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with insaned; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *  Copyright (C) 2013-2014 Alex Busenius <the_unknown@gmx.net>
 */

#ifndef INSANEDAEMON_H
#define INSANEDAEMON_H

#include <vector>
#include <map>
#include <string>

#include <sane/sane.h>


/** Simple SANE button polling daemon.
 */
class InsaneDaemon
{
public:
    /// Daemon name
    static const std::string NAME;

    /**
     * @return daemon instance
     */
    static InsaneDaemon & instance() noexcept;

    /** Destructor
     */
    ~InsaneDaemon() noexcept;

    void init(bool verbose) noexcept;

    void open(std::string device_name);

    void close() noexcept;

    void run();

    /**
     * Try to fetch SANE version.
     * @return major.minor.build
     */
    std::string get_sane_version() noexcept;

    /**
     * Try to fetch list of SANE devices
     * @return device names
     */
    const std::vector<std::string> get_devices();

private:
    /// Singleton instance
    static InsaneDaemon mInstance;

    /// Current SANE device handle
    SANE_Handle mHandle = nullptr;

    /// SANE version
    SANE_Int mVersionCode = 0;

    /// Device to use
    std::string mCurrentDevice;

    /// List of detected devices
    std::vector<std::string> mDevices;

    /// Map of detected buttons (name -> option index)
    std::map<std::string, int> mSensors;

    /// Verbosity level
    int mVerbose = 0;


    /** Constructor
     */
    InsaneDaemon();

    // Forbid copy
    InsaneDaemon(const InsaneDaemon &);
    InsaneDaemon & operator=(const InsaneDaemon &);


    /**
     * Check given SANE status returned by given operation.
     *
     * @param status
     * @param operation
     * @return true if operation was successful, false if operation should be repeated
     */
    bool checkStatus(SANE_Status status, const std::string & operation);

    void log(const std::string & message, int verbosity) noexcept;

    bool is_sensor_option(const SANE_Option_Descriptor * opt);

    void print_option(int opt_num);

    void fetch_options();

    void print_options();

    static void sighandler(int signum);
};


#endif
