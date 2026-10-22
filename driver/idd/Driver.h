#pragma once

#define INITGUID
#define NOMINMAX

#include <windows.h>
#include <wudfwdm.h>
#include <wdf.h>
#include <IddCx.h>
#include <avrt.h>
#include <wrl.h>
#include <wrl/client.h>
#include <d3d11_2.h>
#include <dxgi1_5.h>
#include <d3dkmthk.h>

#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

using Microsoft::WRL::ComPtr;

// ── EDID for a generic 1920×1080 "Generic PnP Monitor" ───────────────────────
// Crafted to look like an off-the-shelf 24" panel (descriptor strings stripped).
static const BYTE s_Edid[] = {
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00, // header
    0x4C,0x2D,0x01,0x00,0x00,0x00,0x00,0x00, // mfr=SAM (Samsung), product 0x0001
    0x01,0x21,                                // week 1, year 2023 (1990+33)
    0x01,0x04,                                // EDID 1.4
    0x52,0x2F,0x1E,0x78,                      // digital, 24", gamma 2.2
    0xEE,0xC2,0xA5,0xA4,0x59,0x4A,0x98,0x25,
    0x20,0x50,0x54,
    0x00,0x00,0x00,                           // no established timings
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,            // no standard timings
    // Preferred: 1920×1080 @ 60 Hz (detailed timing descriptor)
    0x02,0x3A,0x80,0x18,0x71,0x38,0x2D,0x40,
    0x58,0x2C,0x45,0x00,0x0F,0x28,0x21,0x00,
    0x00,0x1E,
    // Monitor name descriptor
    0x00,0x00,0x00,0xFC,0x00,
    'G','e','n','e','r','i','c',' ','P','n','P','\n',' ',
    // Monitor serial descriptor (blank)
    0x00,0x00,0x00,0xFF,0x00,
    '0','0','0','0','0','0','0','0','0','0','0','0','\n',
    0x00,  // extension count
    0x00,  // checksum placeholder (recalculated below)
};

// Swap-chain frame dimensions
static constexpr UINT k_Width  = 1920;
static constexpr UINT k_Height = 1080;

// ── Device context ────────────────────────────────────────────────────────────
struct IndirectDeviceContext {
    WDFDEVICE   Device;
    IDDCX_ADAPTER Adapter{};

    void InitAdapter();
    void FinishInit(UINT ConnectorIndex);

    static IndirectDeviceContext* Get(WDFDEVICE dev) {
        return reinterpret_cast<IndirectDeviceContext*>(
            DeviceGetContext(dev));
    }
};

WDF_DECLARE_CONTEXT_TYPE(IndirectDeviceContext);

// ── Monitor context ───────────────────────────────────────────────────────────
struct SwapChainProcessor;

struct IndirectMonitorContext {
    IDDCX_MONITOR Monitor{};
    std::unique_ptr<SwapChainProcessor> Processor;

    static IndirectMonitorContext* Get(IDDCX_MONITOR mon) {
        return WdfObjectGet_IndirectMonitorContext(mon);
    }
};

WDF_DECLARE_CONTEXT_TYPE(IndirectMonitorContext);

// ── Swap-chain processor ──────────────────────────────────────────────────────
struct SwapChainProcessor {
    IDDCX_SWAPCHAIN SwapChain;
    ComPtr<IDXGISwapChain1> DxSwapChain;
    HANDLE NewFrameEvent{};
    std::thread Thread;
    std::atomic<bool> Terminate{ false };

    SwapChainProcessor(IDDCX_SWAPCHAIN sc,
                       ComPtr<IDirect3DDevice> device,
                       HANDLE newFrameEvent);
    ~SwapChainProcessor();

private:
    void Run();
    void RunCore();
};

// ── WDF / IddCx callbacks (defined in Driver.cpp) ────────────────────────────
extern "C" {
    DRIVER_INITIALIZE DriverEntry;
}
EVT_WDF_DRIVER_DEVICE_ADD             IddDeviceAdd;
EVT_WDF_DEVICE_D0_ENTRY               IddDeviceD0Entry;
EVT_IDD_CX_ADAPTER_INIT_FINISHED      IddAdapterInitFinished;
EVT_IDD_CX_ADAPTER_COMMIT_MODES       IddAdapterCommitModes;
EVT_IDD_CX_PARSE_MONITOR_DESCRIPTION  IddParseMonitorDescription;
EVT_IDD_CX_MONITOR_GET_DEFAULT_DESCRIPTION_MODES IddMonitorGetDefaultModes;
EVT_IDD_CX_MONITOR_QUERY_TARGET_MODES IddMonitorQueryModes;
EVT_IDD_CX_MONITOR_ASSIGN_SWAP_CHAIN  IddMonitorAssignSwapChain;
EVT_IDD_CX_MONITOR_UNASSIGN_SWAP_CHAIN IddMonitorUnassignSwapChain;
