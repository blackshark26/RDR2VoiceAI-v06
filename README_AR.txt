RDR2VoiceAI v0.6 BARGE-IN MEMORY
===================================

الميزة الرئيسية:
تقدر تقاطع NPC وهو كيهضر.

السلوك:
NPC كيهضر
-> كتشد U
-> الصوت ديال NPC كيتوقف فوراً
-> نفس Gemini Live session كتبقى مفتوحة
-> الميكروفون ديالك كيبدا مباشرة
-> كتقول كلامك
-> كتطلق U
-> NPC كيجيب على كلامك الجديد وهو باقي فاكر الحوار السابق

مهم:
المقاطعة ما كتديرش:
- close_session
- reconnect عادي
- reset للمحادثة
- حذف language lock
- حذف history

إعادة الاتصال كتوقع غير إلا websocket مات فعلاً أو تبدلت شخصية/NPC context بطريقة كتفرض Session جديدة.

v0.5 FEATURES مازالين موجودين:
- Continuous microphone stream
- Male NPC -> male_voice (Orus افتراضياً)
- Female NPC -> female_voice (Kore افتراضياً)
- أقرب Human NPC حقيقي
- NPC كيوقف ويواجه اللاعب ويتبعو بالنظر
- Modular folders:
  Prompts
  Languages
  Personalities
  World
  Actions

CONFIG:
"barge_in_enabled": true
"barge_in_keep_session_memory": true
"interrupt_playback_immediately": true

TEST:
1) هضر مع NPC.
2) خليه يبدا يجاوب.
3) وهو مازال كيهضر، شد U.
4) خاص صوتو يوقف مباشرة.
5) قول مثلاً: "لا لا، ماشي هادي اللي قصدت، كنقصد..."
6) طلق U.
7) خاصو يجاوب وهو فاكر الموضوع السابق.

BUILD:
GitHub -> Actions
Build RDR2VoiceAI v0.6 Barge-In Memory
Run workflow

Artifact:
RDR2VoiceAI_v0.6_BARGE_IN_MEMORY_READY
