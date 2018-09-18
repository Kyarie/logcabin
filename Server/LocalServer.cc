#include <getopt.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <thread>

#include "Core/Debug.h"
#include "Core/StringUtil.h"
#include "Core/ThreadId.h"
#include "Core/Util.h"
#include "Server/Globals.h"
#include "Server/RaftConsensus.h"

namespace {

/**
 * RAII-style class to manage a file containing the process ID.
 */
class PidFile {
  public:
    explicit PidFile(const std::string& filename)
        : filename(filename)
        , written(-1)
    {
    }

    ~PidFile() {
        removeFile();
    }

    void writePid(int pid) {
        if (filename.empty())
            return;
        FILE* file = fopen(filename.c_str(), "w");
        if (file == NULL) {
            PANIC("Could not open %s for writing process ID: %s",
                  filename.c_str(),
                  strerror(errno));
        }
        std::string pidString =
            LogCabin::Core::StringUtil::format("%d\n", pid);
        size_t bytesWritten =
            fwrite(pidString.c_str(), 1, pidString.size(), file);
        if (bytesWritten != pidString.size()) {
            PANIC("Could not write process ID %s to pidfile %s: %s",
                  pidString.c_str(), filename.c_str(),
                  strerror(errno));
        }
        int r = fclose(file);
        if (r != 0) {
            PANIC("Could not close pidfile %s: %s",
                  filename.c_str(),
                  strerror(errno));
        }
        NOTICE("Wrote PID %d to %s",
               pid, filename.c_str());
        written = pid;
    }

    void removeFile() {
        if (written < 0)
            return;
        FILE* file = fopen(filename.c_str(), "r");
        if (file == NULL) {
            WARNING("Could not open %s for reading process ID prior to "
                    "removal: %s",
                    filename.c_str(),
                    strerror(errno));
            return;
        }
        char readbuf[10];
        memset(readbuf, 0, sizeof(readbuf));
        size_t bytesRead = fread(readbuf, 1, sizeof(readbuf), file);
        if (bytesRead == 0) {
            WARNING("PID could not be read from pidfile: "
                    "will not remove file %s",
                    filename.c_str());
            fclose(file);
            return;
        }
        int pidRead = atoi(readbuf);
        if (pidRead != written) {
            WARNING("PID read from pidfile (%d) does not match PID written "
                    "earlier (%d): will not remove file %s",
                    pidRead, written, filename.c_str());
            fclose(file);
            return;
        }
        int r = unlink(filename.c_str());
        if (r != 0) {
            WARNING("Could not unlink %s: %s",
                    filename.c_str(), strerror(errno));
            fclose(file);
            return;
        }
        written = -1;
        fclose(file);
        NOTICE("Removed pidfile %s", filename.c_str());
    }

    std::string filename;
    int written;
};

} // anonymous namespace

class LocalServer {
    public: 

    LocalServer() { }

    void init(std::shared_ptr<LogCabin::Server::Globals> globals, std::string configFilename, bool bootstrap) {
        using namespace LogCabin;

        try {
            Core::ThreadId::setName("evloop");
            std::string pidFilename;

            NOTICE("Using config file %s", configFilename.c_str());

            // Write PID file, removed upon destruction
            PidFile pidFile(pidFilename);
            pidFile.writePid(getpid());
            //Server::Globals * globals = new Server::Globals();
            // Initialize and run Globals.
            globals->config.readFile(configFilename.c_str());

            // Set debug log policy.
            // A few log messages above already got through; oh well.
            Core::Debug::setLogPolicy(
                Core::Debug::logPolicyFromString(
                    globals->config.read<std::string>("logPolicy", "NOTICE")));

            NOTICE("Config file settings:\n"
                   "# begin config\n"
                   "%s"
                   "# end config",
                   Core::StringUtil::toString(globals->config).c_str());
            globals->init();
            if (bootstrap) {
                globals->raft->bootstrapConfiguration();
                NOTICE("Done bootstrapping configuration. Exiting.");
            } else {
                globals->leaveSignalsBlocked();                
            }

            //google::protobuf::ShutdownProtobufLibrary();
            //return globals;

        } catch (const Core::Config::Exception& e) {
            ERROR("Fatal exception from config file: %s",
                  e.what());
        }
    }
};
