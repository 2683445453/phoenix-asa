// Included inside g_ESP; all sizes below are converted from physical pixels.
static float PixelsX(float Value) { return Value * static_cast<float>(ProjectionSpace.ScaleX); }
static float PixelsY(float Value) { return Value * static_cast<float>(ProjectionSpace.ScaleY); }
static Shadow::Vec2 MeasureInfo(SDK::UFont* Font, float Scale, const std::string& Text) {
    Shadow::PushFont(Font, Scale); auto Size = Shadow::MeasureTextSize(Text); Shadow::PopFont(); return Size;
}
static float InfoFontScale(SDK::UFont* Font, float DistanceScale, bool Title) {
    const double Pixels = Layout::FontPixels(g_Config::ESPFontPixels,
        g_Config::ESPScale * DistanceScale * (Title ? 1.0 : 0.9), g_Config::ESPMinFontPixels);
    const float NativeHeight = MeasureInfo(Font, 1.0f, "Ag国").y;
    return NativeHeight > 0 ? static_cast<float>(PixelsY(static_cast<float>(Pixels)) / NativeHeight) : 1.0f;
}
static std::string FitInfoText(SDK::UFont* Font, float Scale, std::string Text, float Width) {
    for (auto& Ch : Text) if (static_cast<unsigned char>(Ch) < 0x20) Ch = ' ';
    if (MeasureInfo(Font, Scale, Text).x <= Width) return Text;
    const std::string Suffix = "…";
    while (!Text.empty() && MeasureInfo(Font, Scale, Text + Suffix).x > Width) {
        size_t Start = Text.size() - 1;
        while (Start > 0 && (static_cast<unsigned char>(Text[Start]) & 0xc0) == 0x80) --Start;
        Text.resize(Start);
    }
    return Text + Suffix;
}
static std::string CompactValue(float Value) {
    if (!std::isfinite(Value) || Value < 0) return "?";
    if (Value >= 1000000) return std::format("{:.1f}M", Value / 1000000);
    if (Value >= 10000) return std::format("{:.1f}k", Value / 1000);
    return std::format("{:.0f}", Value);
}
void DrawEntityInfo(SDK::UCanvas* Canvas, const BoxRect& Box, const EntityInfo& Info, int& PreviousSlot) {
    auto* Draw = Shadow::GetBackgroundDrawList();
    auto* Font = g_Util::GetSansationBold18();
    if (!Canvas || !Draw || !Font || !Box.valid || Info.Alpha <= 0) return;
    if (g_Config::bMinimalESP) {
        if (Info.ShowHealth && Info.MaxHealth > 0 && std::isfinite(Info.Health)) {
            const float Width = Box.bottomRight.x - Box.topLeft.x;
            auto Color = Info.HealthColor; Color.a *= Info.Alpha;
            Draw->AddRectFilled({Box.topLeft.x, Box.bottomRight.y + PixelsY(3)}, {Width, PixelsY(3)}, {0,0,0,Info.Alpha*.7f});
            Draw->AddRectFilled({Box.topLeft.x, Box.bottomRight.y + PixelsY(3)},
                {Width * std::clamp(Info.Health / Info.MaxHealth,0.f,1.f), PixelsY(3)}, Color);
        }
        // Turret ammunition remains readable in combat mode.
        if (!(g_Config::bCombatESP && !Info.Ammo.empty())) return;
    }
    struct Row { std::string Text; Shadow::Color Color; float Scale; float Bar = -1; Shadow::Vec2 Size{};
        std::array<SDK::UTexture2D*, 5> Icons{};
        std::array<Shadow::Color, 5> IconQualityColors{}; };
    std::vector<Row> Rows;
    const float TitleScale = InfoFontScale(Font, Info.Scale, true), DetailScale = InfoFontScale(Font, Info.Scale, false);
    if (!g_Config::bMinimalESP && !Info.Name.empty()) Rows.push_back({Info.Name, Info.NameColor, TitleScale});
    if (!g_Config::bMinimalESP && !Info.Detail.empty()) Rows.push_back({Info.Detail, Info.DetailColor, DetailScale});
    if (!g_Config::bMinimalESP && !Info.Armor.empty()) {
        Rows.push_back({Info.Armor, Info.ArmorColor, DetailScale});
        Rows.back().Icons = Info.ArmorIcons;
        Rows.back().IconQualityColors = Info.ArmorQualityColors;
    }
    if (!g_Config::bMinimalESP && !Info.Weapon.empty()) {
        Rows.push_back({Info.Weapon, Info.WeaponColor, DetailScale});
        Rows.back().Icons[0] = Info.WeaponIcon;
        Rows.back().IconQualityColors[0] = Info.WeaponQualityColor;
    }
    if (!Info.Ammo.empty()) Rows.push_back({Info.Ammo, Info.AmmoColor, DetailScale});
    if (!g_Config::bMinimalESP) {
        for (const auto& Item : Info.LootItems)
            Rows.push_back({LanguageManager::Get().TranslateCopy("物品：") + Item, Info.DetailColor, DetailScale});
    }
    std::string Summary = Info.ShowDistance ? std::format("{:.0f}m", Info.Distance) : "";
    if (Info.ShowHealth) {
        if (!Summary.empty()) Summary += "  ·  ";
        Summary += "生命 " + CompactValue(Info.Health) + " / " + CompactValue(Info.MaxHealth);
    }
    if (!g_Config::bMinimalESP && !Summary.empty()) Rows.push_back({Summary, Info.ShowHealth ? Info.HealthColor : Info.DistanceColor, DetailScale,
        Info.ShowHealth && Info.MaxHealth > 0 ? std::clamp(Info.Health / Info.MaxHealth, 0.0f, 1.0f) : -1.0f});
    if (!g_Config::bMinimalESP && Info.ShowTorpor && Info.MaxTorpor > 0) Rows.push_back({"眩晕 " + CompactValue(Info.Torpor) + " / " + CompactValue(Info.MaxTorpor),
        Info.TorporColor, DetailScale, std::clamp(Info.Torpor / Info.MaxTorpor, 0.0f, 1.0f)});
    if (Rows.empty()) return;
    const float PadX = PixelsX(6), PadY = PixelsY(5), Gap = PixelsY(3), BarH = PixelsY(3);
    const float MaxWidth = std::min(PixelsX(280), Canvas->ClipX - PadX * 4);
    if (MaxWidth <= 0) return;
    const float IconPixelsX = PixelsX(g_Config::ESPIconPixels);
    const float IconPixelsY = PixelsY(g_Config::ESPIconPixels);
    float Width = PixelsX(92), Height = 2 * PadY;
    for (auto& Row : Rows) {
        const size_t IconCount = std::count_if(Row.Icons.begin(), Row.Icons.end(), [](const auto* Icon) { return Icon != nullptr; });
        const float IconWidth = IconCount ? IconCount * IconPixelsX + PixelsX(3) : 0.0f;
        Row.Text = FitInfoText(Font, Row.Scale, Row.Text, MaxWidth - 2 * PadX - IconWidth);
        Row.Size = MeasureInfo(Font, Row.Scale, Row.Text);
        Width = std::max(Width, Row.Size.x + IconWidth + 2 * PadX);
        Row.Size.y = std::max(Row.Size.y, IconCount ? IconPixelsY : 0.0f);
        Height += Row.Size.y + Gap + (Row.Bar >= 0 ? BarH + Gap : 0);
    }
    Height -= Gap;
    std::vector<Layout::Rect> Occupied;
    for (const auto& R : s_occupiedTopLabels) Occupied.push_back({R.minX, R.minY, R.maxX - R.minX, R.maxY - R.minY});
    const Layout::Rect Target{Box.topLeft.x, Box.topLeft.y, Box.bottomRight.x - Box.topLeft.x, Box.bottomRight.y - Box.topLeft.y};
    const auto Position = Layout::Place(Target, Width, Height, Canvas->ClipX, Canvas->ClipY, PixelsY(7),
        Occupied, g_Config::bESPPreventOverlap, PreviousSlot);
    if (!Position) return; // Hide this entire group; never leave an orphan number/bar.
    PreviousSlot = Position->Slot;
    const auto& R = Position->Bounds;
    s_occupiedTopLabels.push_back({static_cast<float>(R.X), static_cast<float>(R.Y),
        static_cast<float>(R.X + R.W), static_cast<float>(R.Y + R.H)});
    const float X = static_cast<float>(R.X), Y = static_cast<float>(R.Y);
    Shadow::Color Accent = Info.NameColor; Accent.a *= Info.Alpha;
    const float AnchorX = std::clamp(X + Width * 0.5f, Box.topLeft.x, Box.bottomRight.x);
    const float AnchorY = std::clamp(Y + Height * 0.5f, Box.topLeft.y, Box.bottomRight.y);
    const float EndX = std::clamp(AnchorX, X, X + Width), EndY = std::clamp(AnchorY, Y, Y + Height);
    Draw->AddLine({AnchorX, AnchorY}, {EndX, EndY}, {0, 0, 0, Info.Alpha * 0.9f}, PixelsX(3));
    Draw->AddLine({AnchorX, AnchorY}, {EndX, EndY}, Accent, PixelsX(1));
    Draw->AddRectFilled({X, Y}, {PixelsX(2), Height}, Accent);
    float CursorY = Y + PadY;
    for (const auto& Row : Rows) {
        auto Color = Row.Color; Color.a *= Info.Alpha;
        float TextX = X + PadX;
        const SDK::FVector2D Zero{0.0, 0.0}, One{1.0, 1.0};
        for (size_t Slot = 0; Slot < Row.Icons.size(); ++Slot) if (auto* Icon = Row.Icons[Slot]) {
            const auto& QualityColor = Row.IconQualityColors[Slot];
            if (Canvas->DefaultTexture && QualityColor.a > 0.0f)
                Canvas->K2_DrawTexture(Canvas->DefaultTexture, {TextX, CursorY}, {IconPixelsX, IconPixelsY},
                    Zero, One, {QualityColor.r, QualityColor.g, QualityColor.b, Info.Alpha * 0.8f},
                    SDK::EBlendMode::BLEND_Translucent, 0.0f, Zero);
            const float InsetX = QualityColor.a > 0.0f ? PixelsX(2) : 0.0f;
            const float InsetY = QualityColor.a > 0.0f ? PixelsY(2) : 0.0f;
            Canvas->K2_DrawTexture(Icon, {TextX + InsetX, CursorY + InsetY},
                {IconPixelsX - 2 * InsetX, IconPixelsY - 2 * InsetY},
                Zero, One, {1.0f, 1.0f, 1.0f, Info.Alpha},
                SDK::EBlendMode::BLEND_Translucent, 0.0f, Zero);
            TextX += IconPixelsX;
        }
        if (TextX > X + PadX) TextX += PixelsX(3);
        Draw->AddText(Font, Row.Scale, {0, 0, 0, Info.Alpha}, {0, 0, 0, Info.Alpha},
            {TextX, CursorY}, Color, Row.Text);
        CursorY += Row.Size.y + Gap;
        if (Row.Bar >= 0) {
            Draw->AddRectFilled({X + PadX, CursorY}, {Width - 2 * PadX, BarH}, {0.15f, 0.17f, 0.19f, Info.Alpha});
            Draw->AddRectFilled({X + PadX, CursorY}, {(Width - 2 * PadX) * Row.Bar, BarH}, Color);
            CursorY += BarH + Gap;
        }
    }
}
