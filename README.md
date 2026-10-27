# FallSoft — مود سقوط إجرائي لـ GTA San Andreas (موبايل)

مود بسيط لـ AML يضيف تأثير سقوط (تأرجح الذراعين، ميلان الجذع، تصنيف الهبوط).
**هذا ليس Euphoria حقيقياً**: اللعبة الأصلية لا تملك ragdoll فيزيائياً، فالمود طبقة أنيميشن إجرائية فوق الموجود.

> للاستخدام الشخصي في اللعبة أوفلاين فقط. لا تستخدمه في خوادم أونلاين (SAMP/MTA وما شابه).

## الحالة الحالية
| الجزء | الحالة |
|---|---|
| منطق التأثير (`jni/fall_logic.h`) | جاهز ومختبَر على الكمبيوتر |
| الـ hook وهيكل AML (`jni/main.cpp`) | هيكل لم يُجرَّب داخل اللعبة |
| العناوين والأوفسيتات (`jni/offsets.h`) | **فارغة، عليك تعبئتها لنسختك** |
| تعديل العظام (المرحلة 2) | غير منجز (TODO) |

## البناء
1. ارفع المشروع إلى GitHub. الـ workflow يشغّل الاختبارات ثم يبني `.so` لـ armeabi-v7a و arm64-v8a ويرفعه كـ artifact.
2. أو محلياً: ثبّت Android NDK ثم:
   ```
   git clone --depth 1 https://github.com/RusJJ/AndroidModLoader.git /tmp/aml
   mkdir -p jni/mod && cp -r /tmp/aml/mod/* jni/mod/
   ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk NDK_APPLICATION_MK=jni/Application.mk
   ```
3. تحقق من أن أسماء واجهة AML في `main.cpp` (`MYMOD`, `aml->Hook`, `aml->GetLib`, `logger`) تطابق الهيدرات الفعلية في `jni/mod/`، فقد تختلف بين الإصدارات.

## تعبئة `offsets.h`
1. استخرج `libGTASA.so` من نسختك من اللعبة وافتحه في **Ghidra**.
2. اعثر على دالة تحديث الشخصية (PreRender/ProcessControl لـ CPed) بمقارنة البنية مع **plugin-sdk**.
3. اعثر على إزاحة `m_vecMoveSpeed` وعلامة "على الأرض" داخل CPed.
4. ضع القيم في `offsets.h`. ما دامت صفراً، يرفض المود وضع أي hook ويكتب خطأ في السجل.

## خطوات التطوير المقترحة
1. تشغيل المود بالتسجيل فقط (logcat) والتأكد أن كشف السقوط يعمل.
2. استبدال أنيميشن الهبوط حسب `ClassifyLanding`.
3. تعديل عظام الذراعين والجذع (يتطلب RpHAnimHierarchy).

## تشغيل اختبار المنطق محلياً
```
g++ -std=c++17 -o test_fall tests/test_fall_logic.cpp && ./test_fall
```
