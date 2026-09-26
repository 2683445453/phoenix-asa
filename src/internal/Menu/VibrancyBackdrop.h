#pragma once
#include "../../external/Minimal-D3D12-Hook-ImGui/ImGui/imgui_impl_dx12.h"
#include <vector>

// A per-swapchain-buffer scene copy. Only captured while the menu is open.
// The caller has already waited for this frame's fence before reusing it.
namespace PhoenixVibrancy {
    inline constexpr unsigned SrvBase = 128;
    struct Frame { ID3D12Resource* Scene=nullptr; bool Readable=false; };
    inline std::vector<Frame> Frames;
    inline ImTextureID Texture=0;
    inline void Release() {
        for(auto& F:Frames) if(F.Scene)F.Scene->Release();
        Frames.clear();Texture=0;
    }
    inline void Capture(ID3D12Device* Device,ID3D12GraphicsCommandList* List,
        ID3D12Resource* BackBuffer,ID3D12DescriptorHeap* Heap,unsigned Index,unsigned Count,bool Enabled) {
        Texture=0; ImGui_ImplDX12_SetVibrancyTexture(0);
        if(!Enabled || !Device || !List || !BackBuffer || !Heap || Index>=Count)return;
        auto Desc=BackBuffer->GetDesc();
        if(Desc.SampleDesc.Count!=1 || (Desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM &&
            Desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM && Desc.Format!=DXGI_FORMAT_R10G10B10A2_UNORM))return;
        if(Frames.empty())Frames.resize(Count);
        if(Frames.size()!=Count)return; // Recreated after resize cleanup.
        auto& F=Frames[Index];
        if(!F.Scene) {
            Desc.Flags=D3D12_RESOURCE_FLAG_NONE;
            D3D12_HEAP_PROPERTIES Props{};Props.Type=D3D12_HEAP_TYPE_DEFAULT;
            if(FAILED(Device->CreateCommittedResource(&Props,D3D12_HEAP_FLAG_NONE,&Desc,
                D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&F.Scene))))return;
            auto CPU=Heap->GetCPUDescriptorHandleForHeapStart();
            CPU.ptr+=(Index+SrvBase)*Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            D3D12_SHADER_RESOURCE_VIEW_DESC SRV{};SRV.Format=Desc.Format;SRV.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            SRV.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;SRV.Texture2D.MipLevels=1;
            Device->CreateShaderResourceView(F.Scene,&SRV,CPU);
        }
        D3D12_RESOURCE_BARRIER B[2]{};
        for(auto& X:B){X.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;X.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
        B[0].Transition.pResource=BackBuffer;B[0].Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;B[0].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
        B[1].Transition.pResource=F.Scene;B[1].Transition.StateBefore=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;B[1].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
        List->ResourceBarrier(F.Readable?2:1,B);
        List->CopyResource(F.Scene,BackBuffer);
        std::swap(B[0].Transition.StateBefore,B[0].Transition.StateAfter);
        B[1].Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;B[1].Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        List->ResourceBarrier(2,B);F.Readable=true;
        auto GPU=Heap->GetGPUDescriptorHandleForHeapStart();
        GPU.ptr+=(Index+SrvBase)*Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if(ImGui_ImplDX12_SetVibrancyTexture(static_cast<ImTextureID>(GPU.ptr))) Texture=static_cast<ImTextureID>(GPU.ptr);
    }
    inline void DrawSidebar() {
        if(!Texture)return;
        auto P=ImGui::GetWindowPos(),S=ImGui::GetWindowSize(),D=ImGui::GetIO().DisplaySize;
        if(D.x<=0||D.y<=0)return;
        auto* L=ImGui::GetWindowDrawList();
        L->AddImage(Texture,P,{P.x+S.x,P.y+S.y},{P.x/D.x,P.y/D.y},{(P.x+S.x)/D.x,(P.y+S.y)/D.y});
        L->AddRectFilled(P,{P.x+S.x,P.y+S.y},IM_COL32(28,28,30,204),8);
    }
}
