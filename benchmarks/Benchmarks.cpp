TEST_CASE ("Boot performance")
{
    BENCHMARK_ADVANCED ("Processor constructor")
    (Catch::Benchmark::Chronometer meter)
    {
        std::vector<Catch::Benchmark::storage_for<PluginProcessor>> storage (size_t (meter.runs()));
        meter.measure ([&] (int i) { storage[(size_t) i].construct(); });
    };

    BENCHMARK_ADVANCED ("Processor destructor")
    (Catch::Benchmark::Chronometer meter)
    {
        std::vector<Catch::Benchmark::destructable_object<PluginProcessor>> storage (size_t (meter.runs()));
        for (auto& s : storage)
            s.construct();
        meter.measure ([&] (int i) { storage[(size_t) i].destruct(); });
    };

    BENCHMARK_ADVANCED ("Editor open and close")
    (Catch::Benchmark::Chronometer meter)
    {
        PluginProcessor plugin;

        // due to complex construction logic of the editor, let's measure open/close together
        meter.measure ([&] (int /* i */) {
            auto editor = plugin.createEditorAndMakeActive();
            plugin.editorBeingDeleted (editor);
            delete editor;
            return plugin.getActiveEditor();
        });
    };
}

TEST_CASE ("Oversampling processing cost", "[oversampling]")
{
    for (const int blockSize : { 256, 128, 64 })
        for (int mode = 0; mode < openq::oversamplingModeCount; ++mode)
            for (int workload = 0; workload < 3; ++workload)
            {
                PluginProcessor processor;
                processor.setOversamplingMode (mode);
                const auto set = [&] (const juce::String& id, float value) {
                    auto* parameter = processor.parameters.getParameter (id);
                    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
                };
                const auto count = workload == 0 ? size_t { 6 } : openq::bandCount;
                for (size_t band = 0; band < count; ++band)
                {
                    set (openq::bandParameterId (band, "status"), 2.0f);
                    set (openq::bandParameterId (band, "frequency"), 100.0f + static_cast<float> (band) * 100.0f);
                    set (openq::bandParameterId (band, "gain"), 3.0f);
                    if (workload != 0)
                    {
                        set (openq::bandParameterId (band, "type"), static_cast<float> (openq::FilterType::LowCut));
                        set (openq::bandParameterId (band, "slope"), static_cast<float> (openq::cutSlopeNames.size() - 1));
                    }
                }
                std::array<std::atomic<float>*, openq::bandCount> frequencies {};
                for (size_t band = 0; band < count; ++band)
                    frequencies[band] = processor.parameters.getRawParameterValue (openq::bandParameterId (band, "frequency"));
                processor.prepareToPlay (48000, blockSize);
                juce::AudioBuffer<float> buffer (2, blockSize);
                juce::MidiBuffer midi;
                int iteration = 0;
                auto name = std::to_string (blockSize) + " samples, " + std::to_string (1 << mode) + "x (" + std::to_string (processor.getLatencySamples()) + " samples) " + (workload == 0 ? "six bells" : workload == 1 ? "all steep cuts"
                                                                                                                                                                                                                          : "automated steep cuts");
                BENCHMARK (std::move (name))
                {
                    for (int channel = 0; channel < 2; ++channel)
                        juce::FloatVectorOperations::fill (buffer.getWritePointer (channel), 0.01f, buffer.getNumSamples());
                    if (workload == 2)
                        for (size_t band = 0; band < count; ++band)
                            frequencies[band]->store (100.0f + static_cast<float> (band) * 100.0f + static_cast<float> (iteration & 1), std::memory_order_relaxed);
                    ++iteration;
                    processor.processBlock (buffer, midi);
                    return buffer.getSample (0, 0);
                };
            }
}
