#pragma once

// APPEND-ONLY: wire spell ids are 1-based indices into kSpellRecords (id 0 = none/unknown). Do NOT
// reorder or insert — that shifts ids and breaks cross-version compat. To add spells, append at the end
// and regenerate. The proxy can only load a record in this allowlist (no arbitrary asset loads).

#include <array>
#include <cstdint>
#include <string_view>

namespace HogwartsMP::Shared::Modules {
    inline constexpr auto kSpellRecords = std::to_array<std::string_view>({
        "/Game/Gameplay/ToolSet/Spells/Accio/DA_AccioSpellRecord.DA_AccioSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/AMBossKiller/DA_AMBossKillerSpellRecord.DA_AMBossKillerSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Apparition/DA_ApparitionSpellRecord.DA_ApparitionSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/ArrestoMomentum/DA_ArrestoMomentumSpellRecord.DA_ArrestoMomentumSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/AvadaKedavra/DA_AvadaKedavraSpellRecord.DA_AvadaKedavraSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Confringo/DA_ConfringoSpellRecord.DA_ConfringoSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Confundo/DA_ConfundoSpellRecord.DA_ConfundoSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Conjuration/DA_ConjurationSpellRecord.DA_ConjurationSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Crucio/DA_CrucioSpellRecord.DA_CrucioSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/DarkWizardSpells/DA_ProtegoSpellRecord_Boss.DA_ProtegoSpellRecord_Boss",
        "/Game/Gameplay/ToolSet/Spells/DarkWizardSpells/DA_ProtegoSpellRecord_DW.DA_ProtegoSpellRecord_DW",
        "/Game/Gameplay/ToolSet/Spells/Depulso/DA_DepulsoSpellRecord.DA_DepulsoSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Depulso/DA_DepulsoSpellRecordDH.DA_DepulsoSpellRecordDH",
        "/Game/Gameplay/ToolSet/Spells/Descendo/DA_DescendoSpellRecord.DA_DescendoSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Descendo/DA_DescendoSpellRecordDH.DA_DescendoSpellRecordDH",
        "/Game/Gameplay/ToolSet/Spells/Diffindo/DA_DiffindoSpellRecord.DA_DiffindoSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Diffindo/DA_DiffindoSpellRecordDH.DA_DiffindoSpellRecordDH",
        "/Game/Gameplay/ToolSet/Spells/Disillusionment/DA_DisillusionmentSpellRecord.DA_DisillusionmentSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Disillusionment/DA_DistractionSpellRecordDH.DA_DistractionSpellRecordDH",
        "/Game/Gameplay/ToolSet/Spells/Disillusionment/DA_InvisibilitySpellRecordDH.DA_InvisibilitySpellRecordDH",
        "/Game/Gameplay/ToolSet/Spells/Dragon/DA_AttackSpellRecord_DragonBolt.DA_AttackSpellRecord_DragonBolt",
        "/Game/Gameplay/ToolSet/Spells/Dragon/DA_AttackSpellRecord_DragonBolt_AOE.DA_AttackSpellRecord_DragonBolt_AOE",
        "/Game/Gameplay/ToolSet/Spells/Dragon/DA_AttackSpellRecord_DragonFire_Instakill.DA_AttackSpellRecord_DragonFire_Instakill",
        "/Game/Gameplay/ToolSet/Spells/Episkey/DA_EpiskeySpellRecord.DA_EpiskeySpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Expelliarmus/DA_ExpelliarmusSpellRecord.DA_ExpelliarmusSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Expulso/DA_ExpulsoSpellRecord.DA_ExpulsoSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Expulso/DA_ExpulsoSpellRecordDH.DA_ExpulsoSpellRecordDH",
        "/Game/Gameplay/ToolSet/Spells/FiendFyre/DA_FiendFyreSimpleSpellRecord.DA_FiendFyreSimpleSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Flipendo/DA_FlipendoSpellRecord.DA_FlipendoSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Glacius/DA_DWGlaciusSpellRecord.DA_DWGlaciusSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Glacius/DA_GlaciusSpellRecord.DA_GlaciusSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/GoblinSpells/DA_Attack1SpellRecord_Goblin.DA_Attack1SpellRecord_Goblin",
        "/Game/Gameplay/ToolSet/Spells/GoblinSpells/DA_Attack2SpellRecord_Goblin.DA_Attack2SpellRecord_Goblin",
        "/Game/Gameplay/ToolSet/Spells/GoblinSpells/DA_Attack3SpellRecord_Goblin.DA_Attack3SpellRecord_Goblin",
        "/Game/Gameplay/ToolSet/Spells/GoblinSpells/DA_Goblin_Shielded_ProtegoSpellRecord.DA_Goblin_Shielded_ProtegoSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/GoblinSpells/DA_SpellRecord_Chieftain_Protego.DA_SpellRecord_Chieftain_Protego",
        "/Game/Gameplay/ToolSet/Spells/GoblinSpells/DA_SpellRecord_Goblin_AOE.DA_SpellRecord_Goblin_AOE",
        "/Game/Gameplay/ToolSet/Spells/GoblinSpells/DA_SpellRecord_Socrerer_Protego.DA_SpellRecord_Socrerer_Protego",
        "/Game/Gameplay/ToolSet/Spells/GoblinSpells/DA_SpellRecord_Sorcerer_Close.DA_SpellRecord_Sorcerer_Close",
        "/Game/Gameplay/ToolSet/Spells/GoblinSpells/DA_SpellRecord_Sorcerer_Combo.DA_SpellRecord_Sorcerer_Combo",
        "/Game/Gameplay/ToolSet/Spells/GoblinSpells/DA_SpellRecord_Sorcerer_Flame.DA_SpellRecord_Sorcerer_Flame",
        "/Game/Gameplay/ToolSet/Spells/Imperious/DA_ImperiusSpellRecord.DA_ImperiusSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Incendio/DA_IncendioSpellRecord.DA_IncendioSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/InteractionGeneral/DA_InteractionGeneralSpellRecord.DA_InteractionGeneralSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Levioso/DA_LeviosoSpellRecord.DA_LeviosoSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Lumos/DA_LumosSpellRecord.DA_LumosSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Lumos/DA_LumosSpellRecordNPC.DA_LumosSpellRecordNPC",
        "/Game/Gameplay/ToolSet/Spells/Obliviate/DA_ObliviateSpellRecord.DA_ObliviateSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Oppugno/DA_OppugnoSpellRecord.DA_OppugnoSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Petrificus/DA_DWPetrificusSpellRecord.DA_DWPetrificusSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Petrificus/DA_PetrificusSpellRecord.DA_PetrificusSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Protego/DA_ProtegoSpellRecord.DA_ProtegoSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Protego/DA_ProtegoSpellRecordDH.DA_ProtegoSpellRecordDH",
        "/Game/Gameplay/ToolSet/Spells/Ranrak/DA_AttackSpellRecord_RanrakBossFireBreath.DA_AttackSpellRecord_RanrakBossFireBreath",
        "/Game/Gameplay/ToolSet/Spells/Ranrak/DA_AttackSpellRecord_RanrakBossFireBreathSweep.DA_AttackSpellRecord_RanrakBossFireBreathSweep",
        "/Game/Gameplay/ToolSet/Spells/Ranrak/DA_AttackSpellRecord_RanrakBossP1.DA_AttackSpellRecord_RanrakBossP1",
        "/Game/Gameplay/ToolSet/Spells/Ranrak/DA_AttackSpellRecord_RanrakBossP2.DA_AttackSpellRecord_RanrakBossP2",
        "/Game/Gameplay/ToolSet/Spells/Ranrak/DA_AttackSpellRecord_RanrakBossPulse.DA_AttackSpellRecord_RanrakBossPulse",
        "/Game/Gameplay/ToolSet/Spells/Reparo/DA_ReparoSpellRecord.DA_ReparoSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Revelio/DA_RevelioSpellRecord.DA_RevelioSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Spider/DA_AttackSpellRecord_Spider_Venomous.DA_AttackSpellRecord_Spider_Venomous",
        "/Game/Gameplay/ToolSet/Spells/Spider/DA_AttackSpellRecord_Spider_Woodlouse.DA_AttackSpellRecord_Spider_Woodlouse",
        "/Game/Gameplay/ToolSet/Spells/Spider/DA_SpellRecord_Spider_BurrowAoe.DA_SpellRecord_Spider_BurrowAoe",
        "/Game/Gameplay/ToolSet/Spells/Spider/DA_SpellRecord_Spider_Woodlouse_Sniper_SpitWebs.DA_SpellRecord_Spider_Woodlouse_Sniper_SpitWebs",
        "/Game/Gameplay/ToolSet/Spells/StealthTakedown/DA_StealthTakedownSpellRecord.DA_StealthTakedownSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Stupefy/DA_StingingSpellRecord.DA_StingingSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Stupefy/DA_StupefyHeavySpellRecord.DA_StupefyHeavySpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Stupefy/DA_StupefySpecialSendSpellRecord.DA_StupefySpecialSendSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Stupefy/DA_StupefySpellRecord.DA_StupefySpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Stupefy/DA_StupefySpellRecordDH.DA_StupefySpellRecordDH",
        "/Game/Gameplay/ToolSet/Spells/TombProtector/DA_AttackSpellRecord_TombProtector_Missile.DA_AttackSpellRecord_TombProtector_Missile",
        "/Game/Gameplay/ToolSet/Spells/TombProtector/DA_AttackSpellRecord_TombProtector_Stomp.DA_AttackSpellRecord_TombProtector_Stomp",
        "/Game/Gameplay/ToolSet/Spells/Transformation/DA_TransformationOverlandSpellRecord.DA_TransformationOverlandSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Transformation/DA_TransformationSpellRecord.DA_TransformationSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Troll/DA_AttackSpellRecord_Troll.DA_AttackSpellRecord_Troll",
        "/Game/Gameplay/ToolSet/Spells/Vanishment/DA_VanishmentSpellRecord.DA_VanishmentSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/VenomousTentacula/DA_AttackSpellRecord_VenomousTentacula.DA_AttackSpellRecord_VenomousTentacula",
        "/Game/Gameplay/ToolSet/Spells/VFXTest/DA_FXBeamTestSpellRecord.DA_FXBeamTestSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/VFXTest/DA_FXTestSpellRecord.DA_FXTestSpellRecord",
        "/Game/Gameplay/ToolSet/Spells/Wingardium/DA_WingardiumSpellRecord.DA_WingardiumSpellRecord",
    });

    // Asset path -> 1-based wire id (0 if not in the allowlist). Linear scan (~80, called once per cast).
    inline uint8_t SpellRecordId(std::string_view path) {
        if (path.empty()) {
            return 0;
        }
        for (std::size_t i = 0; i < kSpellRecords.size(); ++i) {
            if (kSpellRecords[i] == path) {
                return static_cast<uint8_t>(i + 1);
            }
        }
        return 0;
    }

    // Wire id -> asset path (nullptr if 0/out of range; null-terminated literal otherwise).
    inline const char *SpellRecordPath(uint8_t id) {
        return (id >= 1 && id <= kSpellRecords.size()) ? kSpellRecords[id - 1].data() : nullptr;
    }
} // namespace HogwartsMP::Shared::Modules
