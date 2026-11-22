#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "core/Capture.h"
#include "core/Publisher.h"

/** Sits last on the master. It never changes the sound: audio passes straight through while a copy is caught for upload. */
class UploaderProcessor : public juce::AudioProcessor, public juce::ChangeBroadcaster
{
public:
    UploaderProcessor();
    ~UploaderProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }
    const juce::String getName() const override             { return "MU Uploader"; }
    bool acceptsMidi() const override                       { return false; }
    bool producesMidi() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 0.0; }
    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    sd::Services& services()                                { return *shared; }
    sd::Capture capture;
    sd::Draft draft;

    void newDraft();
    void draftChanged()                                     { sendChangeMessage(); }
    double hostBpm() const                                  { return bpm.load(); }

private:
    void captured (juce::File, double seconds);

    juce::SharedResourcePointer<sd::Services> shared;
    std::atomic<double> bpm { 0.0 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UploaderProcessor)
};
