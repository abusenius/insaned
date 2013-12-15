
#include "InsaneDaemon.h"
#include "InsaneException.h"

#include <iostream>
#include <sstream>
#include <cstdlib>
#include <stdexcept>
#include <csignal>
#include <unistd.h>
#include <sys/types.h>


const std::string InsaneDaemon::NAME = "insaned";

InsaneDaemon InsaneDaemon::mInstance;


InsaneDaemon & InsaneDaemon::instance() noexcept
{
    return InsaneDaemon::mInstance;
}


InsaneDaemon::InsaneDaemon()
{
    log("Initializing...", 1);
    checkStatus(sane_init(&mVersionCode, NULL), "sane_init");
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
        mHandle = NULL;
        log("Calling sane_exit", 1);
        sane_exit();

        log("Finished", 1);
    } catch (...) {
        log("Error calling sane_exit!", 0);
    }
}


void InsaneDaemon::init(bool verbose) noexcept
{
    mVerbose = verbose;
}


void InsaneDaemon::open(std::string device_name)
{
    close();

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
    log("Opening device '" + device_name + "'", 1);
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
}


void InsaneDaemon::close() noexcept
{
    try {
        if (mHandle)
        {
            log("Closing device '" + mCurrentDevice + "'", 1);
            sane_close(mHandle);
        }
    } catch (...) {
        log("Error closing device!", 0);
    }
    mHandle = nullptr;
}


void InsaneDaemon::run()
{
    log("Running...", 1);
    fetch_options();
    print_options();
}


std::string InsaneDaemon::get_sane_version() noexcept
{
    std::stringstream s;
    s << SANE_VERSION_MAJOR(mVersionCode) << "." << SANE_VERSION_MINOR(mVersionCode) << "." << SANE_VERSION_BUILD(mVersionCode);
    return s.str();
}


const std::vector<std::string> InsaneDaemon::get_devices()
{
    if (mDevices.empty()) {
        log("Fetching device list...", 1);
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
    }
    return mDevices;
}


bool InsaneDaemon::checkStatus(SANE_Status status, const std::string & operation)
{
    if (status == SANE_STATUS_DEVICE_BUSY) {
        log(operation + " returned status DEVICE BUSY", 1);
        return false;
    } else if (status == SANE_STATUS_GOOD) {
        return true;
    }
    log(operation + " failed: " + std::string(sane_strstatus(status)), 1);
    // TODO throw?
    return false;
}


void InsaneDaemon::log(const std::string & message, int verbosity) noexcept
{
    try {
        if (mVerbose >= verbosity) {
            // TODO syslog
            std::cerr << InsaneDaemon::NAME << ": " << message << std::endl;
        }
    } catch (...) {
        // try to log on stderr
        try {
            std::cerr << InsaneDaemon::NAME << ": " << message << std::endl;
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


void InsaneDaemon::fetch_options()
{
    const SANE_Option_Descriptor * opt = sane_get_option_descriptor(mHandle, 0);
    if (opt == NULL) {
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
        if (opt == NULL) {
            std::stringstream s;
            s << "Could not get option descriptor for option " << i;
            log(s.str(), 0);
            throw InsaneException("Could not fetch device options");
        }

        if (is_sensor_option(opt)) {
            mSensors[std::string(opt->name)] = i;
        }
    }
}


void InsaneDaemon::print_option(int opt_num)
{
    const SANE_Option_Descriptor * opt = sane_get_option_descriptor(mHandle, opt_num);

    if (!opt || opt->type == SANE_TYPE_GROUP) {
        return;
    }

    /* print the sensor option */
    if (is_sensor_option(opt)) {
        /* name */
        std::cout << "    " << opt->name << " ";
        /* print current option value */
        if (opt->size == sizeof (SANE_Word)) {
            SANE_Word val;
            if (!checkStatus(sane_control_option(mHandle, opt_num, SANE_ACTION_GET_VALUE, &val, 0),
                             "Fetching value of option " + std::string(opt->name))) {
                throw InsaneException("Could not fetch value of option " + std::string(opt->name));
            }
            if (opt->type == SANE_TYPE_BOOL) {
                std::cout << (*reinterpret_cast<SANE_Bool *>(&val) ? "[yes]" : "[no]");
            }
        } else {
            std::cout << "[unsupported size: " << opt->size << "]";
        }
        std::cout << '\n';
    } else {
        std::cout << "    " << opt->name << " [not a sensor option]\n";
    }
}


void InsaneDaemon::print_options()
{
    std::cout << "Sensors of device '" << mCurrentDevice << "':\n";
    for (auto & entry : mSensors) {
        print_option(entry.second);
    }
}


void InsaneDaemon::sighandler(int signum)
{
    static bool first_time = true;

    if (InsaneDaemon::instance().mHandle) {
        std::stringstream s;
        s << "Received signal " << signum;
        InsaneDaemon::instance().log(s.str(), 1);
        if (first_time) {
            first_time = false;
            InsaneDaemon::instance().log("Trying to stop scanner", 1);
            sane_cancel(InsaneDaemon::instance().mHandle);
        } else {
            InsaneDaemon::instance().log("Aborting", 1);
            std::exit(2);
        }
    }
}
