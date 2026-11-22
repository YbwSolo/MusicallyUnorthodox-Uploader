// Draws every page of the plugin to a PNG with some example beats in it. Run with HOME pointing at an empty folder.
//   MUUPreview <output folder>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "core/Media.h"

using namespace sd;

static juce::File makeCover (const juce::File& dir, const juce::String& name, juce::Colour a, juce::Colour b)
{
    juce::Image image (juce::Image::RGB, 800, 800, true);
    juce::Graphics g (image);
    g.setGradientFill (juce::ColourGradient (a, 0, 0, b, 800, 800, false));
    g.fillAll();
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    for (int i = 0; i < 9; ++i) g.drawEllipse (400.0f - 40.0f * (float) i, 400.0f - 40.0f * (float) i, 80.0f * (float) i, 80.0f * (float) i, 3.0f);
    auto file = dir.getChildFile (name + ".png");
    juce::PNGImageFormat png;
    if (auto out = file.createOutputStream()) png.writeImageToStream (image, *out);
    return file;
}

static juce::File makeAudio (const juce::File& dir, const juce::String& name, double seconds)
{
    auto file = dir.getChildFile (name + ".wav");
    juce::WavAudioFormat wav;
    if (auto* stream = file.createOutputStream().release())
        if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (stream, 44100.0, 2, 16, {}, 0)))
        {
            juce::AudioBuffer<float> buffer (2, 44100);
            for (int i = 0; i < 44100; ++i) { const float v = 0.3f * std::sin ((float) i * 0.05f); buffer.setSample (0, i, v); buffer.setSample (1, i, v); }
            for (int s = 0; s < (int) seconds; ++s) writer->writeFromAudioSampleBuffer (buffer, 0, 44100);
        }
    return file;
}

class PreviewApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "MUUPreview"; }
    const juce::String getApplicationVersion() override { return "1"; }

    void initialise (const juce::String& commandLine) override
    {
        const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (commandLine.trim().unquoted().isEmpty() ? juce::String ("preview") : commandLine.trim().unquoted());
        out.createDirectory();
        const auto scratch = out.getChildFile ("scratch");
        scratch.createDirectory();

        processor = std::make_unique<UploaderProcessor>();
        auto& s = processor->services();
        s.settings.set ("producer", "JP");
        s.settings.set ("email", "jp@example.com");
        s.settings.set ("instagram", "prodjp");
        s.settings.set ("storeLink", "https://www.beatstars.com/jp");

        const auto trap = s.library.addCatalog ("Trap", {});
        const auto dark = s.library.addCatalog ("Dark", trap.id);
        const auto guitar = s.library.addCatalog ("Guitar", trap.id);
        const auto rnb = s.library.addCatalog ("R&B", {});
        s.library.addCatalog ("Sold", {});

        struct Example { const char* title; const char* artist; double bpm; const char* key; double price; const char* licence; juce::String catalog; juce::Colour a, b; const char* yt; };
        const Example examples[] = {
            { "Get It", "Gunna", 161, "E Minor", 20, "ffp", guitar.id, juce::Colour (0xff1b3a2f), juce::Colour (0xffd9c36a), "abc123" },
            { "Midnight Run", "Drake", 142, "C# Minor", 30, "free", rnb.id, juce::Colour (0xff1a1040), juce::Colour (0xff7a3cff), "def456" },
            { "No Sleep", "Lil Baby", 150, "F Minor", 25, "ffp", dark.id, juce::Colour (0xff3a0d0d), juce::Colour (0xffff5a36), "" },
            { "Rot", "Che", 170, "G Minor", 20, "ffp", dark.id, juce::Colour (0xff0d2a3a), juce::Colour (0xff36c8ff), "" } };

        for (auto& e : examples)
        {
            Beat b;
            b.title = e.title; b.artist = e.artist; b.bpm = e.bpm; b.key = e.key; b.price = e.price; b.licence = e.licence; b.catalogId = e.catalog;
            b.seconds = 151;
            if (juce::String (e.yt).isNotEmpty()) { b.youtubeId = e.yt; b.youtubeUrl = juce::String ("https://youtu.be/") + e.yt; }
            s.library.save (b, makeAudio (scratch, e.title, 3), makeCover (scratch, e.title, e.a, e.b), true);
        }

        Contact c;
        c.name = "Sam Carter"; c.company = "Northside Records"; c.email = "demos@northside.example"; c.kind = "A&R";
        s.contacts.save (c);
        c = Contact(); c.name = "Maya"; c.company = "Low End Management"; c.email = "maya@lowend.example"; c.kind = "Manager";
        s.contacts.save (c);

        auto& d = processor->draft;
        d.beat.title = "Get It"; d.beat.artist = "Gunna"; d.beat.bpm = 161; d.beat.key = "E Minor"; d.beat.tags = "wunna, melodic, guitar";
        d.beat.catalogId = guitar.id;
        d.beat.seconds = 151;
        d.audio = makeAudio (scratch, "take", 3);
        d.cover = makeCover (scratch, "draftcover", juce::Colour (0xff1b3a2f), juce::Colour (0xffd9c36a));

        editor.reset (dynamic_cast<UploaderEditor*> (processor->createEditor()));
        editor->setVisible (true);
        editor->addToDesktop (0);

        const char* names[] = { "1-upload", "2-beats", "3-email", "4-assistant", "5-settings" };
        for (int i = 0; i < pageCount; ++i)
        {
            editor->selectPage (i);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
            const auto shot = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
            juce::PNGImageFormat png;
            auto file = out.getChildFile (juce::String (names[i]) + ".png");
            file.deleteFile();
            if (auto stream = file.createOutputStream()) png.writeImageToStream (shot, *stream);
        }

        media::FrameText words;
        words.title = "Get It"; words.subtitle = "Gunna type beat"; words.label = "FREE FOR PROFIT"; words.credit = "prod. JP";
        words.chips = { "161 BPM", "E MINOR" };
        const auto coverImage = juce::ImageFileFormat::loadFrom (d.cover);
        juce::PNGImageFormat png;
        auto save = [&] (const juce::Image& image, const juce::String& name)
        {
            auto file = out.getChildFile (name);
            file.deleteFile();
            if (auto stream = file.createOutputStream()) png.writeImageToStream (image, *stream);
        };
        save (media::composeFrame (coverImage, words).image, "6-video-frame.png");
        save (media::composeFrame (coverImage, words, true).image, "7-video-frame-tall.png");
        words.title = "Midnight Run Forever";
        save (media::composeFrame ({}, words).image, "8-video-frame-no-cover.png");

        quit();
    }

    void shutdown() override { editor.reset(); processor.reset(); }

private:
    std::unique_ptr<UploaderProcessor> processor;
    std::unique_ptr<UploaderEditor> editor;
};

START_JUCE_APPLICATION (PreviewApp)
