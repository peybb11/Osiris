#pragma once

#include <cstddef>

#include "Vector.h"
#include "VirtualMethod.h"

struct Ray {
    Ray(const Vector& src, const Vector& dest) : start(src), delta(dest - src) { isSwept = delta.x || delta.y || delta.z; }
    Vector start{ };
    float pad{ };
    Vector delta{ };
#if IS_WIN32()
    std::byte pad2[40]{ };
#elif IS_LINUX()
    std::byte pad2[44]{ };
#endif
    bool isRay{ true };
    bool isSwept{ };
};

namespace csgo::pod { struct Entity; }

struct TraceFilter {
    TraceFilter(csgo::pod::Entity* entity) : skip{ entity } { }
    virtual bool shouldHitEntity(csgo::pod::Entity* entity, int) { return entity != skip; }
    virtual int getTraceType() const { return 0; }
    csgo::pod::Entity* skip;
};

namespace HitGroup {
    enum {
        Invalid = -1,
        Generic,
        Head,
        Chest,
        Stomach,
        LeftArm,
        RightArm,
        LeftLeg,
        RightLeg,
        Gear = 10
    };

    constexpr float getDamageMultiplier(int hitGroup) noexcept
    {
        switch (hitGroup) {
        case Head:
            return 4.0f;
        case Stomach:
            return 1.25f;
        case LeftLeg:
        case RightLeg:
            return 0.75f;
        default:
            return 1.0f;
        }
    }

    constexpr bool isArmored(int hitGroup, bool helmet) noexcept
    {
        switch (hitGroup) {
        case Head:
            return helmet;

        case Chest:
        case Stomach:
        case LeftArm:
        case RightArm:
            return true;
        default:
            return false;
        }
    }
}

struct Trace {
    Vector startpos;
    Vector endpos;
    std::byte pad[20];
    float fraction;
    int contents;
    unsigned short dispFlags;
    bool allSolid;
    bool startSolid;
    std::byte pad1[4];
    struct Surface {
        const char* name;
        short surfaceProps;
        unsigned short flags;
    } surface;
    int hitgroup;
    std::byte pad2[4];
    csgo::pod::Entity* entity;
    int hitbox;
};

class EngineTrace : private VirtualCallable {
public:
    using VirtualCallable::VirtualCallable;

    VIRTUAL_METHOD(int, getPointContents, 0, (const Vector& absPosition, int contentsMask), (std::cref(absPosition), contentsMask, nullptr))
    VIRTUAL_METHOD(void, traceRay, 5, (const Ray& ray, unsigned int mask, const TraceFilter& filter, Trace& trace), (std::cref(ray), mask, std::cref(filter), std::ref(trace)))
};
