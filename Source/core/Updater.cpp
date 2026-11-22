#include "Updater.h"
#include <juce_cryptography/juce_cryptography.h>
#if ! JUCE_WINDOWS
 #include <unistd.h>
#endif

#ifndef MUU_BUILD_NUMBER
 #define MUU_BUILD_NUMBER 0
#endif
#ifndef MUU_VERSION_STRING
 #define MUU_VERSION_STRING "0.0.0"
#endif
#ifndef MUU_UPDATE_BASE_URL
 #define MUU_UPDATE_BASE_URL ""
#endif

namespace sd
{
static const char* const kBundleName = "MU Uploader";

Updater::Updater (Settings& s) : juce::Thread ("MU Uploader updater"), settings (s), baseUrl (MUU_UPDATE_BASE_URL) {}
Updater::~Updater() { stopThread (6000); }

int Updater::currentBuild()             { return MUU_BUILD_NUMBER; }
juce::String Updater::currentVersion()  { return MUU_VERSION_STRING; }

UpdateState Updater::getState() const
{
    const juce::ScopedLock sl (lock);
    return state;
}

void Updater::set (UpdateState::Phase phase, const juce::String& message, float progress)
{
    {
        const juce::ScopedLock sl (lock);
        state.phase = phase;
        state.message = message;
        state.progress = progress;
    }
    sendChangeMessage();
}

void Updater::configureForTesting (const juce::String& feed, const juce::File& au, const juce::File& vst3, const juce::File& tool, int pretendBuild)
{
    baseUrl = feed;
    testAU = au;
    testVST3 = vst3;
    testTool = tool;
    buildOverride = pretendBuild;
}

void Updater::checkNow()
{
    if (isThreadRunning()) return;
    job.store ((int) Job::check);
    set (UpdateState::Phase::checking, "Checking for updates...");
    startThread();
}

void Updater::autoCheck()
{
    if (currentBuild() <= 0 && buildOverride < 0) return;           // a build made by hand never nags
    if (getState().phase != UpdateState::Phase::idle) return;
    const double last = settings.getNumber ("lastUpdateCheckMs", 0.0);
    const double now = (double) juce::Time::currentTimeMillis();
    if (now - last < 6.0 * 3600.0 * 1000.0) return;
    checkNow();
}

void Updater::installNow()
{
    if (isThreadRunning() || getState().phase != UpdateState::Phase::available) return;
   #if JUCE_WINDOWS
    // On Windows the plugin lives in Program Files, which only an installer run by the person may write to, and the
    // music software has the plugin file open. So the new installer is fetched in the browser for them to run.
    if (buildOverride < 0)
    {
        juce::URL (baseUrl + "/MUUploader-Windows-Setup.exe").launchInDefaultBrowser();
        set (UpdateState::Phase::available, "The new installer is downloading in your browser. Close your music software, run it, then open your project again.");
        return;
    }
   #endif
    job.store ((int) Job::install);
    set (UpdateState::Phase::downloading, "Downloading the update...");
    startThread();
}

void Updater::run()
{
    const auto j = (Job) job.exchange ((int) Job::none);
    if (j == Job::check)        doCheck();
    else if (j == Job::install) doInstall();
}

bool Updater::parseFeed (const juce::String& json, UpdateInfo& out)
{
    const auto v = juce::JSON::parse (json);
    if (! v.isObject()) return false;
    out.build = (int) v.getProperty ("build", 0);
    out.version = v.getProperty ("version", "").toString();
    out.file = v.getProperty ("file", "").toString();
    out.sha256 = v.getProperty ("sha256", "").toString().toLowerCase();
    out.notes = v.getProperty ("notes", "").toString();
    // the file name is joined onto our own address, so it must be a plain name, never a path or another site
    const bool safeName = out.file.isNotEmpty() && ! out.file.startsWith (".")
                       && out.file.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-");
    return out.build > 0 && safeName && out.sha256.length() == 64;
}

void Updater::doCheck()
{
    int status = 0;
    juce::String text;
    {
        juce::URL url (baseUrl + "/latest.json");
        auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                 .withConnectionTimeoutMs (10000).withNumRedirectsToFollow (8).withStatusCode (&status));
        if (stream != nullptr) text = stream->readEntireStreamAsString();
    }
    juce::MessageManager::callAsync ([this] { settings.set ("lastUpdateCheckMs", juce::String ((double) juce::Time::currentTimeMillis(), 0)); });

    UpdateInfo info;
    if (text.isEmpty() || ! parseFeed (text, info))
    {
        set (UpdateState::Phase::failed, "Could not reach the update page. Check the internet connection and try again.");
        return;
    }

    const int mine = buildOverride >= 0 ? buildOverride : currentBuild();
    {
        const juce::ScopedLock sl (lock);
        state.latest = info;
    }
    if (info.build > mine) set (UpdateState::Phase::available, "Version " + info.version + " is ready to install.");
    else                   set (UpdateState::Phase::upToDate, "This is the newest version.");
}

bool Updater::download (const juce::String& address, const juce::File& target, juce::String& error)
{
    int status = 0;
    juce::URL url (address);
    auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                         .withConnectionTimeoutMs (15000).withNumRedirectsToFollow (8).withStatusCode (&status));
    if (in == nullptr || (status != 0 && status >= 400)) { error = "The update could not be downloaded."; return false; }

    target.deleteFile();
    juce::FileOutputStream out (target);
    if (! out.openedOk()) { error = "No space to save the download."; return false; }

    const auto total = in->getTotalLength();
    juce::HeapBlock<char> buffer (1 << 16);
    juce::int64 done = 0;
    while (! in->isExhausted())
    {
        if (threadShouldExit()) { error = "Cancelled."; return false; }
        const int n = in->read (buffer, 1 << 16);
        if (n <= 0) break;
        if (! out.write (buffer, (size_t) n)) { error = "The download could not be saved."; return false; }
        done += n;
        set (UpdateState::Phase::downloading, "Downloading the update...", total > 0 ? (float) ((double) done / (double) total) : 0.0f);
    }
    out.flush();
    return done > 0;
}

juce::String Updater::sha256Of (const juce::File& f)
{
    juce::FileInputStream in (f);
    if (! in.openedOk()) return {};
    return juce::SHA256 (in).toHexString().toLowerCase();
}

// Copies every file of the new bundle next to its old counterpart and renames it into place. A plugin that is
// loaded keeps using the old file it already has open, so this is safe while the music software is running.
bool Updater::replaceTree (const juce::File& source, const juce::File& destination, juce::String& error)
{
    if (! source.isDirectory()) { error = "The update is incomplete."; return false; }
    if (! destination.createDirectory()) { error = "No permission to write to " + destination.getFullPathName(); return false; }

    juce::StringArray kept;
    for (const auto& entry : juce::RangedDirectoryIterator (source, true, "*", juce::File::findFilesAndDirectories))
    {
        const auto from = entry.getFile();
        const auto relative = from.getRelativePathFrom (source);
        const auto to = destination.getChildFile (relative);
        if (from.isDirectory())
        {
            if (! to.createDirectory()) { error = "No permission to write to " + to.getFullPathName(); return false; }
            continue;
        }

        kept.add (relative);
        const auto staged = to.getSiblingFile (to.getFileName() + ".muu-new");
        staged.deleteFile();
        if (! from.copyFileTo (staged)) { error = "No permission to write to " + to.getParentDirectory().getFullPathName(); return false; }
       #if ! JUCE_WINDOWS
        if (::access (from.getFullPathName().toRawUTF8(), X_OK) == 0) staged.setExecutePermission (true);
       #endif
        if (! staged.moveFileTo (to)) { staged.deleteFile(); error = "Could not replace " + to.getFileName(); return false; }
    }

    // anything the new version no longer ships
    for (const auto& entry : juce::RangedDirectoryIterator (destination, true, "*", juce::File::findFiles))
        if (! kept.contains (entry.getFile().getRelativePathFrom (destination)))
            entry.getFile().deleteFile();
    return true;
}

juce::File Updater::destinationFor (const juce::String& folder, const juce::String& bundle, const juce::File& testOverride) const
{
    if (testOverride != juce::File()) return testOverride;
    auto writable = [] (const juce::File& b) { return b.isDirectory() && b.getChildFile ("Contents").hasWriteAccess() && b.getChildFile ("Contents/MacOS").hasWriteAccess(); };

    // 1. the copy that is running right now, if it is this kind of bundle and we may write to it
    auto running = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getParentDirectory().getParentDirectory();
    if (running.getFileName() == bundle && writable (running)) return running;

    // 2. the shared location the installer uses, 3. the personal one
    const juce::File shared ("/Library/Audio/Plug-Ins/" + folder + "/" + bundle);
    const auto personal = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins/" + folder + "/" + bundle);
    if (writable (shared)) return shared;
    if (personal.isDirectory()) return personal;
    return {};          // this format is not installed where we may write, so leave it alone
}

void Updater::doInstall()
{
    const auto info = getState().latest;
    auto work = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("muu-update-" + juce::String (info.build));
    work.deleteRecursively();
    work.createDirectory();
    const auto zip = work.getChildFile ("bundles.zip");
    juce::String error;

    if (! download (baseUrl + "/" + info.file, zip, error)) { set (UpdateState::Phase::failed, error); work.deleteRecursively(); return; }
    if (sha256Of (zip) != info.sha256)
    {
        set (UpdateState::Phase::failed, "The download was damaged on the way. Try again.");
        work.deleteRecursively();
        return;
    }

    set (UpdateState::Phase::installing, "Installing...", 1.0f);
    const auto unpacked = work.getChildFile ("unpacked");
    unpacked.createDirectory();
    juce::ChildProcess unzip;
   #if JUCE_MAC
    const juce::StringArray command { "/usr/bin/ditto", "-x", "-k", zip.getFullPathName(), unpacked.getFullPathName() };
   #else
    const juce::StringArray command { "unzip", "-q", "-o", zip.getFullPathName(), "-d", unpacked.getFullPathName() };
   #endif
    if (! unzip.start (command) || ! unzip.waitForProcessToFinish (120000) || unzip.getExitCode() != 0)
    {
        set (UpdateState::Phase::failed, "The update could not be unpacked.");
        work.deleteRecursively();
        return;
    }

    struct Format { const char* folder; const char* extension; juce::File test; };
    const Format formats[] = { { "Components", ".component", testAU }, { "VST3", ".vst3", testVST3 } };
    int installed = 0;
    for (auto& f : formats)
    {
        const auto bundle = juce::String (kBundleName) + f.extension;
        const auto source = unpacked.getChildFile (f.folder).getChildFile (bundle);
        if (! source.isDirectory()) continue;
        const auto destination = destinationFor (f.folder, bundle, f.test);
        if (destination == juce::File()) continue;
        if (! replaceTree (source, destination, error))
        {
            set (UpdateState::Phase::failed, error + ". Use the installer from the download page instead.");
            work.deleteRecursively();
            return;
        }
        ++installed;
    }

    // the video tool travels with the plugin, so a new look for the videos arrives with the update
    const auto tool = unpacked.getChildFile ("Support").getChildFile ("ffmpeg");
    if (installed > 0 && tool.existsAsFile())
    {
        const auto folder = testTool != juce::File() ? testTool : Settings::sharedDir();
        const auto target = folder.getChildFile ("ffmpeg");
        const auto staged = folder.getChildFile ("ffmpeg.muu-new");
        staged.deleteFile();
        if (folder.isDirectory() && folder.hasWriteAccess() && tool.copyFileTo (staged))
        {
            staged.setExecutePermission (true);
            if (! staged.moveFileTo (target)) staged.deleteFile();
        }
    }

    work.deleteRecursively();
    if (installed == 0)
    {
        set (UpdateState::Phase::failed, "This copy of the plugin cannot update itself. Use the installer from the download page once, and it will from then on.");
        return;
    }
    set (UpdateState::Phase::installed, "Version " + info.version + " is installed. Quit and reopen your music software to start using it.", 1.0f);
}
}
