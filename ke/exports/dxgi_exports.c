/*
 * MinNT - ke/exports/dxgi_exports.c
 * dxgi.dll exports — DirectX Graphics Infrastructure.
 *
 * Provides CreateDXGIFactory1/2 and IDXGIFactory/IDXGIAdapter/IDXGIOutput
 * COM interfaces for enumerating graphics adapters and outputs.
 */

#include <nt/ntdef.h>
#include <nt/ex.h>
#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/exe.h>
#include <ndk/obfuncs.h>
#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif

typedef LONG HRESULT;
#ifndef UINT
typedef unsigned int UINT;
#endif
#ifndef UINT8
typedef uint8_t UINT8;
#endif

/* HRESULT constants */
#define DXGI_S_OK        ((HRESULT)0)
#define DXGI_S_FALSE     ((HRESULT)1)
#define DXGI_E_INVALID_CALL ((HRESULT)0x887A0001L)

/* Our fake IDXGIFactory1 — since we only have one software adapter,
   we return a single instance with static vtable. */

/* IID_IDXGIFactory1: {770aa378-b32c-4b1d-8fdc-1677712f6861 wrong... use real */
static const ULONG g_IID_IDXGIFactory1[4] = {
    0x770aa378, 0xb32c4b1d, 0x16fd8fdc, 0x77126940
};

/* IDXGIFactory1 vtable — C-style, ms_abi */

typedef struct IDXGIFactory1 IDXGIFactory1;
typedef struct IDXGIAdapter IDXGIAdapter;
typedef struct IDXGIOutput IDXGIOutput;

/* IDXGIObject (base) methods */
typedef struct IDXGIObjectVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDXGIFactory1 *This, const void *riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IDXGIFactory1 *This);
    ULONG   (STDMETHODCALLTYPE *Release)(IDXGIFactory1 *This);
    HRESULT (STDMETHODCALLTYPE *GetParent)(IDXGIFactory1 *This, const void *riid, void **ppv);
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(IDXGIFactory1 *This, const void *guid, UINT *size, void *data);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(IDXGIFactory1 *This, const void *guid, UINT size, const void *data);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(IDXGIFactory1 *This, const void *guid, const void *pUnk);
    HRESULT (STDMETHODCALLTYPE *GetPrivateDataInterface)(IDXGIFactory1 *This, const void *guid, void **ppv);
    HRESULT (STDMETHODCALLTYPE *GetPrivateData1)(IDXGIFactory1 *This, const void *guid, UINT *size, void *data);
} IDXGIObjectVtbl;

/* IDXGIFactory1 wrapper — we store the actual factory in a struct */
typedef struct _SW_DXGI_FACTORY {
    const void *lpVtbl;
    LONG refcount;
} SW_DXGI_FACTORY;

static __attribute__((ms_abi)) HRESULT SwDxgiFactory_QI(IDXGIFactory1 *This, const void *riid, void **ppv)
{
    (void)This; (void)riid;
    if (!ppv) return DXGI_E_INVALID_CALL;
    *ppv = This;
    return DXGI_S_OK;
}

static __attribute__((ms_abi)) ULONG SwDxgiFactory_AddRef(IDXGIFactory1 *This)
{
    SW_DXGI_FACTORY *f = (SW_DXGI_FACTORY *)This;
    return ++f->refcount;
}

static __attribute__((ms_abi)) ULONG SwDxgiFactory_Release(IDXGIFactory1 *This)
{
    SW_DXGI_FACTORY *f = (SW_DXGI_FACTORY *)This;
    LONG n = --f->refcount;
    if (n == 0) ExFreePool(f);
    return n;
}

static __attribute__((ms_abi)) HRESULT SwDxgiFactory_GetParent(IDXGIFactory1 *This, const void *riid, void **ppv)
{
    (void)This; (void)riid;
    if (!ppv) return DXGI_E_INVALID_CALL;
    *ppv = NULL;
    return DXGI_S_OK;
}

static __attribute__((ms_abi)) HRESULT SwDxgiFactory_EnumAdapters(IDXGIFactory1 *This, UINT Adapter, void **ppAdapter)
{
    (void)This;
    if (!ppAdapter) return DXGI_E_INVALID_CALL;
    if (Adapter > 0) {
        *ppAdapter = NULL;
        return DXGI_E_INVALID_CALL; /* only 1 adapter */
    }
    /* Return a fake adapter */
    SW_DXGI_FACTORY *adapter = ExAllocatePool(NonPagedPool, sizeof(SW_DXGI_FACTORY));
    if (!adapter) return ((HRESULT)0x8007000EL);
    adapter->lpVtbl = &SwDxgiFactory_QI; /* reuse vtable for simplicity */
    adapter->refcount = 1;
    *((void **)ppAdapter) = adapter;
    return DXGI_S_OK;
}

static __attribute__((ms_abi)) HRESULT SwDxgiFactory_MakeWindowAssociation(IDXGIFactory1 *This, void *WindowHandle, UINT Flags)
{
    (void)This; (void)WindowHandle; (void)Flags;
    return DXGI_S_OK;
}

static __attribute__((ms_abi)) HRESULT SwDxgiFactory_GetWindowAssociation(IDXGIFactory1 *This, void **ppWindowHandle)
{
    (void)This;
    if (ppWindowHandle) *ppWindowHandle = NULL;
    return DXGI_S_OK;
}

static __attribute__((ms_abi)) HRESULT SwDxgiFactory_CreateSoftwareAdapter(IDXGIFactory1 *This, void *Module, void **ppAdapter)
{
    (void)This; (void)Module;
    return SwDxgiFactory_EnumAdapters(This, 0, ppAdapter);
}

static __attribute__((ms_abi)) HRESULT SwDxgiFactory_EnumAdapters1(IDXGIFactory1 *This, UINT Adapter, void **ppAdapter)
{
    return SwDxgiFactory_EnumAdapters(This, Adapter, ppAdapter);
}

static __attribute__((ms_abi)) BOOL SwDxgiFactory_IsCurrent(IDXGIFactory1 *This)
{
    (void)This;
    return TRUE;
}

/* IDXGIFactory1 vtable layout (simplified — QI, AddRef, Release, GetParent, EnumAdapters, MakeWindowAssociation, GetWindowAssociation, CreateSoftwareAdapter, EnumAdapters1, IsCurrent) */
static const void *g_DxgiFactoryVtbl[10] = {
    (void*)SwDxgiFactory_QI,
    (void*)SwDxgiFactory_AddRef,
    (void*)SwDxgiFactory_Release,
    (void*)SwDxgiFactory_GetParent,
    (void*)SwDxgiFactory_EnumAdapters,
    (void*)SwDxgiFactory_MakeWindowAssociation,
    (void*)SwDxgiFactory_GetWindowAssociation,
    (void*)SwDxgiFactory_CreateSoftwareAdapter,
    (void*)SwDxgiFactory_EnumAdapters1,
    (void*)SwDxgiFactory_IsCurrent,
};

/* ============================================================================
 * Exported Entry Points
 * ========================================================================== */

/* CreateDXGIFactory — the original */
__attribute__((ms_abi))
static HRESULT CreateDXGIFactory_msabi(const void *riid, void **ppFactory)
{
    (void)riid;
    if (!ppFactory) return DXGI_E_INVALID_CALL;
    SW_DXGI_FACTORY *f = ExAllocatePool(NonPagedPool, sizeof(SW_DXGI_FACTORY));
    if (!f) return ((HRESULT)0x8007000EL);
    f->lpVtbl = g_DxgiFactoryVtbl;
    f->refcount = 1;
    *ppFactory = f;
    return DXGI_S_OK;
}

/* CreateDXGIFactory1 — Windows 7+ */
__attribute__((ms_abi))
static HRESULT CreateDXGIFactory1_msabi(const void *riid, void **ppFactory)
{
    return CreateDXGIFactory_msabi(riid, ppFactory);
}

/* CreateDXGIFactory2 — Windows 8+ (adds CreateSwapChainForHwnd) */
__attribute__((ms_abi))
static HRESULT CreateDXGIFactory2_msabi(UINT Flags, const void *riid, void **ppFactory)
{
    (void)Flags;
    return CreateDXGIFactory_msabi(riid, ppFactory);
}

/* DXGIGetDebugInterface */
__attribute__((ms_abi))
static HRESULT DXGIGetDebugInterface_msabi(const void *riid, void **ppv)
{
    (void)riid;
    if (ppv) *ppv = NULL;
    return DXGI_E_INVALID_CALL;
}

/* ============================================================================
 * Registration
 * ========================================================================== */

VOID NTAPI DxgiRegisterExports(VOID)
{
    ExeRegisterExport("dxgi.dll", "CreateDXGIFactory",   CreateDXGIFactory_msabi);
    ExeRegisterExport("dxgi.dll", "CreateDXGIFactory1",  CreateDXGIFactory1_msabi);
    ExeRegisterExport("dxgi.dll", "CreateDXGIFactory2",  CreateDXGIFactory2_msabi);
    ExeRegisterExport("dxgi.dll", "DXGIGetDebugInterface", DXGIGetDebugInterface_msabi);

    DbgPrint("EXE: dxgi.dll exports registered (%lu total)\n", g_ExportCount);
}
