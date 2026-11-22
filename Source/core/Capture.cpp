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
    alive->store (false);
    finishing.removeAllJobs (true, 20000);
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

    juce::File unused;
    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        writer.reset();
        unused = currentFile;
        currentFile = juce::File();
    }
    if (unused.existsAsFile() && unused.getSize() < 4096)
        unused.deleteFile();
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

void Capture::process (const juce::AudioBuffer<float>& buffer, bool hostIsPlaying, bool hostHasTransport, bool offlineRender, juce::int64 hostSample)
{
    lastBlockMs.store (juce::Time::getMillisecondCounter());
    auto s = currentState.load();
    const auto previousSample = lastHostSample.exchange (hostIsPlaying ? hostSample : -1);

    if (s == State::idle && ! takeEnded.load())
    {
        if (offlineRender && catchExports.load())
        {
            currentKind.store (Kind::exported);
            currentState.store (s = State::recording);
        }
        else if (! offlineRender && listen.load() && hostHasTransport && hostIsPlaying)
        {
            // the beat is playing: keep what is heard, so a play-through is enough to catch it
            currentKind.store (Kind::heard);
            sawPlaying.store (true);
            currentState.store (s = State::recording);
        }
    }
    else if (s == State::armed && (hostIsPlaying || ! hostHasTransport))
    {
        currentKind.store (Kind::recorded);
        sawPlaying.store (hostIsPlaying);
        currentState.store (s = State::recording);
    }

    if (s != State::recording || takeEnded.load())
        return;

    // the take is over when the transport stops, when playback jumps back (a loop, or play pressed again),
    // or when an export hands back to normal playback; the timer then closes the file
    const bool exported = currentKind.load() == Kind::exported;
    const bool stopped = ! exported && hostHasTransport && sawPlaying.load() && ! hostIsPlaying;
    const bool jumpedBack = ! exported && hostSample >= 0 && previousSample >= 0 && hostSample + (juce::int64) (rate * 0.25) < previousSample;
    if (stopped || jumpedBack || (exported && ! offlineRender))
    {
        takeEnded.store (true);
        return;
    }

    const juce::SpinLock::ScopedTryLockType lock (writerLock);
    if (lock.isLocked() && writer != nullptr && buffer.getNumSamples() > 0)
    {
        // an export runs faster than real time, so there it is fine (and necessary) to wait for the disk
        bool kept = writer->write (buffer.getArrayOfReadPointers(), buffer.getNumSamples());
        for (int tries = 0; ! kept && offlineRender && tries < 4000; ++tries)
        {
            juce::Thread::sleep (1);
            kept = writer->write (buffer.getArrayOfReadPointers(), buffer.getNumSamples());
        }
        if (kept) samplesWritten.fetch_add (buffer.getNumSamples());
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

    // an export has also ended when the host simply stops sending audio
    const auto quietMs = juce::Time::getMillisecondCounter() - lastBlockMs.load();
    const bool exportEnded = currentKind.load() == Kind::exported && quietMs > 700;
    const bool hostWentAway = quietMs > 3000;
    if (exportEnded || hostWentAway || takeEnded.load())
    {
        const juce::ScopedLock sl (lifecycle);
        if (currentState.load() == State::recording)
            finish();
    }
}

void Capture::finish()
{
    const double seconds = secondsRecorded();
    const auto file = currentFile;
    const auto kind = currentKind.load();
    currentState.store (State::idle);

    if (prepared) openWriter();                     // closes this take and gets the next one ready
    else
    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        writer.reset();
    }
    takeEnded.store (false);

    const double shortest = kind == Kind::heard ? shortestHeardTake : shortestOtherTake;
    if (seconds < shortest || onCaptured == nullptr)        // a blip, not a beat
    {
        file.deleteFile();
        return;
    }

    finishing.addJob ([stillHere = alive, callback = onCaptured, file, seconds, kind, shortest]
    {
        // an export is exactly what was asked for; anything played by hand starts and ends with silence to cut
        const double length = kind == Kind::exported ? seconds : trimSilence (file);
        if (length < shortest || ! stillHere->load())
        {
            file.deleteFile();
            return;
        }

        juce::MessageManager::callAsync ([stillHere, callback, file, length, kind]
        {
            if (stillHere->load()) callback (file, length, kind);
        });
    });
}

double Capture::trimSilence (const juce::File& file)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader;
    if (auto* in = file.createInputStream().release())
        reader.reset (wav.createReaderFor (in, true));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return 0.0;

    const auto total = reader->lengthInSamples;
    const double sampleRate = reader->sampleRate;
    const int numChannels = (int) reader->numChannels;
    const float floor = 0.0004f;                    // about -68 dB: below this is the room, not the beat
    const int block = 1 << 15;
    juce::AudioBuffer<float> buffer (numChannels, block);
    juce::int64 first = -1, last = -1;

    for (juce::int64 at = 0; at < total; at += block)
    {
        const int n = (int) juce::jmin ((juce::int64) block, total - at);
        reader->read (&buffer, 0, n, at, true, true);
        for (int c = 0; c < numChannels; ++c)
        {
            const float* d = buffer.getReadPointer (c);
            for (int i = 0; i < n; ++i)
                if (std::abs (d[i]) > floor)
                {
                    if (first < 0 || at + i < first) first = at + i;
                    if (at + i > last) last = at + i;
                }
        }
    }

    if (first < 0)
        return 0.0;                                 // nothing but silence

    const auto start = juce::jmax ((juce::int64) 0, first - (juce::int64) (sampleRate * 0.02));
    const auto end = juce::jmin (total, last + (juce::int64) (sampleRate * 0.4));
    if (start < (juce::int64) (sampleRate * 0.05) && total - end < (juce::int64) (sampleRate * 0.5))
        return (double) total / sampleRate;         // already tight

    const auto trimmed = file.getSiblingFile (file.getFileNameWithoutExtension() + "-trimmed.wav");
    trimmed.deleteFile();
    bool written = false;
    if (auto* out = trimmed.createOutputStream().release())
    {
        if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (out, sampleRate, (unsigned int) numChannels, 24, {}, 0)))
            written = writer->writeFromAudioReader (*reader, start, end - start);
        else
            delete out;
    }

    reader.reset();
    if (! written || ! trimmed.replaceFileIn (file))
    {
        trimmed.deleteFile();
        return (double) total / sampleRate;
    }
    return (double) (end - start) / sampleRate;
}
}
