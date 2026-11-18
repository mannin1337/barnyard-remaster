#pragma once
#include <array>
#include <cwctype>
#include <string>
#include <string_view>

namespace CoopMiniHud
{
// Keep the game title separate from localized slot markers and player-supplied names.
inline constexpr wchar_t Title[] = L"\x1f";
using Pair = std::array<std::wstring_view, 2>;

inline std::wstring Scorecard(std::wstring_view text, const Pair& labels, const Pair& names,
                              std::wstring_view title)
{
    std::wstring result;
    bool seen[2]{};
    const auto word = [](wchar_t c) { return std::iswalnum(c) || c == L'_'; };
    for (size_t pos = 0; pos < text.size();)
    {
        if (text[pos] == Title[0])
        {
            result.append(title);
            ++pos;
            continue;
        }
        bool matched = false;
        for (size_t role = 0; role < labels.size(); ++role)
        {
            const auto label = labels[role];
            const auto end = pos + label.size();
            if (!label.empty() && text.substr(pos, label.size()) == label &&
                (!pos || !word(text[pos - 1])) && (end == text.size() || !word(text[end])))
            {
                result.append(names[role]);
                seen[role] = true;
                pos = end;
                matched = true;
                break;
            }
        }
        if (!matched) result += text[pos++];
    }
    // A custom translation can spell a role out instead of using its numeric marker.
    // Keep its wording and provide a key for the names absent from that scorecard.
    for (size_t role = 0; role < labels.size(); ++role)
    {
        if (seen[role]) continue;
        result.append(L"\n").append(labels[role]).append(L": ").append(names[role]);
    }
    return result;
}
}
