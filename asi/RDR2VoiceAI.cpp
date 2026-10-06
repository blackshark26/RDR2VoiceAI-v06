// RDR2VoiceAI v0.6 BARGE-IN MEMORY
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
            }
        } else if (line.rfind("NPC_SPEAK_PULSE|", 0) == 0) {
            int ped = atoi(line.substr(16).c_str());
            if (ped && ENTITY::DOES_ENTITY_EXIST((Ped)ped)) {
                keepNpcEngaged((Ped)ped, 3000);
            }
        } else if (line.rfind("NPC_RELEASE|", 0) == 0) {
            int ped = atoi(line.substr(12).c_str());
            if (!ped || ped == g_activePed) releaseNpc();
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
    logLine("v0.6 BARGE-IN MEMORY ASI started");
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
