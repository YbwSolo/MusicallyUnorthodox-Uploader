#pragma once
#include "Settings.h"
#include <juce_events/juce_events.h>

namespace sd
{
/** Sign in with Google for YouTube. The person approves in their own browser; the plugin never sees a password.
    Only two permissions are asked for: upload videos, and read the channel (its name, and view counts). */
class GoogleAuth : public juce::ChangeBroadcaster, private juce::Thread
{
public:
    explicit GoogleAuth (Settings&);
    ~GoogleAuth() override;

    bool configured() const;                    // a Google client ID is available
    bool signedIn() const;
    bool busy() const                           { return isThreadRunning(); }
    juce::String channelName() const            { return settings.get ("yt.channel"); }
    juce::String lastError() const              { return error; }

    void signIn();                              // opens the browser; result arrives as a change message
    void cancel();
    void signOut();

    /** Any thread except the audio thread. Empty when not signed in or the refresh failed. */
    juce::String accessToken (juce::String& errorOut);

private:
    void run() override;
    juce::String clientId() const;
    juce::String clientSecret() const;
    void fail (const juce::String& message);

    Settings& settings;
    juce::CriticalSection tokenLock;
    juce::String token, error;
    juce::int64 tokenExpiresMs = 0;
    std::unique_ptr<juce::StreamingSocket> listener;
};
}
