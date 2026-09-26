// WARP-only GPU regression: never attaches to the game.
#include "../src/internal/Menu/VibrancyBackdrop.h"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <cassert>
#include <iostream>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
int main(){
    ComPtr<ID3D12Debug> Debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&Debug))))Debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> Factory;assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&Factory))));
    ComPtr<IDXGIAdapter> Adapter;assert(SUCCEEDED(Factory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter))));
    ComPtr<ID3D12Device> Device;assert(SUCCEEDED(D3D12CreateDevice(Adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&Device))));
    ComPtr<ID3D12InfoQueue> Info;Device.As(&Info);
    D3D12_COMMAND_QUEUE_DESC QD{};ComPtr<ID3D12CommandQueue> Queue;assert(SUCCEEDED(Device->CreateCommandQueue(&QD,IID_PPV_ARGS(&Queue))));
    ComPtr<ID3D12CommandAllocator> Alloc;assert(SUCCEEDED(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&Alloc))));
    ComPtr<ID3D12GraphicsCommandList> List;assert(SUCCEEDED(Device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,Alloc.Get(),nullptr,IID_PPV_ARGS(&List))));List->Close();
    D3D12_DESCRIPTOR_HEAP_DESC HD{};HD.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;HD.NumDescriptors=PhoenixVibrancy::SrvBase+3;HD.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> Heap;assert(SUCCEEDED(Device->CreateDescriptorHeap(&HD,IID_PPV_ARGS(&Heap))));
    HD.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;HD.NumDescriptors=1;HD.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ComPtr<ID3D12DescriptorHeap> RtvHeap;assert(SUCCEEDED(Device->CreateDescriptorHeap(&HD,IID_PPV_ARGS(&RtvHeap))));
    constexpr unsigned W=160,H=96,Pitch=768;
    D3D12_RESOURCE_DESC RD{};RD.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;RD.Width=W;RD.Height=H;RD.DepthOrArraySize=1;RD.MipLevels=1;RD.SampleDesc.Count=1;RD.Format=DXGI_FORMAT_R8G8B8A8_UNORM;RD.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES HP{};HP.Type=D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> Scene;assert(SUCCEEDED(Device->CreateCommittedResource(&HP,D3D12_HEAP_FLAG_NONE,&RD,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&Scene))));
    auto RTV=RtvHeap->GetCPUDescriptorHandleForHeapStart();Device->CreateRenderTargetView(Scene.Get(),nullptr,RTV);
    RD={};RD.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;RD.Width=Pitch*H;RD.Height=1;RD.DepthOrArraySize=1;RD.MipLevels=1;RD.SampleDesc.Count=1;RD.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;HP.Type=D3D12_HEAP_TYPE_READBACK;
    ComPtr<ID3D12Resource> Readback;assert(SUCCEEDED(Device->CreateCommittedResource(&HP,D3D12_HEAP_FLAG_NONE,&RD,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&Readback))));
    ComPtr<ID3D12Fence> Fence;assert(SUCCEEDED(Device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&Fence))));HANDLE Event=CreateEvent(nullptr,FALSE,FALSE,nullptr);
    ImGui::CreateContext();auto& IO=ImGui::GetIO();IO.DisplaySize={W,H};IO.DeltaTime=1.f/60;IO.IniFilename=nullptr;
    IO.Fonts->AddFontDefault();IO.Fonts->Build();
    assert(ImGui_ImplDX12_Init(Device.Get(),3,DXGI_FORMAT_R8G8B8A8_UNORM,Heap.Get(),Heap->GetCPUDescriptorHandleForHeapStart(),Heap->GetGPUDescriptorHandleForHeapStart()));
    for(unsigned N=0;N<6;++N){
        Alloc->Reset();List->Reset(Alloc.Get(),nullptr);
        float Red[4]={1,0,0,1},Blue[4]={0,0,1,1};D3D12_RECT Right{80,0,W,H};
        List->ClearRenderTargetView(RTV,Red,0,nullptr);List->ClearRenderTargetView(RTV,Blue,1,&Right);
        ImGui_ImplDX12_NewFrame();
        PhoenixVibrancy::Capture(Device.Get(),List.Get(),Scene.Get(),Heap.Get(),N%3,3,true);
        assert(PhoenixVibrancy::Texture!=0);
        ImGui::NewFrame();auto* Draw=ImGui::GetBackgroundDrawList();
        Draw->AddImage(PhoenixVibrancy::Texture,{0,0},{W,H});Draw->AddRectFilled({0,0},{W,H},IM_COL32(28,28,30,204));ImGui::Render();
        List->OMSetRenderTargets(1,&RTV,FALSE,nullptr);ID3D12DescriptorHeap* Heaps[]={Heap.Get()};List->SetDescriptorHeaps(1,Heaps);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(),List.Get());
        D3D12_RESOURCE_BARRIER B{};B.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;B.Transition.pResource=Scene.Get();B.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;B.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;B.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;List->ResourceBarrier(1,&B);
        D3D12_TEXTURE_COPY_LOCATION Dest{},Source{};Dest.pResource=Readback.Get();Dest.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;Dest.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,W,H,1,Pitch};Source.pResource=Scene.Get();Source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        List->CopyTextureRegion(&Dest,0,0,0,&Source,nullptr);std::swap(B.Transition.StateBefore,B.Transition.StateAfter);List->ResourceBarrier(1,&B);
        List->Close();ID3D12CommandList* Lists[]={List.Get()};Queue->ExecuteCommandLists(1,Lists);Queue->Signal(Fence.Get(),N+1);Fence->SetEventOnCompletion(N+1,Event);assert(WaitForSingleObject(Event,10000)==WAIT_OBJECT_0);
        unsigned char* Bytes;D3D12_RANGE Range{0,Pitch*H};assert(SUCCEEDED(Readback->Map(0,&Range,reinterpret_cast<void**>(&Bytes))));
        auto* Center=Bytes+48*Pitch+80*4;
        assert(Center[0]>30&&Center[2]>30&&Center[1]<30); // Mixed red + blue across the blur boundary, under the dark tint.
        D3D12_RANGE Written{0,0};Readback->Unmap(0,&Written);
    }
    PhoenixVibrancy::Capture(Device.Get(),List.Get(),Scene.Get(),Heap.Get(),0,3,false);assert(PhoenixVibrancy::Texture==0);
    PhoenixVibrancy::Release();ImGui_ImplDX12_Shutdown();ImGui::DestroyContext();
    if(Info){for(UINT64 I=0;I<Info->GetNumStoredMessages();++I){SIZE_T Length=0;Info->GetMessage(I,nullptr,&Length);std::vector<char> Buffer(Length);auto* M=reinterpret_cast<D3D12_MESSAGE*>(Buffer.data());Info->GetMessage(I,M,&Length);if(M->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::cerr<<M->pDescription<<"\n";return 1;}}}
    CloseHandle(Event);std::cout<<"DX12 WARP shader, scene-copy barriers, six frame reuse passes, tint pixels and cleanup passed.\n";
}
