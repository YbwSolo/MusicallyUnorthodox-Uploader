#pragma once
#include <juce_core/juce_core.h>

namespace sd
{
struct HttpResult
{
    int status = 0;
    juce::String body;
    juce::StringPairArray headers;

    bool ok() const                 { return status >= 200 && status < 300; }
    juce::var json() const          { return juce::JSON::parse (body); }
    juce::String header (const juce::String& name) const;
    juce::String errorText() const; // the service's own message when it gave one
};

using HttpProgress = std::function<bool (double fractionSent)>;     // return false to cancel

HttpResult httpSend (const juce::String& method, const juce::String& url, const juce::StringArray& headers,
                     const juce::MemoryBlock* body = nullptr, HttpProgress progress = {}, int timeoutMs = 30000);

juce::String formEncode (const juce::StringPairArray& fields);
}
