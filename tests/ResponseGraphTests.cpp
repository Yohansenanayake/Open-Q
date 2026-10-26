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

TEST_CASE ("Response magnitude includes digital coefficients and complex wet/dry mixing", "[response]")
{
    openq::AppliedResponse response { openq::makeBellCoefficients (48000.0, 20000.0, 6.0, 1.0), 1.0, 48000.0 };
    CHECK_THAT (openq::responseMagnitudeDb (response, 20000.0), Catch::Matchers::WithinAbs (6.0, 1e-9));
    CHECK_THAT (openq::responseMagnitudeDb (response, 24000.0), Catch::Matchers::WithinAbs (5.83299135367, 1e-9));
    response.wet = 0.0;
    CHECK_THAT (openq::responseMagnitudeDb (response, 20000.0), Catch::Matchers::WithinAbs (0.0, 0.0));
    response = { { 2.0, 0.0, 0.0, 0.0, 0.0 }, 0.5, 48000.0 };
    CHECK_THAT (openq::responseMagnitudeDb (response, 1000.0), Catch::Matchers::WithinAbs (20.0 * std::log10 (1.5), 1e-9));
    response.coefficients = { 0.0, 1.0, 0.0, 0.0, 0.0 };
    CHECK_THAT (openq::responseMagnitudeDb (response, 24000.0), Catch::Matchers::WithinAbs (-240.0, 1e-9));
}

TEST_CASE ("Processor publishes applied snapshots including flat and fading branches", "[response]")
{
    PluginProcessor processor;
    auto& mailbox = processor.getResponseSnapshots();
    auto* gain = processor.parameters.getParameter ("bell.gain");
    gain->setValueNotifyingHost (gain->convertTo0to1 (12.0f));
    processor.prepareToPlay (48000.0, 64);
    CHECK_FALSE (mailbox.pull());
    juce::AudioBuffer<float> buffer (2, 64);
    buffer.clear();
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);
    REQUIRE (mailbox.pull());
    const auto boosted = mailbox.getReader();
    CHECK_THAT (boosted.wet, Catch::Matchers::WithinAbs (1.0, 0.0));
    CHECK_THAT (boosted.sampleRate, Catch::Matchers::WithinAbs (48000.0, 0.0));
    CHECK_THAT (openq::responseMagnitudeDb (boosted, 1000.0), Catch::Matchers::WithinAbs (12.0, 1e-5));
    processor.processBlock (buffer, midi);
    CHECK_FALSE (mailbox.pull());
    gain->setValueNotifyingHost (gain->convertTo0to1 (0.0f));
    processor.processBlock (buffer, midi);
    REQUIRE (mailbox.pull());
    const auto fading = mailbox.getReader();
    CHECK (fading.wet > 0.0);
    CHECK (fading.wet < 1.0);
    CHECK (openq::responseMagnitudeDb (fading, 1000.0) > 1.0);
    for (int i = 0; i < 20; ++i)
        processor.processBlock (buffer, midi);
    REQUIRE (mailbox.pull());
    CHECK_THAT (mailbox.getReader().wet, Catch::Matchers::WithinAbs (0.0, 0.0));
    CHECK_THAT (openq::responseMagnitudeDb (mailbox.getReader(), 1000.0), Catch::Matchers::WithinAbs (0.0, 0.0));
}

TEST_CASE ("Response worker handles resize, invalid geometry, and repeated stop/restart", "[response][threading]")
{
    openq::TripleBuffer<openq::AppliedResponse> snapshots;
    snapshots.getWriter() = { {}, 0.0, 8000.0 };
    snapshots.publish();
    openq::ResponseWorker worker (snapshots);
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

TEST_CASE ("Response worker waits for prepared audio and accepts the latest publication", "[response][threading]")
{
    openq::TripleBuffer<openq::AppliedResponse> snapshots;
    openq::ResponseWorker worker (snapshots);
    worker.setGeometry ({ 400, 200 });
    REQUIRE (worker.start());
    worker.requestUpdate();
    REQUIRE (awaitFrame (worker, [] (const auto& frame) { return !frame.valid; }));
    snapshots.getWriter() = { openq::makeBellCoefficients (48000.0, 1000.0, 6.0, 1.0), 1.0, 48000.0 };
    snapshots.publish();
    worker.requestUpdate();
    REQUIRE (awaitFrame (worker, [] (const auto& frame) { return frame.valid; }));
    CHECK_THAT (openq::responseMagnitudeDb (worker.getFrame().response, 1000.0), Catch::Matchers::WithinAbs (6.0, 1e-9));
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
