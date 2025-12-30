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

#if !defined(FFMT_DYNLINK_MUSA_H) && !defined(MUSA_VERSION)
#define FFMT_DYNLINK_MUSA_H

#include <stddef.h>
#include <stdint.h>

#define MUSA_VERSION 4000

#if defined(_WIN32) || defined(__CYGWIN__)
#define MUSAAPI __stdcall
#else
#define MUSAAPI
#endif

#define MU_CTX_SCHED_BLOCKING_SYNC 4

typedef int MUdevice;
typedef void *MUdeviceptr;
typedef unsigned long long MUtexObject;

typedef struct MUarray_st            *MUarray;
typedef struct MUctx_st              *MUcontext;
typedef struct MUstream_st           *MUstream;
typedef struct MUevent_st            *MUevent;
typedef struct MUfunc_st             *MUfunction;
typedef struct MUmod_st              *MUmodule;
typedef struct MUmipmappedArray_st   *MUmipmappedArray;
typedef struct MUgraphicsResource_st *MUgraphicsResource;
typedef struct MUextMemory_st        *MUexternalMemory;
typedef struct MUextSemaphore_st     *MUexternalSemaphore;
typedef struct MUeglStreamConnection_st *MUeglStreamConnection;

typedef struct MUlinkState_st *MUlinkState;

typedef enum musaError_enum {
    MUSA_SUCCESS = 0,
    MUSA_ERROR_NOT_READY = 600,
    MUSA_ERROR_LAUNCH_TIMEOUT = 702,
    MUSA_ERROR_UNKNOWN = 999
} MUresult;

/**
 * Device properties (subset)
 */
typedef enum MUdevice_attribute_enum {
    MU_DEVICE_ATTRIBUTE_CLOCK_RATE = 13,
    MU_DEVICE_ATTRIBUTE_TEXTURE_ALIGNMENT = 14,
    MU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT = 16,
    MU_DEVICE_ATTRIBUTE_INTEGRATED = 18,
    MU_DEVICE_ATTRIBUTE_CAN_MAP_HOST_MEMORY = 19,
    MU_DEVICE_ATTRIBUTE_COMPUTE_MODE = 20,
    MU_DEVICE_ATTRIBUTE_CONCURRENT_KERNELS = 31,
    MU_DEVICE_ATTRIBUTE_PCI_BUS_ID = 33,
    MU_DEVICE_ATTRIBUTE_PCI_DEVICE_ID = 34,
    MU_DEVICE_ATTRIBUTE_TCC_DRIVER = 35,
    MU_DEVICE_ATTRIBUTE_MEMORY_CLOCK_RATE = 36,
    MU_DEVICE_ATTRIBUTE_GLOBAL_MEMORY_BUS_WIDTH = 37,
    MU_DEVICE_ATTRIBUTE_ASYNC_ENGINE_COUNT = 40,
    MU_DEVICE_ATTRIBUTE_UNIFIED_ADDRESSING = 41,
    MU_DEVICE_ATTRIBUTE_PCI_DOMAIN_ID = 50,
    MU_DEVICE_ATTRIBUTE_TEXTURE_PITCH_ALIGNMENT = 51,
    MU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR = 75,
    MU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR = 76,
    MU_DEVICE_ATTRIBUTE_MANAGED_MEMORY = 83,
    MU_DEVICE_ATTRIBUTE_MULTI_GPU_BOARD = 84,
    MU_DEVICE_ATTRIBUTE_MULTI_GPU_BOARD_GROUP_ID = 85,
} MUdevice_attribute;

typedef enum MUarray_format_enum {
    MU_AD_FORMAT_UNSIGNED_INT8  = 0x01,
    MU_AD_FORMAT_UNSIGNED_INT16 = 0x02,
    MU_AD_FORMAT_UNSIGNED_INT32 = 0x03,
    MU_AD_FORMAT_SIGNED_INT8    = 0x08,
    MU_AD_FORMAT_SIGNED_INT16   = 0x09,
    MU_AD_FORMAT_SIGNED_INT32   = 0x0a,
    MU_AD_FORMAT_HALF           = 0x10,
    MU_AD_FORMAT_FLOAT          = 0x20
} MUarray_format;

typedef enum MUmemorytype_enum {
    MU_MEMORYTYPE_HOST = 1,
    MU_MEMORYTYPE_DEVICE = 2,
    MU_MEMORYTYPE_ARRAY = 3
} MUmemorytype;

typedef enum MUlimit_enum {
    MU_LIMIT_STACK_SIZE = 0,
    MU_LIMIT_PRINTF_FIFO_SIZE = 1,
    MU_LIMIT_MALLOC_HEAP_SIZE = 2,
    MU_LIMIT_DEV_RUNTIME_SYNC_DEPTH = 3,
    MU_LIMIT_DEV_RUNTIME_PENDING_LAUNCH_COUNT = 4
} MUlimit;

typedef enum MUresourcetype_enum {
    MU_RESOURCE_TYPE_ARRAY           = 0x00,
    MU_RESOURCE_TYPE_MIPMAPPED_ARRAY = 0x01,
    MU_RESOURCE_TYPE_LINEAR          = 0x02,
    MU_RESOURCE_TYPE_PITCH2D         = 0x03
} MUresourcetype;

typedef enum MUaddress_mode_enum {
    MU_TR_ADDRESS_MODE_WRAP = 0,
    MU_TR_ADDRESS_MODE_CLAMP = 1,
    MU_TR_ADDRESS_MODE_MIRROR = 2,
    MU_TR_ADDRESS_MODE_BORDER = 3
} MUaddress_mode;

typedef enum MUfilter_mode_enum {
    MU_TR_FILTER_MODE_POINT = 0,
    MU_TR_FILTER_MODE_LINEAR = 1
} MUfilter_mode;

typedef enum MUgraphicsRegisterFlags_enum {
    MU_GRAPHICS_REGISTER_FLAGS_NONE = 0,
    MU_GRAPHICS_REGISTER_FLAGS_READ_ONLY = 1,
    MU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD = 2,
    MU_GRAPHICS_REGISTER_FLAGS_SURFACE_LDST = 4,
    MU_GRAPHICS_REGISTER_FLAGS_TEXTURE_GATHER = 8
} MUgraphicsRegisterFlags;

typedef enum MUexternalMemoryHandleType_enum {
    MU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD        = 1,
    MU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32     = 2,
    MU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT = 3,
    MU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP       = 4,
    MU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE   = 5,
} MUexternalMemoryHandleType;

typedef enum MUexternalSemaphoreHandleType_enum {
    MU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD                = 1,
    MU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32             = 2,
    MU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_KMT         = 3,
    MU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE              = 4,
    MU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_FD    = 9,
    MU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_WIN32 = 10,
} MUexternalSemaphoreHandleType;

typedef enum MUjit_option_enum
{
    MU_JIT_MAX_REGISTERS               = 0,
    MU_JIT_THREADS_PER_BLOCK           = 1,
    MU_JIT_WALL_TIME                   = 2,
    MU_JIT_INFO_LOG_BUFFER             = 3,
    MU_JIT_INFO_LOG_BUFFER_SIZE_BYTES  = 4,
    MU_JIT_ERROR_LOG_BUFFER            = 5,
    MU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES = 6,
    MU_JIT_OPTIMIZATION_LEVEL          = 7,
    MU_JIT_TARGET_FROM_MUCONTEXT       = 8,
    MU_JIT_TARGET                      = 9,
    MU_JIT_FALLBACK_STRATEGY           = 10,
    MU_JIT_GENERATE_DEBUG_INFO         = 11,
    MU_JIT_LOG_VERBOSE                 = 12,
    MU_JIT_GENERATE_LINE_INFO          = 13,
    MU_JIT_CACHE_MODE                  = 14,
    MU_JIT_NEW_SM3X_OPT                = 15,
    MU_JIT_FAST_COMPILE                = 16,
    MU_JIT_GLOBAL_SYMBOL_NAMES         = 17,
    MU_JIT_GLOBAL_SYMBOL_ADDRESSES     = 18,
    MU_JIT_GLOBAL_SYMBOL_COUNT         = 19,
    MU_JIT_NUM_OPTIONS
} MUjit_option;

typedef enum MUjitInputType_enum
{
    MU_JIT_INPUT_MUBIN     = 0,
    MU_JIT_INPUT_PTX       = 1,
    MU_JIT_INPUT_FATBINARY = 2,
    MU_JIT_INPUT_OBJECT    = 3,
    MU_JIT_INPUT_LIBRARY   = 4,
    MU_JIT_NUM_INPUT_TYPES
} MUjitInputType;

typedef enum MUeglFrameType
{
    MU_EGL_FRAME_TYPE_ARRAY = 0,
    MU_EGL_FRAME_TYPE_PITCH = 1,
} MUeglFrameType;

typedef enum MUeglColorFormat
{
    MU_EGL_COLOR_FORMAT_YUV420_PLANAR = 0x00,
    MU_EGL_COLOR_FORMAT_YUV420_SEMIPLANAR = 0x01,
    MU_EGL_COLOR_FORMAT_YVU420_SEMIPLANAR = 0x15,
    MU_EGL_COLOR_FORMAT_Y10V10U10_420_SEMIPLANAR = 0x17,
    MU_EGL_COLOR_FORMAT_Y12V12U12_420_SEMIPLANAR = 0x19,
} MUeglColorFormat;

typedef enum MUd3d11DeviceList_enum
{
    MU_D3D11_DEVICE_LIST_ALL = 1,
    MU_D3D11_DEVICE_LIST_CURRENT_FRAME = 2,
    MU_D3D11_DEVICE_LIST_NEXT_FRAME = 3,
} MUd3d11DeviceList;

#ifndef MU_UUID_HAS_BEEN_DEFINED
#define MU_UUID_HAS_BEEN_DEFINED
typedef struct MUuuid_st {
    char bytes[16];
} MUuuid;
#endif

typedef struct MUSA_MEMCPY2D_st {
    size_t srcXInBytes;
    size_t srcY;
    MUmemorytype srcMemoryType;
    const void *srcHost;
    MUdeviceptr srcDevice;
    MUarray srcArray;
    size_t srcPitch;

    size_t dstXInBytes;
    size_t dstY;
    MUmemorytype dstMemoryType;
    void *dstHost;
    MUdeviceptr dstDevice;
    MUarray dstArray;
    size_t dstPitch;

    size_t WidthInBytes;
    size_t Height;
} MUSA_MEMCPY2D;

typedef struct MUSA_RESOURCE_DESC_st {
    MUresourcetype resType;
    union {
        struct {
            MUarray hArray;
        } array;
        struct {
            MUmipmappedArray hMipmappedArray;
        } mipmap;
        struct {
            MUdeviceptr devPtr;
            MUarray_format format;
            unsigned int numChannels;
            size_t sizeInBytes;
        } linear;
        struct {
            MUdeviceptr devPtr;
            MUarray_format format;
            unsigned int numChannels;
            size_t width;
            size_t height;
            size_t pitchInBytes;
        } pitch2D;
        struct {
            int reserved[32];
        } reserved;
    } res;
    unsigned int flags;
} MUSA_RESOURCE_DESC;

typedef struct MUSA_TEXTURE_DESC_st {
    MUaddress_mode addressMode[3];
    MUfilter_mode filterMode;
    unsigned int flags;
    unsigned int maxAnisotropy;
    MUfilter_mode mipmapFilterMode;
    float mipmapLevelBias;
    float minMipmapLevelClamp;
    float maxMipmapLevelClamp;
    float borderColor[4];
    int reserved[12];
} MUSA_TEXTURE_DESC;

/* Unused type */
typedef struct MUSA_RESOURCE_VIEW_DESC_st MUSA_RESOURCE_VIEW_DESC;

typedef unsigned int GLenum;
typedef unsigned int GLuint;
/*
 * Prefix type name to avoid collisions. Clients using these types
 * will include the real headers with real definitions.
 */
typedef int32_t ffmt_EGLint;
typedef void *ffmt_EGLStreamKHR;

typedef enum MUGLDeviceList_enum {
    MU_GL_DEVICE_LIST_ALL = 1,
    MU_GL_DEVICE_LIST_CURRENT_FRAME = 2,
    MU_GL_DEVICE_LIST_NEXT_FRAME = 3,
} MUGLDeviceList;

typedef struct MUSA_EXTERNAL_MEMORY_HANDLE_DESC_st {
    MUexternalMemoryHandleType type;
    union {
        int fd;
        struct {
            void *handle;
            const void *name;
        } win32;
    } handle;
    unsigned long long size;
    unsigned int flags;
    unsigned int reserved[16];
} MUSA_EXTERNAL_MEMORY_HANDLE_DESC;

typedef struct MUSA_EXTERNAL_MEMORY_BUFFER_DESC_st {
    unsigned long long offset;
    unsigned long long size;
    unsigned int flags;
    unsigned int reserved[16];
} MUSA_EXTERNAL_MEMORY_BUFFER_DESC;

typedef struct MUSA_EXTERNAL_SEMAPHORE_HANDLE_DESC_st {
    MUexternalSemaphoreHandleType type;
    union {
        int fd;
        struct {
            void *handle;
            const void *name;
        } win32;
    } handle;
    unsigned int flags;
    unsigned int reserved[16];
} MUSA_EXTERNAL_SEMAPHORE_HANDLE_DESC;

typedef struct MUSA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS_st {
    struct {
        struct {
            unsigned long long value;
        } fence;
        unsigned int reserved[16];
    } params;
    unsigned int flags;
    unsigned int reserved[16];
} MUSA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS;

typedef MUSA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS MUSA_EXTERNAL_SEMAPHORE_WAIT_PARAMS;

typedef struct MUSA_ARRAY_DESCRIPTOR_st {
    size_t Width;
    size_t Height;

    MUarray_format Format;
    unsigned int NumChannels;
} MUSA_ARRAY_DESCRIPTOR;

typedef struct MUSA_ARRAY3D_DESCRIPTOR_st {
    size_t Width;
    size_t Height;
    size_t Depth;

    MUarray_format Format;
    unsigned int NumChannels;
    unsigned int Flags;
} MUSA_ARRAY3D_DESCRIPTOR;

typedef struct MUSA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC_st {
    unsigned long long offset;
    MUSA_ARRAY3D_DESCRIPTOR arrayDesc;
    unsigned int numLevels;
    unsigned int reserved[16];
} MUSA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC;

#define MU_EGL_FRAME_MAX_PLANES 3
typedef struct MUeglFrame_st {
    union {
        MUarray pArray[MU_EGL_FRAME_MAX_PLANES];
        void* pPitch[MU_EGL_FRAME_MAX_PLANES];
    } frame;
    unsigned int width;
    unsigned int height;
    unsigned int depth;
    unsigned int pitch;
    unsigned int planeCount;
    unsigned int numChannels;
    MUeglFrameType frameType;
    MUeglColorFormat eglColorFormat;
    MUarray_format muFormat;
} MUeglFrame;

#define MU_STREAM_DEFAULT      0
#define MU_STREAM_NON_BLOCKING 1

#define MU_EVENT_DEFAULT        0
#define MU_EVENT_BLOCKING_SYNC  1
#define MU_EVENT_DISABLE_TIMING 2

#define MU_EVENT_WAIT_DEFAULT  0
#define MU_EVENT_WAIT_EXTERNAL 1

#define MU_TRSF_READ_AS_INTEGER 1

typedef void MUSAAPI MUstreamCallback(MUstream hStream, MUresult status, void *userdata);

typedef MUresult MUSAAPI tmuInit(unsigned int Flags);
typedef MUresult MUSAAPI tmuDriverGetVersion(int *driverVersion);
typedef MUresult MUSAAPI tmuDeviceGetCount(int *count);
typedef MUresult MUSAAPI tmuDeviceGet(MUdevice *device, int ordinal);
typedef MUresult MUSAAPI tmuDeviceGetAttribute(int *pi, MUdevice_attribute attrib, MUdevice dev);
typedef MUresult MUSAAPI tmuDeviceGetName(char *name, int len, MUdevice dev);
typedef MUresult MUSAAPI tmuDeviceGetUuid(MUuuid *uuid, MUdevice dev);
typedef MUresult MUSAAPI tmuDeviceGetUuid_v2(MUuuid *uuid, MUdevice dev);
typedef MUresult MUSAAPI tmuDeviceGetLuid(char* luid, unsigned int* deviceNodeMask, MUdevice dev);
typedef MUresult MUSAAPI tmuDeviceGetByPCIBusId(MUdevice* dev, const char* pciBusId);
typedef MUresult MUSAAPI tmuDeviceGetPCIBusId(char* pciBusId, int len, MUdevice dev);
typedef MUresult MUSAAPI tmuDeviceComputeCapability(int *major, int *minor, MUdevice dev);
typedef MUresult MUSAAPI tmuCtxCreate_v2(MUcontext *pctx, unsigned int flags, MUdevice dev);
typedef MUresult MUSAAPI tmuCtxGetCurrent(MUcontext *pctx);
typedef MUresult MUSAAPI tmuCtxSetLimit(MUlimit limit, size_t value);
typedef MUresult MUSAAPI tmuCtxPushCurrent_v2(MUcontext pctx);
typedef MUresult MUSAAPI tmuCtxPopCurrent_v2(MUcontext *pctx);
typedef MUresult MUSAAPI tmuCtxDestroy_v2(MUcontext ctx);
typedef MUresult MUSAAPI tmuMemAlloc_v2(MUdeviceptr *dptr, size_t bytesize);
typedef MUresult MUSAAPI tmuMemAllocPitch_v2(MUdeviceptr *dptr, size_t *pPitch, size_t WidthInBytes, size_t Height, unsigned int ElementSizeBytes);
typedef MUresult MUSAAPI tmuMemAllocManaged(MUdeviceptr *dptr, size_t bytesize, unsigned int flags);
typedef MUresult MUSAAPI tmuMemsetD8Async(MUdeviceptr dstDevice, unsigned char uc, size_t N, MUstream hStream);
typedef MUresult MUSAAPI tmuMemFree_v2(MUdeviceptr dptr);
typedef MUresult MUSAAPI tmuMemcpy(MUdeviceptr dst, MUdeviceptr src, size_t bytesize);
typedef MUresult MUSAAPI tmuMemcpyAsync(MUdeviceptr dst, MUdeviceptr src, size_t bytesize, MUstream hStream);
typedef MUresult MUSAAPI tmuMemcpy2D_v2(const MUSA_MEMCPY2D *pcopy);
typedef MUresult MUSAAPI tmuMemcpy2DAsync_v2(const MUSA_MEMCPY2D *pcopy, MUstream hStream);
typedef MUresult MUSAAPI tmuMemcpyHtoD_v2(MUdeviceptr dstDevice, const void *srcHost, size_t ByteCount);
typedef MUresult MUSAAPI tmuMemcpyHtoDAsync_v2(MUdeviceptr dstDevice, const void *srcHost, size_t ByteCount, MUstream hStream);
typedef MUresult MUSAAPI tmuMemcpyDtoH_v2(void *dstHost, MUdeviceptr srcDevice, size_t ByteCount);
typedef MUresult MUSAAPI tmuMemcpyDtoHAsync_v2(void *dstHost, MUdeviceptr srcDevice, size_t ByteCount, MUstream hStream);
typedef MUresult MUSAAPI tmuMemcpyDtoD_v2(MUdeviceptr dstDevice, MUdeviceptr srcDevice, size_t ByteCount);
typedef MUresult MUSAAPI tmuMemcpyDtoDAsync_v2(MUdeviceptr dstDevice, MUdeviceptr srcDevice, size_t ByteCount, MUstream hStream);
typedef MUresult MUSAAPI tmuGetErrorName(MUresult error, const char** pstr);
typedef MUresult MUSAAPI tmuGetErrorString(MUresult error, const char** pstr);
typedef MUresult MUSAAPI tmuCtxGetDevice(MUdevice *device);

typedef MUresult MUSAAPI tmuDevicePrimaryCtxRetain(MUcontext *pctx, MUdevice dev);
typedef MUresult MUSAAPI tmuDevicePrimaryCtxRelease(MUdevice dev);
typedef MUresult MUSAAPI tmuDevicePrimaryCtxSetFlags(MUdevice dev, unsigned int flags);
typedef MUresult MUSAAPI tmuDevicePrimaryCtxGetState(MUdevice dev, unsigned int *flags, int *active);
typedef MUresult MUSAAPI tmuDevicePrimaryCtxReset(MUdevice dev);

typedef MUresult MUSAAPI tmuStreamCreate(MUstream *phStream, unsigned int flags);
typedef MUresult MUSAAPI tmuStreamQuery(MUstream hStream);
typedef MUresult MUSAAPI tmuStreamSynchronize(MUstream hStream);
typedef MUresult MUSAAPI tmuStreamDestroy_v2(MUstream hStream);
typedef MUresult MUSAAPI tmuStreamAddCallback(MUstream hStream, MUstreamCallback *callback, void *userdata, unsigned int flags);
typedef MUresult MUSAAPI tmuStreamWaitEvent(MUstream hStream, MUevent hEvent, unsigned int flags);
typedef MUresult MUSAAPI tmuEventCreate(MUevent *phEvent, unsigned int flags);
typedef MUresult MUSAAPI tmuEventDestroy_v2(MUevent hEvent);
typedef MUresult MUSAAPI tmuEventSynchronize(MUevent hEvent);
typedef MUresult MUSAAPI tmuEventQuery(MUevent hEvent);
typedef MUresult MUSAAPI tmuEventRecord(MUevent hEvent, MUstream hStream);

typedef MUresult MUSAAPI tmuLaunchKernel(MUfunction f, unsigned int gridDimX, unsigned int gridDimY, unsigned int gridDimZ, unsigned int blockDimX, unsigned int blockDimY, unsigned int blockDimZ, unsigned int sharedMemBytes, MUstream hStream, void** kernelParams, void** extra);
typedef MUresult MUSAAPI tmuLinkCreate(unsigned int numOptions, MUjit_option* options, void** optionValues, MUlinkState* stateOut);
typedef MUresult MUSAAPI tmuLinkAddData(MUlinkState state, MUjitInputType type, void* data, size_t size, const char* name, unsigned int numOptions, MUjit_option* options, void** optionValues);
typedef MUresult MUSAAPI tmuLinkComplete(MUlinkState state, void** mubinOut, size_t* sizeOut);
typedef MUresult MUSAAPI tmuLinkDestroy(MUlinkState state);
typedef MUresult MUSAAPI tmuModuleLoadData(MUmodule* module, const void* image);
typedef MUresult MUSAAPI tmuModuleUnload(MUmodule hmod);
typedef MUresult MUSAAPI tmuModuleGetFunction(MUfunction* hfunc, MUmodule hmod, const char* name);
typedef MUresult MUSAAPI tmuModuleGetGlobal(MUdeviceptr *dptr, size_t *bytes, MUmodule hmod, const char* name);
typedef MUresult MUSAAPI tmuTexObjectCreate(MUtexObject* pTexObject, const MUSA_RESOURCE_DESC* pResDesc, const MUSA_TEXTURE_DESC* pTexDesc, const MUSA_RESOURCE_VIEW_DESC* pResViewDesc);
typedef MUresult MUSAAPI tmuTexObjectDestroy(MUtexObject texObject);

typedef MUresult MUSAAPI tmuGLGetDevices_v2(unsigned int* pMusaDeviceCount, MUdevice* pMusaDevices, unsigned int musaDeviceCount, MUGLDeviceList deviceList);
typedef MUresult MUSAAPI tmuGraphicsGLRegisterImage(MUgraphicsResource* pMusaResource, GLuint image, GLenum target, unsigned int Flags);
typedef MUresult MUSAAPI tmuGraphicsUnregisterResource(MUgraphicsResource resource);
typedef MUresult MUSAAPI tmuGraphicsMapResources(unsigned int count, MUgraphicsResource* resources, MUstream hStream);
typedef MUresult MUSAAPI tmuGraphicsUnmapResources(unsigned int count, MUgraphicsResource* resources, MUstream hStream);
typedef MUresult MUSAAPI tmuGraphicsSubResourceGetMappedArray(MUarray* pArray, MUgraphicsResource resource, unsigned int arrayIndex, unsigned int mipLevel);
typedef MUresult MUSAAPI tmuGraphicsResourceGetMappedPointer(MUdeviceptr *devPtrOut, size_t *sizeOut, MUgraphicsResource resource);

typedef MUresult MUSAAPI tmuImportExternalMemory(MUexternalMemory* extMem_out, const MUSA_EXTERNAL_MEMORY_HANDLE_DESC* memHandleDesc);
typedef MUresult MUSAAPI tmuDestroyExternalMemory(MUexternalMemory extMem);
typedef MUresult MUSAAPI tmuExternalMemoryGetMappedBuffer(MUdeviceptr* devPtr, MUexternalMemory extMem, const MUSA_EXTERNAL_MEMORY_BUFFER_DESC* bufferDesc);
typedef MUresult MUSAAPI tmuExternalMemoryGetMappedMipmappedArray(MUmipmappedArray* mipmap, MUexternalMemory extMem, const MUSA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC* mipmapDesc);
typedef MUresult MUSAAPI tmuMipmappedArrayGetLevel(MUarray* pLevelArray, MUmipmappedArray hMipmappedArray, unsigned int level);
typedef MUresult MUSAAPI tmuMipmappedArrayDestroy(MUmipmappedArray hMipmappedArray);

typedef MUresult MUSAAPI tmuImportExternalSemaphore(MUexternalSemaphore* extSem_out, const MUSA_EXTERNAL_SEMAPHORE_HANDLE_DESC* semHandleDesc);
typedef MUresult MUSAAPI tmuDestroyExternalSemaphore(MUexternalSemaphore extSem);
typedef MUresult MUSAAPI tmuSignalExternalSemaphoresAsync(const MUexternalSemaphore* extSemArray, const MUSA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS* paramsArray, unsigned int numExtSems, MUstream stream);
typedef MUresult MUSAAPI tmuWaitExternalSemaphoresAsync(const MUexternalSemaphore* extSemArray, const MUSA_EXTERNAL_SEMAPHORE_WAIT_PARAMS* paramsArray, unsigned int numExtSems, MUstream stream);

typedef MUresult MUSAAPI tmuArrayCreate(MUarray *pHandle, const MUSA_ARRAY_DESCRIPTOR* pAllocateArray);
typedef MUresult MUSAAPI tmuArray3DCreate(MUarray *pHandle, const MUSA_ARRAY3D_DESCRIPTOR* pAllocateArray);
typedef MUresult MUSAAPI tmuArrayDestroy(MUarray hArray);

typedef MUresult MUSAAPI tmuEGLStreamProducerConnect(MUeglStreamConnection* conn, ffmt_EGLStreamKHR stream, ffmt_EGLint width, ffmt_EGLint height);
typedef MUresult MUSAAPI tmuEGLStreamProducerDisconnect(MUeglStreamConnection* conn);
typedef MUresult MUSAAPI tmuEGLStreamConsumerDisconnect(MUeglStreamConnection* conn);
typedef MUresult MUSAAPI tmuEGLStreamProducerPresentFrame(MUeglStreamConnection* conn, MUeglFrame eglframe, MUstream* pStream);
typedef MUresult MUSAAPI tmuEGLStreamProducerReturnFrame(MUeglStreamConnection* conn, MUeglFrame* eglframe, MUstream* pStream);

typedef MUresult MUSAAPI tmuD3D11GetDevice(MUdevice *device, void *dxgiAdapter);
typedef MUresult MUSAAPI tmuD3D11GetDevices(unsigned int *deviceCountOut, MUdevice *devices, unsigned int deviceCount, void *d3d11device, MUd3d11DeviceList listType);
typedef MUresult MUSAAPI tmuGraphicsD3D11RegisterResource(MUgraphicsResource *musaResourceOut, void *d3d11Resource, unsigned int flags);
#endif
