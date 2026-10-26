#include "PluginEditor.h"
#include "ui/ResponseMapping.h"
#include "ui/ResponseWorker.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <chrono>
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
