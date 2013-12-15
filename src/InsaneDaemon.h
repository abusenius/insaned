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
private:
    /// Helper RAII class
    class OpenGuard {
    public:
        OpenGuard(const std::string & device);

        ~OpenGuard();

    private:
        InsaneDaemon & mDaemon;
    };

    friend class OpenGuard;

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

    /**
     * Initialize the daemon
     *
     * @param device_name
     * @param events_dir
     * @param sleep_ms
     * @param verbose
     * @param log_to_syslog
     * @param suspend_after_event
     */
    void init(std::string device_name, std::string events_dir, int sleep_ms, int verbose, bool log_to_syslog, bool suspend_after_event) noexcept;

    /**
     * Run main loop and poll sensors.
     */
    void run();

    /**
     * @return currently used device name
     */
    std::string current_device() noexcept;

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

    /**
     * Try to fetch list of detected sensors and their state (true: on, false: off)
     * @return sensor names and values
     */
    std::vector<std::pair<std::string, bool>> get_sensors();

private:
    /// Timeout in ms to skip events for after trigger (avoid multiple invocations)
    static const int SKIP_TIMEOUT_MS;

    /// Timeout in ms to suspend main loop when device is busy
    static const int BUSY_TIMEOUT_MS;

    /// Singleton instance
    static InsaneDaemon mInstance;

    /// Current SANE device handle
    SANE_Handle mHandle = nullptr;

    /// SANE version
    SANE_Int mVersionCode = 0;

    /// Device to use
    std::string mCurrentDevice = "";

    /// Directory where event scripts are located
    std::string mEventsDir = "";

    /// Time in ms to sleep between polling the sensors
    int mSleepMs = 500;

    /// If true, log(..) will log to syslog
    bool mLogToSyslog = false;

    /// Suspend main loop right after event handler script was successfully executed, assuming that device is busy
    bool mSuspendAfterEvent = false;

    /// List of detected devices
    std::vector<std::string> mDevices;

    /// Map of detected buttons (name -> option index)
    std::map<std::string, int> mSensors;

    /// Verbosity level
    int mVerbose = 0;

    /// Main loop is run while true
    bool mRun = false;

    /// Counter to suspend the main loop when device is busy
    int mSuspendCount = 0;

    /// Used to skip events after trigger
    std::map<std::string, int> mRepeatCount;


    /** Constructor
     */
    InsaneDaemon();

    // Forbid copy
    InsaneDaemon(const InsaneDaemon &);
    InsaneDaemon & operator=(const InsaneDaemon &);


    /**
     * Open given device
     * @param device_name
     */
    void open(std::string device_name);

    /**
     * Close currently opened device, if any
     */
    void close() noexcept;

    /**
     * Check given SANE status returned by given operation.
     *
     * @param status
     * @param operation
     * @return true if operation was successful, false if operation should be repeated
     */
    bool checkStatus(SANE_Status status, const std::string & operation);

    /**
     * Log given message to syslog or stdout
     * @param message
     * @param verbosity
     */
    void log(const std::string & message, int verbosity) noexcept;

    /**
     * @param opt
     * @return true iff opt points to a sensor option
     */
    bool is_sensor_option(const SANE_Option_Descriptor * opt);

    /**
     * Fetch value of the given sensor option by its number (as reported by SANE)
     * @param opt_num
     * @return pair of (option name, value)
     */
    std::pair<std::string, bool> fetch_sensor_value(int opt_num);

    /**
     * Fetch and cache internal list of sensors (mSensors)
     */
    void fetch_sensors();

    /**
     * Execute event script, if it exists.
     *
     * @param name sensor name
     */
    void process_event(std::string name);

    /**
     * Signal handler
     * @param signum
     */
    static void sighandler(int signum);
};


#endif
