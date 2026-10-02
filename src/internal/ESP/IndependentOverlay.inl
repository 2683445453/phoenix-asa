// Included inside g_DrawESP. Reads actual posed mesh sockets, never changes
// mesh materials, CustomDepth, bone transforms, or actor visibility.
struct OverlayRig {
    int AssetIndex = -1, AssetSerial = -1;
    std::vector<SDK::FName> Names;
    std::vector<int> Parents;
};
static std::unordered_map<SDK::USkeletalMesh*, OverlayRig> s_overlayRigs;
static SDK::UWorld* s_overlayWorld = nullptr;

static void DrawIndependentOverlay(SDK::UCanvas* Canvas, SDK::APlayerController* PC,
    SDK::APrimalCharacter* LocalChar) {
    using namespace g_ESP::Overlay;
    if (s_overlayWorld != _DD::World || !g_Config::bOverlayESP) {
        s_overlayRigs.clear();
        s_overlayWorld = _DD::World;
    }
    auto* Draw = Shadow::GetBackgroundDrawList();
    if (!g_Config::bOverlayESP || !Canvas || !PC || !PC->Pawn || !Draw) return;
    int32_t ViewW = 0, ViewH = 0;
    PC->GetViewportSize(&ViewW, &ViewH);
    if (ViewW <= 0 || ViewH <= 0 || Canvas->ClipX <= 0 || Canvas->ClipY <= 0) return;
    const float ScaleX = Canvas->ClipX / ViewW, ScaleY = Canvas->ClipY / ViewH;
    const float Thickness = PhoenixInterface::FiniteClamp(g_Config::OverlayLineWidth, .5f, 5.f, 1.5f) * ScaleX;
    int LineBudget = 4096, RigBudget = 32, BoneBudget = 4096, NewRigBudget = 2;
    const auto Line = [&](const SDK::FVector2D& A, const SDK::FVector2D& B, Shadow::Color Color) {
        Point From{A.X, A.Y}, To{B.X, B.Y};
        if (LineBudget <= 0 || !ClipLine(From, To, Canvas->ClipX, Canvas->ClipY)) return;
        --LineBudget;
        Draw->AddLine({float(From.X), float(From.Y)}, {float(To.X), float(To.Y)},
            {0,0,0,Color.a * .8f}, Thickness + 1.5f * ScaleX);
        Draw->AddLine({float(From.X), float(From.Y)}, {float(To.X), float(To.Y)}, Color, Thickness);
    };
    struct Candidate { SDK::AActor* Actor; ActorType Type; float Distance; };
    std::vector<Candidate> Candidates;
    for (const auto& Ref : s_cachedESPActors) {
        const bool Structure = Ref.Type == ActorType::PrimalStructure;
        const bool Player = Ref.Type == ActorType::ShooterCharacter;
        const bool Creature = Ref.Type == ActorType::PrimalDinoCharacter || Ref.Type == ActorType::PrimalCharacter;
        if (!(Structure && g_Config::bOverlayStructures) && !(Player && g_Config::bOverlayPlayers) &&
            !(Creature && g_Config::bOverlayCreatures)) continue;
        if (Ref.ObjectIndex < 0 || Ref.ObjectIndex >= SDK::UObject::GObjects->Num()) continue;
        auto* Object = SDK::UObject::GObjects->GetByIndex(Ref.ObjectIndex);
        if (!Object || Object != Ref.Actor || !Object->Class || Object->Index != Ref.ObjectIndex ||
            (Object->Flags & SDK::EObjectFlags::BeginDestroyed) || (Object->Flags & SDK::EObjectFlags::FinishDestroyed)) continue;
        auto* Actor = Ref.Actor;
        if (Actor == PC->Pawn || Actor == _DD::LocalSC || Actor->bHidden) continue;
        const float Distance = PC->Pawn->GetDistanceTo(Actor) * .01f;
        if (InRange(Distance, Structure ? g_Config::OverlayStructureDistance : g_Config::OverlayCharacterDistance))
            Candidates.push_back({Actor, Ref.Type, Distance});
    }
    // Bound both bone queries and building traversal; nearest candidates win.
    const auto Nearer = [](const Candidate& A, const Candidate& B) { return A.Distance < B.Distance; };
    if (Candidates.size() > 128) {
        std::partial_sort(Candidates.begin(), Candidates.begin() + 128, Candidates.end(), Nearer);
        Candidates.resize(128);
    } else std::sort(Candidates.begin(), Candidates.end(), Nearer);
    std::vector<HighlightObjectRef> Meshes;
    for (const auto& Candidate : Candidates) {
        auto* Actor = Candidate.Actor;
        const bool Structure = Candidate.Type == ActorType::PrimalStructure;
        auto* Character = Structure ? nullptr : static_cast<SDK::APrimalCharacter*>(Actor);
        auto* Building = Structure ? static_cast<SDK::APrimalStructure*>(Actor) : nullptr;
        if (Character && Character->IsDead()) continue;
        if (Building && (Building->bIsPreviewStructure || !std::isfinite(Building->Health) || Building->Health <= 0)) continue;
        bool Friendly = Character ? g_ESP::GetRelation(Character, LocalChar) == g_ESP::RelationType::Team :
            LocalChar && LocalChar->TargetingTeam != 0 && LocalChar->TargetingTeam == Building->TargetingTeam;
        if (Building && !Friendly && LocalChar && (LocalChar->TargetingTeam == 0 || Building->TargetingTeam == 0)) {
            const auto Tribe = LocalChar->TribeName.ToString();
            Friendly = !Tribe.empty() && Tribe != "None" && Tribe == Building->OwnerName.ToString();
        }
        if (g_Config::bOverlayEnemyOnly && Friendly) continue;
        const float* RGBA = Friendly ? g_Config::OverlayTeamColor : Structure ?
            g_Config::OverlayStructureColor : g_Config::OverlayEnemyColor;
        Shadow::Color Color{RGBA[0],RGBA[1],RGBA[2],RGBA[3]};
        if (Color.a <= 0) continue;

        // World bounds for buildings include only their rendered meshes,
        // avoiding turret range spheres and collision triggers.
        SDK::FVector Origin{}, Extent{};
        bool HasBounds = false;
        if (Building) {
            CollectStructureMeshes(Building, Meshes);
            SDK::FVector Min{}, Max{};
            for (const auto& Ref : Meshes) {
                SDK::FVector O{}, E{}; float Radius = 0;
                SDK::UKismetSystemLibrary::GetComponentBounds(Ref.Component, &O, &E, &Radius);
                if (!IsFiniteResourceVector(O) || !IsFiniteResourceVector(E) || E.X <= 0 || E.Y <= 0 || E.Z <= 0 ||
                    E.X > 250000 || E.Y > 250000 || E.Z > 250000) continue;
                auto Low = O - E, High = O + E;
                if (!HasBounds) { Min = Low; Max = High; HasBounds = true; }
                else {
                    Min = {std::min(Min.X,Low.X),std::min(Min.Y,Low.Y),std::min(Min.Z,Low.Z)};
                    Max = {std::max(Max.X,High.X),std::max(Max.Y,High.Y),std::max(Max.Z,High.Z)};
                }
            }
            if (HasBounds) { Origin = (Min + Max) * .5; Extent = (Max - Min) * .5; }
        } else if (Character->Mesh) {
            int32_t Serial = -1;
            if (!TryGetLiveObjectSerial(Character->Mesh, Character->Mesh->Index, &Serial) ||
                (Character->Mesh->Flags & SDK::EObjectFlags::BeginDestroyed) ||
                (Character->Mesh->Flags & SDK::EObjectFlags::FinishDestroyed)) continue;
            float Radius = 0;
            SDK::UKismetSystemLibrary::GetComponentBounds(Character->Mesh, &Origin, &Extent, &Radius);
            HasBounds = IsFiniteResourceVector(Origin) && IsFiniteResourceVector(Extent) &&
                Extent.X > 0 && Extent.Y > 0 && Extent.Z > 0;
        }
        if (!HasBounds) continue;
        std::array<SDK::FVector2D,8> Corners{};
        std::array<bool,8> Projected{};
        double Left = std::numeric_limits<double>::infinity(), Right = -Left, Top = Left, Bottom = -Left;
        bool AllProjected = true;
        for (int I = 0; I < 8; ++I) {
            const SDK::FVector World{Origin.X + ((I&1) ? Extent.X : -Extent.X),
                Origin.Y + ((I&2) ? Extent.Y : -Extent.Y), Origin.Z + ((I&4) ? Extent.Z : -Extent.Z)};
            Projected[I] = g_ESP::ProjectToCanvas(Canvas, PC, World, &Corners[I]);
            AllProjected &= Projected[I];
            if (Projected[I]) {
                Left = std::min(Left,Corners[I].X); Right = std::max(Right,Corners[I].X);
                Top = std::min(Top,Corners[I].Y); Bottom = std::max(Bottom,Corners[I].Y);
            }
        }
        if (AllProjected && (Right < 0 || Bottom < 0 || Left > Canvas->ClipX || Top > Canvas->ClipY)) continue;
        if (Structure && g_Config::bOverlayStructureBox)
            for (const auto& Edge : BoxEdges) if (Projected[Edge[0]] && Projected[Edge[1]])
                Line(Corners[Edge[0]], Corners[Edge[1]], Color);
        if (Character && g_Config::bOverlayCharacterBox && AllProjected) {
            Line({Left,Top},{Right,Top},Color); Line({Right,Top},{Right,Bottom},Color);
            Line({Right,Bottom},{Left,Bottom},Color); Line({Left,Bottom},{Left,Top},Color);
        }
        // Cache names and parent indices only. Posed world positions are read
        // every frame so moving actors never reuse old screen-space bones.
        if (Character && Character->Mesh && g_Config::bOverlaySkeleton && RigBudget > 0 && LineBudget > 0) {
            auto* Mesh = Character->Mesh;
            auto* Asset = Mesh->SkeletalMesh;
            int32_t AssetSerial = -1;
            if (Asset && TryGetLiveObjectSerial(Asset, Asset->Index, &AssetSerial) &&
                !(Asset->Flags & SDK::EObjectFlags::BeginDestroyed) && !(Asset->Flags & SDK::EObjectFlags::FinishDestroyed)) {
                const int Count = Mesh->GetNumBones();
                if (Count > 1 && Count <= 1024 && Count <= BoneBudget) {
                    if (s_overlayRigs.size() > 128) s_overlayRigs.clear();
                    auto& Rig = s_overlayRigs[Asset];
                    const bool NeedsRig = Rig.AssetIndex != Asset->Index || Rig.AssetSerial != AssetSerial || Rig.Names.size() != size_t(Count);
                    if (!NeedsRig || NewRigBudget > 0) {
                        if (NeedsRig) {
                            // Spread first-use topology discovery across frames.
                            --NewRigBudget;
                            Rig = {}; Rig.AssetIndex = Asset->Index; Rig.AssetSerial = AssetSerial;
                            Rig.Names.resize(Count); Rig.Parents.resize(Count, -1);
                            for (int I = 0; I < Count; ++I) Rig.Names[I] = Mesh->GetBoneName(I);
                            for (int I = 0; I < Count; ++I) {
                                const auto Parent = Mesh->GetParentBone(Rig.Names[I]);
                                if (Parent.IsNone()) continue;
                                for (int J = 0; J < Count; ++J) if (J != I && Rig.Names[J] == Parent) {
                                    Rig.Parents[I] = J; break;
                                }
                            }
                        }
                        --RigBudget; BoneBudget -= Count;
                        std::vector<SDK::FVector> Positions(Count);
                        std::vector<SDK::FVector2D> Screen(Count);
                        std::vector<bool> Valid(Count, false);
                        for (int I = 0; I < Count; ++I) {
                            Positions[I] = Mesh->GetSocketLocation(Rig.Names[I]);
                            // Guard missing/invalid transforms and bones far outside the model.
                            const auto Offset = Positions[I] - Origin;
                            if (!IsFiniteResourceVector(Positions[I]) ||
                                std::abs(Offset.X) > Extent.X * 3 + 100 || std::abs(Offset.Y) > Extent.Y * 3 + 100 ||
                                std::abs(Offset.Z) > Extent.Z * 3 + 100) continue;
                            Valid[I] = g_ESP::ProjectToCanvas(Canvas, PC, Positions[I], &Screen[I]);
                        }
                        for (int I = 0; I < Count; ++I) {
                            int Parent = Rig.Parents[I];
                            if (Parent >= 0 && Valid[I] && Valid[Parent]) Line(Screen[I],Screen[Parent],Color);
                        }
                    }
                }
            }
        }
        if (!g_Config::bOverlayName && !g_Config::bOverlayDistance) continue;
        SDK::FVector2D Anchor{};
        if (!g_ESP::ProjectToCanvas(Canvas,PC,Origin + SDK::FVector{0,0,Extent.Z},&Anchor) ||
            Anchor.X < 0 || Anchor.X > Canvas->ClipX || Anchor.Y < 0 || Anchor.Y > Canvas->ClipY) continue;
        std::string Text;
        if (g_Config::bOverlayName) Text = Building ? Building->GetDescriptiveName().ToString() :
            Character->PlayerState ? Character->PlayerState->GetPlayerName().ToString() : Character->GetDescriptiveName().ToString();
        if (g_Config::bOverlayDistance) {
            if (!Text.empty()) Text += "  ";
            Text += std::to_string(int(Candidate.Distance)) + "m";
        }
        auto* Font = g_Util::GetSansationBold18();
        if (!Font || Text.empty()) continue;
        Shadow::PushFont(Font,1);
        const float NativeHeight = Shadow::MeasureTextSize("Ag国").y;
        Shadow::PopFont();
        const float FontScale = NativeHeight > 0 ? PhoenixInterface::FiniteClamp(
            g_Config::OverlayFontPixels,10.f,32.f,15.f) * ScaleY / NativeHeight : 1.f;
        Shadow::PushFont(Font,FontScale);
        const auto Size = Shadow::MeasureTextSize(Text);
        Shadow::PopFont();
        Shadow::Vec2 Pos{float(Anchor.X - Size.x * .5f), float(Anchor.Y - Size.y - 4 * ScaleY)};
        // Keep labels attached to their anchor rather than shifting to an edge.
        if (Pos.x < 0 || Pos.y < 0 || Pos.x + Size.x > Canvas->ClipX || Pos.y + Size.y > Canvas->ClipY) continue;
        Draw->AddRectFilled({Pos.x - 3 * ScaleX,Pos.y - 2 * ScaleY},
            {Size.x + 6 * ScaleX,Size.y + 4 * ScaleY},{.025f,.035f,.05f,Color.a * .8f});
        Draw->AddText(Font,FontScale,{0,0,0,Color.a},{0,0,0,Color.a},Pos,Color,Text);
    }
}
