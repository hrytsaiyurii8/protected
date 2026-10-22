#include "Driver.h"
#include "Trace.h"

// ── DriverEntry ───────────────────────────────────────────────────────────────
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject,
                                 PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, IddDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath,
                           WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

// ── IddDeviceAdd ─────────────────────────────────────────────────────────────
NTSTATUS IddDeviceAdd(WDFDRIVER /*Driver*/, PWDFDEVICE_INIT DeviceInit)
{
    // Mark as an indirect display device
    IDD_CX_CLIENT_CONFIG iddConfig;
    IDD_CX_CLIENT_CONFIG_INIT(&iddConfig);
    iddConfig.EvtIddCxAdapterInitFinished             = IddAdapterInitFinished;
    iddConfig.EvtIddCxAdapterCommitModes              = IddAdapterCommitModes;
    iddConfig.EvtIddCxParseMonitorDescription         = IddParseMonitorDescription;
    iddConfig.EvtIddCxMonitorGetDefaultDescriptionModes = IddMonitorGetDefaultModes;
    iddConfig.EvtIddCxMonitorQueryTargetModes         = IddMonitorQueryModes;
    iddConfig.EvtIddCxMonitorAssignSwapChain          = IddMonitorAssignSwapChain;
    iddConfig.EvtIddCxMonitorUnassignSwapChain        = IddMonitorUnassignSwapChain;

    NTSTATUS status = IddCxDeviceInitConfig(DeviceInit, &iddConfig);
    if (!NT_SUCCESS(status)) return status;

    // Register D0Entry power callback
    WDF_POWER_EVENT_CALLBACKS powerCbs;
    WDF_POWER_EVENT_CALLBACKS_INIT(&powerCbs);
    powerCbs.EvtDeviceD0Entry = IddDeviceD0Entry;
    WdfDeviceInitSetPowerEventCallbacks(DeviceInit, &powerCbs);

    // Create the WDF device
    WDF_OBJECT_ATTRIBUTES attrs;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, IndirectDeviceContext);

    WDFDEVICE device;
    status = WdfDeviceCreate(&DeviceInit, &attrs, &device);
    if (!NT_SUCCESS(status)) return status;

    // Initialize our context
    auto* ctx = IndirectDeviceContext::Get(device);
    ctx->Device = device;
    ctx->InitAdapter();
    return STATUS_SUCCESS;
}

// ── IndirectDeviceContext::InitAdapter ────────────────────────────────────────
void IndirectDeviceContext::InitAdapter()
{
    IDDCX_ADAPTER_CAPS caps = {};
    caps.Size = sizeof(caps);
    caps.MaxMonitorsSupported = 1;
    caps.EndPointDiagnostics.Size = sizeof(caps.EndPointDiagnostics);
    caps.EndPointDiagnostics.GammaSupport =
        IDDCX_FEATURE_IMPLEMENTATION_NONE;
    caps.EndPointDiagnostics.TransmissionType =
        IDDCX_TRANSMISSION_TYPE_OTHER;

    // Friendly strings (shown in Device Manager)
    static constexpr wchar_t kMfr[]   = L"Microsoft Corporation";
    static constexpr wchar_t kModel[]  = L"Microsoft Display Adapter";
    static constexpr wchar_t kSerial[] = L"0000000000001";
    static constexpr wchar_t kFW[]     = L"1.0.0";
    caps.EndPointDiagnostics.pEndPointFriendlyName       = kModel;
    caps.EndPointDiagnostics.pEndPointManufacturerName   = kMfr;
    caps.EndPointDiagnostics.pEndPointModelName          = kModel;

    IDARG_IN_ADAPTER_INIT adapterIn = {};
    adapterIn.WdfDevice   = Device;
    adapterIn.pCaps       = &caps;
    adapterIn.ObjectAttributes = WDF_NO_OBJECT_ATTRIBUTES;

    IDARG_OUT_ADAPTER_INIT adapterOut = {};
    IddCxAdapterInitAsync(&adapterIn, &adapterOut);
    Adapter = adapterOut.AdapterObject;
}

// ── IddAdapterInitFinished ────────────────────────────────────────────────────
NTSTATUS IddAdapterInitFinished(IDDCX_ADAPTER Adapter,
    const IDARG_IN_ADAPTER_INIT_FINISHED* pInArgs)
{
    if (!NT_SUCCESS(pInArgs->AdapterInitStatus)) return STATUS_SUCCESS;

    // Tell IddCx we have one connected monitor
    auto* ctx = IndirectDeviceContext::Get(
        reinterpret_cast<WDFDEVICE>(IddCxAdapterWdfDevice(Adapter)));
    ctx->FinishInit(0);
    return STATUS_SUCCESS;
}

void IndirectDeviceContext::FinishInit(UINT ConnectorIndex)
{
    // Fix EDID checksum
    BYTE edid[sizeof(s_Edid)];
    memcpy(edid, s_Edid, sizeof(edid));
    BYTE sum = 0;
    for (int i = 0; i < 127; ++i) sum += edid[i];
    edid[127] = (BYTE)(256 - sum);

    IDDCX_MONITOR_INFO info = {};
    info.Size = sizeof(info);
    info.MonitorType = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER;
    info.ConnectorIndex = ConnectorIndex;
    info.MonitorDescription.Size = sizeof(info.MonitorDescription);
    info.MonitorDescription.Type = IDDCX_MONITOR_DESCRIPTION_TYPE_EDID;
    info.MonitorDescription.DataSize = sizeof(edid);
    info.MonitorDescription.pData = edid;

    IDARG_IN_MONITORCREATE monCreate = {};
    monCreate.ObjectAttributes = WDF_NO_OBJECT_ATTRIBUTES;
    monCreate.pMonitorInfo = &info;

    IDARG_OUT_MONITORCREATE monOut = {};
    NTSTATUS status = IddCxMonitorCreate(Adapter, &monCreate, &monOut);
    if (!NT_SUCCESS(status)) return;

    auto* monCtx = IndirectMonitorContext::Get(monOut.MonitorObject);
    monCtx->Monitor = monOut.MonitorObject;

    IDARG_IN_MONITOR_ARRIVED arrived = {};
    arrived.MonitorObject = monOut.MonitorObject;
    IddCxMonitorArrival(Adapter, &arrived);
}

// ── IddDeviceD0Entry ──────────────────────────────────────────────────────────
NTSTATUS IddDeviceD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE /*PreviousState*/)
{
    auto* ctx = IndirectDeviceContext::Get(Device);
    ctx->InitAdapter();
    return STATUS_SUCCESS;
}

// ── Mode tables ───────────────────────────────────────────────────────────────
static const struct { UINT W, H, N, D; } s_Modes[] = {
    { 1920, 1080, 60, 1 },
    { 1920, 1080, 30, 1 },
    { 1280, 720,  60, 1 },
};

NTSTATUS IddAdapterCommitModes(IDDCX_ADAPTER /*Adapter*/,
    const IDARG_IN_COMMITMODES* /*pInArgs*/)
{
    return STATUS_SUCCESS;
}

NTSTATUS IddParseMonitorDescription(
    const IDARG_IN_PARSEMONITORDESCRIPTION* pInArgs,
    IDARG_OUT_PARSEMONITORDESCRIPTION* pOutArgs)
{
    pOutArgs->MonitorModeBufferOutputCount = ARRAYSIZE(s_Modes);
    if (pInArgs->MonitorModeBufferInputCount == 0) return STATUS_BUFFER_TOO_SMALL;

    for (UINT i = 0; i < ARRAYSIZE(s_Modes) &&
                     i < pInArgs->MonitorModeBufferInputCount; ++i)
    {
        auto& m = pInArgs->pMonitorModes[i];
        m.Size = sizeof(m);
        m.Origin = IDDCX_MONITOR_MODE_ORIGIN_MONITORDESCRIPTOR;
        m.MonitorVideoSignalInfo.TotalSize.cx = s_Modes[i].W;
        m.MonitorVideoSignalInfo.TotalSize.cy = s_Modes[i].H;
        m.MonitorVideoSignalInfo.ActiveSize.cx = s_Modes[i].W;
        m.MonitorVideoSignalInfo.ActiveSize.cy = s_Modes[i].H;
        m.MonitorVideoSignalInfo.VSyncFreq.Numerator   = s_Modes[i].N;
        m.MonitorVideoSignalInfo.VSyncFreq.Denominator = s_Modes[i].D;
        m.MonitorVideoSignalInfo.HSyncFreq.Numerator   = s_Modes[i].H * s_Modes[i].N;
        m.MonitorVideoSignalInfo.HSyncFreq.Denominator = s_Modes[i].D;
        m.MonitorVideoSignalInfo.PixelRate              = (UINT64)s_Modes[i].W * s_Modes[i].H * s_Modes[i].N;
        m.MonitorVideoSignalInfo.ScanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
    }
    pOutArgs->PreferredMonitorModeIdx = 0;
    return STATUS_SUCCESS;
}

NTSTATUS IddMonitorGetDefaultModes(IDDCX_MONITOR /*Monitor*/,
    const IDARG_IN_GETDEFAULTDESCRIPTIONMODES* pInArgs,
    IDARG_OUT_GETDEFAULTDESCRIPTIONMODES* pOutArgs)
{
    pOutArgs->DefaultMonitorModeBufferOutputCount = ARRAYSIZE(s_Modes);
    if (pInArgs->DefaultMonitorModeBufferInputCount == 0) return STATUS_BUFFER_TOO_SMALL;

    for (UINT i = 0; i < ARRAYSIZE(s_Modes) &&
                     i < pInArgs->DefaultMonitorModeBufferInputCount; ++i)
    {
        auto& m = pInArgs->pDefaultMonitorModes[i];
        m.Size = sizeof(m);
        m.Origin = IDDCX_MONITOR_MODE_ORIGIN_DRIVER;
        m.MonitorVideoSignalInfo.TotalSize.cx = s_Modes[i].W;
        m.MonitorVideoSignalInfo.TotalSize.cy = s_Modes[i].H;
        m.MonitorVideoSignalInfo.ActiveSize.cx = s_Modes[i].W;
        m.MonitorVideoSignalInfo.ActiveSize.cy = s_Modes[i].H;
        m.MonitorVideoSignalInfo.VSyncFreq.Numerator   = s_Modes[i].N;
        m.MonitorVideoSignalInfo.VSyncFreq.Denominator = s_Modes[i].D;
        m.MonitorVideoSignalInfo.HSyncFreq.Numerator   = s_Modes[i].H * s_Modes[i].N;
        m.MonitorVideoSignalInfo.HSyncFreq.Denominator = s_Modes[i].D;
        m.MonitorVideoSignalInfo.PixelRate = (UINT64)s_Modes[i].W * s_Modes[i].H * s_Modes[i].N;
        m.MonitorVideoSignalInfo.ScanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
    }
    pOutArgs->PreferredDefaultModeIdx = 0;
    return STATUS_SUCCESS;
}

NTSTATUS IddMonitorQueryModes(IDDCX_MONITOR /*Monitor*/,
    const IDARG_IN_QUERYTARGETMODES* pInArgs,
    IDARG_OUT_QUERYTARGETMODES* pOutArgs)
{
    pOutArgs->TargetModeBufferOutputCount = ARRAYSIZE(s_Modes);
    if (pInArgs->TargetModeBufferInputCount == 0) return STATUS_BUFFER_TOO_SMALL;

    for (UINT i = 0; i < ARRAYSIZE(s_Modes) &&
                     i < pInArgs->TargetModeBufferInputCount; ++i)
    {
        auto& m = pInArgs->pTargetModes[i];
        m.Size = sizeof(m);
        m.TargetVideoSignalInfo.targetVideoSignalInfo.TotalSize.cx = s_Modes[i].W;
        m.TargetVideoSignalInfo.targetVideoSignalInfo.TotalSize.cy = s_Modes[i].H;
        m.TargetVideoSignalInfo.targetVideoSignalInfo.ActiveSize.cx = s_Modes[i].W;
        m.TargetVideoSignalInfo.targetVideoSignalInfo.ActiveSize.cy = s_Modes[i].H;
        m.TargetVideoSignalInfo.targetVideoSignalInfo.VSyncFreq.Numerator   = s_Modes[i].N;
        m.TargetVideoSignalInfo.targetVideoSignalInfo.VSyncFreq.Denominator = s_Modes[i].D;
        m.TargetVideoSignalInfo.targetVideoSignalInfo.HSyncFreq.Numerator   = s_Modes[i].H * s_Modes[i].N;
        m.TargetVideoSignalInfo.targetVideoSignalInfo.HSyncFreq.Denominator = s_Modes[i].D;
        m.TargetVideoSignalInfo.targetVideoSignalInfo.PixelRate = (UINT64)s_Modes[i].W * s_Modes[i].H * s_Modes[i].N;
        m.TargetVideoSignalInfo.targetVideoSignalInfo.ScanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
    }
    return STATUS_SUCCESS;
}

// ── Swap-chain assign / unassign ──────────────────────────────────────────────
NTSTATUS IddMonitorAssignSwapChain(IDDCX_MONITOR Monitor,
    const IDARG_IN_SETSWAPCHAIN* pInArgs)
{
    auto* ctx = IndirectMonitorContext::Get(Monitor);
    ctx->Processor.reset();
    ctx->Processor = std::make_unique<SwapChainProcessor>(
        pInArgs->hSwapChain, pInArgs->pDevice, pInArgs->RenderAdapterLuid);
    return STATUS_SUCCESS;
}

NTSTATUS IddMonitorUnassignSwapChain(IDDCX_MONITOR Monitor)
{
    auto* ctx = IndirectMonitorContext::Get(Monitor);
    ctx->Processor.reset();
    return STATUS_SUCCESS;
}

// ── SwapChainProcessor ────────────────────────────────────────────────────────
SwapChainProcessor::SwapChainProcessor(IDDCX_SWAPCHAIN sc,
    ComPtr<IDirect3DDevice> device, HANDLE newFrameEvent)
    : SwapChain(sc), NewFrameEvent(newFrameEvent)
{
    Thread = std::thread([this, device]() {
        ComPtr<ID3D11Device> d3d;
        ComPtr<ID3D11DeviceContext> ctx;

        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            0, nullptr, 0, D3D11_SDK_VERSION, &d3d, nullptr, &ctx);
        if (FAILED(hr)) return;

        Run();
    });
}

SwapChainProcessor::~SwapChainProcessor()
{
    Terminate = true;
    if (NewFrameEvent) SetEvent(NewFrameEvent);
    if (Thread.joinable()) Thread.join();
}

void SwapChainProcessor::Run()
{
    // Open shared memory + event written by ScrDisplaySvc.dll
    HANDLE hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\ScrDisplayFrame");
    HANDLE hSvcEv = OpenEventW(SYNCHRONIZE, FALSE, L"Local\\ScrDisplayFrameReady");

    static constexpr UINT W = k_Width, H = k_Height;
    static constexpr UINT stride = W * 4; // BGRA32

    HANDLE waitHandles[2] = { NewFrameEvent, hSvcEv ? hSvcEv : NewFrameEvent };
    DWORD  nWait = hSvcEv ? 2 : 1;

    while (!Terminate) {
        // Wait for IddCx vsync signal or service frame-ready event
        WaitForMultipleObjects(nWait, waitHandles, FALSE, 33 /*~30 fps fallback*/);

        if (Terminate) break;

        // Acquire the IddCx swap-chain buffer
        IDARG_OUT_RELEASEANDACQUIREBUFFER acquireOut = {};
        NTSTATUS status = IddCxSwapChainReleaseAndAcquireBuffer(SwapChain, &acquireOut);
        if (!NT_SUCCESS(status)) continue;

        // Copy service-rendered pixels into the surface.
        // Layout: [UINT W][UINT H][BGRA pixels...] — skip 8-byte header.
        const SIZE_T kHdrSize = 8;
        if (hMap && acquireOut.MetaData.pSurface) {
            PBYTE base = static_cast<PBYTE>(
                MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, kHdrSize + (SIZE_T)stride * H));
            PBYTE src = base ? base + kHdrSize : nullptr;
            if (src) {
                IDXGISurface* surf = nullptr;
                if (SUCCEEDED(acquireOut.MetaData.pSurface->QueryInterface(
                        __uuidof(IDXGISurface), reinterpret_cast<void**>(&surf))))
                {
                    DXGI_MAPPED_RECT rect = {};
                    if (SUCCEEDED(surf->Map(&rect, DXGI_MAP_WRITE))) {
                        for (UINT row = 0; row < H; ++row)
                            memcpy(rect.pBits + (SIZE_T)row * rect.Pitch,
                                   src  + (SIZE_T)row * stride, stride);
                        surf->Unmap();
                    }
                    surf->Release();
                }
                UnmapViewOfFile(base);
            }
        }

        IddCxSwapChainFinishedProcessingFrame(SwapChain);
    }

    if (hMap)   CloseHandle(hMap);
    if (hSvcEv) CloseHandle(hSvcEv);
}
