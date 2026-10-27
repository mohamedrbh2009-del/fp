// main.cpp - مود AML بسيط: تأثير سقوط إجرائي (ليس Euphoria حقيقياً).
// ملاحظة: هذا هيكل لم يُجرَّب داخل اللعبة. أسماء واجهة AML هنا حسب ما هو معروف عن
// مشروع AndroidModLoader، فقارنها مع الهيدرات الفعلية في jni/mod/ بعد نسخها.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <time.h>

#include "fall_logic.h"
#include "offsets.h"

MYMOD(net.example.fallsoft, FallSoft, 0.1, YourName)

uintptr_t g_base = 0;
void (*PedPreRender_orig)(void* ped) = nullptr;

static float NowSeconds() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (float)ts.tv_sec + (float)ts.tv_nsec * 1e-9f;
}

// نقطة الـ hook: تُنفَّذ لكل شخصية.
void PedPreRender_hook(void* ped) {
    PedPreRender_orig(ped); // أولاً دع اللعبة تحدّث الأنيميشن كالمعتاد

    const uint8_t* base = (const uint8_t*)ped;
    const float* v = (const float*)(base + offs::PED_MOVE_SPEED);
    const bool onGround = (*(base + offs::PED_FLAGS_ONGND) & offs::PED_ONGND_MASK) != 0;

    fall::Pose pose = fall::ComputePose(v[0], v[1], v[2], NowSeconds(), onGround);
    if (pose.intensity <= 0.f) return;

    // TODO المرحلة 2: طبّق pose.leftArm / rightArm / spinePitch على عظام الشخصية.
    // هذا يتطلب الوصول لـ RpHAnimHierarchy وتعديل مصفوفات العظام بعد الأنيميشن وقبل الرسم.
    // للبداية: سجّل القيم فقط للتأكد أن كشف السقوط يعمل.
    static int counter = 0;
    if ((++counter % 60) == 0) {
        logger->Info("fall intensity=%.2f L=%.2f R=%.2f", pose.intensity, pose.leftArm, pose.rightArm);
    }
}

extern "C" void OnModLoad() {
    logger->SetTag("FallSoft");

    if (!offs::Ready()) {
        logger->Error("offsets.h غير مضبوط: لن يتم وضع أي hook. عبّئ القيم لنسختك أولاً.");
        return;
    }

    g_base = aml->GetLib("libGTASA.so");
    if (!g_base) {
        logger->Error("تعذر الحصول على قاعدة libGTASA.so");
        return;
    }

    aml->Hook((void*)(g_base + offs::CPed_PreRender),
              (void*)PedPreRender_hook,
              (void**)&PedPreRender_orig);
    logger->Info("تم وضع hook السقوط");
}
