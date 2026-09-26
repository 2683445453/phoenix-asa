// Runs on the existing Canvas path; no second world scan or remote inventory requests.
bool ExpansionVisualObject(SDK::UObject* O) {
    return O && O->Index >= 0 && O->Index < SDK::UObject::GObjects->Num() &&
        SDK::UObject::GObjects->GetByIndex(O->Index) == O && O->Class &&
        !(O->Flags & SDK::EObjectFlags::BeginDestroyed) && !(O->Flags & SDK::EObjectFlags::FinishDestroyed);
}
Shadow::Color ExpansionColor(const float* C) { return {C[0], C[1], C[2], C[3]}; }
void CollectEquipment(SDK::AShooterCharacter* Human, ESPEntry& Entry, std::chrono::steady_clock::time_point Now) {
    if (Now < Entry.nextEquipmentRefresh) return;
    Entry.nextEquipmentRefresh = Now + std::chrono::milliseconds(250);
    Entry.armor.clear(); Entry.weapon.clear();
    Entry.armorIcons = {}; Entry.weaponIcon = {};
    Entry.armorQualityColors = {}; Entry.weaponQualityColor = {};
    auto* GameData = (g_Config::bDrawHeldWeapon || g_Config::bDrawArmorQuality)
        ? SDK::UPrimalGameData::BPGetGameData() : nullptr;
    if (g_Config::bDrawHeldWeapon) {
        auto* W = Human->CurrentWeapon;
        if (ExpansionVisualObject(W)) {
            auto* Item = W->AssociatedPrimalItem;
            Entry.weapon = LanguageManager::Get().TranslateCopy("手持：") +
                (ExpansionVisualObject(Item) ? Item->DescriptiveNameBase.ToString() : W->Class->GetName());
            if (ExpansionVisualObject(Item)) {
                const auto Quality = ResolveItemQuality(Item, GameData);
                Entry.weapon += " (" + LanguageManager::Get().TranslateCopy("品质") + " " +
                    (Quality.name.empty() ? std::to_string(Item->ItemQualityIndex) : Quality.name) + ")";
                Entry.weaponIcon = CaptureItemTexture(Item);
                Entry.weaponQualityColor = Quality.color;
            }
        }
    }
    auto* I = Human->MyInventoryComponent;
    if (g_Config::bDrawArmorQuality && ExpansionVisualObject(I) && I->EquippedItems.Num() >= 0 && I->EquippedItems.Num() <= 64 &&
        (I->EquippedItems.Num() == 0 || I->EquippedItems.IsValid())) {
        std::string Slots[5];
        for (auto* Item : I->EquippedItems) {
            if (!ExpansionVisualObject(Item) || Item->bIsItemSkin) continue;
            const int Slot = static_cast<int>(Item->MyEquipmentType);
            if (Slot < 0 || Slot >= 5) continue;
            const char* Names[] = {"头","身","腿","脚","手"};
            const auto Quality = ResolveItemQuality(Item, GameData);
            Slots[Slot] = LanguageManager::Get().TranslateCopy(Names[Slot]) + ":" +
                (Quality.name.empty() ? LanguageManager::Get().TranslateCopy("品质") + " " +
                    std::to_string(Item->ItemQualityIndex) : Quality.name);
            Entry.armorIcons[Slot] = CaptureItemTexture(Item);
            Entry.armorQualityColors[Slot] = Quality.color;
        }
        for (const auto& Slot : Slots) if (!Slot.empty()) { if (!Entry.armor.empty()) Entry.armor += " / "; Entry.armor += Slot; }
        if (!Entry.armor.empty()) Entry.armor = LanguageManager::Get().TranslateCopy("护甲：") + Entry.armor;
    }
}
void DrawExpansionRadar(SDK::UCanvas* Canvas, SDK::APlayerController* PC, SDK::APrimalCharacter* Human) {
    if (!g_Config::bRadar || !PC || !PC->Pawn) return;
    auto* Draw = Shadow::GetBackgroundDrawList();
    if (!Draw || !Canvas || Canvas->ClipX < 100 || Canvas->ClipY < 100) return;
    const float Size = std::min(PhoenixInterface::FiniteClamp(g_Config::RadarSizePixels,100,360,180),
        std::min(Canvas->ClipX,Canvas->ClipY) - 40);
    const float X = Canvas->ClipX - Size - 20, Y = 30, Half = (Size - 16) * .5f;
    const Shadow::Vec2 Center{X+Size*.5f,Y+Size*.5f};
    Draw->AddRectFilled({X,Y},{Size,Size},{.03f,.04f,.05f,.8f});
    Draw->AddRect({X,Y},{Size,Size},{1,1,1,.35f});
    Draw->AddLine({Center.x-Half,Center.y},{Center.x+Half,Center.y},{1,1,1,.15f});
    Draw->AddLine({Center.x,Center.y-Half},{Center.x,Center.y+Half},{1,1,1,.15f});
    SDK::FVector CameraLocation{}; SDK::FRotator Rotation{};
    PC->GetPlayerViewPoint(&CameraLocation,&Rotation);
    const auto Origin = PC->Pawn->K2_GetActorLocation();
    const double Range = PhoenixInterface::FiniteClamp(g_Config::RadarRangeMeters,10,1000,150)*100.0;
    int Count = 0;
    for (const auto& Ref : s_cachedESPActors) {
        const bool Player = Ref.Type == ActorType::ShooterCharacter;
        const bool Dino = Ref.Type == ActorType::PrimalDinoCharacter;
        if ((!Player && !Dino) || (Dino && g_Config::bCombatESP) || !g_Config::ESPCharacterEnabled(Dino)) continue;
        if (Ref.ObjectIndex < 0 || Ref.ObjectIndex >= SDK::UObject::GObjects->Num() ||
            SDK::UObject::GObjects->GetByIndex(Ref.ObjectIndex) != Ref.Actor || !ExpansionVisualObject(Ref.Actor)) continue;
        auto* A = static_cast<SDK::APrimalCharacter*>(Ref.Actor);
        if (A == PC->Pawn || A == Human || A->bHidden || A->IsDead()) continue;
        if (Dino && g_Config::bEnableFilter && g_Config::entitySearchBuf[0] &&
            !g_Util::IsEntityMatchMulti(A->GetDescriptiveName().ToString(),g_Config::entitySearchBuf)) continue;
        const bool Team = g_ESP::GetRelation(A,Human) == g_ESP::RelationType::Team;
        if (Team && !g_Config::bESPTeamEnabled) continue;
        const auto P = A->K2_GetActorLocation();
        const auto Dot = PhoenixExpansion::ProjectRadar(P.X-Origin.X,P.Y-Origin.Y,Rotation.Yaw,Range);
        if (!Dot.Visible) continue;
        const auto C = ExpansionColor(Team ? g_Config::RadarTeamColor : Player ? g_Config::RadarPlayerColor : g_Config::RadarDinoColor);
        Draw->AddRectFilled({Center.x+float(Dot.X)*Half-2,Center.y+float(Dot.Y)*Half-2},{4,4},C);
        if (++Count >= 512) break;
    }
    Draw->AddTriangleFilled({Center.x,Center.y-5},{Center.x-4,Center.y+4},{Center.x+4,Center.y+4},{1,1,1,1});
}
