#include "Settings.h"

namespace sd
{
static const char* const kAppFolder = "MU Uploader";

juce::File Settings::appDataDir()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    auto dir = base.getChildFile (kAppFolder);
    dir.createDirectory();
    return dir;
}

juce::File Settings::libraryDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (kAppFolder);
    dir.createDirectory();
    return dir;
}

juce::File Settings::sharedDir()
{
    auto base = juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return base.getChildFile (kAppFolder);
}

Settings::Settings()
{
    juce::PropertiesFile::Options o;
    o.millisecondsBeforeSaving = 400;
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    props = std::make_unique<juce::PropertiesFile> (appDataDir().getChildFile ("settings.xml"), o);
}

juce::String Settings::get (const juce::String& key, const juce::String& fallback) const { return props->getValue (key, fallback); }
double Settings::getNumber (const juce::String& key, double fallback) const              { return props->getDoubleValue (key, fallback); }
void Settings::set (const juce::String& key, const juce::String& value)                  { props->setValue (key, value); props->saveIfNeeded(); }
void Settings::remove (const juce::String& key)                                          { props->removeValue (key); props->saveIfNeeded(); }
}
