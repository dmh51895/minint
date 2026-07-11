/*
 * MinNT - ke/exports/ole32_exports.c
 * ole32.dll exports — COM activation and marshalling.
 * Provides CoInitializeEx, CoCreateInstance, and related infrastructure.
 */

#include <nt/ntdef.h>
#include <nt/ex.h>
#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/exe.h>
#include <ndk/obfuncs.h>
/* Windows types: HRESULT, UINT8, UINT16 */
typedef LONG HRESULT;
#ifndef UINT8
typedef uint8_t UINT8;
#endif
#ifndef UINT16
typedef uint16_t UINT16;
#endif
#ifndef STDMETHODCALLTYPEVCALLTYPE
#define STDMETHODCALLTYPE
#endif

/* HRESULT constants */
#define E_INVALIDARG_local ((HRESULT)0x80070057L)
#define E_NOINTERFACE_local ((HRESULT)0x80004002L)
#define E_POINTER_local ((HRESULT)0x80004003L)
#define REGDB_E_CLASSNOTREG_local ((HRESULT)0x80040154L)
#define CLASS_E_NOAGGREGATION_local ((HRESULT)0x80040110L)
#define E_OUTOFMEMORY_local ((HRESULT)0x8007000EL)
#define S_OK_local ((HRESULT)0)
#define S_FALSE_local ((HRESULT)1)

#ifndef DWORD
typedef unsigned long DWORD;
#endif
#ifndef UINT
typedef unsigned int UINT;
#endif

/* GUID is already defined in D3D12 header; for OLE define separately */
#ifndef GUID_DEFINED
typedef struct _OLE_GUID { ULONG32 Data1; UINT16 Data2; UINT16 Data3; UINT8 Data4[8]; } OLE_GUID;
#endif

/* Per-thread COM apartment state */
static ULONG g_ComInitCount = 0;

/* ============================================================================
 * CoInitialize / CoInitializeEx
 * ========================================================================== */

#define COINIT_APARTMENTTHREADED 0x2
#define COINIT_MULTITHREADED     0x0
#define RPC_E_CHANGED_MODE      ((HRESULT)0x80010106L)
#define S_OK_                    ((HRESULT)0)
#define S_FALSE_                ((HRESULT)1)

__attribute__((ms_abi))
static HRESULT CoInitialize_msabi(PVOID pvReserved)
{
    (void)pvReserved;
    g_ComInitCount++;
    return S_OK_;
}

__attribute__((ms_abi))
static HRESULT CoInitializeEx_msabi(PVOID pvReserved, DWORD dwCoInit)
{
    (void)pvReserved; (void)dwCoInit;
    g_ComInitCount++;
    return S_OK_;
}

__attribute__((ms_abi))
static VOID CoUninitialize_msabi(VOID)
{
    if (g_ComInitCount > 0) g_ComInitCount--;
}

/* ============================================================================
 * CoCreateInstance — resolve CLSID to IClassFactory, then CreateInstance
 *
 * For D3D12 and other COM objects, we maintain a static class registry that
 * maps CLSID to factory functions. In a real implementation this queries the
 * registry; here we use a simple array match for known CLSID values.
 *
 * ExeResolveExport already handles the D3D12CreateDevice path (the primary
 * entry for D3D12 COM objects). CoCreateInstance is used when an application
 * calls the generic path: CLSID -> IClassFactory -> CreateInstance.
 *
 * For DirectX objects, the factory gets created directly via
 * D3D12CreateDevice/DXGIFactory. For unknown CLSIDs, we return
 * REGDB_E_CLASSNOTREG which is the correct Windows behavior.
 * ========================================================================== */

#define REGDB_E_CLASSNOTREG ((HRESULT)0x80040154L)
#define CLASS_E_NOAGGREGATION ((HRESULT)0x80040110L)

/* IClassFactory vtable (C-style) */
typedef struct IClassFactory IClassFactory;
typedef struct IClassFactoryVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IClassFactory *This, const void *riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IClassFactory *This);
    ULONG   (STDMETHODCALLTYPE *Release)(IClassFactory *This);
    HRESULT (STDMETHODCALLTYPE *CreateInstance)(IClassFactory *This, void *pUnkOuter, const void *riid, void **ppv);
    HRESULT (STDMETHODCALLTYPE *LockServer)(IClassFactory *This, BOOL fLock);
} IClassFactoryVtbl;
struct IClassFactory { const IClassFactoryVtbl *lpVtbl; };

/* IID_IUnknown (for the factory itself) */
static const OLE_GUID sg_IID_IUnknown = {
    0x00000000, 0x0000, 0x0000, {0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}
};

/* IID_IClassFactory */
static const OLE_GUID sg_IID_IClassFactory = {
    0x00000001, 0x0000, 0x0000, {0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}
};

/* Implemented directly in kernel: */
extern HRESULT STDMETHODCALLTYPE D3D12CreateDevice(void *pAdapter, int MinimumFeatureLevel,
                                                    const void *riid, void **ppvDevice);

/* Fake IClassFactory instance for objects we know how to create */
typedef struct _SW_COM_FACTORY {
    IClassFactory iface;
    LONG refcount;
    HRESULT (*create_fn)(void *pUnkOuter, const void *riid, void **ppv);
} SW_COM_FACTORY;

static __attribute__((ms_abi)) HRESULT SwFactory_QI(IClassFactory *This, const void *riid, void **ppv)
{
    SW_COM_FACTORY *f = CONTAINING_RECORD(This, SW_COM_FACTORY, iface);
    if (!ppv) return E_INVALIDARG_local;
    *ppv = NULL;
    /* Accept IUnknown or IClassFactory */
    const OLE_GUID *g = (const OLE_GUID *)riid;
    const OLE_GUID *u = &sg_IID_IUnknown;
    const OLE_GUID *c = &sg_IID_IClassFactory;
    if (__builtin_memcmp(g, u, 16) == 0 || __builtin_memcmp(g, c, 16) == 0) {
        *ppv = This;
        f->refcount++;
        return S_OK_;
    }
    return E_NOINTERFACE_local;
}

static __attribute__((ms_abi)) ULONG SwFactory_AddRef(IClassFactory *This)
{
    SW_COM_FACTORY *f = CONTAINING_RECORD(This, SW_COM_FACTORY, iface);
    return ++f->refcount;
}

static __attribute__((ms_abi)) ULONG SwFactory_Release(IClassFactory *This)
{
    SW_COM_FACTORY *f = CONTAINING_RECORD(This, SW_COM_FACTORY, iface);
    LONG n = --f->refcount;
    if (n == 0) ExFreePool(f);
    return n;
}

static __attribute__((ms_abi)) HRESULT SwFactory_CreateInstance(IClassFactory *This, void *pUnkOuter, const void *riid, void **ppv)
{
    SW_COM_FACTORY *f = CONTAINING_RECORD(This, SW_COM_FACTORY, iface);
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    return f->create_fn(pUnkOuter, riid, ppv);
}

static __attribute__((ms_abi)) HRESULT SwFactory_LockServer(IClassFactory *This, BOOL fLock)
{
    (void)This; (void)fLock;
    return S_OK_;
}

static const IClassFactoryVtbl g_SWFactoryVtbl = {
    (void*)SwFactory_QI, SwFactory_AddRef, SwFactory_Release,
    SwFactory_CreateInstance, SwFactory_LockServer
};

/* Known Direct3D / DXGI classes: their CLSID is the same as their IID in most cases. */

/* SG_DXGIFactory1: {770aa378-b32c-4b1d-8fdc-1677sg_DXGIFactory1 */
static const OLE_GUID sg_CLSID_DXGIFactory1 = {
    0x770aa378, 0xb32c, 0x4b1d, {0x8f, 0xdc, 0x16, 0x77, 0x12, 0x69, 0x40, 0x21}
};

/* CLSID_D3D12Debug: {4cf5a917-f218-4d1c-8f33-c493ec1c0c0f */
static const OLE_GUID sg_CLSID_D3D12Debug = {
    0xf2dcd8a1, 0x02f8, 0x4c28, {0xa1, 0x11, 0x47, 0x05, 0x27, 0x09, 0x42, 0x49}
};

/* SwCreateD3D12Device: factory hook that calls our D3D12CreateDevice */
static HRESULT SwCreateD3D12Device(void *pUnkOuter, const void *riid, void **ppv)
{
    (void)pUnkOuter;
    /* Call our existing D3D12CreateDevice with no adapter */
    /* riid is typically IID_ID3D12Device */
    return D3D12CreateDevice(NULL, 0xc100 /* D3D_FEATURE_LEVEL_12_1 */, riid, ppv);
}

/* SwCreateDXGIFactory1: factory hook — returns a fake IDXGIFactory1 interface */
static HRESULT SwCreateDXGIFactory1(void *pUnkOuter, const void *riid, void **ppv)
{
    (void)pUnkOuter; (void)riid;
    /* For now, return a dummy interface. ExeResolveExport handles specific GUIDs
       when applications directly call CreateDXGIFactory1. */
    static struct { const void *lpVtbl; LONG refcount; } fake = { NULL, 1 };
    if (!fake.lpVtbl) fake.lpVtbl = &fake; /* self-referencing fake interface */
    *ppv = &fake;
    return S_OK_;
}

/* List of known CLSIDs/factories */
typedef struct _KNOWN_CLSID {
    const OLE_GUID *clsid;
    HRESULT (*create)(void *pUnkOuter, const void *riid, void **ppv);
} KNOWN_CLSID;

static const KNOWN_CLSID sg_KnownClsids[] = {
    { &sg_CLSID_DXGIFactory1, SwCreateDXGIFactory1 },
    { &sg_CLSID_D3D12Debug,  SwCreateD3D12Device },
    /* Add more as needed */
};
static const ULONG sg_NumKnownClsids = sizeof(sg_KnownClsids) / sizeof(sg_KnownClsids[0]);

__attribute__((ms_abi))
static HRESULT CoCreateInstance_msabi(
    const void *rclsid, void *pUnkOuter, DWORD dwClsContext,
    const void *riid, void **ppv)
{
    if (!ppv) return E_POINTER_local;
    *ppv = NULL;
    if (!rclsid || !riid) return E_POINTER_local;
    (void)dwClsContext; /* CLSCTX_ALL = 0x15 typically */

    const OLE_GUID *clsid = (const OLE_GUID *)rclsid;

    /* Try to find a matching factory */
    for (ULONG i = 0; i < sg_NumKnownClsids; i++) {
        if (__builtin_memcmp(clsid, sg_KnownClsids[i].clsid, 16) == 0) {
            /* Create a factory and call it */
            SW_COM_FACTORY *f = ExAllocatePool(NonPagedPool, sizeof(SW_COM_FACTORY));
            if (!f) return ((HRESULT)0x8007000EL); /* E_OUTOFMEMORY */
            f->iface.lpVtbl = &g_SWFactoryVtbl;
            f->refcount = 1;
            f->create_fn = sg_KnownClsids[i].create;
            HRESULT hr = f->iface.lpVtbl->CreateInstance((IClassFactory *)f, pUnkOuter, riid, ppv);
            f->iface.lpVtbl->Release((IClassFactory *)f);
            return hr;
        }
    }

    return REGDB_E_CLASSNOTREG_local;
}

/* CoGetClassObject: similar to CoCreateInstance but returns the factory directly */
__attribute__((ms_abi))
static HRESULT CoGetClassObject_msabi(
    const void *rclsid, DWORD dwClsContext, PVOID pvReserved,
    const void *riid, void **ppv)
{
    if (!ppv) return E_POINTER_local;
    *ppv = NULL;
    if (!rclsid) return E_POINTER_local;
    (void)dwClsContext; (void)pvReserved; (void)riid;

    const OLE_GUID *clsid = (const OLE_GUID *)rclsid;
    for (ULONG i = 0; i < sg_NumKnownClsids; i++) {
        if (__builtin_memcmp(clsid, sg_KnownClsids[i].clsid, 16) == 0) {
            SW_COM_FACTORY *f = ExAllocatePool(NonPagedPool, sizeof(SW_COM_FACTORY));
            if (!f) return ((HRESULT)0x8007000EL);
            f->iface.lpVtbl = &g_SWFactoryVtbl;
            f->refcount = 1;
            f->create_fn = sg_KnownClsids[i].create;
            *ppv = f;
            return S_OK_;
        }
    }
    return REGDB_E_CLASSNOTREG_local;
}

/* ============================================================================
 * COM Memory — route to Ex pool
 * ========================================================================== */

__attribute__((ms_abi))
static PVOID CoTaskMemAlloc_msabi(SIZE_T cb)
{
    PVOID p = ExAllocatePool(NonPagedPool, cb);
    if (p) RtlZeroMemory(p, cb);
    return p;
}

__attribute__((ms_abi))
static VOID CoTaskMemFree_msabi(PVOID pv)
{
    if (pv) ExFreePool(pv);
}

__attribute__((ms_abi))
static PVOID CoTaskMemRealloc_msabi(PVOID pv, SIZE_T cb)
{
    if (!pv) return CoTaskMemAlloc_msabi(cb);
    PVOID p = ExAllocatePool(NonPagedPool, cb);
    if (p) {
        RtlCopyMemory(p, pv, cb);
        ExFreePool(pv);
    }
    return p;
}

/* ============================================================================
 * String helpers
 * ========================================================================== */

__attribute__((ms_abi))
static HRESULT StringFromCLSID_msabi(const void *rclsid, PWSTR *ppsz)
{
    /* "{12345678-1234-1234-1234-123456789abc}" — 38 chars + null */
    PWSTR buf = ExAllocatePool(NonPagedPool, 78);
    if (!buf) return ((HRESULT)0x8007000EL);
    const OLE_GUID *g = (const OLE_GUID *)rclsid;
    static const CHAR *hex = "0123456789abcdef";
    INT pos = 0;
    buf[pos++] = L'{';
    for (INT i = 28; i >= 0; i -= 4) buf[pos++] = (WCHAR)hex[(g->Data1 >> i) & 0xF];
    buf[pos++] = L'-';
    for (INT i = 12; i >= 0; i -= 4) buf[pos++] = (WCHAR)hex[(g->Data2 >> i) & 0xF];
    buf[pos++] = L'-';
    for (INT i = 12; i >= 0; i -= 4) buf[pos++] = (WCHAR)hex[(g->Data3 >> i) & 0xF];
    buf[pos++] = L'-';
    buf[pos++] = (WCHAR)hex[(g->Data4[0] >> 4) & 0xF];
    buf[pos++] = (WCHAR)hex[g->Data4[0] & 0xF];
    buf[pos++] = (WCHAR)hex[(g->Data4[1] >> 4) & 0xF];
    buf[pos++] = (WCHAR)hex[g->Data4[1] & 0xF];
    buf[pos++] = L'-';
    for (INT i = 2; i < 8; i++) {
        buf[pos++] = (WCHAR)hex[(g->Data4[i] >> 4) & 0xF];
        buf[pos++] = (WCHAR)hex[g->Data4[i] & 0xF];
    }
    buf[pos++] = L'}';
    buf[pos++] = 0;
    *ppsz = buf;
    return S_OK_;
}

__attribute__((ms_abi))
static HRESULT CLSIDFromString_msabi(const WCHAR *lpsz, void *pclsid)
{
    memset_unused:
    if (!lpsz || !pclsid) return E_POINTER_local;
    OLE_GUID *g = (OLE_GUID *)pclsid;
    RtlZeroMemory(g, sizeof(*g));
    /* Skip '{' if present */
    const WCHAR *p = lpsz;
    if (*p == L'{') p++;
    ULONG val = 0;
    for (INT i = 28; i >= 0; i -= 4) {
        WCHAR c = *p++;
        ULONG d = (c >= L'0' && c <= L'9') ? c - L'0' :
                  (c >= L'a' && c <= L'f') ? c - L'a' + 10 :
                  (c >= L'A' && c <= L'F') ? c - L'A' + 10 : 0;
        val |= d << i;
        if (i == 16) { g->Data1 = val; val = 0; }
    }
    /* Skip '-' */
    if (*p == L'-') p++;
    g->Data2 = 0;
    for (INT i = 12; i >= 0; i -= 4) {
        WCHAR c = *p++;
        ULONG d = (c >= L'0' && c <= L'9') ? c - L'0' :
                  (c >= L'a' && c <= L'f') ? c - L'a' + 10 :
                  (c >= L'A' && c <= L'F') ? c - L'A' + 10 : 0;
        g->Data2 |= d << i;
    }
    if (*p == L'-') p++;
    g->Data3 = 0;
    for (INT i = 12; i >= 0; i -= 4) {
        WCHAR c = *p++;
        ULONG d = (c >= L'0' && c <= L'9') ? c - L'0' :
                  (c >= L'a' && c <= L'f') ? c - L'a' + 10 :
                  (c >= L'A' && c <= L'F') ? c - L'A' + 10 : 0;
        g->Data3 |= d << i;
    }
    if (*p == L'-') p++;
    /* Read 2 bytes */
    for (INT i = 0; i < 2; i++) {
        g->Data4[i] = 0;
        for (INT j = 4; j >= 0; j -= 4) {
            WCHAR c = *p++;
            ULONG d = (c >= L'0' && c <= L'9') ? c - L'0' :
                      (c >= L'a' && c <= L'f') ? c - L'a' + 10 :
                      (c >= L'A' && c <= L'F') ? c - L'A' + 10 : 0;
            g->Data4[i] |= d << j;
        }
    }
    if (*p == L'-') p++;
    /* Read 6 bytes */
    for (INT i = 2; i < 8; i++) {
        g->Data4[i] = 0;
        for (INT j = 4; j >= 0; j -= 4) {
            WCHAR c = *p++;
            ULONG d = (c >= L'0' && c <= L'9') ? c - L'0' :
                      (c >= L'a' && c <= L'f') ? c - L'a' + 10 :
                      (c >= L'A' && c <= L'F') ? c - L'A' + 10 : 0;
            g->Data4[i] |= d << j;
        }
    }
    return S_OK_;
}

/* ============================================================================
 * Registration
 * ========================================================================== */

VOID NTAPI Ole32RegisterExports(VOID)
{
#define OREG(name, ptr) ExeRegisterExport("ole32.dll", name, ptr)

    OREG("CoInitialize", CoInitialize_msabi);
    OREG("CoInitializeEx", CoInitializeEx_msabi);
    OREG("CoUninitialize", CoUninitialize_msabi);
    OREG("CoCreateInstance", CoCreateInstance_msabi);
    OREG("CoGetClassObject", CoGetClassObject_msabi);
    OREG("CoTaskMemAlloc", CoTaskMemAlloc_msabi);
    OREG("CoTaskMemFree", CoTaskMemFree_msabi);
    OREG("CoTaskMemRealloc", CoTaskMemRealloc_msabi);
    OREG("StringFromCLSID", StringFromCLSID_msabi);
    OREG("CLSIDFromString", CLSIDFromString_msabi);

    DbgPrint("EXE: ole32.dll exports registered (%lu total)\n", g_ExportCount);
}
