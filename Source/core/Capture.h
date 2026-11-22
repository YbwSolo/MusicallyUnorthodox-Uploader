#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

namespace sd
{
/** Catches the beat as it passes through the master. Three ways in, all ending with the same finished WAV:
      heard     the host plays the beat and the plugin keeps what it hears, with nothing to press
      exported  the host renders the beat (faster than real time) and the render is kept
      recorded  the Record button was pressed, for hosts that do not say when they are playing
    The audio thread only pushes samples into a lock-free queue; a background thread writes the file. */
class Capture : private juce::Timer
{
public:
    enum class State { idle, armed, recording };
    enum class Kind { heard, exported, recorded };

    Capture();
    ~Capture() override;

    void prepare (double sampleRate, int numChannels);
    void release();

    /** Audio thread. hostSample is the host's position on its timeline, or -1 when it does not say. */
    void process (const juce::AudioBuffer<float>& buffer, bool hostIsPlaying, bool hostHasTransport, bool offlineRender, juce::int64 hostSample = -1);

    void arm();                 // record the next play-through from the top
    void stop();                // finish now (keeps what was caught) or disarm
    void setCatchExports (bool shouldCatch)  { catchExports.store (shouldCatch); }
    void setListen (bool shouldListen)       { listen.store (shouldListen); }

    State state() const         { return currentState.load(); }
    Kind kind() const           { return currentKind.load(); }
    double secondsRecorded() const;
    float level() const         { return peak.load(); }

    /** Called on the message thread with the finished WAV, its length in seconds, and how it was caught.
        Silence before the first sound and after the last is already trimmed off, except for an export. */
    std::function<void (juce::File, double, Kind)> onCaptured;

    /** A take the plugin started on its own has to be at least this long to count as a beat. */
    static constexpr double shortestHeardTake = 20.0;
    static constexpr double shortestOtherTake = 8.0;

    static double trimSilence (const juce::File& wav);     // returns the new length in seconds

private:
    void timerCallback() override;
    void openWriter();
    void finish();

    juce::TimeSliceThread writerThread { "MU Uploader capture" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    juce::SpinLock writerLock;
    juce::File currentFile;

    std::atomic<State> currentState { State::idle };
    std::atomic<Kind> currentKind { Kind::heard };
    std::atomic<bool> catchExports { true }, listen { true }, sawPlaying { false }, takeEnded { false };
    juce::CriticalSection lifecycle;
    std::atomic<juce::int64> samplesWritten { 0 }, lastHostSample { -1 };
    std::atomic<juce::uint32> lastBlockMs { 0 };
    std::atomic<float> peak { 0.0f };
    double rate = 44100.0;
    int channels = 2;
    bool prepared = false;

    std::shared_ptr<std::atomic<bool>> alive { std::make_shared<std::atomic<bool>> (true) };
    juce::ThreadPool finishing { 1 };
};
}
