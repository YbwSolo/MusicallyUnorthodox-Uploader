#include "YouTube.h"
#include "Http.h"

namespace sd::youtube
{
juce::String upload (GoogleAuth& auth, const juce::File& video, const YouTubeRequest& request, Progress progress, juce::String& error)
{
    auto token = auth.accessToken (error);
    if (token.isEmpty()) return {};

    auto* snippet = new juce::DynamicObject();
    snippet->setProperty ("title", request.title);
    snippet->setProperty ("description", request.description);
    snippet->setProperty ("categoryId", "10");                      // Music
    juce::Array<juce::var> tags;
    for (auto& t : request.tags) tags.add (t);
    snippet->setProperty ("tags", tags);

    auto* status = new juce::DynamicObject();
    const bool scheduled = request.publishAt > juce::Time::getCurrentTime();
    status->setProperty ("privacyStatus", scheduled ? juce::String ("private") : request.privacy);
    if (scheduled) status->setProperty ("publishAt", request.publishAt.toISO8601 (true));
    status->setProperty ("selfDeclaredMadeForKids", false);

    auto* root = new juce::DynamicObject();
    root->setProperty ("snippet", juce::var (snippet));
    root->setProperty ("status", juce::var (status));
    const auto meta = juce::JSON::toString (juce::var (root), true);

    // step 1: tell YouTube what is coming; it answers with the address to send the file to
    juce::MemoryBlock metaBody (meta.toRawUTF8(), meta.getNumBytesAsUTF8());
    auto start = httpSend ("POST", "https://www.googleapis.com/upload/youtube/v3/videos?uploadType=resumable&part=snippet,status",
                           { "Authorization: Bearer " + token, "Content-Type: application/json; charset=UTF-8",
                             "X-Upload-Content-Type: video/mp4", "X-Upload-Content-Length: " + juce::String (video.getSize()) }, &metaBody);

    const auto location = start.header ("Location");
    if (! start.ok() || location.isEmpty())
    {
        error = "YouTube did not accept the upload: " + start.errorText();
        return {};
    }

    // step 2: the file itself
    juce::MemoryBlock bytes;
    if (! video.loadFileAsData (bytes)) { error = "Could not read the video file."; return {}; }

    bool cancelled = false;
    auto sent = httpSend ("PUT", location, { "Authorization: Bearer " + token, "Content-Type: video/mp4" }, &bytes,
                          [&] (double f) { const bool keepGoing = progress == nullptr || progress (f); cancelled = ! keepGoing; return keepGoing; },
                          10 * 60 * 1000);

    if (cancelled) { error = "Cancelled"; return {}; }
    if (! sent.ok()) { error = "The upload to YouTube failed: " + sent.errorText(); return {}; }

    const auto id = sent.json()["id"].toString();
    if (id.isEmpty()) error = "YouTube did not return a video link.";
    return id;
}

juce::StringPairArray viewCounts (GoogleAuth& auth, const juce::StringArray& videoIds)
{
    juce::StringPairArray out;
    juce::String error;
    auto token = auth.accessToken (error);
    if (token.isEmpty()) return out;

    for (int i = 0; i < videoIds.size(); i += 50)
    {
        juce::StringArray chunk;
        for (int j = i; j < juce::jmin (i + 50, videoIds.size()); ++j) chunk.add (videoIds[j]);
        auto reply = httpSend ("GET", "https://www.googleapis.com/youtube/v3/videos?part=statistics&id=" + chunk.joinIntoString (","),
                               { "Authorization: Bearer " + token });
        if (auto* items = reply.json()["items"].getArray())
            for (auto& item : *items)
                out.set (item["id"].toString(), item["statistics"]["viewCount"].toString());
    }
    return out;
}
}
