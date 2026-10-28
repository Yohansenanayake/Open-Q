// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "AnalyzerWorker.h"
#include "GraphArtwork.h"
#include "GraphRefreshLimiter.h"
#include "ResponseWorker.h"

namespace openq
{
    class ResponseGraph : public juce::Component, private juce::Timer
    {
    public:
        ResponseGraph (juce::AudioProcessorValueTreeState& parameters, const std::atomic<double>& sampleRate);
        ~ResponseGraph() override;
        void paint (juce::Graphics& graphics) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;
        void parentHierarchyChanged() override;
        void enablementChanged() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void setSelectedBand (size_t band);
        size_t getSelectedBand() const { return selectedBand; }
        void cancelDrag();
        void setUiScale (float scale);
        void setGloballyBypassed (bool);
        void setContentInsets (juce::BorderSize<int>);
        void deleteSelectedBand();
        void setAnalyzerTransport (AnalyzerTransport& transport);
        juce::Rectangle<int> getSelectedNodeBounds() const;
        std::function<void (size_t)> onBandSelected;
        std::function<void()> onNodeLayoutChanged;
        std::function<void()> onFrameRefresh;
        std::function<bool()> hasPendingControlFeedback;

    private:
        class Node;
        class ResponseLayer;
        void paintResponse (juce::Graphics&);
        bool globallyBypassed = false;
        float uiScale = 1.0f;
        juce::BorderSize<int> contentInsets;
        GraphArtwork artwork;
        std::unique_ptr<ResponseLayer> responseLayer;
        void timerCallback() override;
        void updateVisibility();
        void updateNodes();
        void chooseBand (size_t band);
        void beginDrag (size_t band, const juce::MouseEvent&);
        void drag (const juce::MouseEvent&);
        void wheel (size_t band, const juce::MouseWheelDetails&, const juce::ModifierKeys&);
        void toggleBypass (size_t band);
        void deleteBand (size_t band);
        void setParameter (size_t band, const char* name, float value);
        juce::Point<float> nodePosition (const BandSettings&) const;
        juce::Point<float> constrainPosition (juce::Point<float>, FilterType type = FilterType::Bell) const;
        double maximumFrequency() const;
        juce::Rectangle<int> plotBounds() const;
        juce::AudioProcessorValueTreeState& parameters;
        const std::atomic<double>& sampleRate;
        std::array<BandParameterPointers, bandCount> bandParameters;
        std::array<std::unique_ptr<Node>, bandCount> nodes;
        size_t selectedBand = 0, draggedBand = bandCount;
        std::array<juce::RangedAudioParameter*, 2> dragParameters {};
        juce::Point<float> dragPosition, lastPointer;
        double dragSampleRate = 0.0;
        FilterType dragType = FilterType::Bell;
        double displaySampleRate = 0.0;
        ResponseWorker worker;
        std::unique_ptr<AnalyzerWorker> analyzer;
        AnalyzerTransport* analyzerTransport = nullptr;
        std::unique_ptr<juce::VBlankAttachment> vblank;
        GraphRefreshLimiter refreshLimiter;
        double paintStartedMilliseconds = 0.0;
    };
}
