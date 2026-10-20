#pragma once

#include "merchant_dispatch_model.hpp"
#include "merchant_painter.hpp"

namespace cps::gui::merchant {

struct DispatchDemoSelection {
    int policy = 1;
    QString order;
    cps::TimeMs nowMs = 0;
    bool playing = false;
    int orderOffset = 0;
    bool following = false;
    QString sourceStatus;
};

// Paints the body only. The window owns navigation, footer, combo boxes and clock.
void paintDispatch(Painter& d, const DispatchDemo& demo,
                   const DispatchDemoSelection& selection);

} // namespace cps::gui::merchant
