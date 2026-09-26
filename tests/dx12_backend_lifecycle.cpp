// Exercises the actual backend on a separate WARP device. Never opens the game.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <cassert>
#include <atomic>
#include <iostream>
#include <vector>
static bool FailWait = false;
static DWORD BackendTestWait(HANDLE Event,DWORD Timeout) {
    return FailWait ? WAIT_FAILED : ::WaitForSingleObject(Event,Timeout);
}
#define WaitForSingleObject BackendTestWait
#include "../src/external/Minimal-D3D12-Hook-ImGui/ImGui/imgui_impl_dx12.cpp"
#undef WaitForSingleObject
using Microsoft::WRL::ComPtr;

// Only Signal is faulted; all submitted commands execute on the real WARP queue.
struct QueueProxy final : ID3D12CommandQueue {
    ID3D12CommandQueue* Inner=nullptr; bool FailSignal=false; ULONG References=1; unsigned Submits=0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID I,void** O)override{return Inner->QueryInterface(I,O);}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++References;}
    ULONG STDMETHODCALLTYPE Release()override{return --References;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID G,UINT* S,void* D)override{return Inner->GetPrivateData(G,S,D);}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID G,UINT S,const void* D)override{return Inner->SetPrivateData(G,S,D);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID G,const IUnknown* D)override{return Inner->SetPrivateDataInterface(G,D);}
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR N)override{return Inner->SetName(N);}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID I,void** O)override{return Inner->GetDevice(I,O);}
    void STDMETHODCALLTYPE UpdateTileMappings(ID3D12Resource* R,UINT N,const D3D12_TILED_RESOURCE_COORDINATE* C,const D3D12_TILE_REGION_SIZE* S,ID3D12Heap* H,UINT M,const D3D12_TILE_RANGE_FLAGS* F,const UINT* O,const UINT* T,D3D12_TILE_MAPPING_FLAGS G)override{Inner->UpdateTileMappings(R,N,C,S,H,M,F,O,T,G);}
    void STDMETHODCALLTYPE CopyTileMappings(ID3D12Resource* D,const D3D12_TILED_RESOURCE_COORDINATE* C,ID3D12Resource* S,const D3D12_TILED_RESOURCE_COORDINATE* A,const D3D12_TILE_REGION_SIZE* Z,D3D12_TILE_MAPPING_FLAGS F)override{Inner->CopyTileMappings(D,C,S,A,Z,F);}
    void STDMETHODCALLTYPE ExecuteCommandLists(UINT N,ID3D12CommandList* const* L)override{++Submits;Inner->ExecuteCommandLists(N,L);}
    void STDMETHODCALLTYPE SetMarker(UINT M,const void* D,UINT S)override{Inner->SetMarker(M,D,S);}
    void STDMETHODCALLTYPE BeginEvent(UINT M,const void* D,UINT S)override{Inner->BeginEvent(M,D,S);}
    void STDMETHODCALLTYPE EndEvent()override{Inner->EndEvent();}
    HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence* F,UINT64 V)override{return FailSignal?E_FAIL:Inner->Signal(F,V);}
    HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence* F,UINT64 V)override{return Inner->Wait(F,V);}
    HRESULT STDMETHODCALLTYPE GetTimestampFrequency(UINT64* V)override{return Inner->GetTimestampFrequency(V);}
    HRESULT STDMETHODCALLTYPE GetClockCalibration(UINT64* A,UINT64* B)override{return Inner->GetClockCalibration(A,B);}
    D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE GetDesc()override{return Inner->GetDesc();}
};
struct DescriptorPool {
    ID3D12Device* Device=nullptr;ID3D12DescriptorHeap* Heap=nullptr;bool Used[32]{};
    static void Allocate(ImGui_ImplDX12_InitInfo* I,D3D12_CPU_DESCRIPTOR_HANDLE* C,D3D12_GPU_DESCRIPTOR_HANDLE* G){
        auto& P=*static_cast<DescriptorPool*>(I->UserData);*C={};*G={};
        for(unsigned N=0;N<32;++N)if(!P.Used[N]){P.Used[N]=true;*C=P.Heap->GetCPUDescriptorHandleForHeapStart();*G=P.Heap->GetGPUDescriptorHandleForHeapStart();
            const UINT Step=P.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);C->ptr+=N*Step;G->ptr+=N*Step;return;}
    }
    static void Free(ImGui_ImplDX12_InitInfo* I,D3D12_CPU_DESCRIPTOR_HANDLE C,D3D12_GPU_DESCRIPTOR_HANDLE){
        auto& P=*static_cast<DescriptorPool*>(I->UserData);const auto N=(C.ptr-P.Heap->GetCPUDescriptorHandleForHeapStart().ptr)/P.Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        assert(N<32&&P.Used[N]);P.Used[N]=false;
    }
};
// Fault only this standalone test process's WARP COM entry points. Restoring
// scopes finish before any resource/device is released. No game process access.
struct VTableSlotPatch {
    void** Slot;void* Original;
    VTableSlotPatch(void* Object,size_t Index,void* Replacement):Slot(&(*static_cast<void***>(Object))[Index]),Original(*Slot){
        DWORD Old=0;assert(VirtualProtect(Slot,sizeof(void*),PAGE_READWRITE,&Old));
        InterlockedExchangePointer(Slot,Replacement);DWORD Unused=0;assert(VirtualProtect(Slot,sizeof(void*),Old,&Unused));
    }
    ~VTableSlotPatch(){DWORD Old=0;assert(VirtualProtect(Slot,sizeof(void*),PAGE_READWRITE,&Old));InterlockedExchangePointer(Slot,Original);DWORD Unused=0;assert(VirtualProtect(Slot,sizeof(void*),Old,&Unused));}
};
using CreateResourceFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*,const D3D12_HEAP_PROPERTIES*,D3D12_HEAP_FLAGS,const D3D12_RESOURCE_DESC*,D3D12_RESOURCE_STATES,const D3D12_CLEAR_VALUE*,REFIID,void**);
static CreateResourceFn OriginalCreate=nullptr;static bool FailCreate=false;
static HRESULT STDMETHODCALLTYPE FaultCreate(ID3D12Device* D,const D3D12_HEAP_PROPERTIES* P,D3D12_HEAP_FLAGS F,const D3D12_RESOURCE_DESC* R,D3D12_RESOURCE_STATES S,const D3D12_CLEAR_VALUE* C,REFIID I,void** O){
    if(FailCreate&&P->Type==D3D12_HEAP_TYPE_UPLOAD&&R->Dimension==D3D12_RESOURCE_DIMENSION_BUFFER){*O=nullptr;return E_OUTOFMEMORY;}
    return OriginalCreate(D,P,F,R,S,C,I,O);
}
using MapFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Resource*,UINT,const D3D12_RANGE*,void**);
using UnmapFn=void(STDMETHODCALLTYPE*)(ID3D12Resource*,UINT,const D3D12_RANGE*);
static MapFn OriginalMap=nullptr;static UnmapFn OriginalUnmap=nullptr;
static ID3D12Resource* FailMapTarget=nullptr;static ID3D12Resource* WatchUnmapTarget=nullptr;static unsigned WatchedUnmaps=0;
static HRESULT STDMETHODCALLTYPE FaultMap(ID3D12Resource* R,UINT S,const D3D12_RANGE* Range,void** Out){if(R==FailMapTarget){*Out=nullptr;return E_OUTOFMEMORY;}return OriginalMap(R,S,Range,Out);}
static void STDMETHODCALLTYPE WatchUnmap(ID3D12Resource* R,UINT S,const D3D12_RANGE* Range){if(R==WatchUnmapTarget){assert(Range&&Range->Begin==0&&Range->End==0);++WatchedUnmaps;}OriginalUnmap(R,S,Range);}
int main(){
    ComPtr<ID3D12Debug> Debug;const bool HasDebug=SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&Debug)));if(HasDebug)Debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> Factory;assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&Factory))));
    ComPtr<IDXGIAdapter> Adapter;assert(SUCCEEDED(Factory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter))));
    ComPtr<ID3D12Device> Device;assert(SUCCEEDED(D3D12CreateDevice(Adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&Device))));
    ComPtr<ID3D12InfoQueue> Info;Device.As(&Info);
    D3D12_COMMAND_QUEUE_DESC QD{};ComPtr<ID3D12CommandQueue> Queue;assert(SUCCEEDED(Device->CreateCommandQueue(&QD,IID_PPV_ARGS(&Queue))));
    QueueProxy Proxy;Proxy.Inner=Queue.Get();
    D3D12_DESCRIPTOR_HEAP_DESC HD{};HD.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;HD.NumDescriptors=32;HD.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> Heap;assert(SUCCEEDED(Device->CreateDescriptorHeap(&HD,IID_PPV_ARGS(&Heap))));
    DescriptorPool Pool{Device.Get(),Heap.Get()};
    ImGui::CreateContext();auto& IO=ImGui::GetIO();IO.IniFilename=nullptr;IO.DisplaySize={128,96};IO.DeltaTime=1.f/60;
    IO.Fonts->AddFontDefault(); // Modern InitInfo enables the dynamic atlas.
    ImGui_ImplDX12_InitInfo Init;Init.Device=Device.Get();Init.CommandQueue=&Proxy;Init.NumFramesInFlight=3;
    Init.RTVFormat=DXGI_FORMAT_R8G8B8A8_UNORM;Init.SrvDescriptorHeap=Heap.Get();Init.UserData=&Pool;
    Init.SrvDescriptorAllocFn=DescriptorPool::Allocate;Init.SrvDescriptorFreeFn=DescriptorPool::Free;
    auto BadInit=Init;BadInit.CommandQueue=nullptr;assert(!ImGui_ImplDX12_Init(&BadInit));
    BadInit=Init;BadInit.NumFramesInFlight=0;assert(!ImGui_ImplDX12_Init(&BadInit));
    assert(ImGui_ImplDX12_Init(&Init));ImGui_ImplDX12_NewFrame();assert(ImGui_ImplDX12_IsReady());
    assert(!ImGui_ImplDX12_Init(&Init)); // No replacement of a live backend.
    auto* Backend=ImGui_ImplDX12_GetBackendData();assert(Backend->pCommandQueue==&Proxy&&!Backend->commandQueueOwned);

    ComPtr<ID3D12Fence> Fence;assert(SUCCEEDED(Device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&Fence))));
    HANDLE Event=CreateEvent(nullptr,FALSE,FALSE,nullptr);UINT64 Submitted=0;
    const auto Drain=[&]{const auto V=++Submitted;assert(SUCCEEDED(Queue->Signal(Fence.Get(),V)));assert(SUCCEEDED(Fence->SetEventOnCompletion(V,Event)));assert(WaitForSingleObject(Event,10000)==WAIT_OBJECT_0);assert(SUCCEEDED(Device->GetDeviceRemovedReason()));};
    ComPtr<ID3D12CommandAllocator> Alloc;assert(SUCCEEDED(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&Alloc))));
    ComPtr<ID3D12GraphicsCommandList> List;assert(SUCCEEDED(Device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,Alloc.Get(),nullptr,IID_PPV_ARGS(&List))));List->Close();
    HD.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;HD.NumDescriptors=1;HD.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ComPtr<ID3D12DescriptorHeap> RtvHeap;assert(SUCCEEDED(Device->CreateDescriptorHeap(&HD,IID_PPV_ARGS(&RtvHeap))));
    D3D12_RESOURCE_DESC RD{};RD.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;RD.Width=128;RD.Height=96;RD.DepthOrArraySize=1;RD.MipLevels=1;RD.SampleDesc.Count=1;RD.Format=DXGI_FORMAT_R8G8B8A8_UNORM;RD.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES HP{};HP.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> Target;
    assert(SUCCEEDED(Device->CreateCommittedResource(&HP,D3D12_HEAP_FLAG_NONE,&RD,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&Target))));
    const auto RTV=RtvHeap->GetCPUDescriptorHandleForHeapStart();Device->CreateRenderTargetView(Target.Get(),nullptr,RTV);
    const unsigned Indices[]={2,2,0,2,1,1,0,0,2,1,2,0};
    for(unsigned N=0;N<std::size(Indices);++N){
        Drain();assert(SUCCEEDED(Alloc->Reset()));assert(SUCCEEDED(List->Reset(Alloc.Get(),nullptr)));
        ImGui_ImplDX12_NewFrame();ImGui::NewFrame();auto* Draw=ImGui::GetBackgroundDrawList();
        const unsigned Rectangles=N>=6?4000:16;
        for(unsigned R=0;R<Rectangles;++R)Draw->AddRectFilled({float(R%80),4},{float(R%80+8),12},0xff00ff00);
        Draw->AddText({2,20},0xffffffff,N>5?"Growing font and geometry: ABC xyz 123":"Warm font");ImGui::Render();
        ID3D12DescriptorHeap* Heaps[]={Heap.Get()};List->SetDescriptorHeaps(1,Heaps);List->OMSetRenderTargets(1,&RTV,FALSE,nullptr);
        assert(ImGui_ImplDX12_SetFrameResourceIndex(Indices[N]));ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(),List.Get());
        assert(ImGui_ImplDX12_IsReady()&&ImGui_ImplDX12_LastRenderSucceeded()&&Backend->frameIndex==Indices[N]);
        assert(Backend->pFrameResources[Indices[N]].VertexBuffer!=nullptr);
        assert(SUCCEEDED(List->Close()));ID3D12CommandList* Lists[]={List.Get()};Queue->ExecuteCommandLists(1,Lists);
    }
    assert(Proxy.Submits>=2); // Dynamic glyph growth uploaded on the supplied queue.
    Drain();assert(!ImGui_ImplDX12_SetFrameResourceIndex(3));const auto Before=Backend->frameIndex;
    Alloc->Reset();List->Reset(Alloc.Get(),nullptr);ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(),List.Get());assert(Backend->frameIndex==Before);List->Close();
    assert(!ImGui_ImplDX12_LastRenderSucceeded()&&ImGui_ImplDX12_IsReady());

    // Transient draw allocation and Map failures must fail this submission,
    // preserve the backend, and allow a later normal frame to draw successfully.
    auto* Data=ImGui::GetDrawData();auto& Slot=Backend->pFrameResources[0];
    const auto RenderDiscarded=[&]{assert(SUCCEEDED(Alloc->Reset()));assert(SUCCEEDED(List->Reset(Alloc.Get(),nullptr)));
        ID3D12DescriptorHeap* Heaps[]={Heap.Get()};List->SetDescriptorHeaps(1,Heaps);List->OMSetRenderTargets(1,&RTV,FALSE,nullptr);
        assert(ImGui_ImplDX12_SetFrameResourceIndex(0));ImGui_ImplDX12_RenderDrawData(Data,List.Get());assert(SUCCEEDED(List->Close()));};
    {
        VTableSlotPatch Patch(Device.Get(),27,reinterpret_cast<void*>(FaultCreate));OriginalCreate=reinterpret_cast<CreateResourceFn>(Patch.Original);FailCreate=true;
        const int SavedV=Data->TotalVtxCount,SavedI=Data->TotalIdxCount;auto* OldV=Slot.VertexBuffer;auto* OldI=Slot.IndexBuffer;
        const int OldVSize=Slot.VertexBufferSize,OldISize=Slot.IndexBufferSize;
        Data->TotalVtxCount=OldVSize+1;RenderDiscarded();assert(!ImGui_ImplDX12_LastRenderSucceeded()&&ImGui_ImplDX12_IsReady());
        assert(Slot.VertexBuffer==OldV&&Slot.VertexBufferSize==OldVSize);Data->TotalVtxCount=SavedV;
        Data->TotalIdxCount=OldISize+1;RenderDiscarded();assert(!ImGui_ImplDX12_LastRenderSucceeded()&&ImGui_ImplDX12_IsReady());
        assert(Slot.IndexBuffer==OldI&&Slot.IndexBufferSize==OldISize);Data->TotalIdxCount=SavedI;FailCreate=false;
    }
    {
        VTableSlotPatch MapPatch(Slot.VertexBuffer,8,reinterpret_cast<void*>(FaultMap));OriginalMap=reinterpret_cast<MapFn>(MapPatch.Original);
        VTableSlotPatch UnmapPatch(Slot.VertexBuffer,9,reinterpret_cast<void*>(WatchUnmap));OriginalUnmap=reinterpret_cast<UnmapFn>(UnmapPatch.Original);
        assert((*reinterpret_cast<void***>(Slot.VertexBuffer))[8]==(*reinterpret_cast<void***>(Slot.IndexBuffer))[8]);
        FailMapTarget=Slot.VertexBuffer;RenderDiscarded();assert(!ImGui_ImplDX12_LastRenderSucceeded()&&ImGui_ImplDX12_IsReady());
        FailMapTarget=Slot.IndexBuffer;WatchUnmapTarget=Slot.VertexBuffer;RenderDiscarded();
        assert(!ImGui_ImplDX12_LastRenderSucceeded()&&ImGui_ImplDX12_IsReady()&&WatchedUnmaps==1);
        FailMapTarget=WatchUnmapTarget=nullptr;
    }
    RenderDiscarded();assert(ImGui_ImplDX12_LastRenderSucceeded()&&ImGui_ImplDX12_IsReady());
    const ImVec2 SavedDisplay=Data->DisplaySize;Data->DisplaySize={0,0};RenderDiscarded();assert(ImGui_ImplDX12_LastRenderSucceeded());Data->DisplaySize=SavedDisplay;
    const int SavedV=Data->TotalVtxCount,SavedI=Data->TotalIdxCount;Data->TotalVtxCount=Data->TotalIdxCount=0;RenderDiscarded();assert(ImGui_ImplDX12_LastRenderSucceeded());Data->TotalVtxCount=SavedV;Data->TotalIdxCount=SavedI;

    ImTextureData Texture;Texture.Create(ImTextureFormat_RGBA32,16,16);memset(Texture.Pixels,127,16*16*4);
    Texture.SetStatus(ImTextureStatus_WantCreate);Proxy.FailSignal=true;const unsigned BeforeSubmits=Proxy.Submits;
    ImGui_ImplDX12_UpdateTexture(&Texture);assert(Backend->UploadPending&&Backend->UploadNeedsSignal&&!ImGui_ImplDX12_IsReady());
    auto* HeldAllocator=Backend->pTexCmdAllocator;auto* HeldUpload=Backend->pTexUploadBuffer;auto* HeldTexture=Texture.BackendUserData;
    ImGui_ImplDX12_UpdateTexture(&Texture);ImGui_ImplDX12_InvalidateDeviceObjects();ImGui_ImplDX12_Shutdown();
    assert(ImGui_ImplDX12_GetBackendData()==Backend&&Backend->pTexCmdAllocator==HeldAllocator&&Backend->pTexUploadBuffer==HeldUpload&&Texture.BackendUserData==HeldTexture);
    assert(Proxy.Submits==BeforeSubmits+1);Proxy.FailSignal=false;assert(ImGui_ImplDX12_WaitForPendingUploads(1000));assert(Texture.Status==ImTextureStatus_OK);

    // A queue-side fence blocks the upload without removing the WARP device.
    ComPtr<ID3D12Fence> Gate;assert(SUCCEEDED(Device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&Gate))));
    assert(SUCCEEDED(Queue->Wait(Gate.Get(),1)));Texture.UpdateRect={0,0,8,8};Texture.SetStatus(ImTextureStatus_WantUpdates);FailWait=true;
    ImGui_ImplDX12_UpdateTexture(&Texture);assert(Backend->UploadPending&&!Backend->UploadNeedsSignal&&Texture.Status==ImTextureStatus_WantUpdates);
    const auto HeldFence=Backend->FenceLastSignaledValue;FailWait=false;
    assert(!ImGui_ImplDX12_WaitForPendingUploads(1)); // Timeout must retain everything.
    SetEvent(Backend->FenceEvent);assert(!ImGui_ImplDX12_WaitForPendingUploads(1)); // Stale event isn't fence completion.
    ImGui_ImplDX12_UpdateTexture(&Texture);ImGui_ImplDX12_InvalidateDeviceObjects();ImGui_ImplDX12_Shutdown();
    assert(ImGui_ImplDX12_GetBackendData()==Backend&&Backend->FenceLastSignaledValue==HeldFence&&Backend->pTexCmdAllocator==HeldAllocator);
    assert(SUCCEEDED(Gate->Signal(1)));assert(ImGui_ImplDX12_WaitForPendingUploads(1000));assert(Texture.Status==ImTextureStatus_OK);
    Texture.SetStatus(ImTextureStatus_WantDestroy);Texture.UnusedFrames=3;ImGui_ImplDX12_UpdateTexture(&Texture);assert(!Texture.BackendUserData);
    Texture.DestroyPixels();Texture.SetStatus(ImTextureStatus_Destroyed);Texture.Create(ImTextureFormat_RGBA32,512,512);memset(Texture.Pixels,200,512*512*4);Texture.SetStatus(ImTextureStatus_WantCreate);
    ImGui_ImplDX12_UpdateTexture(&Texture);assert(ImGui_ImplDX12_WaitForPendingUploads(1000));assert(Texture.Status==ImTextureStatus_OK&&Backend->pTexUploadBufferSize>=512*512*4);
    Texture.SetStatus(ImTextureStatus_WantDestroy);Texture.UnusedFrames=3;ImGui_ImplDX12_UpdateTexture(&Texture);assert(!Texture.BackendUserData);
    Drain();ImGui_ImplDX12_Shutdown();assert(!ImGui::GetIO().BackendRendererUserData);ImGui::DestroyContext();CloseHandle(Event);
    if(Info)for(UINT64 I=0;I<Info->GetNumStoredMessages();++I){SIZE_T Size=0;Info->GetMessage(I,nullptr,&Size);std::vector<char> Storage(Size);auto* M=reinterpret_cast<D3D12_MESSAGE*>(Storage.data());Info->GetMessage(I,M,&Size);
        if(M->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::cerr<<M->pDescription<<'\n';return 1;}}
    std::cout<<"DX12 backend WARP passed: explicit nonrotating/repeated frame slots, VB/IB growth, transient VB/IB allocation/Map failures and cleanup/retry, zero/minimized draws, native queue font uploads/updates/growth, Signal failure, wait failure/timeout/stale event retain resources, recovery and shutdown. Debug layer: "<<(HasDebug?"enabled":"unavailable")<<".\n";
}
