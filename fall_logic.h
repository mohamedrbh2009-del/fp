// fall_logic.h
// الجزء الرياضي فقط (لا يعتمد على اللعبة ولا على AML) ليمكن اختباره على الكمبيوتر.
#pragma once
#include <cmath>
#include <algorithm>

namespace fall {

struct Params {
    float minFallSpeed   = 0.10f;  // أقل سرعة سقوط (وحدات اللعبة/إطار) لبدء التأثير
    float intensityScale = 3.0f;   // تحويل السرعة إلى شدة (0..1)
    float swingFreq      = 12.0f;  // تردد تأرجح الذراعين (راديان/ثانية)
    float armSwing       = 0.8f;   // أقصى زاوية تأرجح للذراعين (راديان)
    float spineTilt      = 0.3f;   // أقصى ميلان للجذع (راديان)
};

struct Pose {
    float leftArm   = 0.f;
    float rightArm  = 0.f;
    float spinePitch = 0.f;
    float spineRoll  = 0.f;
    float intensity  = 0.f;
};

// vz: السرعة العمودية (سالبة = سقوط). vx, vy: السرعة الأفقية.
// time: الزمن بالثواني. onGround: هل الشخصية على الأرض.
inline Pose ComputePose(float vx, float vy, float vz, float time, bool onGround,
                        const Params& p = Params()) {
    Pose out;
    if (onGround || vz > -p.minFallSpeed) return out;

    out.intensity = std::clamp((-vz) * p.intensityScale, 0.f, 1.f);
    const float phase = time * p.swingFreq;
    out.leftArm  = std::sin(phase) * p.armSwing * out.intensity;
    out.rightArm = std::sin(phase + 3.14159265f) * p.armSwing * out.intensity;

    // الجذع يميل باتجاه الحركة الأفقية (تقريب بسيط)
    const float h = std::sqrt(vx * vx + vy * vy);
    const float dir = (h > 1e-4f) ? std::clamp(h * 4.f, 0.f, 1.f) : 0.f;
    out.spinePitch = p.spineTilt * out.intensity * dir;
    out.spineRoll  = p.spineTilt * 0.5f * out.intensity * std::sin(phase * 0.5f);
    return out;
}

// نوع الهبوط حسب سرعة السقوط لحظة لمس الأرض.
enum class Landing { Soft, Roll, Collapse };

inline Landing ClassifyLanding(float impactVz) {
    const float s = -impactVz;
    if (s < 0.25f) return Landing::Soft;
    if (s < 0.55f) return Landing::Roll;
    return Landing::Collapse;
}

} // namespace fall
