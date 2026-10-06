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
    static BOOL IS_PED_MALE(Ped p) {
        return invoke<BOOL>(0x6D9F5FAA7488BA46ULL, p);
    }
    static BOOL IS_PED_DEAD_OR_DYING(Ped p, BOOL p1) {
        return invoke<BOOL>(0x3317DEDB88C95038ULL, p, p1);
    }
    static int GET_PED_NEARBY_PEDS(Ped ped, int* peds, int ignoredPedType, int p3) {
        return invoke<int>(0x23F8F5FC7E8C4A6BULL, ped, peds, ignoredPedType, p3);
    }
    static void SET_PED_CAN_PLAY_GESTURE_ANIMS(Ped ped, BOOL toggle, int p2) {
        invoke<Void>(0xBAF20C5432058024ULL, ped, toggle, p2);
    }
}

namespace MISC {
    static Hash GET_HASH_KEY(const char* s) {
        return invoke<Hash>(0xFD340785ADF8CFB7ULL, s);
    }
}

namespace STREAMING {
    static void REQUEST_ANIM_DICT(const char* dict) {
        invoke<Void>(0xA862A2AD321F94B4ULL, dict);
    }
    static BOOL HAS_ANIM_DICT_LOADED(const char* dict) {
        return invoke<BOOL>(0x27FF6FE8009B40CAULL, dict);
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

    static void TASK_FOLLOW_TO_OFFSET_OF_ENTITY(
        Ped follower, Entity target,
        float offsetX, float offsetY, float offsetZ,
        float speed, int timeout, float stoppingRange, BOOL persistFollowing
    ) {
        invoke<Void>(0x304AE42E357B8C7EULL,
            follower, target, offsetX, offsetY, offsetZ,
            speed, timeout, stoppingRange, persistFollowing);
    }

    static void START_SCENARIO_IN_PLACE(
        Ped ped, Hash scenarioHash, int duration = -1
    ) {
        invoke<Void>(0x524B54361229154FULL,
            ped, scenarioHash, duration, TRUE, FALSE, FALSE, 1.0f, FALSE);
    }

    static void TASK_PLAY_ANIM(
        Ped ped, const char* dict, const char* anim,
        float blendIn, float blendOut, int duration,
        int flags, float playbackRate = 0.0f
    ) {
        invoke<Void>(0xEA47FE3719165B94ULL,
            ped, dict, anim,
            blendIn, blendOut, duration, flags, playbackRate,
            0, 0, 0, 0, 0);
    }
}
