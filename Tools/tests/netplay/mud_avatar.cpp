#include "MiniGame/AGateCrashAvatar.h"
#include <cstdio>
#include <initializer_list>

using namespace MudAvatar;
static int checks, failures;
struct Mesh {
    int* material;
    int* GetMaterial() const { return material; }
    void SetMaterial(int* value) { material = value; }
};

static void Check(bool passed, const char* message)
{
    ++checks;
    if (!passed) { ++failures; std::printf("FAIL: %s\n", message); }
}

int main()
{
    const Appearance host{1, 6}, guest{0, 1}, unknown{-1, -1};
    for (int local = 0; local < 2; ++local) {
        const auto mine = local ? guest : host, theirs = local ? host : guest;
        Check(Select(0, local, mine, theirs) == host, "host slot shows female Holstein on both peers");
        Check(Select(1, local, mine, theirs) == guest, "guest slot shows male Angus on both peers");
        Check(Select(2, local, mine, theirs) == Appearance{3, -1}, "first AI keeps native character");
        Check(Select(3, local, mine, theirs) == Appearance{4, -1}, "second AI keeps native character");
        Check(Select(local, local, mine, unknown) == mine, "missing peer leaves local saved cow intact");
        Check(Select(1-local, local, mine, unknown) == Appearance{0, 0}, "old peer gets deterministic default");
    }
    for (int sex = 0; sex < 2; ++sex) for (int breed = 0; breed < 7; ++breed) {
        const Appearance cow{sex, breed};
        Check(Select(0, 1, guest, cow) == cow, "retains each valid remote gender and breed");
        Check(Select(1, 1, cow, host) == cow, "retains each valid local gender and breed");
    }
    for (int sex = 0; sex < 2; ++sex) {
        const Appearance angus{sex, 1}, holstein{sex, 6};
        Check(Select(0, 0, angus, holstein) == angus && Select(0, 1, holstein, angus) == angus,
              "same-gender host keeps Angus on both screens");
        Check(Select(1, 0, angus, holstein) == holstein && Select(1, 1, holstein, angus) == holstein,
              "same-gender guest keeps Holstein on both screens");
    }
    Check(Select(1, 0, host, {5, 6}) == Appearance{0, 0}, "bad model ID cannot index the model list");
    Check(Select(1, 0, host, {1, 7}) == Appearance{0, 0}, "bad breed cannot index the material library");
    Check(NextChange({0, 0}, host, false) == Change::Wait, "keep a model driving a native action");
    Check(NextChange({0, 0}, host, true) == Change::Model, "late remote gender replaces temporary model");
    Check(NextChange({1, 0}, host, false) == Change::Hide, "late same-gender breed can update during action");
    Check(NextChange(host, host, true) == Change::None, "appearance resends leave the instance intact");
    Check(BodyMesh("ss_AmericanF", "ss_americanF", true, true), "female body name matches case-insensitively");
    Check(BodyMesh("ss_avatarM", "ss_avatarM", true, true), "male body name matches authored mesh");
    Check(!BodyMesh("ss_AmericanF", "ss_eye_blue", true, true), "never recolour eyes");
    Check(!BodyMesh("ss_avatarM", "ss_avatarM2", true, true), "a lighting name is not the body mesh");
    Check(!BodyMesh("ss_avatarM", "ss_avatarM", false, true), "other shader types must stay unchanged");
    Check(!BodyMesh("ss_avatarM", "ss_avatarM", true, false), "skip untextured mesh");
    Check(!BodyMesh(nullptr, "ss_avatarM", true, true), "skip unresolved model material");
    int shared = 0, eye = 1, angus = 2, holstein = 3;
    Mesh bodyA{&shared}, bodyB{&shared}, iris{&eye};
    Mesh* lod[] = {&iris, &bodyA, &bodyB};
    for (const auto hide : {&angus, &holstein}) {
        ReplaceMaterial(lod, 3, &shared, hide);
        Check(bodyA.material == hide && bodyB.material == hide && iris.material == &eye,
              "draw current instance's hide on both body parts, leave the eye intact");
        ReplaceMaterial(lod, 3, hide, &shared);
        Check(bodyA.material == &shared && bodyB.material == &shared && iris.material == &eye,
              "next instance or AI sees original material after draw");
    }
    std::printf("mud_avatar: %d/%d passed\n", checks-failures, checks);
    return failures ? 1 : 0;
}
