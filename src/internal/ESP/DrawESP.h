#pragma once
#include <string>
#include <vector>

namespace g_DrawESP {
	struct ResourceESPDiagnostics {
		int Levels = 0;
		int Actors = 0;
		int InstanceComponents = 0;
		int OverlappingInstances = 0;
		int Markers = 0;
		int HighlightedComponents = 0;
		int StencilValue = 0;
		bool CameraStencilEnabled = false;
		int CameraPPBlendables = 0;
		int CameraPPBlendableCapacity = 0;
		int CameraPPAdjusters = 0;
		int CameraBlendCache = 0;
		int CustomDepthMode = -1;
		int NaniteCustomDepthMode = -1;
		bool PostProcessingEnabled = false;
		int PreRenderWrites = 0;
		std::string CameraPPMaterials;
		std::string OutlineMaterialStatus;
	};

	void DrawESP(SDK::UCanvas* Canvas);
	void DrawResourceESP(SDK::UCanvas* Canvas);
	void MaintainResourceModelHighlightState();
	void ResetResourceModelHighlights();
	void ResetTekHelmetNightVision();
	std::string GetTekHelmetNightVisionStatus();
	ResourceESPDiagnostics GetResourceESPDiagnostics();
	bool FindNearestResourceTarget(const SDK::FVector& Origin, float MaxDistanceMeters,
		SDK::FVector* OutLocation, float* OutDistanceMeters, std::string* OutTypeName,
		const std::vector<SDK::FVector>* ExcludedLocations = nullptr,
		float ExclusionRadiusMeters = 0.0f, bool ElementShardOnly = false);
	bool FindResourceTargetNear(const SDK::FVector& ExpectedLocation, float ToleranceMeters,
		SDK::FVector* OutLocation, std::string* OutTypeName,
		bool ElementShardOnly = false);
}
