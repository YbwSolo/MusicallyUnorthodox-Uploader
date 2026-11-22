#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

namespace sd
{
/** Records what passes through the master while the host exports the beat (or while it plays, when armed).
    The audio thread only pushes samples into a lock-free queue; a background thread writes the WAV. */
class Capture : private juce::Timer
{
public:
    enum class State { idle, armed, recording };

    Capture();
    ~Capture() override;

    void prepare (double sampleRate, int numChannels);
    void release();

    /** Audio thread. */
    void process (const juce::AudioBuffer<float>& buffer, bool hostIsPlaying, bool hostHasTransport, bool offlineRender);

    void arm();                 // record the next play-through from the top
    void stop();                // finish now (keeps what was recorded) or disarm
    void setCatchExports (bool shouldCatch)  { catchExports.store (shouldCatch); }

    State state() const         { return currentState.load(); }
    double secondsRecorded() const;
    float level() const         { return peak.load(); }

    /** Called on the message thread with the finished WAV and its length in seconds. */
    std::function<void (juce::File, double)> onCaptured;

private:
    void timerCallback() override;
    void openWriter();
    void finish();

    juce::TimeSliceThread writerThread { "MU Uploader capture" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    juce::SpinLock writerLock;
    juce::File currentFile;

    std::atomic<State> currentState { State::idle };
    std::atomic<bool> catchExports { true }, startedByExport { false }, sawPlaying { false }, transportStopped { false };
    juce::CriticalSection lifecycle;
    std::atomic<juce::int64> samplesWritten { 0 };
    std::atomic<juce::uint32> lastBlockMs { 0 };
    std::atomic<float> peak { 0.0f };
    double rate = 44100.0;
    int channels = 2;
    bool prepared = false;
};
}
