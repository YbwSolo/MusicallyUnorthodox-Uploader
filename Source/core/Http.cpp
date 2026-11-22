#include "Http.h"

namespace sd
{
juce::String HttpResult::header (const juce::String& name) const
{
    for (auto& key : headers.getAllKeys())
        if (key.equalsIgnoreCase (name))
            return headers[key];
    return {};
}

juce::String HttpResult::errorText() const
{
    auto v = json();
    auto message = v["error"]["message"].toString();
    if (message.isEmpty()) message = v["error_description"].toString();
    if (message.isEmpty() && v["error"].isString()) message = v["error"].toString();
    if (message.isEmpty()) message = status == 0 ? "No connection" : "Error " + juce::String (status);
    return message;
}

namespace
{
struct ProgressListener : juce::WebInputStream::Listener
{
    explicit ProgressListener (HttpProgress p) : progress (std::move (p)) {}
    bool postDataSendProgress (juce::WebInputStream&, int sent, int total) override
    {
        return progress == nullptr || progress (total > 0 ? (double) sent / (double) total : 0.0);
    }
    HttpProgress progress;
};
}

HttpResult httpSend (const juce::String& method, const juce::String& url, const juce::StringArray& headers,
                     const juce::MemoryBlock* body, HttpProgress progress, int timeoutMs)
{
    juce::URL target (url);
    if (body != nullptr)
        target = target.withPOSTData (*body);

    juce::WebInputStream stream (target, false);
    stream.withExtraHeaders (headers.joinIntoString ("\r\n"))
          .withCustomRequestCommand (method)
          .withConnectionTimeout (timeoutMs)
          .withNumRedirectsToFollow (3);

    ProgressListener listener (std::move (progress));
    HttpResult result;
    if (! stream.connect (&listener))
        return result;

    result.status = stream.getStatusCode();
    result.headers = stream.getResponseHeaders();
    result.body = stream.readEntireStreamAsString();
    return result;
}

juce::String formEncode (const juce::StringPairArray& fields)
{
    juce::StringArray parts;
    for (auto& key : fields.getAllKeys())
        parts.add (juce::URL::addEscapeChars (key, true) + "=" + juce::URL::addEscapeChars (fields[key], true));
    return parts.joinIntoString ("&");
}
}
