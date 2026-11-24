#pragma once
#include <cctype>

namespace MudAvatar
{
struct Appearance
{
    int model, breed;
    bool operator==(Appearance other) const { return model == other.model && breed == other.breed; }
};

inline Appearance Select(int slot, int localSlot, Appearance local, Appearance remote)
{
    if (slot >= 2) return {slot + 1, -1}; // native AI models
    const auto cow = slot == localSlot ? local : remote;
    if (cow.model < 0 || cow.model > 1 || cow.breed < 0 || cow.breed > 6) return {0, 0};
    return cow;
}

enum class Change { None, Wait, Model, Hide };

inline Change NextChange(Appearance current, Appearance wanted, bool canReplace)
{
    if (current.model != wanted.model) return canReplace ? Change::Model : Change::Wait;
    return current.breed == wanted.breed ? Change::None : Change::Hide;
}

// The model type names the body mesh. Its lighting names can also look like body names.
inline bool BodyMesh(const char* declared, const char* mesh, bool skinShader, bool textured)
{
    if (!declared || !*declared || !mesh || !skinShader || !textured) return false;
    for (; *declared && *mesh; ++declared, ++mesh)
        if (std::tolower(static_cast<unsigned char>(*declared)) !=
            std::tolower(static_cast<unsigned char>(*mesh))) return false;
    return *declared == *mesh;
}

template<class Mesh, class Material>
void ReplaceMaterial(Mesh* const* meshes, int count, Material* from, Material* to)
{
    for (int i = 0; i < count; ++i)
        if (meshes[i]->GetMaterial() == from) meshes[i]->SetMaterial(to);
}
}
