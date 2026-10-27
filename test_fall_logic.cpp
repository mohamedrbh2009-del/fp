// اختبار بسيط للمنطق الرياضي على الكمبيوتر (بدون اللعبة).
#include <cassert>
#include <cstdio>
#include "../jni/fall_logic.h"

int main() {
    using namespace fall;

    // على الأرض: لا تأثير
    Pose a = ComputePose(0, 0, -1.0f, 1.0f, true);
    assert(a.intensity == 0.f);

    // سقوط بطيء جداً: لا تأثير
    Pose b = ComputePose(0, 0, -0.05f, 1.0f, false);
    assert(b.intensity == 0.f);

    // سقوط سريع: شدة عليا ومحدودة بـ 1
    Pose c = ComputePose(0.2f, 0.0f, -0.9f, 0.5f, false);
    assert(c.intensity == 1.f);
    assert(std::fabs(c.leftArm + c.rightArm) < 1e-5f); // الذراعان متعاكستان

    // تصنيف الهبوط
    assert(ClassifyLanding(-0.1f) == Landing::Soft);
    assert(ClassifyLanding(-0.4f) == Landing::Roll);
    assert(ClassifyLanding(-0.9f) == Landing::Collapse);

    std::puts("كل الاختبارات نجحت");
    return 0;
}
