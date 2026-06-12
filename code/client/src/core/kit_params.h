#pragma once
// AUTO-GENERATED from a live BP_Student_C MID dump (2026-06-10).
// Same parameter set applies to all 7 outfit material slots (verified: two
// independent in-game harvests were byte-identical).
//
// ── Material zones ──────────────────────────────────────────────────────────
// Param names like "Color_Tint_High[7]" are flat FNames; the [n] suffix is the
// artists' convention for a ZONE of the outfit's swatch-kit master material.
// A region-ID mask texture maps each texel to a zone; zone n then gets its own
// Swatch_{Diffuse,MRAB,Normal}[n] fabric textures, Color_Tint_{High,Low}[n]
// tints, UVBias/UvRotationScale[n] mapping, and Fuzz/Overlay_Opacity/
// Base_SROD_Modifier[n] surface tweaks.
//
// Zone numbering is PER-MATERIAL — each robe material has its own layout, so
// house overlays only apply to the robe they were harvested from. Per-material
// zone maps live at the top of each kit_params_*_uniNN.h header ("StuUniNN" is
// the game's uniform-style asset variant, not our versioning). Adding another
// uniform style later (StuUni01/02/04…) means harvesting a new header with the
// same AppearanceDump pipeline.
//
// The legacy arrays below belong to the male StuUni01 robe
// (MI_HUM_M_CMBH_Robed_StuUni01) — no longer worn by the proxy (it switched to
// StuUni03 so the house overlays line up). Known zones: 12 = visible skin
// (T_Skin_Arms*); the rest is unmapped. The house tables in
// kit_params_houses.h DO NOT apply to this layout.
// ────────────────────────────────────────────────────────────────────────────

#include <array>

namespace HogwartsMP::KitParams
{
    struct ScalarParam { const wchar_t* name; float value; };
    struct VectorParam { const wchar_t* name; float r, g, b, a; };
    struct TextureParam { const wchar_t* name; const wchar_t* path; };

    inline constexpr auto Scalars = std::to_array<ScalarParam>({
        {L"Overlay_Opacity[1]", 0.5f},
        {L"Overlay_Opacity[9]", 0.5f},
        {L"Fuzz[11]", 0.0f},
        {L"Fuzz[12]", 0.0f},
        {L"Fuzz[13]", 0.0f},
        {L"Overlay_Opacity[6]", 0.0f},
        {L"Fuzz[15]", 0.0f},
        {L"Overlay_Opacity[15]", 0.5f},
        {L"Fuzz[14]", 0.0f},
        {L"Fuzz[7]", 0.0f},
        {L"Normal_Intensity[7]", 0.40000000596046f},
        {L"Overlay_Opacity[5]", 0.20000000298023f},
        {L"Normal_Intensity[14]", 0.0f},
        {L"Fuzz[6]", 0.0f},
        {L"Fuzz[5]", 0.10000000149012f},
        {L"Fuzz[4]", 0.0f},
        {L"Overlay_Opacity[4]", 0.0f},
        {L"Fuzz[1]", 0.0f},
        {L"Fuzz[10]", 0.0f},
        {L"Fuzz[8]", 0.0f},
        {L"Overlay_Opacity[14]", 0.46999999880791f},
        {L"Overlay_Opacity[12]", 0.0f},
    });

    inline constexpr auto Vectors = std::to_array<VectorParam>({
        {L"UvRotationScale[3]", 14.2857f, -0.0000f, 0.0000f, 14.2857f},
        {L"Color_Tint_Low[3]", 0.7758f, 0.7529f, 0.6724f, 1.0000f},
        {L"Color_Tint_High[3]", 0.7758f, 0.7529f, 0.6724f, 1.0000f},
        {L"Base_SROD_Modifier[3]", 1.0000f, 1.0000f, 0.9000f, 1.0000f},
        {L"Scatter_Color[3]", 0.2812f, 0.2655f, 0.2183f, 1.0000f},
        {L"Color_Tint_High[2]", 0.3490f, 0.3490f, 0.3490f, 1.0000f},
        {L"Color_Tint_Low[2]", 0.1620f, 0.1620f, 0.1620f, 1.0000f},
        {L"Scatter_Color[2]", 0.1042f, 0.1042f, 0.1042f, 1.0000f},
        {L"UvRotationScale[2]", 20.0000f, -0.0000f, 0.0000f, 20.0000f},
        {L"Base_SROD_Modifier[1]", 1.0000f, 0.5000f, 0.9000f, 0.0000f},
        {L"Color_Tint_High[1]", 0.4422f, 0.4690f, 0.4249f, 1.0000f},
        {L"Color_Tint_Low[1]", 0.5841f, 0.5841f, 0.5333f, 1.0000f},
        {L"MRAB_Modifier[1]", 1.0000f, 1.0000f, 1.0000f, 0.1400f},
        {L"Scatter_Color[1]", 0.1174f, 0.1250f, 0.1115f, 1.0000f},
        {L"UvRotationScale[1]", 12.5000f, -0.0000f, 0.0000f, 12.5000f},
        {L"Base_SROD_Modifier[4]", 1.0000f, 0.0000f, 0.9000f, 0.0600f},
        {L"Color_Tint_High[4]", 0.6000f, 0.6000f, 0.6000f, 1.0000f},
        {L"Color_Tint_Low[4]", 0.1500f, 0.2500f, 0.1583f, 1.0000f},
        {L"Scatter_Color[4]", 0.2817f, 0.3646f, 0.3478f, 1.0000f},
        {L"UVBias[4]", 0.0700f, 0.0000f, 0.0000f, 1.0000f},
        {L"UvRotationScale[4]", 17.1433f, 10.3008f, -10.3008f, 17.1433f},
        {L"Base_SROD_Modifier[8]", 1.0000f, 0.0000f, 0.9000f, 0.0000f},
        {L"Color_Tint_High[8]", 0.4271f, 0.3898f, 0.2955f, 1.0000f},
        {L"Color_Tint_Low[8]", 0.4323f, 0.3948f, 0.3000f, 1.0000f},
        {L"Scatter_Color[8]", 0.2969f, 0.2969f, 0.2969f, 1.0000f},
        {L"Base_SROD_Modifier[9]", 1.0000f, 0.5000f, 0.9000f, 0.0000f},
        {L"Color_Tint_High[9]", 0.4422f, 0.4690f, 0.4249f, 1.0000f},
        {L"Color_Tint_Low[9]", 0.5841f, 0.5841f, 0.5333f, 1.0000f},
        {L"MRAB_Modifier[9]", 1.0000f, 1.0000f, 1.0000f, 0.1400f},
        {L"Scatter_Color[9]", 0.1174f, 0.1250f, 0.1115f, 1.0000f},
        {L"UvRotationScale[9]", 12.5000f, -0.0000f, 0.0000f, 12.5000f},
        {L"Base_SROD_Modifier[10]", 1.0000f, 0.5000f, 0.9000f, 0.2500f},
        {L"Color_Tint_High[10]", 0.2448f, 0.2448f, 0.2448f, 1.0000f},
        {L"Color_Tint_Low[10]", 0.2656f, 0.2656f, 0.2656f, 1.0000f},
        {L"Scatter_Color[10]", 0.0729f, 0.0729f, 0.0729f, 1.0000f},
        {L"UvRotationScale[10]", 37.7355f, -32.8030f, 32.8030f, 37.7355f},
        {L"Base_SROD_Modifier[11]", 1.0000f, 0.5000f, 0.9000f, 0.2500f},
        {L"Color_Tint_High[11]", 0.2000f, 0.2000f, 0.2000f, 1.0000f},
        {L"Color_Tint_Low[11]", 0.2000f, 0.2000f, 0.2000f, 1.0000f},
        {L"MRAB_Modifier[11]", 1.0000f, 1.0000f, 1.0000f, 0.5000f},
        {L"Scatter_Color[11]", 0.1500f, 0.1500f, 0.1500f, 1.0000f},
        {L"Base_SROD_Modifier[12]", 1.0000f, 0.5000f, 0.9000f, 0.2500f},
        {L"Color_Tint_High[12]", 0.5573f, 0.4228f, 0.3077f, 1.0000f},
        {L"Color_Tint_Low[12]", 0.3140f, 0.2729f, 0.2371f, 1.0000f},
        {L"MRAB_Modifier[12]", 1.0000f, 1.0000f, 1.0000f, 0.5000f},
        {L"Scatter_Color[12]", 0.1500f, 0.1500f, 0.1500f, 1.0000f},
        {L"Base_SROD_Modifier[13]", 1.0000f, 0.5000f, 0.9000f, 0.2500f},
        {L"Color_Tint_High[13]", 0.5573f, 0.2876f, 0.0566f, 1.0000f},
        {L"Color_Tint_Low[13]", 0.3140f, 0.2729f, 0.2371f, 1.0000f},
        {L"MRAB_Modifier[13]", 1.0000f, 1.0000f, 1.0000f, 0.5000f},
        {L"Scatter_Color[13]", 0.1500f, 0.1500f, 0.1500f, 1.0000f},
        {L"UvRotationScale[6]", 6.6667f, -0.0000f, 0.0000f, 6.6667f},
        {L"Color_Tint_High[6]", 0.1800f, 0.1800f, 0.1800f, 1.0000f},
        {L"Color_Tint_Low[6]", 0.1800f, 0.1800f, 0.1800f, 1.0000f},
        {L"Scatter_Color[6]", 0.0242f, 0.0331f, 0.0409f, 1.0000f},
        {L"Base_SROD_Modifier[15]", 0.0000f, 0.5000f, 0.9000f, 0.2500f},
        {L"Color_Tint_High[15]", 0.3200f, 0.4000f, 0.3733f, 1.0000f},
        {L"Color_Tint_Low[15]", 0.3200f, 0.4000f, 0.3733f, 1.0000f},
        {L"MRAB_Modifier[15]", 1.0000f, 1.3000f, 1.0000f, 1.0000f},
        {L"Scatter_Color[15]", 0.1174f, 0.1250f, 0.1115f, 1.0000f},
        {L"UvRotationScale[14]", 5.0000f, -0.0000f, 0.0000f, 5.0000f},
        {L"Base_SROD_Modifier[7]", 1.0000f, 0.5000f, 0.9000f, 0.2500f},
        {L"UVBias[7]", 0.5500f, 0.3000f, 0.0000f, 1.0000f},
        {L"UvRotationScale[7]", 35.0877f, -0.0000f, 0.0000f, 35.0877f},
        {L"Base_SROD_Modifier[6]", 0.5000f, 0.0000f, 0.9000f, 0.2500f},
        {L"Base_SROD_Modifier[2]", 0.0000f, 0.5000f, 0.7500f, 0.2500f},
        {L"Base_SROD_Modifier[5]", 0.5000f, 0.5000f, 0.9000f, 0.2500f},
        {L"Color_Tint_High[5]", 0.2000f, 0.2000f, 0.2000f, 1.0000f},
        {L"Color_Tint_Low[5]", 0.1500f, 0.1500f, 0.1500f, 1.0000f},
        {L"MRAB_Modifier[8]", 1.0000f, 1.2500f, 1.0000f, 1.0000f},
        {L"Scatter_Color[5]", 0.0625f, 0.0625f, 0.0625f, 1.0000f},
        {L"MRAB_Modifier[4]", 1.0000f, 1.2000f, 1.0000f, 1.0000f},
        {L"Scatter_Color[14]", 0.0000f, 0.0000f, 0.0000f, 1.0000f},
        {L"UVBias[14]", 0.5015f, 0.5000f, 0.0000f, 1.0000f},
        {L"Color_Tint_Low[14]", 0.5029f, 0.4969f, 0.5029f, 1.0000f},
        {L"Color_Tint_High[14]", 0.2712f, 0.3302f, 0.3292f, 1.0000f},
        {L"UvRotationScale[5]", 20.0000f, -0.0000f, 0.0000f, 20.0000f},
        {L"MRAB_Modifier[5]", 0.7500f, 1.0000f, 1.0000f, 1.0000f},
        {L"Scatter_Color[7]", 0.0260f, 0.0260f, 0.0260f, 1.0000f},
        {L"Base_SROD_Modifier[14]", 1.0000f, 0.5000f, 0.9000f, 0.4000f},
        {L"UVBias[12]", 0.3300f, 0.2000f, 0.0000f, 1.0000f},
    });

    inline constexpr auto Textures = std::to_array<TextureParam>({
        {L"Swatch_Diffuse[3]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Linen_Canvas_00_D.T_B_Linen_Canvas_00_D"},
        {L"Swatch_MRAB[3]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Linen_Canvas_00_MRAB.T_B_Linen_Canvas_00_MRAB"},
        {L"Swatch_Normal[3]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Linen_Canvas_00_N.T_B_Linen_Canvas_00_N"},
        {L"Swatch_Diffuse[2]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Cotton_Plain_00_D.T_B_Cotton_Plain_00_D"},
        {L"Swatch_MRAB[2]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Cotton_Plain_00_MRAB.T_B_Cotton_Plain_00_MRAB"},
        {L"Swatch_Normal[2]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Cotton_Plain_00_N.T_B_Cotton_Plain_00_N"},
        {L"Swatch_Diffuse[1]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Cotton_Plaid_00_D.T_B_Cotton_Plaid_00_D"},
        {L"Swatch_MRAB[1]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Cotton_Plaid_00_MRAB.T_B_Cotton_Plaid_00_MRAB"},
        {L"Swatch_Normal[1]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Cotton_Plaid_00_N.T_B_Cotton_Plaid_00_N"},
        {L"Swatch_Diffuse[4]", L"/Game/RiggedObjects/Characters/TileableTextures/T_S_Silk_Striped_00_D.T_S_Silk_Striped_00_D"},
        {L"Swatch_MRAB[4]", L"/Game/RiggedObjects/Characters/TileableTextures/T_S_Silk_Striped_00_MRAB.T_S_Silk_Striped_00_MRAB"},
        {L"Swatch_Normal[4]", L"/Game/RiggedObjects/Characters/TileableTextures/T_S_Silk_Striped_00_N.T_S_Silk_Striped_00_N"},
        {L"Swatch_Diffuse[8]", L"/Game/RiggedObjects/Characters/TileableTextures/T_M_Aluminum_Anodized_00_D.T_M_Aluminum_Anodized_00_D"},
        {L"Swatch_MRAB[8]", L"/Game/RiggedObjects/Characters/TileableTextures/T_M_Aluminum_Anodized_00_MRAB.T_M_Aluminum_Anodized_00_MRAB"},
        {L"Swatch_Normal[8]", L"/Game/RiggedObjects/Characters/TileableTextures/T_M_Aluminum_Anodized_00_N.T_M_Aluminum_Anodized_00_N"},
        {L"Swatch_Diffuse[9]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Cotton_Plaid_00_D.T_B_Cotton_Plaid_00_D"},
        {L"Swatch_MRAB[9]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Cotton_Plaid_00_MRAB.T_B_Cotton_Plaid_00_MRAB"},
        {L"Swatch_Normal[9]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Cotton_Plaid_00_N.T_B_Cotton_Plaid_00_N"},
        {L"Swatch_Diffuse[10]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Woven_Kinit_01_D.T_B_Woven_Kinit_01_D"},
        {L"Swatch_MRAB[10]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Woven_Kinit_01_MRAB.T_B_Woven_Kinit_01_MRAB"},
        {L"Swatch_Normal[10]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Woven_Kinit_01_N.T_B_Woven_Kinit_01_N"},
        {L"Swatch_Diffuse[11]", L"/Game/RiggedObjects/Characters/TileableTextures/Standard/T_TileableStandard_Leather_Smooth_D.T_TileableStandard_Leather_Smooth_D"},
        {L"Swatch_MRAB[11]", L"/Game/RiggedObjects/Characters/TileableTextures/Standard/T_TileableStandard_Leather_Smooth_MRAB.T_TileableStandard_Leather_Smooth_MRAB"},
        {L"Swatch_Normal[11]", L"/Game/RiggedObjects/Characters/TileableTextures/Standard/T_TileableStandard_Leather_Smooth_N.T_TileableStandard_Leather_Smooth_N"},
        {L"Swatch_Diffuse[12]", L"/Game/RiggedObjects/Characters/TileableTextures/T_L_Leather_Smooth_00_D.T_L_Leather_Smooth_00_D"},
        {L"Swatch_MRAB[12]", L"/Game/RiggedObjects/Characters/TileableTextures/T_L_Leather_Smooth_00_MRAB.T_L_Leather_Smooth_00_MRAB"},
        {L"Swatch_Normal[12]", L"/Game/RiggedObjects/Characters/TileableTextures/T_L_Leather_Smooth_00_N.T_L_Leather_Smooth_00_N"},
        {L"Swatch_Diffuse[13]", L"/Game/RiggedObjects/Characters/TileableTextures/T_L_Leather_Smooth_00_D.T_L_Leather_Smooth_00_D"},
        {L"Swatch_MRAB[13]", L"/Game/RiggedObjects/Characters/TileableTextures/T_L_Leather_Smooth_00_MRAB.T_L_Leather_Smooth_00_MRAB"},
        {L"Swatch_Normal[13]", L"/Game/RiggedObjects/Characters/TileableTextures/T_L_Leather_Smooth_00_N.T_L_Leather_Smooth_00_N"},
        {L"Swatch_Diffuse[6]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Cotton_Striped_05_D.T_B_Cotton_Striped_05_D"},
        {L"Swatch_MRAB[6]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Cotton_Striped_05_MRAB.T_B_Cotton_Striped_05_MRAB"},
        {L"Swatch_Normal[6]", L"/Game/RiggedObjects/Characters/TileableTextures/T_B_Cotton_Striped_05_N.T_B_Cotton_Striped_05_N"},
        {L"Swatch_Diffuse[15]", L"/Game/RiggedObjects/Characters/TileableTextures/Standard/T_TileableStandard_D.T_TileableStandard_D"},
        {L"Swatch_MRAB[15]", L"/Game/RiggedObjects/Characters/TileableTextures/Standard/T_TileableStandard_Silk_MRAB.T_TileableStandard_Silk_MRAB"},
        {L"Swatch_Normal[15]", L"/Game/RiggedObjects/Characters/TileableTextures/Standard/T_TileableStandard_Silk_N.T_TileableStandard_Silk_N"},
        {L"Swatch_Diffuse[14]", L"/Game/RiggedObjects/Characters/TileableTextures/T_Skin_ArmsIvory_D.T_Skin_ArmsIvory_D"},
        {L"Swatch_Normal[14]", L"/Game/RiggedObjects/Characters/TileableTextures/T_Skin_ArmsEbony_N.T_Skin_ArmsEbony_N"},
        {L"Swatch_MRAB[14]", L"/Game/RiggedObjects/Characters/TileableTextures/T_Skin_ArmsEbony_MRAB.T_Skin_ArmsEbony_MRAB"},
        {L"Swatch_Diffuse[7]", L"/Game/RiggedObjects/Characters/TileableTextures/T_Patch_S_D.T_Patch_S_D"},
        {L"Swatch_MRAB[7]", L"/Game/RiggedObjects/Characters/TileableTextures/T_Patch_S_MRAB.T_Patch_S_MRAB"},
        {L"Swatch_Normal[7]", L"/Game/RiggedObjects/Characters/TileableTextures/T_Patch_S_N.T_Patch_S_N"},
        {L"Swatch_Diffuse[5]", L"/Game/RiggedObjects/Characters/TileableTextures/Standard/T_TileableStandard_D.T_TileableStandard_D"},
        {L"Swatch_MRAB[5]", L"/Game/RiggedObjects/Characters/TileableTextures/Standard/T_TileableStandard_Velvet_MRAB.T_TileableStandard_Velvet_MRAB"},
        {L"Swatch_Normal[5]", L"/Game/RiggedObjects/Characters/TileableTextures/Standard/T_TileableStandardVelvet_N.T_TileableStandardVelvet_N"},
    });

    // SKIN (head+hands) MID params — harvested 2026-06-10 from a live student
    // whose CharacterMesh0 is the same NPC_YM_SebastienSallow mesh our kit uses
    // (textures are *_WithHands_* — this is the missing hand skin). One set
    // shared by all 5 slots, same as the outfit.
    inline constexpr auto SkinScalars = std::to_array<ScalarParam>({
        {L"Complexion Opacity", 1.0f},
        {L"Edge Roughness", 0.35357356071472f},
        {L"Shadow Hardness", 0.30292189121246f},
        {L"Freckle Amount", 1.0f},
        {L"Freckle Opacity", 0.55000001192093f},
        {L"Limbus UV Width Color", 0.050000000745058f},
        {L"Skin Roughness Scale", 0.97000002861023f},
        {L"Skin Scatter Max", 0.94999998807907f},
        {L"Skin Scatter Min", 0.20000000298023f},
        {L"Skin Spec Max", 0.60000002384186f},
        {L"Skin Spec Min", 0.15000000596046f},
        {L"Detail Amount", 0.40000000596046f},
        {L"Sclera Brightness", 1.0f},
        {L"Complexion Amount", 0.97470861673355f},
    });
    inline constexpr auto SkinVectors = std::to_array<VectorParam>({
        {L"Brow Color A", 0.0260f, 0.0152f, 0.0083f, 1.0000f},
        {L"Hair Color A", 0.0312f, 0.0174f, 0.0104f, 1.0000f},
        {L"Eye Corner Darkness Color", 0.4531f, 0.2947f, 0.2856f, 1.0000f},
        {L"Hair Color B", 0.0677f, 0.0395f, 0.0180f, 1.0000f},
        {L"Iris Color B", 0.0573f, 0.0266f, 0.0075f, 1.0000f},
        {L"Skin Base Color Tint", 0.9219f, 0.9008f, 0.8915f, 1.0000f},
    });
    inline constexpr auto SkinTextures = std::to_array<TextureParam>({
        {L"Eye Ball Diffuse", L"/Game/RiggedObjects/Characters/Human/Heads/Textures/T_Eye_ScleraB_D.T_Eye_ScleraB_D"},
        {L"HairCap MSK", L"/Game/RiggedObjects/Characters/Human/Heads/Textures/T_HUM_M_Haircap_StuHair15_MSK.T_HUM_M_Haircap_StuHair15_MSK"},
        {L"Skin Normal", L"/Game/RiggedObjects/Characters/Human/Heads/Young_M/Textures/T_Young_M_Head_SebastienSallow_WithHands_N.T_Young_M_Head_SebastienSallow_WithHands_N"},
        {L"Eyebrows MSK", L"/Game/RiggedObjects/Characters/Human/Heads/Textures/T_HUM_M_Head_Eyebrow04_MSK.T_HUM_M_Head_Eyebrow04_MSK"},
        {L"Eyebrows Normal", L"/Game/RiggedObjects/Characters/Human/Heads/Textures/T_HUM_M_Head_Eyebrow04_N.T_HUM_M_Head_Eyebrow04_N"},
        {L"Skin Diffuse", L"/Game/RiggedObjects/Characters/Human/Heads/Young_M/Textures/T_Young_M_Head_Ivory_WithHands_D.T_Young_M_Head_Ivory_WithHands_D"},
        {L"Skin SRXO", L"/Game/RiggedObjects/Characters/Human/Heads/Young_M/Textures/T_Young_M_Head_Ivory_SebastianSallow_WithHands_SRXO.T_Young_M_Head_Ivory_SebastianSallow_WithHands_SRXO"},
        {L"Detail Mask", L"/Game/RiggedObjects/Characters/Human/Heads/Textures/T_HUM_Head_Pore_WithHands_MSK.T_HUM_Head_Pore_WithHands_MSK"},
        {L"Complexion MSK", L"/Game/RiggedObjects/Characters/Human/Heads/Textures/T_HUM_Head_Complexion_Acne01_MSK.T_HUM_Head_Complexion_Acne01_MSK"},
    });
}

