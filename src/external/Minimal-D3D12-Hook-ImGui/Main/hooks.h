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

    void StartAllHooks();
    void StopAllHooks();
    void SetModuleHandle(HMODULE module);
    void RequestUnload();

    inline bool ActorTickOK = false;
	inline bool HandleDisconnectOK = false;
	inline bool OutputTextLineOK = false;
	inline bool PostRenderOK = false;
	inline bool PhysicsRotationOK = false;
	inline bool TakeDamageOK = false;
	inline bool ClientAddFloatingDamageTextOK = false;

	inline bool ClientNotifyReconnectedOK = false;
	inline bool ClientNotifyRespawnedOK = false;

	inline bool ClientChatMessageOK = false;
}
