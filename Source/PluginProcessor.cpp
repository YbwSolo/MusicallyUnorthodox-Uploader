#include "PluginProcessor.h"
#include "PluginEditor.h"

UploaderProcessor::UploaderProcessor()
    : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                             .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    newDraft();
    capture.setCatchExports (services().settings.get ("catchExports", "1") == "1");
    capture.setListen (services().settings.get ("listen", "1") == "1");
    capture.onCaptured = [this] (juce::File file, double seconds, sd::Capture::Kind kind) { captured (file, seconds, kind); };
}

UploaderProcessor::~UploaderProcessor() { capture.onCaptured = nullptr; }

void UploaderProcessor::newDraft()
{
    draft = sd::Draft();
    draftWasHeard = false;
    auto& s = services().settings;
    draft.beat.price = s.getNumber ("defaultPrice", 20.0);
    draft.beat.licence = s.get ("defaultLicence", "ffp");
    draft.toBeatStars = s.get ("defaultBeatStars", "0") == "1";
    draft.toShort = s.get ("defaultShort", "0") == "1";
    draft.publishAt = juce::Time::getCurrentTime() + juce::RelativeTime::days (1);
}

static bool isOurTake (const juce::File& file)
{
    return file.existsAsFile() && file.isAChildOf (sd::Settings::libraryDir().getChildFile ("Captures"));
}

void UploaderProcessor::adopt (juce::File file, double seconds, bool heard)
{
    // a new take replaces the audio of a beat that has not been saved yet; a saved beat is left alone
    if (draft.beat.id.isNotEmpty())
        newDraft();
    else if (isOurTake (draft.audio) && draft.audio != file)
        draft.audio.deleteFile();

    draft.audio = file;
    draft.beat.seconds = seconds;
    draftWasHeard = heard;
    if (draft.beat.bpm <= 0.0 && bpm.load() > 0.0)
        draft.beat.bpm = std::round (bpm.load());
}

void UploaderProcessor::captured (juce::File file, double seconds, sd::Capture::Kind kind)
{
    // Exporting or pressing Record is asking for this take, so it always goes straight onto the page.
    // A take that was only heard is a guess, and it never pushes aside something chosen on purpose.
    if (kind != sd::Capture::Kind::heard)
    {
        if (isOurTake (waitingTake.file)) waitingTake.file.deleteFile();
        waitingTake = {};
        adopt (file, seconds, false);
    }
    else if (! draft.audio.existsAsFile())
    {
        adopt (file, seconds, true);
    }
    else if (draft.beat.id.isEmpty() && draftWasHeard)
    {
        // playing a short section does not replace a full play-through; a fuller or newer full pass does
        if (seconds >= draft.beat.seconds * 0.75) adopt (file, seconds, true);
        else                                      file.deleteFile();
    }
    else if (! waitingTake.file.existsAsFile() || seconds >= waitingTake.seconds * 0.75)
    {
        if (isOurTake (waitingTake.file)) waitingTake.file.deleteFile();
        waitingTake = { file, seconds };
    }
    else
    {
        file.deleteFile();
    }

    draftChanged();
}

void UploaderProcessor::useWaitingTake()
{
    if (! waitingTake.file.existsAsFile()) return;
    const auto take = waitingTake;
    waitingTake = {};
    adopt (take.file, take.seconds, true);
    draftChanged();
}

void UploaderProcessor::prepareToPlay (double sampleRate, int)
{
    capture.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
}

void UploaderProcessor::releaseResources() { capture.release(); }

bool UploaderProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono()) && layouts.getMainInputChannelSet() == out;
}

void UploaderProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    bool playing = false, hasTransport = false;
    juce::int64 hostSample = -1;

    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
        {
            hasTransport = true;
            playing = position->getIsPlaying();
            if (auto tempo = position->getBpm()) bpm.store (*tempo);
            if (auto at = position->getTimeInSamples()) hostSample = *at;
        }

    capture.process (buffer, playing, hasTransport, isNonRealtime(), hostSample);
}

juce::AudioProcessorEditor* UploaderProcessor::createEditor() { return new UploaderEditor (*this); }

void UploaderProcessor::getStateInformation (juce::MemoryBlock& data)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("beat", draft.beat.toVar());
    o->setProperty ("audio", draft.audio.getFullPathName());
    o->setProperty ("cover", draft.cover.getFullPathName());
    o->setProperty ("privacy", draft.privacy);
    o->setProperty ("youtube", draft.toYouTube);
    o->setProperty ("beatstars", draft.toBeatStars);
    o->setProperty ("short", draft.toShort);
    const auto text = juce::JSON::toString (juce::var (o), true);
    data.replaceAll (text.toRawUTF8(), text.getNumBytesAsUTF8());
}

void UploaderProcessor::setStateInformation (const void* data, int size)
{
    const auto v = juce::JSON::parse (juce::String::fromUTF8 ((const char*) data, size));
    if (! v.isObject()) return;

    draft.beat = sd::Beat::fromVar (v["beat"]);
    auto asFile = [] (const juce::String& path) { return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File(); };
    draft.audio = asFile (v["audio"].toString());
    draft.cover = asFile (v["cover"].toString());
    draft.privacy = v.getProperty ("privacy", "public").toString();
    draft.toYouTube = (bool) v.getProperty ("youtube", true);
    draft.toBeatStars = (bool) v.getProperty ("beatstars", false);
    draft.toShort = (bool) v.getProperty ("short", false);
    draftWasHeard = false;

    // the project may be older than the library: pick up the saved version of this beat if there is one
    if (auto* saved = services().library.find (draft.beat.id))
        draft.beat = *saved;
    else
        draft.beat.id.clear();

    sendChangeMessage();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new UploaderProcessor(); }
