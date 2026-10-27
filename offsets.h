// offsets.h
// عناوين وأوفسيتات خاصة بنسخة اللعبة عندك. القيم 0 = غير مضبوطة، والمود لن يضع hook عليها.
// تجدها بتحليل libGTASA.so في Ghidra ومقارنة البنية مع plugin-sdk (CPed / CPhysical).
// تأكد دائماً من النسخة (مثلاً 2.00 ARM32 أو 64-bit) فالقيم تختلف بين النسخ.
#pragma once
#include <cstdint>

namespace offs {

// --- دوال داخل libGTASA.so (الإزاحة عن قاعدة المكتبة، لا العنوان المطلق) ---
// دالة تُستدعى لكل شخصية كل إطار بعد تحديث الأنيميشن (مثلاً PreRender أو ProcessControl للـ CPed).
constexpr uintptr_t CPed_PreRender = 0x0;   // TODO

// --- حقول داخل كائن CPed (إزاحات بالبايت) ---
constexpr size_t PED_MOVE_SPEED   = 0x0;    // TODO: بداية CVector m_vecMoveSpeed (x,y,z)
constexpr size_t PED_FLAGS_ONGND  = 0x0;    // TODO: بايت/بت يدل على أنه واقف على الأرض
constexpr unsigned PED_ONGND_MASK = 0x0;    // TODO

inline bool Ready() {
    return CPed_PreRender && PED_MOVE_SPEED && PED_FLAGS_ONGND && PED_ONGND_MASK;
}

} // namespace offs
