// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../EditorSize.h"
namespace openq
{
    struct UiScale
    {
        explicit UiScale (int width) : card (EditorSize::scaleForWidth (width)) {}
        float card;
        float header() const { return grow (0.7f); }
        float bottom() const { return grow (0.4f); }
        float graph() const { return grow (0.6f); }

    private:
        float grow (float factor) const { return 1.0f + (card - 1.0f) * factor; }
    };
}
