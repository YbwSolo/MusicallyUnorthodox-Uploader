#include "Pages.h"
#include "../core/Http.h"

namespace sd
{
namespace
{
struct Message { juce::String text; bool mine = false; };

/** The conversation: your requests on the right in yellow, the answers on the left. */
class Conversation : public juce::Component
{
public:
    void add (const juce::String& text, bool mine)
    {
        messages.add ({ text, mine });
        layout();
    }

    void setWidth (int w) { width = w; layout(); }

    void paint (juce::Graphics& g) override
    {
        float y = 8.0f;
        const auto font = theme::body (13.5f);
        for (auto& m : messages)
        {
            const float maxText = (float) width * 0.72f;
            const float h = theme::wrappedHeight (m.text, maxText, font);
            const float w = juce::jmin (maxText, widest (m.text, font, maxText)) + 24.0f;
            const auto bubble = juce::Rectangle<float> (m.mine ? (float) width - w - 4.0f : 4.0f, y, w, h + 18.0f);
            g.setColour (m.mine ? theme::yellow : theme::raised);
            g.fillRoundedRectangle (bubble, 10.0f);
            theme::drawWrapped (g, m.text, bubble.reduced (12.0f, 9.0f), font, m.mine ? theme::onYellow : theme::text);
            y += h + 28.0f;
        }
    }

private:
    static float widest (const juce::String& text, const juce::Font& font, float limit)
    {
        float w = 0.0f;
        for (auto& line : juce::StringArray::fromLines (text))
            w = juce::jmax (w, juce::GlyphArrangement::getStringWidth (font, line));
        return juce::jmin (limit, w + 2.0f);
    }

    void layout()
    {
        float y = 8.0f;
        const auto font = theme::body (13.5f);
        for (auto& m : messages)
            y += theme::wrappedHeight (m.text, (float) width * 0.72f, font) + 28.0f;
        setSize (width, (int) y + 4);
        repaint();
    }

    juce::Array<Message> messages;
    int width = 400;
};

class AssistantPage : public Page, private juce::Thread
{
public:
    explicit AssistantPage (App& a) : Page (a), juce::Thread ("MU Uploader assistant")
    {
        addAndMakeVisible (view);
        view.setViewedComponent (&conversation, false);
        view.setScrollBarsShown (true, false);

        addAndMakeVisible (input);
        theme::styleEditor (input, "Tell me about the beat and what to do with it");
        input.onReturnKey = [this] { submit(); };
        addAndMakeVisible (send);
        theme::makePrimary (send);
        send.onClick = [this] { submit(); };

        const juce::StringArray ideas { "Name it Get It, Gunna type beat, $25, free for profit",
                                        "Post it tomorrow at 6pm",
                                        "Free or free for profit: what is the difference?" };
        for (auto& idea : ideas)
        {
            auto* b = chips.add (new juce::TextButton (idea));
            addAndMakeVisible (b);
            b->onClick = [this, idea] { input.setText (idea, false); submit(); };
        }

        conversation.add ("Tell me what the beat is and what you want done, and I will fill in the details and post it.\n"
                          "Example: \"Name it Get It, Gunna type beat, 161 bpm, key E minor, $25, free for profit, upload it\".", false);
    }

    ~AssistantPage() override { stopThread (6000); }

    void paint (juce::Graphics& g) override
    {
        paintHeader (g, "ASSISTANT", "Say it in one go. The details are filled in and the beat is posted for you.");
        theme::fillPanel (g, view.getBounds().expanded (8).toFloat(), theme::panel);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (24, 0);
        r.removeFromTop (84);
        r.removeFromBottom (20);

        auto bottom = r.removeFromBottom (36);
        send.setBounds (bottom.removeFromRight (90));
        bottom.removeFromRight (10);
        input.setBounds (bottom);
        r.removeFromBottom (12);

        auto chipRow = r.removeFromBottom (30);
        const int widths[] = { 296, 152, 0 };
        for (int i = 0; i < chips.size(); ++i)
        {
            chips[i]->setBounds (i == chips.size() - 1 ? chipRow : chipRow.removeFromLeft (widths[i]));
            chipRow.removeFromLeft (8);
        }
        r.removeFromBottom (16);

        view.setBounds (r.reduced (8));
        conversation.setWidth (view.getWidth() - 12);
    }

    void shown() override { input.grabKeyboardFocus(); }

private:
    void submit()
    {
        const auto text = input.getText().trim();
        if (text.isEmpty() || isThreadRunning()) return;
        input.clear();
        say (text, true);

        const auto endpoint = app.services.settings.get ("ai.endpoint").trim();
        if (endpoint.isEmpty())
        {
            answer (assistant::understand (text, app.draft(), app.services.library));
            return;
        }

        // an online assistant is set up: ask it, and fall back to the built-in understanding if it does not answer
        pendingText = text;
        pendingEndpoint = endpoint;
        pendingState = juce::JSON::toString (assistant::draftAsJson (app.draft(), app.services.library, app.services.settings), true);
        startThread();
    }

    void run() override
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("request", pendingText);
        o->setProperty ("state", juce::JSON::parse (pendingState));
        const auto json = juce::JSON::toString (juce::var (o), true);
        juce::MemoryBlock body (json.toRawUTF8(), json.getNumBytesAsUTF8());
        const auto reply = httpSend ("POST", pendingEndpoint, { "Content-Type: application/json" }, &body, {}, 20000);

        juce::Component::SafePointer<AssistantPage> safe (this);
        juce::MessageManager::callAsync ([safe, reply, text = pendingText]
        {
            if (safe == nullptr) return;
            auto parsed = assistant::fromJson (reply.json());
            if (! reply.ok() || (parsed.message.isEmpty() && parsed.actions.isEmpty()))
                parsed = assistant::understand (text, safe->app.draft(), safe->app.services.library);
            safe->answer (parsed);
        });
    }

    void answer (const AssistantReply& reply)
    {
        if (! reply.actions.isEmpty()) app.apply (reply.actions);
        say (reply.message.isNotEmpty() ? reply.message : juce::String ("Done."), false);
    }

    void say (const juce::String& text, bool mine)
    {
        conversation.add (text, mine);
        view.setViewPosition (0, juce::jmax (0, conversation.getHeight() - view.getHeight()));
    }

    juce::Viewport view;
    Conversation conversation;
    juce::TextEditor input;
    juce::TextButton send { "Send" };
    juce::OwnedArray<juce::TextButton> chips;
    juce::String pendingText, pendingEndpoint, pendingState;
};
}

std::unique_ptr<Page> makeAssistantPage (App& app) { return std::make_unique<AssistantPage> (app); }
}
