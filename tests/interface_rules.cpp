#include "../src/internal/Menu/StartupPresentation.h"
#include "../src/internal/Menu/FeatureShortcuts.h"
#include "../src/internal/Config/ConfigManager.h"
#include "../src/internal/Hack/CameraControls.h"
#include "../src/internal/ESP/ESPLayout.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

int main() {
    using namespace PhoenixInterface;
    using namespace PhoenixShortcuts;
    PhoenixStartup::Presentation Intro;
    for(int I=0;I<200;++I) Intro.Tick(true,true,false,.1f);
    assert(Intro.Elapsed==0 && Intro.Alpha==0 && !Intro.Finished);
    Intro.Tick(false,true,true,.1f);assert(Intro.Elapsed==0);
    for(int I=0;I<8;++I) Intro.Tick(true,true,true,.1f);
    assert(Intro.Alpha==1 && !Intro.Finished);
    float Seen=Intro.Elapsed;Intro.Tick(true,true,false,10);assert(Intro.Elapsed==Seen && Intro.Alpha==0);
    for(int I=0;I<40;++I) Intro.Tick(true,true,true,.1f);
    assert(Intro.Finished && Intro.Alpha==0);
    Intro.Replay();Intro.Tick(true,false,true,.1f);assert(Intro.Alpha>0);
    Intro.Dismiss();Intro.Tick(true,true,true,.1f);assert(Intro.Alpha==0);

    assert(FiniteClamp(std::numeric_limits<float>::quiet_NaN(), 12, 12, 13) == 12);
    assert(ShouldTrigger(Ctrl|0x7b,Ctrl|0x7b,false,true,true,true,true));
    assert(!ShouldTrigger(0x75,0x75,true,true,false,false));
    assert(!ShouldTrigger(0x75,0x75,false,false,false,false));
    assert(!ShouldTrigger(0x75,0x75,false,true,true,false));
    assert(!ShouldTrigger(0x75,0x75,false,true,false,true));
    assert(!ShouldTrigger(0x75,Ctrl|0x75,false,true,false,false));
    assert(!ShouldTrigger(0,0,false,true,false,false));
    assert(!Assign(Actions[0].Key,MenuKey));
    assert(Assign(Actions[0].Key,Ctrl|'K'));
    assert(!Assign(Actions[1].Key,Ctrl|'K'));
    assert(!Assign(MenuKey,0)); assert(!Assign(StopKey,0));
    assert(Assign(Actions[0].Key,0));
    assert(!Assign(Actions[0].Key,0x10));
    Actions[0].Key=Actions[1].Key=Alt|'H'; Normalize();
    assert(Actions[0].Key==(Alt|'H') && Actions[1].Key==0);
    for(auto& A:Actions) *A.Enabled=true;
    g_Config::bAutoFeed=g_Config::bAutoEatDrink=g_Config::bAutomatic=true;
    const float Preserved=g_Config::AutoCraftRangeMeters=74;
    StopAll();
    for(auto& A:Actions) if(A.Enabled!=&g_Config::bResourceESPEnabled&&A.Enabled!=&g_Config::bModelHighlightEnabled) assert(!*A.Enabled);
    assert(g_Config::bResourceESPEnabled && g_Config::bModelHighlightEnabled);
    assert(!g_Config::bAutoFeed&&!g_Config::bAutoEatDrink&&!g_Config::bAutomatic);
    assert(g_Config::AutoCraftRangeMeters==Preserved);
    auto Step=g_Hack::CameraControls::Step(0,0,1,0,0,200,0.1,false);
    assert(std::abs(Step.X-2000)<.001);
    auto Capped=g_Hack::CameraControls::Step(0,0,1,0,0,999,0.1,false);
    assert(Capped.X==Step.X);
    assert(g_ESP::Layout::FontPixels(48,1,13)==48);
    auto TestDir=std::filesystem::temp_directory_path()/("phoenix-ui-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ConfigManager::Get().Initialize(TestDir.string());
    g_Config::AimbotMaxDistanceMeters=2000; g_Config::ESPFontPixels=48;g_Config::ESPMinFontPixels=40;
    g_Config::FreeCameraSpeedMeters=200;g_Config::bShowFeatureHelp=true;g_Config::bShowWelcome=false;
    MenuKey=Ctrl|0x71;StopKey=Ctrl|0x7b;Actions[0].Key=Alt|'K';
    g_Config::bAutoDropResources=g_Config::bUnderwaterWeapons=g_Config::bAutoDepositGachaGear=g_Config::bAutoDepositFishingBlueprints=true;
    g_Config::AutoDropResourceMask=1|16|1024;g_Config::AutoDepositGrinderRangeMeters=30;g_Config::AutoDepositVaultRangeMeters=25;
    g_Config::bDinoHarvestRange=true;g_Config::DinoHarvestRangeMultiplier=5;
    g_Config::AutoHarvestSearchRadiusMeters=300;g_Config::AutoHarvestAttackDistanceMeters=30;
    g_Config::AutoHarvestTekStriderBeamRangeMeters=150;g_Config::AutoHarvestAttackIntervalMs=10000;g_Config::AutoHarvestMaxSafeDropMeters=15;
    assert(ConfigManager::Get().SaveConfig("roundtrip.ini"));
    g_Config::AimbotMaxDistanceMeters=300;g_Config::ESPFontPixels=16;g_Config::FreeCameraSpeedMeters=20;
    g_Config::bShowFeatureHelp=false;g_Config::bShowWelcome=true;Actions[0].Key=0;MenuKey=0x71;
    assert(ConfigManager::Get().LoadConfig("roundtrip.ini"));
    assert(g_Config::bAutoDropResources && g_Config::bUnderwaterWeapons && g_Config::bAutoDepositGachaGear && g_Config::bAutoDepositFishingBlueprints);
    assert(g_Config::bDinoHarvestRange && g_Config::DinoHarvestRangeMultiplier==5);
    assert(g_Config::AutoHarvestSearchRadiusMeters==300 && g_Config::AutoHarvestAttackDistanceMeters==30);
    assert(g_Config::AutoHarvestTekStriderBeamRangeMeters==150 && g_Config::AutoHarvestAttackIntervalMs==10000 && g_Config::AutoHarvestMaxSafeDropMeters==15);

    assert(g_Config::AutoDropResourceMask==(1|16|1024));assert(g_Config::AutoDepositGrinderRangeMeters==30 && g_Config::AutoDepositVaultRangeMeters==25);
    assert(g_Config::AimbotMaxDistanceMeters==2000&&g_Config::ESPFontPixels==48&&g_Config::FreeCameraSpeedMeters==200);
    assert(g_Config::bShowFeatureHelp&&!g_Config::bShowWelcome);
    assert(Actions[0].Key==(Alt|'K')&&MenuKey==(Ctrl|0x71));
    std::ofstream Bad(TestDir/"invalid.ini");
    Bad<<"g_Config::DinoHarvestRangeMultiplier=999\ng_Config::AutoHarvestSearchRadiusMeters=999\ng_Config::AutoHarvestAttackDistanceMeters=999\ng_Config::AutoHarvestTekStriderBeamRangeMeters=999\ng_Config::AutoHarvestAttackIntervalMs=99999\ng_Config::AutoHarvestMaxSafeDropMeters=999\ng_Config::MenuScaleIdx=999\ng_Config::ESPFontPixels=999\ng_Config::ESPMinFontPixels=999\ng_Config::FreeCameraSpeedMeters=999\nShortcut.menu=16\nShortcut.stop=0\nShortcut.aim=113\nShortcut.trigger=113\n";Bad.close();
    assert(ConfigManager::Get().LoadConfig("invalid.ini"));
    assert(!g_Config::bDinoHarvestRange && g_Config::DinoHarvestRangeMultiplier==5 && g_Config::AutoHarvestSearchRadiusMeters==300 && g_Config::AutoHarvestAttackIntervalMs==10000 && g_Config::AutoHarvestMaxSafeDropMeters==15);
    assert(g_Config::MenuScaleIdx==4&&g_Config::ESPFontPixels==48&&g_Config::ESPMinFontPixels==48&&g_Config::FreeCameraSpeedMeters==200);
    assert(MenuKey==DefaultMenuKey&&StopKey==DefaultStopKey&&Actions[0].Key==0&&Actions[1].Key==0);
    std::ofstream Legacy(TestDir/"legacy.ini");Legacy<<"g_Config::MenuScaleIdx=1\n";Legacy.close();
    Actions[0].Key=Alt|'K';
    assert(ConfigManager::Get().LoadConfig("legacy.ini"));assert(Actions[0].Key==0);
    assert(!g_Config::bDinoHarvestRange && g_Config::DinoHarvestRangeMultiplier==2);
    assert(!g_Config::bAutoDropResources && !g_Config::bUnderwaterWeapons && !g_Config::bAutoDepositGachaGear && !g_Config::bAutoDepositFishingBlueprints);
    assert(g_Config::AutoDropResourceMask==0 && g_Config::AutoDepositGrinderRangeMeters==12 && g_Config::bShowFeatureHelp);
    std::ofstream Extra(TestDir/"extra.ini"); Extra<<"g_Config::AutoHarvestAttackDistanceMeters=-100\ng_Config::AutoHarvestTekStriderBeamRangeMeters=-100\ng_Config::AutoDropResourceMask=-1\ng_Config::AutoDepositGrinderRangeMeters=999\ng_Config::AutoDepositVaultRangeMeters=0\ng_Config::TekSpearParryTapSeconds=999\n";Extra.close();
    assert(ConfigManager::Get().LoadConfig("extra.ini"));assert(g_Config::AutoDropResourceMask==0 && g_Config::AutoDepositGrinderRangeMeters==30 && g_Config::AutoDepositVaultRangeMeters==2 && g_Config::TekSpearParryTapSeconds==.5f);
    assert(g_Config::AutoHarvestAttackDistanceMeters==1.5f && g_Config::AutoHarvestTekStriderBeamRangeMeters==10.f);
    g_Config::TekSpearParryTapSeconds=.5f;assert(ConfigManager::Get().SaveConfig("extra.ini"));g_Config::TekSpearParryTapSeconds=.03f;
    assert(ConfigManager::Get().LoadConfig("extra.ini"));assert(g_Config::TekSpearParryTapSeconds==.5f);std::filesystem::remove(TestDir/"extra.ini");
    std::filesystem::remove(TestDir/"roundtrip.ini"); std::filesystem::remove(TestDir/"invalid.ini");std::filesystem::remove(TestDir/"legacy.ini");std::filesystem::remove(TestDir);
    std::cout<<"Interface shortcuts, stop behavior, shared limits and config round-trip passed.\n";
}
