#include "../src/internal/ESP/OverlayGeometry.h"
#include "../src/internal/Legit/Aimbot/AimSelection.h"
#include "../src/internal/Config/Configs.h"
#include "../src/internal/Config/ConfigManager.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

int main() {
    using namespace g_ESP::Overlay;
    Point A{-100,50}, B{200,50};
    assert(ClipLine(A,B,100,100) && A.X == 0 && B.X == 100);
    A = {50,-100}; B = {50,200};
    assert(ClipLine(A,B,100,100) && A.Y == 0 && B.Y == 100);
    A = {-20,-20}; B = {-1,-1}; assert(!ClipLine(A,B,100,100));
    A = {10,10}; B = {10,10}; assert(!ClipLine(A,B,100,100));
    A = {std::numeric_limits<double>::quiet_NaN(),10}; B = {20,20};
    assert(!ClipLine(A,B,100,100));
    A = {-std::numeric_limits<double>::max(),10}; B = {std::numeric_limits<double>::max(),10};
    assert(!ClipLine(A,B,100,100));
    A = {10,10}; B = {20,20}; assert(!ClipLine(A,B,0,100));
    assert(InRange(200,200) && !InRange(201,200) && !InRange(-1,200));
    assert(!InRange(1,std::numeric_limits<double>::infinity()));
    assert(!InRange(2001,9000));
    int Degree[8]{};
    for (auto E : BoxEdges) { assert(E[0] != E[1]); ++Degree[E[0]]; ++Degree[E[1]]; }
    for (int D : Degree) assert(D == 3);
    // Production converter used by both actual bone positions and legacy ESP.
    for (double DPI : {1.0,1.25,1.5,2.0}) {
        const auto Space = g_Aimbot::AimCanvasSpace::FromViewport(1920,1080,1920/DPI,1080/DPI);
        const auto Bone = Space.ToCanvas(960,540);
        assert(std::abs(Bone.X - 960/DPI) < .001 && std::abs(Bone.Y - 540/DPI) < .001);
    }
    const auto Directory = std::filesystem::temp_directory_path() / "phoenix-independent-overlay-tests";
    std::filesystem::create_directories(Directory);
    auto& Manager = ConfigManager::Get(); Manager.Initialize(Directory.string());
    assert(!g_Config::bOverlayESP);
    g_Config::bOverlayESP = true; g_Config::bOverlayCreatures = true;
    g_Config::bOverlaySkeleton = true; g_Config::bOverlayStructureBox = false;
    g_Config::OverlayCharacterDistance = 999; g_Config::OverlayStructureDistance = 321;
    g_Config::OverlayLineWidth = 3.25f; g_Config::OverlayFontPixels = 22;
    g_Config::OverlayEnemyColor[0] = .7f;
    // Existing switches remain independent through a round trip.
    g_Config::bModelHighlightEnabled = false; g_Config::bDrawStructures = false;
    assert(Manager.SaveConfig("overlay.ini"));
    g_Config::bOverlayESP = false; g_Config::OverlayLineWidth = 1;
    assert(Manager.LoadConfig("overlay.ini"));
    assert(g_Config::bOverlayESP && g_Config::bOverlayCreatures && g_Config::bOverlaySkeleton);
    assert(!g_Config::bOverlayStructureBox && !g_Config::bModelHighlightEnabled && !g_Config::bDrawStructures);
    assert(g_Config::OverlayCharacterDistance == 999 && g_Config::OverlayStructureDistance == 321);
    assert(g_Config::OverlayLineWidth == 3.25f && g_Config::OverlayFontPixels == 22);
    assert(std::abs(g_Config::OverlayEnemyColor[0] - .7f) < .001f);
    { std::ofstream Old(Directory / "legacy.ini"); Old << "g_Config::bDrawName=true\n"; }
    assert(Manager.LoadConfig("legacy.ini"));
    assert(!g_Config::bOverlayESP && g_Config::bOverlaySkeleton && g_Config::bOverlayStructureBox);
    { std::ofstream Bad(Directory / "invalid.ini");
      Bad << "g_Config::OverlayLineWidth=999\ng_Config::OverlayCharacterDistance=-8\n"
             "g_Config::OverlayFontPixels=999\ng_Config::OverlayStructureDistance=9000\n"; }
    assert(Manager.LoadConfig("invalid.ini"));
    assert(g_Config::OverlayLineWidth == 5 && g_Config::OverlayCharacterDistance == 1);
    assert(g_Config::OverlayFontPixels == 32 && g_Config::OverlayStructureDistance == 2000);
    for (auto Name : {"overlay.ini","legacy.ini","invalid.ini"}) std::filesystem::remove(Directory / Name);
    std::cout << "Independent overlay clipping, DPI and persistence checks passed.\n";
}
