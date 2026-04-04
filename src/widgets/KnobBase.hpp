#pragma once
#include <rack.hpp>
#include "KnobStyles.hpp"

using namespace rack;

namespace madzine {
namespace widgets {

class BaseCustomKnob : public app::Knob {
protected:
    NVGcolor baseColor = KnobColors::BLACK_BASE;
    NVGcolor centerColor = KnobColors::BLACK_CENTER;
    NVGcolor borderColor = KnobColors::GRAY_BORDER;
    NVGcolor indicatorColor = KnobColors::WHITE_INDICATOR;
    float indicatorMargin = KnobSizes::INDICATOR_MARGIN;
    bool enableDoubleClickReset = true;
    float cvModulation = 0.0f;
    bool modulationEnabled = false;
    NVGcolor modPositiveColor = KnobColors::MOD_POSITIVE;
    NVGcolor modNegativeColor = KnobColors::MOD_NEGATIVE;
    float modIndicatorWidth = 1.5f;

public:
    BaseCustomKnob() : app::Knob() {
        box.size = Vec(KnobSizes::STANDARD, KnobSizes::STANDARD);
        speed = KnobSensitivity::SLOW;
        snap = false;
    }

    void initParamQuantity() override {
        app::Knob::initParamQuantity();
    }

    float getDisplayAngle() {
        ParamQuantity* pq = getParamQuantity();
        if (!pq) return 0.0f;
        float normalizedValue = pq->getScaledValue();
        float angle = rescale(normalizedValue, 0.0f, 1.0f,
                            KnobAngles::MIN_ANGLE, KnobAngles::MAX_ANGLE);
        return angle;
    }

    void setModulation(float normalizedMod) {
        cvModulation = clamp(normalizedMod, -1.0f, 1.0f);
    }

    void setModulationEnabled(bool enabled) {
        modulationEnabled = enabled;
    }

    bool isModulationEnabled() const {
        return modulationEnabled;
    }

    float getModulatedAngle() {
        float baseAngle = getDisplayAngle();
        float modRange = KnobAngles::MAX_ANGLE - KnobAngles::MIN_ANGLE;
        float modAngle = baseAngle + cvModulation * modRange;
        return clamp(modAngle, KnobAngles::MIN_ANGLE, KnobAngles::MAX_ANGLE);
    }

    virtual void drawKnob(const DrawArgs& args, float radius)
