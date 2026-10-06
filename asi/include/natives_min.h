#pragma once
#include "types.h"
#include "nativeCaller.h"

namespace PLAYER {
    static Ped PLAYER_PED_ID() {
        return invoke<Ped>(0x096275889B8E0EE0ULL);
    }
}

namespace ENTITY {
    static BOOL DOES_ENTITY_EXIST(Entity e) {
        return invoke<BOOL>(0xD42BD6EB2E0F1677ULL, e);
    }
    static Vector3 GET_ENTITY_COORDS(Entity e, BOOL p1, BOOL p2) {
        return invoke<Vector3>(0xA86D5F069399F44DULL, e, p1, p2);
    }
}

namespace PED {
    static BOOL IS_PED_HUMAN(Ped p) {
        return invoke<BOOL>(0xB980061DA992779DULL, p);
    }

    // Correct RDR2 native hash.
    static BOOL IS_PED_MALE(Ped p) {
        return invoke<BOOL>(0x6D9F5FAA7488BA46ULL, p);
    }

    static BOOL IS_PED_DEAD_OR_DYING(Ped p, BOOL p1) {
        return invoke<BOOL>(0x3317DEDB88C95038ULL, p, p1);
    }

    static int GET_PED_NEARBY_PEDS(Ped ped, int* peds, int ignoredPedType, int p3) {
        return invoke<int>(0x23F8F5FC7E8C4A6BULL, ped, peds, ignoredPedType, p3);
    }
}

namespace TASK {
    static void TASK_TURN_PED_TO_FACE_ENTITY(
        Ped ped, Entity target, int duration,
        float p3 = 0.0f, float p4 = 0.0f, float p5 = 0.0f
    ) {
        invoke<Void>(0x5AD23D40115353ACULL, ped, target, duration, p3, p4, p5);
    }

    static void TASK_LOOK_AT_ENTITY(
        Ped ped, Entity target, int duration,
        int flags = 2048, int p4 = 3, int p5 = 0
    ) {
        invoke<Void>(0x69F4BE8C8CC4796CULL, ped, target, duration, flags, p4, p5);
    }

    static void TASK_STAND_STILL(Ped ped, int time) {
        invoke<Void>(0x919BE13EED931959ULL, ped, time);
    }

    static void CLEAR_PED_TASKS(Ped ped) {
        invoke<Void>(0xE1EF3C1216AFF2CDULL, ped);
    }
}
