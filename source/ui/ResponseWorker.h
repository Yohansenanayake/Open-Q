// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../dsp/AppliedResponse.h"
#include "../utility/TripleBuffer.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace openq
{
    double responseMagnitudeDb (const AppliedResponse& response, double frequencyHz);

    struct ResponseGeometry
    {
        int width = 0, height = 0;
        bool operator== (const ResponseGeometry&) const = default;
    };

    struct ResponseFrame
    {
        juce::Path curve;
        AppliedResponse response;
        ResponseGeometry geometry;
        bool valid = false;
    };

    class ResponseWorker : private juce::Thread
    {
    public:
        explicit ResponseWorker (TripleBuffer<AppliedResponse>& snapshots);
        ~ResponseWorker() override;
        bool start();
        void stop();
        void requestUpdate();
        void setGeometry (ResponseGeometry geometry);
        bool pullFrame();
        const ResponseFrame& getFrame() const;

    private:
        void run() override;
        TripleBuffer<AppliedResponse>& snapshots;
        TripleBuffer<ResponseGeometry> geometry;
        TripleBuffer<ResponseFrame> frames;
    };
}
