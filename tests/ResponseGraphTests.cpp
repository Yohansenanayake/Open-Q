#include "PluginEditor.h"
#include "ui/Palette.h"
#include "ui/ResponseMapping.h"
#include "ui/ResponseWorker.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <chrono>
#include <cstdio>
#include <thread>

namespace
{
    bool awaitFrame (openq::ResponseWorker& worker, const std::function<bool (const openq::ResponseFrame&)>& ready)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (2);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (worker.pullFrame() && ready (worker.getFrame()))
                return true;
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        return false;
    }
}

TEST_CASE ("Automatic graph refresh stays bounded on high refresh displays", "[response][ui]")
{
    for (const double monitorHz : { 25.0, 30.0, 50.0, 60.0, 62.5, 90.0, 120.0, 144.0, 165.0, 240.0, 360.0 })
    {
        CAPTURE (monitorHz);
        openq::GraphRefreshLimiter limiter;
        constexpr int seconds = 10;
        int frames = 0;
        double previous = -1.0;
        for (int tick = 0; tick < monitorHz * seconds; ++tick)
        {
            const auto timestamp = static_cast<double> (tick) / monitorHz;
            if (!limiter.tick (timestamp))
                continue;
            if (frames > 0)
                CHECK (timestamp > previous);
            previous = timestamp;
            limiter.recordPaintDuration (0.001, timestamp + 0.001);
            ++frames;
        }
        CHECK (frames <= static_cast<int> (seconds * openq::GraphRefreshLimiter::targetFramesPerSecond));
        CHECK (frames >= seconds * 24);
    }
}

TEST_CASE ("Graph refresh recovers from pauses clock changes and editor reopening without bursts", "[response][ui]")
{
    openq::GraphRefreshLimiter limiter;
    CHECK_FALSE (limiter.tick (std::numeric_limits<double>::quiet_NaN()));
    CHECK_FALSE (limiter.tick (std::numeric_limits<double>::infinity()));
    CHECK (limiter.tick (0.0));
    CHECK_FALSE (limiter.tick (0.0));
    CHECK_FALSE (limiter.tick (0.001));
    CHECK (limiter.tick (20.0));
    CHECK_FALSE (limiter.tick (20.001));
    CHECK (limiter.tick (1.0));
    CHECK_FALSE (limiter.tick (1.001));
    limiter.reset();
    CHECK (limiter.tick (1.001));
    CHECK_FALSE (limiter.tick (1.001));
}

TEST_CASE ("Triple buffer publishes complete latest values without tearing", "[response][threading]")
{
    struct Packet
    {
        std::uint64_t sequence = 0;
        std::array<std::uint64_t, 32> values {};
    };
    openq::TripleBuffer<Packet> mailbox;
    CHECK_FALSE (mailbox.pull());
    constexpr std::uint64_t publications = 100000;
    std::thread producer ([&] {
        for (std::uint64_t sequence = 1; sequence <= publications; ++sequence)
        {
            auto& packet = mailbox.getWriter();
            packet.sequence = sequence;
            packet.values.fill (sequence);
            mailbox.publish();
        }
    });
    std::uint64_t last = 0;
    bool intact = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);
    while (last < publications && std::chrono::steady_clock::now() < deadline)
    {
        if (mailbox.pull())
        {
            const auto& packet = mailbox.getReader();
            intact = intact && packet.sequence > last
                     && std::ranges::all_of (packet.values, [&] (auto value) { return value == packet.sequence; });
            last = packet.sequence;
        }
        else
            std::this_thread::yield();
    }
    producer.join();
    CHECK (intact);
    CHECK (last == publications);
    CHECK_FALSE (mailbox.pull());
}

TEST_CASE ("Target response uses the digital DSP coefficient design", "[response]")
{
    openq::DigitalResponse response { openq::makeFilterCoefficients (openq::FilterType::Bell, 48000.0, 20000.0, 6.0, 1.0), 48000.0 };
    CHECK_THAT (openq::responseMagnitudeDb (response, 20000.0), Catch::Matchers::WithinAbs (6.0, 1e-9));
    CHECK_THAT (openq::responseMagnitudeDb (response, 24000.0), Catch::Matchers::WithinAbs (5.83299135367, 1e-9));
    response.coefficients = openq::makeFilterCoefficients (openq::FilterType::Bell, 48000.0, 1000.0, 0.0, 1.0);
    CHECK_THAT (openq::responseMagnitudeDb (response, 1000.0), Catch::Matchers::WithinAbs (0.0, 0.0));
}

TEST_CASE ("Target response follows all controls with no audio processing", "[response][threading]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 64);
    openq::ResponseWorker worker (processor.parameters, processor.getResponseSampleRate());
    worker.setGeometry ({ 400, 200 });
    REQUIRE (worker.start());
    worker.requestUpdate();
    REQUIRE (awaitFrame (worker, [] (const auto& frame) { return frame.valid; }));
    struct Target
    {
        float frequency, gain, q;
    };
    for (const auto target : { Target { 1000.0f, 6.0f, 1.0f }, Target { 10000.0f, -12.0f, 4.0f }, Target { 20000.0f, 24.0f, 0.1f } })
    {
        auto set = [&] (const char* id, float value) {
            auto* parameter = processor.parameters.getParameter (id);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        };
        set ("band.0.frequency", target.frequency);
        set ("band.0.gain", target.gain);
        set ("band.0.q", target.q);
        const auto probeFrequency = static_cast<double> (target.frequency) * 0.8;
        const openq::DigitalResponse expected { openq::makeFilterCoefficients (openq::FilterType::Bell, 48000.0, target.frequency, target.gain, target.q), 48000.0 };
        const auto expectedDb = openq::responseMagnitudeDb (expected, probeFrequency);
        worker.requestUpdate();
        REQUIRE (awaitFrame (worker, [&] (const auto& frame) {
            return frame.valid && std::abs (openq::responseMagnitudeDb (frame.response, probeFrequency) - expectedDb) < 1e-4;
        }));
    }
    // Sample-rate changes must update the response without a processBlock call too.
    processor.prepareToPlay (8000.0, 64);
    worker.requestUpdate();
    REQUIRE (awaitFrame (worker, [] (const auto& frame) { return frame.valid && juce::exactlyEqual (frame.response.sampleRate, 8000.0); }));
    const openq::DigitalResponse clamped { openq::makeFilterCoefficients (openq::FilterType::Bell, 8000.0, 3920.0, 24.0, 0.1), 8000.0 };
    CHECK_THAT (openq::responseMagnitudeDb (worker.getFrame().response, 3000.0),
        Catch::Matchers::WithinAbs (openq::responseMagnitudeDb (clamped, 3000.0), 1e-4));
    worker.stop();
}

TEST_CASE ("Response follows every slot and sums enabled bands before clipping without audio", "[multiband][response][threading]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 64);
    openq::ResponseWorker worker (processor.parameters, processor.getResponseSampleRate());
    worker.setGeometry ({ 400, 200 });
    REQUIRE (worker.start());
    auto set = [&] (size_t band, const char* name, float value) {
        auto* parameter = processor.parameters.getParameter (openq::bandParameterId (band, name));
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        if (band > 0)
            set (band - 1, "status", 0.0f);
        const auto frequency = 1000.0f + 10000.0f * static_cast<float> (band) / static_cast<float> (openq::bandCount);
        const auto gain = 3.0f + 7.0f * static_cast<float> (band) / static_cast<float> (openq::bandCount);
        const auto q = 1.0f + 4.0f * static_cast<float> (band) / static_cast<float> (openq::bandCount);
        set (band, "frequency", frequency);
        set (band, "gain", gain);
        set (band, "q", q);
        set (band, "status", 2.0f);
        const auto type = static_cast<openq::FilterType> (band % 3);
        set (band, "type", static_cast<float> (type));
        const openq::DigitalResponse expected { openq::makeFilterCoefficients (type, 48000.0, frequency, gain, q), 48000.0 };
        const auto probe = frequency * 0.8;
        worker.requestUpdate();
        REQUIRE (awaitFrame (worker, [&] (const auto& frame) {
            return frame.valid && std::abs (openq::responseMagnitudeDb (frame.response, probe) - openq::responseMagnitudeDb (expected, probe)) < 1e-4;
        }));
    }
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        set (band, "type", 0.0f);
        set (band, "frequency", 20000.0f);
        set (band, "gain", 24.0f);
        set (band, "q", 1.0f);
        set (band, "status", 2.0f);
    }
    worker.requestUpdate();
    REQUIRE (awaitFrame (worker, [] (const auto& frame) {
        return frame.valid && std::abs (openq::responseMagnitudeDb (frame.response, 20000.0) - 24.0 * static_cast<double> (openq::bandCount)) < 1e-4;
    }));
    CHECK_THAT (worker.getFrame().curve.getPointAlongPath (worker.getFrame().curve.getLength()).y, Catch::Matchers::WithinAbs (0.0, 1e-4));

    // The positive subtotal exceeds the graph range, but the final cut brings it back inside.
    // Distribute a fixed subtotal across the preceding slots so this works for different counts.
    constexpr double positiveSubtotalDb = std::min (30.0, 6.0 * static_cast<double> (openq::bandCount - 1));
    constexpr double finalCutDb = -24.0;
    constexpr double combinedDb = positiveSubtotalDb + finalCutDb;
    const auto boostPerBand = static_cast<float> (positiveSubtotalDb / static_cast<double> (openq::bandCount - 1));
    for (size_t band = 0; band < openq::bandCount; ++band)
        set (band, "gain", band == openq::bandCount - 1 ? static_cast<float> (finalCutDb) : boostPerBand);
    worker.requestUpdate();
    REQUIRE (awaitFrame (worker, [] (const auto& frame) {
        return frame.valid && std::abs (openq::responseMagnitudeDb (frame.response, 20000.0) - combinedDb) < 1e-4;
    }));
    const auto expectedY = openq::responseMapping::dbToProportion (combinedDb) * worker.getFrame().geometry.height;
    CHECK_THAT (worker.getFrame().curve.getPointAlongPath (worker.getFrame().curve.getLength()).y, Catch::Matchers::WithinAbs (expectedY, 0.001));
    for (const float status : { 1.0f, 0.0f, 2.0f })
    {
        for (size_t band = 0; band < openq::bandCount; ++band)
            set (band, "status", status);
        worker.requestUpdate();
        const auto expectedDb = juce::exactlyEqual (status, 2.0f) ? combinedDb : 0.0;
        REQUIRE (awaitFrame (worker, [expectedDb, status] (const auto& frame) {
            return frame.valid && std::ranges::all_of (frame.settings, [status] (const auto& band) { return band.status == static_cast<openq::BandStatus> (static_cast<int> (status)); })
                   && std::abs (openq::responseMagnitudeDb (frame.response, 20000.0) - expectedDb) < 1e-4;
        }));
        if (juce::exactlyEqual (status, 0.0f))
            CHECK (worker.getFrame().bandCurves[0].isEmpty());
        else
        {
            const auto bandY = openq::responseMapping::dbToProportion (boostPerBand) * worker.getFrame().geometry.height;
            CHECK_THAT (worker.getFrame().bandCurves[0].getPointAlongPath (worker.getFrame().bandCurves[0].getLength()).y, Catch::Matchers::WithinAbs (bandY, 0.001));
        }
    }
    worker.stop();
}

TEST_CASE ("Response worker handles resize, invalid geometry, and repeated stop/restart", "[response][threading]")
{
    PluginProcessor processor;
    processor.prepareToPlay (8000.0, 64);
    openq::ResponseWorker worker (processor.parameters, processor.getResponseSampleRate());
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        worker.setGeometry ({ 400, 200 });
        REQUIRE (worker.start());
        worker.requestUpdate();
        REQUIRE (awaitFrame (worker, [] (const auto& frame) { return frame.valid && frame.geometry == openq::ResponseGeometry { 400, 200 }; }));
        const auto& frame = worker.getFrame();
        CHECK_THAT (frame.curve.getBounds().getWidth(), Catch::Matchers::WithinAbs (400.0, 1e-5));
        CHECK_THAT (frame.curve.getBounds().getY(), Catch::Matchers::WithinAbs (100.0, 1e-5));
        int points = 0;
        juce::Path::Iterator path (frame.curve);
        while (path.next())
            ++points;
        CHECK (points == 400);
        worker.setGeometry ({ 200, 100 });
        REQUIRE (awaitFrame (worker, [] (const auto& f) { return f.valid && f.geometry == openq::ResponseGeometry { 200, 100 }; }));
        worker.setGeometry ({ 0, 0 });
        REQUIRE (awaitFrame (worker, [] (const auto& f) { return !f.valid && f.geometry == openq::ResponseGeometry { 0, 0 }; }));
        worker.stop();
    }
}

TEST_CASE ("Target response waits for a prepared sample rate and follows state recall", "[response][threading]")
{
    PluginProcessor processor;
    openq::ResponseWorker worker (processor.parameters, processor.getResponseSampleRate());
    worker.setGeometry ({ 400, 200 });
    REQUIRE (worker.start());
    worker.requestUpdate();
    REQUIRE (awaitFrame (worker, [] (const auto& frame) { return !frame.valid; }));
    processor.prepareToPlay (48000.0, 64);
    worker.requestUpdate();
    REQUIRE (awaitFrame (worker, [] (const auto& frame) { return frame.valid; }));

    PluginProcessor preset;
    auto* gain = preset.parameters.getParameter ("band.0.gain");
    gain->setValueNotifyingHost (gain->convertTo0to1 (-9.0f));
    juce::MemoryBlock state;
    preset.getStateInformation (state);
    processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    worker.requestUpdate();
    REQUIRE (awaitFrame (worker, [] (const auto& frame) {
        return frame.valid && std::abs (openq::responseMagnitudeDb (frame.response, 1000.0) + 9.0) < 1e-5;
    }));
    worker.stop();
}

TEST_CASE ("Response editor can paint before audio starts", "[response][ui]")
{
    PluginProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
    REQUIRE (image.isValid());
    juce::MemoryOutputStream png;
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, png));
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openq-response-editor.png");
    REQUIRE (file.replaceWithData (png.getData(), png.getDataSize()));
}

TEST_CASE ("Graph mappings retain frequency endpoints and shelf midpoint behavior", "[response][mapping]")
{
    using namespace openq;
    CHECK_THAT (responseMapping::proportionToFrequency (0.0, 20000.0), Catch::Matchers::WithinAbs (20.0, 1e-12));
    CHECK_THAT (responseMapping::proportionToFrequency (1.0, 20000.0), Catch::Matchers::WithinAbs (20000.0, 1e-9));
    CHECK_THAT (responseMapping::frequencyToProportion (200.0, 20000.0), Catch::Matchers::WithinAbs (1.0 / 3.0, 1e-12));
    CHECK_THAT (responseMapping::maximumFrequency (8000.0), Catch::Matchers::WithinAbs (4000.0, 0.0));
    CHECK_THAT (bandLimits::effectiveMaximumFrequency (8000.0), Catch::Matchers::WithinAbs (3920.0, 0.0));
    CHECK_THAT (responseMapping::dbToProportion (24.0), Catch::Matchers::WithinAbs (0.0, 0.0));
    CHECK_THAT (responseMapping::dbToProportion (-24.0), Catch::Matchers::WithinAbs (1.0, 0.0));
    CHECK_THAT (responseMapping::proportionToDb (0.5f), Catch::Matchers::WithinAbs (0.0, 0.0));
    for (auto type : { FilterType::Bell, FilterType::LowShelf, FilterType::HighShelf })
    {
        const auto height = responseMapping::dbToProportion (12.0 * responseMapping::nodeGainScale (type));
        CHECK_THAT (height, Catch::Matchers::WithinAbs (type == FilterType::Bell ? 0.25 : 0.375, 0.0));
        CHECK_THAT (responseMapping::proportionToDb (height) / responseMapping::nodeGainScale (type), Catch::Matchers::WithinAbs (12.0, 0.0));
    }
}

TEST_CASE ("Cut target curves follow type slope and recall without processBlock", "[cuts][response][threading]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 64);
    openq::ResponseWorker worker (processor.parameters, processor.getResponseSampleRate());
    worker.setGeometry ({ 400, 200 });
    REQUIRE (worker.start());
    auto set = [&] (const char* name, float value) {
        auto* parameter = processor.parameters.getParameter (openq::bandParameterId (0, name));
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };
    for (auto type : { openq::FilterType::LowCut, openq::FilterType::HighCut })
        for (size_t index = 0; index < openq::cutOrders.size(); ++index)
        {
            set ("type", static_cast<float> (type));
            set ("slope", static_cast<float> (index));
            set ("q", static_cast<float> (openq::bandLimits::defaultCutQ));
            const auto settings = openq::getBandParameterPointers (processor.parameters, 0).load();
            const openq::BandResponse expected { openq::makeBandCoefficients (type, 48000.0, settings.frequency, settings.gain, settings.q, settings.slope), 48000.0 };
            const double probe = 800.0;
            worker.requestUpdate();
            REQUIRE (awaitFrame (worker, [&] (const auto& frame) {
                return frame.valid && frame.settings[0] == settings
                       && std::abs (openq::responseMagnitudeDb (frame.response, probe) - openq::responseMagnitudeDb (expected, probe)) < 1e-6;
            }));
        }
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    set ("slope", 0.0f);
    worker.requestUpdate();
    REQUIRE (awaitFrame (worker, [] (const auto& frame) { return frame.valid && frame.settings[0].slope == openq::CutSlope::Db6; }));
    processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    worker.requestUpdate();
    REQUIRE (awaitFrame (worker, [] (const auto& frame) { return frame.valid && frame.settings[0].slope == openq::CutSlope::Db96; }));
    set ("status", 1.0f);
    worker.requestUpdate();
    REQUIRE (awaitFrame (worker, [] (const auto& frame) {
        return frame.valid && frame.settings[0].status == openq::BandStatus::Bypass
               && std::abs (openq::responseMagnitudeDb (frame.response, 800.0)) < 1e-9 && !frame.bandCurves[0].isEmpty();
    }));
    worker.stop();
}

TEST_CASE ("Static artwork caches label margins without blending an image over the entire plot", "[response][ui][cache]")
{
    struct Scene : juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            g.fillAll (openq::palette::background);
            g.setColour (openq::palette::analyzerPost);
            g.fillRect (plot);
        }
        juce::Rectangle<int> plot { 52, 36, 644, 432 };
    } parent;
    openq::GraphArtwork artwork;
    parent.setSize (720, 500);
    parent.addAndMakeVisible (artwork);
    artwork.setBounds (parent.getLocalBounds());
    artwork.setDisplay (parent.plot, 48000.0, true);
    CHECK (artwork.getCachedComponentImage() == nullptr);
    CHECK_FALSE (artwork.hitTest (100, 100));
    int cachedArea = 0;
    for (auto* layer : artwork.getChildren())
    {
        REQUIRE (layer->getCachedComponentImage() != nullptr);
        CHECK_FALSE (layer->hitTest (0, 0));
        cachedArea += layer->getWidth() * layer->getHeight();
        if (layer->isOpaque())
            CHECK_FALSE (layer->getBounds().intersects (parent.plot));
    }
    CHECK (cachedArea < parent.getWidth() * parent.getHeight() / 8);
    auto draw = [&] { return parent.createComponentSnapshot (parent.getLocalBounds()); };
    auto equal = [] (const juce::Image& a, const juce::Image& b) {
        if (a.getBounds() != b.getBounds())
            return false;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                    return false;
        return true;
    };
    const auto first = draw();
    REQUIRE (first.isValid());
    CHECK (first.getPixelAt (parent.plot.getX() + 17, parent.plot.getY() + 25) == openq::palette::analyzerPost);
    parent.repaint();
    artwork.setDisplay (parent.plot, 48000.0, true);
    CHECK (equal (first, draw()));
    artwork.setDisplay (parent.plot, 8000.0, true);
    const auto lowRate = draw();
    CHECK_FALSE (equal (first, lowRate));
    artwork.setDisplay (parent.plot, 8000.0, false);
    CHECK_FALSE (equal (lowRate, draw()));
    artwork.setBounds (0, 0, 600, 450);
    artwork.setDisplay ({ 52, 36, 524, 382 }, 8000.0, false);
    artwork.sendLookAndFeelChange();
    REQUIRE (parent.createComponentSnapshot (parent.getLocalBounds(), true, 2.0f).isValid());
    const auto cached = draw();
    for (auto* layer : artwork.getChildren())
        layer->setBufferedToImage (false);
    const auto direct = draw();
    int difference = 0;
    for (int y = 0; y < cached.getHeight(); ++y)
        for (int x = 0; x < cached.getWidth(); ++x)
        {
            const auto a = cached.getPixelAt (x, y), b = direct.getPixelAt (x, y);
            difference = std::max ({ difference, std::abs (int (a.getRed()) - int (b.getRed())), std::abs (int (a.getGreen()) - int (b.getGreen())), std::abs (int (a.getBlue()) - int (b.getBlue())), std::abs (int (a.getAlpha()) - int (b.getAlpha())) });
        }
    INFO ("Maximum cached/direct channel difference: " << difference);
    CHECK (difference <= 2);
}

TEST_CASE ("Slow full graph paints leave time for input and recover without repaint bursts", "[response][ui][pacing]")
{
    openq::GraphRefreshLimiter limiter;
    REQUIRE (limiter.tick (0.0));
    limiter.recordPaintDuration (0.04, 0.04);
    CHECK_FALSE (limiter.tick (0.04));
    CHECK_FALSE (limiter.tick (0.079));
    CHECK (limiter.tick (0.08));
    int frames = 0;
    for (int tick = 1; tick <= 600; ++tick)
    {
        if (limiter.tick (0.08 + tick / 60.0))
        {
            ++frames;
            limiter.recordPaintDuration (0.04, 0.12 + tick / 60.0);
        }
    }
    CHECK (frames >= 100);
    CHECK (frames <= 125);
    // Removing fill or shrinking the editor allows fast painting to restore the normal cadence.
    for (int tick = 601; tick <= 900; ++tick)
        if (limiter.tick (0.08 + tick / 60.0))
            limiter.recordPaintDuration (0.001, 0.081 + tick / 60.0);
    frames = 0;
    for (int tick = 901; tick <= 960; ++tick)
        if (limiter.tick (0.08 + tick / 60.0))
            ++frames;
    CHECK (frames >= 28);
    CHECK (frames <= 30);
    limiter.recordPaintDuration (std::numeric_limits<double>::infinity(), 0.0);
    limiter.recordPaintDuration (-1.0, 0.0);
    limiter.recordPaintDuration (0.04, std::numeric_limits<double>::quiet_NaN());
    limiter.reset();
    CHECK (limiter.tick (0.0));
    CHECK (limiter.tick (1.0 / openq::GraphRefreshLimiter::targetFramesPerSecond));
}

TEST_CASE ("Pending gain feedback gets one eligible VBlank without bypassing paint budgets", "[response][ui][pacing][feedback]")
{
    openq::GraphRefreshLimiter limiter;
    CHECK_FALSE (limiter.tick (0.0, true));
    CHECK_FALSE (limiter.tick (0.0, true));
    CHECK (limiter.tick (0.016, true));
    limiter.recordPaintDuration (0.04, 0.056);
    CHECK_FALSE (limiter.tick (0.095, true));
    CHECK_FALSE (limiter.tick (0.096, true));
    CHECK (limiter.tick (0.112, true));
    limiter.reset();
    CHECK_FALSE (limiter.tick (0.0, true));
    // A painted readout releases priority immediately; no additional cooldown is imposed.
    CHECK (limiter.tick (0.016, false));
    limiter.reset();
    CHECK (limiter.tick (0.0));
}

TEST_CASE ("Continuous unpainted gain feedback cannot starve animation and normal cadence recovers", "[response][ui][pacing][feedback]")
{
    openq::GraphRefreshLimiter limiter;
    constexpr double monitorHz = 60.0, seconds = 10.0;
    int frames = 0;
    for (int tick = 0; tick < static_cast<int> (monitorHz * seconds); ++tick)
    {
        const auto timestamp = tick / monitorHz;
        if (limiter.tick (timestamp, true))
        {
            ++frames;
            limiter.recordPaintDuration (0.001, timestamp + 0.001);
        }
    }
    CHECK (frames >= static_cast<int> (seconds * openq::GraphRefreshLimiter::targetFramesPerSecond / 2.0));
    CHECK (frames <= static_cast<int> (seconds * openq::GraphRefreshLimiter::targetFramesPerSecond));
    frames = 0;
    for (int tick = 0; tick < static_cast<int> (monitorHz); ++tick)
        if (limiter.tick (seconds + tick / monitorHz))
            ++frames;
    CHECK (frames >= 28);
    CHECK (frames <= 30);
}

TEST_CASE ("Opaque editor bars do not disable expensive plot paint pacing", "[response][ui][pacing][regression]")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        SKIP ("Real peer paint pacing requires a display.");
    class SlowPlot final : public juce::Component
    {
    public:
        void paint (juce::Graphics&) override
        {
            const auto start = juce::Time::getMillisecondCounterHiRes();
            juce::Thread::sleep (45);
            lastCompleted = juce::Time::getMillisecondCounterHiRes();
            lastDuration = lastCompleted - start;
            paintMilliseconds += lastDuration;
            ++paints;
        }
        double paintMilliseconds = 0.0;
        double lastCompleted = 0.0, lastDuration = 0.0;
        int paints = 0;
    } slow;
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 64);
    PluginEditor editor (processor);
    auto* graph = dynamic_cast<openq::ResponseGraph*> (editor.findChildWithID ("response.graph"));
    REQUIRE (graph != nullptr);
    graph->addAndMakeVisible (slow);
    slow.setBounds (graph->getLocalBounds().withSizeKeepingCentre (20, 20));
    editor.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    editor.setVisible (true);
    const auto refreshMeters = graph->onFrameRefresh;
    REQUIRE (refreshMeters != nullptr);
    // No independent editor timer may consume peaks or repaint meters between admitted graph frames.
    graph->onFrameRefresh = nullptr;
    processor.getLevelMeters().publish ({ 0.8f, 0.4f }, { 0.6f, 0.3f });
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    CHECK (juce::exactlyEqual (processor.getLevelMeters().read().peaks[0][0], 0.8f));
    int budgetChecks = 0;
    graph->onFrameRefresh = [&] {
        if (slow.lastCompleted > 0.0)
        {
            const auto now = juce::Time::getMillisecondCounterHiRes();
            INFO ("Idle after paint=" << now - slow.lastCompleted << " ms, previous paint=" << slow.lastDuration << " ms");
            CHECK (now + 1.0 >= slow.lastCompleted + slow.lastDuration);
            ++budgetChecks;
        }
        refreshMeters();
    };
    auto* bottom = editor.findChildWithID ("editor.bottom");
    REQUIRE (bottom != nullptr);
    auto* inputGain = dynamic_cast<openq::GainControl*> (bottom->findChildWithID (openq::gainParameterIds[0]));
    auto* outputGain = dynamic_cast<openq::GainControl*> (bottom->findChildWithID (openq::gainParameterIds[1]));
    REQUIRE (inputGain != nullptr);
    REQUIRE (outputGain != nullptr);
    class MovingMeters final : private juce::Timer
    {
    public:
        explicit MovingMeters (openq::LevelMeterTransport& target) : transport (target) {}
        void start() { startTimerHz (30); }
    private:
        void timerCallback() override
        {
            phase += juce::MathConstants<float>::twoPi / 60.0f;
            const auto peak = 0.2f + 0.7f * (0.5f + 0.5f * std::sin (phase));
            transport.publish ({ peak, peak * 0.5f }, { peak * 0.7f, peak * 0.3f });
        }
        openq::LevelMeterTransport& transport;
        float phase = 0.0f;
    } moving (processor.getLevelMeters());
    juce::int64 eventTimestamp = 0;
    for (bool metersMoving : { false, true })
    {
        if (metersMoving)
            moving.start();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        slow.paints = 0;
        slow.paintMilliseconds = 0.0;
        const auto initialChecks = budgetChecks;
        // A finite input burst may repaint immediately; measure autonomous pacing after it settles.
        // Continuous synthetic edits at both footer ends can force native plot redraws outside that budget.
        for (auto* control : { inputGain, outputGain })
        {
            const auto before = control->getValue();
            const juce::Time time (++eventTimestamp);
            const juce::MouseEvent event { juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, control, control, time, {}, time, 0, false };
            control->mouseWheelMove (event, { 0.0f, 0.01f, false, true, false });
            CHECK (control->getValue() > before);
        }
        const auto start = juce::Time::getMillisecondCounterHiRes();
        while (budgetChecks - initialChecks < 3 && juce::Time::getMillisecondCounterHiRes() - start < 5000.0)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
        INFO ("moving=" << metersMoving << ", paints=" << slow.paints << ", paint_ms=" << slow.paintMilliseconds << ", elapsed_ms=" << elapsed);
        REQUIRE (budgetChecks - initialChecks >= 3);
        REQUIRE (slow.paints > 0);
        std::printf ("moving_meters=%d, plot paints=%d, controlled_paint_ms=%.2f, elapsed_ms=%.2f\n", static_cast<int> (metersMoving), slow.paints, slow.paintMilliseconds, elapsed);
        // Paint share is diagnostic: sleep overshoot and a paint crossing the sample boundary vary across CI backends.
    }
    graph->onFrameRefresh = refreshMeters;
    graph->removeChildComponent (&slow);
}

TEST_CASE ("Paints longer than the estimate cap still leave an input window", "[response][ui][pacing]")
{
    openq::GraphRefreshLimiter limiter;
    REQUIRE (limiter.tick (0.0));
    limiter.recordPaintDuration (0.3, 0.3);
    CHECK_FALSE (limiter.tick (0.3));
    CHECK_FALSE (limiter.tick (0.599));
    CHECK (limiter.tick (0.6));
}

TEST_CASE ("Paint completion anchors the input window even after queueing delay and skipped VBlanks", "[response][ui][pacing]")
{
    openq::GraphRefreshLimiter limiter;
    REQUIRE (limiter.tick (0.0));
    CHECK_FALSE (limiter.tick (0.01));
    // The requested paint starts late and finishes at 90 ms, with 40 ms of actual work.
    limiter.recordPaintDuration (0.04, 0.09);
    CHECK_FALSE (limiter.tick (0.129));
    CHECK (limiter.tick (0.13));
}

TEST_CASE ("An exposed plot reserves input time before the first VBlank", "[response][ui][pacing]")
{
    openq::GraphRefreshLimiter limiter;
    limiter.recordPaintDuration (0.04, 0.04);
    CHECK_FALSE (limiter.tick (0.079));
    CHECK (limiter.tick (0.08));
}
