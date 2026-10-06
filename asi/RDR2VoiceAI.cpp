// RDR2VoiceAI v0.8 INTERACTIVE ROLEPLAY
// Nearest-human targeting without worldGetAllPeds.
// Uses GET_PED_NEARBY_PEDS and only a small verified native set.

#include <windows.h>
#include <fstream>
#include <string>
#include <ctime>
#include <cmath>
#include <sstream>
#include "include/main.h"
#include "include/natives_min.h"

static HMODULE g_module = nullptr;
static volatile bool g_pttPressed = false;
static volatile bool g_pttReleased = false;
static volatile bool g_cancelPressed = false;

static int g_pttKey = 'U';
static int g_cancelKey = 'X';

static Ped g_activePed = 0;
static bool g_conversationActive = false;
static DWORD g_lastPoseTick = 0;
static std::streampos g_commandPos = 0;

static std::string getModuleDir() {
    char path[MAX_PATH]{};
    GetModuleFileNameA(g_module, path, MAX_PATH);
    std::string s(path);
    auto p = s.find_last_of("\\/");
    if (p != std::string::npos) s.resize(p + 1);
    return s;
}
static std::string eventsPath() { return getModuleDir() + "RDR2VoiceAI_events.txt"; }
static std::string commandsPath() { return getModuleDir() + "RDR2VoiceAI_commands.txt"; }
static std::string logPath() { return getModuleDir() + "RDR2VoiceAI.log"; }

static void logLine(const std::string& msg) {
    std::ofstream f(logPath(), std::ios::app);
    if (!f) return;
    time_t now = time(nullptr);
    char buf[32]{};
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    f << "[" << buf << "] " << msg << "\n";
}
static void emit(const std::string& line) {
    std::ofstream f(eventsPath(), std::ios::app);
    if (f) { f << line << "\n"; f.flush(); }
    logLine("EVENT: " + line);
}

static float distSq(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx*dx + dy*dy + dz*dz;
}

static Ped findNearestHumanPed(float maxDistance) {
    Ped player = PLAYER::PLAYER_PED_ID();
    if (!player || !ENTITY::DOES_ENTITY_EXIST(player)) return 0;

    // RDR2 GET_PED_NEARBY_PEDS buffer format:
    // [maxPeds, ?, ped0, ?, ped1, ?, ...]
    const int maxPeds = 32;
    int buffer[maxPeds * 2 + 2]{};
    buffer[0] = maxPeds;

    int found = PED::GET_PED_NEARBY_PEDS(player, buffer, -1, 0);
    if (found <= 0) return 0;
    if (found > maxPeds) found = maxPeds;

    Vector3 pc = ENTITY::GET_ENTITY_COORDS(player, TRUE, FALSE);
    Ped best = 0;
    float bestD2 = maxDistance * maxDistance;

    for (int i = 0; i < found; ++i) {
        Ped p = (Ped)buffer[i * 2 + 2];
        if (!p || p == player) continue;
        if (!ENTITY::DOES_ENTITY_EXIST(p)) continue;
        if (!PED::IS_PED_HUMAN(p)) continue;
        if (PED::IS_PED_DEAD_OR_DYING(p, TRUE)) continue;

        Vector3 c = ENTITY::GET_ENTITY_COORDS(p, TRUE, FALSE);
        float d2 = distSq(pc, c);
        if (d2 < bestD2) {
            bestD2 = d2;
            best = p;
        }
    }
    return best;
}

static void keepNpcEngaged(Ped ped, int durationMs = 2500) {
    if (!ped || !ENTITY::DOES_ENTITY_EXIST(ped)) return;
    Ped player = PLAYER::PLAYER_PED_ID();
    if (!player || !ENTITY::DOES_ENTITY_EXIST(player)) return;

    // Basic realistic interaction pose:
    // stop moving, turn body toward player, track player with head/eyes.
    TASK::TASK_STAND_STILL(ped, durationMs);
    TASK::TASK_TURN_PED_TO_FACE_ENTITY(ped, player, 1200, 0.0f, 0.0f, 0.0f);
    TASK::TASK_LOOK_AT_ENTITY(ped, player, durationMs, 2048, 3, 0);
}

static void releaseNpc() {
    if (g_activePed && ENTITY::DOES_ENTITY_EXIST(g_activePed)) {
        TASK::CLEAR_PED_TASKS(g_activePed);
    }
    g_activePed = 0;
    g_conversationActive = false;
}


static bool waitAnimDict(const char* dict, DWORD timeoutMs = 1200) {
    STREAMING::REQUEST_ANIM_DICT(dict);
    DWORD start = GetTickCount();
    while (!STREAMING::HAS_ANIM_DICT_LOADED(dict)) {
        WAIT(0);
        if (GetTickCount() - start > timeoutMs) return false;
    }
    return true;
}

static void playConversationGesture(Ped ped) {
    if (!ped || !ENTITY::DOES_ENTITY_EXIST(ped)) return;

    PED::SET_PED_CAN_PLAY_GESTURE_ANIMS(ped, TRUE, 0);

    const bool male = PED::IS_PED_MALE(ped);
    const DWORD variant = (GetTickCount() / 3000) % 3;

    const char* dict = nullptr;
    const char* anim = nullptr;

    if (male) {
        dict = "ai_gestures@gen_male@standing@speaker";
        if (variant == 0) anim = "neutral_wave_fl_001";
        else if (variant == 1) anim = "neutral_wave_fl_001";
        else anim = "neutral_wave_fl_001";
    } else {
        dict = "ai_gestures@gen_female@standing@speaker";
        if (variant == 0) anim = "neutral_get_attention_l_001";
        else if (variant == 1) anim = "neutral_get_attention_l_001";
        else anim = "neutral_get_attention_l_001";
    }

    if (dict && anim && waitAnimDict(dict)) {
        // Upper-body style flag so feet remain planted and interaction stays stable.
        TASK::TASK_PLAY_ANIM(ped, dict, anim, 4.0f, -4.0f, 1800, 24, 0.0f);
    }
}

static void actionFollow(Ped ped) {
    if (!ped || !ENTITY::DOES_ENTITY_EXIST(ped)) return;
    Ped player = PLAYER::PLAYER_PED_ID();
    TASK::CLEAR_PED_TASKS(ped);
    TASK::TASK_FOLLOW_TO_OFFSET_OF_ENTITY(
        ped, player,
        0.8f, -1.4f, 0.0f,
        2.0f, -1, 1.8f, TRUE
    );
    logLine("ACTION follow ped=" + std::to_string((int)ped));
}

static void actionStay(Ped ped) {
    if (!ped || !ENTITY::DOES_ENTITY_EXIST(ped)) return;
    TASK::CLEAR_PED_TASKS(ped);
    TASK::TASK_STAND_STILL(ped, -1);
    Ped player = PLAYER::PLAYER_PED_ID();
    TASK::TASK_LOOK_AT_ENTITY(ped, player, 5000, 2048, 3, 0);
    logLine("ACTION stay ped=" + std::to_string((int)ped));
}

static void actionDance(Ped ped) {
    if (!ped || !ENTITY::DOES_ENTITY_EXIST(ped)) return;
    TASK::CLEAR_PED_TASKS(ped);
    Hash scenario = MISC::GET_HASH_KEY("WORLD_HUMAN_DANCING");
    TASK::START_SCENARIO_IN_PLACE(ped, scenario, -1);
    logLine("ACTION dance ped=" + std::to_string((int)ped));
}

static void actionStopDance(Ped ped) {
    if (!ped || !ENTITY::DOES_ENTITY_EXIST(ped)) return;
    TASK::CLEAR_PED_TASKS(ped);
    keepNpcEngaged(ped, 5000);
    logLine("ACTION stop_dance ped=" + std::to_string((int)ped));
}

static void actionFacePlayer(Ped ped) {
    if (!ped || !ENTITY::DOES_ENTITY_EXIST(ped)) return;
    keepNpcEngaged(ped, 5000);
    logLine("ACTION face_player ped=" + std::to_string((int)ped));
}

static void actionWave(Ped ped) {
    if (!ped || !ENTITY::DOES_ENTITY_EXIST(ped)) return;
    playConversationGesture(ped);
    logLine("ACTION wave ped=" + std::to_string((int)ped));
}

static void processBridgeCommands() {
    std::ifstream f(commandsPath());
    if (!f) return;

    f.seekg(0, std::ios::end);
    auto end = f.tellg();
    if (g_commandPos > end) g_commandPos = 0;
    f.seekg(g_commandPos);

    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("NPC_SPEAK_START|", 0) == 0) {
            int ped = atoi(line.substr(16).c_str());
            if (ped && ENTITY::DOES_ENTITY_EXIST((Ped)ped)) {
                g_activePed = (Ped)ped;
                g_conversationActive = true;
                keepNpcEngaged(g_activePed, 5000);
                playConversationGesture(g_activePed);
            }
        } else if (line.rfind("NPC_SPEAK_PULSE|", 0) == 0) {
            int ped = atoi(line.substr(16).c_str());
            if (ped && ENTITY::DOES_ENTITY_EXIST((Ped)ped)) {
                keepNpcEngaged((Ped)ped, 3000);
            }
        } else if (line.rfind("NPC_RELEASE|", 0) == 0) {
            int ped = atoi(line.substr(12).c_str());
            if (!ped || ped == g_activePed) releaseNpc();
        } else if (line.rfind("ACTION_FOLLOW|", 0) == 0) {
            int ped = atoi(line.substr(14).c_str());
            actionFollow((Ped)ped);
        } else if (line.rfind("ACTION_STAY|", 0) == 0) {
            int ped = atoi(line.substr(12).c_str());
            actionStay((Ped)ped);
        } else if (line.rfind("ACTION_DANCE|", 0) == 0) {
            int ped = atoi(line.substr(13).c_str());
            actionDance((Ped)ped);
        } else if (line.rfind("ACTION_STOP_DANCE|", 0) == 0) {
            int ped = atoi(line.substr(18).c_str());
            actionStopDance((Ped)ped);
        } else if (line.rfind("ACTION_FACE_PLAYER|", 0) == 0) {
            int ped = atoi(line.substr(19).c_str());
            actionFacePlayer((Ped)ped);
        } else if (line.rfind("ACTION_WAVE|", 0) == 0) {
            int ped = atoi(line.substr(12).c_str());
            actionWave((Ped)ped);
        }
    }
    g_commandPos = f.tellg();
}

static void keyboardHandler(
    DWORD key, WORD, BYTE, BOOL, BOOL,
    BOOL wasDownBefore, BOOL isUpNow
) {
    if ((int)key == g_pttKey) {
        if (!isUpNow && !wasDownBefore) g_pttPressed = true;
        if (isUpNow) g_pttReleased = true;
    }
    if ((int)key == g_cancelKey) {
        if (!isUpNow && !wasDownBefore) g_cancelPressed = true;
    }
}

static void ScriptMain() {
    logLine("v0.8 INTERACTIVE ROLEPLAY ASI started");
    emit("READY|0");

    // Start command reader at EOF so stale commands from older launches are ignored.
    {
        std::ifstream f(commandsPath(), std::ios::ate);
        if (f) g_commandPos = f.tellg();
    }

    while (true) {
        processBridgeCommands();

        if (g_cancelPressed) {
            g_cancelPressed = false;
            emit("CANCEL|0");
            releaseNpc();
        }

        if (g_pttPressed) {
            g_pttPressed = false;

            Ped ped = findNearestHumanPed(5.0f);
            if (!ped) {
                emit("NO_NPC|0");
            } else {
                g_activePed = ped;
                g_conversationActive = true;
                keepNpcEngaged(ped, 30000);

                const char* gender = PED::IS_PED_MALE(ped) ? "male" : "female";
                std::ostringstream ctx;
                ctx << "gender=" << gender
                    << ";role=civilian"
                    << ";zone=unknown"
                    << ";time=unknown";

                emit("PTT_START|" + std::to_string((int)ped) + "|" + ctx.str());
                logLine(std::string("Selected NPC=") + std::to_string((int)ped) +
                        " gender=" + gender);
            }
        }

        if (g_pttReleased) {
            g_pttReleased = false;
            if (g_activePed) {
                emit("PTT_STOP|" + std::to_string((int)g_activePed));
                keepNpcEngaged(g_activePed, 30000);
            }
        }

        // Keep the selected NPC naturally engaged during the conversation.
        if (g_conversationActive && g_activePed && ENTITY::DOES_ENTITY_EXIST(g_activePed)) {
            DWORD now = GetTickCount();
            if (now - g_lastPoseTick >= 2200) {
                g_lastPoseTick = now;
                keepNpcEngaged(g_activePed, 3500);
            }
        }

        WAIT(0);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = hModule;
        DisableThreadLibraryCalls(hModule);
        scriptRegister(hModule, ScriptMain);
        keyboardHandlerRegister(keyboardHandler);
    } else if (reason == DLL_PROCESS_DETACH) {
        keyboardHandlerUnregister(keyboardHandler);
        scriptUnregister(hModule);
    }
    return TRUE;
}
