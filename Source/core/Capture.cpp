#include "Capture.h"
#include "Settings.h"

namespace sd
{
Capture::Capture()
{
    writerThread.startThread (juce::Thread::Priority::normal);
    startTimerHz (8);
}

Capture::~Capture()
{
    stopTimer();
    release();
    writerThread.stopThread (2000);
}

void Capture::prepare (double sampleRate, int numChannels)
{
    // hosts call this again right before an export, often with a different sample rate
    const juce::ScopedLock sl (lifecycle);
    if (prepared && currentState.load() == State::recording)
        finish();

    rate = sampleRate;
    channels = juce::jlimit (1, 2, numChannels);
    prepared = true;
    openWriter();
}

void Capture::release()
{
    const juce::ScopedLock sl (lifecycle);
    if (currentState.load() == State::recording)
        finish();

    const juce::SpinLock::ScopedLockType lock (writerLock);
    writer.reset();
    prepared = false;
}

void Capture::openWriter()
{
    auto dir = Settings::libraryDir().getChildFile ("Captures");
    dir.createDirectory();
    auto file = dir.getNonexistentChildFile ("capture", ".wav", false);

    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> fresh;
    if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
    {
        juce::WavAudioFormat wav;
        if (auto* w = wav.createWriterFor (stream.get(), rate, (unsigned int) channels, 24, {}, 0))
        {
            stream.release();
            fresh = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w, writerThread, 1 << 17);
        }
    }

    juce::File stale;
    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        std::swap (writer, fresh);
        stale = currentFile;
        currentFile = file;
        samplesWritten.store (0);
    }

    fresh.reset();                                  // flushes and closes the previous file, off the audio thread
    if (stale.existsAsFile() && stale.getSize() < 4096)
        stale.deleteFile();                         // an unused, empty take
}

void Capture::process (const juce::AudioBuffer<float>& buffer, bool hostIsPlaying, bool hostHasTransport, bool offlineRender)
{
    lastBlockMs.store (juce::Time::getMillisecondCounter());
    auto s = currentState.load();

    if (s == State::idle && offlineRender && catchExports.load())
    {
        startedByExport.store (true);
        currentState.store (State::recording);
        s = State::recording;
    }
    else if (s == State::armed && (hostIsPlaying || ! hostHasTransport))
    {
        startedByExport.store (false);
        sawPlaying.store (hostIsPlaying);
        transportStopped.store (false);
        currentState.store (State::recording);
        s = State::recording;
    }

    if (s != State::recording)
        return;

    if (! startedByExport.load() && hostHasTransport && sawPlaying.load() && ! hostIsPlaying)
    {
        transportStopped.store (true);              // the timer finishes the take
        return;
    }

    const juce::SpinLock::ScopedTryLockType lock (writerLock);
    if (lock.isLocked() && writer != nullptr && buffer.getNumSamples() > 0)
    {
        // an export runs faster than real time, so there it is fine (and necessary) to wait for the disk
        for (int tries = 0; ! writer->write (buffer.getArrayOfReadPointers(), buffer.getNumSamples()); ++tries)
        {
            if (! offlineRender || tries > 4000) break;
            juce::Thread::sleep (1);
        }
        samplesWritten.fetch_add (buffer.getNumSamples());
        peak.store (juce::jmax (peak.load() * 0.9f, buffer.getMagnitude (0, buffer.getNumSamples())));
    }
}

void Capture::arm()
{
    const juce::ScopedLock sl (lifecycle);
    if (currentState.load() == State::idle)
    {
        if (samplesWritten.load() > 0 || writer == nullptr)
            openWriter();
        currentState.store (State::armed);
    }
}

void Capture::stop()
{
    const juce::ScopedLock sl (lifecycle);
    auto s = currentState.load();
    if (s == State::armed)          currentState.store (State::idle);
    else if (s == State::recording) finish();
}

double Capture::secondsRecorded() const { return rate > 0.0 ? (double) samplesWritten.load() / rate : 0.0; }

void Capture::timerCallback()
{
    peak.store (peak.load() * 0.8f);
    if (currentState.load() != State::recording)
        return;

    // an export has ended when the host stops sending audio; a play-through when the transport stops
    const auto quietMs = juce::Time::getMillisecondCounter() - lastBlockMs.load();
    const bool exportEnded = startedByExport.load() && quietMs > 700;
    const bool hostWentAway = quietMs > 3000;
    if (exportEnded || hostWentAway || transportStopped.exchange (false))
    {
        const juce::ScopedLock sl (lifecycle);
        if (currentState.load() == State::recording)
            finish();
    }
}

void Capture::finish()
{
    currentState.store (State::idle);
    const double seconds = secondsRecorded();
    auto file = currentFile;

    if (prepared) openWriter();                     // closes this take and gets the next one ready
    else
    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        writer.reset();
    }

    if (seconds < 8.0)                              // a blip, not a beat
    {
        file.deleteFile();
        return;
    }

    if (onCaptured)
        juce::MessageManager::callAsync ([cb = onCaptured, file, seconds] { cb (file, seconds); });
}
}
