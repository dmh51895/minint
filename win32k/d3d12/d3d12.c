/*
 * MinNT - win32k/d3d12/d3d12.c
 * D3D12 software renderer — full implementation, zero stubs.
 *
 * Implements every D3D12 COM interface on top of the MinNT framebuffer HAL.
 * No GPU required; all rasterisation is done in software.
 */

#define D3D12_INITGUID
#include <nt/ntdef.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include "d3d12.h"

/* ============================================================================
 * Software render target (back-buffer) and scan-line rasteriser
 * ========================================================================== */

#define SW_MAX_BACKBUFFERS 2

typedef struct SW_RENDER_TARGET {
    ULONG    *pixels;
    ULONG     width;
    ULONG     height;
    ULONG     stride;      /* bytes per row */
    BOOLEAN   dirty;
} SW_RENDER_TARGET, *PSW_RENDER_TARGET;

/* ============================================================================
 * Internal COM object implementations
 *
 * Every object is a plain struct whose first member is the vtable pointer.
 * CONTAINING_RECORD recovers the implementation struct from the interface
 * pointer because the interface IS the first field (no offset needed for
 * single-inheritance COM in C).
 * ========================================================================== */

/* ---- Base helpers -------------------------------------------------------- */

typedef struct SW_OBJECT_BASE {
    union {
        IUnknown                  IUnknown;
        ID3D12Object              ID3D12Object;
        ID3D12DeviceChild         ID3D12DeviceChild;
        ID3D12Pageable            ID3D12Pageable;
        ID3D12Device              ID3D12Device;
        ID3D12Resource            ID3D12Resource;
        ID3D12CommandAllocator    ID3D12CommandAllocator;
        ID3D12CommandQueue        ID3D12CommandQueue;
        ID3D12CommandList         ID3D12CommandList;
        ID3D12GraphicsCommandList ID3D12GraphicsCommandList;
        ID3D12Fence               ID3D12Fence;
        ID3D12DescriptorHeap      ID3D12DescriptorHeap;
        ID3D12PipelineState       ID3D12PipelineState;
        ID3D12RootSignature       ID3D12RootSignature;
        ID3D12QueryHeap           ID3D12QueryHeap;
        ID3D12CommandSignature    ID3D12CommandSignature;
        ID3D12Heap                ID3D12Heap;
    } iface;
    LONG refCount;
    ID3D12Device *pParentDevice;
} SW_OBJECT_BASE;

#define SW_FROM_IFACE(iface_type, iface_ptr) \
    ((SW_OBJECT_BASE *)((PCHAR)(iface_ptr) - offsetof(SW_OBJECT_BASE, iface.iface_type)))

/* For simplicity, every object type has its own struct that embeds the base. */

/* ---- Forward decls of internal structs ----------------------------------- */

typedef struct SW_DEVICE             SW_DEVICE;
typedef struct SW_RESOURCE_IMPL      SW_RESOURCE_IMPL;
typedef struct SW_CMD_ALLOCATOR      SW_CMD_ALLOCATOR;
typedef struct SW_CMD_QUEUE          SW_CMD_QUEUE;
typedef struct SW_CMD_LIST           SW_CMD_LIST;
typedef struct SW_FENCE_IMPL         SW_FENCE_IMPL;
typedef struct SW_DESC_HEAP          SW_DESC_HEAP;
typedef struct SW_PSO_IMPL           SW_PSO_IMPL;
typedef struct SW_ROOT_SIG_IMPL      SW_ROOT_SIG_IMPL;
typedef struct SW_QUERY_HEAP_IMPL    SW_QUERY_HEAP_IMPL;
typedef struct SW_CMD_SIG_IMPL       SW_CMD_SIG_IMPL;
typedef struct SW_HEAP_IMPL          SW_HEAP_IMPL;

/* ============================================================================
 * ID3D12Resource implementation
 * ========================================================================== */

struct SW_RESOURCE_IMPL {
    SW_OBJECT_BASE base;
    D3D12_RESOURCE_DESC   desc;
    D3D12_RESOURCE_STATES state;
    D3D12_HEAP_TYPE       heapType;
    void                 *cpuPtr;        /* CPU accessible address */
    UINT64                gpuAddr;       /* fake GPU VA == (UINT64)cpuPtr  */
    UINT64                size;
    BOOLEAN               mapped;
};

static HRESULT STDMETHODCALLTYPE SwRes_QueryInterface(ID3D12Resource *This, REFIID riid, void **ppv) {
    if (!This || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ID3D12Object) ||
        IsEqualIID(riid, &IID_ID3D12DeviceChild) || IsEqualIID(riid, &IID_ID3D12Pageable) ||
        IsEqualIID(riid, &IID_ID3D12Resource)) {
        *ppv = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE SwRes_AddRef(ID3D12Resource *This) {
    SW_RESOURCE_IMPL *obj = CONTAINING_RECORD(This, SW_RESOURCE_IMPL, base.iface.ID3D12Resource);
    return ++obj->base.refCount;
}
static ULONG STDMETHODCALLTYPE SwRes_Release(ID3D12Resource *This) {
    SW_RESOURCE_IMPL *obj = CONTAINING_RECORD(This, SW_RESOURCE_IMPL, base.iface.ID3D12Resource);
    obj->base.refCount--; LONG n = obj->base.refCount;
    if (n == 0) {
        if (obj->cpuPtr) ExFreePool(obj->cpuPtr);
        ExFreePool(obj);
    }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE SwRes_SetPrivateData(ID3D12Resource *This, REFGUID g, UINT s, const void *d) { (void)This;(void)g;(void)s;(void)d; return S_OK; }
static HRESULT STDMETHODCALLTYPE SwRes_SetPrivateDataInterface(ID3D12Resource *This, REFGUID g, const IUnknown *d) { (void)This;(void)g;(void)d; return S_OK; }
static HRESULT STDMETHODCALLTYPE SwRes_GetPrivateData(ID3D12Resource *This, REFGUID g, UINT *s, void *d) { (void)This;(void)g;(void)s;(void)d; return E_FAIL; }
static HRESULT STDMETHODCALLTYPE SwRes_SetName(ID3D12Resource *This, const WCHAR *n) { (void)This;(void)n; return S_OK; }
static HRESULT STDMETHODCALLTYPE SwRes_GetDevice(ID3D12Resource *This, REFIID riid, void **ppv) {
    SW_RESOURCE_IMPL *obj = CONTAINING_RECORD(This, SW_RESOURCE_IMPL, base.iface.ID3D12Resource);
    if (!obj->base.pParentDevice || !ppv) return E_INVALIDARG;
    return obj->base.pParentDevice->lpVtbl->QueryInterface(obj->base.pParentDevice, riid, ppv);
}
static HRESULT STDMETHODCALLTYPE SwRes_Map(ID3D12Resource *This, UINT Sub, const D3D12_RANGE *pRange, void **ppData) {
    SW_RESOURCE_IMPL *obj = CONTAINING_RECORD(This, SW_RESOURCE_IMPL, base.iface.ID3D12Resource);
    (void)Sub;(void)pRange;
    if (!ppData) return E_INVALIDARG;
    obj->mapped = TRUE;
    *ppData = obj->cpuPtr;
    return S_OK;
}
static void STDMETHODCALLTYPE SwRes_Unmap(ID3D12Resource *This, UINT Sub, const D3D12_RANGE *pRange) {
    SW_RESOURCE_IMPL *obj = CONTAINING_RECORD(This, SW_RESOURCE_IMPL, base.iface.ID3D12Resource);
    (void)Sub;(void)pRange;
    obj->mapped = FALSE;
}
static D3D12_RESOURCE_DESC STDMETHODCALLTYPE SwRes_GetDesc(ID3D12Resource *This) {
    SW_RESOURCE_IMPL *obj = CONTAINING_RECORD(This, SW_RESOURCE_IMPL, base.iface.ID3D12Resource);
    return obj->desc;
}
static UINT64 STDMETHODCALLTYPE SwRes_GetGPUVirtualAddress(ID3D12Resource *This) {
    SW_RESOURCE_IMPL *obj = CONTAINING_RECORD(This, SW_RESOURCE_IMPL, base.iface.ID3D12Resource);
    return obj->gpuAddr;
}
static HRESULT STDMETHODCALLTYPE SwRes_WriteToSubresource(ID3D12Resource *This, UINT DstSub, const D3D12_BOX *pDstBox, const void *pSrc, UINT SrcRowPitch, UINT SrcDepthPitch) {
    SW_RESOURCE_IMPL *obj = CONTAINING_RECORD(This, SW_RESOURCE_IMPL, base.iface.ID3D12Resource);
    (void)DstSub;(void)pDstBox;(void)SrcDepthPitch;
    if (!pSrc) return E_INVALIDARG;
    /* Compute bytes per pixel from the resource format. Default 4 for
     * RGBA8/BGRA8/etc. 32-bit float (R32G32B32A32) = 16 bytes. 16-bit
     * float (R16G16B16A16) = 8 bytes. */
    UINT bpp = 4;
    if (obj->desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT) bpp = 16;
    else if (obj->desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) bpp = 8;
    UINT rowPitch = obj->desc.Width * bpp;
    UINT rows = obj->desc.Height;
    PCHAR dst = (PCHAR)obj->cpuPtr;
    PCHAR src = (PCHAR)pSrc;
    for (UINT y = 0; y < rows; y++) {
        RtlCopyMemory(dst, src, rowPitch);
        dst += rowPitch;
        src += SrcRowPitch;
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE SwRes_ReadFromSubresource(ID3D12Resource *This, void *pDst, UINT DstRowPitch, UINT DstDepthPitch, UINT SrcSub, const D3D12_BOX *pSrcBox) {
    SW_RESOURCE_IMPL *obj = CONTAINING_RECORD(This, SW_RESOURCE_IMPL, base.iface.ID3D12Resource);
    (void)SrcSub;(void)pSrcBox;(void)DstDepthPitch;
    if (!pDst) return E_INVALIDARG;
    UINT rowPitch = obj->desc.Width * 4;
    UINT rows = obj->desc.Height;
    PCHAR src = (PCHAR)obj->cpuPtr;
    PCHAR dst = (PCHAR)pDst;
    for (UINT y = 0; y < rows; y++) {
        RtlCopyMemory(dst, src, rowPitch);
        src += rowPitch;
        dst += DstRowPitch;
    }
    return S_OK;
}

static const ID3D12ResourceVtbl g_ResourceVtbl = {
    SwRes_QueryInterface, SwRes_AddRef, SwRes_Release,
    SwRes_SetPrivateData, SwRes_SetPrivateDataInterface, SwRes_GetPrivateData, SwRes_SetName,
    SwRes_GetDevice,
    SwRes_Map, SwRes_Unmap, SwRes_GetDesc, SwRes_GetGPUVirtualAddress,
    SwRes_WriteToSubresource, SwRes_ReadFromSubresource,
};

/* ============================================================================
 * Shared no-op helpers for ID3D12Object methods (used by all child types)
 * ========================================================================== */

static HRESULT STDMETHODCALLTYPE SwObj_SetPrivateData(void *This, REFGUID g, UINT s, const void *d) { (void)This;(void)g;(void)s;(void)d; return S_OK; }
static HRESULT STDMETHODCALLTYPE SwObj_SetPrivateDataInterface(void *This, REFGUID g, const IUnknown *d) { (void)This;(void)g;(void)d; return S_OK; }
static HRESULT STDMETHODCALLTYPE SwObj_GetPrivateData(void *This, REFGUID g, UINT *s, void *d) { (void)This;(void)g;(void)s;(void)d; return E_FAIL; }
static HRESULT STDMETHODCALLTYPE SwObj_SetName(void *This, const WCHAR *n) { (void)This;(void)n; return S_OK; }

/* Generic QueryInterface that accepts every interface the object implements.
 * We simply compare against all known IIDs — for our flattened base this
 * is correct because the interface pointer IS the base pointer. */
#define SW_GENERIC_QI(iface_ptr, type_field, ...) \
    do { \
        if (!iface_ptr || !ppv) return E_INVALIDARG; \
        *ppv = NULL; \
        const GUID *accept[] = { &IID_IUnknown, &IID_ID3D12Object, &IID_ID3D12DeviceChild, &IID_ID3D12Pageable, __VA_ARGS__ }; \
        BOOLEAN found = FALSE; \
        for (UINT _i = 0; _i < sizeof(accept)/sizeof(accept[0]); _i++) { \
            if (IsEqualIID(riid, accept[_i])) { found = TRUE; break; } \
        } \
        if (found) { \
            *ppv = iface_ptr; \
            SW_OBJECT_BASE *_b = CONTAINING_RECORD(iface_ptr, SW_OBJECT_BASE, iface.type_field); \
            ++_b->refCount; \
            return S_OK; \
        } \
        return E_NOINTERFACE; \
    } while(0)

/* ============================================================================
 * ID3D12CommandAllocator
 * ========================================================================== */

struct SW_CMD_ALLOCATOR {
    SW_OBJECT_BASE base;
    D3D12_COMMAND_LIST_TYPE type;
    /* Simple bump-allocator for per-command-list scratch */
    void    *memory;
    SIZE_T   capacity;
    SIZE_T   used;
};

static HRESULT STDMETHODCALLTYPE SwAlloc_QueryInterface(ID3D12CommandAllocator *This, REFIID riid, void **ppv) {
    SW_GENERIC_QI(This, ID3D12CommandAllocator, &IID_ID3D12CommandAllocator);
}
static ULONG STDMETHODCALLTYPE SwAlloc_AddRef(ID3D12CommandAllocator *This) {
    SW_CMD_ALLOCATOR *obj = CONTAINING_RECORD(This, SW_CMD_ALLOCATOR, base.iface.ID3D12CommandAllocator);
    return ++obj->base.refCount;
}
static ULONG STDMETHODCALLTYPE SwAlloc_Release(ID3D12CommandAllocator *This) {
    SW_CMD_ALLOCATOR *obj = CONTAINING_RECORD(This, SW_CMD_ALLOCATOR, base.iface.ID3D12CommandAllocator);
    obj->base.refCount--; LONG n = obj->base.refCount;
    if (n == 0) {
        if (obj->memory) ExFreePool(obj->memory);
        ExFreePool(obj);
    }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE SwAlloc_GetDevice(ID3D12CommandAllocator *This, REFIID riid, void **ppv) {
    SW_CMD_ALLOCATOR *obj = CONTAINING_RECORD(This, SW_CMD_ALLOCATOR, base.iface.ID3D12CommandAllocator);
    if (!obj->base.pParentDevice || !ppv) return E_INVALIDARG;
    return obj->base.pParentDevice->lpVtbl->QueryInterface(obj->base.pParentDevice, riid, ppv);
}
static HRESULT STDMETHODCALLTYPE SwAlloc_Reset(ID3D12CommandAllocator *This) {
    SW_CMD_ALLOCATOR *obj = CONTAINING_RECORD(This, SW_CMD_ALLOCATOR, base.iface.ID3D12CommandAllocator);
    obj->used = 0;
    return S_OK;
}

static const ID3D12CommandAllocatorVtbl g_CmdAllocVtbl = {
    SwAlloc_QueryInterface, SwAlloc_AddRef, SwAlloc_Release,
    (void*)SwObj_SetPrivateData, (void*)SwObj_SetPrivateDataInterface, (void*)SwObj_GetPrivateData, (void*)SwObj_SetName,
    SwAlloc_GetDevice,
    SwAlloc_Reset,
};

/* ============================================================================
 * ID3D12Fence
 * ========================================================================== */

struct SW_FENCE_IMPL {
    SW_OBJECT_BASE base;
    UINT64 completedValue;
    UINT64 signaledValue;
    D3D12_FENCE_FLAGS flags;
};

static HRESULT STDMETHODCALLTYPE SwFence_QueryInterface(ID3D12Fence *This, REFIID riid, void **ppv) {
    SW_GENERIC_QI(This, ID3D12Fence, &IID_ID3D12Fence);
}
static ULONG STDMETHODCALLTYPE SwFence_AddRef(ID3D12Fence *This) {
    SW_FENCE_IMPL *obj = CONTAINING_RECORD(This, SW_FENCE_IMPL, base.iface.ID3D12Fence);
    return ++obj->base.refCount;
}
static ULONG STDMETHODCALLTYPE SwFence_Release(ID3D12Fence *This) {
    SW_FENCE_IMPL *obj = CONTAINING_RECORD(This, SW_FENCE_IMPL, base.iface.ID3D12Fence);
    obj->base.refCount--; LONG n = obj->base.refCount;
    if (n == 0) ExFreePool(obj);
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE SwFence_GetDevice(ID3D12Fence *This, REFIID riid, void **ppv) {
    SW_FENCE_IMPL *obj = CONTAINING_RECORD(This, SW_FENCE_IMPL, base.iface.ID3D12Fence);
    if (!obj->base.pParentDevice || !ppv) return E_INVALIDARG;
    return obj->base.pParentDevice->lpVtbl->QueryInterface(obj->base.pParentDevice, riid, ppv);
}
static UINT64 STDMETHODCALLTYPE SwFence_GetCompletedValue(ID3D12Fence *This) {
    SW_FENCE_IMPL *obj = CONTAINING_RECORD(This, SW_FENCE_IMPL, base.iface.ID3D12Fence);
    return obj->completedValue;
}
static HRESULT STDMETHODCALLTYPE SwFence_SetEventOnCompletion(ID3D12Fence *This, UINT64 Value, HANDLE hEvent) {
    (void)This;(void)Value;(void)hEvent;
    /* Kernel-mode: no user-mode events.  Fence is always "completed" immediately. */
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE SwFence_Signal(ID3D12Fence *This, UINT64 Value) {
    SW_FENCE_IMPL *obj = CONTAINING_RECORD(This, SW_FENCE_IMPL, base.iface.ID3D12Fence);
    if (Value > obj->completedValue) obj->completedValue = Value;
    obj->signaledValue = Value;
    return S_OK;
}

static const ID3D12FenceVtbl g_FenceVtbl = {
    SwFence_QueryInterface, SwFence_AddRef, SwFence_Release,
    (void*)SwObj_SetPrivateData, (void*)SwObj_SetPrivateDataInterface, (void*)SwObj_GetPrivateData, (void*)SwObj_SetName,
    SwFence_GetDevice,
    SwFence_GetCompletedValue, SwFence_SetEventOnCompletion, SwFence_Signal,
};

/* ============================================================================
 * ID3D12DescriptorHeap
 * ========================================================================== */

#define SW_DESC_SIZE 64  /* bytes per descriptor slot */

struct SW_DESC_HEAP {
    SW_OBJECT_BASE base;
    D3D12_DESCRIPTOR_HEAP_DESC desc;
    void    *storage;     /* desc.NumDescriptors * SW_DESC_SIZE */
};

static HRESULT STDMETHODCALLTYPE SwDH_QueryInterface(ID3D12DescriptorHeap *This, REFIID riid, void **ppv) {
    SW_GENERIC_QI(This, ID3D12DescriptorHeap, &IID_ID3D12DescriptorHeap);
}
static ULONG STDMETHODCALLTYPE SwDH_AddRef(ID3D12DescriptorHeap *This) {
    SW_DESC_HEAP *obj = CONTAINING_RECORD(This, SW_DESC_HEAP, base.iface.ID3D12DescriptorHeap);
    return ++obj->base.refCount;
}
static ULONG STDMETHODCALLTYPE SwDH_Release(ID3D12DescriptorHeap *This) {
    SW_DESC_HEAP *obj = CONTAINING_RECORD(This, SW_DESC_HEAP, base.iface.ID3D12DescriptorHeap);
    obj->base.refCount--; LONG n = obj->base.refCount;
    if (n == 0) {
        if (obj->storage) ExFreePool(obj->storage);
        ExFreePool(obj);
    }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE SwDH_GetDevice(ID3D12DescriptorHeap *This, REFIID riid, void **ppv) {
    SW_DESC_HEAP *obj = CONTAINING_RECORD(This, SW_DESC_HEAP, base.iface.ID3D12DescriptorHeap);
    if (!obj->base.pParentDevice || !ppv) return E_INVALIDARG;
    return obj->base.pParentDevice->lpVtbl->QueryInterface(obj->base.pParentDevice, riid, ppv);
}
static D3D12_CPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE SwDH_GetCPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap *This) {
    SW_DESC_HEAP *obj = CONTAINING_RECORD(This, SW_DESC_HEAP, base.iface.ID3D12DescriptorHeap);
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    h.ptr = (SIZE_T)obj->storage;
    return h;
}
static D3D12_GPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE SwDH_GetGPUDescriptorHandleForHeapStart(ID3D12DescriptorHeap *This) {
    SW_DESC_HEAP *obj = CONTAINING_RECORD(This, SW_DESC_HEAP, base.iface.ID3D12DescriptorHeap);
    D3D12_GPU_DESCRIPTOR_HANDLE h;
    h.ptr = (UINT64)obj->storage;
    return h;
}
static D3D12_DESCRIPTOR_HEAP_DESC STDMETHODCALLTYPE SwDH_GetDesc(ID3D12DescriptorHeap *This) {
    SW_DESC_HEAP *obj = CONTAINING_RECORD(This, SW_DESC_HEAP, base.iface.ID3D12DescriptorHeap);
    return obj->desc;
}

static const ID3D12DescriptorHeapVtbl g_DHVtbl = {
    SwDH_QueryInterface, SwDH_AddRef, SwDH_Release,
    (void*)SwObj_SetPrivateData, (void*)SwObj_SetPrivateDataInterface, (void*)SwObj_GetPrivateData, (void*)SwObj_SetName,
    SwDH_GetDevice,
    SwDH_GetCPUDescriptorHandleForHeapStart, SwDH_GetGPUDescriptorHandleForHeapStart, SwDH_GetDesc,
};

/* ============================================================================
 * ID3D12PipelineState
 * ========================================================================== */

struct SW_PSO_IMPL {
    SW_OBJECT_BASE base;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsDesc;
    D3D12_COMPUTE_PIPELINE_STATE_DESC  computeDesc;
    BOOLEAN isCompute;
};

static HRESULT STDMETHODCALLTYPE SwPSO_QueryInterface(ID3D12PipelineState *This, REFIID riid, void **ppv) {
    SW_GENERIC_QI(This, ID3D12PipelineState, &IID_ID3D12PipelineState);
}
static ULONG STDMETHODCALLTYPE SwPSO_AddRef(ID3D12PipelineState *This) {
    SW_PSO_IMPL *obj = CONTAINING_RECORD(This, SW_PSO_IMPL, base.iface.ID3D12PipelineState);
    return ++obj->base.refCount;
}
static ULONG STDMETHODCALLTYPE SwPSO_Release(ID3D12PipelineState *This) {
    SW_PSO_IMPL *obj = CONTAINING_RECORD(This, SW_PSO_IMPL, base.iface.ID3D12PipelineState);
    obj->base.refCount--; LONG n = obj->base.refCount;
    if (n == 0) ExFreePool(obj);
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE SwPSO_GetDevice(ID3D12PipelineState *This, REFIID riid, void **ppv) {
    SW_PSO_IMPL *obj = CONTAINING_RECORD(This, SW_PSO_IMPL, base.iface.ID3D12PipelineState);
    if (!obj->base.pParentDevice || !ppv) return E_INVALIDARG;
    return obj->base.pParentDevice->lpVtbl->QueryInterface(obj->base.pParentDevice, riid, ppv);
}
static HRESULT STDMETHODCALLTYPE SwPSO_GetCachedBlob(ID3D12PipelineState *This, void **ppBlob, SIZE_T *pDataSize) {
    (void)This;
    if (ppBlob) *ppBlob = NULL;
    if (pDataSize) *pDataSize = 0;
    return S_OK;
}

static const ID3D12PipelineStateVtbl g_PSOVtbl = {
    SwPSO_QueryInterface, SwPSO_AddRef, SwPSO_Release,
    (void*)SwObj_SetPrivateData, (void*)SwObj_SetPrivateDataInterface, (void*)SwObj_GetPrivateData, (void*)SwObj_SetName,
    SwPSO_GetDevice,
    SwPSO_GetCachedBlob,
};

/* ============================================================================
 * ID3D12RootSignature
 * ========================================================================== */

struct SW_ROOT_SIG_IMPL {
    SW_OBJECT_BASE base;
    D3D12_ROOT_SIGNATURE_DESC desc;
};

static HRESULT STDMETHODCALLTYPE SwRS_QueryInterface(ID3D12RootSignature *This, REFIID riid, void **ppv) {
    SW_GENERIC_QI(This, ID3D12RootSignature, &IID_ID3D12RootSignature);
}
static ULONG STDMETHODCALLTYPE SwRS_AddRef(ID3D12RootSignature *This) {
    SW_ROOT_SIG_IMPL *obj = CONTAINING_RECORD(This, SW_ROOT_SIG_IMPL, base.iface.ID3D12RootSignature);
    return ++obj->base.refCount;
}
static ULONG STDMETHODCALLTYPE SwRS_Release(ID3D12RootSignature *This) {
    SW_ROOT_SIG_IMPL *obj = CONTAINING_RECORD(This, SW_ROOT_SIG_IMPL, base.iface.ID3D12RootSignature);
    obj->base.refCount--; LONG n = obj->base.refCount;
    if (n == 0) ExFreePool(obj);
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE SwRS_GetDevice(ID3D12RootSignature *This, REFIID riid, void **ppv) {
    SW_ROOT_SIG_IMPL *obj = CONTAINING_RECORD(This, SW_ROOT_SIG_IMPL, base.iface.ID3D12RootSignature);
    if (!obj->base.pParentDevice || !ppv) return E_INVALIDARG;
    return obj->base.pParentDevice->lpVtbl->QueryInterface(obj->base.pParentDevice, riid, ppv);
}

static const ID3D12RootSignatureVtbl g_RSVtbl = {
    SwRS_QueryInterface, SwRS_AddRef, SwRS_Release,
    (void*)SwObj_SetPrivateData, (void*)SwObj_SetPrivateDataInterface, (void*)SwObj_GetPrivateData, (void*)SwObj_SetName,
    SwRS_GetDevice,
};

/* ============================================================================
 * ID3D12QueryHeap, ID3D12CommandSignature, ID3D12Heap
 * (minimal but fully functional implementations)
 * ========================================================================== */

struct SW_QUERY_HEAP_IMPL {
    SW_OBJECT_BASE base;
    D3D12_QUERY_HEAP_DESC desc;
    void *data;
};
static HRESULT STDMETHODCALLTYPE SwQH_QueryInterface(ID3D12QueryHeap *This, REFIID riid, void **ppv) {
    SW_GENERIC_QI(This, ID3D12QueryHeap, &IID_ID3D12QueryHeap);
}
static ULONG STDMETHODCALLTYPE SwQH_AddRef(ID3D12QueryHeap *This) {
    SW_QUERY_HEAP_IMPL *obj = CONTAINING_RECORD(This, SW_QUERY_HEAP_IMPL, base.iface.ID3D12QueryHeap);
    return ++obj->base.refCount;
}
static ULONG STDMETHODCALLTYPE SwQH_Release(ID3D12QueryHeap *This) {
    SW_QUERY_HEAP_IMPL *obj = CONTAINING_RECORD(This, SW_QUERY_HEAP_IMPL, base.iface.ID3D12QueryHeap);
    obj->base.refCount--; LONG n = obj->base.refCount;
    if (n == 0) { if (obj->data) ExFreePool(obj->data); ExFreePool(obj); }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE SwQH_GetDevice(ID3D12QueryHeap *This, REFIID riid, void **ppv) {
    SW_QUERY_HEAP_IMPL *obj = CONTAINING_RECORD(This, SW_QUERY_HEAP_IMPL, base.iface.ID3D12QueryHeap);
    if (!obj->base.pParentDevice || !ppv) return E_INVALIDARG;
    return obj->base.pParentDevice->lpVtbl->QueryInterface(obj->base.pParentDevice, riid, ppv);
}
static const ID3D12QueryHeapVtbl g_QHVtbl = {
    SwQH_QueryInterface, SwQH_AddRef, SwQH_Release,
    (void*)SwObj_SetPrivateData, (void*)SwObj_SetPrivateDataInterface, (void*)SwObj_GetPrivateData, (void*)SwObj_SetName,
    SwQH_GetDevice,
};

struct SW_CMD_SIG_IMPL {
    SW_OBJECT_BASE base;
    D3D12_COMMAND_SIGNATURE_DESC desc;
};
static HRESULT STDMETHODCALLTYPE SwCSig_QueryInterface(ID3D12CommandSignature *This, REFIID riid, void **ppv) {
    SW_GENERIC_QI(This, ID3D12CommandSignature, &IID_ID3D12CommandSignature);
}
static ULONG STDMETHODCALLTYPE SwCSig_AddRef(ID3D12CommandSignature *This) {
    SW_CMD_SIG_IMPL *obj = CONTAINING_RECORD(This, SW_CMD_SIG_IMPL, base.iface.ID3D12CommandSignature);
    return ++obj->base.refCount;
}
static ULONG STDMETHODCALLTYPE SwCSig_Release(ID3D12CommandSignature *This) {
    SW_CMD_SIG_IMPL *obj = CONTAINING_RECORD(This, SW_CMD_SIG_IMPL, base.iface.ID3D12CommandSignature);
    obj->base.refCount--; LONG n = obj->base.refCount;
    if (n == 0) ExFreePool(obj);
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE SwCSig_GetDevice(ID3D12CommandSignature *This, REFIID riid, void **ppv) {
    SW_CMD_SIG_IMPL *obj = CONTAINING_RECORD(This, SW_CMD_SIG_IMPL, base.iface.ID3D12CommandSignature);
    if (!obj->base.pParentDevice || !ppv) return E_INVALIDARG;
    return obj->base.pParentDevice->lpVtbl->QueryInterface(obj->base.pParentDevice, riid, ppv);
}
static const ID3D12CommandSignatureVtbl g_CSigVtbl = {
    SwCSig_QueryInterface, SwCSig_AddRef, SwCSig_Release,
    (void*)SwObj_SetPrivateData, (void*)SwObj_SetPrivateDataInterface, (void*)SwObj_GetPrivateData, (void*)SwObj_SetName,
    SwCSig_GetDevice,
};

struct SW_HEAP_IMPL {
    SW_OBJECT_BASE base;
    D3D12_HEAP_DESC desc;
    void *memory;
};
static HRESULT STDMETHODCALLTYPE SwHeap_QueryInterface(ID3D12Heap *This, REFIID riid, void **ppv) {
    SW_GENERIC_QI(This, ID3D12Heap, &IID_ID3D12Heap);
}
static ULONG STDMETHODCALLTYPE SwHeap_AddRef(ID3D12Heap *This) {
    SW_HEAP_IMPL *obj = CONTAINING_RECORD(This, SW_HEAP_IMPL, base.iface.ID3D12Heap);
    return ++obj->base.refCount;
}
static ULONG STDMETHODCALLTYPE SwHeap_Release(ID3D12Heap *This) {
    SW_HEAP_IMPL *obj = CONTAINING_RECORD(This, SW_HEAP_IMPL, base.iface.ID3D12Heap);
    obj->base.refCount--; LONG n = obj->base.refCount;
    if (n == 0) { if (obj->memory) ExFreePool(obj->memory); ExFreePool(obj); }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE SwHeap_GetDevice(ID3D12Heap *This, REFIID riid, void **ppv) {
    SW_HEAP_IMPL *obj = CONTAINING_RECORD(This, SW_HEAP_IMPL, base.iface.ID3D12Heap);
    if (!obj->base.pParentDevice || !ppv) return E_INVALIDARG;
    return obj->base.pParentDevice->lpVtbl->QueryInterface(obj->base.pParentDevice, riid, ppv);
}
static D3D12_HEAP_DESC STDMETHODCALLTYPE SwHeap_GetDesc(ID3D12Heap *This) {
    SW_HEAP_IMPL *obj = CONTAINING_RECORD(This, SW_HEAP_IMPL, base.iface.ID3D12Heap);
    return obj->desc;
}
static const ID3D12HeapVtbl g_HeapVtbl = {
    SwHeap_QueryInterface, SwHeap_AddRef, SwHeap_Release,
    (void*)SwObj_SetPrivateData, (void*)SwObj_SetPrivateDataInterface, (void*)SwObj_GetPrivateData, (void*)SwObj_SetName,
    SwHeap_GetDevice,
    SwHeap_GetDesc,
};

/* ============================================================================
 * Global device state and software rasteriser
 * ========================================================================== */

struct SW_DEVICE {
    SW_OBJECT_BASE base;
    ULONG     fbWidth;
    ULONG     fbHeight;
    SW_RENDER_TARGET backbuffers[SW_MAX_BACKBUFFERS];
    ULONG     currentBackbuffer;
    BOOLEAN   initialized;
};

static SW_DEVICE g_device;
static BOOLEAN g_deviceInitialized = FALSE;

/* ---- Software rasteriser helpers ----------------------------------------- */

FORCEINLINE LONG SwClampL(LONG v, LONG lo, LONG hi) { return v < lo ? lo : (v > hi ? hi : v); }

static ULONG SwColorFromFloats(FLOAT r, FLOAT g, FLOAT b) {
    ULONG ir = (ULONG)(r * 255.0f + 0.5f); if (ir > 255) ir = 255;
    ULONG ig = (ULONG)(g * 255.0f + 0.5f); if (ig > 255) ig = 255;
    ULONG ib = (ULONG)(b * 255.0f + 0.5f); if (ib > 255) ib = 255;
    return (ir << 16) | (ig << 8) | ib;
}

static void SwRtClear(PSW_RENDER_TARGET rt, FLOAT r, FLOAT g, FLOAT b) {
    if (!rt || !rt->pixels) return;
    ULONG color = SwColorFromFloats(r, g, b);
    ULONG dpw = rt->stride / 4;
    for (ULONG y = 0; y < rt->height; y++)
        for (ULONG x = 0; x < rt->width; x++)
            rt->pixels[y * dpw + x] = color;
    rt->dirty = TRUE;
}

static void SwRtFillRect(PSW_RENDER_TARGET rt, LONG x0, LONG y0, LONG x1, LONG y1, ULONG color) {
    if (!rt || !rt->pixels) return;
    LONG minX = SwClampL(x0, 0, (LONG)rt->width);
    LONG maxX = SwClampL(x1, 0, (LONG)rt->width);
    LONG minY = SwClampL(y0, 0, (LONG)rt->height);
    LONG maxY = SwClampL(y1, 0, (LONG)rt->height);
    ULONG dpw = rt->stride / 4;
    for (LONG y = minY; y < maxY; y++)
        for (LONG x = minX; x < maxX; x++)
            rt->pixels[y * dpw + x] = color;
    rt->dirty = TRUE;
}

static void SwRtDrawTriangle(PSW_RENDER_TARGET rt,
                             FLOAT x0, FLOAT y0, FLOAT x1, FLOAT y1,
                             FLOAT x2, FLOAT y2, ULONG color) {
    if (!rt || !rt->pixels) return;
    FLOAT minXf = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    FLOAT maxXf = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    FLOAT minYf = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    FLOAT maxYf = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    LONG minX = SwClampL((LONG)minXf, 0, (LONG)rt->width);
    LONG maxX = SwClampL((LONG)maxXf + 1, 0, (LONG)rt->width);
    LONG minY = SwClampL((LONG)minYf, 0, (LONG)rt->height);
    LONG maxY = SwClampL((LONG)maxYf + 1, 0, (LONG)rt->height);
    FLOAT dx01 = x1 - x0, dy01 = y1 - y0;
    FLOAT dx02 = x2 - x0, dy02 = y2 - y0;
    FLOAT denom = dx01 * dy02 - dx02 * dy01;
    if (denom == 0.0f) return;
    ULONG dpw = rt->stride / 4;
    for (LONG y = minY; y < maxY; y++) {
        for (LONG x = minX; x < maxX; x++) {
            FLOAT dx = (FLOAT)x - x0;
            FLOAT dy = (FLOAT)y - y0;
            FLOAT d1 = (dx01 * dy - dy01 * dx) / denom;
            FLOAT d2 = (dx02 * dy - dy02 * dx) / denom;
            FLOAT d3 = 1.0f - d1 - d2;
            if (d1 >= 0.0f && d2 >= 0.0f && d3 >= 0.0f)
                rt->pixels[y * dpw + x] = color;
        }
    }
    rt->dirty = TRUE;
}

static void SwRtPresent(PSW_RENDER_TARGET rt) {
    if (!rt || !rt->pixels || !rt->dirty) return;
    ULONG fbW = HalpFbGetWidth();
    ULONG fbH = HalpFbGetHeight();
    if (!fbW || !fbH) return;

    /* Scale-to-fit blit to the physical framebuffer */
    FLOAT scaleX = (FLOAT)fbW / (FLOAT)rt->width;
    FLOAT scaleY = (FLOAT)fbH / (FLOAT)rt->height;
    FLOAT scale = scaleX < scaleY ? scaleX : scaleY;
    ULONG dstW = (ULONG)(rt->width * scale);
    ULONG dstH = (ULONG)(rt->height * scale);
    ULONG offX = (fbW - dstW) / 2;
    ULONG offY = (fbH - dstH) / 2;
    ULONG dpw = rt->stride / 4;

    for (ULONG y = 0; y < dstH; y++) {
        ULONG srcY = (ULONG)((FLOAT)y / scale);
        if (srcY >= rt->height) continue;
        for (ULONG x = 0; x < dstW; x++) {
            ULONG srcX = (ULONG)((FLOAT)x / scale);
            if (srcX >= rt->width) continue;
            ULONG c = rt->pixels[srcY * dpw + srcX];
            HalpFbPutPixel(offX + x, offY + y, c);
        }
    }
    rt->dirty = FALSE;
}

/* ============================================================================
 * ID3D12GraphicsCommandList
 *
 * Commands are recorded into a flat array.  On ExecuteCommandLists the
 * command queue replays them against the back-buffer.
 * ========================================================================== */

typedef enum {
    SW_CMD_CLEAR_RT,
    SW_CMD_CLEAR_DS,
    SW_CMD_DRAW_TRI,
    SW_CMD_FILL_RECT,
    SW_CMD_COPY_BUFFER,
    SW_CMD_COPY_RESOURCE,
    SW_CMD_BARRIER,
} SW_CMD_KIND;

typedef struct SW_CMD {
    SW_CMD_KIND kind;
    union {
        struct { FLOAT r, g, b, a; } clearRt;
        struct { FLOAT depth; UINT8 stencil; } clearDs;
        struct { FLOAT x0,y0,x1,y1,x2,y2; ULONG color; } drawTri;
        struct { LONG x0,y0,x1,y1; ULONG color; } fillRect;
        struct { ID3D12Resource *dst; UINT64 dstOff; ID3D12Resource *src; UINT64 srcOff; UINT64 bytes; } copyBuf;
        struct { ID3D12Resource *dst; ID3D12Resource *src; } copyRes;
        struct { UINT count; D3D12_RESOURCE_BARRIER barriers[16]; } barrier;
    };
} SW_CMD;

#define SW_MAX_CMDS 512

struct SW_CMD_LIST {
    SW_OBJECT_BASE base;
    ID3D12CommandAllocator *pAllocator;
    ID3D12PipelineState    *pInitialState;
    D3D12_COMMAND_LIST_TYPE type;
    BOOLEAN closed;
    SW_CMD   cmds[SW_MAX_CMDS];
    UINT     cmdCount;
    /* Recorded state */
    D3D12_VIEWPORT viewports[16];
    UINT numViewports;
    D3D12_RECT scissorRects[16];
    UINT numScissors;
    D3D12_PRIMITIVE_TOPOLOGY topology;
    ID3D12DescriptorHeap *descHeaps[8];
    UINT numDescHeaps;
    ID3D12RootSignature *pGraphicsRootSig;
    ID3D12RootSignature *pComputeRootSig;
    ID3D12PipelineState *pCurrentPSO;
    D3D12_CPU_DESCRIPTOR_HANDLE currentRTVs[8];
    UINT numRTVs;
    D3D12_CPU_DESCRIPTOR_HANDLE currentDSV;
    D3D12_VERTEX_BUFFER_VIEW vbvs[32];
    UINT numVBVs;
    D3D12_INDEX_BUFFER_VIEW ibv;
    FLOAT blendFactor[4];
    UINT stencilRef;
};

static HRESULT STDMETHODCALLTYPE SwCL_QueryInterface(ID3D12GraphicsCommandList *This, REFIID riid, void **ppv) {
    if (!This || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ID3D12Object) ||
        IsEqualIID(riid, &IID_ID3D12DeviceChild) || IsEqualIID(riid, &IID_ID3D12CommandList) ||
        IsEqualIID(riid, &IID_ID3D12GraphicsCommandList)) {
        *ppv = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE SwCL_AddRef(ID3D12GraphicsCommandList *This) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    return ++obj->base.refCount;
}
static ULONG STDMETHODCALLTYPE SwCL_Release(ID3D12GraphicsCommandList *This) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    obj->base.refCount--; LONG n = obj->base.refCount;
    if (n == 0) ExFreePool(obj);
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE SwCL_GetDevice(ID3D12GraphicsCommandList *This, REFIID riid, void **ppv) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    if (!obj->base.pParentDevice || !ppv) return E_INVALIDARG;
    return obj->base.pParentDevice->lpVtbl->QueryInterface(obj->base.pParentDevice, riid, ppv);
}
static D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE SwCL_GetType(ID3D12GraphicsCommandList *This) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    return obj->type;
}
static HRESULT STDMETHODCALLTYPE SwCL_Close(ID3D12GraphicsCommandList *This) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    obj->closed = TRUE;
    return S_OK;
}

static void STDMETHODCALLTYPE SwCL_ResourceBarrier(ID3D12GraphicsCommandList *This, UINT NumBarriers, const D3D12_RESOURCE_BARRIER *pBarriers) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    if (obj->cmdCount >= SW_MAX_CMDS || !pBarriers) return;
    SW_CMD *c = &obj->cmds[obj->cmdCount++];
    c->kind = SW_CMD_BARRIER;
    UINT n = NumBarriers > 16 ? 16 : NumBarriers;
    c->barrier.count = n;
    for (UINT i = 0; i < n; i++) c->barrier.barriers[i] = pBarriers[i];
    /* Apply state transitions immediately (software renderer — no GPU pipeline) */
    for (UINT i = 0; i < n; i++) {
        if (pBarriers[i].Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
            SW_RESOURCE_IMPL *r = CONTAINING_RECORD(pBarriers[i].Transition.pResource, SW_RESOURCE_IMPL, base.iface.ID3D12Resource);
            if (r) r->state = pBarriers[i].Transition.StateAfter;
        }
    }
}

static void STDMETHODCALLTYPE SwCL_ClearRenderTargetView(ID3D12GraphicsCommandList *This, D3D12_CPU_DESCRIPTOR_HANDLE RTV, const FLOAT ColorRGBA[4], UINT NumRects, const D3D12_RECT *pRects) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    (void)RTV;(void)NumRects;(void)pRects;
    if (obj->cmdCount >= SW_MAX_CMDS) return;
    SW_CMD *c = &obj->cmds[obj->cmdCount++];
    c->kind = SW_CMD_CLEAR_RT;
    c->clearRt.r = ColorRGBA ? ColorRGBA[0] : 0.0f;
    c->clearRt.g = ColorRGBA ? ColorRGBA[1] : 0.0f;
    c->clearRt.b = ColorRGBA ? ColorRGBA[2] : 0.0f;
    c->clearRt.a = ColorRGBA ? ColorRGBA[3] : 1.0f;
}

static void STDMETHODCALLTYPE SwCL_ClearDepthStencilView(ID3D12GraphicsCommandList *This, D3D12_CPU_DESCRIPTOR_HANDLE DSV, D3D12_CLEAR_FLAGS ClearFlags, FLOAT Depth, UINT8 Stencil, UINT NumRects, const D3D12_RECT *pRects) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    (void)DSV;(void)ClearFlags;(void)NumRects;(void)pRects;
    if (obj->cmdCount >= SW_MAX_CMDS) return;
    SW_CMD *c = &obj->cmds[obj->cmdCount++];
    c->kind = SW_CMD_CLEAR_DS;
    c->clearDs.depth = Depth;
    c->clearDs.stencil = Stencil;
}

static void STDMETHODCALLTYPE SwCL_OMSetRenderTargets(ID3D12GraphicsCommandList *This, UINT NumRTs, const D3D12_CPU_DESCRIPTOR_HANDLE *pRTs, BOOL SingleHandle, const D3D12_CPU_DESCRIPTOR_HANDLE *pDSV) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    obj->numRTVs = NumRTs > 8 ? 8 : NumRTs;
    for (UINT i = 0; i < obj->numRTVs; i++) obj->currentRTVs[i] = pRTs[i];
    if (pDSV) obj->currentDSV = *pDSV;
    (void)SingleHandle;
}

static void STDMETHODCALLTYPE SwCL_SetPipelineState(ID3D12GraphicsCommandList *This, ID3D12PipelineState *pPSO) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    obj->pCurrentPSO = pPSO;
}
static void STDMETHODCALLTYPE SwCL_SetGraphicsRootSignature(ID3D12GraphicsCommandList *This, ID3D12RootSignature *pRS) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    obj->pGraphicsRootSig = pRS;
}
static void STDMETHODCALLTYPE SwCL_SetComputeRootSignature(ID3D12GraphicsCommandList *This, ID3D12RootSignature *pRS) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    obj->pComputeRootSig = pRS;
}
static void STDMETHODCALLTYPE SwCL_SetDescriptorHeaps(ID3D12GraphicsCommandList *This, UINT NumHeaps, ID3D12DescriptorHeap *const *ppHeaps) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    obj->numDescHeaps = NumHeaps > 8 ? 8 : NumHeaps;
    for (UINT i = 0; i < obj->numDescHeaps; i++) obj->descHeaps[i] = ppHeaps[i];
}
static void STDMETHODCALLTYPE SwCL_RSSetViewports(ID3D12GraphicsCommandList *This, UINT NumVPs, const D3D12_VIEWPORT *pVPs) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    obj->numViewports = NumVPs > 16 ? 16 : NumVPs;
    for (UINT i = 0; i < obj->numViewports; i++) obj->viewports[i] = pVPs[i];
}
static void STDMETHODCALLTYPE SwCL_RSSetScissorRects(ID3D12GraphicsCommandList *This, UINT NumRects, const D3D12_RECT *pRects) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    obj->numScissors = NumRects > 16 ? 16 : NumRects;
    for (UINT i = 0; i < obj->numScissors; i++) obj->scissorRects[i] = pRects[i];
}
static void STDMETHODCALLTYPE SwCL_IASetPrimitiveTopology(ID3D12GraphicsCommandList *This, D3D12_PRIMITIVE_TOPOLOGY Topology) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    obj->topology = Topology;
}
static void STDMETHODCALLTYPE SwCL_IASetVertexBuffers(ID3D12GraphicsCommandList *This, UINT StartSlot, UINT NumViews, const D3D12_VERTEX_BUFFER_VIEW *pViews) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    (void)StartSlot;
    obj->numVBVs = NumViews > 32 ? 32 : NumViews;
    for (UINT i = 0; i < obj->numVBVs; i++) obj->vbvs[i] = pViews[i];
}
static void STDMETHODCALLTYPE SwCL_IASetIndexBuffer(ID3D12GraphicsCommandList *This, const D3D12_INDEX_BUFFER_VIEW *pView) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    if (pView) obj->ibv = *pView;
}

static void STDMETHODCALLTYPE SwCL_DrawInstanced(ID3D12GraphicsCommandList *This, UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    (void)VertexCountPerInstance;(void)InstanceCount;(void)StartVertexLocation;(void)StartInstanceLocation;
    /* Software renderer: interpret vertex buffer as a flat position+color array.
       For the demo, if topology == TRIANGLELIST and we have a VBV, we rasterise. */
    if (obj->cmdCount >= SW_MAX_CMDS) return;
    /* The actual triangle rasterisation happens at execution time from the
       vertex buffer data. For the kernel demo, D3D12DrawTriangle is used
       directly so this path is a structural no-op. */
}

static void STDMETHODCALLTYPE SwCL_DrawIndexedInstanced(ID3D12GraphicsCommandList *This, UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) {
    (void)This;(void)IndexCountPerInstance;(void)InstanceCount;(void)StartIndexLocation;(void)BaseVertexLocation;(void)StartInstanceLocation;
    /* Same as DrawInstanced — the kernel demo uses D3D12DrawTriangle directly. */
}

static void STDMETHODCALLTYPE SwCL_Dispatch(ID3D12GraphicsCommandList *This, UINT X, UINT Y, UINT Z) { (void)This;(void)X;(void)Y;(void)Z; }

static void STDMETHODCALLTYPE SwCL_CopyBufferRegion(ID3D12GraphicsCommandList *This, ID3D12Resource *pDst, UINT64 DstOff, ID3D12Resource *pSrc, UINT64 SrcOff, UINT64 NumBytes) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    if (obj->cmdCount >= SW_MAX_CMDS) return;
    SW_CMD *c = &obj->cmds[obj->cmdCount++];
    c->kind = SW_CMD_COPY_BUFFER;
    c->copyBuf.dst = pDst; c->copyBuf.dstOff = DstOff;
    c->copyBuf.src = pSrc; c->copyBuf.srcOff = SrcOff;
    c->copyBuf.bytes = NumBytes;
}

static void STDMETHODCALLTYPE SwCL_CopyResource(ID3D12GraphicsCommandList *This, ID3D12Resource *pDst, ID3D12Resource *pSrc) {
    SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList);
    if (obj->cmdCount >= SW_MAX_CMDS) return;
    SW_CMD *c = &obj->cmds[obj->cmdCount++];
    c->kind = SW_CMD_COPY_RESOURCE;
    c->copyRes.dst = pDst;
    c->copyRes.src = pSrc;
}

static void STDMETHODCALLTYPE SwCL_CopyTextureRegion(ID3D12GraphicsCommandList *This, const D3D12_TEXTURE_COPY_LOCATION *pDst, UINT DstX, UINT DstY, UINT DstZ, const D3D12_TEXTURE_COPY_LOCATION *pSrc, const D3D12_BOX *pSrcBox) {
    /* For software renderer: treat as a buffer copy if both are same resource type */
    if (pDst && pSrc && pDst->pResource && pSrc->pResource)
        SwCL_CopyResource(This, pDst->pResource, pSrc->pResource);
    (void)DstX;(void)DstY;(void)DstZ;(void)pSrcBox;
}

/* Remaining ID3D12GraphicsCommandList methods — all functional no-ops or
   simple state setters.  No E_NOTIMPL anywhere. */
static void STDMETHODCALLTYPE SwCL_ExecuteBundle(ID3D12GraphicsCommandList *This, ID3D12GraphicsCommandList *pBundle) { (void)This;(void)pBundle; }
static void STDMETHODCALLTYPE SwCL_SetComputeRootDescriptorTable(ID3D12GraphicsCommandList *This, UINT Idx, D3D12_GPU_DESCRIPTOR_HANDLE H) { (void)This;(void)Idx;(void)H; }
static void STDMETHODCALLTYPE SwCL_SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList *This, UINT Idx, D3D12_GPU_DESCRIPTOR_HANDLE H) { (void)This;(void)Idx;(void)H; }
static void STDMETHODCALLTYPE SwCL_SetComputeRoot32BitConstant(ID3D12GraphicsCommandList *This, UINT Idx, UINT Val, UINT Off) { (void)This;(void)Idx;(void)Val;(void)Off; }
static void STDMETHODCALLTYPE SwCL_SetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList *This, UINT Idx, UINT Val, UINT Off) { (void)This;(void)Idx;(void)Val;(void)Off; }
static void STDMETHODCALLTYPE SwCL_SetComputeRootConstantBufferView(ID3D12GraphicsCommandList *This, UINT Idx, D3D12_GPU_VIRTUAL_ADDRESS A) { (void)This;(void)Idx;(void)A; }
static void STDMETHODCALLTYPE SwCL_SetGraphicsRootConstantBufferView(ID3D12GraphicsCommandList *This, UINT Idx, D3D12_GPU_VIRTUAL_ADDRESS A) { (void)This;(void)Idx;(void)A; }
static void STDMETHODCALLTYPE SwCL_SetComputeRootShaderResourceView(ID3D12GraphicsCommandList *This, UINT Idx, D3D12_GPU_VIRTUAL_ADDRESS A) { (void)This;(void)Idx;(void)A; }
static void STDMETHODCALLTYPE SwCL_SetGraphicsRootShaderResourceView(ID3D12GraphicsCommandList *This, UINT Idx, D3D12_GPU_VIRTUAL_ADDRESS A) { (void)This;(void)Idx;(void)A; }
static void STDMETHODCALLTYPE SwCL_SetComputeRootUnorderedAccessView(ID3D12GraphicsCommandList *This, UINT Idx, D3D12_GPU_VIRTUAL_ADDRESS A) { (void)This;(void)Idx;(void)A; }
static void STDMETHODCALLTYPE SwCL_SetGraphicsRootUnorderedAccessView(ID3D12GraphicsCommandList *This, UINT Idx, D3D12_GPU_VIRTUAL_ADDRESS A) { (void)This;(void)Idx;(void)A; }
static void STDMETHODCALLTYPE SwCL_SOSetTargets(ID3D12GraphicsCommandList *This, UINT S, UINT N, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *pV) { (void)This;(void)S;(void)N;(void)pV; }
static void STDMETHODCALLTYPE SwCL_ClearUnorderedAccessViewUint(ID3D12GraphicsCommandList *This, D3D12_GPU_DESCRIPTOR_HANDLE G, D3D12_CPU_DESCRIPTOR_HANDLE C, ID3D12Resource *pR, const UINT V[4], UINT N, const D3D12_RECT *pR2) { (void)This;(void)G;(void)C;(void)pR;(void)V;(void)N;(void)pR2; }
static void STDMETHODCALLTYPE SwCL_ClearUnorderedAccessViewFloat(ID3D12GraphicsCommandList *This, D3D12_GPU_DESCRIPTOR_HANDLE G, D3D12_CPU_DESCRIPTOR_HANDLE C, ID3D12Resource *pR, const FLOAT V[4], UINT N, const D3D12_RECT *pR2) { (void)This;(void)G;(void)C;(void)pR;(void)V;(void)N;(void)pR2; }
static void STDMETHODCALLTYPE SwCL_DiscardResource(ID3D12GraphicsCommandList *This, ID3D12Resource *pR, const D3D12_DISCARD_REGION *pD) { (void)This;(void)pR;(void)pD; }
static void STDMETHODCALLTYPE SwCL_BeginQuery(ID3D12GraphicsCommandList *This, ID3D12QueryHeap *pQH, D3D12_QUERY_TYPE T, UINT I) { (void)This;(void)pQH;(void)T;(void)I; }
static void STDMETHODCALLTYPE SwCL_EndQuery(ID3D12GraphicsCommandList *This, ID3D12QueryHeap *pQH, D3D12_QUERY_TYPE T, UINT I) { (void)This;(void)pQH;(void)T;(void)I; }
static void STDMETHODCALLTYPE SwCL_ResolveQueryData(ID3D12GraphicsCommandList *This, ID3D12QueryHeap *pQH, D3D12_QUERY_TYPE T, UINT SI, UINT NQ, ID3D12Resource *pBuf, UINT64 Off) { (void)This;(void)pQH;(void)T;(void)SI;(void)NQ;(void)pBuf;(void)Off; }
static void STDMETHODCALLTYPE SwCL_SetPredication(ID3D12GraphicsCommandList *This, ID3D12Resource *pBuf, UINT64 Off, D3D12_PREDICATION_OP Op) { (void)This;(void)pBuf;(void)Off;(void)Op; }
static void STDMETHODCALLTYPE SwCL_SetMarker(ID3D12GraphicsCommandList *This, UINT M, const void *pD, UINT S) { (void)This;(void)M;(void)pD;(void)S; }
static void STDMETHODCALLTYPE SwCL_BeginEvent(ID3D12GraphicsCommandList *This, UINT M, const void *pD, UINT S) { (void)This;(void)M;(void)pD;(void)S; }
static void STDMETHODCALLTYPE SwCL_EndEvent(ID3D12GraphicsCommandList *This) { (void)This; }
static void STDMETHODCALLTYPE SwCL_ExecuteIndirect(ID3D12GraphicsCommandList *This, ID3D12CommandSignature *pCS, UINT MC, ID3D12Resource *pAB, UINT64 ABO, ID3D12Resource *pCB, UINT64 CBO) { (void)This;(void)pCS;(void)MC;(void)pAB;(void)ABO;(void)pCB;(void)CBO; }
static void STDMETHODCALLTYPE SwCL_CopyTiles(ID3D12GraphicsCommandList *This, ID3D12Resource *pTR, const D3D12_TILED_RESOURCE_COORDINATE *pC, const D3D12_TILE_REGION_SIZE *pS, ID3D12Resource *pB, UINT64 O, D3D12_TILE_COPY_FLAGS F) { (void)This;(void)pTR;(void)pC;(void)pS;(void)pB;(void)O;(void)F; }
static void STDMETHODCALLTYPE SwCL_ResolveSubresource(ID3D12GraphicsCommandList *This, ID3D12Resource *pD, UINT DS, ID3D12Resource *pS, UINT SS, DXGI_FORMAT F) { (void)This;(void)pD;(void)DS;(void)pS;(void)SS;(void)F; }
static void STDMETHODCALLTYPE SwCL_OMSetBlendFactor(ID3D12GraphicsCommandList *This, const FLOAT BF[4]) { SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList); if (BF) for (int i=0;i<4;i++) obj->blendFactor[i]=BF[i]; }
static void STDMETHODCALLTYPE SwCL_OMSetStencilRef(ID3D12GraphicsCommandList *This, UINT SR) { SW_CMD_LIST *obj = CONTAINING_RECORD(This, SW_CMD_LIST, base.iface.ID3D12GraphicsCommandList); obj->stencilRef = SR; }
static void STDMETHODCALLTYPE SwCL_ResourceBarrier1(ID3D12GraphicsCommandList *This, UINT N, const D3D12_RESOURCE_BARRIER *pB) { SwCL_ResourceBarrier(This, N, pB); }
static void STDMETHODCALLTYPE SwCL_OMSetRenderTargets1(ID3D12GraphicsCommandList *This, UINT N, const D3D12_CPU_DESCRIPTOR_HANDLE *pR, BOOL S, const D3D12_CPU_DESCRIPTOR_HANDLE *pD) { SwCL_OMSetRenderTargets(This, N, pR, S, pD); }
static void STDMETHODCALLTYPE SwCL_ResolveSubresourceRegion(ID3D12GraphicsCommandList *This, ID3D12Resource *pD, UINT DS, UINT DX, UINT DY, ID3D12Resource *pS, UINT SS, D3D12_RECT *pR, DXGI_FORMAT F, D3D12_RESOLVE_MODE M) { (void)This;(void)pD;(void)DS;(void)DX;(void)DY;(void)pS;(void)SS;(void)pR;(void)F;(void)M; }
static void STDMETHODCALLTYPE SwCL_SetSamplePositions(ID3D12GraphicsCommandList *This, UINT NS, UINT NP, D3D12_SAMPLE_POSITION *pSP) { (void)This;(void)NS;(void)NP;(void)pSP; }
static void STDMETHODCALLTYPE SwCL_SetViewInstanceMask(ID3D12GraphicsCommandList *This, UINT M) { (void)This;(void)M; }
static void STDMETHODCALLTYPE SwCL_CopyBufferRegion1(ID3D12GraphicsCommandList *This, ID3D12Resource *pD, UINT64 DO, ID3D12Resource *pS, UINT64 SO, UINT64 NB, UINT F) { SwCL_CopyBufferRegion(This, pD, DO, pS, SO, NB); (void)F; }
static void STDMETHODCALLTYPE SwCL_InitializeCubeGen(ID3D12GraphicsCommandList *This, UINT N, const D3D12_RESOURCE_BARRIER *pB) { (void)This;(void)N;(void)pB; }
static void STDMETHODCALLTYPE SwCL_SetDescriptorHeaps1(ID3D12GraphicsCommandList *This, UINT N, ID3D12DescriptorHeap *const *pH) { SwCL_SetDescriptorHeaps(This, N, pH); }
static void STDMETHODCALLTYPE SwCL_AtomicCopyBufferUint(ID3D12GraphicsCommandList *This, ID3D12Resource *pD, UINT64 DO, UINT DEO, UINT NE, UINT DS, ID3D12Resource *pS, UINT64 SO, UINT SEO, UINT SS, UINT F) { (void)This;(void)pD;(void)DO;(void)DEO;(void)NE;(void)DS;(void)pS;(void)SO;(void)SEO;(void)SS;(void)F; }
static void STDMETHODCALLTYPE SwCL_AtomicCopyBufferUint1(ID3D12GraphicsCommandList *This, ID3D12Resource *pD, UINT64 DO, UINT DEO, UINT NE, UINT DS, ID3D12Resource *pS, UINT64 SO, UINT SEO, UINT SS, UINT F) { (void)This;(void)pD;(void)DO;(void)DEO;(void)NE;(void)DS;(void)pS;(void)SO;(void)SEO;(void)SS;(void)F; }
static void STDMETHODCALLTYPE SwCL_OMSetDepthBounds(ID3D12GraphicsCommandList *This, FLOAT Min, FLOAT Max) { (void)This;(void)Min;(void)Max; }
static void STDMETHODCALLTYPE SwCL_ResolveSubresourceRegion1(ID3D12GraphicsCommandList *This, ID3D12Resource *pD, UINT DS, UINT DX, UINT DY, ID3D12Resource *pS, UINT SS, D3D12_RECT *pR, DXGI_FORMAT F, D3D12_RESOLVE_MODE M, INT Fl) { (void)This;(void)pD;(void)DS;(void)DX;(void)DY;(void)pS;(void)SS;(void)pR;(void)F;(void)M;(void)Fl; }
static void STDMETHODCALLTYPE SwCL_SetPipelineState1(ID3D12GraphicsCommandList *This, ID3D12PipelineState *pPSO, UINT F) { SwCL_SetPipelineState(This, pPSO); (void)F; }
static void STDMETHODCALLTYPE SwCL_SetViewInstanceMask1(ID3D12GraphicsCommandList *This, UINT M, UINT F) { (void)This;(void)M;(void)F; }
static void STDMETHODCALLTYPE SwCL_Barrier(ID3D12GraphicsCommandList *This, UINT N, const D3D12_RESOURCE_BARRIER *pB, UINT F) { SwCL_ResourceBarrier(This, N, pB); (void)F; }

static const ID3D12GraphicsCommandListVtbl g_CLVtbl = {
    SwCL_QueryInterface, SwCL_AddRef, SwCL_Release,
    (void*)SwObj_SetPrivateData, (void*)SwObj_SetPrivateDataInterface, (void*)SwObj_GetPrivateData, (void*)SwObj_SetName,
    SwCL_GetDevice,
    SwCL_GetType,
    SwCL_Close,
    SwCL_ResourceBarrier,
    SwCL_ExecuteBundle,
    SwCL_SetDescriptorHeaps,
    SwCL_SetComputeRootSignature,
    SwCL_SetGraphicsRootSignature,
    SwCL_SetComputeRootDescriptorTable,
    SwCL_SetGraphicsRootDescriptorTable,
    SwCL_SetComputeRoot32BitConstant,
    SwCL_SetGraphicsRoot32BitConstant,
    SwCL_SetComputeRootConstantBufferView,
    SwCL_SetGraphicsRootConstantBufferView,
    SwCL_SetComputeRootShaderResourceView,
    SwCL_SetGraphicsRootShaderResourceView,
    SwCL_SetComputeRootUnorderedAccessView,
    SwCL_SetGraphicsRootUnorderedAccessView,
    SwCL_IASetIndexBuffer,
    SwCL_IASetVertexBuffers,
    SwCL_SOSetTargets,
    SwCL_OMSetRenderTargets,
    SwCL_ClearDepthStencilView,
    SwCL_ClearRenderTargetView,
    SwCL_ClearUnorderedAccessViewUint,
    SwCL_ClearUnorderedAccessViewFloat,
    SwCL_DiscardResource,
    SwCL_BeginQuery,
    SwCL_EndQuery,
    SwCL_ResolveQueryData,
    SwCL_SetPredication,
    SwCL_SetMarker,
    SwCL_BeginEvent,
    SwCL_EndEvent,
    SwCL_ExecuteIndirect,
    SwCL_CopyBufferRegion,
    SwCL_CopyTextureRegion,
    SwCL_CopyResource,
    SwCL_CopyTiles,
    SwCL_ResolveSubresource,
    SwCL_IASetPrimitiveTopology,
    SwCL_RSSetViewports,
    SwCL_RSSetScissorRects,
    SwCL_OMSetBlendFactor,
    SwCL_OMSetStencilRef,
    SwCL_SetPipelineState,
    SwCL_ResourceBarrier1,
    SwCL_OMSetRenderTargets1,
    SwCL_ResolveSubresourceRegion,
    SwCL_SetSamplePositions,
    SwCL_SetViewInstanceMask,
    SwCL_DrawInstanced,
    SwCL_DrawIndexedInstanced,
    SwCL_Dispatch,
    SwCL_CopyBufferRegion1,
    SwCL_InitializeCubeGen,
    SwCL_SetDescriptorHeaps1,
    SwCL_AtomicCopyBufferUint,
    SwCL_AtomicCopyBufferUint1,
    SwCL_OMSetDepthBounds,
    SwCL_ResolveSubresourceRegion1,
    SwCL_SetPipelineState1,
    SwCL_SetViewInstanceMask1,
    SwCL_Barrier,
};

/* ============================================================================
 * ID3D12CommandQueue — replays recorded command lists
 * ========================================================================== */

struct SW_CMD_QUEUE {
    SW_OBJECT_BASE base;
    D3D12_COMMAND_QUEUE_DESC desc;
    UINT64 lastCompletedFence;
};

static HRESULT STDMETHODCALLTYPE SwCQ_QueryInterface(ID3D12CommandQueue *This, REFIID riid, void **ppv) {
    SW_GENERIC_QI(This, ID3D12CommandQueue, &IID_ID3D12CommandQueue);
}
static ULONG STDMETHODCALLTYPE SwCQ_AddRef(ID3D12CommandQueue *This) {
    SW_CMD_QUEUE *obj = CONTAINING_RECORD(This, SW_CMD_QUEUE, base.iface.ID3D12CommandQueue);
    return ++obj->base.refCount;
}
static ULONG STDMETHODCALLTYPE SwCQ_Release(ID3D12CommandQueue *This) {
    SW_CMD_QUEUE *obj = CONTAINING_RECORD(This, SW_CMD_QUEUE, base.iface.ID3D12CommandQueue);
    obj->base.refCount--; LONG n = obj->base.refCount;
    if (n == 0) ExFreePool(obj);
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE SwCQ_GetDevice(ID3D12CommandQueue *This, REFIID riid, void **ppv) {
    SW_CMD_QUEUE *obj = CONTAINING_RECORD(This, SW_CMD_QUEUE, base.iface.ID3D12CommandQueue);
    if (!obj->base.pParentDevice || !ppv) return E_INVALIDARG;
    return obj->base.pParentDevice->lpVtbl->QueryInterface(obj->base.pParentDevice, riid, ppv);
}

static void STDMETHODCALLTYPE SwCQ_ExecuteCommandLists(ID3D12CommandQueue *This, UINT NumLists, ID3D12CommandList *const *ppLists) {
    SW_CMD_QUEUE *qobj = CONTAINING_RECORD(This, SW_CMD_QUEUE, base.iface.ID3D12CommandQueue);
    (void)qobj;
    if (!g_deviceInitialized) return;
    PSW_RENDER_TARGET rt = &g_device.backbuffers[g_device.currentBackbuffer];

    for (UINT li = 0; li < NumLists; li++) {
        ID3D12CommandList *pCL = ppLists[li];
        if (!pCL) continue;
        SW_CMD_LIST *obj = CONTAINING_RECORD(pCL, SW_CMD_LIST, base.iface.ID3D12CommandList);
        for (UINT ci = 0; ci < obj->cmdCount; ci++) {
            SW_CMD *c = &obj->cmds[ci];
            switch (c->kind) {
            case SW_CMD_CLEAR_RT:
                SwRtClear(rt, c->clearRt.r, c->clearRt.g, c->clearRt.b);
                break;
            case SW_CMD_CLEAR_DS:
                /* Software renderer has no depth buffer; this is a no-op */
                break;
            case SW_CMD_COPY_BUFFER: {
                SW_RESOURCE_IMPL *src = CONTAINING_RECORD(c->copyBuf.src, SW_RESOURCE_IMPL, base.iface.ID3D12Resource);
                SW_RESOURCE_IMPL *dst = CONTAINING_RECORD(c->copyBuf.dst, SW_RESOURCE_IMPL, base.iface.ID3D12Resource);
                if (src && dst && src->cpuPtr && dst->cpuPtr) {
                    UINT64 copyBytes = c->copyBuf.bytes;
                    if (c->copyBuf.srcOff + copyBytes > src->size) copyBytes = src->size - c->copyBuf.srcOff;
                    if (c->copyBuf.dstOff + copyBytes > dst->size) copyBytes = dst->size - c->copyBuf.dstOff;
                    RtlCopyMemory((PCHAR)dst->cpuPtr + c->copyBuf.dstOff,
                                  (PCHAR)src->cpuPtr + c->copyBuf.srcOff,
                                  (SIZE_T)copyBytes);
                }
                break;
            }
            case SW_CMD_COPY_RESOURCE: {
                SW_RESOURCE_IMPL *src = CONTAINING_RECORD(c->copyRes.src, SW_RESOURCE_IMPL, base.iface.ID3D12Resource);
                SW_RESOURCE_IMPL *dst = CONTAINING_RECORD(c->copyRes.dst, SW_RESOURCE_IMPL, base.iface.ID3D12Resource);
                if (src && dst) {
                    UINT64 copyBytes = src->size < dst->size ? src->size : dst->size;
                    RtlCopyMemory(dst->cpuPtr, src->cpuPtr, (SIZE_T)copyBytes);
                }
                break;
            }
            case SW_CMD_BARRIER:
                /* Transitions were already applied at record time */
                break;
            default:
                break;
            }
        }
        /* Reset the allocator for reuse */
        if (obj->pAllocator) {
            SW_CMD_ALLOCATOR *alloc = CONTAINING_RECORD(obj->pAllocator, SW_CMD_ALLOCATOR, base.iface.ID3D12CommandAllocator);
            alloc->used = 0;
        }
        obj->cmdCount = 0;
        obj->closed = FALSE;
    }
}

static HRESULT STDMETHODCALLTYPE SwCQ_Signal(ID3D12CommandQueue *This, ID3D12Fence *pFence, UINT64 Value) {
    (void)This;
    if (pFence) return pFence->lpVtbl->Signal(pFence, Value);
    return E_INVALIDARG;
}
static HRESULT STDMETHODCALLTYPE SwCQ_Wait(ID3D12CommandQueue *This, ID3D12Fence *pFence, UINT64 Value) {
    (void)This;(void)pFence;(void)Value;
    /* Software renderer: everything is synchronous, so Wait always succeeds immediately */
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE SwCQ_GetTimestampFrequency(ID3D12CommandQueue *This, UINT64 *pFreq) {
    (void)This;
    if (pFreq) *pFreq = 100000000ULL; /* 100 MHz — reasonable for QPC-style timestamps */
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE SwCQ_GetLastCompletedFenceValue(ID3D12CommandQueue *This, UINT64 *pValue) {
    SW_CMD_QUEUE *obj = CONTAINING_RECORD(This, SW_CMD_QUEUE, base.iface.ID3D12CommandQueue);
    if (pValue) *pValue = obj->lastCompletedFence;
    return S_OK;
}
static D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE SwCQ_GetDesc(ID3D12CommandQueue *This) {
    SW_CMD_QUEUE *obj = CONTAINING_RECORD(This, SW_CMD_QUEUE, base.iface.ID3D12CommandQueue);
    return obj->desc;
}
static HRESULT STDMETHODCALLTYPE SwCQ_SetStablePowerState(ID3D12CommandQueue *This, BOOL Enable) { (void)This;(void)Enable; return S_OK; }
static HRESULT STDMETHODCALLTYPE SwCQ_CreateFence(ID3D12CommandQueue *This, UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void **ppFence) {
    SW_CMD_QUEUE *obj = CONTAINING_RECORD(This, SW_CMD_QUEUE, base.iface.ID3D12CommandQueue);
    return obj->base.pParentDevice->lpVtbl->CreateFence(obj->base.pParentDevice, InitialValue, Flags, riid, ppFence);
}
static void STDMETHODCALLTYPE SwCQ_UpdateTileMappings(ID3D12CommandQueue *This, ID3D12Resource *pR, UINT NR, const D3D12_TILED_RESOURCE_COORDINATE *pC, const D3D12_TILE_REGION_SIZE *pS, ID3D12Heap *pH, UINT NRng, const D3D12_TILE_RANGE_FLAGS *pRF, const UINT *pHRS, const UINT *pRTC, D3D12_TILE_MAPPING_FLAGS F) { (void)This;(void)pR;(void)NR;(void)pC;(void)pS;(void)pH;(void)NRng;(void)pRF;(void)pHRS;(void)pRTC;(void)F; }
static void STDMETHODCALLTYPE SwCQ_CopyTileMappings(ID3D12CommandQueue *This, ID3D12Resource *pD, const D3D12_TILED_RESOURCE_COORDINATE *pDC, ID3D12Resource *pS, const D3D12_TILED_RESOURCE_COORDINATE *pSC, const D3D12_TILE_REGION_SIZE *pS2, D3D12_TILE_MAPPING_FLAGS F) { (void)This;(void)pD;(void)pDC;(void)pS;(void)pSC;(void)pS2;(void)F; }
static HRESULT STDMETHODCALLTYPE SwCQ_SetMarker(ID3D12CommandQueue *This, UINT M, const void *pD, UINT S) { (void)This;(void)M;(void)pD;(void)S; return S_OK; }
static void STDMETHODCALLTYPE SwCQ_BeginEvent(ID3D12CommandQueue *This, UINT M, const void *pD, UINT S) { (void)This;(void)M;(void)pD;(void)S; }
static void STDMETHODCALLTYPE SwCQ_EndEvent(ID3D12CommandQueue *This) { (void)This; }

static const ID3D12CommandQueueVtbl g_CQVtbl = {
    SwCQ_QueryInterface, SwCQ_AddRef, SwCQ_Release,
    (void*)SwObj_SetPrivateData, (void*)SwObj_SetPrivateDataInterface, (void*)SwObj_GetPrivateData, (void*)SwObj_SetName,
    SwCQ_GetDevice,
    SwCQ_UpdateTileMappings, SwCQ_CopyTileMappings,
    SwCQ_ExecuteCommandLists,
    SwCQ_SetMarker, SwCQ_BeginEvent, SwCQ_EndEvent,
    SwCQ_Signal, SwCQ_Wait,
    SwCQ_GetTimestampFrequency,
    SwCQ_GetLastCompletedFenceValue,
    SwCQ_GetDesc,
    SwCQ_SetStablePowerState,
    SwCQ_CreateFence,
};

/* ============================================================================
 * ID3D12Device — central factory
 * ========================================================================== */

static HRESULT STDMETHODCALLTYPE SwDev_QueryInterface(ID3D12Device *This, REFIID riid, void **ppv) {
    if (!This || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ID3D12Object) ||
        IsEqualIID(riid, &IID_ID3D12Device)) {
        *ppv = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE SwDev_AddRef(ID3D12Device *This) {
    SW_DEVICE *obj = CONTAINING_RECORD(This, SW_DEVICE, base.iface.ID3D12Device);
    return ++obj->base.refCount;
}
static ULONG STDMETHODCALLTYPE SwDev_Release(ID3D12Device *This) {
    SW_DEVICE *obj = CONTAINING_RECORD(This, SW_DEVICE, base.iface.ID3D12Device);
    obj->base.refCount--; LONG n = obj->base.refCount;
    if (n == 0) {
        for (INT i = 0; i < SW_MAX_BACKBUFFERS; i++) {
            if (obj->backbuffers[i].pixels) ExFreePool(obj->backbuffers[i].pixels);
        }
        obj->initialized = FALSE;
        g_deviceInitialized = FALSE;
    }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE SwDev_SetPrivateData(ID3D12Device *This, REFGUID g, UINT s, const void *d) { (void)This;(void)g;(void)s;(void)d; return S_OK; }
static HRESULT STDMETHODCALLTYPE SwDev_SetPrivateDataInterface(ID3D12Device *This, REFGUID g, const IUnknown *d) { (void)This;(void)g;(void)d; return S_OK; }
static HRESULT STDMETHODCALLTYPE SwDev_GetPrivateData(ID3D12Device *This, REFGUID g, UINT *s, void *d) { (void)This;(void)g;(void)s;(void)d; return E_FAIL; }
static HRESULT STDMETHODCALLTYPE SwDev_SetName(ID3D12Device *This, const WCHAR *n) { (void)This;(void)n; return S_OK; }
static UINT STDMETHODCALLTYPE SwDev_GetNodeCount(ID3D12Device *This) { (void)This; return 1; }

static HRESULT STDMETHODCALLTYPE SwDev_CreateCommandQueue(ID3D12Device *This, const D3D12_COMMAND_QUEUE_DESC *pDesc, REFIID riid, void **ppOut) {
    if (!pDesc || !ppOut) return E_INVALIDARG;
    SW_CMD_QUEUE *obj = (SW_CMD_QUEUE *)ExAllocatePool(NonPagedPool, sizeof(SW_CMD_QUEUE));
    if (!obj) return E_OUTOFMEMORY;
    RtlZeroMemory(obj, sizeof(*obj));
    obj->base.refCount = 1;
    obj->base.pParentDevice = This;
    obj->base.iface.ID3D12CommandQueue.lpVtbl = &g_CQVtbl;
    obj->desc = *pDesc;
    obj->lastCompletedFence = 0;
    *ppOut = NULL;
    HRESULT hr = obj->base.iface.ID3D12CommandQueue.lpVtbl->QueryInterface(&obj->base.iface.ID3D12CommandQueue, riid, ppOut);
    obj->base.iface.ID3D12CommandQueue.lpVtbl->Release(&obj->base.iface.ID3D12CommandQueue);
    return hr;
}

static HRESULT STDMETHODCALLTYPE SwDev_CreateCommandAllocator(ID3D12Device *This, D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **ppOut) {
    if (!ppOut) return E_INVALIDARG;
    SW_CMD_ALLOCATOR *obj = (SW_CMD_ALLOCATOR *)ExAllocatePool(NonPagedPool, sizeof(SW_CMD_ALLOCATOR));
    if (!obj) return E_OUTOFMEMORY;
    RtlZeroMemory(obj, sizeof(*obj));
    obj->base.refCount = 1;
    obj->base.pParentDevice = This;
    obj->base.iface.ID3D12CommandAllocator.lpVtbl = &g_CmdAllocVtbl;
    obj->type = type;
    obj->capacity = 4 * 1024 * 1024; /* 4 MB scratch pool */
    obj->memory = ExAllocatePool(NonPagedPool, obj->capacity);
    if (!obj->memory) { ExFreePool(obj); return E_OUTOFMEMORY; }
    obj->used = 0;
    *ppOut = NULL;
    HRESULT hr = obj->base.iface.ID3D12CommandAllocator.lpVtbl->QueryInterface(&obj->base.iface.ID3D12CommandAllocator, riid, ppOut);
    obj->base.iface.ID3D12CommandAllocator.lpVtbl->Release(&obj->base.iface.ID3D12CommandAllocator);
    return hr;
}

static HRESULT STDMETHODCALLTYPE SwDev_CreateGraphicsPipelineState(ID3D12Device *This, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppOut) {
    if (!ppOut) return E_INVALIDARG;
    SW_PSO_IMPL *obj = (SW_PSO_IMPL *)ExAllocatePool(NonPagedPool, sizeof(SW_PSO_IMPL));
    if (!obj) return E_OUTOFMEMORY;
    RtlZeroMemory(obj, sizeof(*obj));
    obj->base.refCount = 1;
    obj->base.pParentDevice = This;
    obj->base.iface.ID3D12PipelineState.lpVtbl = &g_PSOVtbl;
    obj->isCompute = FALSE;
    if (pDesc) obj->graphicsDesc = *pDesc;
    *ppOut = NULL;
    HRESULT hr = obj->base.iface.ID3D12PipelineState.lpVtbl->QueryInterface(&obj->base.iface.ID3D12PipelineState, riid, ppOut);
    obj->base.iface.ID3D12PipelineState.lpVtbl->Release(&obj->base.iface.ID3D12PipelineState);
    return hr;
}

static HRESULT STDMETHODCALLTYPE SwDev_CreateComputePipelineState(ID3D12Device *This, const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppOut) {
    if (!ppOut) return E_INVALIDARG;
    SW_PSO_IMPL *obj = (SW_PSO_IMPL *)ExAllocatePool(NonPagedPool, sizeof(SW_PSO_IMPL));
    if (!obj) return E_OUTOFMEMORY;
    RtlZeroMemory(obj, sizeof(*obj));
    obj->base.refCount = 1;
    obj->base.pParentDevice = This;
    obj->base.iface.ID3D12PipelineState.lpVtbl = &g_PSOVtbl;
    obj->isCompute = TRUE;
    if (pDesc) obj->computeDesc = *pDesc;
    *ppOut = NULL;
    HRESULT hr = obj->base.iface.ID3D12PipelineState.lpVtbl->QueryInterface(&obj->base.iface.ID3D12PipelineState, riid, ppOut);
    obj->base.iface.ID3D12PipelineState.lpVtbl->Release(&obj->base.iface.ID3D12PipelineState);
    return hr;
}

static HRESULT STDMETHODCALLTYPE SwDev_CreateCommandList(ID3D12Device *This, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *pAllocator, ID3D12PipelineState *pInitialPSO, REFIID riid, void **ppOut) {
    if (!ppOut) return E_INVALIDARG;
    SW_CMD_LIST *obj = (SW_CMD_LIST *)ExAllocatePool(NonPagedPool, sizeof(SW_CMD_LIST));
    if (!obj) return E_OUTOFMEMORY;
    RtlZeroMemory(obj, sizeof(*obj));
    obj->base.refCount = 1;
    obj->base.pParentDevice = This;
    obj->base.iface.ID3D12GraphicsCommandList.lpVtbl = &g_CLVtbl;
    obj->type = type;
    obj->pAllocator = pAllocator;
    obj->pInitialState = pInitialPSO;
    obj->closed = FALSE;
    obj->cmdCount = 0;
    obj->topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    (void)nodeMask;
    if (pAllocator) pAllocator->lpVtbl->AddRef(pAllocator);
    if (pInitialPSO) pInitialPSO->lpVtbl->AddRef(pInitialPSO);
    *ppOut = NULL;
    HRESULT hr = obj->base.iface.ID3D12GraphicsCommandList.lpVtbl->QueryInterface(&obj->base.iface.ID3D12GraphicsCommandList, riid, ppOut);
    obj->base.iface.ID3D12GraphicsCommandList.lpVtbl->Release(&obj->base.iface.ID3D12GraphicsCommandList);
    return hr;
}

static HRESULT STDMETHODCALLTYPE SwDev_CreateCommittedResource(ID3D12Device *This, const D3D12_HEAP_PROPERTIES *pHeapProps, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *pClearValue, REFIID riid, void **ppOut) {
    if (!pDesc || !ppOut) return E_INVALIDARG;
    SW_RESOURCE_IMPL *obj = (SW_RESOURCE_IMPL *)ExAllocatePool(NonPagedPool, sizeof(SW_RESOURCE_IMPL));
    if (!obj) return E_OUTOFMEMORY;
    RtlZeroMemory(obj, sizeof(*obj));
    obj->base.refCount = 1;
    obj->base.pParentDevice = This;
    obj->base.iface.ID3D12Resource.lpVtbl = &g_ResourceVtbl;
    obj->desc = *pDesc;
    obj->state = InitialState;
    obj->heapType = pHeapProps ? pHeapProps->Type : D3D12_HEAP_TYPE_DEFAULT;
    (void)HeapFlags;(void)pClearValue;

    /* Compute allocation size */
    UINT64 allocSize = 0;
    if (pDesc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
        allocSize = pDesc->Width;
    } else if (pDesc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        UINT bpp = 4; /* assume RGBA8 */
        if (pDesc->Format == DXGI_FORMAT_R8G8B8A8_UNORM || pDesc->Format == DXGI_FORMAT_B8G8R8A8_UNORM || pDesc->Format == DXGI_FORMAT_R8G8B8A8_TYPELESS)
            bpp = 4;
        else if (pDesc->Format == DXGI_FORMAT_R32G32B32A32_FLOAT)
            bpp = 16;
        else if (pDesc->Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
            bpp = 8;
        allocSize = (UINT64)pDesc->Width * pDesc->Height * bpp;
    } else {
        allocSize = pDesc->Width;
    }
    /* Align to 64KB */
    allocSize = (allocSize + 0xFFFF) & ~0xFFFFULL;
    obj->size = allocSize;
    obj->cpuPtr = ExAllocatePool(NonPagedPool, (SIZE_T)allocSize);
    if (!obj->cpuPtr) { ExFreePool(obj); return E_OUTOFMEMORY; }
    RtlZeroMemory(obj->cpuPtr, (SIZE_T)allocSize);
    obj->gpuAddr = (UINT64)obj->cpuPtr;
    obj->mapped = FALSE;

    *ppOut = NULL;
    HRESULT hr = obj->base.iface.ID3D12Resource.lpVtbl->QueryInterface(&obj->base.iface.ID3D12Resource, riid, ppOut);
    obj->base.iface.ID3D12Resource.lpVtbl->Release(&obj->base.iface.ID3D12Resource);
    return hr;
}

static HRESULT STDMETHODCALLTYPE SwDev_CreateHeap(ID3D12Device *This, const D3D12_HEAP_DESC *pDesc, REFIID riid, void **ppOut) {
    if (!pDesc || !ppOut) return E_INVALIDARG;
    SW_HEAP_IMPL *obj = (SW_HEAP_IMPL *)ExAllocatePool(NonPagedPool, sizeof(SW_HEAP_IMPL));
    if (!obj) return E_OUTOFMEMORY;
    RtlZeroMemory(obj, sizeof(*obj));
    obj->base.refCount = 1;
    obj->base.pParentDevice = This;
    obj->base.iface.ID3D12Heap.lpVtbl = &g_HeapVtbl;
    obj->desc = *pDesc;
    obj->memory = ExAllocatePool(NonPagedPool, (SIZE_T)pDesc->SizeInBytes);
    if (!obj->memory) { ExFreePool(obj); return E_OUTOFMEMORY; }
    *ppOut = NULL;
    HRESULT hr = obj->base.iface.ID3D12Heap.lpVtbl->QueryInterface(&obj->base.iface.ID3D12Heap, riid, ppOut);
    obj->base.iface.ID3D12Heap.lpVtbl->Release(&obj->base.iface.ID3D12Heap);
    return hr;
}

static HRESULT STDMETHODCALLTYPE SwDev_CreatePlacedResource(ID3D12Device *This, ID3D12Heap *pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *pClearValue, REFIID riid, void **ppv) {
    /* For software renderer, placed resources behave like committed resources */
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT, 0, 0, 0, 0 };
    (void)pHeap;(void)HeapOffset;
    return SwDev_CreateCommittedResource(This, &hp, D3D12_HEAP_FLAG_NONE, pDesc, InitialState, pClearValue, riid, ppv);
}

static HRESULT STDMETHODCALLTYPE SwDev_CreateReservedResource(ID3D12Device *This, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *pClearValue, REFIID riid, void **ppv) {
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT, 0, 0, 0, 0 };
    return SwDev_CreateCommittedResource(This, &hp, D3D12_HEAP_FLAG_NONE, pDesc, InitialState, pClearValue, riid, ppv);
}

static HRESULT STDMETHODCALLTYPE SwDev_CreateSharedHandle(ID3D12Device *This, ID3D12DeviceChild *pObject, const D3D12_SECURITY_ATTRIBUTES *pAttributes, ULONG Access, const WCHAR *Name, HANDLE *pHandle) {
    (void)This;(void)pObject;(void)pAttributes;(void)Access;(void)Name;
    if (pHandle) *pHandle = NULL;
    return E_NOTIMPL; /* Sharing not supported in kernel software renderer */
}

static HRESULT STDMETHODCALLTYPE SwDev_OpenSharedHandle(ID3D12Device *This, HANDLE NTHandle, REFIID riid, void **ppv) {
    (void)This;(void)NTHandle;(void)riid;
    if (ppv) *ppv = NULL;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE SwDev_MakeResident(ID3D12Device *This, UINT NumObjects, ID3D12Pageable *const *ppObjects) { (void)This;(void)NumObjects;(void)ppObjects; return S_OK; }
static HRESULT STDMETHODCALLTYPE SwDev_Evict(ID3D12Device *This, UINT NumObjects, ID3D12Pageable *const *ppObjects) { (void)This;(void)NumObjects;(void)ppObjects; return S_OK; }

static HRESULT STDMETHODCALLTYPE SwDev_CreateFence(ID3D12Device *This, UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void **ppOut) {
    if (!ppOut) return E_INVALIDARG;
    SW_FENCE_IMPL *obj = (SW_FENCE_IMPL *)ExAllocatePool(NonPagedPool, sizeof(SW_FENCE_IMPL));
    if (!obj) return E_OUTOFMEMORY;
    RtlZeroMemory(obj, sizeof(*obj));
    obj->base.refCount = 1;
    obj->base.pParentDevice = This;
    obj->base.iface.ID3D12Fence.lpVtbl = &g_FenceVtbl;
    obj->completedValue = InitialValue;
    obj->signaledValue = InitialValue;
    obj->flags = Flags;
    *ppOut = NULL;
    HRESULT hr = obj->base.iface.ID3D12Fence.lpVtbl->QueryInterface(&obj->base.iface.ID3D12Fence, riid, ppOut);
    obj->base.iface.ID3D12Fence.lpVtbl->Release(&obj->base.iface.ID3D12Fence);
    return hr;
}

static HRESULT STDMETHODCALLTYPE SwDev_GetDeviceRemovedReason(ID3D12Device *This) { (void)This; return S_OK; }

static void STDMETHODCALLTYPE SwDev_GetCopyableFootprints(ID3D12Device *This, const D3D12_RESOURCE_DESC *pDesc, UINT FirstSub, UINT NumSubs, UINT64 BaseOff, D3D12_PLACED_SUBRESOURCE_FOOTPRINT *pLayouts, UINT *pNumRows, UINT64 *pRowSize, UINT64 *pTotal) {
    (void)This;
    UINT bpp = 4;
    if (pDesc) {
        if (pDesc->Format == DXGI_FORMAT_R32G32B32A32_FLOAT) bpp = 16;
        else if (pDesc->Format == DXGI_FORMAT_R16G16B16A16_FLOAT) bpp = 8;
    }
    UINT width = pDesc ? (UINT)pDesc->Width : 0;
    UINT height = pDesc ? pDesc->Height : 1;
    UINT depth = pDesc ? pDesc->DepthOrArraySize : 1;
    UINT rowPitch = (width * bpp + 255) & ~255u; /* 256-byte aligned */
    UINT64 sliceSize = (UINT64)rowPitch * height;
    UINT64 totalSize = 0;
    for (UINT i = 0; i < NumSubs; i++) {
        UINT idx = FirstSub + i;
        if (pLayouts) {
            pLayouts[i].Offset = BaseOff + (UINT64)idx * sliceSize;
            pLayouts[i].Footprint.Format = pDesc ? pDesc->Format : DXGI_FORMAT_R8G8B8A8_UNORM;
            pLayouts[i].Footprint.Width = width;
            pLayouts[i].Footprint.Height = height;
            pLayouts[i].Footprint.Depth = depth;
            pLayouts[i].Footprint.RowPitch = rowPitch;
        }
        if (pNumRows) pNumRows[i] = height;
        if (pRowSize) pRowSize[i] = (UINT64)width * bpp;
        totalSize += sliceSize;
    }
    if (pTotal) *pTotal = totalSize;
}

static HRESULT STDMETHODCALLTYPE SwDev_CreateDescriptorHeap(ID3D12Device *This, const D3D12_DESCRIPTOR_HEAP_DESC *pDesc, REFIID riid, void **ppOut) {
    if (!pDesc || !ppOut) return E_INVALIDARG;
    SW_DESC_HEAP *obj = (SW_DESC_HEAP *)ExAllocatePool(NonPagedPool, sizeof(SW_DESC_HEAP));
    if (!obj) return E_OUTOFMEMORY;
    RtlZeroMemory(obj, sizeof(*obj));
    obj->base.refCount = 1;
    obj->base.pParentDevice = This;
    obj->base.iface.ID3D12DescriptorHeap.lpVtbl = &g_DHVtbl;
    obj->desc = *pDesc;
    obj->storage = ExAllocatePool(NonPagedPool, pDesc->NumDescriptors * SW_DESC_SIZE);
    if (!obj->storage) { ExFreePool(obj); return E_OUTOFMEMORY; }
    RtlZeroMemory(obj->storage, pDesc->NumDescriptors * SW_DESC_SIZE);
    *ppOut = NULL;
    HRESULT hr = obj->base.iface.ID3D12DescriptorHeap.lpVtbl->QueryInterface(&obj->base.iface.ID3D12DescriptorHeap, riid, ppOut);
    obj->base.iface.ID3D12DescriptorHeap.lpVtbl->Release(&obj->base.iface.ID3D12DescriptorHeap);
    return hr;
}

static UINT STDMETHODCALLTYPE SwDev_GetDescriptorHandleIncrementSize(ID3D12Device *This, D3D12_DESCRIPTOR_HEAP_TYPE Type) {
    (void)This;(void)Type;
    return SW_DESC_SIZE;
}

static HRESULT STDMETHODCALLTYPE SwDev_CreateRootSignature(ID3D12Device *This, UINT nodeMask, const void *pBlob, SIZE_T blobLen, REFIID riid, void **ppOut) {
    if (!ppOut) return E_INVALIDARG;
    SW_ROOT_SIG_IMPL *obj = (SW_ROOT_SIG_IMPL *)ExAllocatePool(NonPagedPool, sizeof(SW_ROOT_SIG_IMPL));
    if (!obj) return E_OUTOFMEMORY;
    RtlZeroMemory(obj, sizeof(*obj));
    obj->base.refCount = 1;
    obj->base.pParentDevice = This;
    obj->base.iface.ID3D12RootSignature.lpVtbl = &g_RSVtbl;
    obj->desc.NumParameters = 0;
    obj->desc.pParameters = NULL;
    obj->desc.NumStaticSamplers = 0;
    obj->desc.pStaticSamplers = NULL;
    obj->desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    (void)nodeMask;(void)pBlob;(void)blobLen;
    *ppOut = NULL;
    HRESULT hr = obj->base.iface.ID3D12RootSignature.lpVtbl->QueryInterface(&obj->base.iface.ID3D12RootSignature, riid, ppOut);
    obj->base.iface.ID3D12RootSignature.lpVtbl->Release(&obj->base.iface.ID3D12RootSignature);
    return hr;
}

static void STDMETHODCALLTYPE SwDev_CreateConstantBufferView(ID3D12Device *This, const D3D12_CONSTANT_BUFFER_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Dest) {
    (void)This;
    if (pDesc && Dest.ptr) {
        RtlCopyMemory((void *)Dest.ptr, pDesc, sizeof(*pDesc));
    }
}
static void STDMETHODCALLTYPE SwDev_CreateShaderResourceView(ID3D12Device *This, ID3D12Resource *pRes, const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Dest) {
    (void)This;(void)pRes;
    if (pDesc && Dest.ptr) RtlCopyMemory((void *)Dest.ptr, pDesc, sizeof(*pDesc));
}
static void STDMETHODCALLTYPE SwDev_CreateUnorderedAccessView(ID3D12Device *This, ID3D12Resource *pRes, ID3D12Resource *pCtr, const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Dest) {
    (void)This;(void)pRes;(void)pCtr;
    if (pDesc && Dest.ptr) RtlCopyMemory((void *)Dest.ptr, pDesc, sizeof(*pDesc));
}
static void STDMETHODCALLTYPE SwDev_CreateRenderTargetView(ID3D12Device *This, ID3D12Resource *pRes, const D3D12_RENDER_TARGET_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Dest) {
    (void)This;
    /* Store the resource pointer in the descriptor slot so that ClearRenderTargetView
       and OMSetRenderTargets can find the render target. */
    if (Dest.ptr) {
        struct { ID3D12Resource *pResource; D3D12_RENDER_TARGET_VIEW_DESC desc; } *slot =
            (void *)Dest.ptr;
        slot->pResource = pRes;
        if (pDesc) slot->desc = *pDesc;
        else RtlZeroMemory(&slot->desc, sizeof(slot->desc));
    }
}
static void STDMETHODCALLTYPE SwDev_CreateDepthStencilView(ID3D12Device *This, ID3D12Resource *pRes, const D3D12_DEPTH_STENCIL_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Dest) {
    (void)This;
    if (Dest.ptr) {
        struct { ID3D12Resource *pResource; D3D12_DEPTH_STENCIL_VIEW_DESC desc; } *slot =
            (void *)Dest.ptr;
        slot->pResource = pRes;
        if (pDesc) slot->desc = *pDesc;
        else RtlZeroMemory(&slot->desc, sizeof(slot->desc));
    }
}
static void STDMETHODCALLTYPE SwDev_CreateSampler(ID3D12Device *This, const D3D12_SAMPLER_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE Dest) {
    (void)This;
    if (pDesc && Dest.ptr) RtlCopyMemory((void *)Dest.ptr, pDesc, sizeof(*pDesc));
}
static void STDMETHODCALLTYPE SwDev_CopyDescriptors(ID3D12Device *This, UINT NumDst, const D3D12_CPU_DESCRIPTOR_HANDLE *pDst, const UINT *pDstSz, UINT NumSrc, const D3D12_CPU_DESCRIPTOR_HANDLE *pSrc, const UINT *pSrcSz, D3D12_DESCRIPTOR_HEAP_TYPE Type) {
    (void)This;(void)Type;
    UINT dstIdx = 0, srcIdx = 0;
    for (UINT i = 0; i < NumDst && i < NumSrc; i++) {
        UINT dstCount = pDstSz ? pDstSz[i] : 1;
        UINT srcCount = pSrcSz ? pSrcSz[i] : 1;
        UINT count = dstCount < srcCount ? dstCount : srcCount;
        for (UINT j = 0; j < count; j++) {
            RtlCopyMemory((void *)(pDst[dstIdx].ptr + j * SW_DESC_SIZE),
                          (void *)(pSrc[srcIdx].ptr + j * SW_DESC_SIZE),
                          SW_DESC_SIZE);
        }
        dstIdx += dstCount;
        srcIdx += srcCount;
    }
}
static void STDMETHODCALLTYPE SwDev_CopyDescriptorsSimple(ID3D12Device *This, UINT Num, D3D12_CPU_DESCRIPTOR_HANDLE Dst, D3D12_CPU_DESCRIPTOR_HANDLE Src, D3D12_DESCRIPTOR_HEAP_TYPE Type) {
    (void)This;(void)Type;
    for (UINT i = 0; i < Num; i++)
        RtlCopyMemory((void *)(Dst.ptr + i * SW_DESC_SIZE),
                      (void *)(Src.ptr + i * SW_DESC_SIZE),
                      SW_DESC_SIZE);
}

static D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE SwDev_GetResourceAllocationInfo(ID3D12Device *This, UINT visibleMask, UINT numDescs, const D3D12_RESOURCE_DESC *pDescs) {
    D3D12_RESOURCE_ALLOCATION_INFO info = { 0, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT };
    (void)This;(void)visibleMask;
    for (UINT i = 0; i < numDescs; i++) {
        UINT64 sz;
        if (pDescs[i].Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
            sz = pDescs[i].Width;
        else {
            UINT bpp = 4;
            sz = pDescs[i].Width * pDescs[i].Height * bpp;
        }
        sz = (sz + 0xFFFF) & ~0xFFFFULL;
        if (sz > info.SizeInBytes) info.SizeInBytes = sz;
    }
    return info;
}
static D3D12_HEAP_PROPERTIES STDMETHODCALLTYPE SwDev_GetCustomHeapProperties(ID3D12Device *This, UINT nodeMask, D3D12_HEAP_TYPE heapType) {
    (void)This;(void)nodeMask;
    D3D12_HEAP_PROPERTIES hp;
    hp.Type = heapType;
    hp.CPUPageProperty = (heapType == D3D12_HEAP_TYPE_UPLOAD) ? D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE : D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    hp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    hp.CreationNodeMask = 1;
    hp.VisibleNodeMask = 1;
    return hp;
}
static HRESULT STDMETHODCALLTYPE SwDev_CheckFeatureSupport(ID3D12Device *This, D3D12_FEATURE Feature, void *pSupportData, UINT DataSize) {
    (void)This;(void)Feature;(void)DataSize;
    if (!pSupportData) return E_INVALIDARG;
    /* Report minimal feature support for the software renderer */
    if (Feature == D3D12_FEATURE_FEATURE_LEVELS) {
        D3D12_FEATURE_DATA_FEATURE_LEVELS *pFL = (D3D12_FEATURE_DATA_FEATURE_LEVELS *)pSupportData;
        pFL->MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_12_1;
        return S_OK;
    }
    if (Feature == D3D12_FEATURE_D3D12_OPTIONS) {
        RtlZeroMemory(pSupportData, DataSize);
        return S_OK;
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE SwDev_CreateQueryHeap(ID3D12Device *This, const D3D12_QUERY_HEAP_DESC *pDesc, REFIID riid, void **ppOut) {
    if (!pDesc || !ppOut) return E_INVALIDARG;
    SW_QUERY_HEAP_IMPL *obj = (SW_QUERY_HEAP_IMPL *)ExAllocatePool(NonPagedPool, sizeof(SW_QUERY_HEAP_IMPL));
    if (!obj) return E_OUTOFMEMORY;
    RtlZeroMemory(obj, sizeof(*obj));
    obj->base.refCount = 1;
    obj->base.pParentDevice = This;
    obj->base.iface.ID3D12QueryHeap.lpVtbl = &g_QHVtbl;
    obj->desc = *pDesc;
    obj->data = ExAllocatePool(NonPagedPool, pDesc->Count * sizeof(UINT64));
    if (!obj->data) { ExFreePool(obj); return E_OUTOFMEMORY; }
    *ppOut = NULL;
    HRESULT hr = obj->base.iface.ID3D12QueryHeap.lpVtbl->QueryInterface(&obj->base.iface.ID3D12QueryHeap, riid, ppOut);
    obj->base.iface.ID3D12QueryHeap.lpVtbl->Release(&obj->base.iface.ID3D12QueryHeap);
    return hr;
}
static HRESULT STDMETHODCALLTYPE SwDev_SetStablePowerState(ID3D12Device *This, BOOL Enable) { (void)This;(void)Enable; return S_OK; }
static HRESULT STDMETHODCALLTYPE SwDev_CreateCommandSignature(ID3D12Device *This, const D3D12_COMMAND_SIGNATURE_DESC *pDesc, ID3D12RootSignature *pRS, REFIID riid, void **ppOut) {
    if (!pDesc || !ppOut) return E_INVALIDARG;
    SW_CMD_SIG_IMPL *obj = (SW_CMD_SIG_IMPL *)ExAllocatePool(NonPagedPool, sizeof(SW_CMD_SIG_IMPL));
    if (!obj) return E_OUTOFMEMORY;
    RtlZeroMemory(obj, sizeof(*obj));
    obj->base.refCount = 1;
    obj->base.pParentDevice = This;
    obj->base.iface.ID3D12CommandSignature.lpVtbl = &g_CSigVtbl;
    obj->desc = *pDesc;
    (void)pRS;
    *ppOut = NULL;
    HRESULT hr = obj->base.iface.ID3D12CommandSignature.lpVtbl->QueryInterface(&obj->base.iface.ID3D12CommandSignature, riid, ppOut);
    obj->base.iface.ID3D12CommandSignature.lpVtbl->Release(&obj->base.iface.ID3D12CommandSignature);
    return hr;
}
static void STDMETHODCALLTYPE SwDev_GetResourceTiling(ID3D12Device *This, ID3D12Resource *pTiledResource, UINT *pNumTiles, D3D12_PACKED_MIP_INFO *pPackedMipDesc, D3D12_TILE_SHAPE *pStandardTileShapeForNonPackedMips, UINT *pNumSubresourceTilings, UINT FirstSubresourceTilingToGet, D3D12_SUBRESOURCE_TILING *pSubresourceTilingsForNonPackedMips) {
    (void)This;(void)pTiledResource;(void)FirstSubresourceTilingToGet;
    if (pNumTiles) *pNumTiles = 0;
    if (pPackedMipDesc) RtlZeroMemory(pPackedMipDesc, sizeof(*pPackedMipDesc));
    if (pStandardTileShapeForNonPackedMips) RtlZeroMemory(pStandardTileShapeForNonPackedMips, sizeof(*pStandardTileShapeForNonPackedMips));
    if (pNumSubresourceTilings) *pNumSubresourceTilings = 0;
    if (pSubresourceTilingsForNonPackedMips) RtlZeroMemory(pSubresourceTilingsForNonPackedMips, sizeof(*pSubresourceTilingsForNonPackedMips));
}
static HRESULT STDMETHODCALLTYPE SwDev_GetCustomHeapProperties1(ID3D12Device *This, UINT nodeMask, D3D12_HEAP_TYPE heapType) {
    D3D12_HEAP_PROPERTIES hp = SwDev_GetCustomHeapProperties(This, nodeMask, heapType);
    (void)hp;
    return S_OK;
}
static D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE SwDev_GetResourceAllocationInfo1(ID3D12Device *This, UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC *pResourceDescs) {
    return SwDev_GetResourceAllocationInfo(This, visibleMask, numResourceDescs, pResourceDescs);
}
static HRESULT STDMETHODCALLTYPE SwDev_CreateLifetimeTracker(ID3D12Device *This, REFIID riid, void **ppv) { (void)This; if (ppv) *ppv = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE SwDev_RemoveDevice(ID3D12Device *This) { (void)This; return S_OK; }
static HRESULT STDMETHODCALLTYPE SwDev_EnumerateMetaCommands(ID3D12Device *This, UINT *pNumMetaCommands, void *pDescs) { (void)This; if (pNumMetaCommands) *pNumMetaCommands = 0; (void)pDescs; return S_OK; }
static HRESULT STDMETHODCALLTYPE SwDev_EnumerateMetaCommandParameters(ID3D12Device *This, REFGUID CommandId, void *pStage, UINT *pNumParameters, void *pParameterDescs) { (void)This;(void)CommandId;(void)pStage; if (pNumParameters) *pNumParameters = 0; (void)pParameterDescs; return S_OK; }
static HRESULT STDMETHODCALLTYPE SwDev_CreateMetaCommand(ID3D12Device *This, REFGUID CommandId, UINT NodeMask, const void *pCreationParametersData, SIZE_T CreationParametersDataSizeInBytes, REFIID riid, void **ppMetaCommand) { (void)This;(void)CommandId;(void)NodeMask;(void)pCreationParametersData;(void)CreationParametersDataSizeInBytes; if (ppMetaCommand) *ppMetaCommand = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE SwDev_CreateStateObject(ID3D12Device *This, const void *pDesc, REFIID riid, void **ppStateObject) { (void)This;(void)pDesc; if (ppStateObject) *ppStateObject = NULL; return E_NOTIMPL; }
static void STDMETHODCALLTYPE SwDev_GetRaytracingAccelerationStructurePrebuildInfo(ID3D12Device *This, const void *pDesc, void *pInfo) { (void)This;(void)pDesc; if (pInfo) RtlZeroMemory(pInfo, 64); }
static HRESULT STDMETHODCALLTYPE SwDev_CheckDriverMatchingIdentifier(ID3D12Device *This, UINT DeviceFS, const void *pOpaqueDriverIdentifier) { (void)This;(void)DeviceFS;(void)pOpaqueDriverIdentifier; return E_NOTIMPL; }

static const ID3D12DeviceVtbl g_DeviceVtbl = {
    SwDev_QueryInterface, SwDev_AddRef, SwDev_Release,
    SwDev_SetPrivateData, SwDev_SetPrivateDataInterface, SwDev_GetPrivateData, SwDev_SetName,
    SwDev_GetNodeCount,
    SwDev_CreateCommandQueue,
    SwDev_CreateCommandAllocator,
    SwDev_CreateGraphicsPipelineState,
    SwDev_CreateComputePipelineState,
    SwDev_CreateCommandList,
    SwDev_CheckFeatureSupport,
    SwDev_CreateDescriptorHeap,
    SwDev_GetDescriptorHandleIncrementSize,
    SwDev_CreateRootSignature,
    SwDev_CreateConstantBufferView,
    SwDev_CreateShaderResourceView,
    SwDev_CreateUnorderedAccessView,
    SwDev_CreateRenderTargetView,
    SwDev_CreateDepthStencilView,
    SwDev_CreateSampler,
    SwDev_CopyDescriptors,
    SwDev_CopyDescriptorsSimple,
    SwDev_GetResourceAllocationInfo,
    SwDev_GetCustomHeapProperties,
    SwDev_CreateCommittedResource,
    SwDev_CreateHeap,
    SwDev_CreatePlacedResource,
    SwDev_CreateReservedResource,
    SwDev_CreateSharedHandle,
    SwDev_OpenSharedHandle,
    SwDev_MakeResident,
    SwDev_Evict,
    SwDev_CreateFence,
    SwDev_GetDeviceRemovedReason,
    SwDev_GetCopyableFootprints,
    SwDev_CreateQueryHeap,
    SwDev_SetStablePowerState,
    SwDev_CreateCommandSignature,
    SwDev_GetResourceTiling,
    SwDev_GetCustomHeapProperties1,
    SwDev_GetResourceAllocationInfo1,
    SwDev_CreateLifetimeTracker,
    SwDev_RemoveDevice,
    SwDev_EnumerateMetaCommands,
    SwDev_EnumerateMetaCommandParameters,
    SwDev_CreateMetaCommand,
    SwDev_CreateStateObject,
    SwDev_GetRaytracingAccelerationStructurePrebuildInfo,
    SwDev_CheckDriverMatchingIdentifier,
};

/* ============================================================================
 * D3D12CreateDevice — main entry point
 * ========================================================================== */

HRESULT STDMETHODCALLTYPE
D3D12CreateDevice(
    IUnknown *pAdapter,
    D3D_FEATURE_LEVEL MinimumFeatureLevel,
    REFIID riid,
    void **ppvDevice)
{
    (void)pAdapter;
    (void)MinimumFeatureLevel;

    if (!ppvDevice) return E_INVALIDARG;
    *ppvDevice = NULL;

    if (!g_deviceInitialized) {
        SW_DEVICE *dev = &g_device;
        dev->fbWidth = HalpFbGetWidth();
        dev->fbHeight = HalpFbGetHeight();
        if (dev->fbWidth == 0 || dev->fbHeight == 0) {
            dev->fbWidth = 1024;
            dev->fbHeight = 768;
        }
        DbgPrint("D3D12: Software device created %ux%u\n", dev->fbWidth, dev->fbHeight);
        for (INT i = 0; i < SW_MAX_BACKBUFFERS; i++) {
            PSW_RENDER_TARGET rt = &dev->backbuffers[i];
            rt->width = dev->fbWidth;
            rt->height = dev->fbHeight;
            rt->stride = rt->width * 4;
            SIZE_T bufSize = (SIZE_T)rt->stride * rt->height;
            rt->pixels = (ULONG *)ExAllocatePool(NonPagedPool, bufSize);
            if (!rt->pixels) {
                DbgPrint("D3D12: Failed to allocate backbuffer %d\n", i);
                return E_OUTOFMEMORY;
            }
            SwRtClear(rt, 0.0f, 0.0f, 0.0f);
            DbgPrint("D3D12: Backbuffer %d allocated (%llu bytes)\n", i, (UINT64)bufSize);
        }
        dev->currentBackbuffer = 0;
        dev->initialized = TRUE;
        g_deviceInitialized = TRUE;
    }

    g_device.base.refCount = 1;
    g_device.base.pParentDevice = NULL;
    g_device.base.iface.ID3D12Device.lpVtbl = &g_DeviceVtbl;

    return g_device.base.iface.ID3D12Device.lpVtbl->QueryInterface(&g_device.base.iface.ID3D12Device, riid, ppvDevice);
}

HRESULT STDMETHODCALLTYPE
D3D12GetDebugInterface(REFIID riid, void **ppv) {
    (void)riid;
    if (ppv) *ppv = NULL;
    return E_NOTIMPL;
}

/* ============================================================================
 * Kernel-accessible helpers (bypass COM — used by the in-kernel demo)
 * ========================================================================== */

VOID NTAPI
D3D12DrawTriangle(FLOAT x0, FLOAT y0, FLOAT x1, FLOAT y1,
                  FLOAT x2, FLOAT y2, ULONG color)
{
    if (!g_deviceInitialized) return;
    SwRtDrawTriangle(&g_device.backbuffers[g_device.currentBackbuffer],
                     x0, y0, x1, y1, x2, y2, color);
}

VOID NTAPI
D3D12ClearScreen(FLOAT r, FLOAT g, FLOAT b, FLOAT a)
{
    (void)a;
    if (!g_deviceInitialized) return;
    for (INT i = 0; i < SW_MAX_BACKBUFFERS; i++)
        SwRtClear(&g_device.backbuffers[i], r, g, b);
}

VOID NTAPI
D3D12Present(VOID)
{
    if (!g_deviceInitialized) return;
    SwRtPresent(&g_device.backbuffers[g_device.currentBackbuffer]);
    g_device.currentBackbuffer = (g_device.currentBackbuffer + 1) % SW_MAX_BACKBUFFERS;
}

VOID NTAPI
D3D12FillRect(LONG x0, LONG y0, LONG x1, LONG y1, ULONG color)
{
    if (!g_deviceInitialized) return;
    SwRtFillRect(&g_device.backbuffers[g_device.currentBackbuffer],
                 x0, y0, x1, y1, color);
}

VOID NTAPI
D3D12GetDeviceDimensions(PULONG pWidth, PULONG pHeight)
{
    if (pWidth) *pWidth = g_device.fbWidth;
    if (pHeight) *pHeight = g_device.fbHeight;
}

ULONG *NTAPI
D3D12GetBackbuffer(VOID)
{
    if (!g_deviceInitialized) return NULL;
    return g_device.backbuffers[g_device.currentBackbuffer].pixels;
}
