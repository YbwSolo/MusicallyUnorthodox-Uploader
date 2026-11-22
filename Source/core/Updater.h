#pragma once
#include "Settings.h"
#include <juce_events/juce_events.h>

namespace sd
{
struct UpdateInfo
{
    int build = 0;
    juce::String version, file, sha256, notes;
};

struct UpdateState
{
    enum class Phase { idle, checking, upToDate, available, downloading, installing, installed, failed };
    Phase phase = Phase::idle;
    UpdateInfo latest;
    float progress = 0.0f;
    juce::String message;
};

/** Looks for a newer build on the download page, fetches it, checks it arrived intact and swaps the installed
    plugin for it. The new version loads the next time the music software starts. Shared by every instance. */
class Updater : public juce::ChangeBroadcaster, private juce::Thread
{
public:
    explicit Updater (Settings&);
    ~Updater() override;

    static int currentBuild();
    static juce::String currentVersion();

    void checkNow();
    void autoCheck();            // at most once every six hours per computer
    void installNow();
    UpdateState getState() const;

    // Pure helpers, covered by the tests.
    static bool parseFeed (const juce::String& json, UpdateInfo& out);
    static bool replaceTree (const juce::File& source, const juce::File& destination, juce::String& error);
    static juce::String sha256Of (const juce::File&);

    /** Test hook: read the feed from a folder and install into scratch folders. */
    void configureForTesting (const juce::String& feedBaseUrl, const juce::File& auDestination, const juce::File& vst3Destination,
                              const juce::File& toolDestination, int pretendBuild);

private:
    enum class Job { none, check, install };
    void run() override;
    void doCheck();
    void doInstall();
    void set (UpdateState::Phase, const juce::String& message = {}, float progress = 0.0f);
    bool download (const juce::String& url, const juce::File& target, juce::String& error);
    juce::File destinationFor (const juce::String& folder, const juce::String& bundleName, const juce::File& testOverride) const;

    Settings& settings;
    mutable juce::CriticalSection lock;
    UpdateState state;
    std::atomic<int> job { 0 };
    juce::String baseUrl;
    juce::File testAU, testVST3, testTool;
    int buildOverride = -1;
};
}
