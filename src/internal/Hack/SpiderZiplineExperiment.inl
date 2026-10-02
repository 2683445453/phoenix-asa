// One-shot SpiderS zipline flip experiment. Included inside g_Hack.
namespace {
    std::atomic<int> SpiderFlipRequest{0}; // 1: component path, 2: server event
    std::mutex SpiderFlipStatusMutex;
    std::string SpiderFlipStatus = "待命：骑乘小蜘蛛并附着蛛丝后点击一次按钮";
    std::string SpiderFlipPath;

    struct SpiderFlipCapture {
        ExtensionObject Dino;
        ExtensionObject Component;
        ExtensionObject Buff;
        std::ofstream File;
        uint64_t Started = 0;
        int NextSample = 0;
    } SpiderFlip;

    void SetSpiderFlipStatus(std::string Value) {
        std::scoped_lock Lock(SpiderFlipStatusMutex);
        SpiderFlipStatus = std::move(Value);
    }

    SDK::UFunction* SpiderFlipFunction(SDK::UObject* Object, const char* Owner,
                                       const char* Name, int Size) {
        if (!IsUsableObject(Object) || !Object->Class) return nullptr;
        auto* Function = Object->Class->GetFunction(Owner, Name);
        return IsUsableObject(Function) && Function->Size == Size ? Function : nullptr;
    }

    bool SpiderFlipOpenLog(uint64_t Now) {
        HMODULE Module = nullptr;
        wchar_t Path[32768]{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 reinterpret_cast<LPCWSTR>(&SpiderFlipRequest), &Module)) return false;
        const DWORD Length = GetModuleFileNameW(Module, Path, 32768);
        if (!Length || Length >= 32768) return false;
        auto Directory = std::filesystem::path(Path).parent_path() / L"logs";
        std::error_code Error;
        std::filesystem::create_directories(Directory, Error);
        if (Error) return false;
        SYSTEMTIME Time{};
        GetLocalTime(&Time);
        const auto Name = std::format("spider-zipline-{:04}{:02}{:02}-{:02}{:02}{:02}-{}-{}.csv",
            Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond,
            GetCurrentProcessId(), Now);
        auto Destination = Directory / Name;
        SpiderFlip.File.clear();
        SpiderFlip.File.open(Destination, std::ios::out | std::ios::trunc);
        if (!SpiderFlip.File) return false;
        SpiderFlip.File.imbue(std::locale::classic());
        SpiderFlip.File << std::setprecision(12)
            << "# client_observation_only; coordinates_cm; server_correction_inferred_from_later_samples\n"
            << "elapsed_ms,event,role,using_zipline,hanging,attach_offset,actor_x,actor_y,actor_z,"
               "capsule_x,capsule_y,capsule_z,mesh_x,mesh_y,mesh_z\n";
        const auto Utf8 = Destination.u8string();
        {
            std::scoped_lock Lock(SpiderFlipStatusMutex);
            SpiderFlipPath.assign(reinterpret_cast<const char*>(Utf8.data()), Utf8.size());
        }
        return SpiderFlip.File.good();
    }

    void SpiderFlipSample(const char* Event, uint64_t Now) {
        auto* Dino = static_cast<SDK::APrimalDinoCharacter*>(SpiderFlip.Dino.Get());
        auto* Component = SpiderFlip.Component.Get();
        auto* Buff = SpiderFlip.Buff.Get();
        if (!Dino || !Component) return;
        const auto Actor = Dino->K2_GetActorLocation();
        const auto Capsule = IsUsableObject(Dino->CapsuleComponent)
            ? Dino->CapsuleComponent->K2_GetComponentLocation() : SDK::FVector{};
        const auto Mesh = IsUsableObject(Dino->Mesh)
            ? Dino->Mesh->K2_GetComponentLocation() : SDK::FVector{};
        bool Using = false, Hanging = false;
        ReadReflectedBoolValue(Component, "UsingZipline", &Using);
        if (Buff) ReadReflectedBoolValue(Buff, "ZiplineHanging", &Hanging);
        const double Offset = Buff ? GlideProbeScalar(Buff, "AttachOffset") :
            std::numeric_limits<double>::quiet_NaN();
        SpiderFlip.File << Now - SpiderFlip.Started << ',' << Event << ','
            << static_cast<int>(Dino->Role) << ',' << Using << ',' << Hanging << ',' << Offset << ','
            << Actor.X << ',' << Actor.Y << ',' << Actor.Z << ','
            << Capsule.X << ',' << Capsule.Y << ',' << Capsule.Z << ','
            << Mesh.X << ',' << Mesh.Y << ',' << Mesh.Z << '\n';
        SpiderFlip.File.flush();
    }

    void SpiderFlipTraceGeometry(SDK::APrimalDinoCharacter* Dino, SDK::UObject* Buff) {
        if (!IsUsableObject(Dino->RootComponent) || !IsUsableObject(Buff)) return;
        const double Above = GlideProbeScalar(Buff, "AttachOffsetAbove");
        const double Hanging = GlideProbeScalar(Buff, "AttachOffsetHanging");
        const double Radius = GlideProbeScalar(Buff, "ZiplineFlipTraceRadius");
        bool IsHanging = false;
        if (!ReadReflectedBoolValue(Buff, "ZiplineHanging", &IsHanging) ||
            !std::isfinite(Above) || !std::isfinite(Hanging) ||
            !std::isfinite(Radius) || Radius < 0 || Radius > 10000) return;
        const double NativeLength = std::max(IsHanging ? Above : Hanging, 1.0);
        const double FullLength = std::abs(Above - Hanging);
        const auto Start = Dino->RootComponent->K2_GetComponentLocation();
        const auto Up = Dino->RootComponent->GetUpVector();
        SDK::EObjectTypeQuery TypeStorage[4] = {
            static_cast<SDK::EObjectTypeQuery>(0), static_cast<SDK::EObjectTypeQuery>(8),
            static_cast<SDK::EObjectTypeQuery>(5), static_cast<SDK::EObjectTypeQuery>(6)
        };
        SDK::TArray<SDK::EObjectTypeQuery> Types(TypeStorage, 4, 4);
        SDK::AActor* IgnoreStorage[2] = {Dino, nullptr};
        if (auto* Rider = Dino->Rider.Get(); IsUsableObject(Rider)) IgnoreStorage[1] = Rider;
        SDK::TArray<SDK::AActor*> Ignore(IgnoreStorage, IgnoreStorage[1] ? 2 : 1, 2);
        auto Trace = [&](const char* Label, double Length) {
            const SDK::FVector End{Start.X - Up.X * Length,
                                   Start.Y - Up.Y * Length,
                                   Start.Z - Up.Z * Length};
            SDK::FHitResult Hit{};
            const bool Any = SDK::UKismetSystemLibrary::SphereTraceSingleForObjects(
                Buff, Start, End, static_cast<float>(Radius), Types, false, Ignore,
                SDK::EDrawDebugTrace::None, &Hit, true,
                SDK::FLinearColor{1.0f, 0.0f, 0.0f, 1.0f},
                SDK::FLinearColor{0.0f, 1.0f, 0.0f, 1.0f}, -1.0f);
            SpiderFlip.File << "# trace_" << Label << ",length=" << Length
                << ",radius=" << Radius << ",hit=" << Any
                << ",blocking=" << Hit.bBlockingHit
                << ",initial_overlap=" << Hit.bStartPenetrating
                << ",distance=" << Hit.Distance
                << ",point=" << Hit.ImpactPoint.X << ',' << Hit.ImpactPoint.Y << ',' << Hit.ImpactPoint.Z
                << ",start=" << Start.X << ',' << Start.Y << ',' << Start.Z
                << ",end=" << End.X << ',' << End.Y << ',' << End.Z << '\n';
        };
        SpiderFlip.File << "# diagnostic_single_hit_traces; blueprint_uses_multi_hit_and_filters_volume_and_ninja\n";
        Trace("native_length", NativeLength);
        Trace("full_displacement", FullLength);
        SpiderFlip.File.flush();
    }

    void SpiderFlipFinish(const char* Reason) {
        if (SpiderFlip.File.is_open()) {
            SpiderFlip.File << "# end=" << Reason << '\n';
            SpiderFlip.File.close();
        }
        SpiderFlip.Dino = {};
        SpiderFlip.Component = {};
        SpiderFlip.Buff = {};
        SpiderFlip.Started = 0;
        SpiderFlip.NextSample = 0;
    }
}

void RequestSpiderZiplineFlip(bool DirectServerEvent) {
    if (SpiderFlipRequest.exchange(DirectServerEvent ? 2 : 1) == 0)
        SetSpiderFlipStatus(DirectServerEvent ? "已排队：直接请求服务器倒挂" :
                            "已排队：调用蛛丝组件 CrouchPressed");
}

std::string GetSpiderZiplineExperimentStatus() {
    std::scoped_lock Lock(SpiderFlipStatusMutex);
    return SpiderFlipPath.empty() ? SpiderFlipStatus :
        SpiderFlipStatus + "\n日志：" + SpiderFlipPath;
}

void ResetSpiderZiplineExperiment() {
    SpiderFlipRequest.store(0);
    SpiderFlipFinish("unload");
}

void SpiderZiplineExperimentTick(SDK::UWorld* World) {
    constexpr uint64_t SampleAt[] = {250, 1000, 3000};
    const uint64_t Now = GetTickCount64();
    if (SpiderFlip.Started && SpiderFlip.NextSample < 3 &&
        Now - SpiderFlip.Started >= SampleAt[SpiderFlip.NextSample]) {
        if (!SpiderFlip.Dino.Get() || !SpiderFlip.Component.Get()) {
            SpiderFlipFinish("actor_lost");
            SetSpiderFlipStatus("采样中断：蜘蛛或蛛丝组件已销毁");
        } else {
            SpiderFlipSample("followup", Now);
            if (++SpiderFlip.NextSample == 3) {
                SpiderFlipFinish("complete");
                SetSpiderFlipStatus("采样完成；可打开日志比较倒挂状态和后续坐标");
            }
        }
    }
    const int Request = SpiderFlipRequest.exchange(0);
    if (!Request) return;
    if (SpiderFlip.Started) {
        SetSpiderFlipStatus("上一轮仍在采样，请等待约 3 秒后重试");
        return;
    }
    auto* PC = GetLocalShooterController(World);
    auto* Pawn = IsUsableObject(PC) ? PC->Pawn : nullptr;
    auto* Dino = IsUsableObject(Pawn) && Pawn->IsA(SDK::APrimalDinoCharacter::StaticClass())
        ? static_cast<SDK::APrimalDinoCharacter*>(Pawn) : nullptr;
    if (!Dino || Dino->IsDead() ||
        !(Dino->Role == SDK::ENetRole::ROLE_AutonomousProxy ||
          Dino->Role == SDK::ENetRole::ROLE_Authority) ||
        !ExpansionHasClass(Dino, "spiders_character_bp")) {
        SetSpiderFlipStatus("未执行：当前没有控制可用的小蜘蛛 SpiderS");
        return;
    }
    auto* Component = ReadReflectedObjectValue(Dino, "ZiplineComponent_SpiderS");
    if (!ExpansionHasClass(Component, "ziplinecomponent_spiders")) {
        SetSpiderFlipStatus("未执行：蜘蛛蛛丝组件不存在或类型不符");
        return;
    }
    bool Using = false;
    if (!ReadReflectedBoolValue(Component, "UsingZipline", &Using) || !Using) {
        SetSpiderFlipStatus("未执行：蜘蛛当前未附着蛛丝");
        return;
    }
    auto* Buff = ReadReflectedObjectValue(Component, "ZiplineBuffRef");
    if (!ExpansionHasClass(Buff, "buff_zipline_dino_spiders")) {
        SetSpiderFlipStatus("未执行：当前蛛丝倒挂 Buff 未就绪");
        return;
    }
    auto* ComponentEvent = SpiderFlipFunction(Component, "ZiplineComponent_SpiderS_C", "CrouchPressed", 1);
    auto* ServerEvent = SpiderFlipFunction(Buff, "Buff_Zipline_DinoBase_C", "FlipOnZipline_RunOnServer", 0);
    auto* CanFlip = SpiderFlipFunction(Buff, "Buff_Zipline_DinoBase_C", "CanZiplineFlip", 744);
    if (!ComponentEvent || !ServerEvent || !CanFlip ||
        !(static_cast<uint32_t>(ServerEvent->FunctionFlags) & 0x40) ||
        !(static_cast<uint32_t>(ServerEvent->FunctionFlags) & 0x200000)) {
        SetSpiderFlipStatus("未执行：蓝图函数签名或服务器 RPC 标志不匹配");
        return;
    }
    if (!SpiderFlipOpenLog(Now)) {
        SetSpiderFlipStatus("未执行：无法创建蜘蛛实验日志");
        return;
    }
    SpiderFlip.Dino = ExtensionObject(Dino);
    SpiderFlip.Component = ExtensionObject(Component);
    SpiderFlip.Buff = ExtensionObject(Buff);
    SpiderFlip.Started = Now;
    SpiderFlipSample("before", Now);
    SpiderFlipTraceGeometry(Dino, Buff);
    alignas(16) uint8_t CanFlipArgs[744]{};
    Buff->ProcessEvent(CanFlip, CanFlipArgs);
    const bool Allowed = CanFlipArgs[0] != 0;
    SpiderFlip.File << "# can_zipline_flip_client=" << Allowed
                    << "; route=" << (Request == 2 ? "direct_server_rpc" : "component_crouch_pressed") << '\n';
    if (Request == 2) Buff->ProcessEvent(ServerEvent, nullptr);
    else {
        uint8_t Args[1]{};
        Component->ProcessEvent(ComponentEvent, Args);
    }
    SpiderFlipSample("after_call", GetTickCount64());
    SetSpiderFlipStatus(std::format("已调用{}；客户端 CanZiplineFlip={}。继续采样 3 秒",
        Request == 2 ? "服务器倒挂事件" : "蛛丝组件入口", Allowed));
}
