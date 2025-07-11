/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%                      CCCC   AAA    CCCC  H   H  EEEEE                       %
%                     C      A   A  C      H   H  E                           %
%                     C      AAAAA  C      HHHHH  EEE                         %
%                     C      A   A  C      H   H  E                           %
%                      CCCC  A   A   CCCC  H   H  EEEEE                       %
%                                                                             %
%                                                                             %
%                       MagickCore Pixel Cache Methods                        %
%                                                                             %
%                              Software Design                                %
%                                   Cristy                                    %
%                                 July 1999                                   %
%                                                                             %
%                                                                             %
%  Copyright 1999 ImageMagick Studio LLC, a non-profit organization           %
%  dedicated to making software imaging solutions freely available.           %
%                                                                             %
%  You may not use this file except in compliance with the License.  You may  %
%  obtain a copy of the License at                                            %
%                                                                             %
%    https://imagemagick.org/script/license.php                               %
%                                                                             %
%  Unless required by applicable law or agreed to in writing, software        %
%  distributed under the License is distributed on an "AS IS" BASIS,          %
%  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   %
%  See the License for the specific language governing permissions and        %
%  limitations under the License.                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%
%
*/

/*
  Include declarations.
*/
#include "magick/studio.h"
#include "magick/blob.h"
#include "magick/blob-private.h"
#include "magick/cache.h"
#include "magick/cache-private.h"
#include "magick/color-private.h"
#include "magick/colorspace.h"
#include "magick/colorspace-private.h"
#include "magick/composite-private.h"
#include "magick/distribute-cache-private.h"
#include "magick/exception.h"
#include "magick/exception-private.h"
#include "magick/geometry.h"
#include "magick/list.h"
#include "magick/log.h"
#include "magick/magick.h"
#include "magick/memory_.h"
#include "magick/memory-private.h"
#include "magick/nt-base-private.h"
#include "magick/option.h"
#include "magick/pixel.h"
#include "magick/pixel-accessor.h"
#include "magick/pixel-private.h"
#include "magick/policy.h"
#include "magick/quantum.h"
#include "magick/random_.h"
#include "magick/registry.h"
#include "magick/resource_.h"
#include "magick/semaphore.h"
#include "magick/splay-tree.h"
#include "magick/string_.h"
#include "magick/string-private.h"
#include "magick/thread-private.h"
#include "magick/timer-private.h"
#include "magick/utility.h"
#include "magick/utility-private.h"
#if defined(MAGICKCORE_ZLIB_DELEGATE)
#include "zlib.h"
#endif

/*
  Define declarations.
*/
#define CacheTick(offset,extent)  QuantumTick((MagickOffsetType) offset,extent)
#define IsFileDescriptorLimitExceeded() (GetMagickResource(FileResource) > \
  GetMagickResourceLimit(FileResource) ? MagickTrue : MagickFalse)

/*
  Typedef declarations.
*/
typedef struct _MagickModulo
{
  ssize_t
    quotient,
    remainder;
} MagickModulo;

/*
  Forward declarations.
*/
#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

static Cache
  GetImagePixelCache(Image *,const MagickBooleanType,ExceptionInfo *)
    magick_hot_spot;

static const IndexPacket
  *GetVirtualIndexesFromCache(const Image *);

static const PixelPacket
  *GetVirtualPixelCache(const Image *,const VirtualPixelMethod,const ssize_t,
    const ssize_t,const size_t,const size_t,ExceptionInfo *),
  *GetVirtualPixelsCache(const Image *);

static MagickBooleanType
  GetOneAuthenticPixelFromCache(Image *,const ssize_t,const ssize_t,
    PixelPacket *,ExceptionInfo *),
  GetOneVirtualPixelFromCache(const Image *,const VirtualPixelMethod,
    const ssize_t,const ssize_t,PixelPacket *,ExceptionInfo *),
  OpenPixelCache(Image *,const MapMode,ExceptionInfo *),
  OpenPixelCacheOnDisk(CacheInfo *,const MapMode),
  ReadPixelCacheIndexes(CacheInfo *magick_restrict,NexusInfo *magick_restrict,
    ExceptionInfo *),
  ReadPixelCachePixels(CacheInfo *magick_restrict,NexusInfo *magick_restrict,
    ExceptionInfo *),
  SyncAuthenticPixelsCache(Image *,ExceptionInfo *),
  WritePixelCacheIndexes(CacheInfo *,NexusInfo *magick_restrict,
    ExceptionInfo *),
  WritePixelCachePixels(CacheInfo *,NexusInfo *magick_restrict,
    ExceptionInfo *);

static PixelPacket
  *GetAuthenticPixelsCache(Image *,const ssize_t,const ssize_t,const size_t,
    const size_t,ExceptionInfo *),
  *QueueAuthenticPixelsCache(Image *,const ssize_t,const ssize_t,const size_t,
    const size_t,ExceptionInfo *),
  *SetPixelCacheNexusPixels(const CacheInfo *magick_restrict,const MapMode,
    const ssize_t,const ssize_t,const size_t,const size_t,
    const MagickBooleanType,NexusInfo *magick_restrict,ExceptionInfo *)
    magick_hot_spot;

#if defined(MAGICKCORE_OPENCL_SUPPORT)
static void
  CopyOpenCLBuffer(CacheInfo *magick_restrict);
#endif

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

/*
  Global declarations.
*/
static SemaphoreInfo
  *cache_semaphore = (SemaphoreInfo *) NULL;

static ssize_t
  cache_anonymous_memory = (-1);

#if defined(MAGICKCORE_OPENCL_SUPPORT)
static inline OpenCLCacheInfo *RelinquishOpenCLCacheInfo(MagickCLEnv clEnv,
  OpenCLCacheInfo *info)
{
  ssize_t
    i;

  for (i=0; i < (ssize_t) info->event_count; i++)
    clEnv->library->clReleaseEvent(info->events[i]);
  info->events=(cl_event *) RelinquishMagickMemory(info->events);
  DestroySemaphoreInfo(&info->events_semaphore);
  if (info->buffer != (cl_mem) NULL)
  {
    clEnv->library->clReleaseMemObject(info->buffer);
    info->buffer=(cl_mem) NULL;
  }
  return((OpenCLCacheInfo *) RelinquishMagickMemory(info));
}

static void CL_API_CALL RelinquishPixelCachePixelsDelayed(
  cl_event magick_unused(event),cl_int magick_unused(event_command_exec_status),
  void *user_data)
{
  MagickCLEnv
    clEnv;

  OpenCLCacheInfo
    *info;

  PixelPacket
    *pixels;

  ssize_t
    i;

  magick_unreferenced(event);
  magick_unreferenced(event_command_exec_status);
  info=(OpenCLCacheInfo *) user_data;
  clEnv=GetDefaultOpenCLEnv();
  for (i=(ssize_t)info->event_count-1; i >= 0; i--)
  {
    cl_int
      event_status;

    cl_uint
      status;

    status=clEnv->library->clGetEventInfo(info->events[i],
      CL_EVENT_COMMAND_EXECUTION_STATUS,sizeof(cl_int),&event_status,NULL);
    if ((status == CL_SUCCESS) && (event_status > CL_COMPLETE))
      {
        clEnv->library->clSetEventCallback(info->events[i],CL_COMPLETE,
          &RelinquishPixelCachePixelsDelayed,info);
        return;
      }
  }
  pixels=info->pixels;
  RelinquishMagickResource(MemoryResource,info->length);
  (void) RelinquishOpenCLCacheInfo(clEnv,info);
  (void) RelinquishAlignedMemory(pixels);
}

static MagickBooleanType RelinquishOpenCLBuffer(
  CacheInfo *magick_restrict cache_info)
{
  MagickCLEnv
    clEnv;

  assert(cache_info != (CacheInfo *) NULL);
  if (cache_info->opencl == (OpenCLCacheInfo *) NULL)
    return(MagickFalse);
  RelinquishPixelCachePixelsDelayed((cl_event) NULL,0,cache_info->opencl);
  return(MagickTrue);
}

static cl_event *CopyOpenCLEvents(OpenCLCacheInfo *opencl_info,
  cl_uint *event_count)
{
  cl_event
    *events;

  size_t
    i;

  assert(opencl_info != (OpenCLCacheInfo *) NULL);
  events=(cl_event *) NULL;
  LockSemaphoreInfo(opencl_info->events_semaphore);
  *event_count=opencl_info->event_count;
  if (*event_count > 0)
    {
      events=(cl_event *) AcquireQuantumMemory(*event_count,sizeof(*events));
      if (events == (cl_event *) NULL)
        *event_count=0;
      else
        {
          for (i=0; i < opencl_info->event_count; i++)
            events[i]=opencl_info->events[i];
        }
    }
  UnlockSemaphoreInfo(opencl_info->events_semaphore);
  return(events);
}
#endif

#if defined(MAGICKCORE_OPENCL_SUPPORT)
/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   A d d O p e n C L E v e n t                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  AddOpenCLEvent() adds an event to the list of operations the next operation
%  should wait for.
%
%  The format of the AddOpenCLEvent() method is:
%
%      void AddOpenCLEvent(const Image *image,cl_event event)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o event: the event that should be added.
%
*/
extern MagickPrivate void AddOpenCLEvent(const Image *image,cl_event event)
{
  CacheInfo
    *magick_restrict cache_info;

  MagickCLEnv
    clEnv;

  assert(image != (const Image *) NULL);
  assert(event != (cl_event) NULL);
  cache_info=(CacheInfo *)image->cache;
  assert(cache_info->opencl != (OpenCLCacheInfo *) NULL);
  clEnv=GetDefaultOpenCLEnv();
  if (clEnv->library->clRetainEvent(event) != CL_SUCCESS)
    {
      clEnv->library->clWaitForEvents(1,&event);
      return;
    }
  LockSemaphoreInfo(cache_info->opencl->events_semaphore);
  if (cache_info->opencl->events == (cl_event *) NULL)
    {
      cache_info->opencl->events=(cl_event *) AcquireMagickMemory(sizeof(
        *cache_info->opencl->events));
      cache_info->opencl->event_count=1;
    }
  else
    cache_info->opencl->events=(cl_event *) ResizeQuantumMemory(
      cache_info->opencl->events,++cache_info->opencl->event_count,
      sizeof(*cache_info->opencl->events));
  if (cache_info->opencl->events == (cl_event *) NULL)
    ThrowFatalException(ResourceLimitFatalError,"MemoryAllocationFailed");
  cache_info->opencl->events[cache_info->opencl->event_count-1]=event;
  UnlockSemaphoreInfo(cache_info->opencl->events_semaphore);
}
#endif

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   A c q u i r e P i x e l C a c h e                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  AcquirePixelCache() acquires a pixel cache.
%
%  The format of the AcquirePixelCache() method is:
%
%      Cache AcquirePixelCache(const size_t number_threads)
%
%  A description of each parameter follows:
%
%    o number_threads: the number of nexus threads.
%
*/
MagickExport Cache AcquirePixelCache(const size_t number_threads)
{
  CacheInfo
    *magick_restrict cache_info;

  char
    *value;

  cache_info=(CacheInfo *) AcquireAlignedMemory(1,sizeof(*cache_info));
  if (cache_info == (CacheInfo *) NULL)
    ThrowFatalException(ResourceLimitFatalError,"MemoryAllocationFailed");
  (void) memset(cache_info,0,sizeof(*cache_info));
  cache_info->type=UndefinedCache;
  cache_info->mode=IOMode;
  cache_info->disk_mode=IOMode;
  cache_info->colorspace=sRGBColorspace;
  cache_info->channels=4;
  cache_info->file=(-1);
  cache_info->id=GetMagickThreadId();
  cache_info->number_threads=number_threads;
  if (GetOpenMPMaximumThreads() > cache_info->number_threads)
    cache_info->number_threads=GetOpenMPMaximumThreads();
  if (cache_info->number_threads == 0)
    cache_info->number_threads=1;
  cache_info->nexus_info=AcquirePixelCacheNexus(cache_info->number_threads);
  value=GetEnvironmentValue("MAGICK_SYNCHRONIZE");
  if (value != (const char *) NULL)
    {
      cache_info->synchronize=IsStringTrue(value);
      value=DestroyString(value);
    }
  value=GetPolicyValue("cache:synchronize");
  if (value != (const char *) NULL)
    {
      cache_info->synchronize=IsStringTrue(value);
      value=DestroyString(value);
    }
  cache_info->width_limit=MagickMin(GetMagickResourceLimit(WidthResource),
    (MagickSizeType) MAGICK_SSIZE_MAX);
  cache_info->height_limit=MagickMin(GetMagickResourceLimit(HeightResource),
    (MagickSizeType) MAGICK_SSIZE_MAX);
  cache_info->semaphore=AllocateSemaphoreInfo();
  cache_info->reference_count=1;
  cache_info->file_semaphore=AllocateSemaphoreInfo();
  cache_info->debug=GetLogEventMask() & CacheEvent ? MagickTrue : MagickFalse;
  cache_info->signature=MagickCoreSignature;
  return((Cache ) cache_info);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   A c q u i r e P i x e l C a c h e N e x u s                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  AcquirePixelCacheNexus() allocates the NexusInfo structure.
%
%  The format of the AcquirePixelCacheNexus method is:
%
%      NexusInfo **AcquirePixelCacheNexus(const size_t number_threads)
%
%  A description of each parameter follows:
%
%    o number_threads: the number of nexus threads.
%
*/
MagickExport NexusInfo **AcquirePixelCacheNexus(const size_t number_threads)
{
  NexusInfo
    **magick_restrict nexus_info;

  ssize_t
    i;

  nexus_info=(NexusInfo **) MagickAssumeAligned(AcquireAlignedMemory(2*
    number_threads,sizeof(*nexus_info)));
  if (nexus_info == (NexusInfo **) NULL)
    ThrowFatalException(ResourceLimitFatalError,"MemoryAllocationFailed");
  *nexus_info=(NexusInfo *) AcquireQuantumMemory(number_threads,
    2*sizeof(**nexus_info));
  if (*nexus_info == (NexusInfo *) NULL)
    ThrowFatalException(ResourceLimitFatalError,"MemoryAllocationFailed");
  (void) memset(*nexus_info,0,2*number_threads*sizeof(**nexus_info));
  for (i=0; i < (ssize_t) (2*number_threads); i++)
  {
    nexus_info[i]=(*nexus_info+i);
    if (i < (ssize_t) number_threads)
      nexus_info[i]->virtual_nexus=(*nexus_info+number_threads+i);
    nexus_info[i]->signature=MagickCoreSignature;
  }
  return(nexus_info);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   A c q u i r e P i x e l C a c h e P i x e l s                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  AcquirePixelCachePixels() returns the pixels associated with the specified
%  image.
%
%  The format of the AcquirePixelCachePixels() method is:
%
%      const void *AcquirePixelCachePixels(const Image *image,
%        MagickSizeType *length,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o length: the pixel cache length.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport const void *AcquirePixelCachePixels(const Image *image,
  MagickSizeType *length,ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  (void) exception;
  *length=0;
  if ((cache_info->type != MemoryCache) && (cache_info->type != MapCache))
    return((const void *) NULL);
  *length=cache_info->length;
  return((const void *) cache_info->pixels);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   C a c h e C o m p o n e n t G e n e s i s                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  CacheComponentGenesis() instantiates the cache component.
%
%  The format of the CacheComponentGenesis method is:
%
%      MagickBooleanType CacheComponentGenesis(void)
%
*/
MagickExport MagickBooleanType CacheComponentGenesis(void)
{
  if (cache_semaphore == (SemaphoreInfo *) NULL)
    cache_semaphore=AllocateSemaphoreInfo();
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   C a c h e C o m p o n e n t T e r m i n u s                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  CacheComponentTerminus() destroys the cache component.
%
%  The format of the CacheComponentTerminus() method is:
%
%      CacheComponentTerminus(void)
%
*/
MagickExport void CacheComponentTerminus(void)
{
  if (cache_semaphore == (SemaphoreInfo *) NULL)
    ActivateSemaphoreInfo(&cache_semaphore);
  /* no op-- nothing to destroy */
  DestroySemaphoreInfo(&cache_semaphore);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   C l i p P i x e l C a c h e N e x u s                                     %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ClipPixelCacheNexus() clips the cache nexus as defined by the image clip
%  mask.  The method returns MagickTrue if the pixel region is clipped,
%  otherwise MagickFalse.
%
%  The format of the ClipPixelCacheNexus() method is:
%
%      MagickBooleanType ClipPixelCacheNexus(Image *image,NexusInfo *nexus_info,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o nexus_info: the cache nexus to clip.
%
%    o exception: return any errors or warnings in this structure.
%
*/
static MagickBooleanType ClipPixelCacheNexus(Image *image,
  NexusInfo *nexus_info,ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  const PixelPacket
    *magick_restrict r;

  IndexPacket
    *magick_restrict nexus_indexes,
    *magick_restrict indexes;

  MagickOffsetType
    n;

  NexusInfo
    **magick_restrict clip_nexus;

  PixelPacket
    *magick_restrict p,
    *magick_restrict q;

  ssize_t
    y;

  /*
    Apply clip mask.
  */
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  if ((image->clip_mask == (Image *) NULL) ||
      (image->storage_class == PseudoClass))
    return(MagickTrue);
  if ((nexus_info->region.width == 0) || (nexus_info->region.height == 0))
    return(MagickTrue);
  cache_info=(CacheInfo *) image->cache;
  if (cache_info == (Cache) NULL)
    return(MagickFalse);
  clip_nexus=AcquirePixelCacheNexus(1);
  p=GetAuthenticPixelCacheNexus(image,nexus_info->region.x,nexus_info->region.y,
    nexus_info->region.width,nexus_info->region.height,
    nexus_info->virtual_nexus,exception);
  indexes=nexus_info->virtual_nexus->indexes;
  q=nexus_info->pixels;
  nexus_indexes=nexus_info->indexes;
  r=GetVirtualPixelCacheNexus(image->clip_mask,MaskVirtualPixelMethod,
    nexus_info->region.x,nexus_info->region.y,nexus_info->region.width,
    nexus_info->region.height,clip_nexus[0],exception);
  if ((p == (PixelPacket *) NULL) || (q == (PixelPacket *) NULL) ||
      (r == (const PixelPacket *) NULL))
    return(MagickFalse);
  n=0;
  for (y=0; y < (ssize_t) nexus_info->region.height; y++)
  {
    ssize_t
      x;

    for (x=0; x < (ssize_t) nexus_info->region.width; x++)
    {
      double
        mask_alpha;

      mask_alpha=QuantumScale*GetPixelIntensity(image,r);
      if (fabs(mask_alpha) >= MagickEpsilon)
        {
          SetPixelRed(q,MagickOver_((MagickRealType) p->red,(MagickRealType)
            GetPixelOpacity(p),(MagickRealType) q->red,(MagickRealType)
            GetPixelOpacity(q)));
          SetPixelGreen(q,MagickOver_((MagickRealType) p->green,(MagickRealType)
            GetPixelOpacity(p),(MagickRealType) q->green,(MagickRealType)
            GetPixelOpacity(q)));
          SetPixelBlue(q,MagickOver_((MagickRealType) p->blue,(MagickRealType)
            GetPixelOpacity(p),(MagickRealType) q->blue,(MagickRealType)
            GetPixelOpacity(q)));
          SetPixelOpacity(q,GetPixelOpacity(p));
          if (cache_info->active_index_channel != MagickFalse)
            SetPixelIndex(nexus_indexes+n,GetPixelIndex(indexes+n));
        }
      p++;
      q++;
      r++;
      n++;
    }
  }
  clip_nexus=DestroyPixelCacheNexus(clip_nexus,1);
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   C l o n e P i x e l C a c h e                                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ClonePixelCache() clones a pixel cache.
%
%  The format of the ClonePixelCache() method is:
%
%      Cache ClonePixelCache(const Cache cache)
%
%  A description of each parameter follows:
%
%    o cache: the pixel cache.
%
*/
MagickExport Cache ClonePixelCache(const Cache cache)
{
  CacheInfo
    *magick_restrict clone_info;

  const CacheInfo
    *magick_restrict cache_info;

  assert(cache != NULL);
  cache_info=(const CacheInfo *) cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",
      cache_info->filename);
  clone_info=(CacheInfo *) AcquirePixelCache(cache_info->number_threads);
  clone_info->virtual_pixel_method=cache_info->virtual_pixel_method;
  return((Cache ) clone_info);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   C l o n e P i x e l C a c h e M e t h o d s                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ClonePixelCacheMethods() clones the pixel cache methods from one cache to
%  another.
%
%  The format of the ClonePixelCacheMethods() method is:
%
%      void ClonePixelCacheMethods(Cache clone,const Cache cache)
%
%  A description of each parameter follows:
%
%    o clone: Specifies a pointer to a Cache structure.
%
%    o cache: the pixel cache.
%
*/
MagickExport void ClonePixelCacheMethods(Cache clone,const Cache cache)
{
  CacheInfo
    *magick_restrict cache_info,
    *magick_restrict source_info;

  assert(clone != (Cache) NULL);
  source_info=(CacheInfo *) clone;
  assert(source_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",
      source_info->filename);
  assert(cache != (Cache) NULL);
  cache_info=(CacheInfo *) cache;
  assert(cache_info->signature == MagickCoreSignature);
  source_info->methods=cache_info->methods;
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   C l o n e P i x e l C a c h e R e p o s i t o r y                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% %
%
%  ClonePixelCacheRepository() clones the source pixel cache to the destination
%  cache.
%
%  The format of the ClonePixelCacheRepository() method is:
%
%      MagickBooleanType ClonePixelCacheRepository(CacheInfo *cache_info,
%        CacheInfo *source_info,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o cache_info: the pixel cache.
%
%    o source_info: the source pixel cache.
%
%    o exception: return any errors or warnings in this structure.
%
*/

static MagickBooleanType ClonePixelCacheOnDisk(
  CacheInfo *magick_restrict cache_info,CacheInfo *magick_restrict clone_info)
{
  MagickSizeType
    extent;

  size_t
    quantum;

  ssize_t
    count;

  struct stat
    file_stats;

  unsigned char
    *buffer;

  /*
    Clone pixel cache on disk with identical morphology.
  */
  if ((OpenPixelCacheOnDisk(cache_info,ReadMode) == MagickFalse) ||
      (OpenPixelCacheOnDisk(clone_info,IOMode) == MagickFalse))
    return(MagickFalse);
  if ((lseek(cache_info->file,0,SEEK_SET) < 0) ||
      (lseek(clone_info->file,0,SEEK_SET) < 0))
    return(MagickFalse);
  quantum=(size_t) MagickMaxBufferExtent;
  if ((fstat(cache_info->file,&file_stats) == 0) && (file_stats.st_size > 0))
    {
#if defined(MAGICKCORE_HAVE_LINUX_SENDFILE)
      if (cache_info->length < 0x7ffff000)
        {
          count=sendfile(clone_info->file,cache_info->file,(off_t *) NULL,
            (size_t) cache_info->length);
          if (count == (ssize_t) cache_info->length)
            return(MagickTrue);
          if ((lseek(cache_info->file,0,SEEK_SET) < 0) ||
              (lseek(clone_info->file,0,SEEK_SET) < 0))
            return(MagickFalse);
        }
#endif
      quantum=(size_t) MagickMin(file_stats.st_size,MagickMaxBufferExtent);
    }
  buffer=(unsigned char *) AcquireQuantumMemory(quantum,sizeof(*buffer));
  if (buffer == (unsigned char *) NULL)
    ThrowFatalException(ResourceLimitFatalError,"MemoryAllocationFailed");
  extent=0;
  while ((count=read(cache_info->file,buffer,quantum)) > 0)
  {
    ssize_t
      number_bytes;

    number_bytes=write(clone_info->file,buffer,(size_t) count);
    if (number_bytes != count)
      break;
    extent+=(size_t) number_bytes;
  }
  buffer=(unsigned char *) RelinquishMagickMemory(buffer);
  if (extent != cache_info->length)
    return(MagickFalse);
  return(MagickTrue);
}

static inline int GetCacheNumberThreads(const CacheInfo *source,
  const CacheInfo *destination,const size_t chunk,const int factor)
{
  size_t
    max_threads = (size_t) GetMagickResourceLimit(ThreadResource),
    number_threads = 1,
    workload_factor = 64UL << factor;

  /*
    Determine number of threads based on workload.
  */
  number_threads=(chunk <= workload_factor) ? 1 :
    (chunk >= (workload_factor << 6)) ? max_threads :
    1+(chunk-workload_factor)*(max_threads-1)/(((workload_factor << 6))-1);
  /*
    Limit threads for non-memory or non-map cache sources/destinations.
  */
  if (((source->type != MemoryCache) && (source->type != MapCache)) ||
      ((destination->type != MemoryCache) && (destination->type != MapCache)))
    number_threads=MagickMin(number_threads,4);
  return((int) number_threads);
}

static MagickBooleanType ClonePixelCacheRepository(
  CacheInfo *magick_restrict clone_info,CacheInfo *magick_restrict cache_info,
  ExceptionInfo *exception)
{
#define cache_number_threads(source,destination,chunk,factor) \
  num_threads(GetCacheNumberThreads((source),(destination),(chunk),(factor)))

  MagickBooleanType
    status;

  NexusInfo
    **magick_restrict cache_nexus,
    **magick_restrict clone_nexus;

  size_t
    length;

  ssize_t
    y;

  assert(cache_info != (CacheInfo *) NULL);
  assert(clone_info != (CacheInfo *) NULL);
  assert(exception != (ExceptionInfo *) NULL);
  if (cache_info->type == PingCache)
    return(MagickTrue);
  if ((cache_info->storage_class == clone_info->storage_class) &&
      (cache_info->colorspace == clone_info->colorspace) &&
      (cache_info->channels == clone_info->channels) &&
      (cache_info->columns == clone_info->columns) &&
      (cache_info->rows == clone_info->rows) &&
      (cache_info->active_index_channel == clone_info->active_index_channel))
    {
      /*
        Identical pixel cache morphology.
      */
      if (((cache_info->type == MemoryCache) ||
           (cache_info->type == MapCache)) &&
          ((clone_info->type == MemoryCache) ||
           (clone_info->type == MapCache)))
        {
          (void) memcpy(clone_info->pixels,cache_info->pixels,
            cache_info->columns*cache_info->rows*sizeof(*cache_info->pixels));
          if ((cache_info->active_index_channel != MagickFalse) &&
              (clone_info->active_index_channel != MagickFalse))
            (void) memcpy(clone_info->indexes,cache_info->indexes,
              cache_info->columns*cache_info->rows*
              sizeof(*cache_info->indexes));
          return(MagickTrue);
        }
      if ((cache_info->type == DiskCache) && (clone_info->type == DiskCache))
        return(ClonePixelCacheOnDisk(cache_info,clone_info));
    }
  /*
    Mismatched pixel cache morphology.
  */
  cache_nexus=AcquirePixelCacheNexus(cache_info->number_threads);
  clone_nexus=AcquirePixelCacheNexus(clone_info->number_threads);
  length=(size_t) MagickMin(cache_info->columns,clone_info->columns)*
    sizeof(*cache_info->pixels);
  status=MagickTrue;
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(status) \
    cache_number_threads(cache_info,clone_info,cache_info->rows,3)
#endif
  for (y=0; y < (ssize_t) cache_info->rows; y++)
  {
    const int
      id = GetOpenMPThreadId();

    PixelPacket
      *pixels;

    if (status == MagickFalse)
      continue;
    if (y >= (ssize_t) clone_info->rows)
      continue;
    pixels=SetPixelCacheNexusPixels(cache_info,ReadMode,0,y,
      cache_info->columns,1,MagickFalse,cache_nexus[id],exception);
    if (pixels == (PixelPacket *) NULL)
      continue;
    status=ReadPixelCachePixels(cache_info,cache_nexus[id],exception);
    if (status == MagickFalse)
      continue;
    pixels=SetPixelCacheNexusPixels(clone_info,WriteMode,0,y,
      clone_info->columns,1,MagickFalse,clone_nexus[id],exception);
    if (pixels == (PixelPacket *) NULL)
      continue;
    (void) memset(clone_nexus[id]->pixels,0,(size_t) clone_nexus[id]->length);
    (void) memcpy(clone_nexus[id]->pixels,cache_nexus[id]->pixels,length);
    status=WritePixelCachePixels(clone_info,clone_nexus[id],exception);
  }
  if ((cache_info->active_index_channel != MagickFalse) &&
      (clone_info->active_index_channel != MagickFalse))
    {
      /*
        Clone indexes.
      */
      length=(size_t) MagickMin(cache_info->columns,clone_info->columns)*
        sizeof(*cache_info->indexes);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
      #pragma omp parallel for schedule(static) shared(status) \
        cache_number_threads(cache_info,clone_info,cache_info->rows,3)
#endif
      for (y=0; y < (ssize_t) cache_info->rows; y++)
      {
        const int
          id = GetOpenMPThreadId();

        PixelPacket
          *pixels;

        if (status == MagickFalse)
          continue;
        if (y >= (ssize_t) clone_info->rows)
          continue;
        pixels=SetPixelCacheNexusPixels(cache_info,ReadMode,0,y,
          cache_info->columns,1,MagickFalse,cache_nexus[id],exception);
        if (pixels == (PixelPacket *) NULL)
          continue;
        status=ReadPixelCacheIndexes(cache_info,cache_nexus[id],exception);
        if (status == MagickFalse)
          continue;
        pixels=SetPixelCacheNexusPixels(clone_info,WriteMode,0,y,
          clone_info->columns,1,MagickFalse,clone_nexus[id],exception);
        if (pixels == (PixelPacket *) NULL)
          continue;
        (void) memcpy(clone_nexus[id]->indexes,cache_nexus[id]->indexes,length);
        status=WritePixelCacheIndexes(clone_info,clone_nexus[id],exception);
      }
    }
  clone_nexus=DestroyPixelCacheNexus(clone_nexus,clone_info->number_threads);
  cache_nexus=DestroyPixelCacheNexus(cache_nexus,cache_info->number_threads);
  if (cache_info->debug != MagickFalse)
    {
      char
        message[MaxTextExtent];

      (void) FormatLocaleString(message,MaxTextExtent,"%s => %s",
        CommandOptionToMnemonic(MagickCacheOptions,(ssize_t) cache_info->type),
        CommandOptionToMnemonic(MagickCacheOptions,(ssize_t) clone_info->type));
      (void) LogMagickEvent(CacheEvent,GetMagickModule(),"%s",message);
    }
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   D e s t r o y I m a g e P i x e l C a c h e                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DestroyImagePixelCache() deallocates memory associated with the pixel cache.
%
%  The format of the DestroyImagePixelCache() method is:
%
%      void DestroyImagePixelCache(Image *image)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
*/
static void DestroyImagePixelCache(Image *image)
{
  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  if (image->cache != (void *) NULL)
    image->cache=DestroyPixelCache(image->cache);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   D e s t r o y I m a g e P i x e l s                                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DestroyImagePixels() deallocates memory associated with the pixel cache.
%
%  The format of the DestroyImagePixels() method is:
%
%      void DestroyImagePixels(Image *image)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
*/
MagickExport void DestroyImagePixels(Image *image)
{
  CacheInfo
    *magick_restrict cache_info;

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (cache_info->methods.destroy_pixel_handler != (DestroyPixelHandler) NULL)
    {
      cache_info->methods.destroy_pixel_handler(image);
      return;
    }
  image->cache=DestroyPixelCache(image->cache);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   D e s t r o y P i x e l C a c h e                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DestroyPixelCache() deallocates memory associated with the pixel cache.
%
%  The format of the DestroyPixelCache() method is:
%
%      Cache DestroyPixelCache(Cache cache)
%
%  A description of each parameter follows:
%
%    o cache: the pixel cache.
%
*/

static MagickBooleanType ClosePixelCacheOnDisk(CacheInfo *cache_info)
{
  int
    status;

  status=(-1);
  if (cache_info->file != -1)
    {
      status=close(cache_info->file);
      cache_info->file=(-1);
      RelinquishMagickResource(FileResource,1);
    }
  return(status == -1 ? MagickFalse : MagickTrue);
}

static inline void RelinquishPixelCachePixels(CacheInfo *cache_info)
{
  switch (cache_info->type)
  {
    case MemoryCache:
    {
      (void) ShredMagickMemory(cache_info->pixels,(size_t) cache_info->length);
#if defined(MAGICKCORE_OPENCL_SUPPORT)
      if (RelinquishOpenCLBuffer(cache_info) != MagickFalse)
        {
          cache_info->pixels=(PixelPacket *) NULL;
          break;
        }
#endif
      if (cache_info->mapped == MagickFalse)
        cache_info->pixels=(PixelPacket *) RelinquishAlignedMemory(
          cache_info->pixels);
      else
        {
          (void) UnmapBlob(cache_info->pixels,(size_t) cache_info->length);
          cache_info->pixels=(PixelPacket *) NULL;
        }
      RelinquishMagickResource(MemoryResource,cache_info->length);
      break;
    }
    case MapCache:
    {
      (void) UnmapBlob(cache_info->pixels,(size_t) cache_info->length);
      cache_info->pixels=(PixelPacket *) NULL;
      if ((cache_info->mode != ReadMode) && (cache_info->mode != PersistMode))
        (void) RelinquishUniqueFileResource(cache_info->cache_filename);
      *cache_info->cache_filename='\0';
      RelinquishMagickResource(MapResource,cache_info->length);
      magick_fallthrough;
    }
    case DiskCache:
    {
      if (cache_info->file != -1)
        (void) ClosePixelCacheOnDisk(cache_info);
      if ((cache_info->mode != ReadMode) && (cache_info->mode != PersistMode))
        (void) RelinquishUniqueFileResource(cache_info->cache_filename);
      *cache_info->cache_filename='\0';
      RelinquishMagickResource(DiskResource,cache_info->length);
      break;
    }
    case DistributedCache:
    {
      *cache_info->cache_filename='\0';
      (void) RelinquishDistributePixelCache((DistributeCacheInfo *)
        cache_info->server_info);
      break;
    }
    default:
      break;
  }
  cache_info->type=UndefinedCache;
  cache_info->mapped=MagickFalse;
  cache_info->indexes=(IndexPacket *) NULL;
}

MagickExport Cache DestroyPixelCache(Cache cache)
{
  CacheInfo
    *magick_restrict cache_info;

  assert(cache != (Cache) NULL);
  cache_info=(CacheInfo *) cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",
      cache_info->filename);
  LockSemaphoreInfo(cache_info->semaphore);
  cache_info->reference_count--;
  if (cache_info->reference_count != 0)
    {
      UnlockSemaphoreInfo(cache_info->semaphore);
      return((Cache) NULL);
    }
  UnlockSemaphoreInfo(cache_info->semaphore);
  if (cache_info->debug != MagickFalse)
    {
      char
        message[MaxTextExtent];

      (void) FormatLocaleString(message,MaxTextExtent,"destroy %s",
        cache_info->filename);
      (void) LogMagickEvent(CacheEvent,GetMagickModule(),"%s",message);
    }
  RelinquishPixelCachePixels(cache_info);
  if (cache_info->server_info != (DistributeCacheInfo *) NULL)
    cache_info->server_info=DestroyDistributeCacheInfo((DistributeCacheInfo *)
      cache_info->server_info);
  if (cache_info->nexus_info != (NexusInfo **) NULL)
    cache_info->nexus_info=DestroyPixelCacheNexus(cache_info->nexus_info,
      cache_info->number_threads);
  if (cache_info->random_info != (RandomInfo *) NULL)
    cache_info->random_info=DestroyRandomInfo(cache_info->random_info);
  if (cache_info->file_semaphore != (SemaphoreInfo *) NULL)
    DestroySemaphoreInfo(&cache_info->file_semaphore);
  if (cache_info->semaphore != (SemaphoreInfo *) NULL)
    DestroySemaphoreInfo(&cache_info->semaphore);
  cache_info->signature=(~MagickCoreSignature);
  cache_info=(CacheInfo *) RelinquishAlignedMemory(cache_info);
  cache=(Cache) NULL;
  return(cache);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   D e s t r o y P i x e l C a c h e N e x u s                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DestroyPixelCacheNexus() destroys a pixel cache nexus.
%
%  The format of the DestroyPixelCacheNexus() method is:
%
%      NexusInfo **DestroyPixelCacheNexus(NexusInfo *nexus_info,
%        const size_t number_threads)
%
%  A description of each parameter follows:
%
%    o nexus_info: the nexus to destroy.
%
%    o number_threads: the number of nexus threads.
%
*/

static inline void RelinquishCacheNexusPixels(NexusInfo *nexus_info)
{
  if (nexus_info->mapped == MagickFalse)
    (void) RelinquishAlignedMemory(nexus_info->cache);
  else
    (void) UnmapBlob(nexus_info->cache,(size_t) nexus_info->length);
  nexus_info->cache=(PixelPacket *) NULL;
  nexus_info->pixels=(PixelPacket *) NULL;
  nexus_info->indexes=(IndexPacket *) NULL;
  nexus_info->length=0;
  nexus_info->mapped=MagickFalse;
}

MagickExport NexusInfo **DestroyPixelCacheNexus(NexusInfo **nexus_info,
  const size_t number_threads)
{
  ssize_t
    i;

  assert(nexus_info != (NexusInfo **) NULL);
  for (i=0; i < (ssize_t) (2*number_threads); i++)
  {
    if (nexus_info[i]->cache != (PixelPacket *) NULL)
      RelinquishCacheNexusPixels(nexus_info[i]);
    nexus_info[i]->signature=(~MagickCoreSignature);
  }
  *nexus_info=(NexusInfo *) RelinquishMagickMemory(*nexus_info);
  nexus_info=(NexusInfo **) RelinquishAlignedMemory(nexus_info);
  return(nexus_info);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t A u t h e n t i c I n d e x e s F r o m C a c h e                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetAuthenticIndexesFromCache() returns the indexes associated with the last
%  call to QueueAuthenticPixelsCache() or GetAuthenticPixelsCache().
%
%  The format of the GetAuthenticIndexesFromCache() method is:
%
%      IndexPacket *GetAuthenticIndexesFromCache(const Image *image)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
*/
static IndexPacket *GetAuthenticIndexesFromCache(const Image *image)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  assert(id < (int) cache_info->number_threads);
  return(cache_info->nexus_info[id]->indexes);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   G e t A u t h e n t i c I n d e x Q u e u e                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetAuthenticIndexQueue() returns the authentic black channel or the colormap
%  indexes associated with the last call to QueueAuthenticPixels() or
%  GetVirtualPixels().  NULL is returned if the black channel or colormap
%  indexes are not available.
%
%  The format of the GetAuthenticIndexQueue() method is:
%
%      IndexPacket *GetAuthenticIndexQueue(const Image *image)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
*/
MagickExport IndexPacket *GetAuthenticIndexQueue(const Image *image)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (cache_info->methods.get_authentic_indexes_from_handler !=
       (GetAuthenticIndexesFromHandler) NULL)
    return(cache_info->methods.get_authentic_indexes_from_handler(image));
  assert(id < (int) cache_info->number_threads);
  return(cache_info->nexus_info[id]->indexes);
}

#if defined(MAGICKCORE_OPENCL_SUPPORT)
/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t A u t h e n t i c O p e n C L B u f f e r                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetAuthenticOpenCLBuffer() returns an OpenCL buffer used to execute OpenCL
%  operations.
%
%  The format of the GetAuthenticOpenCLBuffer() method is:
%
%      cl_mem GetAuthenticOpenCLBuffer(const Image *image)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
*/
MagickPrivate cl_mem GetAuthenticOpenCLBuffer(const Image *image,
  ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  cl_context
    context;

  cl_int
    status;

  MagickCLEnv
    clEnv;

  assert(image != (const Image *) NULL);
  cache_info=(CacheInfo *)image->cache;
  if ((cache_info->type == UndefinedCache) || (cache_info->reference_count > 1))
    {
      SyncImagePixelCache((Image *) image,exception);
      cache_info=(CacheInfo *)image->cache;
    }
  if ((cache_info->type != MemoryCache) || (cache_info->mapped != MagickFalse))
    return((cl_mem) NULL);
  LockSemaphoreInfo(cache_info->semaphore);
  clEnv=GetDefaultOpenCLEnv();
  if (cache_info->opencl == (OpenCLCacheInfo *) NULL)
    {
      assert(cache_info->pixels != NULL);
      context=GetOpenCLContext(clEnv);
      cache_info->opencl=(OpenCLCacheInfo *) AcquireCriticalMemory(
        sizeof(*cache_info->opencl));
      (void) memset(cache_info->opencl,0,sizeof(*cache_info->opencl));
      cache_info->opencl->events_semaphore=AllocateSemaphoreInfo();
      cache_info->opencl->length=cache_info->length;
      cache_info->opencl->pixels=cache_info->pixels;
      cache_info->opencl->buffer=clEnv->library->clCreateBuffer(context,
        CL_MEM_USE_HOST_PTR,cache_info->length,cache_info->pixels,&status);
      if (status != CL_SUCCESS)
        cache_info->opencl=RelinquishOpenCLCacheInfo(clEnv,cache_info->opencl);
    }
  if (cache_info->opencl != (OpenCLCacheInfo *) NULL)
    clEnv->library->clRetainMemObject(cache_info->opencl->buffer);
  UnlockSemaphoreInfo(cache_info->semaphore);
  if (cache_info->opencl == (OpenCLCacheInfo *) NULL)
    return((cl_mem) NULL);
  return(cache_info->opencl->buffer);
}
#endif

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t A u t h e n t i c P i x e l C a c h e N e x u s                     %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetAuthenticPixelCacheNexus() gets authentic pixels from the in-memory or
%  disk pixel cache as defined by the geometry parameters.   A pointer to the
%  pixels is returned if the pixels are transferred, otherwise a NULL is
%  returned.
%
%  The format of the GetAuthenticPixelCacheNexus() method is:
%
%      PixelPacket *GetAuthenticPixelCacheNexus(Image *image,const ssize_t x,
%        const ssize_t y,const size_t columns,const size_t rows,
%        NexusInfo *nexus_info,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o x,y,columns,rows:  These values define the perimeter of a region of
%      pixels.
%
%    o nexus_info: the cache nexus to return.
%
%    o exception: return any errors or warnings in this structure.
%
*/

MagickExport PixelPacket *GetAuthenticPixelCacheNexus(Image *image,
  const ssize_t x,const ssize_t y,const size_t columns,const size_t rows,
  NexusInfo *nexus_info,ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  PixelPacket
    *magick_restrict pixels;

  /*
    Transfer pixels from the cache.
  */
  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  pixels=QueueAuthenticPixelCacheNexus(image,x,y,columns,rows,MagickTrue,
    nexus_info,exception);
  if (pixels == (PixelPacket *) NULL)
    return((PixelPacket *) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (nexus_info->authentic_pixel_cache != MagickFalse)
    return(pixels);
  if (ReadPixelCachePixels(cache_info,nexus_info,exception) == MagickFalse)
    return((PixelPacket *) NULL);
  if (cache_info->active_index_channel != MagickFalse)
    if (ReadPixelCacheIndexes(cache_info,nexus_info,exception) == MagickFalse)
      return((PixelPacket *) NULL);
  return(pixels);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t A u t h e n t i c P i x e l s F r o m C a c h e                     %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetAuthenticPixelsFromCache() returns the pixels associated with the last
%  call to the QueueAuthenticPixelsCache() or GetAuthenticPixelsCache() methods.
%
%  The format of the GetAuthenticPixelsFromCache() method is:
%
%      PixelPacket *GetAuthenticPixelsFromCache(const Image image)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
*/
static PixelPacket *GetAuthenticPixelsFromCache(const Image *image)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  assert(id < (int) cache_info->number_threads);
  return(cache_info->nexus_info[id]->pixels);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   G e t A u t h e n t i c P i x e l Q u e u e                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetAuthenticPixelQueue() returns the authentic pixels associated with the
%  last call to QueueAuthenticPixels() or GetAuthenticPixels().
%
%  The format of the GetAuthenticPixelQueue() method is:
%
%      PixelPacket *GetAuthenticPixelQueue(const Image image)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
*/
MagickExport PixelPacket *GetAuthenticPixelQueue(const Image *image)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (cache_info->methods.get_authentic_pixels_from_handler !=
       (GetAuthenticPixelsFromHandler) NULL)
    return(cache_info->methods.get_authentic_pixels_from_handler(image));
  assert(id < (int) cache_info->number_threads);
  return(cache_info->nexus_info[id]->pixels);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   G e t A u t h e n t i c P i x e l s                                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetAuthenticPixels() obtains a pixel region for read/write access. If the
%  region is successfully accessed, a pointer to a PixelPacket array
%  representing the region is returned, otherwise NULL is returned.
%
%  The returned pointer may point to a temporary working copy of the pixels
%  or it may point to the original pixels in memory. Performance is maximized
%  if the selected region is part of one row, or one or more full rows, since
%  then there is opportunity to access the pixels in-place (without a copy)
%  if the image is in memory, or in a memory-mapped file. The returned pointer
%  must *never* be deallocated by the user.
%
%  Pixels accessed via the returned pointer represent a simple array of type
%  PixelPacket. If the image type is CMYK or if the storage class is
%  PseduoClass, call GetAuthenticIndexQueue() after invoking
%  GetAuthenticPixels() to obtain the black color component or colormap indexes
%  (of type IndexPacket) corresponding to the region.  Once the PixelPacket
%  (and/or IndexPacket) array has been updated, the changes must be saved back
%  to the underlying image using SyncAuthenticPixels() or they may be lost.
%
%  The format of the GetAuthenticPixels() method is:
%
%      PixelPacket *GetAuthenticPixels(Image *image,const ssize_t x,
%        const ssize_t y,const size_t columns,const size_t rows,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o x,y,columns,rows:  These values define the perimeter of a region of
%      pixels.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport PixelPacket *GetAuthenticPixels(Image *image,const ssize_t x,
  const ssize_t y,const size_t columns,const size_t rows,
  ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (cache_info->methods.get_authentic_pixels_handler !=
       (GetAuthenticPixelsHandler) NULL)
    return(cache_info->methods.get_authentic_pixels_handler(image,x,y,columns,
      rows,exception));
  assert(id < (int) cache_info->number_threads);
  return(GetAuthenticPixelCacheNexus(image,x,y,columns,rows,
    cache_info->nexus_info[id],exception));
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t A u t h e n t i c P i x e l s C a c h e                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetAuthenticPixelsCache() gets pixels from the in-memory or disk pixel cache
%  as defined by the geometry parameters.   A pointer to the pixels is returned
%  if the pixels are transferred, otherwise a NULL is returned.
%
%  The format of the GetAuthenticPixelsCache() method is:
%
%      PixelPacket *GetAuthenticPixelsCache(Image *image,const ssize_t x,
%        const ssize_t y,const size_t columns,const size_t rows,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o x,y,columns,rows:  These values define the perimeter of a region of
%      pixels.
%
%    o exception: return any errors or warnings in this structure.
%
*/
static PixelPacket *GetAuthenticPixelsCache(Image *image,const ssize_t x,
  const ssize_t y,const size_t columns,const size_t rows,
  ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  if (cache_info == (Cache) NULL)
    return((PixelPacket *) NULL);
  assert(cache_info->signature == MagickCoreSignature);
  assert(id < (int) cache_info->number_threads);
  return(GetAuthenticPixelCacheNexus(image,x,y,columns,rows,
    cache_info->nexus_info[id],exception));
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t I m a g e E x t e n t                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetImageExtent() returns the extent of the pixels associated with the
%  last call to QueueAuthenticPixels() or GetAuthenticPixels().
%
%  The format of the GetImageExtent() method is:
%
%      MagickSizeType GetImageExtent(const Image *image)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
*/
MagickExport MagickSizeType GetImageExtent(const Image *image)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  assert(id < (int) cache_info->number_threads);
  return(GetPixelCacheNexusExtent(cache_info,cache_info->nexus_info[id]));
}

#if defined(MAGICKCORE_OPENCL_SUPPORT)
/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t O p e n C L E v e n t s                                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetOpenCLEvents() returns the events that the next operation should wait
%  for.  The argument event_count is set to the number of events.
%
%  The format of the GetOpenCLEvents() method is:
%
%      const cl_event *GetOpenCLEvents(const Image *image,
%        cl_command_queue queue)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o event_count: will be set to the number of events.
%
*/

extern MagickPrivate cl_event *GetOpenCLEvents(const Image *image,
  cl_uint *event_count)
{
  CacheInfo
    *magick_restrict cache_info;

  cl_event
    *events;

  assert(image != (const Image *) NULL);
  assert(event_count != (cl_uint *) NULL);
  cache_info=(CacheInfo *) image->cache;
  *event_count=0;
  events=(cl_event *) NULL;
  if (cache_info->opencl != (OpenCLCacheInfo *) NULL)
    events=CopyOpenCLEvents(cache_info->opencl,event_count);
  return(events);
}
#endif

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t I m a g e P i x e l C a c h e                                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetImagePixelCache() ensures that there is only a single reference to the
%  pixel cache to be modified, updating the provided cache pointer to point to
%  a clone of the original pixel cache if necessary.
%
%  The format of the GetImagePixelCache method is:
%
%      Cache GetImagePixelCache(Image *image,const MagickBooleanType clone,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o clone: any value other than MagickFalse clones the cache pixels.
%
%    o exception: return any errors or warnings in this structure.
%
*/

static inline MagickBooleanType ValidatePixelCacheMorphology(
  const Image *magick_restrict image)
{
  CacheInfo
    *magick_restrict cache_info;

  /*
    Does the image match the pixel cache morphology?
  */
  cache_info=(CacheInfo *) image->cache;
  if ((image->storage_class != cache_info->storage_class) ||
      (image->colorspace != cache_info->colorspace) ||
      (image->channels != cache_info->channels) ||
      (image->columns != cache_info->columns) ||
      (image->rows != cache_info->rows) ||
      (cache_info->nexus_info == (NexusInfo **) NULL))
    return(MagickFalse);
  return(MagickTrue);
}

static Cache GetImagePixelCache(Image *image,const MagickBooleanType clone,
  ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  MagickBooleanType
    destroy,
    status = MagickTrue;

  static MagickSizeType
    cpu_throttle = MagickResourceInfinity,
    cycles = 0;

  if (IsImageTTLExpired(image) != MagickFalse)
    {
#if defined(ESTALE)
      errno=ESTALE;
#endif
      (void) ThrowMagickException(exception,GetMagickModule(),
        ResourceLimitError,"TimeLimitExceeded","`%s'",image->filename);
      return((Cache) NULL);
    }
  if (cpu_throttle == MagickResourceInfinity)
    cpu_throttle=GetMagickResourceLimit(ThrottleResource);
  if ((cpu_throttle != 0) && ((cycles++ % 4096) == 0))
    MagickDelay(cpu_throttle);
  LockSemaphoreInfo(image->semaphore);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
#if defined(MAGICKCORE_OPENCL_SUPPORT)
  CopyOpenCLBuffer(cache_info);
#endif
  destroy=MagickFalse;
  if ((cache_info->reference_count > 1) || (cache_info->mode == ReadMode))
    {
      LockSemaphoreInfo(cache_info->semaphore);
      if ((cache_info->reference_count > 1) || (cache_info->mode == ReadMode))
        {
          CacheInfo
            *clone_info;

          Image
            clone_image;

          /*
            Clone pixel cache.
          */
          clone_image=(*image);
          clone_image.semaphore=AllocateSemaphoreInfo();
          clone_image.reference_count=1;
          clone_image.cache=ClonePixelCache(cache_info);
          clone_info=(CacheInfo *) clone_image.cache;
          status=OpenPixelCache(&clone_image,IOMode,exception);
          if (status == MagickFalse)
            clone_info=(CacheInfo *) DestroyPixelCache(clone_info);
          else
            {
              if (clone != MagickFalse)
                status=ClonePixelCacheRepository(clone_info,cache_info,
                  exception);
              if (status == MagickFalse)
                clone_info=(CacheInfo *) DestroyPixelCache(clone_info);
              else
                {
                  destroy=MagickTrue;
                  image->cache=clone_info;
                }
            }
          DestroySemaphoreInfo(&clone_image.semaphore);
        }
      UnlockSemaphoreInfo(cache_info->semaphore);
    }
  if (destroy != MagickFalse)
    cache_info=(CacheInfo *) DestroyPixelCache(cache_info);
  if (status != MagickFalse)
    {
      /*
        Ensure the image matches the pixel cache morphology.
      */
      if (image->type != UndefinedType)
        image->type=UndefinedType;
      if (ValidatePixelCacheMorphology(image) == MagickFalse)
        {
          status=OpenPixelCache(image,IOMode,exception);
          cache_info=(CacheInfo *) image->cache;
          if (cache_info->file != -1)
            (void) ClosePixelCacheOnDisk(cache_info);
        }
    }
  UnlockSemaphoreInfo(image->semaphore);
  if (status == MagickFalse)
    return((Cache) NULL);
  return(image->cache);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t I m a g e P i x e l C a c h e T y p e                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetImagePixelCacheType() returns the pixel cache type: UndefinedCache,
%  DiskCache, MapCache, MemoryCache, or PingCache.
%
%  The format of the GetImagePixelCacheType() method is:
%
%      CacheType GetImagePixelCacheType(const Image *image)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
*/

MagickExport CacheType GetPixelCacheType(const Image *image)
{
  return(GetImagePixelCacheType(image));
}

MagickExport CacheType GetImagePixelCacheType(const Image *image)
{
  CacheInfo
    *magick_restrict cache_info;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  return(cache_info->type);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   G e t O n e A u t h e n t i c P i x e l                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetOneAuthenticPixel() returns a single pixel at the specified (x,y)
%  location.  The image background color is returned if an error occurs.
%
%  The format of the GetOneAuthenticPixel() method is:
%
%      MagickBooleanType GetOneAuthenticPixel(const Image image,const ssize_t x,
%        const ssize_t y,PixelPacket *pixel,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o x,y:  These values define the location of the pixel to return.
%
%    o pixel: return a pixel at the specified (x,y) location.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport MagickBooleanType GetOneAuthenticPixel(Image *image,
  const ssize_t x,const ssize_t y,PixelPacket *pixel,ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  PixelPacket
    *magick_restrict pixels;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  *pixel=image->background_color;
  if (cache_info->methods.get_one_authentic_pixel_from_handler != (GetOneAuthenticPixelFromHandler) NULL)
    return(cache_info->methods.get_one_authentic_pixel_from_handler(image,x,y,pixel,exception));
  pixels=GetAuthenticPixelsCache(image,x,y,1UL,1UL,exception);
  if (pixels == (PixelPacket *) NULL)
    return(MagickFalse);
  *pixel=(*pixels);
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t O n e A u t h e n t i c P i x e l F r o m C a c h e                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetOneAuthenticPixelFromCache() returns a single pixel at the specified (x,y)
%  location.  The image background color is returned if an error occurs.
%
%  The format of the GetOneAuthenticPixelFromCache() method is:
%
%      MagickBooleanType GetOneAuthenticPixelFromCache(const Image image,
%        const ssize_t x,const ssize_t y,PixelPacket *pixel,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o x,y:  These values define the location of the pixel to return.
%
%    o pixel: return a pixel at the specified (x,y) location.
%
%    o exception: return any errors or warnings in this structure.
%
*/
static MagickBooleanType GetOneAuthenticPixelFromCache(Image *image,
  const ssize_t x,const ssize_t y,PixelPacket *pixel,ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  PixelPacket
    *magick_restrict pixels;

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  *pixel=image->background_color;
  assert(id < (int) cache_info->number_threads);
  pixels=GetAuthenticPixelCacheNexus(image,x,y,1UL,1UL,
    cache_info->nexus_info[id],exception);
  if (pixels == (PixelPacket *) NULL)
    return(MagickFalse);
  *pixel=(*pixels);
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   G e t O n e V i r t u a l M a g i c k P i x e l                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetOneVirtualMagickPixel() returns a single pixel at the specified (x,y)
%  location.  The image background color is returned if an error occurs.  If
%  you plan to modify the pixel, use GetOneAuthenticPixel() instead.
%
%  The format of the GetOneVirtualMagickPixel() method is:
%
%      MagickBooleanType GetOneVirtualMagickPixel(const Image image,
%        const ssize_t x,const ssize_t y,MagickPixelPacket *pixel,
%        ExceptionInfo exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o x,y:  these values define the location of the pixel to return.
%
%    o pixel: return a pixel at the specified (x,y) location.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport MagickBooleanType GetOneVirtualMagickPixel(const Image *image,
  const ssize_t x,const ssize_t y,MagickPixelPacket *pixel,
  ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  const IndexPacket
    *magick_restrict indexes;

  const PixelPacket
    *magick_restrict pixels;

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  assert(id < (int) cache_info->number_threads);
  pixels=GetVirtualPixelCacheNexus(image,GetPixelCacheVirtualMethod(image),x,y,
    1UL,1UL,cache_info->nexus_info[id],exception);
  GetMagickPixelPacket(image,pixel);
  if (pixels == (const PixelPacket *) NULL)
    return(MagickFalse);
  indexes=GetVirtualIndexesFromNexus(cache_info,cache_info->nexus_info[id]);
  SetMagickPixelPacket(image,pixels,indexes,pixel);
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   G e t O n e V i r t u a l M e t h o d P i x e l                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetOneVirtualMethodPixel() returns a single pixel at the specified (x,y)
%  location as defined by specified pixel method.  The image background color
%  is returned if an error occurs.  If you plan to modify the pixel, use
%  GetOneAuthenticPixel() instead.
%
%  The format of the GetOneVirtualMethodPixel() method is:
%
%      MagickBooleanType GetOneVirtualMethodPixel(const Image image,
%        const VirtualPixelMethod virtual_pixel_method,const ssize_t x,
%        const ssize_t y,Pixelpacket *pixel,ExceptionInfo exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o virtual_pixel_method: the virtual pixel method.
%
%    o x,y:  These values define the location of the pixel to return.
%
%    o pixel: return a pixel at the specified (x,y) location.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport MagickBooleanType GetOneVirtualMethodPixel(const Image *image,
  const VirtualPixelMethod virtual_pixel_method,const ssize_t x,const ssize_t y,
  PixelPacket *pixel,ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  const PixelPacket
    *magick_restrict pixels;

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  *pixel=image->background_color;
  if (cache_info->methods.get_one_virtual_pixel_from_handler !=
       (GetOneVirtualPixelFromHandler) NULL)
    return(cache_info->methods.get_one_virtual_pixel_from_handler(image,
      virtual_pixel_method,x,y,pixel,exception));
  assert(id < (int) cache_info->number_threads);
  pixels=GetVirtualPixelCacheNexus(image,virtual_pixel_method,x,y,1UL,1UL,
    cache_info->nexus_info[id],exception);
  if (pixels == (const PixelPacket *) NULL)
    return(MagickFalse);
  *pixel=(*pixels);
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   G e t O n e V i r t u a l P i x e l                                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetOneVirtualPixel() returns a single virtual pixel at the specified
%  (x,y) location.  The image background color is returned if an error occurs.
%  If you plan to modify the pixel, use GetOneAuthenticPixel() instead.
%
%  The format of the GetOneVirtualPixel() method is:
%
%      MagickBooleanType GetOneVirtualPixel(const Image image,const ssize_t x,
%        const ssize_t y,PixelPacket *pixel,ExceptionInfo exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o x,y:  These values define the location of the pixel to return.
%
%    o pixel: return a pixel at the specified (x,y) location.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport MagickBooleanType GetOneVirtualPixel(const Image *image,
  const ssize_t x,const ssize_t y,PixelPacket *pixel,ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  const PixelPacket
    *magick_restrict pixels;

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  *pixel=image->background_color;
  if (cache_info->methods.get_one_virtual_pixel_from_handler !=
       (GetOneVirtualPixelFromHandler) NULL)
    return(cache_info->methods.get_one_virtual_pixel_from_handler(image,
      GetPixelCacheVirtualMethod(image),x,y,pixel,exception));
  assert(id < (int) cache_info->number_threads);
  pixels=GetVirtualPixelCacheNexus(image,GetPixelCacheVirtualMethod(image),x,y,
    1UL,1UL,cache_info->nexus_info[id],exception);
  if (pixels == (const PixelPacket *) NULL)
    return(MagickFalse);
  *pixel=(*pixels);
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t O n e V i r t u a l P i x e l F r o m C a c h e                     %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetOneVirtualPixelFromCache() returns a single virtual pixel at the
%  specified (x,y) location.  The image background color is returned if an
%  error occurs.
%
%  The format of the GetOneVirtualPixelFromCache() method is:
%
%      MagickBooleanType GetOneVirtualPixelFromCache(const Image image,
%        const VirtualPixelPacket method,const ssize_t x,const ssize_t y,
%        PixelPacket *pixel,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o virtual_pixel_method: the virtual pixel method.
%
%    o x,y:  These values define the location of the pixel to return.
%
%    o pixel: return a pixel at the specified (x,y) location.
%
%    o exception: return any errors or warnings in this structure.
%
*/
static MagickBooleanType GetOneVirtualPixelFromCache(const Image *image,
  const VirtualPixelMethod virtual_pixel_method,const ssize_t x,const ssize_t y,
  PixelPacket *pixel,ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  const PixelPacket
    *magick_restrict pixels;

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  assert(id < (int) cache_info->number_threads);
  *pixel=image->background_color;
  pixels=GetVirtualPixelCacheNexus(image,virtual_pixel_method,x,y,1UL,1UL,
    cache_info->nexus_info[id],exception);
  if (pixels == (const PixelPacket *) NULL)
    return(MagickFalse);
  *pixel=(*pixels);
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t P i x e l C a c h e C h a n n e l s                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetPixelCacheChannels() returns the number of pixel channels associated
%  with this instance of the pixel cache.
%
%  The format of the GetPixelCacheChannels() method is:
%
%      size_t GetPixelCacheChannels(Cache cache)
%
%  A description of each parameter follows:
%
%    o type: GetPixelCacheChannels returns DirectClass or PseudoClass.
%
%    o cache: the pixel cache.
%
*/
MagickExport size_t GetPixelCacheChannels(const Cache cache)
{
  CacheInfo
    *magick_restrict cache_info;

  assert(cache != (Cache) NULL);
  cache_info=(CacheInfo *) cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",
      cache_info->filename);
  return(cache_info->channels);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t P i x e l C a c h e C o l o r s p a c e                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetPixelCacheColorspace() returns the colorspace of the pixel cache.
%
%  The format of the GetPixelCacheColorspace() method is:
%
%      Colorspace GetPixelCacheColorspace(const Cache cache)
%
%  A description of each parameter follows:
%
%    o cache: the pixel cache.
%
*/
MagickExport ColorspaceType GetPixelCacheColorspace(const Cache cache)
{
  CacheInfo
    *magick_restrict cache_info;

  assert(cache != (Cache) NULL);
  cache_info=(CacheInfo *) cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",
      cache_info->filename);
  return(cache_info->colorspace);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t P i x e l C a c h e F i l e n a m e                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetPixelCacheFilename() returns the filename associated with the pixel
%  cache.
%
%  The format of the GetPixelCacheFilename() method is:
%
%      const char *GetPixelCacheFilename(const Image *image)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
*/
MagickExport const char *GetPixelCacheFilename(const Image *image)
{
  CacheInfo
    *magick_restrict cache_info;

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  return(cache_info->cache_filename);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t P i x e l C a c h e M e t h o d s                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetPixelCacheMethods() initializes the CacheMethods structure.
%
%  The format of the GetPixelCacheMethods() method is:
%
%      void GetPixelCacheMethods(CacheMethods *cache_methods)
%
%  A description of each parameter follows:
%
%    o cache_methods: Specifies a pointer to a CacheMethods structure.
%
*/
MagickExport void GetPixelCacheMethods(CacheMethods *cache_methods)
{
  assert(cache_methods != (CacheMethods *) NULL);
  (void) memset(cache_methods,0,sizeof(*cache_methods));
  cache_methods->get_virtual_pixel_handler=GetVirtualPixelCache;
  cache_methods->get_virtual_pixels_handler=GetVirtualPixelsCache;
  cache_methods->get_virtual_indexes_from_handler=GetVirtualIndexesFromCache;
  cache_methods->get_one_virtual_pixel_from_handler=GetOneVirtualPixelFromCache;
  cache_methods->get_authentic_pixels_handler=GetAuthenticPixelsCache;
  cache_methods->get_authentic_indexes_from_handler=
    GetAuthenticIndexesFromCache;
  cache_methods->get_authentic_pixels_from_handler=GetAuthenticPixelsFromCache;
  cache_methods->get_one_authentic_pixel_from_handler=
    GetOneAuthenticPixelFromCache;
  cache_methods->queue_authentic_pixels_handler=QueueAuthenticPixelsCache;
  cache_methods->sync_authentic_pixels_handler=SyncAuthenticPixelsCache;
  cache_methods->destroy_pixel_handler=DestroyImagePixelCache;
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t P i x e l C a c h e N e x u s E x t e n t                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetPixelCacheNexusExtent() returns the extent of the pixels associated with
%  the last call to SetPixelCacheNexusPixels() or GetPixelCacheNexusPixels().
%
%  The format of the GetPixelCacheNexusExtent() method is:
%
%      MagickSizeType GetPixelCacheNexusExtent(const Cache cache,
%        NexusInfo *nexus_info)
%
%  A description of each parameter follows:
%
%    o nexus_info: the nexus info.
%
*/
MagickExport MagickSizeType GetPixelCacheNexusExtent(const Cache cache,
  NexusInfo *nexus_info)
{
  CacheInfo
    *magick_restrict cache_info;

  MagickSizeType
    extent;

  assert(cache != NULL);
  cache_info=(CacheInfo *) cache;
  assert(cache_info->signature == MagickCoreSignature);
  extent=(MagickSizeType) nexus_info->region.width*nexus_info->region.height;
  if (extent == 0)
    return((MagickSizeType) cache_info->columns*cache_info->rows);
  return(extent);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t P i x e l C a c h e P i x e l s                                     %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetPixelCachePixels() returns the pixels associated with the specified image.
%
%  The format of the GetPixelCachePixels() method is:
%
%      void *GetPixelCachePixels(Image *image,MagickSizeType *length,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o length: the pixel cache length.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport void *GetPixelCachePixels(Image *image,MagickSizeType *length,
  ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  assert(length != (MagickSizeType *) NULL);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  (void) exception;
  *length=cache_info->length;
  if ((cache_info->type != MemoryCache) && (cache_info->type != MapCache))
    return((void *) NULL);
  return((void *) cache_info->pixels);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t P i x e l C a c h e S t o r a g e C l a s s                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetPixelCacheStorageClass() returns the class type of the pixel cache.
%
%  The format of the GetPixelCacheStorageClass() method is:
%
%      ClassType GetPixelCacheStorageClass(Cache cache)
%
%  A description of each parameter follows:
%
%    o type: GetPixelCacheStorageClass returns DirectClass or PseudoClass.
%
%    o cache: the pixel cache.
%
*/
MagickExport ClassType GetPixelCacheStorageClass(const Cache cache)
{
  CacheInfo
    *magick_restrict cache_info;

  assert(cache != (Cache) NULL);
  cache_info=(CacheInfo *) cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",
      cache_info->filename);
  return(cache_info->storage_class);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t P i x e l C a c h e T i l e S i z e                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetPixelCacheTileSize() returns the pixel cache tile size.
%
%  The format of the GetPixelCacheTileSize() method is:
%
%      void GetPixelCacheTileSize(const Image *image,size_t *width,
%        size_t *height)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o width: the optimize cache tile width in pixels.
%
%    o height: the optimize cache tile height in pixels.
%
*/
MagickExport void GetPixelCacheTileSize(const Image *image,size_t *width,
  size_t *height)
{
  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  *width=2048UL/sizeof(PixelPacket);
  if (GetImagePixelCacheType(image) == DiskCache)
    *width=8192UL/sizeof(PixelPacket);
  *height=(*width);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t P i x e l C a c h e V i r t u a l M e t h o d                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetPixelCacheVirtualMethod() gets the "virtual pixels" method for the
%  pixel cache.  A virtual pixel is any pixel access that is outside the
%  boundaries of the image cache.
%
%  The format of the GetPixelCacheVirtualMethod() method is:
%
%      VirtualPixelMethod GetPixelCacheVirtualMethod(const Image *image)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
*/
MagickExport VirtualPixelMethod GetPixelCacheVirtualMethod(const Image *image)
{
  CacheInfo
    *magick_restrict cache_info;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  return(cache_info->virtual_pixel_method);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t V i r t u a l I n d e x e s F r o m C a c h e                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetVirtualIndexesFromCache() returns the indexes associated with the last
%  call to QueueAuthenticPixelsCache() or GetVirtualPixelCache().
%
%  The format of the GetVirtualIndexesFromCache() method is:
%
%      IndexPacket *GetVirtualIndexesFromCache(const Image *image)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
*/
static const IndexPacket *GetVirtualIndexesFromCache(const Image *image)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  assert(id < (int) cache_info->number_threads);
  return(GetVirtualIndexesFromNexus(cache_info,cache_info->nexus_info[id]));
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t V i r t u a l I n d e x e s F r o m N e x u s                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetVirtualIndexesFromNexus() returns the indexes associated with the
%  specified cache nexus.
%
%  The format of the GetVirtualIndexesFromNexus() method is:
%
%      const IndexPacket *GetVirtualIndexesFromNexus(const Cache cache,
%        NexusInfo *nexus_info)
%
%  A description of each parameter follows:
%
%    o cache: the pixel cache.
%
%    o nexus_info: the cache nexus to return the colormap indexes.
%
*/
MagickExport const IndexPacket *GetVirtualIndexesFromNexus(const Cache cache,
  NexusInfo *nexus_info)
{
  CacheInfo
    *magick_restrict cache_info;

  assert(cache != (Cache) NULL);
  cache_info=(CacheInfo *) cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (cache_info->storage_class == UndefinedClass)
    return((IndexPacket *) NULL);
  return(nexus_info->indexes);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   G e t V i r t u a l I n d e x Q u e u e                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetVirtualIndexQueue() returns the virtual black channel or the
%  colormap indexes associated with the last call to QueueAuthenticPixels() or
%  GetVirtualPixels().  NULL is returned if the black channel or colormap
%  indexes are not available.
%
%  The format of the GetVirtualIndexQueue() method is:
%
%      const IndexPacket *GetVirtualIndexQueue(const Image *image)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
*/
MagickExport const IndexPacket *GetVirtualIndexQueue(const Image *image)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (cache_info->methods.get_virtual_indexes_from_handler !=
       (GetVirtualIndexesFromHandler) NULL)
    {
      const IndexPacket
        *indexes;

      indexes=cache_info->methods.get_virtual_indexes_from_handler(image);
      if (indexes != (IndexPacket *) NULL)
        return(indexes);
    }
  assert(id < (int) cache_info->number_threads);
  return(GetVirtualIndexesFromNexus(cache_info,cache_info->nexus_info[id]));
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t V i r t u a l P i x e l C a c h e N e x u s                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetVirtualPixelCacheNexus() gets virtual pixels from the in-memory or disk
%  pixel cache as defined by the geometry parameters.   A pointer to the pixels
%  is returned if the pixels are transferred, otherwise a NULL is returned.
%
%  The format of the GetVirtualPixelCacheNexus() method is:
%
%      PixelPacket *GetVirtualPixelCacheNexus(const Image *image,
%        const VirtualPixelMethod method,const ssize_t x,const ssize_t y,
%        const size_t columns,const size_t rows,NexusInfo *nexus_info,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o virtual_pixel_method: the virtual pixel method.
%
%    o x,y,columns,rows:  These values define the perimeter of a region of
%      pixels.
%
%    o nexus_info: the cache nexus to acquire.
%
%    o exception: return any errors or warnings in this structure.
%
*/

static ssize_t
  DitherMatrix[64] =
  {
     0,  48,  12,  60,   3,  51,  15,  63,
    32,  16,  44,  28,  35,  19,  47,  31,
     8,  56,   4,  52,  11,  59,   7,  55,
    40,  24,  36,  20,  43,  27,  39,  23,
     2,  50,  14,  62,   1,  49,  13,  61,
    34,  18,  46,  30,  33,  17,  45,  29,
    10,  58,   6,  54,   9,  57,   5,  53,
    42,  26,  38,  22,  41,  25,  37,  21
  };

static inline ssize_t DitherX(const ssize_t x,const size_t columns)
{
  ssize_t
    index;

  index=x+DitherMatrix[x & 0x07]-32L;
  if (index < 0L)
    return(0L);
  if (index >= (ssize_t) columns)
    return((ssize_t) columns-1L);
  return(index);
}

static inline ssize_t DitherY(const ssize_t y,const size_t rows)
{
  ssize_t
    index;

  index=y+DitherMatrix[y & 0x07]-32L;
  if (index < 0L)
    return(0L);
  if (index >= (ssize_t) rows)
    return((ssize_t) rows-1L);
  return(index);
}

static inline ssize_t EdgeX(const ssize_t x,const size_t columns)
{
  if (x < 0L)
    return(0L);
  if (x >= (ssize_t) columns)
    return((ssize_t) (columns-1));
  return(x);
}

static inline ssize_t EdgeY(const ssize_t y,const size_t rows)
{
  if (y < 0L)
    return(0L);
  if (y >= (ssize_t) rows)
    return((ssize_t) (rows-1));
  return(y);
}

static inline MagickBooleanType IsOffsetOverflow(const ssize_t x,
  const ssize_t y)
{             
  if (((y > 0) && (x > (MAGICK_SSIZE_MAX-y))) ||
      ((y < 0) && (x < (MAGICK_SSIZE_MIN-y))))
    return(MagickFalse);
  return(MagickTrue);
}     

static inline ssize_t RandomX(RandomInfo *random_info,const size_t columns)
{
  return((ssize_t) (columns*GetPseudoRandomValue(random_info)));
}

static inline ssize_t RandomY(RandomInfo *random_info,const size_t rows)
{
  return((ssize_t) (rows*GetPseudoRandomValue(random_info)));
}

static inline MagickModulo VirtualPixelModulo(const ssize_t offset,
  const size_t extent)
{
  MagickModulo
    modulo;

  modulo.quotient=offset;
  modulo.remainder=0;
  if (extent != 0)
    {
      modulo.quotient=offset/((ssize_t) extent);
      modulo.remainder=offset % ((ssize_t) extent);
    }
  if ((modulo.remainder != 0) && ((offset ^ ((ssize_t) extent)) < 0))
    {
      modulo.quotient-=1;
      modulo.remainder+=((ssize_t) extent);
    }
  return(modulo);
}

MagickExport const PixelPacket *GetVirtualPixelCacheNexus(const Image *image,
  const VirtualPixelMethod virtual_pixel_method,const ssize_t x,const ssize_t y,
  const size_t columns,const size_t rows,NexusInfo *nexus_info,
  ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  const IndexPacket
    *magick_restrict virtual_indexes;

  const PixelPacket
    *magick_restrict p;

  IndexPacket
    virtual_index,
    *magick_restrict indexes;

  MagickOffsetType
    offset;

  MagickSizeType
    length,
    number_pixels;

  NexusInfo
    *magick_restrict virtual_nexus;

  PixelPacket
    *magick_restrict pixels,
    *magick_restrict q,
    virtual_pixel;

  ssize_t
    u,
    v;

  /*
    Acquire pixels.
  */
  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (cache_info->type == UndefinedCache)
    return((const PixelPacket *) NULL);
#if defined(MAGICKCORE_OPENCL_SUPPORT)
  CopyOpenCLBuffer(cache_info);
#endif
  pixels=SetPixelCacheNexusPixels(cache_info,ReadMode,x,y,columns,rows,
    (image->clip_mask != (Image *) NULL) || (image->mask != (Image *) NULL) ?
    MagickTrue : MagickFalse,nexus_info,exception);
  if (pixels == (PixelPacket *) NULL)
    return((const PixelPacket *) NULL);
  if (IsValidPixelOffset(nexus_info->region.y,cache_info->columns) == MagickFalse)
    return((const PixelPacket *) NULL);
  offset=nexus_info->region.y*(MagickOffsetType) cache_info->columns;
  if (IsOffsetOverflow(offset,nexus_info->region.x) == MagickFalse)
    return((const PixelPacket *) NULL);
  offset+=nexus_info->region.x;
  length=(MagickSizeType) (nexus_info->region.height-1L)*cache_info->columns+
    nexus_info->region.width-1L;
  number_pixels=(MagickSizeType) cache_info->columns*cache_info->rows;
  if ((offset >= 0) && (((MagickSizeType) offset+length) < number_pixels))
    if ((x >= 0) && ((x+(ssize_t) columns) <= (ssize_t) cache_info->columns) &&
        (y >= 0) && ((y+(ssize_t) rows) <= (ssize_t) cache_info->rows))
      {
        MagickBooleanType
          status;

        /*
          Pixel request is inside cache extents.
        */
        if (nexus_info->authentic_pixel_cache != MagickFalse)
          return(pixels);
        status=ReadPixelCachePixels(cache_info,nexus_info,exception);
        if (status == MagickFalse)
          return((const PixelPacket *) NULL);
        if ((cache_info->storage_class == PseudoClass) ||
            (cache_info->colorspace == CMYKColorspace))
          {
            status=ReadPixelCacheIndexes(cache_info,nexus_info,exception);
            if (status == MagickFalse)
              return((const PixelPacket *) NULL);
          }
        return(pixels);
      }
  /*
    Pixel request is outside cache extents.
  */
  virtual_nexus=nexus_info->virtual_nexus;
  q=pixels;
  indexes=nexus_info->indexes;
  switch (virtual_pixel_method)
  {
    case BlackVirtualPixelMethod:
    {
      SetPixelRed(&virtual_pixel,0);
      SetPixelGreen(&virtual_pixel,0);
      SetPixelBlue(&virtual_pixel,0);
      SetPixelOpacity(&virtual_pixel,OpaqueOpacity);
      break;
    }
    case GrayVirtualPixelMethod:
    {
      SetPixelRed(&virtual_pixel,QuantumRange/2);
      SetPixelGreen(&virtual_pixel,QuantumRange/2);
      SetPixelBlue(&virtual_pixel,QuantumRange/2);
      SetPixelOpacity(&virtual_pixel,OpaqueOpacity);
      break;
    }
    case TransparentVirtualPixelMethod:
    {
      SetPixelRed(&virtual_pixel,0);
      SetPixelGreen(&virtual_pixel,0);
      SetPixelBlue(&virtual_pixel,0);
      SetPixelOpacity(&virtual_pixel,TransparentOpacity);
      break;
    }
    case MaskVirtualPixelMethod:
    case WhiteVirtualPixelMethod:
    {
      SetPixelRed(&virtual_pixel,QuantumRange);
      SetPixelGreen(&virtual_pixel,QuantumRange);
      SetPixelBlue(&virtual_pixel,QuantumRange);
      SetPixelOpacity(&virtual_pixel,OpaqueOpacity);
      break;
    }
    default:
    {
      virtual_pixel=image->background_color;
      break;
    }
  }
  virtual_index=(IndexPacket) 0;
  for (v=0; v < (ssize_t) rows; v++)
  {
    ssize_t
      y_offset;

    y_offset=y+v;
    if ((virtual_pixel_method == EdgeVirtualPixelMethod) ||
        (virtual_pixel_method == UndefinedVirtualPixelMethod))
      y_offset=EdgeY(y_offset,cache_info->rows);
    for (u=0; u < (ssize_t) columns; u+=(ssize_t) length)
    {
      ssize_t
        x_offset;

      x_offset=x+u;
      length=(MagickSizeType) MagickMin((ssize_t) cache_info->columns-x_offset,
        (ssize_t) columns-u);
      if (((x_offset < 0) || (x_offset >= (ssize_t) cache_info->columns)) ||
          ((y_offset < 0) || (y_offset >= (ssize_t) cache_info->rows)) ||
          (length == 0))
        {
          MagickModulo
            x_modulo,
            y_modulo;

          /*
            Transfer a single pixel.
          */
          length=(MagickSizeType) 1;
          switch (virtual_pixel_method)
          {
            case BackgroundVirtualPixelMethod:
            case ConstantVirtualPixelMethod:
            case BlackVirtualPixelMethod:
            case GrayVirtualPixelMethod:
            case TransparentVirtualPixelMethod:
            case MaskVirtualPixelMethod:
            case WhiteVirtualPixelMethod:
            {
              p=(&virtual_pixel);
              virtual_indexes=(&virtual_index);
              break;
            }
            case EdgeVirtualPixelMethod:
            default:
            {
              p=GetVirtualPixelCacheNexus(image,virtual_pixel_method,
                EdgeX(x_offset,cache_info->columns),
                EdgeY(y_offset,cache_info->rows),1UL,1UL,virtual_nexus,
                exception);
              virtual_indexes=GetVirtualIndexesFromNexus(cache_info,
                virtual_nexus);
              break;
            }
            case RandomVirtualPixelMethod:
            {
              if (cache_info->random_info == (RandomInfo *) NULL)
                cache_info->random_info=AcquireRandomInfo();
              p=GetVirtualPixelCacheNexus(image,virtual_pixel_method,
                RandomX(cache_info->random_info,cache_info->columns),
                RandomY(cache_info->random_info,cache_info->rows),1UL,1UL,
                virtual_nexus,exception);
              virtual_indexes=GetVirtualIndexesFromNexus(cache_info,
                virtual_nexus);
              break;
            }
            case DitherVirtualPixelMethod:
            {
              p=GetVirtualPixelCacheNexus(image,virtual_pixel_method,
                DitherX(x_offset,cache_info->columns),
                DitherY(y_offset,cache_info->rows),1UL,1UL,virtual_nexus,
                exception);
              virtual_indexes=GetVirtualIndexesFromNexus(cache_info,
                virtual_nexus);
              break;
            }
            case TileVirtualPixelMethod:
            {
              x_modulo=VirtualPixelModulo(x_offset,cache_info->columns);
              y_modulo=VirtualPixelModulo(y_offset,cache_info->rows);
              p=GetVirtualPixelCacheNexus(image,virtual_pixel_method,
                x_modulo.remainder,y_modulo.remainder,1UL,1UL,virtual_nexus,
                exception);
              virtual_indexes=GetVirtualIndexesFromNexus(cache_info,
                virtual_nexus);
              break;
            }
            case MirrorVirtualPixelMethod:
            {
              x_modulo=VirtualPixelModulo(x_offset,cache_info->columns);
              if ((x_modulo.quotient & 0x01) == 1L)
                x_modulo.remainder=(ssize_t) cache_info->columns-
                  x_modulo.remainder-1L;
              y_modulo=VirtualPixelModulo(y_offset,cache_info->rows);
              if ((y_modulo.quotient & 0x01) == 1L)
                y_modulo.remainder=(ssize_t) cache_info->rows-
                  y_modulo.remainder-1L;
              p=GetVirtualPixelCacheNexus(image,virtual_pixel_method,
                x_modulo.remainder,y_modulo.remainder,1UL,1UL,virtual_nexus,
                exception);
              virtual_indexes=GetVirtualIndexesFromNexus(cache_info,
                virtual_nexus);
              break;
            }
            case CheckerTileVirtualPixelMethod:
            {
              x_modulo=VirtualPixelModulo(x_offset,cache_info->columns);
              y_modulo=VirtualPixelModulo(y_offset,cache_info->rows);
              if (((x_modulo.quotient ^ y_modulo.quotient) & 0x01) != 0L)
                {
                  p=(&virtual_pixel);
                  virtual_indexes=(&virtual_index);
                  break;
                }
              p=GetVirtualPixelCacheNexus(image,virtual_pixel_method,
                x_modulo.remainder,y_modulo.remainder,1UL,1UL,virtual_nexus,
                exception);
              virtual_indexes=GetVirtualIndexesFromNexus(cache_info,
                virtual_nexus);
              break;
            }
            case HorizontalTileVirtualPixelMethod:
            {
              if ((y_offset < 0) || (y_offset >= (ssize_t) cache_info->rows))
                {
                  p=(&virtual_pixel);
                  virtual_indexes=(&virtual_index);
                  break;
                }
              x_modulo=VirtualPixelModulo(x_offset,cache_info->columns);
              y_modulo=VirtualPixelModulo(y_offset,cache_info->rows);
              p=GetVirtualPixelCacheNexus(image,virtual_pixel_method,
                x_modulo.remainder,y_modulo.remainder,1UL,1UL,virtual_nexus,
                exception);
              virtual_indexes=GetVirtualIndexesFromNexus(cache_info,
                virtual_nexus);
              break;
            }
            case VerticalTileVirtualPixelMethod:
            {
              if ((x_offset < 0) || (x_offset >= (ssize_t) cache_info->columns))
                {
                  p=(&virtual_pixel);
                  virtual_indexes=(&virtual_index);
                  break;
                }
              x_modulo=VirtualPixelModulo(x_offset,cache_info->columns);
              y_modulo=VirtualPixelModulo(y_offset,cache_info->rows);
              p=GetVirtualPixelCacheNexus(image,virtual_pixel_method,
                x_modulo.remainder,y_modulo.remainder,1UL,1UL,virtual_nexus,
                exception);
              virtual_indexes=GetVirtualIndexesFromNexus(cache_info,
                virtual_nexus);
              break;
            }
            case HorizontalTileEdgeVirtualPixelMethod:
            {
              x_modulo=VirtualPixelModulo(x_offset,cache_info->columns);
              p=GetVirtualPixelCacheNexus(image,virtual_pixel_method,
                x_modulo.remainder,EdgeY(y_offset,cache_info->rows),1UL,1UL,
                virtual_nexus,exception);
              virtual_indexes=GetVirtualIndexesFromNexus(cache_info,
                virtual_nexus);
              break;
            }
            case VerticalTileEdgeVirtualPixelMethod:
            {
              y_modulo=VirtualPixelModulo(y_offset,cache_info->rows);
              p=GetVirtualPixelCacheNexus(image,virtual_pixel_method,
                EdgeX(x_offset,cache_info->columns),y_modulo.remainder,1UL,1UL,
                virtual_nexus,exception);
              virtual_indexes=GetVirtualIndexesFromNexus(cache_info,
                virtual_nexus);
              break;
            }
          }
          if (p == (const PixelPacket *) NULL)
            break;
          *q++=(*p);
          if ((indexes != (IndexPacket *) NULL) &&
              (virtual_indexes != (const IndexPacket *) NULL))
            *indexes++=(*virtual_indexes);
          continue;
        }
      /*
        Transfer a run of pixels.
      */
      p=GetVirtualPixelCacheNexus(image,virtual_pixel_method,x_offset,y_offset,
        (size_t) length,1UL,virtual_nexus,exception);
      if (p == (const PixelPacket *) NULL)
        break;
      virtual_indexes=GetVirtualIndexesFromNexus(cache_info,virtual_nexus);
      (void) memcpy(q,p,(size_t) length*sizeof(*p));
      q+=(ptrdiff_t) length;
      if ((indexes != (IndexPacket *) NULL) &&
          (virtual_indexes != (const IndexPacket *) NULL))
        {
          (void) memcpy(indexes,virtual_indexes,(size_t) length*
            sizeof(*virtual_indexes));
          indexes+=length;
        }
    }
    if (u < (ssize_t) columns)
      break;
  }
  /*
    Free resources.
  */
  if (v < (ssize_t) rows)
    return((const PixelPacket *) NULL);
  return(pixels);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t V i r t u a l P i x e l C a c h e                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetVirtualPixelCache() get virtual pixels from the in-memory or disk pixel
%  cache as defined by the geometry parameters.   A pointer to the pixels
%  is returned if the pixels are transferred, otherwise a NULL is returned.
%
%  The format of the GetVirtualPixelCache() method is:
%
%      const PixelPacket *GetVirtualPixelCache(const Image *image,
%        const VirtualPixelMethod virtual_pixel_method,const ssize_t x,
%        const ssize_t y,const size_t columns,const size_t rows,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o virtual_pixel_method: the virtual pixel method.
%
%    o x,y,columns,rows:  These values define the perimeter of a region of
%      pixels.
%
%    o exception: return any errors or warnings in this structure.
%
*/
static const PixelPacket *GetVirtualPixelCache(const Image *image,
  const VirtualPixelMethod virtual_pixel_method,const ssize_t x,const ssize_t y,
  const size_t columns,const size_t rows,ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  assert(id < (int) cache_info->number_threads);
  return(GetVirtualPixelCacheNexus(image,virtual_pixel_method,x,y,columns,rows,
    cache_info->nexus_info[id],exception));
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   G e t V i r t u a l P i x e l Q u e u e                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetVirtualPixelQueue() returns the virtual pixels associated with the
%  last call to QueueAuthenticPixels() or GetVirtualPixels().
%
%  The format of the GetVirtualPixelQueue() method is:
%
%      const PixelPacket *GetVirtualPixelQueue(const Image image)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
*/
MagickExport const PixelPacket *GetVirtualPixelQueue(const Image *image)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (cache_info->methods.get_virtual_pixels_handler !=
       (GetVirtualPixelsHandler) NULL)
    return(cache_info->methods.get_virtual_pixels_handler(image));
  assert(id < (int) cache_info->number_threads);
  return(GetVirtualPixelsNexus(cache_info,cache_info->nexus_info[id]));
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   G e t V i r t u a l P i x e l s                                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetVirtualPixels() returns an immutable pixel region. If the
%  region is successfully accessed, a pointer to it is returned, otherwise
%  NULL is returned.  The returned pointer may point to a temporary working
%  copy of the pixels or it may point to the original pixels in memory.
%  Performance is maximized if the selected region is part of one row, or one
%  or more full rows, since there is opportunity to access the pixels in-place
%  (without a copy) if the image is in memory, or in a memory-mapped file.  The
%  returned pointer must *never* be deallocated by the user.
%
%  Pixels accessed via the returned pointer represent a simple array of type
%  PixelPacket.  If the image type is CMYK or the storage class is PseudoClass,
%  call GetAuthenticIndexQueue() after invoking GetAuthenticPixels() to access
%  the black color component or to obtain the colormap indexes (of type
%  IndexPacket) corresponding to the region.
%
%  If you plan to modify the pixels, use GetAuthenticPixels() instead.
%
%  Note, the GetVirtualPixels() and GetAuthenticPixels() methods are not thread-
%  safe.  In a threaded environment, use GetCacheViewVirtualPixels() or
%  GetCacheViewAuthenticPixels() instead.
%
%  The format of the GetVirtualPixels() method is:
%
%      const PixelPacket *GetVirtualPixels(const Image *image,const ssize_t x,
%        const ssize_t y,const size_t columns,const size_t rows,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o x,y,columns,rows:  These values define the perimeter of a region of
%      pixels.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport const PixelPacket *GetVirtualPixels(const Image *image,
  const ssize_t x,const ssize_t y,const size_t columns,const size_t rows,
  ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (cache_info->methods.get_virtual_pixel_handler !=
       (GetVirtualPixelHandler) NULL)
    return(cache_info->methods.get_virtual_pixel_handler(image,
      GetPixelCacheVirtualMethod(image),x,y,columns,rows,exception));
  assert(id < (int) cache_info->number_threads);
  return(GetVirtualPixelCacheNexus(image,GetPixelCacheVirtualMethod(image),x,y,
    columns,rows,cache_info->nexus_info[id],exception));
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t V i r t u a l P i x e l s F r o m C a c h e                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetVirtualPixelsCache() returns the pixels associated with the last call
%  to QueueAuthenticPixelsCache() or GetVirtualPixelCache().
%
%  The format of the GetVirtualPixelsCache() method is:
%
%      PixelPacket *GetVirtualPixelsCache(const Image *image)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
*/
static const PixelPacket *GetVirtualPixelsCache(const Image *image)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  assert(id < (int) cache_info->number_threads);
  return(GetVirtualPixelsNexus(image->cache,cache_info->nexus_info[id]));
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t V i r t u a l P i x e l s N e x u s                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetVirtualPixelsNexus() returns the pixels associated with the specified
%  cache nexus.
%
%  The format of the GetVirtualPixelsNexus() method is:
%
%      const IndexPacket *GetVirtualPixelsNexus(const Cache cache,
%        NexusInfo *nexus_info)
%
%  A description of each parameter follows:
%
%    o cache: the pixel cache.
%
%    o nexus_info: the cache nexus to return the colormap pixels.
%
*/
MagickExport const PixelPacket *GetVirtualPixelsNexus(const Cache cache,
  NexusInfo *nexus_info)
{
  CacheInfo
    *magick_restrict cache_info;

  assert(cache != (Cache) NULL);
  cache_info=(CacheInfo *) cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (cache_info->storage_class == UndefinedClass)
    return((PixelPacket *) NULL);
  return((const PixelPacket *) nexus_info->pixels);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   M a s k P i x e l C a c h e N e x u s                                     %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  MaskPixelCacheNexus() masks the cache nexus as defined by the image mask.
%  The method returns MagickTrue if the pixel region is masked, otherwise
%  MagickFalse.
%
%  The format of the MaskPixelCacheNexus() method is:
%
%      MagickBooleanType MaskPixelCacheNexus(Image *image,
%        NexusInfo *nexus_info,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o nexus_info: the cache nexus to clip.
%
%    o exception: return any errors or warnings in this structure.
%
*/

static inline void ApplyPixelCompositeMask(const MagickPixelPacket *p,
  const MagickRealType alpha,const MagickPixelPacket *q,
  const MagickRealType beta,MagickPixelPacket *composite)
{
  double
    gamma;

  if (fabs((double) alpha-(double) TransparentOpacity) < MagickEpsilon)
    {
      *composite=(*q);
      return;
    }
  gamma=1.0-QuantumScale*QuantumScale*alpha*beta;
  gamma=MagickSafeReciprocal(gamma);
  composite->red=gamma*MagickOver_(p->red,alpha,q->red,beta);
  composite->green=gamma*MagickOver_(p->green,alpha,q->green,beta);
  composite->blue=gamma*MagickOver_(p->blue,alpha,q->blue,beta);
  if ((p->colorspace == CMYKColorspace) && (q->colorspace == CMYKColorspace))
    composite->index=gamma*MagickOver_(p->index,alpha,q->index,beta);
}

static MagickBooleanType MaskPixelCacheNexus(Image *image,NexusInfo *nexus_info,
  ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  const PixelPacket
    *magick_restrict r;

  IndexPacket
    *magick_restrict nexus_indexes,
    *magick_restrict indexes;

  MagickOffsetType
    n;

  MagickPixelPacket
    alpha,
    beta;

  NexusInfo
    **magick_restrict mask_nexus;

  PixelPacket
    *magick_restrict p,
    *magick_restrict q;

  ssize_t
    y;

  /*
    Apply composite mask.
  */
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  if ((image->mask == (Image *) NULL) || (image->storage_class == PseudoClass))
    return(MagickTrue);
  if ((nexus_info->region.width == 0) || (nexus_info->region.height == 0))
    return(MagickTrue);
  cache_info=(CacheInfo *) image->cache;
  if (cache_info == (Cache) NULL)
    return(MagickFalse);
  mask_nexus=AcquirePixelCacheNexus(1);
  p=GetAuthenticPixelCacheNexus(image,nexus_info->region.x,nexus_info->region.y,    nexus_info->region.width,nexus_info->region.height,
    nexus_info->virtual_nexus,exception);
  indexes=nexus_info->virtual_nexus->indexes;
  q=nexus_info->pixels;
  nexus_indexes=nexus_info->indexes;
  r=GetVirtualPixelCacheNexus(image->mask,MaskVirtualPixelMethod,
    nexus_info->region.x,nexus_info->region.y,nexus_info->region.width,
    nexus_info->region.height,mask_nexus[0],&image->exception);
  if ((p == (PixelPacket *) NULL) || (q == (PixelPacket *) NULL) ||
      (r == (const PixelPacket *) NULL))
    return(MagickFalse);
  n=0;
  GetMagickPixelPacket(image,&alpha);
  GetMagickPixelPacket(image,&beta);
  for (y=0; y < (ssize_t) nexus_info->region.height; y++)
  {
    ssize_t
      x;

    for (x=0; x < (ssize_t) nexus_info->region.width; x++)
    {
      SetMagickPixelPacket(image,p,indexes+n,&alpha);
      SetMagickPixelPacket(image,q,nexus_indexes+n,&beta);
      ApplyPixelCompositeMask(&beta,GetPixelIntensity(image,r),&alpha,
        alpha.opacity,&beta);
      SetPixelRed(q,ClampToQuantum(beta.red));
      SetPixelGreen(q,ClampToQuantum(beta.green));
      SetPixelBlue(q,ClampToQuantum(beta.blue));
      SetPixelOpacity(q,ClampToQuantum(beta.opacity));
      if (cache_info->active_index_channel != MagickFalse)
        SetPixelIndex(nexus_indexes+n,GetPixelIndex(indexes+n));
      p++;
      q++;
      r++;
      n++;
    }
  }
  mask_nexus=DestroyPixelCacheNexus(mask_nexus,1);
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   O p e n P i x e l C a c h e                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  OpenPixelCache() allocates the pixel cache.  This includes defining the cache
%  dimensions, allocating space for the image pixels and optionally the
%  colormap indexes, and memory mapping the cache if it is disk based.  The
%  cache nexus array is initialized as well.
%
%  The format of the OpenPixelCache() method is:
%
%      MagickBooleanType OpenPixelCache(Image *image,const MapMode mode,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o mode: ReadMode, WriteMode, or IOMode.
%
%    o exception: return any errors or warnings in this structure.
%
*/

static MagickBooleanType OpenPixelCacheOnDisk(CacheInfo *cache_info,
  const MapMode mode)
{
  int
    file;

  /*
    Open pixel cache on disk.
  */
  if ((cache_info->file != -1) && (cache_info->disk_mode == mode))
    return(MagickTrue);  /* cache already open and in the proper mode */
  if (*cache_info->cache_filename == '\0')
    file=AcquireUniqueFileResource(cache_info->cache_filename);
  else
    switch (mode)
    {
      case ReadMode:
      {
        file=open_utf8(cache_info->cache_filename,O_RDONLY | O_BINARY,0);
        break;
      }
      case WriteMode:
      {
        file=open_utf8(cache_info->cache_filename,O_WRONLY | O_CREAT |
          O_BINARY | O_EXCL,S_MODE);
        if (file == -1)
          file=open_utf8(cache_info->cache_filename,O_WRONLY | O_BINARY,S_MODE);
        break;
      }
      case IOMode:
      default:
      {
        file=open_utf8(cache_info->cache_filename,O_RDWR | O_CREAT | O_BINARY |
          O_EXCL,S_MODE);
        if (file == -1)
          file=open_utf8(cache_info->cache_filename,O_RDWR | O_BINARY,S_MODE);
        break;
      }
    }
  if (file == -1)
    return(MagickFalse);
  (void) AcquireMagickResource(FileResource,1);
  if (cache_info->file != -1)
    (void) ClosePixelCacheOnDisk(cache_info);
  cache_info->file=file;
  cache_info->disk_mode=mode;
  return(MagickTrue);
}

static inline MagickOffsetType WritePixelCacheRegion(
  const CacheInfo *magick_restrict cache_info,const MagickOffsetType offset,
  const MagickSizeType length,const unsigned char *magick_restrict buffer)
{
  MagickOffsetType
    i;

  ssize_t
    count = 0;

#if !defined(MAGICKCORE_HAVE_PWRITE)
  if (lseek(cache_info->file,offset,SEEK_SET) < 0)
    return((MagickOffsetType) -1);
#endif
  for (i=0; i < (MagickOffsetType) length; i+=count)
  {
#if !defined(MAGICKCORE_HAVE_PWRITE)
    count=write(cache_info->file,buffer+i,(size_t) MagickMin(length-
      (MagickSizeType) i,MagickMaxBufferExtent));
#else
    count=pwrite(cache_info->file,buffer+i,(size_t) MagickMin(length-
      (MagickSizeType) i,MagickMaxBufferExtent),offset+i);
#endif
    if (count <= 0)
      {
        count=0;
        if (errno != EINTR)
          break;
      }
  }
  return(i);
}

static MagickBooleanType SetPixelCacheExtent(Image *image,MagickSizeType length)
{
  CacheInfo
    *magick_restrict cache_info;

  MagickOffsetType
    offset;

  cache_info=(CacheInfo *) image->cache;
  if (cache_info->debug != MagickFalse)
    {
      char
        format[MaxTextExtent],
        message[MaxTextExtent];

      (void) FormatMagickSize(length,MagickFalse,format);
      (void) FormatLocaleString(message,MaxTextExtent,
        "extend %s (%s[%d], disk, %s)",cache_info->filename,
        cache_info->cache_filename,cache_info->file,format);
      (void) LogMagickEvent(CacheEvent,GetMagickModule(),"%s",message);
    }
  offset=(MagickOffsetType) lseek(cache_info->file,0,SEEK_END);
  if (offset < 0)
    return(MagickFalse);
  if ((MagickSizeType) offset < length)
    {
      MagickOffsetType
        count,
        extent;

      extent=(MagickOffsetType) length-1;
      count=WritePixelCacheRegion(cache_info,extent,1,(const unsigned char *)
        "");
      if (count != 1)
        return(MagickFalse);
#if defined(MAGICKCORE_HAVE_POSIX_FALLOCATE)
      if (cache_info->synchronize != MagickFalse)
        if (posix_fallocate(cache_info->file,offset+1,extent-offset) != 0)
          return(MagickFalse);
#endif
    }
  offset=(MagickOffsetType) lseek(cache_info->file,0,SEEK_SET);
  if (offset < 0)
    return(MagickFalse);
  return(MagickTrue);
}

static MagickBooleanType OpenPixelCache(Image *image,const MapMode mode,
  ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info,
    source_info;

  char
    format[MaxTextExtent],
    message[MaxTextExtent];

  const char
    *hosts,
    *type;

  MagickSizeType
    length,
    number_pixels;

  MagickStatusType
    status;

  size_t
    columns,
    packet_size;

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  if (cache_anonymous_memory < 0)
    {
      char
        *value;

      /*
        Does the security policy require anonymous mapping for pixel cache?
      */
      cache_anonymous_memory=0;
      value=GetPolicyValue("pixel-cache-memory");
      if (value == (char *) NULL)
        value=GetPolicyValue("cache:memory-map");
      if (LocaleCompare(value,"anonymous") == 0)
        {
#if defined(MAGICKCORE_HAVE_MMAP) && defined(MAP_ANONYMOUS)
          cache_anonymous_memory=1;
#else
          (void) ThrowMagickException(exception,GetMagickModule(),
            MissingDelegateError,"DelegateLibrarySupportNotBuiltIn",
            "'%s' (policy requires anonymous memory mapping)",image->filename);
#endif
        }
      value=DestroyString(value);
    }
  if ((image->columns == 0) || (image->rows == 0))
    ThrowBinaryException(CacheError,"NoPixelsDefinedInCache",image->filename);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (((MagickSizeType) image->columns > cache_info->width_limit) ||
      ((MagickSizeType) image->rows > cache_info->height_limit))
    ThrowBinaryException(ImageError,"WidthOrHeightExceedsLimit",
      image->filename);
  if (GetMagickResourceLimit(ListLengthResource) != MagickResourceInfinity)
    {
      length=GetImageListLength(image);
      if (AcquireMagickResource(ListLengthResource,length) == MagickFalse)
        ThrowBinaryException(ResourceLimitError,"ListLengthExceedsLimit",
          image->filename);
    }
  source_info=(*cache_info);
  source_info.file=(-1);
  (void) FormatLocaleString(cache_info->filename,MaxTextExtent,"%s[%.20g]",
    image->filename,(double) image->scene);
  cache_info->storage_class=image->storage_class;
  cache_info->colorspace=image->colorspace;
  cache_info->rows=image->rows;
  cache_info->columns=image->columns;
  cache_info->channels=image->channels;
  cache_info->active_index_channel=((image->storage_class == PseudoClass) ||
    (image->colorspace == CMYKColorspace)) ? MagickTrue : MagickFalse;
  cache_info->mode=mode;
  number_pixels=(MagickSizeType) cache_info->columns*cache_info->rows;
  packet_size=sizeof(PixelPacket);
  if (cache_info->active_index_channel != MagickFalse)
    packet_size+=sizeof(IndexPacket);
  length=number_pixels*packet_size;
  columns=(size_t) (length/cache_info->rows/packet_size);
  if ((cache_info->columns != columns) || ((ssize_t) cache_info->columns < 0) ||
      ((ssize_t) cache_info->rows < 0))
    ThrowBinaryException(ResourceLimitError,"PixelCacheAllocationFailed",
      image->filename);
  cache_info->length=length;
  if (image->ping != MagickFalse)
    {
      cache_info->type=PingCache;
      return(MagickTrue);
    }
  status=AcquireMagickResource(AreaResource,(MagickSizeType)
    cache_info->columns*cache_info->rows);
  if (cache_info->mode == PersistMode)
    status=MagickFalse;
  length=number_pixels*(sizeof(PixelPacket)+sizeof(IndexPacket));
  if ((status != MagickFalse) &&
      (length == (MagickSizeType) ((size_t) length)) &&
      ((cache_info->type == UndefinedCache) ||
       (cache_info->type == MemoryCache)))
    {
      status=AcquireMagickResource(MemoryResource,cache_info->length);
      if (status != MagickFalse)
        {
          status=MagickTrue;
          if (cache_anonymous_memory <= 0)
            {
              cache_info->mapped=MagickFalse;
              cache_info->pixels=(PixelPacket *) MagickAssumeAligned(
                AcquireAlignedMemory(1,(size_t) cache_info->length));
            }
          else
            {
              cache_info->mapped=MagickTrue;
              cache_info->pixels=(PixelPacket *) MapBlob(-1,IOMode,0,(size_t)
                cache_info->length);
            }
          if (cache_info->pixels == (PixelPacket *) NULL)
            {
              cache_info->mapped=source_info.mapped;
              cache_info->pixels=source_info.pixels;
            }
          else
            {
              /*
                Create memory pixel cache.
              */
              cache_info->colorspace=image->colorspace;
              cache_info->type=MemoryCache;
              cache_info->indexes=(IndexPacket *) NULL;
              if (cache_info->active_index_channel != MagickFalse)
                cache_info->indexes=(IndexPacket *) (cache_info->pixels+
                  number_pixels);
              if ((source_info.storage_class != UndefinedClass) &&
                  (mode != ReadMode))
                {
                  status&=ClonePixelCacheRepository(cache_info,&source_info,
                    exception);
                  RelinquishPixelCachePixels(&source_info);
                }
              if (cache_info->debug != MagickFalse)
                {
                  (void) FormatMagickSize(cache_info->length,MagickTrue,format);
                  type=CommandOptionToMnemonic(MagickCacheOptions,(ssize_t)
                    cache_info->type);
                  (void) FormatLocaleString(message,MaxTextExtent,
                    "open %s (%s %s, %.20gx%.20g %s)",cache_info->filename,
                    cache_info->mapped != MagickFalse ? "Anonymous" : "Heap",
                    type,(double) cache_info->columns,(double) cache_info->rows,
                    format);
                  (void) LogMagickEvent(CacheEvent,GetMagickModule(),"%s",
                    message);
                }
              cache_info->storage_class=image->storage_class;
              if (status == 0)
                {
                  if ((source_info.storage_class != UndefinedClass) &&
                      (mode != ReadMode))
                    RelinquishPixelCachePixels(&source_info);
                  cache_info->type=UndefinedCache;
                  return(MagickFalse);
                }
              return(MagickTrue);
            }
        }
    }
  status=AcquireMagickResource(DiskResource,cache_info->length);
  hosts=(const char *) GetImageRegistry(StringRegistryType,"cache:hosts",
    exception);
  if ((status == MagickFalse) && (hosts != (const char *) NULL))
    {
      DistributeCacheInfo
        *server_info;

      /*
        Distribute the pixel cache to a remote server.
      */
      server_info=AcquireDistributeCacheInfo(exception);
      if (server_info != (DistributeCacheInfo *) NULL)
        {
          status=OpenDistributePixelCache(server_info,image);
          if (status == MagickFalse)
            {
              ThrowFileException(exception,CacheError,"UnableToOpenPixelCache",
                GetDistributeCacheHostname(server_info));
              server_info=DestroyDistributeCacheInfo(server_info);
            }
          else
            {
              /*
                Create a distributed pixel cache.
              */
              status=MagickTrue;
              cache_info->type=DistributedCache;
              cache_info->storage_class=image->storage_class;
              cache_info->colorspace=image->colorspace;
              cache_info->server_info=server_info;
              (void) FormatLocaleString(cache_info->cache_filename,
                MaxTextExtent,"%s:%d",GetDistributeCacheHostname(
                (DistributeCacheInfo *) cache_info->server_info),
                GetDistributeCachePort((DistributeCacheInfo *)
                cache_info->server_info));
              if ((source_info.storage_class != UndefinedClass) &&
                  (mode != ReadMode))
                {
                  status=ClonePixelCacheRepository(cache_info,&source_info,
                    exception);
                  RelinquishPixelCachePixels(&source_info);
                }
              if (cache_info->debug != MagickFalse)
                {
                  (void) FormatMagickSize(cache_info->length,MagickFalse,
                    format);
                  type=CommandOptionToMnemonic(MagickCacheOptions,(ssize_t)
                    cache_info->type);
                  (void) FormatLocaleString(message,MaxTextExtent,
                    "open %s (%s[%d], %s, %.20gx%.20g %s)",cache_info->filename,
                    cache_info->cache_filename,GetDistributeCacheFile(
                    (DistributeCacheInfo *) cache_info->server_info),type,
                    (double) cache_info->columns,(double) cache_info->rows,
                    format);
                  (void) LogMagickEvent(CacheEvent,GetMagickModule(),"%s",
                    message);
                }
              if (status == 0)
                {
                  if ((source_info.storage_class != UndefinedClass) &&
                      (mode != ReadMode))
                    RelinquishPixelCachePixels(&source_info);
                  cache_info->type=UndefinedCache;
                  return(MagickFalse);
                }
              return(MagickTrue);
            }
        }
      if ((source_info.storage_class != UndefinedClass) && (mode != ReadMode))
        RelinquishPixelCachePixels(&source_info);
      cache_info->type=UndefinedCache;
      (void) ThrowMagickException(exception,GetMagickModule(),CacheError,
        "CacheResourcesExhausted","`%s'",image->filename);
      return(MagickFalse);
    }
  /*
    Create pixel cache on disk.
  */
  if (status == MagickFalse)
    {
      if ((source_info.storage_class != UndefinedClass) && (mode != ReadMode))
        RelinquishPixelCachePixels(&source_info);
      cache_info->type=UndefinedCache;
      (void) ThrowMagickException(exception,GetMagickModule(),CacheError,
        "CacheResourcesExhausted","`%s'",image->filename);
      return(MagickFalse);
    }
  if ((source_info.storage_class != UndefinedClass) && (mode != ReadMode) &&
      (cache_info->mode != PersistMode))
    {
      (void) ClosePixelCacheOnDisk(cache_info);
      *cache_info->cache_filename='\0';
    }
  if (OpenPixelCacheOnDisk(cache_info,mode) == MagickFalse)
    {
      if ((source_info.storage_class != UndefinedClass) && (mode != ReadMode))
        RelinquishPixelCachePixels(&source_info);
      cache_info->type=UndefinedCache;
      ThrowFileException(exception,CacheError,"UnableToOpenPixelCache",
        image->filename);
      return(MagickFalse);
    }
  status=SetPixelCacheExtent(image,(MagickSizeType) cache_info->offset+
    cache_info->length);
  if (status == MagickFalse)
    {
      if ((source_info.storage_class != UndefinedClass) && (mode != ReadMode))
        RelinquishPixelCachePixels(&source_info);
      cache_info->type=UndefinedCache;
      ThrowFileException(exception,CacheError,"UnableToExtendCache",
        image->filename);
      return(MagickFalse);
    }
  cache_info->storage_class=image->storage_class;
  cache_info->colorspace=image->colorspace;
  cache_info->type=DiskCache;
  length=number_pixels*(sizeof(PixelPacket)+sizeof(IndexPacket));
  if (length == (MagickSizeType) ((size_t) length))
    {
      status=AcquireMagickResource(MapResource,cache_info->length);
      if (status != MagickFalse)
        {
          cache_info->pixels=(PixelPacket *) MapBlob(cache_info->file,mode,
            cache_info->offset,(size_t) cache_info->length);
          if (cache_info->pixels == (PixelPacket *) NULL)
            {
              cache_info->mapped=source_info.mapped;
              cache_info->pixels=source_info.pixels;
              RelinquishMagickResource(MapResource,cache_info->length);
            }
          else
            {
              /*
                Create file-backed memory-mapped pixel cache.
              */
              (void) ClosePixelCacheOnDisk(cache_info);
              cache_info->type=MapCache;
              cache_info->mapped=MagickTrue;
              cache_info->indexes=(IndexPacket *) NULL;
              if (cache_info->active_index_channel != MagickFalse)
                cache_info->indexes=(IndexPacket *) (cache_info->pixels+
                  number_pixels);
              if ((source_info.storage_class != UndefinedClass) &&
                  (mode != ReadMode))
                {
                  status=ClonePixelCacheRepository(cache_info,&source_info,
                    exception);
                  RelinquishPixelCachePixels(&source_info);
                }
              if (cache_info->debug != MagickFalse)
                {
                  (void) FormatMagickSize(cache_info->length,MagickTrue,format);
                  type=CommandOptionToMnemonic(MagickCacheOptions,(ssize_t)
                    cache_info->type);
                  (void) FormatLocaleString(message,MaxTextExtent,
                     "open %s (%s[%d], %s, %.20gx%.20g %s)",
                     cache_info->filename,cache_info->cache_filename,
                     cache_info->file,type,(double) cache_info->columns,
                     (double) cache_info->rows,format);
                  (void) LogMagickEvent(CacheEvent,GetMagickModule(),"%s",
                     message);
                }
              if (status == 0)
                {
                  if ((source_info.storage_class != UndefinedClass) &&
                      (mode != ReadMode))
                    RelinquishPixelCachePixels(&source_info);
                  cache_info->type=UndefinedCache;
                  return(MagickFalse);
                }
              return(MagickTrue);
            }
        }
    }
  status=MagickTrue;
  if ((source_info.storage_class != UndefinedClass) && (mode != ReadMode))
    {
      status=ClonePixelCacheRepository(cache_info,&source_info,exception);
      RelinquishPixelCachePixels(&source_info);
    }
  if (cache_info->debug != MagickFalse)
    {
      (void) FormatMagickSize(cache_info->length,MagickFalse,format);
      type=CommandOptionToMnemonic(MagickCacheOptions,(ssize_t)
        cache_info->type);
      (void) FormatLocaleString(message,MaxTextExtent,
        "open %s (%s[%d], %s, %.20gx%.20g %s)",cache_info->filename,
        cache_info->cache_filename,cache_info->file,type,(double)
        cache_info->columns,(double) cache_info->rows,format);
      (void) LogMagickEvent(CacheEvent,GetMagickModule(),"%s",message);
    }
  if (status == 0)
    {
      cache_info->type=UndefinedCache;
      return(MagickFalse);
    }
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   P e r s i s t P i x e l C a c h e                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  PersistPixelCache() attaches to or initializes a persistent pixel cache.  A
%  persistent pixel cache is one that resides on disk and is not destroyed
%  when the program exits.
%
%  The format of the PersistPixelCache() method is:
%
%      MagickBooleanType PersistPixelCache(Image *image,const char *filename,
%        const MagickBooleanType attach,MagickOffsetType *offset,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o filename: the persistent pixel cache filename.
%
%    o attach: A value other than zero initializes the persistent pixel cache.
%
%    o initialize: A value other than zero initializes the persistent pixel
%      cache.
%
%    o offset: the offset in the persistent cache to store pixels.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport MagickBooleanType PersistPixelCache(Image *image,
  const char *filename,const MagickBooleanType attach,MagickOffsetType *offset,
  ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info,
    *magick_restrict clone_info;

  MagickBooleanType
    status;

  ssize_t
    page_size;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(image->cache != (void *) NULL);
  assert(filename != (const char *) NULL);
  assert(offset != (MagickOffsetType *) NULL);
  page_size=GetMagickPageSize();
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
#if defined(MAGICKCORE_OPENCL_SUPPORT)
  CopyOpenCLBuffer(cache_info);
#endif
  if (attach != MagickFalse)
    {
      /*
        Attach existing persistent pixel cache.
      */
      if (cache_info->debug != MagickFalse)
        (void) LogMagickEvent(CacheEvent,GetMagickModule(),
          "attach persistent cache");
      (void) CopyMagickString(cache_info->cache_filename,filename,
        MaxTextExtent);
      cache_info->type=MapCache;
      cache_info->offset=(*offset);
      if (OpenPixelCache(image,ReadMode,exception) == MagickFalse)
        return(MagickFalse);
      *offset=(*offset+(MagickOffsetType) cache_info->length+page_size-
        ((MagickOffsetType) cache_info->length % page_size));
      return(MagickTrue);
    }
  /*
    Clone persistent pixel cache.
  */
  status=AcquireMagickResource(DiskResource,cache_info->length);
  if (status == MagickFalse)
    {
      (void) ThrowMagickException(exception,GetMagickModule(),CacheError,
        "CacheResourcesExhausted","`%s'",image->filename);
      return(MagickFalse);
    }
  clone_info=(CacheInfo *) ClonePixelCache(cache_info);
  clone_info->type=DiskCache;
  (void) CopyMagickString(clone_info->cache_filename,filename,MaxTextExtent);
  clone_info->file=(-1);
  clone_info->storage_class=cache_info->storage_class;
  clone_info->colorspace=cache_info->colorspace;
  clone_info->columns=cache_info->columns;
  clone_info->rows=cache_info->rows;
  clone_info->active_index_channel=cache_info->active_index_channel;
  clone_info->mode=PersistMode;
  clone_info->length=cache_info->length;
  clone_info->channels=cache_info->channels;
  clone_info->offset=(*offset);
  status=OpenPixelCacheOnDisk(clone_info,WriteMode);
  if (status != MagickFalse)
    status=ClonePixelCacheRepository(clone_info,cache_info,exception);
  *offset=(*offset+(MagickOffsetType) cache_info->length+page_size-
    ((MagickOffsetType) cache_info->length % page_size));
  clone_info=(CacheInfo *) DestroyPixelCache(clone_info);
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   Q u e u e A u t h e n t i c P i x e l C a c h e N e x u s                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  QueueAuthenticPixelCacheNexus() allocates an region to store image pixels as
%  defined by the region rectangle and returns a pointer to the region.  This
%  region is subsequently transferred from the pixel cache with
%  SyncAuthenticPixelsCache().  A pointer to the pixels is returned if the
%  pixels are transferred, otherwise a NULL is returned.
%
%  The format of the QueueAuthenticPixelCacheNexus() method is:
%
%      PixelPacket *QueueAuthenticPixelCacheNexus(Image *image,const ssize_t x,
%        const ssize_t y,const size_t columns,const size_t rows,
%        const MagickBooleanType clone,NexusInfo *nexus_info,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o x,y,columns,rows:  These values define the perimeter of a region of
%      pixels.
%
%    o nexus_info: the cache nexus to set.
%
%    o clone: clone the pixel cache.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport PixelPacket *QueueAuthenticPixel(Image *image,const ssize_t x,
  const ssize_t y,const size_t columns,const size_t rows,
  const MagickBooleanType clone,NexusInfo *nexus_info,
  ExceptionInfo *exception)
{
  return(QueueAuthenticPixelCacheNexus(image,x,y,columns,rows,clone,nexus_info,
    exception));
}

MagickExport PixelPacket *QueueAuthenticPixelCacheNexus(Image *image,
  const ssize_t x,const ssize_t y,const size_t columns,const size_t rows,
  const MagickBooleanType clone,NexusInfo *nexus_info,ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  MagickOffsetType
    offset;

  MagickSizeType
    number_pixels;

  PixelPacket
    *magick_restrict pixels;

  /*
    Validate pixel cache geometry.
  */
  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) GetImagePixelCache(image,clone,exception);
  if (cache_info == (Cache) NULL)
    return((PixelPacket *) NULL);
  assert(cache_info->signature == MagickCoreSignature);
  if ((cache_info->columns == 0) || (cache_info->rows == 0) || (x < 0) ||
      (y < 0) || (x >= (ssize_t) cache_info->columns) ||
      (y >= (ssize_t) cache_info->rows))
    {
      (void) ThrowMagickException(exception,GetMagickModule(),CacheError,
        "PixelsAreNotAuthentic","`%s'",image->filename);
      return((PixelPacket *) NULL);
    }
  if (IsValidPixelOffset(y,cache_info->columns) == MagickFalse)
    return((PixelPacket *) NULL);
  offset=y*(MagickOffsetType) cache_info->columns+x;
  if (offset < 0)
    return((PixelPacket *) NULL);
  number_pixels=(MagickSizeType) cache_info->columns*cache_info->rows;
  offset+=((MagickOffsetType) rows-1)*(MagickOffsetType) cache_info->columns+
    (MagickOffsetType) columns-1;
  if ((MagickSizeType) offset >= number_pixels)
    return((PixelPacket *) NULL);
  /*
    Return pixel cache.
  */
  pixels=SetPixelCacheNexusPixels(cache_info,WriteMode,x,y,columns,rows,
    (image->clip_mask != (Image *) NULL) || (image->mask != (Image *) NULL) ?
    MagickTrue : MagickFalse,nexus_info,exception);
  return(pixels);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   Q u e u e A u t h e n t i c P i x e l s C a c h e                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  QueueAuthenticPixelsCache() allocates an region to store image pixels as
%  defined by the region rectangle and returns a pointer to the region.  This
%  region is subsequently transferred from the pixel cache with
%  SyncAuthenticPixelsCache().  A pointer to the pixels is returned if the
%  pixels are transferred, otherwise a NULL is returned.
%
%  The format of the QueueAuthenticPixelsCache() method is:
%
%      PixelPacket *QueueAuthenticPixelsCache(Image *image,const ssize_t x,
%        const ssize_t y,const size_t columns,const size_t rows,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o x,y,columns,rows:  These values define the perimeter of a region of
%      pixels.
%
%    o exception: return any errors or warnings in this structure.
%
*/
static PixelPacket *QueueAuthenticPixelsCache(Image *image,const ssize_t x,
  const ssize_t y,const size_t columns,const size_t rows,
  ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  assert(image != (const Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  assert(id < (int) cache_info->number_threads);
  return(QueueAuthenticPixelCacheNexus(image,x,y,columns,rows,MagickFalse,
    cache_info->nexus_info[id],exception));
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   Q u e u e A u t h e n t i c P i x e l s                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  QueueAuthenticPixels() queues a mutable pixel region.  If the region is
%  successfully initialized a pointer to a PixelPacket array representing the
%  region is returned, otherwise NULL is returned.  The returned pointer may
%  point to a temporary working buffer for the pixels or it may point to the
%  final location of the pixels in memory.
%
%  Write-only access means that any existing pixel values corresponding to
%  the region are ignored.  This is useful if the initial image is being
%  created from scratch, or if the existing pixel values are to be
%  completely replaced without need to refer to their preexisting values.
%  The application is free to read and write the pixel buffer returned by
%  QueueAuthenticPixels() any way it pleases. QueueAuthenticPixels() does not
%  initialize the pixel array values. Initializing pixel array values is the
%  application's responsibility.
%
%  Performance is maximized if the selected region is part of one row, or
%  one or more full rows, since then there is opportunity to access the
%  pixels in-place (without a copy) if the image is in memory, or in a
%  memory-mapped file. The returned pointer must *never* be deallocated
%  by the user.
%
%  Pixels accessed via the returned pointer represent a simple array of type
%  PixelPacket. If the image type is CMYK or the storage class is PseudoClass,
%  call GetAuthenticIndexQueue() after invoking GetAuthenticPixels() to obtain
%  the black color component or the colormap indexes (of type IndexPacket)
%  corresponding to the region.  Once the PixelPacket (and/or IndexPacket)
%  array has been updated, the changes must be saved back to the underlying
%  image using SyncAuthenticPixels() or they may be lost.
%
%  The format of the QueueAuthenticPixels() method is:
%
%      PixelPacket *QueueAuthenticPixels(Image *image,const ssize_t x,
%        const ssize_t y,const size_t columns,const size_t rows,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o x,y,columns,rows:  These values define the perimeter of a region of
%      pixels.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport PixelPacket *QueueAuthenticPixels(Image *image,const ssize_t x,
  const ssize_t y,const size_t columns,const size_t rows,
  ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (cache_info->methods.queue_authentic_pixels_handler !=
       (QueueAuthenticPixelsHandler) NULL)
    return(cache_info->methods.queue_authentic_pixels_handler(image,x,y,columns,
      rows,exception));
  assert(id < (int) cache_info->number_threads);
  return(QueueAuthenticPixelCacheNexus(image,x,y,columns,rows,MagickFalse,
    cache_info->nexus_info[id],exception));
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   R e a d P i x e l C a c h e I n d e x e s                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ReadPixelCacheIndexes() reads colormap indexes from the specified region of
%  the pixel cache.
%
%  The format of the ReadPixelCacheIndexes() method is:
%
%      MagickBooleanType ReadPixelCacheIndexes(CacheInfo *cache_info,
%        NexusInfo *nexus_info,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o cache_info: the pixel cache.
%
%    o nexus_info: the cache nexus to read the colormap indexes.
%
%    o exception: return any errors or warnings in this structure.
%
*/

static inline MagickOffsetType ReadPixelCacheRegion(
  const CacheInfo *magick_restrict cache_info,const MagickOffsetType offset,
  const MagickSizeType length,unsigned char *magick_restrict buffer)
{
  MagickOffsetType
    i;

  ssize_t
    count = 0;

#if !defined(MAGICKCORE_HAVE_PREAD)
  if (lseek(cache_info->file,offset,SEEK_SET) < 0)
    return((MagickOffsetType) -1);
#endif
  for (i=0; i < (MagickOffsetType) length; i+=count)
  {
#if !defined(MAGICKCORE_HAVE_PREAD)
    count=read(cache_info->file,buffer+i,(size_t) MagickMin(length-
      (MagickSizeType) i,(size_t) MagickMaxBufferExtent));
#else
    count=pread(cache_info->file,buffer+i,(size_t) MagickMin(length-
      (MagickSizeType) i,(size_t) MagickMaxBufferExtent),offset+i);
#endif
    if (count <= 0)
      {
        count=0;
        if (errno != EINTR)
          break;
      }
  }
  return(i);
}

static MagickBooleanType ReadPixelCacheIndexes(
  CacheInfo *magick_restrict cache_info,NexusInfo *magick_restrict nexus_info,
  ExceptionInfo *exception)
{
  IndexPacket
    *magick_restrict q;

  MagickOffsetType
    count,
    offset;

  MagickSizeType
    extent,
    length;

  ssize_t
    y;

  size_t
    rows;

  if (cache_info->active_index_channel == MagickFalse)
    return(MagickFalse);
  if (nexus_info->authentic_pixel_cache != MagickFalse)
    return(MagickTrue);
  if (IsValidPixelOffset(nexus_info->region.y,cache_info->columns) == MagickFalse)
    return(MagickFalse);
  offset=nexus_info->region.y*(MagickOffsetType) cache_info->columns+
    nexus_info->region.x;
  length=(MagickSizeType) nexus_info->region.width*sizeof(IndexPacket);
  rows=nexus_info->region.height;
  extent=length*rows;
  q=nexus_info->indexes;
  y=0;
  switch (cache_info->type)
  {
    case MemoryCache:
    case MapCache:
    {
      IndexPacket
        *magick_restrict p;

      /*
        Read indexes from memory.
      */
      if ((cache_info->columns == nexus_info->region.width) &&
          (extent == (MagickSizeType) ((size_t) extent)))
        {
          length=extent;
          rows=1UL;
        }
      p=cache_info->indexes+offset;
      for (y=0; y < (ssize_t) rows; y++)
      {
        (void) memcpy(q,p,(size_t) length);
        p+=(ptrdiff_t) cache_info->columns;
        q+=(ptrdiff_t) nexus_info->region.width;
      }
      break;
    }
    case DiskCache:
    {
      /*
        Read indexes from disk.
      */
      LockSemaphoreInfo(cache_info->file_semaphore);
      if (OpenPixelCacheOnDisk(cache_info,IOMode) == MagickFalse)
        {
          ThrowFileException(exception,FileOpenError,"UnableToOpenFile",
            cache_info->cache_filename);
          UnlockSemaphoreInfo(cache_info->file_semaphore);
          return(MagickFalse);
        }
      if ((cache_info->columns == nexus_info->region.width) &&
          (extent <= MagickMaxBufferExtent))
        {
          length=extent;
          rows=1UL;
        }
      extent=(MagickSizeType) cache_info->columns*cache_info->rows;
      for (y=0; y < (ssize_t) rows; y++)
      {
        count=ReadPixelCacheRegion(cache_info,cache_info->offset+
          (MagickOffsetType) extent*(MagickOffsetType) sizeof(PixelPacket)+
          offset*(MagickOffsetType) sizeof(*q),length,(unsigned char *) q);
        if (count < (MagickOffsetType) length)
          break;
        offset+=(MagickOffsetType) cache_info->columns;
        q+=(ptrdiff_t) nexus_info->region.width;
      }
      if (IsFileDescriptorLimitExceeded() != MagickFalse)
        (void) ClosePixelCacheOnDisk(cache_info);
      UnlockSemaphoreInfo(cache_info->file_semaphore);
      break;
    }
    case DistributedCache:
    {
      RectangleInfo
        region;

      /*
        Read indexes from distributed cache.
      */
      LockSemaphoreInfo(cache_info->file_semaphore);
      region=nexus_info->region;
      if ((cache_info->columns != nexus_info->region.width) ||
          (extent > MagickMaxBufferExtent))
        region.height=1UL;
      else
        {
          length=extent;
          rows=1UL;
        }
      for (y=0; y < (ssize_t) rows; y++)
      {
        count=ReadDistributePixelCacheIndexes((DistributeCacheInfo *)
          cache_info->server_info,&region,length,(unsigned char *) q);
        if (count != (MagickOffsetType) length)
          break;
        q+=(ptrdiff_t) nexus_info->region.width;
        region.y++;
      }
      UnlockSemaphoreInfo(cache_info->file_semaphore);
      break;
    }
    default:
      break;
  }
  if (y < (ssize_t) rows)
    {
      ThrowFileException(exception,CacheError,"UnableToReadPixelCache",
        cache_info->cache_filename);
      return(MagickFalse);
    }
  if ((cache_info->debug != MagickFalse) &&
      (CacheTick(nexus_info->region.y,cache_info->rows) != MagickFalse))
    (void) LogMagickEvent(CacheEvent,GetMagickModule(),
      "%s[%.20gx%.20g%+.20g%+.20g]",cache_info->filename,(double)
      nexus_info->region.width,(double) nexus_info->region.height,(double)
      nexus_info->region.x,(double) nexus_info->region.y);
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   R e a d P i x e l C a c h e P i x e l s                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ReadPixelCachePixels() reads pixels from the specified region of the pixel
%  cache.
%
%  The format of the ReadPixelCachePixels() method is:
%
%      MagickBooleanType ReadPixelCachePixels(CacheInfo *cache_info,
%        NexusInfo *nexus_info,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o cache_info: the pixel cache.
%
%    o nexus_info: the cache nexus to read the pixels.
%
%    o exception: return any errors or warnings in this structure.
%
*/
static MagickBooleanType ReadPixelCachePixels(
  CacheInfo *magick_restrict cache_info,NexusInfo *magick_restrict nexus_info,
  ExceptionInfo *exception)
{
  MagickOffsetType
    count,
    offset;

  MagickSizeType
    extent,
    length;

  PixelPacket
    *magick_restrict q;

  size_t
    rows;

  ssize_t
    y;

  if (nexus_info->authentic_pixel_cache != MagickFalse)
    return(MagickTrue);
  if (IsValidPixelOffset(nexus_info->region.y,cache_info->columns) == MagickFalse)
    return(MagickFalse);
  offset=nexus_info->region.y*(MagickOffsetType) cache_info->columns;
  if ((offset/(MagickOffsetType) cache_info->columns) != nexus_info->region.y)
    return(MagickFalse);
  offset+=nexus_info->region.x;
  length=(MagickSizeType) nexus_info->region.width*sizeof(PixelPacket);
  if ((length/sizeof(PixelPacket)) != nexus_info->region.width)
    return(MagickFalse);
  rows=nexus_info->region.height;
  extent=length*rows;
  if ((extent == 0) || ((extent/length) != rows))
    return(MagickFalse);
  q=nexus_info->pixels;
  y=0;
  switch (cache_info->type)
  {
    case MemoryCache:
    case MapCache:
    {
      PixelPacket
        *magick_restrict p;

      /*
        Read pixels from memory.
      */
      if ((cache_info->columns == nexus_info->region.width) &&
          (extent == (MagickSizeType) ((size_t) extent)))
        {
          length=extent;
          rows=1UL;
        }
      p=cache_info->pixels+offset;
      for (y=0; y < (ssize_t) rows; y++)
      {
        (void) memcpy(q,p,(size_t) length);
        p+=(ptrdiff_t) cache_info->columns;
        q+=(ptrdiff_t) nexus_info->region.width;
      }
      break;
    }
    case DiskCache:
    {
      /*
        Read pixels from disk.
      */
      LockSemaphoreInfo(cache_info->file_semaphore);
      if (OpenPixelCacheOnDisk(cache_info,IOMode) == MagickFalse)
        {
          ThrowFileException(exception,FileOpenError,"UnableToOpenFile",
            cache_info->cache_filename);
          UnlockSemaphoreInfo(cache_info->file_semaphore);
          return(MagickFalse);
        }
      if ((cache_info->columns == nexus_info->region.width) &&
          (extent <= MagickMaxBufferExtent))
        {
          length=extent;
          rows=1UL;
        }
      for (y=0; y < (ssize_t) rows; y++)
      {
        count=ReadPixelCacheRegion(cache_info,cache_info->offset+offset*
          (MagickOffsetType) sizeof(*q),length,(unsigned char *) q);
        if (count < (MagickOffsetType) length)
          break;
        offset+=(MagickOffsetType) cache_info->columns;
        q+=(ptrdiff_t) nexus_info->region.width;
      }
      if (IsFileDescriptorLimitExceeded() != MagickFalse)
        (void) ClosePixelCacheOnDisk(cache_info);
      UnlockSemaphoreInfo(cache_info->file_semaphore);
      break;
    }
    case DistributedCache:
    {
      RectangleInfo
        region;

      /*
        Read pixels from distributed cache.
      */
      LockSemaphoreInfo(cache_info->file_semaphore);
      region=nexus_info->region;
      if ((cache_info->columns != nexus_info->region.width) ||
          (extent > MagickMaxBufferExtent))
        region.height=1UL;
      else
        {
          length=extent;
          rows=1UL;
        }
      for (y=0; y < (ssize_t) rows; y++)
      {
        count=ReadDistributePixelCachePixels((DistributeCacheInfo *)
          cache_info->server_info,&region,length,(unsigned char *) q);
        if (count != (MagickOffsetType) length)
          break;
        q+=(ptrdiff_t) nexus_info->region.width;
        region.y++;
      }
      UnlockSemaphoreInfo(cache_info->file_semaphore);
      break;
    }
    default:
      break;
  }
  if (y < (ssize_t) rows)
    {
      ThrowFileException(exception,CacheError,"UnableToReadPixelCache",
        cache_info->cache_filename);
      return(MagickFalse);
    }
  if ((cache_info->debug != MagickFalse) &&
      (CacheTick(nexus_info->region.y,cache_info->rows) != MagickFalse))
    (void) LogMagickEvent(CacheEvent,GetMagickModule(),
      "%s[%.20gx%.20g%+.20g%+.20g]",cache_info->filename,(double)
      nexus_info->region.width,(double) nexus_info->region.height,(double)
      nexus_info->region.x,(double) nexus_info->region.y);
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   R e f e r e n c e P i x e l C a c h e                                     %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ReferencePixelCache() increments the reference count associated with the
%  pixel cache returning a pointer to the cache.
%
%  The format of the ReferencePixelCache method is:
%
%      Cache ReferencePixelCache(Cache cache_info)
%
%  A description of each parameter follows:
%
%    o cache_info: the pixel cache.
%
*/
MagickExport Cache ReferencePixelCache(Cache cache)
{
  CacheInfo
    *magick_restrict cache_info;

  assert(cache != (Cache *) NULL);
  cache_info=(CacheInfo *) cache;
  assert(cache_info->signature == MagickCoreSignature);
  LockSemaphoreInfo(cache_info->semaphore);
  cache_info->reference_count++;
  UnlockSemaphoreInfo(cache_info->semaphore);
  return(cache_info);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   R e s e t C a c h e A n o n y m o u s M e m o r y                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ResetCacheAnonymousMemory() resets the anonymous_memory value.
%
%  The format of the ResetCacheAnonymousMemory method is:
%
%      void ResetCacheAnonymousMemory(void)
%
*/
MagickPrivate void ResetCacheAnonymousMemory(void)
{
  cache_anonymous_memory=0;
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   S e t P i x e l C a c h e M e t h o d s                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  SetPixelCacheMethods() sets the image pixel methods to the specified ones.
%
%  The format of the SetPixelCacheMethods() method is:
%
%      SetPixelCacheMethods(Cache *,CacheMethods *cache_methods)
%
%  A description of each parameter follows:
%
%    o cache: the pixel cache.
%
%    o cache_methods: Specifies a pointer to a CacheMethods structure.
%
*/
MagickExport void SetPixelCacheMethods(Cache cache,CacheMethods *cache_methods)
{
  CacheInfo
    *magick_restrict cache_info;

  GetOneAuthenticPixelFromHandler
    get_one_authentic_pixel_from_handler;

  GetOneVirtualPixelFromHandler
    get_one_virtual_pixel_from_handler;

  /*
    Set cache pixel methods.
  */
  assert(cache != (Cache) NULL);
  assert(cache_methods != (CacheMethods *) NULL);
  cache_info=(CacheInfo *) cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",
      cache_info->filename);
  if (cache_methods->get_virtual_pixel_handler != (GetVirtualPixelHandler) NULL)
    cache_info->methods.get_virtual_pixel_handler=
      cache_methods->get_virtual_pixel_handler;
  if (cache_methods->destroy_pixel_handler != (DestroyPixelHandler) NULL)
    cache_info->methods.destroy_pixel_handler=
      cache_methods->destroy_pixel_handler;
  if (cache_methods->get_virtual_indexes_from_handler !=
      (GetVirtualIndexesFromHandler) NULL)
    cache_info->methods.get_virtual_indexes_from_handler=
      cache_methods->get_virtual_indexes_from_handler;
  if (cache_methods->get_authentic_pixels_handler !=
      (GetAuthenticPixelsHandler) NULL)
    cache_info->methods.get_authentic_pixels_handler=
      cache_methods->get_authentic_pixels_handler;
  if (cache_methods->queue_authentic_pixels_handler !=
      (QueueAuthenticPixelsHandler) NULL)
    cache_info->methods.queue_authentic_pixels_handler=
      cache_methods->queue_authentic_pixels_handler;
  if (cache_methods->sync_authentic_pixels_handler !=
      (SyncAuthenticPixelsHandler) NULL)
    cache_info->methods.sync_authentic_pixels_handler=
      cache_methods->sync_authentic_pixels_handler;
  if (cache_methods->get_authentic_pixels_from_handler !=
      (GetAuthenticPixelsFromHandler) NULL)
    cache_info->methods.get_authentic_pixels_from_handler=
      cache_methods->get_authentic_pixels_from_handler;
  if (cache_methods->get_authentic_indexes_from_handler !=
      (GetAuthenticIndexesFromHandler) NULL)
    cache_info->methods.get_authentic_indexes_from_handler=
      cache_methods->get_authentic_indexes_from_handler;
  get_one_virtual_pixel_from_handler=
    cache_info->methods.get_one_virtual_pixel_from_handler;
  if (get_one_virtual_pixel_from_handler !=
      (GetOneVirtualPixelFromHandler) NULL)
    cache_info->methods.get_one_virtual_pixel_from_handler=
      cache_methods->get_one_virtual_pixel_from_handler;
  get_one_authentic_pixel_from_handler=
    cache_methods->get_one_authentic_pixel_from_handler;
  if (get_one_authentic_pixel_from_handler !=
      (GetOneAuthenticPixelFromHandler) NULL)
    cache_info->methods.get_one_authentic_pixel_from_handler=
      cache_methods->get_one_authentic_pixel_from_handler;
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   S e t P i x e l C a c h e N e x u s P i x e l s                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  SetPixelCacheNexusPixels() defines the region of the cache for the
%  specified cache nexus.
%
%  The format of the SetPixelCacheNexusPixels() method is:
%
%      PixelPacket SetPixelCacheNexusPixels(
%        const CacheInfo *magick_restrcit cache_info,const MapMode mode,
%        const ssize_t y,const size_t width,const size_t height,
%        const MagickBooleanType buffered,NexusInfo *magick_restrict nexus_info,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o cache_info: the pixel cache.
%
%    o mode: ReadMode, WriteMode, or IOMode.
%
%    o x,y,width,height: define the region of this particular cache nexus.
%
%    o buffered: pixels are buffered.
%
%    o nexus_info: the cache nexus to set.
%
%    o exception: return any errors or warnings in this structure.
%
*/

static inline MagickBooleanType AcquireCacheNexusPixels(
  const CacheInfo *magick_restrict cache_info,const MagickSizeType length,
  NexusInfo *magick_restrict nexus_info,ExceptionInfo *exception)
{
  if (length != (MagickSizeType) ((size_t) length))
    {
      (void) ThrowMagickException(exception,GetMagickModule(),
        ResourceLimitError,"PixelCacheAllocationFailed","`%s'",
        cache_info->filename);
      return(MagickFalse);
    }
  nexus_info->length=0;
  nexus_info->mapped=MagickFalse;
  if (cache_anonymous_memory <= 0)
    {
      nexus_info->cache=(PixelPacket *) MagickAssumeAligned(
        AcquireAlignedMemory(1,(size_t) length));
      if (nexus_info->cache != (PixelPacket *) NULL)
        (void) memset(nexus_info->cache,0,(size_t) length);
    }
  else
    {
      nexus_info->cache=(PixelPacket *) MapBlob(-1,IOMode,0,(size_t) length);
      if (nexus_info->cache != (PixelPacket *) NULL)
        nexus_info->mapped=MagickTrue;
    }
  if (nexus_info->cache == (PixelPacket *) NULL)
    {
      (void) ThrowMagickException(exception,GetMagickModule(),
        ResourceLimitError,"PixelCacheAllocationFailed","`%s'",
        cache_info->filename);
      return(MagickFalse);
    }
  nexus_info->length=length;
  return(MagickTrue);
}

static inline void PrefetchPixelCacheNexusPixels(const NexusInfo *nexus_info,
  const MapMode mode)
{
  if (nexus_info->length < CACHE_LINE_SIZE)
    return;
  if (mode == ReadMode)
    {
      MagickCachePrefetch((unsigned char *) nexus_info->pixels+CACHE_LINE_SIZE,
        0,1);
      return;
    }
  MagickCachePrefetch((unsigned char *) nexus_info->pixels+CACHE_LINE_SIZE,1,1);
}

static inline MagickBooleanType ValidatePixelOffset(const ssize_t x,
  const size_t a)
{
  if ((x >= 0) && (x >= ((ssize_t) (MAGICK_SSIZE_MAX-5*a))))
    return(MagickFalse);
  if (x <= ((ssize_t) (MAGICK_SSIZE_MIN+5*(MagickOffsetType) a)))
    return(MagickFalse);
  return(MagickTrue);
}

static PixelPacket *SetPixelCacheNexusPixels(
  const CacheInfo *magick_restrict cache_info,const MapMode mode,
  const ssize_t x,const ssize_t y,const size_t width,const size_t height,
  const MagickBooleanType buffered,NexusInfo *magick_restrict nexus_info,
  ExceptionInfo *exception)
{
  MagickBooleanType
    status;

  MagickSizeType
    length,
    number_pixels;

  assert(cache_info != (const CacheInfo *) NULL);
  assert(cache_info->signature == MagickCoreSignature);
  if (cache_info->type == UndefinedCache)
    return((PixelPacket *) NULL);
  assert(nexus_info->signature == MagickCoreSignature);
  (void) memset(&nexus_info->region,0,sizeof(nexus_info->region));
  if ((width == 0) || (height == 0))
    {
      (void) ThrowMagickException(exception,GetMagickModule(),CacheError,
        "NoPixelsDefinedInCache","`%s'",cache_info->filename);
      return((PixelPacket *) NULL);
    }
  if (((MagickSizeType) width > cache_info->width_limit) ||
      ((MagickSizeType) height > cache_info->height_limit))
    {
      (void) ThrowMagickException(exception,GetMagickModule(),ImageError,
        "WidthOrHeightExceedsLimit","`%s'",cache_info->filename);
      return((PixelPacket *) NULL);
    }
  if ((ValidatePixelOffset(x,width) == MagickFalse) ||
      (ValidatePixelOffset(y,height) == MagickFalse))
    {
      (void) ThrowMagickException(exception,GetMagickModule(),CorruptImageError,
        "InvalidPixel","`%s'",cache_info->filename);
      return((PixelPacket *) NULL);
    }
  if (((cache_info->type == MemoryCache) || (cache_info->type == MapCache)) &&
      (buffered == MagickFalse))
    {
      if (((x >= 0) && (y >= 0) &&
          (((ssize_t) height+y-1) < (ssize_t) cache_info->rows)) &&
          (((x == 0) && (width == cache_info->columns)) || ((height == 1) &&
          (((ssize_t) width+x-1) < (ssize_t) cache_info->columns))))
        {
          MagickOffsetType
            offset;

          /*
            Pixels are accessed directly from memory.
          */
          if (IsValidPixelOffset(y,cache_info->columns) == MagickFalse)
            return((PixelPacket *) NULL);
          offset=y*(MagickOffsetType) cache_info->columns+x;
          nexus_info->pixels=cache_info->pixels+offset;
          nexus_info->indexes=(IndexPacket *) NULL;
          if (cache_info->active_index_channel != MagickFalse)
            nexus_info->indexes=cache_info->indexes+offset;
          nexus_info->region.width=width;
          nexus_info->region.height=height;
          nexus_info->region.x=x;
          nexus_info->region.y=y;
          nexus_info->authentic_pixel_cache=MagickTrue;
          PrefetchPixelCacheNexusPixels(nexus_info,mode);
          return(nexus_info->pixels);
        }
    }
  /*
    Pixels are stored in a staging region until they are synced to the cache.
  */
  number_pixels=(MagickSizeType) width*height;
  length=MagickMax(number_pixels,MagickMax(cache_info->columns,
    cache_info->rows))*sizeof(PixelPacket);
  if (cache_info->active_index_channel != MagickFalse)
    length+=number_pixels*sizeof(IndexPacket);
  status=MagickTrue;
  if (nexus_info->cache == (PixelPacket *) NULL)
    status=AcquireCacheNexusPixels(cache_info,length,nexus_info,exception);
  else
    if (nexus_info->length < length)
      {
        RelinquishCacheNexusPixels(nexus_info);
        status=AcquireCacheNexusPixels(cache_info,length,nexus_info,exception);
      }
  if (status == MagickFalse)
    {
      (void) memset(&nexus_info->region,0,sizeof(nexus_info->region));
      return((PixelPacket *) NULL);
    }
  nexus_info->pixels=nexus_info->cache;
  nexus_info->indexes=(IndexPacket *) NULL;
  if (cache_info->active_index_channel != MagickFalse)
    nexus_info->indexes=(IndexPacket *) (nexus_info->pixels+number_pixels);
  nexus_info->region.width=width;
  nexus_info->region.height=height;
  nexus_info->region.x=x;
  nexus_info->region.y=y;
  nexus_info->authentic_pixel_cache=cache_info->type == PingCache ?
    MagickTrue : MagickFalse;
  PrefetchPixelCacheNexusPixels(nexus_info,mode);
  return(nexus_info->pixels);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   S e t P i x e l C a c h e V i r t u a l M e t h o d                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  SetPixelCacheVirtualMethod() sets the "virtual pixels" method for the
%  pixel cache and returns the previous setting.  A virtual pixel is any pixel
%  access that is outside the boundaries of the image cache.
%
%  The format of the SetPixelCacheVirtualMethod() method is:
%
%      VirtualPixelMethod SetPixelCacheVirtualMethod(const Image *image,
%        const VirtualPixelMethod virtual_pixel_method)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o virtual_pixel_method: choose the type of virtual pixel.
%
*/

static MagickBooleanType SetCacheAlphaChannel(Image *image,
  const Quantum opacity)
{
  CacheView
    *magick_restrict image_view;

  MagickBooleanType
    status;

  ssize_t
    y;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(image->cache != (Cache) NULL);
  image->matte=MagickTrue;
  status=MagickTrue;
  image_view=AcquireVirtualCacheView(image,&image->exception);  /* must be virtual */
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(status) \
    magick_number_threads(image,image,image->rows,2)
#endif
  for (y=0; y < (ssize_t) image->rows; y++)
  {
    PixelPacket
      *magick_restrict q;

    ssize_t
      x;

    if (status == MagickFalse)
      continue;
    q=GetCacheViewAuthenticPixels(image_view,0,y,image->columns,1,
      &image->exception);
    if (q == (PixelPacket *) NULL)
      {
        status=MagickFalse;
        continue;
      }
    for (x=0; x < (ssize_t) image->columns; x++)
    {
      q->opacity=opacity;
      q++;
    }
    status=SyncCacheViewAuthenticPixels(image_view,&image->exception);
  }
  image_view=DestroyCacheView(image_view);
  return(status);
}

MagickExport VirtualPixelMethod SetPixelCacheVirtualMethod(const Image *image,
  const VirtualPixelMethod virtual_pixel_method)
{
  CacheInfo
    *magick_restrict cache_info;

  VirtualPixelMethod
    method;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  method=cache_info->virtual_pixel_method;
  cache_info->virtual_pixel_method=virtual_pixel_method;
  if ((image->columns != 0) && (image->rows != 0))
    switch (virtual_pixel_method)
    {
      case BackgroundVirtualPixelMethod:
      {
        if ((image->background_color.opacity != OpaqueOpacity) &&
            (image->matte == MagickFalse))
          (void) SetCacheAlphaChannel((Image *) image,OpaqueOpacity);
        if ((IsPixelGray(&image->background_color) == MagickFalse) &&
            (IsGrayColorspace(image->colorspace) != MagickFalse))
          (void) SetImageColorspace((Image *) image,sRGBColorspace);
        break;
      }
      case TransparentVirtualPixelMethod:
      {
        if (image->matte == MagickFalse)
          (void) SetCacheAlphaChannel((Image *) image,OpaqueOpacity);
        break;
      }
      default:
        break;
    }
  return(method);
}

#if defined(MAGICKCORE_OPENCL_SUPPORT)
/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   S y n c A u t h e n t i c O p e n C L B u f f e r                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  SyncAuthenticOpenCLBuffer() ensures all the OpenCL operations have been
%  completed and updates the host memory.
%
%  The format of the SyncAuthenticOpenCLBuffer() method is:
%
%      void SyncAuthenticOpenCLBuffer(const Image *image)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
*/
static void CopyOpenCLBuffer(CacheInfo *magick_restrict cache_info)
{
  MagickCLEnv
    clEnv;

  assert(cache_info != (CacheInfo *)NULL);
  if ((cache_info->type != MemoryCache) ||
    (cache_info->opencl == (OpenCLCacheInfo *)NULL))
    return;
  /*
  Ensure single threaded access to OpenCL environment.
  */
  LockSemaphoreInfo(cache_info->semaphore);
  if (cache_info->opencl != (OpenCLCacheInfo *)NULL)
  {
    cl_event
      *events;

    cl_uint
      event_count;

    clEnv=GetDefaultOpenCLEnv();
    events=CopyOpenCLEvents(cache_info->opencl,&event_count);
    if (events != (cl_event *) NULL)
    {
      cl_command_queue
        queue;

      cl_context
        context;

      cl_int
        status;

      PixelPacket
        *pixels;

      context=GetOpenCLContext(clEnv);
      queue=AcquireOpenCLCommandQueue(clEnv);
      pixels=(PixelPacket *) clEnv->library->clEnqueueMapBuffer(queue,
        cache_info->opencl->buffer,CL_TRUE, CL_MAP_READ | CL_MAP_WRITE,0,
        cache_info->length,event_count,events,NULL,&status);
      assert(pixels == cache_info->pixels);
      events=(cl_event *) RelinquishMagickMemory(events);
      RelinquishOpenCLCommandQueue(clEnv,queue);
    }
    cache_info->opencl=RelinquishOpenCLCacheInfo(clEnv,cache_info->opencl);
  }
  UnlockSemaphoreInfo(cache_info->semaphore);
}

MagickPrivate void SyncAuthenticOpenCLBuffer(const Image *image)
{
  CacheInfo
    *magick_restrict cache_info;

  assert(image != (Image *)NULL);
  cache_info = (CacheInfo *)image->cache;
  CopyOpenCLBuffer(cache_info);
}
#endif

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   S y n c A u t h e n t i c P i x e l C a c h e N e x u s                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  SyncAuthenticPixelCacheNexus() saves the authentic image pixels to the
%  in-memory or disk cache.  The method returns MagickTrue if the pixel region
%  is synced, otherwise MagickFalse.
%
%  The format of the SyncAuthenticPixelCacheNexus() method is:
%
%      MagickBooleanType SyncAuthenticPixelCacheNexus(Image *image,
%        NexusInfo *nexus_info,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o nexus_info: the cache nexus to sync.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport MagickBooleanType SyncAuthenticPixelCacheNexus(Image *image,
  NexusInfo *magick_restrict nexus_info,ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  MagickBooleanType
    status;

  /*
    Transfer pixels to the cache.
  */
  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->cache == (Cache) NULL)
    ThrowBinaryException(CacheError,"PixelCacheIsNotOpen",image->filename);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (cache_info->type == UndefinedCache)
    return(MagickFalse);
  if ((image->storage_class == DirectClass) &&
      (image->clip_mask != (Image *) NULL) &&
      (ClipPixelCacheNexus(image,nexus_info,exception) == MagickFalse))
    return(MagickFalse);
  if ((image->storage_class == DirectClass) &&
      (image->mask != (Image *) NULL) &&
      (MaskPixelCacheNexus(image,nexus_info,exception) == MagickFalse))
    return(MagickFalse);
  if (nexus_info->authentic_pixel_cache != MagickFalse)
    {
      if (image->taint == MagickFalse)
        image->taint=MagickTrue;
      return(MagickTrue);
    }
  assert(cache_info->signature == MagickCoreSignature);
  status=WritePixelCachePixels(cache_info,nexus_info,exception);
  if ((cache_info->active_index_channel != MagickFalse) &&
      (WritePixelCacheIndexes(cache_info,nexus_info,exception) == MagickFalse))
    return(MagickFalse);
  if ((status != MagickFalse) && (image->taint == MagickFalse))
    image->taint=MagickTrue;
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   S y n c A u t h e n t i c P i x e l C a c h e                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  SyncAuthenticPixelsCache() saves the authentic image pixels to the in-memory
%  or disk cache.  The method returns MagickTrue if the pixel region is synced,
%  otherwise MagickFalse.
%
%  The format of the SyncAuthenticPixelsCache() method is:
%
%      MagickBooleanType SyncAuthenticPixelsCache(Image *image,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o exception: return any errors or warnings in this structure.
%
*/
static MagickBooleanType SyncAuthenticPixelsCache(Image *image,
  ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  MagickBooleanType
    status;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  assert(id < (int) cache_info->number_threads);
  status=SyncAuthenticPixelCacheNexus(image,cache_info->nexus_info[id],
    exception);
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   S y n c A u t h e n t i c P i x e l s                                     %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  SyncAuthenticPixels() saves the image pixels to the in-memory or disk cache.
%  The method returns MagickTrue if the pixel region is flushed, otherwise
%  MagickFalse.
%
%  The format of the SyncAuthenticPixels() method is:
%
%      MagickBooleanType SyncAuthenticPixels(Image *image,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport MagickBooleanType SyncAuthenticPixels(Image *image,
  ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  const int
    id = GetOpenMPThreadId();

  MagickBooleanType
    status;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(image->cache != (Cache) NULL);
  cache_info=(CacheInfo *) image->cache;
  assert(cache_info->signature == MagickCoreSignature);
  if (cache_info->methods.sync_authentic_pixels_handler !=
       (SyncAuthenticPixelsHandler) NULL)
    return(cache_info->methods.sync_authentic_pixels_handler(image,exception));
  assert(id < (int) cache_info->number_threads);
  status=SyncAuthenticPixelCacheNexus(image,cache_info->nexus_info[id],
    exception);
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   S y n c I m a g e P i x e l C a c h e                                     %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  SyncImagePixelCache() saves the image pixels to the in-memory or disk cache.
%  The method returns MagickTrue if the pixel region is flushed, otherwise
%  MagickFalse.
%
%  The format of the SyncImagePixelCache() method is:
%
%      MagickBooleanType SyncImagePixelCache(Image *image,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickPrivate MagickBooleanType SyncImagePixelCache(Image *image,
  ExceptionInfo *exception)
{
  CacheInfo
    *magick_restrict cache_info;

  assert(image != (Image *) NULL);
  assert(exception != (ExceptionInfo *) NULL);
  cache_info=(CacheInfo *) GetImagePixelCache(image,MagickTrue,exception);
  return(cache_info == (CacheInfo *) NULL ? MagickFalse : MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   W r i t e P i x e l C a c h e I n d e x e s                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  WritePixelCacheIndexes() writes the colormap indexes to the specified
%  region of the pixel cache.
%
%  The format of the WritePixelCacheIndexes() method is:
%
%      MagickBooleanType WritePixelCacheIndexes(CacheInfo *cache_info,
%        NexusInfo *nexus_info,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o cache_info: the pixel cache.
%
%    o nexus_info: the cache nexus to write the colormap indexes.
%
%    o exception: return any errors or warnings in this structure.
%
*/
static MagickBooleanType WritePixelCacheIndexes(CacheInfo *cache_info,
  NexusInfo *magick_restrict nexus_info,ExceptionInfo *exception)
{
  MagickOffsetType
    count,
    offset;

  MagickSizeType
    extent,
    length;

  const IndexPacket
    *magick_restrict p;

  ssize_t
    y;

  size_t
    rows;

  if (cache_info->active_index_channel == MagickFalse)
    return(MagickFalse);
  if (nexus_info->authentic_pixel_cache != MagickFalse)
    return(MagickTrue);
  if (nexus_info->indexes == (IndexPacket *) NULL)
    return(MagickFalse);
  if (IsValidPixelOffset(nexus_info->region.y,cache_info->columns) == MagickFalse)
    return(MagickFalse);
  offset=nexus_info->region.y*(MagickOffsetType) cache_info->columns+
    nexus_info->region.x;
  length=(MagickSizeType) nexus_info->region.width*sizeof(IndexPacket);
  rows=nexus_info->region.height;
  extent=(MagickSizeType) length*rows;
  p=nexus_info->indexes;
  y=0;
  switch (cache_info->type)
  {
    case MemoryCache:
    case MapCache:
    {
      IndexPacket
        *magick_restrict q;

      /*
        Write indexes to memory.
      */
      if ((cache_info->columns == nexus_info->region.width) &&
          (extent == (MagickSizeType) ((size_t) extent)))
        {
          length=extent;
          rows=1UL;
        }
      q=cache_info->indexes+offset;
      for (y=0; y < (ssize_t) rows; y++)
      {
        (void) memcpy(q,p,(size_t) length);
        p+=(ptrdiff_t) nexus_info->region.width;
        q+=(ptrdiff_t) cache_info->columns;
      }
      break;
    }
    case DiskCache:
    {
      /*
        Write indexes to disk.
      */
      LockSemaphoreInfo(cache_info->file_semaphore);
      if (OpenPixelCacheOnDisk(cache_info,IOMode) == MagickFalse)
        {
          ThrowFileException(exception,FileOpenError,"UnableToOpenFile",
            cache_info->cache_filename);
          UnlockSemaphoreInfo(cache_info->file_semaphore);
          return(MagickFalse);
        }
      if ((cache_info->columns == nexus_info->region.width) &&
          (extent <= MagickMaxBufferExtent))
        {
          length=extent;
          rows=1UL;
        }
      extent=(MagickSizeType) cache_info->columns*cache_info->rows;
      for (y=0; y < (ssize_t) rows; y++)
      {
        count=WritePixelCacheRegion(cache_info,cache_info->offset+
          (MagickOffsetType) extent*(MagickOffsetType) sizeof(PixelPacket)+
          offset*(MagickOffsetType) sizeof(*p),length,(const unsigned char *)
          p);
        if (count < (MagickOffsetType) length)
          break;
        p+=(ptrdiff_t) nexus_info->region.width;
        offset+=(MagickOffsetType) cache_info->columns;
      }
      if (IsFileDescriptorLimitExceeded() != MagickFalse)
        (void) ClosePixelCacheOnDisk(cache_info);
      UnlockSemaphoreInfo(cache_info->file_semaphore);
      break;
    }
    case DistributedCache:
    {
      RectangleInfo
        region;

      /*
        Write indexes to distributed cache.
      */
      LockSemaphoreInfo(cache_info->file_semaphore);
      region=nexus_info->region;
      if ((cache_info->columns != nexus_info->region.width) ||
          (extent > MagickMaxBufferExtent))
        region.height=1UL;
      else
        {
          length=extent;
          rows=1UL;
        }
      for (y=0; y < (ssize_t) rows; y++)
      {
        count=WriteDistributePixelCacheIndexes((DistributeCacheInfo *)
          cache_info->server_info,&region,length,(const unsigned char *) p);
        if (count != (MagickOffsetType) length)
          break;
        p+=(ptrdiff_t) nexus_info->region.width;
        region.y++;
      }
      UnlockSemaphoreInfo(cache_info->file_semaphore);
      break;
    }
    default:
      break;
  }
  if (y < (ssize_t) rows)
    {
      ThrowFileException(exception,CacheError,"UnableToWritePixelCache",
        cache_info->cache_filename);
      return(MagickFalse);
    }
  if ((cache_info->debug != MagickFalse) &&
      (CacheTick(nexus_info->region.y,cache_info->rows) != MagickFalse))
    (void) LogMagickEvent(CacheEvent,GetMagickModule(),
      "%s[%.20gx%.20g%+.20g%+.20g]",cache_info->filename,(double)
      nexus_info->region.width,(double) nexus_info->region.height,(double)
      nexus_info->region.x,(double) nexus_info->region.y);
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   W r i t e P i x e l C a c h e P i x e l s                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  WritePixelCachePixels() writes image pixels to the specified region of the
%  pixel cache.
%
%  The format of the WritePixelCachePixels() method is:
%
%      MagickBooleanType WritePixelCachePixels(CacheInfo *cache_info,
%        NexusInfo *nexus_info,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o cache_info: the pixel cache.
%
%    o nexus_info: the cache nexus to write the pixels.
%
%    o exception: return any errors or warnings in this structure.
%
*/
static MagickBooleanType WritePixelCachePixels(CacheInfo *cache_info,
  NexusInfo *magick_restrict nexus_info,ExceptionInfo *exception)
{
  MagickOffsetType
    count,
    offset;

  MagickSizeType
    extent,
    length;

  const PixelPacket
    *magick_restrict p;

  ssize_t
    y;

  size_t
    rows;

  if (nexus_info->authentic_pixel_cache != MagickFalse)
    return(MagickTrue);
  if (IsValidPixelOffset(nexus_info->region.y,cache_info->columns) == MagickFalse)
    return(MagickFalse);
  offset=nexus_info->region.y*(MagickOffsetType) cache_info->columns+
    nexus_info->region.x;
  length=(MagickSizeType) nexus_info->region.width*sizeof(PixelPacket);
  rows=nexus_info->region.height;
  extent=length*rows;
  p=nexus_info->pixels;
  y=0;
  switch (cache_info->type)
  {
    case MemoryCache:
    case MapCache:
    {
      PixelPacket
        *magick_restrict q;

      /*
        Write pixels to memory.
      */
      if ((cache_info->columns == nexus_info->region.width) &&
          (extent == (MagickSizeType) ((size_t) extent)))
        {
          length=extent;
          rows=1UL;
        }
      q=cache_info->pixels+offset;
      for (y=0; y < (ssize_t) rows; y++)
      {
        (void) memcpy(q,p,(size_t) length);
        p+=(ptrdiff_t) nexus_info->region.width;
        q+=(ptrdiff_t) cache_info->columns;
      }
      break;
    }
    case DiskCache:
    {
      /*
        Write pixels to disk.
      */
      LockSemaphoreInfo(cache_info->file_semaphore);
      if (OpenPixelCacheOnDisk(cache_info,IOMode) == MagickFalse)
        {
          ThrowFileException(exception,FileOpenError,"UnableToOpenFile",
            cache_info->cache_filename);
          UnlockSemaphoreInfo(cache_info->file_semaphore);
          return(MagickFalse);
        }
      if ((cache_info->columns == nexus_info->region.width) &&
          (extent <= MagickMaxBufferExtent))
        {
          length=extent;
          rows=1UL;
        }
      for (y=0; y < (ssize_t) rows; y++)
      {
        count=WritePixelCacheRegion(cache_info,cache_info->offset+offset*
          (MagickOffsetType) sizeof(*p),length,(const unsigned char *) p);
        if (count < (MagickOffsetType) length)
          break;
        p+=(ptrdiff_t) nexus_info->region.width;
        offset+=(MagickOffsetType) cache_info->columns;
      }
      if (IsFileDescriptorLimitExceeded() != MagickFalse)
        (void) ClosePixelCacheOnDisk(cache_info);
      UnlockSemaphoreInfo(cache_info->file_semaphore);
      break;
    }
    case DistributedCache:
    {
      RectangleInfo
        region;

      /*
        Write pixels to distributed cache.
      */
      LockSemaphoreInfo(cache_info->file_semaphore);
      region=nexus_info->region;
      if ((cache_info->columns != nexus_info->region.width) ||
          (extent > MagickMaxBufferExtent))
        region.height=1UL;
      else
        {
          length=extent;
          rows=1UL;
        }
      for (y=0; y < (ssize_t) rows; y++)
      {
        count=WriteDistributePixelCachePixels((DistributeCacheInfo *)
          cache_info->server_info,&region,length,(const unsigned char *) p);
        if (count != (MagickOffsetType) length)
          break;
        p+=(ptrdiff_t) nexus_info->region.width;
        region.y++;
      }
      UnlockSemaphoreInfo(cache_info->file_semaphore);
      break;
    }
    default:
      break;
  }
  if (y < (ssize_t) rows)
    {
      ThrowFileException(exception,CacheError,"UnableToWritePixelCache",
        cache_info->cache_filename);
      return(MagickFalse);
    }
  if ((cache_info->debug != MagickFalse) &&
      (CacheTick(nexus_info->region.y,cache_info->rows) != MagickFalse))
    (void) LogMagickEvent(CacheEvent,GetMagickModule(),
      "%s[%.20gx%.20g%+.20g%+.20g]",cache_info->filename,(double)
      nexus_info->region.width,(double) nexus_info->region.height,(double)
      nexus_info->region.x,(double) nexus_info->region.y);
  return(MagickTrue);
}
