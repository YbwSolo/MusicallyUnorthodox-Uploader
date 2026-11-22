#pragma once
#include "Publisher.h"

namespace sd
{
struct AssistantAction
{
    juce::String type, text;
    double number = 0;
};

struct AssistantReply
{
    juce::String message;
    juce::Array<AssistantAction> actions;
};

/** Turns a typed request ("name it Get It, $25, free for profit, post it tomorrow at 6pm") into the steps to carry out.
    This part works with no internet and no account. When an online assistant is set up it takes over, and this is the fallback. */
namespace assistant
{
    AssistantReply understand (const juce::String& request, const Draft&, const Library&);
    AssistantReply fromJson (const juce::var& reply);
    juce::var draftAsJson (const Draft&, const Library&, const Settings&);
}
}
