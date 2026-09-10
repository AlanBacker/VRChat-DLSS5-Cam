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
        std::string assetName;              // the archive the release was looked up for (this edition's, or the other one's)
        bool edition = false;               // an edition switch: the other edition of this program, this version or newer
        bool downgrade = false;             // the stable channel's newest release is older than this program: the way back
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
    // otherEdition: look for the other edition of this program instead (its archive, this version or newer).
    void   Check(const std::string& currentVersion, bool includePrerelease, bool manual, bool otherEdition = false);
    void   Download(const std::wstring& exeDir, const std::wstring& stagingDir);
    Status Get() const;
    bool   Busy() const;
    void   Cancel();

private:
    void SetState(State st, const std::string& error = std::string());
    void Join();
    bool RunCheck(const std::string& currentVersion, bool includePrerelease, bool otherEdition, Release& out, bool& newer, std::string& error);
    bool RunDownload(const std::wstring& exeDir, const std::wstring& stagingDir, std::string& error);

    mutable std::mutex m_mutex;
    Status             m_status;
    std::thread        m_thread;
    std::atomic<bool>  m_cancel{false};
    std::atomic<bool>  m_busy{false};
};

// The DLSS-NR-on-AMD installer (Radeon edition): a separate program under its own terms, nothing of it is part of
// this project. On request its installer is fetched from that project's latest GitHub release into the program
// folder and started there; it finds the executable and nvngx_dlssnr.dll next to it and installs its DLL. Runs on
// its own thread; Get() hands back a copy of the state.
class PortSetup {
public:
    enum class State { Idle, Checking, Ready, Downloading, Installing, Launched, Finished, Failed };
    struct Status {
        State       state = State::Idle;
        std::string tag, date, pageUrl, assetUrl;   // the latest release, once looked up
        unsigned long long assetSize = 0;
        std::string error;                          // Failed: what went wrong
        double      downloadedMb = 0.0, totalMb = 0.0;
        std::wstring setupPath;                     // the installer file, once downloaded
        unsigned long exitCode = 0;                 // Finished
        unsigned    generation = 0;                 // counts state changes, so the interface announces each once
    };
    static constexpr const char* kPageUrl = "https://github.com/danielblnc/DLSS-NR-on-AMD/releases/latest";
    static constexpr const char* kLicenseUrl = "https://github.com/danielblnc/DLSS-NR-on-AMD/blob/master/LICENSE";
    // Its files next to the executable: the weights its installer makes from nvngx_dlssnr.dll, the installer, its log.
    static constexpr const wchar_t* kWeightsFile = L"dlssnr_on_amd_weights.bin";
    static constexpr const wchar_t* kSetupFile   = L"dlssnr_on_amd_setup.exe";
    static constexpr const wchar_t* kLogFile     = L"dlssnr_on_amd.log";
    static constexpr const wchar_t* kIniFile     = L"dlssnr_on_amd.ini";
    // Adjusts the port's settings file for frames that are saved rather than shown (see the definition). Returns
    // true when a value was changed; `changes` names them. The port reads the file when it loads, so a change made
    // at start-up takes effect on the next start.
    static bool TuneIni(const std::wstring& exeDir, std::string& changes);

    ~PortSetup();
    void   Check();                                 // look up the latest release (tag, date, installer)
    void   Install(const std::wstring& exeDir);     // download the installer next to the executable and start it
    Status Get() const;
    bool   Busy() const;
    void   Cancel();

private:
    void SetState(State st, const std::string& error = std::string());
    void Join();
    bool RunCheck(std::string& error);
    bool RunInstall(const std::wstring& exeDir, std::string& error);
    // Runs the setup without a window, feeding it the answers and capturing its console output, up to a
    // timeout (it is killed if the application is closing). Returns false when it could not run at all.
    bool RunSetupHidden(const std::wstring& path, const std::wstring& exeDir, const std::string& answers,
                        unsigned timeoutMs, std::string& captured, unsigned long& exitCode, std::string& error);

    mutable std::mutex m_mutex;
    Status             m_status;
    std::thread        m_thread;
    std::atomic<bool>  m_cancel{false};
    std::atomic<bool>  m_busy{false};
};

} // namespace vdc
