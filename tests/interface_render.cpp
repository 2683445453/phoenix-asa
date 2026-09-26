#include "../src/internal/Menu/StartupPresentation.h"
// Actual ImGui menu rendering with game-side services stubbed; never loads or controls a game.
#include "../src/external/Minimal-D3D12-Hook-ImGui/Main/hooks.h"
#include "../src/internal/Menu/FeatureCatalog.h"
#include "../src/internal/Hack/Hack.h"
#include "../src/internal/Hack/TekSpear.h"
#include "../src/internal/ESP/DrawESP.h"
#include "../src/internal/AActor/Tick.h"
#include "../src/internal/Config/ConfigManager.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
static bool CheckToggleIds = false;
static std::unordered_set<ImGuiID> ToggleIds;

struct RenderItem { ImRect Bounds; std::string Label; ImGuiWindow* Window=nullptr; };
std::unordered_map<ImGuiID,RenderItem> RenderItems;
std::string RenderedText;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* C,ImGuiID Id,const ImRect& R,const ImGuiLastItemData*) {
    if(Id){auto& Item=RenderItems[Id];Item.Bounds=R;Item.Window=C->CurrentWindow;}
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*,ImGuiID Id,const char* Label,ImGuiItemStatusFlags Flags) {
    if (CheckToggleIds && (Flags & ImGuiItemStatusFlags_Checkable)) assert(ToggleIds.insert(Id).second);
    if(Id&&Label)RenderItems[Id].Label=Label;
}
void ImGuiTestEngineHook_Log(ImGuiContext*,const char*,...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*,ImGuiID Id) {
    auto It=RenderItems.find(Id);return It==RenderItems.end()?nullptr:It->second.Label.c_str();
}

namespace SDK { UWorld* UWorld::GetWorld(){return nullptr;} }
namespace g_MDX12::g_MenuState { bool g_isOpen=true; UINT* g_pCurrentBindingKey=nullptr; }
namespace g_Hook { void RequestUnload(){} }
namespace g_AActor { TickDiagnostics GetTickDiagnostics(){return {};} }
namespace g_DrawESP { ResourceESPDiagnostics GetResourceESPDiagnostics(){return {};} }
namespace g_Aimbot {
    std::string GetAimbotStatus(){return "离线界面预览 · 未连接游戏";}
    std::string GetMagicBulletStatus(){return "等待有效目标";}
}
namespace g_TekSpear {
    std::string GetStatus(){return "等待装备泰克长矛";} std::string GetProjectileStatus(){return "识别 0 · 移动 0 · 接近 0";}
    void RequestRetarget(){} void RequestReset(){}
}
namespace g_Hack {
#define STATUS(Name) std::string Name(){return LanguageManager::Get().IsEnglish()?"Offline preview · waiting for game state":"离线预览 · 等待游戏状态";}
    STATUS(GetDinoHarvestRangeStatus) STATUS(GetAutoHarvestStatus) STATUS(GetWeaponFireRateStatus) STATUS(GetNoteXPBuffStatus)
    STATUS(GetUploadTerminalStatus) STATUS(GetAutoFeedStatus) STATUS(GetDaeodonHealingStatus)
    STATUS(GetAutoLootStatus) STATUS(GetAutoPickupStatus) STATUS(GetAutoEatDrinkStatus)
    STATUS(GetAutoBrewStatus) STATUS(GetAutoPullGachaStatus) STATUS(GetFishingQTEStatus)
    STATUS(GetFishingRodMaintenanceStatus) STATUS(GetAutoRepairStatus) STATUS(GetAutoCraftStatus)
    STATUS(GetFreeCameraStatus) STATUS(GetTekEquipmentStatus) STATUS(GetArmorSwapStatus)
    STATUS(GetAutoBuffSoupStatus) STATUS(GetFireExtinguishStatus)
    STATUS(GetInstantReloadStatus) STATUS(GetInstantChargeStatus) STATUS(GetTekLauncherSlotStatus)
    STATUS(GetFloatingStructureStatus)
    STATUS(GetExpansionCombatStatus) STATUS(GetHusbandryStatus)
    STATUS(GetNativeMountStatus) STATUS(GetDeinoAirLatchStatus)
    STATUS(GetDeinoGlideProbeStatus) STATUS(GetDeinoGlideProbePath)
    STATUS(GetGrinderDepositStatus) STATUS(GetBlueprintDepositStatus) STATUS(GetAutoDropStatus) STATUS(GetUnderwaterWeaponStatus)
#undef STATUS
    void RequestNoteXPBuff(){} void CancelNoteXPBuff(){} void RequestOpenUploadTerminal(){}
    void RequestAllocateLevel(int){}
    bool CanAllocateLevel(){return false;}
    void RequestEnableNearbyDaeodonHealing(){} void DumpServerInfo(){}
    std::vector<AutoCraftStationView> GetAutoCraftCatalog(SDK::UWorld*){return {};}
    int GetSelectedAutoCraftStation(){return -1;} int GetSelectedAutoCraftRecipe(){return -1;}
    void SelectAutoCraftStation(int){} void SelectAutoCraftRecipe(int){}
}
namespace g_DrawImGui {
    // Legacy tools are outside this redesign's page-body coverage.
    void EntityList_Menu(){} void StructureList_Menu(){} void ItemList_Menu(){}
    void Language_Menu(){} void Configs_Menu(){} void Lua_Menu(){}
}

void Snapshot(const char* Name) {
    auto* Data=ImGui::GetDrawData();int W=int(Data->DisplaySize.x),H=int(Data->DisplaySize.y);
    unsigned char* Atlas=nullptr;int AW,AH;ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&Atlas,&AW,&AH);
    std::vector<unsigned char> Pixels(size_t(W)*H*3,20);
    auto Edge=[](ImVec2 A,ImVec2 B,ImVec2 P){return (P.x-A.x)*(B.y-A.y)-(P.y-A.y)*(B.x-A.x);};
    for(auto* L:Data->CmdLists) for(const auto& C:L->CmdBuffer) {
        if(C.UserCallback)continue;
        for(unsigned J=0;J+2<C.ElemCount;J+=3) {
            const auto& A=L->VtxBuffer[L->IdxBuffer[C.IdxOffset+J]+C.VtxOffset];
            const auto& B=L->VtxBuffer[L->IdxBuffer[C.IdxOffset+J+1]+C.VtxOffset];
            const auto& D=L->VtxBuffer[L->IdxBuffer[C.IdxOffset+J+2]+C.VtxOffset];
            float Area=Edge(A.pos,B.pos,D.pos);if(std::abs(Area)<.001f)continue;
            int X0=std::max({0,int(std::floor(std::min({A.pos.x,B.pos.x,D.pos.x}))),int(C.ClipRect.x)});
            int Y0=std::max({0,int(std::floor(std::min({A.pos.y,B.pos.y,D.pos.y}))),int(C.ClipRect.y)});
            int X1=std::min({W,int(std::ceil(std::max({A.pos.x,B.pos.x,D.pos.x}))),int(C.ClipRect.z)});
            int Y1=std::min({H,int(std::ceil(std::max({A.pos.y,B.pos.y,D.pos.y}))),int(C.ClipRect.w)});
            for(int Y=Y0;Y<Y1;++Y)for(int X=X0;X<X1;++X){
                ImVec2 P{X+.5f,Y+.5f};float U=Edge(B.pos,D.pos,P)/Area,V=Edge(D.pos,A.pos,P)/Area,T=1-U-V;
                if(U<0||V<0||T<0)continue;
                float TX=(U*A.uv.x+V*B.uv.x+T*D.uv.x)*AW-.5f,TY=(U*A.uv.y+V*B.uv.y+T*D.uv.y)*AH-.5f;
                int IX=int(std::floor(TX)),IY=int(std::floor(TY));float FX=TX-IX,FY=TY-IY;
                float Tex[4]{};
                for(int DY=0;DY<2;++DY)for(int DX=0;DX<2;++DX){
                    size_t Off=(size_t(std::clamp(IY+DY,0,AH-1))*AW+std::clamp(IX+DX,0,AW-1))*4;
                    float Weight=(DX?FX:1-FX)*(DY?FY:1-FY);
                    for(int K=0;K<4;++K)Tex[K]+=Atlas[Off+K]*Weight/255.f;
                }
                float Alpha=Tex[3]*(U*((A.col>>24)&255)+V*((B.col>>24)&255)+T*((D.col>>24)&255))/255.f;
                for(int K=0;K<3;++K){float Color=Tex[K]*(U*((A.col>>(K*8))&255)+V*((B.col>>(K*8))&255)+T*((D.col>>(K*8))&255));
                    auto& Out=Pixels[(size_t(Y)*W+X)*3+K];Out=static_cast<unsigned char>(std::clamp(Color*Alpha+Out*(1-Alpha),0.f,255.f));}
            }
        }
    }
    std::ofstream Out(std::string("release/ui-work/")+Name+".ppm",std::ios::binary);
    Out<<"P6\n"<<W<<" "<<H<<"\n255\n";Out.write(reinterpret_cast<char*>(Pixels.data()),Pixels.size());
}

void Frame(g_DrawImGui::Topic T,bool Help,int W,int H,const char* Image=nullptr) {
    using namespace g_DrawImGui;
    SelectTopic(T,false);g_Config::bShowFeatureHelp=Help;
    ImGui::GetIO().DisplaySize={float(W),float(H)};
    for(int I=0;I<18;++I){
        RenderItems.clear();ToggleIds.clear();ImGui::NewFrame();
        if(I==17)ImGui::LogToBuffer();
        MyImGuiDraw(nullptr,0,0);
        if(I==17){RenderedText=GImGui->LogBuffer.c_str();ImGui::LogFinish();}
        ImGui::Render();
        assert(ImGui::GetCurrentContext()->CurrentWindowStack.Size==0);
    }
    auto* Win=ImGui::FindWindowByName("phoenix###phoenixMain");assert(Win);
    assert(Win->Size.x<=W&&Win->Size.y<=H);
    if(Image)Snapshot(Image);
}
void DrawOnce(){
    RenderItems.clear();ToggleIds.clear();ImGui::NewFrame();g_DrawImGui::MyImGuiDraw(nullptr,0,0);ImGui::Render();
    assert(ImGui::GetCurrentContext()->CurrentWindowStack.Size==0);
}
void AssertIntroAboveMenu(ImGuiWindow* Intro){
    auto* Main=ImGui::FindWindowByName("phoenix###phoenixMain");assert(Main&&Intro&&Intro->Active);
    int IntroLayer=-1,LastMenuLayer=-1,Layer=0;
    for(auto* List:ImGui::GetDrawData()->CmdLists){
        if(List==Intro->DrawList)IntroLayer=Layer;
        for(auto* Window:GImGui->Windows)
            if(Window->RootWindow==Main&&Window->DrawList==List)LastMenuLayer=Layer;
        ++Layer;
    }
    // An active window can still be completely obscured by a later menu draw list.
    assert(LastMenuLayer>=0&&IntroLayer>LastMenuLayer);
}
ImGuiWindow* HarvestWindow(){
    for(auto* W:ImGui::GetCurrentContext()->Windows)
        if(W->Active&&std::string(W->Name).find("ResourceHarvestRegion")!=std::string::npos)return W;
    assert(false);return nullptr;
}
void HarvestScroll(float Ratio,int Width,int Height,const char* Name){
    auto* W=HarvestWindow();ImGui::SetScrollY(W,W->ScrollMax.y*Ratio);
    Frame(g_DrawImGui::Topic::Harvest,true,Width,Height,Name);
}
void StartupCases(){
    using namespace g_DrawImGui;
    g_Config::bInitWelcome=false;g_Config::bShowWelcome=true;
    g_MDX12::g_MenuState::g_isOpen=true;
    Frame(Topic::Home,false,1366,900,"startup-initial");
    auto* Intro=ImGui::FindWindowByName("phoenix##welcome");
    assert(Intro&&Intro->Active&&PhoenixStartup::Current.Alpha>0&&!g_Config::bInitWelcome);
    AssertIntroAboveMenu(Intro);
    assert(Intro->Pos.x>=0&&Intro->Pos.y>=0&&Intro->Pos.x+Intro->Size.x<=1366&&Intro->Pos.y+Intro->Size.y<=900);
    Frame(Topic::Home,false,1366,900);
    Frame(Topic::Home,false,1366,900,"startup-initial-hold");
    assert(PhoenixStartup::Current.Alpha==1);AssertIntroAboveMenu(Intro);
    // Config persistence must suppress automatic playback, while the real replay button remains usable.
    ConfigManager::Get().Initialize("release/ui-work/startup-config-qa");
    g_Config::bShowWelcome=false;assert(ConfigManager::Get().SaveConfig("welcome-disabled.ini"));
    g_Config::bShowWelcome=true;assert(ConfigManager::Get().LoadConfig("welcome-disabled.ini"));
    assert(!g_Config::bShowWelcome);
    PhoenixStartup::Current={};
    Frame(Topic::Interface,false,1366,900,"startup-disabled");
    assert(PhoenixStartup::Current.Alpha==0);
    RenderItem Replay;bool Found=false;
    for(const auto& [Id,Item]:RenderItems)if(Item.Label==Tr(U8("重播启动动画"))){Replay=Item;Found=true;break;}
    assert(Found&&Replay.Window&&Replay.Window->ClipRect.Contains(Replay.Bounds.GetCenter()));
    const auto Center=Replay.Bounds.GetCenter();auto& IO=ImGui::GetIO();
    IO.AddMousePosEvent(Center.x,Center.y);IO.AddMouseButtonEvent(0,true);DrawOnce();
    IO.AddMouseButtonEvent(0,false);DrawOnce();
    assert(PhoenixStartup::Current.Manual&&!g_Config::bShowWelcome);
    IO.AddMousePosEvent(-100,-100);
    Frame(Topic::Interface,false,1366,900,"startup-replayed");
    assert(PhoenixStartup::Current.Alpha>0&&Intro->Active&&!g_Config::bInitWelcome);
    AssertIntroAboveMenu(Intro);
    Frame(Topic::Interface,false,1366,900);
    Frame(Topic::Interface,false,1366,900,"startup-replayed-hold");
    assert(PhoenixStartup::Current.Alpha==1);AssertIntroAboveMenu(Intro);
    Found=false;
    for(const auto& [Id,Item]:RenderItems)if(Item.Label==Tr(U8("关闭启动展示"))){Replay=Item;Found=true;break;}
    assert(Found&&Replay.Window==Intro&&Intro->ClipRect.Contains(Replay.Bounds.GetCenter()));
    const auto Dismiss=Replay.Bounds.GetCenter();
    IO.AddMousePosEvent(Dismiss.x,Dismiss.y);IO.AddMouseButtonEvent(0,true);DrawOnce();
    IO.AddMouseButtonEvent(0,false);DrawOnce();
    assert(PhoenixStartup::Current.Finished&&!g_Config::bShowWelcome);
    IO.AddMousePosEvent(-100,-100);
}
void HarvestCases(const char* Locale){
    using namespace g_DrawImGui;
    g_Config::bAutoHarvestResources=true;g_Config::bAutoHarvestTekStrider=false;
    g_Config::bDinoHarvestRange=true;g_Config::bAutoHarvestApproach=true;
    g_Config::bAutoHarvestSafeNavigation=true;g_Config::bAutoDropResources=true;
    g_Config::AutoDropResourceMask=2047;
    const auto Name=[&](const char* Part){return std::string("harvest-")+Part+"-"+Locale;};
    Frame(Topic::Harvest,true,1366,900);
    HarvestScroll(0,1366,900,Name("ordinary-top").c_str());
    const auto Targets=RenderedText.find(Tr(U8("采集目标")));
    const auto Metal=RenderedText.find(LanguageManager::Visuals_Menu::ResourceMetal,Targets);
    const auto Attack=RenderedText.find(Tr(U8("期望停靠距离")),Targets);
    const auto Drop=RenderedText.find(Tr(U8("背包清理")),Targets);
    assert(Targets!=std::string::npos&&Targets<Metal&&Metal<Attack&&Attack<Drop);
    assert(RenderedText.find(Tr(U8("本地近战半径倍率")))!=std::string::npos);
    assert(RenderedText.find(Tr(U8("跨步者采集光束距离")))==std::string::npos);
    HarvestScroll(.5f,1366,900,Name("ordinary-middle").c_str());
    HarvestScroll(1,1366,900,Name("ordinary-bottom").c_str());
    auto* Body=HarvestWindow();assert(Body->ScrollMax.y>0&&Body->Scroll.y>0);
    g_Config::bAutoHarvestTekStrider=true;
    Frame(Topic::Harvest,true,1366,900);
    HarvestScroll(0,1366,900,Name("strider-top").c_str());
    assert(RenderedText.find(Tr(U8("跨步者采集光束距离")))!=std::string::npos);
    assert(RenderedText.find(Tr(U8("期望停靠距离")))==std::string::npos);
    assert(RenderedText.find(Tr(U8("本地近战半径倍率")))==std::string::npos);
    HarvestScroll(1,1366,900,Name("strider-bottom").c_str());
    g_Config::bAutoHarvestTekStrider=false;
    Frame(Topic::Harvest,true,520,720);
    HarvestScroll(0,520,720,Name("narrow-top").c_str());
    HarvestScroll(.5f,520,720,Name("narrow-middle").c_str());
    HarvestScroll(1,520,720,Name("narrow-bottom").c_str());
    Frame(Topic::Harvest,true,1366,900);HarvestScroll(0,1366,900,nullptr);
}
int main(){
    using namespace g_DrawImGui;
    ImGui::CreateContext();auto& IO=ImGui::GetIO();IO.IniFilename=nullptr;IO.DeltaTime=1.f/60;
    IO.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf",18);
    ImFontConfig Merge;Merge.MergeMode=true;
    IO.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc",18,&Merge,IO.Fonts->GetGlyphRangesChineseFull());
    g_TitleFont=IO.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/georgia.ttf",22);
    IO.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/simsun.ttc",22,&Merge,IO.Fonts->GetGlyphRangesChineseFull());
    IO.Fonts->Build();IO.Fonts->SetTexID(1);
    ImGui::GetCurrentContext()->TestEngineHookItems=true;
    SetupCustomImGuiStyle();g_Config::bInitWelcome=true;g_Config::bShowWelcome=false;
    g_Hook::ActorTickOK=g_Hook::PostRenderOK=g_Hook::UpdateCameraOK=true;
    g_Hook::PhysicsRotationOK=g_Hook::TakeDamageOK=g_Hook::ClientNotifyRespawnedOK=g_Hook::ShotgunSpreadOK=true;
    StartupCases();
    Frame(Topic::Home,false,1366,900,"home");
    Frame(Topic::Aim,false,1366,900,"aim-off");
    auto Height=[](){for(auto* W:ImGui::GetCurrentContext()->Windows)if(std::string(W->Name).find("CombatDetail")!=std::string::npos)return W->ContentSize.y;return 0.f;};
    float OffHeight=Height();
    g_Config::bAimbotEnabled=true;
    Frame(Topic::Aim,false,1366,900,"aim-on");assert(Height()>OffHeight+100);
    g_Config::bAimbotEnabled=false;Frame(Topic::Aim,false,1366,900);assert(Height()==OffHeight);
    for(auto& A:PhoenixShortcuts::Actions)*A.Enabled=true;
    g_Config::bAutoFeed=g_Config::bAutoEatDrink=g_Config::bAutoMedicalBrew=g_Config::bAutoEnergyBrew=true;
    g_Config::bDrawStructures=g_Config::bEnableOOF=g_Config::bDrawWater=true;
    for(const auto& F:Features){Frame(F.TopicId,false,1366,900);Frame(F.TopicId,true,1366,900);}
    Frame(Topic::Care,false,1366,900,"care-off-cn");
    g_Config::bAutoClaimBabies = g_Config::bAutoFeedBabies = g_Config::bAutoFillGacha = true;
    Frame(Topic::Care,true,1366,900,"care-on-cn");
    assert(RenderedText.find(Tr(U8("照料搜索范围")))!=std::string::npos);
    Frame(Topic::Care,true,520,720,"care-narrow-cn");
    g_Config::bRadar=true;g_Config::bDrawArmorQuality=g_Config::bDrawHeldWeapon=true;
    Frame(Topic::Objects,true,1366,900,"equipment-cn");
    Frame(Topic::World,true,1366,900,"radar-cn");
    Frame(Topic::Loot,true,1366,900,"loot-help");
    Frame(Topic::Shortcuts,false,1366,900,"shortcuts");
    Frame(Topic::Home,false,800,640,"compact");
    Frame(Topic::Harvest,true,520,720,"narrow");
    Frame(Topic::Home,false,1366,900);
    HarvestCases("cn");
    static float Color[4]={.04f,.52f,1,.8f};
    LanguageManager::Get().Initialize("language");
    assert(LanguageManager::Get().LoadLanguage("EN.ini"));assert(LanguageManager::Get().IsEnglish());
    assert(std::string(Tr(U8("自动丢弃所选物资")))=="Auto drop selected resources");
    assert(std::string(LanguageManager::Misc_Menu::FreeCameraEnabled)=="Local Free Camera (default Z)");
    HarvestCases("en");
    for(const auto& F:Features) Frame(F.TopicId,true,1366,900);
    Frame(Topic::Care,true,1366,900,"care-en");
    assert(RenderedText.find("Care search range")!=std::string::npos);
    Frame(Topic::Harvest,true,1366,900,"harvest-en");
    Frame(Topic::Gacha,true,1366,900,"gacha-en");
    g_Config::bInstantReload = g_Config::bWeaponFireRate = true;
    CheckToggleIds = true;
    Frame(Topic::Weapons,true,1366,900,"weapons-en");
    assert(LanguageManager::Get().LoadLanguage("CN.ini"));
    Frame(Topic::Weapons,true,1366,900,"weapons-cn");
    CheckToggleIds = false;
    // Fade the old page fully out before replacing it, then fade in the new one.
    SelectTopic(Topic::Fishing);bool SawOut=false,SawIn=false;
    for(int I=0;I<24;++I){ImGui::NewFrame();MyImGuiDraw(nullptr,0,0);ImGui::Render();
        if(IsTopic(Topic::Weapons)&&TopicFade.Alpha<1)SawOut=true;
        if(IsTopic(Topic::Fishing)&&TopicFade.Alpha>0&&TopicFade.Alpha<1)SawIn=true;}
    assert(SawOut&&SawIn&&IsTopic(Topic::Fishing)&&TopicFade.Alpha==1);
    assert(LanguageManager::Get().LoadLanguage("CN.ini"));assert(!LanguageManager::Get().IsEnglish());
    Frame(Topic::Harvest,true,1366,900,"harvest-cn");
    Frame(Topic::Gacha,true,1366,900,"gacha-cn");
    // A control's help is absent until hovered, independent of whether enabled.
    IO.MousePos={-100,-100};bool Enabled=false;ImVec2 Center;
    for(int I=0;I<48;++I) {
        ImGui::NewFrame();
        HoverHelp::BeginFrame();HoverHelp::Guide=U8("自动丢弃所选物资");
        ImGui::SetNextWindowPos({380,190});ImGui::SetNextWindowSize({460,120});
        ImGui::Begin("Hover QA",nullptr,ImGuiWindowFlags_NoSavedSettings);
        DrawCustomCheckbox(Tr(U8("自动丢弃所选物资")),&Enabled);Center=ImGui::GetItemRectMin();Center.x+=35;Center.y+=8;
        const float BeforeHelp=ImGui::GetCursorPosY();HelpText(Tr(U8("请先勾选要丢弃的物资；未选择时不会丢弃")));assert(ImGui::GetCursorPosY()==BeforeHelp);
        ImGui::End();HoverHelp::Draw();ImGui::Render();
        if(I==12){assert(HoverHelp::Alpha==0);IO.MousePos=Center;}
    }
    assert(!Enabled&&HoverHelp::Alpha==1);Snapshot("hover-help");
    IO.MousePos={-100,-100};
    for(int I=0;I<16;++I){ImGui::NewFrame();MyImGuiDraw(nullptr,0,0);ImGui::Render();}
    assert(HoverHelp::Alpha==0);
    ColorPalette::Show(Color,{740,180});
    for(int I=0;I<16;++I){ImGui::NewFrame();MyImGuiDraw(nullptr,0,0);ImGui::Render();}
    assert(ColorPalette::Alpha==1);Snapshot("palette");
    ColorPalette::Open=false;
    ImGui::NewFrame();MyImGuiDraw(nullptr,0,0);ImGui::Render();assert(ColorPalette::Alpha>0&&ColorPalette::Alpha<1);
    for(int I=0;I<16;++I){ImGui::NewFrame();MyImGuiDraw(nullptr,0,0);ImGui::Render();}
    assert(ColorPalette::Alpha==0&&ColorPalette::Target==nullptr);
    // Switching to an old partial language file must not retain English labels.
    LanguageFile Partial;Partial.name="qa-partial";Partial.isWorkshop=true;Partial.content="[Aimbot_Menu]\nTabLabel=Partial\n";
    LanguageManager::Get().GetLanguages().push_back(Partial);
    assert(LanguageManager::Get().LoadLanguage("EN.ini"));
    assert(LanguageManager::Get().LoadLanguage("qa-partial"));assert(!LanguageManager::Get().IsEnglish());
    assert(std::string(Tr(U8("自动丢弃所选物资")))==U8("自动丢弃所选物资"));
    assert(std::string(LanguageManager::Misc_Menu::FreeCameraEnabled)==U8("灵魂出窍（本地飞行，默认 Z）"));
    std::cout<<"30 CN/EN topics, startup without gameplay initialization/canvas, saved-off manual replay, harvest mode/filter/drop ordering, narrow/scrolling layouts, hidden parameters, hover help, fades and ImGui stacks passed.\n";
    ImGui::DestroyContext();
}
