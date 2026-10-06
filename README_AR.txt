RDR2VoiceAI v0.8 INTERACTIVE ROLEPLAY
=====================================

NEW:
- Voice commands can trigger real NPC actions.
- Strong 1899 roleplay lock.
- Conversation gestures while NPC speaks.
- Keeps v0.7 Windows-default microphone pipeline.
- Keeps v0.6 barge-in + conversation memory.

VOICE ACTIONS:
Darija examples:
- "تبعني" / "جي معايا" -> NPC follows player.
- "بقا هنا" / "تسنا هنا" -> NPC stays.
- "رقص ليا" / "دير شي رقصة" -> WORLD_HUMAN_DANCING scenario.
- "وقف الرقص" -> stops dance and faces player again.
- "شوف فيا" -> faces/looks at player.
- "لوح ليا" -> wave/gesture.

English phrases also exist in:
Actions/voice_commands.json

IMPORTANT:
Actions only trigger when a phrase clearly matches an explicit command.
Normal conversation does not randomly fire actions.

ROLEPLAY:
Prompts/Base.txt and Prompts/RoleplayImmersion.txt force the NPC to stay in 1899.
No AI / API / game / modern-tech talk in normal roleplay.
If the player says something modern, NPC reacts as an 1899 person rather than explaining it.

GESTURES:
When NPC speech starts, ASI may play a short upper-body conversation gesture.
NPC still faces and looks at player.
Dance uses the native RDR2 WORLD_HUMAN_DANCING scenario.

FOLLOW:
Follow uses TASK_FOLLOW_TO_OFFSET_OF_ENTITY and keeps a natural offset behind the player.

MIC:
Still uses Windows/default input when:
"input_device": null
"use_windows_default_microphone": true

BUILD:
GitHub -> Actions
Build RDR2VoiceAI v0.8 Interactive Roleplay
Run workflow

ARTIFACT:
RDR2VoiceAI_v0.8_INTERACTIVE_ROLEPLAY_READY
