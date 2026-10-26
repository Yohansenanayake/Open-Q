// SPDX-License-Identifier: AGPL-3.0-only
#include "BellFilter.h"
#include <cmath>

namespace openq
{
    void BellFilter::prepare (double newSampleRate, double frequencyHz, double gainDb, double q, bool enabled)
    {
        sampleRate = std::isfinite (newSampleRate) && newSampleRate >= 1000.0 ? newSampleRate : 0.0;
        reset();
        if (sampleRate < 1000.0)
            return;

        frequency.reset (sampleRate, 0.02);
        quality.reset (sampleRate, 0.02);
        gain.reset (sampleRate, 0.02);
        wet.reset (sampleRate, 0.02);
        setParameters (frequencyHz, gainDb, q, enabled);
        frequency.setCurrentAndTargetValue (frequency.getTargetValue());
        quality.setCurrentAndTargetValue (quality.getTargetValue());
        gain.setCurrentAndTargetValue (gain.getTargetValue());
        wet.setCurrentAndTargetValue (wet.getTargetValue());
        coefficients = makeBellCoefficients (sampleRate, frequency.getCurrentValue(), gain.getCurrentValue(), quality.getCurrentValue());
    }

    void BellFilter::setParameters (double frequencyHz, double gainDb, double q, bool enabled)
    {
        if (sampleRate < 1000.0)
            return;
        frequency.setTargetValue (juce::jlimit (20.0, std::min (20000.0, sampleRate * 0.49), std::isfinite (frequencyHz) ? frequencyHz : 1000.0));
        quality.setTargetValue (std::isfinite (q) ? juce::jlimit (0.1, 10.0, q) : 1.0);
        gain.setTargetValue (std::isfinite (gainDb) ? juce::jlimit (-24.0, 24.0, gainDb) : 0.0);
        wet.setTargetValue (enabled && !juce::exactlyEqual (gain.getTargetValue(), 0.0) ? 1.0 : 0.0);
    }

    void BellFilter::reset()
    {
        states = {};
    }

    void BellFilter::process (juce::AudioBuffer<float>& buffer, int numChannels)
    {
        if (sampleRate < 1000.0)
            return;
        numChannels = juce::jlimit (0, std::min (2, buffer.getNumChannels()), numChannels);
        if (!wet.isSmoothing() && juce::exactlyEqual (wet.getCurrentValue(), 0.0))
        {
            reset();
            frequency.skip (buffer.getNumSamples());
            quality.skip (buffer.getNumSamples());
            gain.skip (buffer.getNumSamples());
            return;
        }

        // Targets may have settled while dry without updating the coefficients.
        if (wet.isSmoothing() && juce::exactlyEqual (wet.getCurrentValue(), 0.0))
            coefficients = makeBellCoefficients (sampleRate, frequency.getCurrentValue(), gain.getCurrentValue(), quality.getCurrentValue());

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            if (frequency.isSmoothing() || quality.isSmoothing() || gain.isSmoothing())
                coefficients = makeBellCoefficients (sampleRate, frequency.getNextValue(), gain.getNextValue(), quality.getNextValue());

            const auto mix = wet.getNextValue();
            if (!wet.isSmoothing() && juce::exactlyEqual (mix, 0.0))
            {
                // Fade out residual ringing before clearing the dry band's state.
                reset();
                frequency.skip (buffer.getNumSamples() - sample - 1);
                quality.skip (buffer.getNumSamples() - sample - 1);
                gain.skip (buffer.getNumSamples() - sample - 1);
                return;
            }

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto& state = states[static_cast<size_t> (channel)];
                const auto input = static_cast<double> (buffer.getSample (channel, sample));
                const auto output = coefficients.b0 * input + state.s1;
                state.s1 = coefficients.b1 * input - coefficients.a1 * output + state.s2;
                state.s2 = coefficients.b2 * input - coefficients.a2 * output;
                buffer.setSample (channel, sample, static_cast<float> (input + mix * (output - input)));
            }
        }
    }
}
