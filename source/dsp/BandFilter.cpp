// SPDX-License-Identifier: AGPL-3.0-only
#include "BandFilter.h"
#include <cmath>

namespace openq
{
    void BandFilter::prepare (double newSampleRate, double frequencyHz, double gainDb, double q, BandStatus newStatus, FilterType newType)
    {
        sampleRate = std::isfinite (newSampleRate) && newSampleRate >= 1000.0 ? newSampleRate : 0.0;
        reset();
        if (sampleRate < 1000.0)
            return;

        frequency.reset (sampleRate, 0.02);
        quality.reset (sampleRate, 0.02);
        gain.reset (sampleRate, 0.02);
        wet.reset (sampleRate, 0.02);
        type = targetType = newType;
        setParameters (frequencyHz, gainDb, q, newStatus, newType);
        frequency.setCurrentAndTargetValue (frequency.getTargetValue());
        quality.setCurrentAndTargetValue (quality.getTargetValue());
        gain.setCurrentAndTargetValue (gain.getTargetValue());
        wet.setCurrentAndTargetValue (wet.getTargetValue());
        coefficients = makeFilterCoefficients (type, sampleRate, frequency.getCurrentValue(), gain.getCurrentValue(), quality.getCurrentValue());
        needsCoefficientRefresh = false;
    }

    void BandFilter::setParameters (double frequencyHz, double gainDb, double q, BandStatus newStatus, FilterType newType)
    {
        if (sampleRate < 1000.0)
            return;
        frequency.setTargetValue (juce::jlimit (20.0, std::min (20000.0, sampleRate * 0.49), std::isfinite (frequencyHz) ? frequencyHz : 1000.0));
        quality.setTargetValue (std::isfinite (q) ? juce::jlimit (0.1, 10.0, q) : 1.0);
        gain.setTargetValue (std::isfinite (gainDb) ? juce::jlimit (-24.0, 24.0, gainDb) : 0.0);
        status = newStatus;
        targetType = newType;
        wet.setTargetValue (type == targetType && status == BandStatus::On && !juce::exactlyEqual (gain.getTargetValue(), 0.0) ? 1.0 : 0.0);
    }

    void BandFilter::applyPendingType()
    {
        if (type == targetType || !juce::exactlyEqual (wet.getCurrentValue(), 0.0))
            return;
        // Change topology only while inaudible, then fade the new response back in.
        type = targetType;
        reset();
        coefficients = makeFilterCoefficients (type, sampleRate, frequency.getCurrentValue(), gain.getCurrentValue(), quality.getCurrentValue());
        needsCoefficientRefresh = false;
        wet.setTargetValue (status == BandStatus::On && !juce::exactlyEqual (gain.getTargetValue(), 0.0) ? 1.0 : 0.0);
    }

    void BandFilter::reset()
    {
        states = {};
    }

    void BandFilter::process (juce::AudioBuffer<float>& buffer, int numChannels)
    {
        if (sampleRate < 1000.0)
            return;
        numChannels = juce::jlimit (0, std::min (2, buffer.getNumChannels()), numChannels);
        applyPendingType();
        const bool keepWarm = status == BandStatus::Bypass;
        if (!keepWarm && !wet.isSmoothing() && juce::exactlyEqual (wet.getCurrentValue(), 0.0))
        {
            reset();
            frequency.skip (buffer.getNumSamples());
            quality.skip (buffer.getNumSamples());
            gain.skip (buffer.getNumSamples());
            needsCoefficientRefresh = true;
            return;
        }

        // Targets may have settled while dry without updating the coefficients.
        if (needsCoefficientRefresh)
        {
            coefficients = makeFilterCoefficients (type, sampleRate, frequency.getCurrentValue(), gain.getCurrentValue(), quality.getCurrentValue());
            needsCoefficientRefresh = false;
        }

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            if (frequency.isSmoothing() || quality.isSmoothing() || gain.isSmoothing())
                coefficients = makeFilterCoefficients (type, sampleRate, frequency.getNextValue(), gain.getNextValue(), quality.getNextValue());

            const auto mix = wet.getNextValue();
            applyPendingType();
            if (!keepWarm && !wet.isSmoothing() && juce::exactlyEqual (mix, 0.0))
            {
                // Fade out residual ringing before clearing the dry band's state.
                reset();
                frequency.skip (buffer.getNumSamples() - sample - 1);
                quality.skip (buffer.getNumSamples() - sample - 1);
                gain.skip (buffer.getNumSamples() - sample - 1);
                needsCoefficientRefresh = true;
                return;
            }

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto& state = states[static_cast<size_t> (channel)];
                const auto input = static_cast<double> (buffer.getSample (channel, sample));
                const auto output = coefficients.b0 * input + state.s1;
                state.s1 = coefficients.b1 * input - coefficients.a1 * output + state.s2;
                state.s2 = coefficients.b2 * input - coefficients.a2 * output;
                if (mix > 0.0)
                    buffer.setSample (channel, sample, static_cast<float> (input + mix * (output - input)));
            }
        }
    }
}
