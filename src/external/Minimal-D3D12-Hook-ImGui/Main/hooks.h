#include "mdx12_api.h"

#pragma warning(push)
#pragma warning(disable: 26451)
#pragma warning(disable: 26812)
#pragma warning(disable: 6387)
#pragma warning(pop)

namespace g_Hook {
    class CallbackScope {
    public:
        CallbackScope();
        ~CallbackScope();

        CallbackScope(const CallbackScope&) = delete;
        CallbackScope& operator=(const CallbackScope&) = delete;
    };

    bool SupportsWeaponAim(SDK::AShooterWeapon* Weapon);
    void StartAllHooks();
    bool IsUnloadRequested();
    bool StopAllHooks();
    void SetModuleHandle(HMODULE module);
    void SetInitializationThread(HANDLE thread);
    void SetRuntimeReady(bool ready);
    bool IsStopping();
    void LogLifecycle(const char* message);
    void CloseLifecycleLog();
    void RequestUnload();

    inline bool ActorTickOK = false;
	inline bool HandleDisconnectOK = false;
	inline bool OutputTextLineOK = false;
	inline bool PostRenderOK = false;
	inline bool UpdateCameraOK = false;
	inline bool WeaponAimOK = false;
    inline bool ProjectileShotEventsOK = false;
    inline bool DinoActionEventsOK = false;
    inline void* DinoActionEventTarget = nullptr;
    inline bool ProjectileModifiersOK = false;
	inline bool TekBowInstantAimOK = false;
	inline bool ShotgunSpreadOK = false;
	inline bool InstantThrowOK = false;
	// 弓满蓄力直接出箭（StartFire 包装拦截，见 hooks.cpp::initBowFullChargeShot）
	inline bool BowFullChargeOK = false;
	inline bool PhysicsRotationOK = false;
	inline bool TakeDamageOK = false;
	inline bool ClientAddFloatingDamageTextOK = false;

	inline bool ClientNotifyReconnectedOK = false;
	inline bool ClientNotifyRespawnedOK = false;

	inline bool ClientChatMessageOK = false;
}
