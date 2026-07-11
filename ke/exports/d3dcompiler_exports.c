/*
 * MinNT - ke/exports/d3dcompiler_exports.c
 * d3dcompiler_47.dll exports — HLSL shader compiler.
 *
 * In real Windows, this compiles HLSL source bytecode into DXBC.
 * For our software renderer, shaders are interpreted via the kernel
 * D3D12 software pipeline (fixed-function for now). We accept HLSL source,
 * store it as a bytecode blob, and return a blob interface that callers can
 * pass to CreateVertexShader etc.
 *
 * Real compilation to DXBC would require a full HLSL parser + code generator,
 * which is enormous. For D3D12 software rendering with fixed-function
 * rasterization, the shader code doesn't actually execute (the software
 * rasterizer reads vertex buffers directly). The blob is just a wrapper.
 */

#include <nt/ntdef.h>
#include <nt/ex.h>
#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/exe.h>
#include <nt/rtl.h>
#include <ndk/obfuncs.h>
#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif

typedef LONG HRESULT;
#ifndef UINT
typedef unsigned int UINT;
#endif

#define D3DCOMPILE_OK ((HRESULT)0)

/* ID3DBlob — simple blob interface */
typedef struct ID3DBlob ID3DBlob;
typedef struct ID3DBlobVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID3DBlob *This, const void *riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(ID3DBlob *This);
    ULONG   (STDMETHODCALLTYPE *Release)(ID3DBlob *This);
    const void * (STDMETHODCALLTYPE *GetBufferPointer)(ID3DBlob *This);
    SIZE_T      (STDMETHODCALLTYPE *GetBufferSize)(ID3DBlob *This);
} ID3DBlobVtbl;

typedef struct _SW_SHADER_BLOB {
    const ID3DBlobVtbl *lpVtbl;
    LONG refcount;
    PVOID buffer;
    SIZE_T size;
} SW_SHADER_BLOB;

static __attribute__((ms_abi)) HRESULT SwBlob_QI(ID3DBlob *This, const void *riid, void **ppv)
{
    (void)This; (void)riid;
    if (!ppv) return ((HRESULT)0x80004003L);
    *ppv = This;
    return D3DCOMPILE_OK;
}

static __attribute__((ms_abi)) ULONG SwBlob_AddRef(ID3DBlob *This)
{
    SW_SHADER_BLOB *b = (SW_SHADER_BLOB *)This;
    return ++b->refcount;
}

static __attribute__((ms_abi)) ULONG SwBlob_Release(ID3DBlob *This)
{
    SW_SHADER_BLOB *b = (SW_SHADER_BLOB *)This;
    LONG n = --b->refcount;
    if (n == 0) {
        if (b->buffer) ExFreePool(b->buffer);
        ExFreePool(b);
    }
    return n;
}

static __attribute__((ms_abi)) const void *SwBlob_GetBufferPointer(ID3DBlob *This)
{
    SW_SHADER_BLOB *b = (SW_SHADER_BLOB *)This;
    return b->buffer;
}

static __attribute__((ms_abi)) SIZE_T SwBlob_GetBufferSize(ID3DBlob *This)
{
    SW_SHADER_BLOB *b = (SW_SHADER_BLOB *)This;
    return b->size;
}

static const ID3DBlobVtbl g_SWBlobVtbl = {
    SwBlob_QI, SwBlob_AddRef, SwBlob_Release, SwBlob_GetBufferPointer, SwBlob_GetBufferSize
};

static SW_SHADER_BLOB *SwCreateBlob(SIZE_T size)
{
    SW_SHADER_BLOB *b = ExAllocatePool(NonPagedPool, sizeof(SW_SHADER_BLOB));
    if (!b) return NULL;
    b->lpVtbl = &g_SWBlobVtbl;
    b->refcount = 1;
    b->size = size;
    b->buffer = ExAllocatePool(NonPagedPool, size);
    if (!b->buffer) { ExFreePool(b); return NULL; }
    RtlZeroMemory(b->buffer, size);
    return b;
}

/* ============================================================================
 * Exported functions
 * ========================================================================== */

__attribute__((ms_abi))
static HRESULT D3DCompile_msabi(
    const void *pSrcData, SIZE_T SrcDataSize, const char *pSourceName,
    const void *pDefines, const void *pInclude, const char *pEntrypoint,
    const char *pTarget, UINT Flags1, UINT Flags2,
    ID3DBlob **ppCode, ID3DBlob **ppErrorMsgs)
{
    /* For our software renderer, we just store the HLSL source as the "blob".
       The software renderer does fixed-function rasterization, so the shader
       bytecode doesn't actually need to be valid DXBC. */
    (void)pSourceName; (void)pDefines; (void)pInclude; (void)pEntrypoint;
    (void)pTarget; (void)Flags1; (void)Flags2;

    if (!ppCode) return D3DCOMPILE_OK;
    *ppCode = NULL;
    if (ppErrorMsgs) *ppErrorMsgs = NULL;

    if (!pSrcData || SrcDataSize == 0) {
        /* Return error blob */
        if (ppErrorMsgs) {
            const CHAR *err = "D3DCompile: empty source\n";
            SIZE_T len = 0; while (err[len]) len++;
            SW_SHADER_BLOB *b = SwCreateBlob(len);
            if (b) { RtlCopyMemory(b->buffer, err, len); *ppErrorMsgs = (ID3DBlob *)b; }
        }
        return ((HRESULT)0x80004005L); /* E_FAIL */
    }

    /* Create a blob containing the source HLSL (we store it as-is) */
    SW_SHADER_BLOB *b = SwCreateBlob(SrcDataSize);
    if (!b) return ((HRESULT)0x8007000EL);
    RtlCopyMemory(b->buffer, pSrcData, SrcDataSize);
    *ppCode = (ID3DBlob *)b;
    return D3DCOMPILE_OK;
}

__attribute__((ms_abi))
static HRESULT D3DCompile2_msabi(
    const void *pSrcData, SIZE_T SrcDataSize, const char *pSourceName,
    const void *pDefines, const void *pInclude, const char *pEntrypoint,
    const char *pTarget, UINT Flags1, UINT Flags2, UINT Flags3,
    ID3DBlob **ppCode, ID3DBlob **ppErrorMsgs)
{
    (void)Flags3;
    return D3DCompile_msabi(pSrcData, SrcDataSize, pSourceName, pDefines, pInclude,
                              pEntrypoint, pTarget, Flags1, Flags2, ppCode, ppErrorMsgs);
}

__attribute__((ms_abi))
static HRESULT D3DReflect_msabi(
    const void *pSrcData, SIZE_T SrcDataSize, const void *pInterface,
    void **ppReflector)
{
    (void)pSrcData; (void)SrcDataSize; (void)pInterface;
    if (!ppReflector) return ((HRESULT)0x80004003L);
    *ppReflector = NULL;
    /* For our software renderer, return an empty reflector */
    SW_SHADER_BLOB *b = SwCreateBlob(0);
    if (!b) return ((HRESULT)0x8007000EL);
    *ppReflector = b;
    return D3DCOMPILE_OK;
}

__attribute__((ms_abi))
static HRESULT D3DGetInputSignatureBlob_msabi(
    const void *pSrcData, SIZE_T SrcDataSize, ID3DBlob **ppBlob)
{
    (void)pSrcData; (void)SrcDataSize;
    if (!ppBlob) return ((HRESULT)0x80004003L);
    *ppBlob = NULL;
    SW_SHADER_BLOB *b = SwCreateBlob(0);
    if (!b) return ((HRESULT)0x8007000EL);
    *ppBlob = (ID3DBlob *)b;
    return D3DCOMPILE_OK;
}

__attribute__((ms_abi))
static HRESULT D3DGetOutputSignatureBlob_msabi(
    const void *pSrcData, SIZE_T SrcDataSize, ID3DBlob **ppBlob)
{
    return D3DGetInputSignatureBlob_msabi(pSrcData, SrcDataSize, ppBlob);
}

__attribute__((ms_abi))
static HRESULT D3DStripShader_msabi(
    const void *pSrcData, SIZE_T SrcDataSize, UINT uStripFlags, ID3DBlob **ppBlob)
{
    (void)uStripFlags;
    if (!ppBlob) return ((HRESULT)0x80004003L);
    *ppBlob = NULL;
    if (!pSrcData || SrcDataSize == 0) return ((HRESULT)0x80004005L);
    SW_SHADER_BLOB *b = SwCreateBlob(SrcDataSize);
    if (!b) return ((HRESULT)0x8007000EL);
    RtlCopyMemory(b->buffer, pSrcData, SrcDataSize);
    *ppBlob = (ID3DBlob *)b;
    return D3DCOMPILE_OK;
}

__attribute__((ms_abi))
static HRESULT D3DDisassemble_msabi(
    const void *pSrcData, SIZE_T SrcDataSize, UINT Flags, const char *szComments,
    ID3DBlob **ppDisassembly)
{
    (void)Flags; (void)szComments;
    return D3DStripShader_msabi(pSrcData, SrcDataSize, 0, ppDisassembly);
}

/* ============================================================================
 * Registration
 * ========================================================================== */

VOID NTAPI D3dCompilerRegisterExports(VOID)
{
    ExeRegisterExport("d3dcompiler_47.dll", "D3DCompile",                  D3DCompile_msabi);
    ExeRegisterExport("d3dcompiler_47.dll", "D3DCompile2",                 D3DCompile2_msabi);
    ExeRegisterExport("d3dcompiler_47.dll", "D3DReflect",                 D3DReflect_msabi);
    ExeRegisterExport("d3dcompiler_47.dll", "D3DGetInputSignatureBlob",   D3DGetInputSignatureBlob_msabi);
    ExeRegisterExport("d3dcompiler_47.dll", "D3DGetOutputSignatureBlob", D3DGetOutputSignatureBlob_msabi);
    ExeRegisterExport("d3dcompiler_47.dll", "D3DStripShader",              D3DStripShader_msabi);
    ExeRegisterExport("d3dcompiler_47.dll", "D3DDisassemble",              D3DDisassemble_msabi);

    DbgPrint("EXE: d3dcompiler_47.dll exports registered (%lu total)\n", g_ExportCount);
}
