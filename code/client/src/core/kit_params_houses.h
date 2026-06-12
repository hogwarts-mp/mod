#pragma once
// AUTO-GENERATED from the in-game AppearanceDump house signature harvest
// (2026-06-12, T3/T4 students of all four houses, both genders).
// House-distinguishing robe MID vector params: constant within a house,
// different across houses. STUUNI03-SPECIFIC — zone indices belong to the
// male/female StuUni03 robe layouts (see kit_params_{male,female}_uni03.h);
// they do not transfer to other uniform styles (StuUni01 etc.).
// House order: 0=Gryffindor, 1=Slytherin, 2=Ravenclaw, 3=Hufflepuff.
// Caveat: a couple of vector entries may be sampling artifacts (per-individual
// variety that happened to correlate with house in the harvest, e.g. skin-tone
// swatches) — harmless, but suspect these first if a house look seems off.

#include "kit_params.h"

namespace HogwartsMP::KitParams
{
    struct HouseParam { const wchar_t* name; float v[4][4]; }; // [house][rgba]
    struct HouseTexture { const wchar_t* name; const wchar_t* path[4]; }; // [house]

    // Hand-added from the texture signature diff (2026-06-12): the house crest
    // patch is the T_Patch_{G,S,R,H} texture set — slot 6 on the male StuUni03
    // robe, slot 4 on the female. Same house order as HouseParam.
#define HMP_PATCH(suffix) \
        {L"/Game/RiggedObjects/Characters/TileableTextures/T_Patch_G_" #suffix ".T_Patch_G_" #suffix, \
         L"/Game/RiggedObjects/Characters/TileableTextures/T_Patch_S_" #suffix ".T_Patch_S_" #suffix, \
         L"/Game/RiggedObjects/Characters/TileableTextures/T_Patch_R_" #suffix ".T_Patch_R_" #suffix, \
         L"/Game/RiggedObjects/Characters/TileableTextures/T_Patch_H_" #suffix ".T_Patch_H_" #suffix}

    inline constexpr auto HouseTexturesM = std::to_array<HouseTexture>({
        {L"Swatch_Diffuse[6]", HMP_PATCH(D)},
        {L"Swatch_MRAB[6]",    HMP_PATCH(MRAB)},
        {L"Swatch_Normal[6]",  HMP_PATCH(N)},
    });

    inline constexpr auto HouseTexturesF = std::to_array<HouseTexture>({
        {L"Swatch_Diffuse[4]", HMP_PATCH(D)},
        {L"Swatch_MRAB[4]",    HMP_PATCH(MRAB)},
        {L"Swatch_Normal[4]",  HMP_PATCH(N)},
    });
#undef HMP_PATCH

    inline constexpr auto HouseParamsM = std::to_array<HouseParam>({
        {L"Color_Tint_High[1]", {{0.604f, 0.434f, 0.392f, 1.0f}, {0.445f, 0.468f, 0.423f, 1.0f}, {0.402f, 0.468f, 0.558f, 1.0f}, {0.539f, 0.491f, 0.418f, 1.0f}}},
        {L"Color_Tint_High[10]", {{0.730f, 0.686f, 0.610f, 1.0f}, {0.133f, 0.130f, 0.117f, 1.0f}, {0.133f, 0.130f, 0.117f, 1.0f}, {0.133f, 0.130f, 0.117f, 1.0f}}},
        {L"Color_Tint_High[11]", {{0.432f, 0.360f, 0.338f, 1.0f}, {0.223f, 0.205f, 0.184f, 1.0f}, {0.223f, 0.205f, 0.184f, 1.0f}, {0.223f, 0.205f, 0.184f, 1.0f}}},
        {L"Color_Tint_High[2]", {{0.584f, 0.485f, 0.445f, 1.0f}, {0.485f, 0.533f, 0.515f, 1.0f}, {0.527f, 0.565f, 0.644f, 1.0f}, {0.584f, 0.558f, 0.491f, 1.0f}}},
        {L"Color_Tint_High[4]", {{0.558f, 0.141f, 0.141f, 1.0f}, {0.319f, 0.402f, 0.371f, 1.0f}, {0.319f, 0.418f, 0.527f, 1.0f}, {0.617f, 0.497f, 0.266f, 1.0f}}},
        {L"Color_Tint_High[7]", {{0.730f, 0.651f, 0.468f, 1.0f}, {0.597f, 0.597f, 0.597f, 1.0f}, {0.753f, 0.565f, 0.366f, 1.0f}, {0.250f, 0.250f, 0.250f, 1.0f}}},
        {L"Color_Tint_High[8]", {{0.604f, 0.434f, 0.392f, 1.0f}, {0.445f, 0.468f, 0.423f, 1.0f}, {0.402f, 0.468f, 0.558f, 1.0f}, {0.539f, 0.491f, 0.418f, 1.0f}}},
        {L"Color_Tint_Low[1]", {{0.672f, 0.533f, 0.451f, 1.0f}, {0.584f, 0.584f, 0.533f, 1.0f}, {0.515f, 0.558f, 0.651f, 1.0f}, {0.708f, 0.638f, 0.558f, 1.0f}}},
        {L"Color_Tint_Low[10]", {{0.729f, 0.636f, 0.608f, 1.0f}, {0.133f, 0.130f, 0.117f, 1.0f}, {0.133f, 0.130f, 0.117f, 1.0f}, {0.133f, 0.130f, 0.117f, 1.0f}}},
        {L"Color_Tint_Low[11]", {{0.146f, 0.146f, 0.146f, 1.0f}, {0.223f, 0.209f, 0.181f, 1.0f}, {0.223f, 0.209f, 0.181f, 1.0f}, {0.223f, 0.209f, 0.181f, 1.0f}}},
        {L"Color_Tint_Low[2]", {{0.584f, 0.485f, 0.445f, 1.0f}, {0.485f, 0.533f, 0.515f, 1.0f}, {0.527f, 0.565f, 0.644f, 1.0f}, {0.584f, 0.558f, 0.491f, 1.0f}}},
        {L"Color_Tint_Low[4]", {{0.631f, 0.347f, 0.347f, 1.0f}, {0.306f, 0.402f, 0.365f, 1.0f}, {0.254f, 0.332f, 0.423f, 1.0f}, {0.558f, 0.521f, 0.337f, 1.0f}}},
        {L"Color_Tint_Low[6]", {{0.771f, 0.297f, 0.304f, 1.0f}, {0.296f, 0.328f, 0.771f, 1.0f}, {0.296f, 0.328f, 0.771f, 1.0f}, {0.296f, 0.328f, 0.771f, 1.0f}}},
        {L"Color_Tint_Low[7]", {{0.703f, 0.314f, 0.290f, 1.0f}, {0.194f, 0.323f, 0.205f, 1.0f}, {0.342f, 0.429f, 0.527f, 1.0f}, {0.831f, 0.708f, 0.397f, 1.0f}}},
        {L"Color_Tint_Low[8]", {{0.672f, 0.533f, 0.451f, 1.0f}, {0.584f, 0.584f, 0.533f, 1.0f}, {0.515f, 0.558f, 0.651f, 1.0f}, {0.708f, 0.638f, 0.558f, 1.0f}}},
    });

    inline constexpr auto HouseParamsF = std::to_array<HouseParam>({
        {L"Color_Tint_High[1]", {{0.927f, 0.927f, 0.927f, 1.0f}, {0.927f, 0.927f, 0.927f, 1.0f}, {0.948f, 0.948f, 0.948f, 1.0f}, {0.930f, 0.930f, 0.930f, 1.0f}}},
        {L"Color_Tint_High[11]", {{0.558f, 0.141f, 0.141f, 1.0f}, {0.319f, 0.402f, 0.371f, 1.0f}, {0.319f, 0.416f, 0.526f, 1.0f}, {0.620f, 0.496f, 0.265f, 1.0f}}},
        {L"Color_Tint_High[2]", {{0.604f, 0.434f, 0.392f, 1.0f}, {0.442f, 0.469f, 0.425f, 1.0f}, {0.401f, 0.467f, 0.557f, 1.0f}, {0.539f, 0.491f, 0.418f, 1.0f}}},
        {L"Color_Tint_High[3]", {{0.730f, 0.651f, 0.468f, 1.0f}, {0.597f, 0.597f, 0.597f, 1.0f}, {0.750f, 0.564f, 0.368f, 1.0f}, {0.250f, 0.250f, 0.250f, 1.0f}}},
        {L"Color_Tint_High[4]", {{0.516f, 0.516f, 0.516f, 1.0f}, {0.443f, 0.443f, 0.443f, 1.0f}, {0.445f, 0.445f, 0.445f, 1.0f}, {0.443f, 0.443f, 0.443f, 1.0f}}},
        {L"Color_Tint_High[6]", {{0.200f, 0.200f, 0.200f, 1.0f}, {0.200f, 0.200f, 0.200f, 1.0f}, {0.202f, 0.202f, 0.202f, 1.0f}, {0.443f, 0.443f, 0.443f, 1.0f}}},
        {L"Color_Tint_High[7]", {{0.600f, 0.600f, 0.600f, 1.0f}, {0.445f, 0.468f, 0.423f, 1.0f}, {0.402f, 0.468f, 0.558f, 1.0f}, {0.539f, 0.491f, 0.418f, 1.0f}}},
        {L"Color_Tint_High[8]", {{0.130f, 0.115f, 0.115f, 1.0f}, {0.275f, 0.254f, 0.231f, 1.0f}, {0.202f, 0.195f, 0.178f, 1.0f}, {0.275f, 0.254f, 0.231f, 1.0f}}},
        {L"Color_Tint_High[9]", {{0.130f, 0.115f, 0.115f, 1.0f}, {0.503f, 0.462f, 0.413f, 1.0f}, {0.297f, 0.295f, 0.292f, 1.0f}, {0.202f, 0.181f, 0.150f, 1.0f}}},
        {L"Color_Tint_Low[1]", {{0.938f, 0.938f, 0.938f, 1.0f}, {0.938f, 0.938f, 0.938f, 1.0f}, {0.922f, 0.922f, 0.922f, 1.0f}, {0.939f, 0.939f, 0.939f, 1.0f}}},
        {L"Color_Tint_Low[11]", {{0.631f, 0.347f, 0.347f, 1.0f}, {0.308f, 0.402f, 0.351f, 1.0f}, {0.252f, 0.334f, 0.422f, 1.0f}, {0.557f, 0.522f, 0.339f, 1.0f}}},
        {L"Color_Tint_Low[14]", {{0.271f, 0.223f, 0.207f, 1.0f}, {0.271f, 0.223f, 0.207f, 1.0f}, {0.271f, 0.223f, 0.207f, 1.0f}, {1.000f, 0.033f, 0.000f, 1.0f}}},
        {L"Color_Tint_Low[2]", {{0.672f, 0.533f, 0.451f, 1.0f}, {0.584f, 0.584f, 0.533f, 1.0f}, {0.515f, 0.554f, 0.651f, 1.0f}, {0.708f, 0.638f, 0.558f, 1.0f}}},
        {L"Color_Tint_Low[3]", {{0.599f, 0.268f, 0.247f, 1.0f}, {0.175f, 0.292f, 0.185f, 1.0f}, {0.342f, 0.429f, 0.527f, 1.0f}, {0.833f, 0.706f, 0.399f, 1.0f}}},
        {L"Color_Tint_Low[4]", {{0.738f, 0.423f, 0.328f, 1.0f}, {0.771f, 0.297f, 0.304f, 1.0f}, {0.296f, 0.328f, 0.768f, 1.0f}, {0.771f, 0.297f, 0.304f, 1.0f}}},
        {L"Color_Tint_Low[6]", {{0.200f, 0.200f, 0.200f, 1.0f}, {0.200f, 0.200f, 0.200f, 1.0f}, {0.165f, 0.175f, 0.202f, 1.0f}, {0.771f, 0.297f, 0.304f, 1.0f}}},
        {L"Color_Tint_Low[7]", {{0.600f, 0.600f, 0.600f, 1.0f}, {0.584f, 0.584f, 0.533f, 1.0f}, {0.515f, 0.558f, 0.651f, 1.0f}, {0.708f, 0.638f, 0.558f, 1.0f}}},
        {L"Color_Tint_Low[8]", {{0.271f, 0.172f, 0.140f, 1.0f}, {0.250f, 0.216f, 0.171f, 1.0f}, {0.250f, 0.216f, 0.171f, 1.0f}, {0.181f, 0.156f, 0.125f, 1.0f}}},
        {L"Color_Tint_Low[9]", {{0.271f, 0.223f, 0.207f, 1.0f}, {0.223f, 0.209f, 0.178f, 1.0f}, {0.365f, 0.354f, 0.344f, 1.0f}, {0.242f, 0.198f, 0.156f, 1.0f}}},
        {L"UVBias[4]", {{0.470f, 0.485f, 0.000f, 1.0f}, {0.470f, 0.482f, 0.000f, 1.0f}, {0.470f, 0.484f, 0.000f, 1.0f}, {0.470f, 0.485f, 0.000f, 1.0f}}},
    });

} // namespace HogwartsMP::KitParams
