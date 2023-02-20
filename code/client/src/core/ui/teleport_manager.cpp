#include "teleport_manager.h"

#include "core/application.h"

#include <imgui/imgui.h>

#include <utils/string_utils.h>
#include <logging/logger.h>


const char *teleportLocations[] = {"Azkaban", "BothyA", "FT _FGM_01_GRYFF_FT_Graveyard", "FT_AnnounceDestA", "FT_AnnounceDestB", "FT_Azkaban", "FT_BlackOffice", "FT_CentralHogsmeade", "FT_Combat_DarkArts_Entry", "FT_Combat_DarkArts_Return", "FT_DeathHallows",
    "FT_DIVE_Vault_UnderwaterA_Surface", "FT_DIVE_Vault_UnderwaterA_VaultInt", "FT_DIVE_Vault_UnderwaterB_CO1_CO_AS_Surface", "FT_DIVE_Vault_UnderwaterB_CO1_CO_AS_VaultInt", "FT_DIVE_Vault_UnderwaterB_CO2_CO_AN_Surface", "FT_DIVE_Vault_UnderwaterB_CO2_CO_AN_VaultInt",
    "FT_DIVE_Vault_UnderwaterB_HN1_HN_AU_Surface", "FT_DIVE_Vault_UnderwaterB_HN1_HN_AU_VaultInt", "FT_DIVE_Vault_UnderwaterB_HN2_HN_BH_Surface", "FT_DIVE_Vault_UnderwaterB_HN2_HN_BH_VaultInt", "FT_DIVE_Vault_UnderwaterB_HS1_HS_AS_Surface",
    "FT_DIVE_Vault_UnderwaterB_HS1_HS_AS_VaultInt", "FT_DIVE_Vault_UnderwaterB_HS2_HS_BF_Surface", "FT_DIVE_Vault_UnderwaterB_HS2_HS_BF_VaultInt", "FT_FGH_GoToHaven", "FT_FGM_01_GRYFF_FT_Graveyard", "FT_FIG_01_CP9", "FT_Floo_TestA", "FT_Floo_TestB", "FT_Hogsmeade_North",
    "FT_Hogsmeade_South", "FT_Hogsmeade_West", "FT_HW_AstronomyTower", "FT_HW_BellTowerCourtyard", "FT_HW_Boathouse", "FT_HW_CentralTower", "FT_HW_CharmsClass", "FT_HW_ClockTowerCourtyard", "FT_HW_DadaClass", "FT_HW_DADATower", "FT_HW_DivinationClass", "FT_HW_Door_Ravenclaw_EXT",
    "FT_HW_Door_Ravenclaw_INT", "FT_HW_FacultyTower", "FT_HW_FigClass", "FT_HW_FlyingClass", "FT_HW_Grandstaircase", "FT_HW_GrandStaircaseTower", "FT_HW_GreatHall", "FT_HW_Greenhouses", "FT_HW_GryffindorCommonRoom", "FT_HW_Haven", "FT_HW_HogwartsDungeon", "FT_HW_HospitalWing",
    "FT_HW_HufflepuffCommonRoom", "FT_HW_Library", "FT_HW_LowerGrandstaircase", "FT_HW_MagicalCreatures", "FT_HW_NorthExitHogwarts", "FT_HW_NorthTower", "FT_HW_PotionsClass", "FT_HW_QuadCourtyard", "FT_HW_RavenclawCommonRoom", "FT_HW_RavenclawTower", "FT_HW_RoomOfRequirement",
    "FT_HW_SlytherinCommonRoom", "FT_HW_SouthExitHogwarts", "FT_HW_TransfigurationClass", "FT_HW_TransfigurationCourtyard", "FT_HW_TrophyRoom", "FT_HW_ViaductCourtyard", "FT_M_EVJ_DADA", "FT_M_FGT_01_CINCapture", "FT_OL_ArchiesFort_HS_AH", "FT_OL_BothyA_CO_AM",
    "FT_OL_BothyA_CO_AS", "FT_OL_BothyA_CO_BA", "FT_OL_BothyA_CO_BQ", "FT_OL_BothyA_HN_AK", "FT_OL_BothyA_HN_AP", "FT_OL_BothyA_HN_AS", "FT_OL_BothyA_HN_BI", "FT_OL_BothyA_HS_AW", "FT_OL_BothyA_HS_AZ", "FT_OL_Cairn_Dungeon_2_CO_AA", "FT_OL_CairnDungeon3_HS_BF",
    "FT_OL_CastleArbroath_CO_BQ", "FT_OL_CastleChepstow_HS_AW", "FT_OL_CastleDungeon1_TU_BB", "FT_OL_CastleJerpoint_CO_AG", "FT_OL_CavDungeon12_CO_AV", "FT_OL_CaveOfDarkness_CO_AH", "FT_OL_CoastalEntrance_CO_AA", "FT_OL_CoastRegionVault_HS_BA", "FT_OL_DarkForestEast_HN_AU",
    "FT_OL_DFBanditCamp_HN_AN", "FT_OL_FalbartonCastle_FO_AU", "FT_OL_Fig07Graphorn_CO_BB", "FT_OL_Forbidden_Forest_Entrance_HN_AV", "FT_OL_GoblinBridge_CO_AM", "FT_OL_Gobmine_Dungeon_07_HS_BA", "FT_OL_GobmineDungeon06_CO_AN", "FT_OL_HamletHalkirk_CO_BB",
    "FT_OL_HamletHearth_HN_BD", "FT_OL_HamletHelmsdale_CO_BD", "FT_OL_HamletIrondale_HS_AY", "FT_OL_HamletKeenbridge_HS_AR", "FT_OL_HamletKinloch_HS_AW", "FT_OL_HamletLowerHogsfield_HN_BG", "FT_OL_HamletMaruweem_CO_AT", "FT_OL_HamletMotherwell_HN_BJ",
    "FT_OL_HamletStirling_HN_AS", "FT_OL_NorthBogEntrance_HN_AO", "FT_OL_NorthFeldcroft_HS_AG", "FT_OL_OldIsidoraCastle_CO_AS", "FT_OL_PercivalsTower_HN_AK", "FT_OL_Pitt-UponFord_HN_AK", "FT_OL_SanctumDungeonCavern2_HN_AU", "FT_OL_SNC_02_MooncalfDen_HN_AZ",
    "FT_OL_TheCollectorsCave_HN_AU", "FT_OutsideDetainment", "FT_OverlandTestSite", "FT_PortkeyTestA", "FT_PortkeyTestB", "FT_PRC_BackToSanctum", "FT_TENT_HER_01_Entrance", "FT_TENT_HER_01_Exit", "FT_TENT_PRC_Entrance", "FT_TENT_PRC_Exit", "FT_Viaduct_FIG_01",
    "M _FGM_01_GRYFF_FT_Graveyard", "M_FT_AVM_02_ExitTent", "M_FT_AVM_02_SkipToTent", "M_FT_BRR_01", "M_FT_BRR_02", "M_FT_BRR_03", "M_FT_EVC_Undercroft", "M_FT_EVL_Convo_01_Hamlet", "M_FT_EVL_SlytherinDungeon", "M_FT_EVZ_GreatHall", "M_FT_FGB_BlackOffice", "M_FT_FGB_HavenRoom",
    "M_FT_FIG_01_CP9", "M_FT_GryfFPlayerBed", "M_FT_GryfMPlayerBed", "M_FT_GT01_HN_AW", "M_FT_Haven_IceWallExit", "M_FT_HER_01_EndLocation", "M_FT_HER_Hamlet", "M_FT_HER_HospitalWing", "M_FT_HufFPlayerBed", "M_FT_HufMPlayerBed", "M_FT_NTR01_DragonChasm", "M_FT_NTR01_EndLocation",
    "M_FT_NTR02_MissionEnd", "M_FT_NTR_MissionEnd", "M_FT_NTR_Moonhenge", "M_FT_OLI_3Broomsticks", "M_FT_OLI_HogwartsGrounds", "M_FT_PNB_End", "M_FT_PNB_ExitDungeon", "M_FT_PNP_ExitDungeon", "M_FT_RavFPlayerBed", "M_FT_RavMPlayerBed", "M_FT_SlyFPlayerBed", "M_FT_SlyMPlayerBed",
    "M_FT_SNC_02_MooncalfDen", "M_FT_SNY_DungeonStart", "M_FT_SNY_ExitDungeon", "M_FT_TIO_01_RookwoodBossFight", "M_FT_ZZC_Classroom", "M_FT_ZZS_HogwartsReturn", "M_SNY_DungeonStart", "Mission_SNC_02_PuffskeinDen", "Old Wizards Tomb"};

// todo move to sdk
UObjectBase *find_uobject(const char *obj_full_name);

namespace HogwartsMP::Core::UI {
    void TeleportManager::Update() {
        static int selectedLocation = 7; // default FT_CentralHogsmeade

        // imgui window with listbox
        ImGui::SetNextWindowSize(ImVec2(470, 240), ImGuiCond_FirstUseEver);
        ImGui::Begin("Teleport manager");
        {
            ImGui::ListBox("Locations", &selectedLocation, teleportLocations, IM_ARRAYSIZE(teleportLocations), 10);
            if (ImGui::Button("Teleport")) {
                std::string name = teleportLocations[selectedLocation];
                TeleportTo(name);
            }
        }
        ImGui::End();
    }

    void TeleportManager::TeleportTo(const std::string &name) {
        UClass* fastTravelManager = (UClass*)find_uobject("Class /Script/Phoenix.FastTravelManager");
        UFunction* fastTravelManagerGetter = (UFunction*)find_uobject("Function /Script/Phoenix.FastTravelManager.Get");

        UClass* fastTravelmanagerInstance{nullptr};
        fastTravelManager->ProcessEvent(fastTravelManagerGetter, (void*)&fastTravelmanagerInstance);

        if(fastTravelmanagerInstance) {
            auto wideTeleportLocation = Framework::Utils::StringUtils::NormalToWide(name);
            FString gname(wideTeleportLocation.c_str());
            Framework::Logging::GetLogger("TeleportManager")->info("Teleporting to {}, instance: {} !", name.c_str(), fmt::ptr(fastTravelmanagerInstance));

            UFunction* fastTravelTo = (UFunction *)find_uobject("Function /Script/Phoenix.FastTravelManager.FastTravel_To");
            fastTravelmanagerInstance->ProcessEvent(fastTravelTo, (void*)&gname);
        }
    }
} // namespace HogwartsMP::Core::UI
