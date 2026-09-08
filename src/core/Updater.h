#pragma once
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace vdc {

// Looks for a newer release of the program on GitHub, downloads it and hands the swap of the program files to a
// script that runs once the program has closed. All of it runs on its own thread; Get() hands back a copy of the
// state for the interface.
class Updater {
public:
    enum class State { Idle, Checking, UpToDate, Available, Downloading, Extracting, Restarting, Failed };
    struct Release {
        std::string tag, version, date, notes, assetUrl, pageUrl;
        unsigned long long assetSize = 0;
        bool prerelease = false;
    };
    struct Status {
        State       state = State::Idle;
        Release     release;                // the newest release seen (Available: newer than this program)
        std::string error;                  // Failed: what went wrong
        double      downloadedMb = 0.0, totalMb = 0.0;
        bool        manual = false;         // the check was asked for by hand
        bool        download = false;       // the state belongs to a download rather than a check
        bool        writable = true;        // the program folder can be written to
        unsigned    generation = 0;         // counts state changes, so the interface announces each once
    };

    ~Updater();
    void   Check(const std::string& currentVersion, bool includePrerelease, bool manual);
    void   Download(const std::wstring& exeDir, const std::wstring& stagingDir);
    Status Get() const;
    bool   Busy() const;
    void   Cancel();

private:
    void SetState(State st, const std::string& error = std::string());
    void Join();
    bool RunCheck(const std::string& currentVersion, bool includePrerelease, Release& out, bool& newer, std::string& error);
    bool RunDownload(const std::wstring& exeDir, const std::wstring& stagingDir, std::string& error);

    mutable std::mutex m_mutex;
    Status             m_status;
    std::thread        m_thread;
    std::atomic<bool>  m_cancel{false};
    std::atomic<bool>  m_busy{false};
};

} // namespace vdc
