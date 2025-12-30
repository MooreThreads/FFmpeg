/*
 * This copyright notice applies to this header file only:
 *
 * Copyright (c) 2025
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the software, and to permit persons to whom the
 * software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef FFMT_MUSA_DYNLINK_LOADER_H
#define FFMT_MUSA_DYNLINK_LOADER_H

#include <stdlib.h>

#include "dynlink_musa.h"
#include "dynlink_mtmuvid.h"
#include "mtEncodeAPI.h"

#if defined(_WIN32) && (!defined(FFMT_LOAD_FUNC) || !defined(FFMT_SYM_FUNC) || !defined(FFMT_LIB_HANDLE))
# include <windows.h>
#endif

#ifndef FFMT_LIB_HANDLE
# if defined(_WIN32)
#  define FFMT_LIB_HANDLE HMODULE
# else
#  define FFMT_LIB_HANDLE void*
# endif
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
# define MUSA_LIBNAME "mtmusa.dll"
# define MTMUVID_LIBNAME "mtmuvid.dll"
# if defined(_WIN64) || defined(__CYGWIN64__)
#  define MTENC_LIBNAME "mtencodeapi64.dll"
# else
#  define MTENC_LIBNAME "mtencodeapi32.dll"
# endif
#else
# define MUSA_LIBNAME "libmusa.so"
# define MTMUVID_LIBNAME "libvideo_musa.so"
# define MTENC_LIBNAME "libencode_musa.so"
#endif

#if !defined(FFMT_LOAD_FUNC) || !defined(FFMT_SYM_FUNC)
# ifdef _WIN32
#  define FFMT_LOAD_FUNC(path) LoadLibrary(TEXT(path))
#  define FFMT_SYM_FUNC(lib, sym) GetProcAddress((lib), (sym))
#  define FFMT_FREE_FUNC(lib) FreeLibrary(lib)
# else
#  include <dlfcn.h>
#  define FFMT_LOAD_FUNC(path) dlopen((path), RTLD_LAZY)
#  define FFMT_SYM_FUNC(lib, sym) dlsym((lib), (sym))
#  define FFMT_FREE_FUNC(lib) dlclose(lib)
# endif
#endif

#if !defined(FFMT_LOG_FUNC) || !defined(FFMT_DEBUG_LOG_FUNC)
# include <stdio.h>
# define FFMT_LOG_FUNC(logctx, msg, ...) fprintf(stderr, (msg), __VA_ARGS__)
# define FFMT_DEBUG_LOG_FUNC(logctx, msg, ...)
#endif

#define LOAD_LIBRARY(l, path)                                  \
    do {                                                       \
        if (!((l) = FFMT_LOAD_FUNC(path))) {                   \
            FFMT_LOG_FUNC(logctx, "Cannot load %s\n", path);   \
            ret = -1;                                          \
            goto error;                                        \
        }                                                      \
        FFMT_DEBUG_LOG_FUNC(logctx, "Loaded lib: %s\n", path); \
    } while (0)

#define LOAD_SYMBOL(fun, tp, symbol)                             \
    do {                                                         \
        if (!((f->fun) = (tp*)FFMT_SYM_FUNC(f->lib, symbol))) {  \
            FFMT_LOG_FUNC(logctx, "Cannot load %s\n", symbol);   \
            ret = -1;                                            \
            goto error;                                          \
        }                                                        \
        FFMT_DEBUG_LOG_FUNC(logctx, "Loaded sym: %s\n", symbol); \
    } while (0)

#define LOAD_SYMBOL_OPT(fun, tp, symbol)                                      \
    do {                                                                      \
        if (!((f->fun) = (tp*)FFMT_SYM_FUNC(f->lib, symbol))) {               \
            FFMT_DEBUG_LOG_FUNC(logctx, "Cannot load optional %s\n", symbol); \
        } else {                                                              \
            FFMT_DEBUG_LOG_FUNC(logctx, "Loaded sym: %s\n", symbol);          \
        }                                                                     \
    } while (0)

#define GENERIC_LOAD_FUNC_PREAMBLE(T, n, N)     \
    T *f;                                       \
    int ret;                                    \
                                                \
    n##_free_functions(functions);              \
                                                \
    f = *functions = (T*)calloc(1, sizeof(*f)); \
    if (!f)                                     \
        return -1;                              \
                                                \
    LOAD_LIBRARY(f->lib, N);

#define GENERIC_LOAD_FUNC_FINALE(n) \
    return 0;                       \
error:                              \
    n##_free_functions(functions);  \
    return ret;

#define GENERIC_FREE_FUNC()                \
    if (!functions)                        \
        return;                            \
    if (*functions && (*functions)->lib)   \
        FFMT_FREE_FUNC((*functions)->lib); \
    free(*functions);                      \
    *functions = NULL;

#ifdef FFMT_DYNLINK_MUSA_H
typedef struct MusaFunctions {
    tmuInit *muInit;
    tmuDriverGetVersion *muDriverGetVersion;
    tmuDeviceGetCount *muDeviceGetCount;
    tmuDeviceGet *muDeviceGet;
    tmuDeviceGetAttribute *muDeviceGetAttribute;
    tmuDeviceGetName *muDeviceGetName;
    tmuDeviceGetUuid *muDeviceGetUuid;
    tmuDeviceGetUuid_v2 *muDeviceGetUuid_v2;
    tmuDeviceGetLuid *muDeviceGetLuid;
    tmuDeviceGetByPCIBusId *muDeviceGetByPCIBusId;
    tmuDeviceGetPCIBusId *muDeviceGetPCIBusId;
    tmuDeviceComputeCapability *muDeviceComputeCapability;
    tmuCtxCreate_v2 *muCtxCreate;
    tmuCtxGetCurrent *muCtxGetCurrent;
    tmuCtxSetLimit *muCtxSetLimit;
    tmuCtxPushCurrent_v2 *muCtxPushCurrent;
    tmuCtxPopCurrent_v2 *muCtxPopCurrent;
    tmuCtxDestroy_v2 *muCtxDestroy;
    tmuMemAlloc_v2 *muMemAlloc;
    tmuMemAllocPitch_v2 *muMemAllocPitch;
    tmuMemAllocManaged *muMemAllocManaged;
    tmuMemsetD8Async *muMemsetD8Async;
    tmuMemFree_v2 *muMemFree;
    tmuMemcpy *muMemcpy;
    tmuMemcpyAsync *muMemcpyAsync;
    tmuMemcpy2D_v2 *muMemcpy2D;
    tmuMemcpy2DAsync_v2 *muMemcpy2DAsync;
    tmuMemcpyHtoD_v2 *muMemcpyHtoD;
    tmuMemcpyHtoDAsync_v2 *muMemcpyHtoDAsync;
    tmuMemcpyDtoH_v2 *muMemcpyDtoH;
    tmuMemcpyDtoHAsync_v2 *muMemcpyDtoHAsync;
    tmuMemcpyDtoD_v2 *muMemcpyDtoD;
    tmuMemcpyDtoDAsync_v2 *muMemcpyDtoDAsync;
    tmuGetErrorName *muGetErrorName;
    tmuGetErrorString *muGetErrorString;
    tmuCtxGetDevice *muCtxGetDevice;

    tmuDevicePrimaryCtxRetain *muDevicePrimaryCtxRetain;
    tmuDevicePrimaryCtxRelease *muDevicePrimaryCtxRelease;
    tmuDevicePrimaryCtxSetFlags *muDevicePrimaryCtxSetFlags;
    tmuDevicePrimaryCtxGetState *muDevicePrimaryCtxGetState;
    tmuDevicePrimaryCtxReset *muDevicePrimaryCtxReset;

    tmuStreamCreate *muStreamCreate;
    tmuStreamQuery *muStreamQuery;
    tmuStreamSynchronize *muStreamSynchronize;
    tmuStreamDestroy_v2 *muStreamDestroy;
    tmuStreamAddCallback *muStreamAddCallback;
    tmuStreamWaitEvent *muStreamWaitEvent;
    tmuEventCreate *muEventCreate;
    tmuEventDestroy_v2 *muEventDestroy;
    tmuEventSynchronize *muEventSynchronize;
    tmuEventQuery *muEventQuery;
    tmuEventRecord *muEventRecord;

    tmuLaunchKernel *muLaunchKernel;
    tmuLinkCreate *muLinkCreate;
    tmuLinkAddData *muLinkAddData;
    tmuLinkComplete *muLinkComplete;
    tmuLinkDestroy *muLinkDestroy;
    tmuModuleLoadData *muModuleLoadData;
    tmuModuleUnload *muModuleUnload;
    tmuModuleGetFunction *muModuleGetFunction;
    tmuModuleGetGlobal *muModuleGetGlobal;
    tmuTexObjectCreate *muTexObjectCreate;
    tmuTexObjectDestroy *muTexObjectDestroy;

    tmuGLGetDevices_v2 *muGLGetDevices;
    tmuGraphicsGLRegisterImage *muGraphicsGLRegisterImage;
    tmuGraphicsUnregisterResource *muGraphicsUnregisterResource;
    tmuGraphicsMapResources *muGraphicsMapResources;
    tmuGraphicsUnmapResources *muGraphicsUnmapResources;
    tmuGraphicsSubResourceGetMappedArray *muGraphicsSubResourceGetMappedArray;
    tmuGraphicsResourceGetMappedPointer *muGraphicsResourceGetMappedPointer;

    tmuImportExternalMemory *muImportExternalMemory;
    tmuDestroyExternalMemory *muDestroyExternalMemory;
    tmuExternalMemoryGetMappedBuffer *muExternalMemoryGetMappedBuffer;
    tmuExternalMemoryGetMappedMipmappedArray *muExternalMemoryGetMappedMipmappedArray;
    tmuMipmappedArrayDestroy *muMipmappedArrayDestroy;

    tmuMipmappedArrayGetLevel *muMipmappedArrayGetLevel;

    tmuImportExternalSemaphore *muImportExternalSemaphore;
    tmuDestroyExternalSemaphore *muDestroyExternalSemaphore;
    tmuSignalExternalSemaphoresAsync *muSignalExternalSemaphoresAsync;
    tmuWaitExternalSemaphoresAsync *muWaitExternalSemaphoresAsync;

    tmuArrayCreate *muArrayCreate;
    tmuArray3DCreate *muArray3DCreate;
    tmuArrayDestroy *muArrayDestroy;

    tmuEGLStreamProducerConnect *muEGLStreamProducerConnect;
    tmuEGLStreamProducerDisconnect *muEGLStreamProducerDisconnect;
    tmuEGLStreamConsumerDisconnect *muEGLStreamConsumerDisconnect;
    tmuEGLStreamProducerPresentFrame *muEGLStreamProducerPresentFrame;
    tmuEGLStreamProducerReturnFrame *muEGLStreamProducerReturnFrame;

#if defined(_WIN32) || defined(__CYGWIN__)
    tmuD3D11GetDevice *muD3D11GetDevice;
    tmuD3D11GetDevices *muD3D11GetDevices;
    tmuGraphicsD3D11RegisterResource *muGraphicsD3D11RegisterResource;
#endif

    FFMT_LIB_HANDLE lib;
} MusaFunctions;
#else
typedef struct MusaFunctions MusaFunctions;
#endif

typedef struct MuvidFunctions {
    tmuvidGetDecoderCaps *muvidGetDecoderCaps;
    tmuvidCreateDecoder *muvidCreateDecoder;
    tmuvidDestroyDecoder *muvidDestroyDecoder;
    tmuvidDecodePicture *muvidDecodePicture;
    tmuvidGetDecodeStatus *muvidGetDecodeStatus;
    tmuvidReconfigureDecoder *muvidReconfigureDecoder;
    tmuvidMapVideoFrame *muvidMapVideoFrame;
    tmuvidUnmapVideoFrame *muvidUnmapVideoFrame;
    tmuvidCreateVideoParser *muvidCreateVideoParser;
    tmuvidParseVideoData *muvidParseVideoData;
    tmuvidDestroyVideoParser *muvidDestroyVideoParser;

    FFMT_LIB_HANDLE lib;
} MuvidFunctions;

typedef MTENCSTATUS MTENCAPI tMtEncodeAPICreateInstance(MT_ENCODE_API_FUNCTION_LIST *functionList);
typedef MTENCSTATUS MTENCAPI tMtEncodeAPIGetMaxSupportedVersion(uint32_t* version);

typedef struct MtencFunctions {
    tMtEncodeAPICreateInstance *MtEncodeAPICreateInstance;
    tMtEncodeAPIGetMaxSupportedVersion *MtEncodeAPIGetMaxSupportedVersion;

    FFMT_LIB_HANDLE lib;
} MtencFunctions;

#ifdef FFMT_DYNLINK_MUSA_H
static inline void musa_free_functions(MusaFunctions **functions)
{
    GENERIC_FREE_FUNC();
}
#endif

static inline void muvid_free_functions(MuvidFunctions **functions)
{
    GENERIC_FREE_FUNC();
}

static inline void mtenc_free_functions(MtencFunctions **functions)
{
    GENERIC_FREE_FUNC();
}

#ifdef FFMT_DYNLINK_MUSA_H
static inline int musa_load_functions(MusaFunctions **functions, void *logctx)
{
    GENERIC_LOAD_FUNC_PREAMBLE(MusaFunctions, musa, MUSA_LIBNAME);

    LOAD_SYMBOL(muInit, tmuInit, "muInit");
    LOAD_SYMBOL(muDriverGetVersion, tmuDriverGetVersion, "muDriverGetVersion");
    LOAD_SYMBOL(muDeviceGetCount, tmuDeviceGetCount, "muDeviceGetCount");
    LOAD_SYMBOL(muDeviceGet, tmuDeviceGet, "muDeviceGet");
    LOAD_SYMBOL(muDeviceGetAttribute, tmuDeviceGetAttribute, "muDeviceGetAttribute");
    LOAD_SYMBOL(muDeviceGetName, tmuDeviceGetName, "muDeviceGetName");
    LOAD_SYMBOL(muDeviceComputeCapability, tmuDeviceComputeCapability, "muDeviceComputeCapability");
    LOAD_SYMBOL(muCtxCreate, tmuCtxCreate_v2, "muCtxCreate_v2");
    LOAD_SYMBOL(muCtxGetCurrent, tmuCtxGetCurrent, "muCtxGetCurrent");
    LOAD_SYMBOL(muCtxSetLimit, tmuCtxSetLimit, "muCtxSetLimit");
    LOAD_SYMBOL(muCtxPushCurrent, tmuCtxPushCurrent_v2, "muCtxPushCurrent_v2");
    LOAD_SYMBOL(muCtxPopCurrent, tmuCtxPopCurrent_v2, "muCtxPopCurrent_v2");
    LOAD_SYMBOL(muCtxDestroy, tmuCtxDestroy_v2, "muCtxDestroy_v2");
    LOAD_SYMBOL(muMemAlloc, tmuMemAlloc_v2, "muMemAlloc_v2");
    LOAD_SYMBOL(muMemAllocPitch, tmuMemAllocPitch_v2, "muMemAllocPitch_v2");
    LOAD_SYMBOL(muMemAllocManaged, tmuMemAllocManaged, "muMemAllocManaged");
    LOAD_SYMBOL(muMemsetD8Async, tmuMemsetD8Async, "muMemsetD8Async");
    LOAD_SYMBOL(muMemFree, tmuMemFree_v2, "muMemFree_v2");
    LOAD_SYMBOL(muMemcpy, tmuMemcpy, "muMemcpy");
    LOAD_SYMBOL(muMemcpyAsync, tmuMemcpyAsync, "muMemcpyAsync");
    LOAD_SYMBOL(muMemcpy2D, tmuMemcpy2D_v2, "muMemcpy2D_v2");
    LOAD_SYMBOL(muMemcpy2DAsync, tmuMemcpy2DAsync_v2, "muMemcpy2DAsync_v2");
    LOAD_SYMBOL(muMemcpyHtoD, tmuMemcpyHtoD_v2, "muMemcpyHtoD_v2");
    LOAD_SYMBOL(muMemcpyHtoDAsync, tmuMemcpyHtoDAsync_v2, "muMemcpyHtoDAsync_v2");
    LOAD_SYMBOL(muMemcpyDtoH, tmuMemcpyDtoH_v2, "muMemcpyDtoH_v2");
    LOAD_SYMBOL(muMemcpyDtoHAsync, tmuMemcpyDtoHAsync_v2, "muMemcpyDtoHAsync_v2");
    LOAD_SYMBOL(muMemcpyDtoD, tmuMemcpyDtoD_v2, "muMemcpyDtoD_v2");
    LOAD_SYMBOL(muMemcpyDtoDAsync, tmuMemcpyDtoDAsync_v2, "muMemcpyDtoDAsync_v2");
    LOAD_SYMBOL(muGetErrorName, tmuGetErrorName, "muGetErrorName");
    LOAD_SYMBOL(muGetErrorString, tmuGetErrorString, "muGetErrorString");
    LOAD_SYMBOL(muCtxGetDevice, tmuCtxGetDevice, "muCtxGetDevice");

    //LOAD_SYMBOL(muDevicePrimaryCtxRetain, tmuDevicePrimaryCtxRetain, "muDevicePrimaryCtxRetain");
    //LOAD_SYMBOL(muDevicePrimaryCtxRelease, tmuDevicePrimaryCtxRelease, "muDevicePrimaryCtxRelease");
    //LOAD_SYMBOL(muDevicePrimaryCtxSetFlags, tmuDevicePrimaryCtxSetFlags, "muDevicePrimaryCtxSetFlags");
    //LOAD_SYMBOL(muDevicePrimaryCtxGetState, tmuDevicePrimaryCtxGetState, "muDevicePrimaryCtxGetState");
    //LOAD_SYMBOL(muDevicePrimaryCtxReset, tmuDevicePrimaryCtxReset, "muDevicePrimaryCtxReset");

    LOAD_SYMBOL(muStreamCreate, tmuStreamCreate, "muStreamCreate");
    LOAD_SYMBOL(muStreamQuery, tmuStreamQuery, "muStreamQuery");
    LOAD_SYMBOL(muStreamSynchronize, tmuStreamSynchronize, "muStreamSynchronize");
    LOAD_SYMBOL(muStreamDestroy, tmuStreamDestroy_v2, "muStreamDestroy_v2");
    LOAD_SYMBOL(muStreamAddCallback, tmuStreamAddCallback, "muStreamAddCallback");
    LOAD_SYMBOL(muStreamWaitEvent, tmuStreamWaitEvent, "muStreamWaitEvent");
    LOAD_SYMBOL(muEventCreate, tmuEventCreate, "muEventCreate");
    LOAD_SYMBOL(muEventDestroy, tmuEventDestroy_v2, "muEventDestroy_v2");
    LOAD_SYMBOL(muEventSynchronize, tmuEventSynchronize, "muEventSynchronize");
    LOAD_SYMBOL(muEventQuery, tmuEventQuery, "muEventQuery");
    LOAD_SYMBOL(muEventRecord, tmuEventRecord, "muEventRecord");

    LOAD_SYMBOL(muLaunchKernel, tmuLaunchKernel, "muLaunchKernel");
    //LOAD_SYMBOL(muLinkCreate, tmuLinkCreate, "muLinkCreate");
    //LOAD_SYMBOL(muLinkAddData, tmuLinkAddData, "muLinkAddData");
    //LOAD_SYMBOL(muLinkComplete, tmuLinkComplete, "muLinkComplete");
    //LOAD_SYMBOL(muLinkDestroy, tmuLinkDestroy, "muLinkDestroy");
    //LOAD_SYMBOL(muModuleLoadData, tmuModuleLoadData, "muModuleLoadData");
    LOAD_SYMBOL(muModuleUnload, tmuModuleUnload, "muModuleUnload");
    LOAD_SYMBOL(muModuleGetFunction, tmuModuleGetFunction, "muModuleGetFunction");
    //LOAD_SYMBOL(muModuleGetGlobal, tmuModuleGetGlobal, "muModuleGetGlobal");
    LOAD_SYMBOL(muTexObjectCreate, tmuTexObjectCreate, "muTexObjectCreate");
    LOAD_SYMBOL(muTexObjectDestroy, tmuTexObjectDestroy, "muTexObjectDestroy");

    //LOAD_SYMBOL(muGLGetDevices, tmuGLGetDevices_v2, "muGLGetDevices_v2");
    //LOAD_SYMBOL(muGraphicsGLRegisterImage, tmuGraphicsGLRegisterImage, "muGraphicsGLRegisterImage");
    //LOAD_SYMBOL(muGraphicsUnregisterResource, tmuGraphicsUnregisterResource, "muGraphicsUnregisterResource");
    //LOAD_SYMBOL(muGraphicsMapResources, tmuGraphicsMapResources, "muGraphicsMapResources");
    //LOAD_SYMBOL(muGraphicsUnmapResources, tmuGraphicsUnmapResources, "muGraphicsUnmapResources");
    //LOAD_SYMBOL(muGraphicsSubResourceGetMappedArray, tmuGraphicsSubResourceGetMappedArray, "muGraphicsSubResourceGetMappedArray");
    //LOAD_SYMBOL(muGraphicsResourceGetMappedPointer, tmuGraphicsResourceGetMappedPointer, "muGraphicsResourceGetMappedPointer_v2");

    LOAD_SYMBOL_OPT(muDeviceGetUuid, tmuDeviceGetUuid, "muDeviceGetUuid");
    LOAD_SYMBOL_OPT(muDeviceGetUuid_v2, tmuDeviceGetUuid_v2, "muDeviceGetUuid_v2");
    LOAD_SYMBOL_OPT(muDeviceGetLuid, tmuDeviceGetLuid, "muDeviceGetLuid");
    LOAD_SYMBOL_OPT(muDeviceGetByPCIBusId, tmuDeviceGetByPCIBusId, "muDeviceGetByPCIBusId");
    LOAD_SYMBOL_OPT(muDeviceGetPCIBusId, tmuDeviceGetPCIBusId, "muDeviceGetPCIBusId");
    LOAD_SYMBOL_OPT(muImportExternalMemory, tmuImportExternalMemory, "muImportExternalMemory");
    LOAD_SYMBOL_OPT(muDestroyExternalMemory, tmuDestroyExternalMemory, "muDestroyExternalMemory");
    LOAD_SYMBOL_OPT(muExternalMemoryGetMappedBuffer, tmuExternalMemoryGetMappedBuffer, "muExternalMemoryGetMappedBuffer");
    LOAD_SYMBOL_OPT(muExternalMemoryGetMappedMipmappedArray, tmuExternalMemoryGetMappedMipmappedArray, "muExternalMemoryGetMappedMipmappedArray");
    LOAD_SYMBOL_OPT(muMipmappedArrayGetLevel, tmuMipmappedArrayGetLevel, "muMipmappedArrayGetLevel");
    LOAD_SYMBOL_OPT(muMipmappedArrayDestroy, tmuMipmappedArrayDestroy, "muMipmappedArrayDestroy");

    LOAD_SYMBOL_OPT(muImportExternalSemaphore, tmuImportExternalSemaphore, "muImportExternalSemaphore");
    LOAD_SYMBOL_OPT(muDestroyExternalSemaphore, tmuDestroyExternalSemaphore, "muDestroyExternalSemaphore");
    LOAD_SYMBOL_OPT(muSignalExternalSemaphoresAsync, tmuSignalExternalSemaphoresAsync, "muSignalExternalSemaphoresAsync");
    LOAD_SYMBOL_OPT(muWaitExternalSemaphoresAsync, tmuWaitExternalSemaphoresAsync, "muWaitExternalSemaphoresAsync");

    LOAD_SYMBOL(muArrayCreate, tmuArrayCreate, "muArrayCreate_v2");
    LOAD_SYMBOL(muArray3DCreate, tmuArray3DCreate, "muArray3DCreate_v2");
    LOAD_SYMBOL(muArrayDestroy, tmuArrayDestroy, "muArrayDestroy");

    LOAD_SYMBOL_OPT(muEGLStreamProducerConnect, tmuEGLStreamProducerConnect, "muEGLStreamProducerConnect");
    LOAD_SYMBOL_OPT(muEGLStreamProducerDisconnect, tmuEGLStreamProducerDisconnect, "muEGLStreamProducerDisconnect");
    LOAD_SYMBOL_OPT(muEGLStreamConsumerDisconnect, tmuEGLStreamConsumerDisconnect, "muEGLStreamConsumerDisconnect");
    LOAD_SYMBOL_OPT(muEGLStreamProducerPresentFrame, tmuEGLStreamProducerPresentFrame, "muEGLStreamProducerPresentFrame");
    LOAD_SYMBOL_OPT(muEGLStreamProducerReturnFrame, tmuEGLStreamProducerReturnFrame, "muEGLStreamProducerReturnFrame");

#if defined(_WIN32) || defined(__CYGWIN__)
    LOAD_SYMBOL(muD3D11GetDevice, tmuD3D11GetDevice, "muD3D11GetDevice");
    LOAD_SYMBOL(muD3D11GetDevices, tmuD3D11GetDevices, "muD3D11GetDevices");
    LOAD_SYMBOL(muGraphicsD3D11RegisterResource, tmuGraphicsD3D11RegisterResource, "muGraphicsD3D11RegisterResource");
#endif

    GENERIC_LOAD_FUNC_FINALE(musa);
}
#endif

static inline int muvid_load_functions(MuvidFunctions **functions, void *logctx)
{
    GENERIC_LOAD_FUNC_PREAMBLE(MuvidFunctions, muvid, MTMUVID_LIBNAME);

    LOAD_SYMBOL_OPT(muvidGetDecoderCaps, tmuvidGetDecoderCaps, "muvidGetDecoderCaps");
    LOAD_SYMBOL(muvidCreateDecoder, tmuvidCreateDecoder, "muvidCreateDecoder");
    LOAD_SYMBOL(muvidDestroyDecoder, tmuvidDestroyDecoder, "muvidDestroyDecoder");
    LOAD_SYMBOL(muvidDecodePicture, tmuvidDecodePicture, "muvidDecodePicture");
    LOAD_SYMBOL(muvidGetDecodeStatus, tmuvidGetDecodeStatus, "muvidGetDecodeStatus");
    LOAD_SYMBOL(muvidReconfigureDecoder, tmuvidReconfigureDecoder, "muvidReconfigureDecoder");

    LOAD_SYMBOL(muvidMapVideoFrame, tmuvidMapVideoFrame, "muvidMapVideoFrame");
    LOAD_SYMBOL(muvidUnmapVideoFrame, tmuvidUnmapVideoFrame, "muvidUnmapVideoFrame");

    LOAD_SYMBOL(muvidCreateVideoParser, tmuvidCreateVideoParser, "muvidCreateVideoParser");
    LOAD_SYMBOL(muvidParseVideoData, tmuvidParseVideoData, "muvidParseVideoData");
    LOAD_SYMBOL(muvidDestroyVideoParser, tmuvidDestroyVideoParser, "muvidDestroyVideoParser");

    GENERIC_LOAD_FUNC_FINALE(muvid);
}

static inline int mtenc_load_functions(MtencFunctions **functions, void *logctx)
{
    GENERIC_LOAD_FUNC_PREAMBLE(MtencFunctions, mtenc, MTENC_LIBNAME);

    LOAD_SYMBOL(MtEncodeAPICreateInstance, tMtEncodeAPICreateInstance, "MTEncodeAPICreateInstance");
    LOAD_SYMBOL(MtEncodeAPIGetMaxSupportedVersion, tMtEncodeAPIGetMaxSupportedVersion, "MTEncodeAPIGetMaxSupportedVersion");

    GENERIC_LOAD_FUNC_FINALE(mtenc);
}

#undef GENERIC_LOAD_FUNC_PREAMBLE
#undef LOAD_LIBRARY
#undef LOAD_SYMBOL
#undef GENERIC_LOAD_FUNC_FINALE
#undef GENERIC_FREE_FUNC
#undef MUSA_LIBNAME
#undef MTMUVID_LIBNAME
#undef MTENC_LIBNAME

#endif
