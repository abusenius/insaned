
#include "InsaneDaemon.h"
#include "InsaneException.h"

#include <cassert>
#include <iostream>
#include <cstdlib>
#include <stdexcept>
#include <csignal>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <cerrno>
#include <cstring>

#include "Timer.h"


const std::string InsaneDaemon::NAME = "insaned";

const int InsaneDaemon::SKIP_TIMEOUT_MS = 2500;
const int InsaneDaemon::BUSY_TIMEOUT_MS = 15000;

InsaneDaemon InsaneDaemon::mInstance;


InsaneDaemon & InsaneDaemon::instance() noexcept
{
    return InsaneDaemon::mInstance;
}


InsaneDaemon::InsaneDaemon()
{
    log("Initializing...", 1);
    Timer t;
    if (!checkStatus(sane_init(&mVersionCode, nullptr), "sane_init")) {
        log("error, failed to initialize SANE library!", 0);
    }
    log("timer: sane_init: " + std::to_string(t.restart()) + " ms", 2);

#ifdef SIGHUP
    signal (SIGHUP, InsaneDaemon::sighandler);
#endif
#ifdef SIGPIPE
    signal (SIGPIPE, InsaneDaemon::sighandler);
#endif
    signal (SIGINT, InsaneDaemon::sighandler);
    signal (SIGTERM, InsaneDaemon::sighandler);
}


InsaneDaemon::~InsaneDaemon() noexcept
{
    log("Exiting...", 1);
    close();
    try {
        mHandle = nullptr;
        log("Calling sane_exit", 1);
        sane_exit();

        ::close(0);
        ::close(1);
        ::close(2);

        log("Finished", 1);
    } catch (...) {
        log("Error calling sane_exit!", 0);
    }
}


void InsaneDaemon::init(std::string device_name, std::string events_dir, int sleep_ms, int verbose, bool log_to_syslog, bool suspend_after_event)
{
    mCurrentDevice = device_name;
    mEventsDir = events_dir;
    mSleepMs = sleep_ms;
    if (mSleepMs <= 1) {
        throw std::out_of_range("Value of sleep ms is out of range");
    }
    mVerbose = verbose;
    mLogToSyslog = log_to_syslog;
    mSuspendAfterEvent = suspend_after_event;
}


void InsaneDaemon::open(std::string device_name)
{
    close();

    Timer t;
    if (device_name.empty())
    {
        /* If no device name was specified explicitly, we look at the
           environment variable SANE_DEFAULT_DEVICE.  If this variable
           is not set, we open the first device we find (if any): */
        const char * defname = getenv("SANE_DEFAULT_DEVICE");
        if (defname != nullptr) {
            device_name = std::string(defname);
        } else {
            device_name = get_devices().at(0);
        }
    }
    log("Opening device '" + device_name + "'", 2);
    mCurrentDevice = device_name;

    if (!checkStatus(sane_open(device_name.c_str(), &mHandle), "opening device '" + device_name + "'")) {
        if (device_name[0] == '/') {
            std::cerr << "\nYou seem to have specified a UNIX device name, or filename instead of selecting\n"
                         "the SANE scanner or image acquisition device you want to use. As an example,\n"
                         "you might want \"epson:/dev/sg0\" or \"hp:/dev/usbscanner0\". If any supported\n"
                         "devices are installed in your system, you should be able to see a list with\n"
                         "\"scanimage --list-devices\"." << std::endl;
        }
        throw InsaneException("Failed to open device '" + device_name + "'");
    }
    log("timer: sane_open: " + std::to_string(t.restart()) + " ms", 2);
}


void InsaneDaemon::close() noexcept
{
    try {
        if (mHandle)
        {
            log("Closing device '" + mCurrentDevice + "'", 2);
            Timer t;
            sane_close(mHandle);
            log("timer: sane_close: " + std::to_string(t.restart()) + " ms", 2);

        }
    } catch (...) {
        log("Error closing device!", 0);
    }
    mHandle = nullptr;
}


void InsaneDaemon::run()
{
    mRun = true;
    {
        // try to open the device to select one if no device was given
        OpenGuard g(mCurrentDevice);
    }

    log("Starting polling sensors of " + mCurrentDevice + " every " + std::to_string(mSleepMs) + " ms", 1);
    while (mRun) {
        if (mSuspendCount <= 0) {
            // TODO skip reading sensors if
            // - some process (e.g. xsane, screensaver, screenlocker) is running
            // - some file (e.g. libsane) is opened by another process
            log("Reading sensors...", 2);
            try {
                auto sensors = get_sensors();
                for (auto & sensor : sensors) {
                    if (mRepeatCount.find(sensor.first) != mRepeatCount.end()) {
                        mRepeatCount[sensor.first]--;
                    }
                    if (sensor.second) {
                        process_event(sensor.first);
                    }
                }
            } catch (InsaneException & e) {
                log(e.what(), 1);
            }
        } else {
            log("Reading sensors is suspended: " + std::to_string(mSuspendCount) + " events left", 2);
            mSuspendCount--;
        }

        usleep(mSleepMs * 1000);
    }
}


void InsaneDaemon::process_event(std::string name)
{
    if (mRepeatCount.find(name) != mRepeatCount.end()) {
        int count = mRepeatCount[name];
        if (count > 0) {
            log("Skipping event '" + name + "', will wait for " + std::to_string(count) + " more periods", 2);
            return;
        }
    }
    mRepeatCount[name] = SKIP_TIMEOUT_MS / mSleepMs;

    log("Processing event '" + name + "'", 1);
    std::string handler = mEventsDir + "/" + name;
    struct stat f;
    if (stat(handler.c_str(), &f) < 0) {
        std::string err = strerror(errno);
        if (errno == ENOENT || errno == ENOTDIR) {
            log("script handler '" + handler + "' does not exist, please create an empty executable "
                "file to silence this warning, error: " + err, 0);
            return;
        }
        log("cannot stat event handler script '" + handler + "': " + err, 0);
        return;
    }
    if (S_ISREG((f.st_mode))) {
        if (!((f.st_mode & S_IXUSR) | (f.st_mode & S_IXGRP) | (f.st_mode & S_IXOTH))) {
            log("warning, script handler '" + handler + "' is not executable", 0);
            return;
        } else {
            if (f.st_size == 0) {
                // ignore
                return;
            }
        }
        log("calling event handler script '" + handler + "'", 2);
        if (system((handler + " " + mCurrentDevice).c_str()) < 0) {
            std::string err = strerror(errno);
            log("Failed to execute script handler '" + handler + "': " + err, 0);
            return;
        } else if (mSuspendAfterEvent) {
            mSuspendCount = BUSY_TIMEOUT_MS / mSleepMs;
        }
    } else {
        log("warning, script handler '" + handler + "' is not a regular file", 0);
        return;
    }
}


std::string InsaneDaemon::current_device() noexcept
{
    return mCurrentDevice;
}


std::string InsaneDaemon::get_sane_version() noexcept
{
    return std::to_string(SANE_VERSION_MAJOR(mVersionCode)) + "." + std::to_string(SANE_VERSION_MINOR(mVersionCode))
            + "." + std::to_string(SANE_VERSION_BUILD(mVersionCode));
}


const std::vector<std::string> InsaneDaemon::get_devices()
{
    if (mDevices.empty()) {
        log("Fetching device list...", 1);
        Timer t;
        const SANE_Device ** device_list;

        if (!checkStatus(sane_get_devices(&device_list, SANE_FALSE), "sane_get_devices")) {
            throw InsaneException("Could not list SANE devices");
        }
        if (!device_list[0]) {
            throw InsaneException("No SANE devices found");
        }
        for (int i = 0; device_list[i]; ++i) {
            mDevices.push_back(device_list[i]->name);
        }
        log("timer: sane_get_devices: " + std::to_string(t.restart()) + " ms", 2);
    }
    return mDevices;
}


std::vector<std::pair<std::string, bool> > InsaneDaemon::get_sensors()
{
    OpenGuard g(mCurrentDevice);

    if (mSensors.empty()) {
        fetch_sensors();
    }
    Timer t;
    std::vector<std::pair<std::string, bool> > result;
    for (auto & entry : mSensors) {
        result.push_back(fetch_sensor_value(entry.second));
    }
    log("timer: fetch all sensor values: " + std::to_string(t.restart()) + " ms", 2);
    return result;
}


bool InsaneDaemon::checkStatus(SANE_Status status, const std::string & operation)
{
    if (status == SANE_STATUS_DEVICE_BUSY) {
        log(operation + " returned status DEVICE BUSY", 1);
        mSuspendCount = BUSY_TIMEOUT_MS / mSleepMs;
        return false;
    } else if (status == SANE_STATUS_GOOD) {
        return true;
    }
    log(operation + " failed: " + std::string(sane_strstatus(status)), 0);
    // TODO throw?
    return false;
}


void InsaneDaemon::log(const std::string & message, int verbosity) noexcept
{
    try {
        if (mVerbose >= verbosity) {
            if (mLogToSyslog) {
                syslog((verbosity == 1 ? LOG_INFO : LOG_ERR) | LOG_USER, "%s: %s", InsaneDaemon::NAME.c_str(), message.c_str());
            } else {
                std::cerr << InsaneDaemon::NAME << ": " << message << std::endl;
            }
        }
    } catch (...) {
        // try to log on stderr if syslog failed somehow
        try {
            if (mLogToSyslog) {
                std::cerr << InsaneDaemon::NAME << ": " << message << std::endl;
            } else {
                // die
                std::abort();
            }
        } catch (...) {
            // nothing helps, die
            std::abort();
        }
    }
}


bool InsaneDaemon::is_sensor_option(const SANE_Option_Descriptor * opt)
{
    return opt && opt->name
        && opt->type == SANE_TYPE_BOOL
        && opt->cap & SANE_CAP_HARD_SELECT
        && !(opt->cap & SANE_CAP_SOFT_SELECT)
        && opt->cap & SANE_CAP_SOFT_DETECT
        && SANE_OPTION_IS_ACTIVE (opt->cap);
}


void InsaneDaemon::fetch_sensors()
{
    assert(mHandle);
    Timer t;
    const SANE_Option_Descriptor * opt = sane_get_option_descriptor(mHandle, 0);
    if (opt == nullptr) {
        log("Could not get option descriptor for option 0", 0);
        throw InsaneException("Could not fetch device options");
    }

    SANE_Int num_dev_options = 0;
    if (!checkStatus(sane_control_option(mHandle, 0, SANE_ACTION_GET_VALUE, &num_dev_options, 0), "Fetching value for option 0")) {
        throw InsaneException("Could not fetch device options");
    }

    /* build the table of sensors */
    for (int i = 1; i < num_dev_options; ++i)
    {
        opt = sane_get_option_descriptor(mHandle, i);
        if (opt == nullptr) {
            log("Could not get option descriptor for option " + std::to_string(i), 0);
            throw InsaneException("Could not fetch device options");
        }

        if (is_sensor_option(opt)) {
            mSensors[std::string(opt->name)] = i;
        }
    }
    log("timer: fetch_sensors: " + std::to_string(t.restart()) + " ms", 2);
}


std::pair<std::string, bool> InsaneDaemon::fetch_sensor_value(int opt_num)
{
    assert(mHandle);
    const SANE_Option_Descriptor * opt = sane_get_option_descriptor(mHandle, opt_num);

    if (!opt || opt->type == SANE_TYPE_GROUP) {
        throw InsaneException("Invalid option number: " + std::to_string(opt_num));
    }

    std::pair<std::string, bool> result;
    if (is_sensor_option(opt)) {
        /* name */
        result.first = opt->name;
        result.second = false;
        /* print current option value */
        if (opt->size == sizeof (SANE_Word)) {
            SANE_Word val;
            if (!checkStatus(sane_control_option(mHandle, opt_num, SANE_ACTION_GET_VALUE, &val, 0),
                             "Fetching value of option " + std::string(opt->name))) {
                throw InsaneException("Could not fetch value of option " + std::string(opt->name));
            }
            if (opt->type == SANE_TYPE_BOOL) {
                result.second = *reinterpret_cast<SANE_Bool *>(&val);
            }
        } else {
            throw InsaneException("Unsupported size: " + std::to_string(opt->size) + " of option " + std::string(opt->name));
        }
    } else {
        throw InsaneException("Could not fetch option " + std::string(opt->name) + ", it is not a sensor option");
    }
    return result;
}


void InsaneDaemon::sighandler(int signum)
{
    static bool first_time = true;
    InsaneDaemon & daemon = InsaneDaemon::instance();

    daemon.log("Received signal " + std::to_string(signum), 1);
    switch (signum) {
#ifdef SIGHUP
    case SIGHUP:
        daemon.mSensors.clear();
        daemon.mDevices.clear();
        break;
#endif
#ifdef SIGPIPE
    case SIGPIPE:
#endif
    case SIGINT:
    case SIGTERM:
        daemon.mRun = false;
        break;
    default:
        // do nothing
        break;
    }

    if (daemon.mHandle) {
        if (first_time) {
            first_time = false;
            daemon.log("Trying to stop scanner", 1);
            sane_cancel(daemon.mHandle);
        } else {
            daemon.log("Aborting", 1);
            std::exit(2);
        }
    }
}


InsaneDaemon::OpenGuard::OpenGuard(const std::string & device)
    : mDaemon(InsaneDaemon::instance()) {
    mDaemon.log("OPEN " + device, 3);
    mDaemon.open(device);
}

InsaneDaemon::OpenGuard::~OpenGuard() {
    mDaemon.log("CLOSE " + mDaemon.current_device(), 3);
    mDaemon.close();
}
