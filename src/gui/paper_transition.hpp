#pragma once
#include <QSignalBlocker>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>

namespace cps::gui {
// A finite, velocity-continuous transition for frequently reversed hover motion.
// Cubic Hermite interpolation carries the incoming tangent and settles at rest.
class PaperTransition : public QVariantAnimation {
public:
    explicit PaperTransition(QObject *parent) : QVariantAnimation(parent) {
        setEasingCurve(QEasingCurve::Linear); // interpolation below owns the curve
        setDuration(240);
    }
    qreal velocity() const {
        if (state() != Running || duration() <= 0) return 0.;
        const qreal t = qBound(0., currentTime() / qreal(duration()), 1.);
        const qreal seconds = duration() / 1000.;
        const qreal position = evaluate(t);
        if (position <= 0. || position >= 1.) return 0.;
        return ((6*t*t - 6*t) * startValue().toReal()
                + (3*t*t - 4*t + 1) * incomingVelocity_ * seconds
                + (-6*t*t + 6*t) * endValue().toReal()) / seconds;
    }
    void retarget(qreal position, qreal target) {
        const qreal incoming = velocity();
        const QSignalBlocker configuring(this);
        stop();
        incomingVelocity_ = incoming;
        setDuration(std::max(120, qRound(240 * std::abs(target - position))));
        setStartValue(position);
        setEndValue(target);
        start();
    }
protected:
    QVariant interpolated(const QVariant &, const QVariant &, qreal progress) const override {
        return qBound(0., evaluate(progress), 1.);
    }
private:
    qreal evaluate(qreal t) const {
        const qreal start = startValue().toReal(), end = endValue().toReal();
        return (2*t*t*t - 3*t*t + 1) * start
               + (t*t*t - 2*t*t + t) * incomingVelocity_ * duration() / 1000.
               + (-2*t*t*t + 3*t*t) * end;
    }
    qreal incomingVelocity_ = 0.;
};
} // namespace cps::gui
