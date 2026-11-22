#include "GoogleAuth.h"
#include "Http.h"
#include <juce_cryptography/juce_cryptography.h>

#ifndef MUU_GOOGLE_CLIENT_ID
 #define MUU_GOOGLE_CLIENT_ID ""
#endif
#ifndef MUU_GOOGLE_CLIENT_SECRET
 #define MUU_GOOGLE_CLIENT_SECRET ""
#endif

namespace sd
{
static const char* const kScopes = "https://www.googleapis.com/auth/youtube.upload https://www.googleapis.com/auth/youtube.readonly";

static juce::String base64Url (const void* data, size_t size)
{
    return juce::Base64::toBase64 (data, size).replaceCharacter ('+', '-').replaceCharacter ('/', '_').upToFirstOccurrenceOf ("=", false, false);
}

static juce::String randomToken (int length)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    juce::Random random;
    random.setSeedRandomly();
    juce::String s;
    for (int i = 0; i < length; ++i)
        s << alphabet[random.nextInt (66)];
    return s;
}

GoogleAuth::GoogleAuth (Settings& s) : juce::Thread ("MU Uploader sign-in"), settings (s) {}
GoogleAuth::~GoogleAuth() { cancel(); }

juce::String GoogleAuth::clientId() const
{
    auto own = settings.get ("google.clientId").trim();
    return own.isNotEmpty() ? own : juce::String (MUU_GOOGLE_CLIENT_ID);
}

juce::String GoogleAuth::clientSecret() const
{
    auto own = settings.get ("google.clientSecret").trim();
    return own.isNotEmpty() ? own : juce::String (MUU_GOOGLE_CLIENT_SECRET);
}

bool GoogleAuth::configured() const   { return clientId().isNotEmpty(); }
bool GoogleAuth::signedIn() const     { return settings.get ("yt.refresh").isNotEmpty(); }

void GoogleAuth::signIn()
{
    if (isThreadRunning()) return;
    error.clear();
    if (! configured()) { fail ("YouTube sign-in is not set up in this build yet."); return; }
    startThread();
}

void GoogleAuth::cancel()
{
    signalThreadShouldExit();
    if (listener != nullptr) listener->close();
    stopThread (3000);
}

void GoogleAuth::signOut()
{
    {
        const juce::ScopedLock sl (tokenLock);
        token.clear();
        tokenExpiresMs = 0;
    }
    settings.remove ("yt.refresh");
    settings.remove ("yt.channel");
    sendChangeMessage();
}

void GoogleAuth::fail (const juce::String& message)
{
    error = message;
    sendChangeMessage();
}

void GoogleAuth::run()
{
    // Google sends the browser back to a page on this computer, which is how the plugin gets the answer
    listener = std::make_unique<juce::StreamingSocket>();
    int port = 0;
    for (int candidate = 53682; candidate < 53702; ++candidate)
        if (listener->createListener (candidate, "127.0.0.1")) { port = candidate; break; }

    if (port == 0) { fail ("Could not open a local port for the sign-in."); return; }

    const auto redirect = "http://127.0.0.1:" + juce::String (port);
    const auto verifier = randomToken (64);
    const auto state = randomToken (24);
    juce::SHA256 hash (verifier.toRawUTF8(), verifier.getNumBytesAsUTF8());
    const auto challenge = base64Url (hash.getRawData().getData(), hash.getRawData().getSize());

    juce::StringPairArray q;
    q.set ("client_id", clientId());              q.set ("redirect_uri", redirect);
    q.set ("response_type", "code");              q.set ("scope", kScopes);
    q.set ("code_challenge", challenge);          q.set ("code_challenge_method", "S256");
    q.set ("access_type", "offline");             q.set ("prompt", "consent");
    q.set ("state", state);
    juce::URL ("https://accounts.google.com/o/oauth2/v2/auth?" + formEncode (q)).launchInDefaultBrowser();

    juce::String code, problem;
    const auto deadline = juce::Time::getMillisecondCounter() + 5 * 60 * 1000;

    while (! threadShouldExit() && code.isEmpty() && problem.isEmpty() && juce::Time::getMillisecondCounter() < deadline)
    {
        if (listener->waitUntilReady (true, 500) != 1) continue;
        std::unique_ptr<juce::StreamingSocket> client (listener->waitForNextConnection());
        if (client == nullptr) continue;

        char buffer[4096] = {};
        if (client->waitUntilReady (true, 2000) == 1)
            client->read (buffer, sizeof (buffer) - 1, false);

        const auto requestLine = juce::String::fromUTF8 (buffer).upToFirstOccurrenceOf ("\r\n", false, false);
        const auto path = requestLine.fromFirstOccurrenceOf (" ", false, false).upToLastOccurrenceOf (" ", false, false);
        juce::URL parsed ("http://127.0.0.1" + path);
        juce::String gotCode, gotState, gotError;
        for (int i = 0; i < parsed.getParameterNames().size(); ++i)
        {
            const auto name = parsed.getParameterNames()[i];
            const auto value = parsed.getParameterValues()[i];
            if (name == "code") gotCode = value; else if (name == "state") gotState = value; else if (name == "error") gotError = value;
        }

        const bool good = gotCode.isNotEmpty() && gotState == state;
        const bool answer = good || gotError.isNotEmpty();
        const juce::String page = "<!doctype html><meta charset=utf-8><title>MU Uploader</title>"
                                  "<body style='background:#000;color:#f2f2f2;font-family:-apple-system,Segoe UI,sans-serif;text-align:center;padding-top:18vh'>"
                                  "<h1 style='color:#fbf20d;letter-spacing:.04em'>" + juce::String (good ? "YOUTUBE CONNECTED" : (answer ? "SIGN-IN CANCELLED" : "MU UPLOADER"))
                                + "</h1><p>" + juce::String (good ? "You can close this tab and go back to your beat." : "Nothing was connected. Go back to the plugin to try again.") + "</p></body>";
        const auto response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: "
                              + juce::String (page.getNumBytesAsUTF8()) + "\r\n\r\n" + page;
        client->write (response.toRawUTF8(), (int) response.getNumBytesAsUTF8());

        if (good) code = gotCode;
        else if (gotError.isNotEmpty()) problem = "Sign-in was cancelled.";
    }

    listener->close();
    if (threadShouldExit()) return;
    if (code.isEmpty()) { fail (problem.isNotEmpty() ? problem : "Sign-in timed out. Try again."); return; }

    juce::StringPairArray form;
    form.set ("client_id", clientId());           form.set ("client_secret", clientSecret());
    form.set ("code", code);                      form.set ("code_verifier", verifier);
    form.set ("grant_type", "authorization_code"); form.set ("redirect_uri", redirect);
    const auto encoded = formEncode (form);
    juce::MemoryBlock body (encoded.toRawUTF8(), encoded.getNumBytesAsUTF8());
    auto reply = httpSend ("POST", "https://oauth2.googleapis.com/token", { "Content-Type: application/x-www-form-urlencoded" }, &body);
    if (! reply.ok()) { fail ("Google refused the sign-in: " + reply.errorText()); return; }

    const auto json = reply.json();
    const auto refresh = json["refresh_token"].toString();
    if (refresh.isEmpty()) { fail ("Google did not return a long-lived sign-in. Try again."); return; }

    {
        const juce::ScopedLock sl (tokenLock);
        token = json["access_token"].toString();
        tokenExpiresMs = juce::Time::currentTimeMillis() + (juce::int64) ((int) json["expires_in"] - 60) * 1000;
    }

    auto channel = httpSend ("GET", "https://www.googleapis.com/youtube/v3/channels?part=snippet&mine=true", { "Authorization: Bearer " + token });
    auto name = channel.json()["items"][0]["snippet"]["title"].toString();
    if (channel.ok() && name.isEmpty())
    {
        fail ("That Google account has no YouTube channel yet. Create one on YouTube, then connect again.");
        return;
    }

    juce::MessageManager::callAsync ([this, refresh, name]
    {
        settings.set ("yt.refresh", refresh);
        settings.set ("yt.channel", name.isNotEmpty() ? name : "YouTube channel");
        error.clear();
        sendChangeMessage();
    });
}

juce::String GoogleAuth::accessToken (juce::String& errorOut)
{
    const juce::ScopedLock sl (tokenLock);
    if (token.isNotEmpty() && juce::Time::currentTimeMillis() < tokenExpiresMs)
        return token;

    const auto refresh = settings.get ("yt.refresh");
    if (refresh.isEmpty()) { errorOut = "YouTube is not connected."; return {}; }

    juce::StringPairArray form;
    form.set ("client_id", clientId());           form.set ("client_secret", clientSecret());
    form.set ("refresh_token", refresh);          form.set ("grant_type", "refresh_token");
    const auto encoded = formEncode (form);
    juce::MemoryBlock body (encoded.toRawUTF8(), encoded.getNumBytesAsUTF8());
    auto reply = httpSend ("POST", "https://oauth2.googleapis.com/token", { "Content-Type: application/x-www-form-urlencoded" }, &body);

    if (! reply.ok())
    {
        errorOut = reply.status == 400 || reply.status == 401 ? "YouTube needs connecting again (the sign-in expired)." : "Could not reach Google: " + reply.errorText();
        return {};
    }

    const auto json = reply.json();
    token = json["access_token"].toString();
    tokenExpiresMs = juce::Time::currentTimeMillis() + (juce::int64) ((int) json["expires_in"] - 60) * 1000;
    return token;
}
}
