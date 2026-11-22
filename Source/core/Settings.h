#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace sd
{
/** Everything the plugin remembers between sessions, shared by every instance in the host. */
class Settings
{
public:
    Settings();

    juce::String get (const juce::String& key, const juce::String& fallback = {}) const;
    double getNumber (const juce::String& key, double fallback) const;
    void set (const juce::String& key, const juce::String& value);
    void remove (const juce::String& key);

    static juce::File appDataDir();     // per-user settings, sign-in, contacts
    static juce::File libraryDir();     // the beats themselves: Music/MU Uploader
    static juce::File sharedDir();      // what the installer puts down for every user (ffmpeg)

private:
    std::unique_ptr<juce::PropertiesFile> props;
};
}
