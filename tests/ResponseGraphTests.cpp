#include "PluginEditor.h"
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
    openq::DigitalResponse response { openq::makeBellCoefficients (48000.0, 20000.0, 6.0, 1.0), 48000.0 };
    CHECK_THAT (openq::responseMagnitudeDb (response, 20000.0), Catch::Matchers::WithinAbs (6.0, 1e-9));
    CHECK_THAT (openq::responseMagnitudeDb (response, 24000.0), Catch::Matchers::WithinAbs (5.83299135367, 1e-9));
    response.coefficients = openq::makeBellCoefficients (48000.0, 1000.0, 0.0, 1.0);
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
        const openq::DigitalResponse expected { openq::makeBellCoefficients (48000.0, target.frequency, target.gain, target.q), 48000.0 };
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
    const openq::DigitalResponse clamped { openq::makeBellCoefficients (8000.0, 3920.0, 24.0, 0.1), 8000.0 };
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
            set (band - 1, "enabled", 0.0f);
        const auto frequency = 1000.0f + static_cast<float> (band) * 200.0f;
        const auto gain = 3.0f + static_cast<float> (band);
        const auto q = 1.0f + static_cast<float> (band) * 0.2f;
        set (band, "frequency", frequency);
        set (band, "gain", gain);
        set (band, "q", q);
        set (band, "enabled", 1.0f);
        const openq::DigitalResponse expected { openq::makeBellCoefficients (48000.0, frequency, gain, q), 48000.0 };
        const auto probe = frequency * 0.8;
        worker.requestUpdate();
        REQUIRE (awaitFrame (worker, [&] (const auto& frame) {
            return frame.valid && std::abs (openq::responseMagnitudeDb (frame.response, probe) - openq::responseMagnitudeDb (expected, probe)) < 1e-4;
        }));
    }
    for (size_t band = 0; band < openq::bandCount; ++band)
    {
        set (band, "frequency", 20000.0f);
        set (band, "gain", 24.0f);
        set (band, "q", 1.0f);
        set (band, "enabled", 1.0f);
    }
    worker.requestUpdate();
    REQUIRE (awaitFrame (worker, [] (const auto& frame) {
        return frame.valid && std::abs (openq::responseMagnitudeDb (frame.response, 20000.0) - 192.0) < 1e-4;
    }));
    CHECK_THAT (worker.getFrame().curve.getPointAlongPath (worker.getFrame().curve.getLength()).y, Catch::Matchers::WithinAbs (0.0, 1e-4));

    // Seven boosts exceed the display range before the final cut brings the endpoint to +18 dB.
    for (size_t band = 0; band < openq::bandCount; ++band)
        set (band, "gain", band == 7 ? -24.0f : 6.0f);
    worker.requestUpdate();
    REQUIRE (awaitFrame (worker, [] (const auto& frame) {
        return frame.valid && std::abs (openq::responseMagnitudeDb (frame.response, 20000.0) - 18.0) < 1e-4;
    }));
    CHECK_THAT (worker.getFrame().curve.getPointAlongPath (worker.getFrame().curve.getLength()).y, Catch::Matchers::WithinAbs (25.0, 0.001));
    for (size_t band = 0; band < openq::bandCount; ++band)
        set (band, "enabled", 0.0f);
    worker.requestUpdate();
    REQUIRE (awaitFrame (worker, [] (const auto& frame) {
        return frame.valid && std::abs (openq::responseMagnitudeDb (frame.response, 20000.0)) < 1e-9;
    }));
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
