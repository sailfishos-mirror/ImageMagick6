/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%                  M   M   AAA    GGGG  IIIII   CCCC  K   K                   %
%                  MM MM  A   A  G        I    C      K  K                    %
%                  M M M  AAAAA  G GGG    I    C      KKK                     %
%                  M   M  A   A  G   G    I    C      K  K                    %
%                  M   M  A   A   GGGG  IIIII   CCCC  K   K                   %
%                                                                             %
%                                                                             %
%               Methods to Read or List ImageMagick Image formats             %
%                                                                             %
%                            Software Design                                  %
%                            Bob Friesenhahn                                  %
%                                 Cristy                                      %
%                             November 1998                                   %
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
*/

/*
  Include declarations.
*/
#include "magick/studio.h"
#include "magick/annotate.h"
#include "magick/blob.h"
#include "magick/blob-private.h"
#include "magick/cache.h"
#include "magick/coder.h"
#include "magick/client.h"
#include "magick/coder.h"
#include "magick/configure.h"
#include "magick/constitute.h"
#include "magick/delegate.h"
#include "magick/draw.h"
#include "magick/exception.h"
#include "magick/exception-private.h"
#include "magick/locale_.h"
#include "magick/log.h"
#include "magick/magic.h"
#include "magick/magick.h"
#include "magick/memory_.h"
#include "magick/mime.h"
#include "magick/module.h"
#include "magick/monitor-private.h"
#include "magick/mutex.h"
#include "magick/nt-base-private.h"
#include "magick/nt-feature.h"
#include "magick/opencl-private.h"
#include "magick/random_.h"
#include "magick/registry.h"
#include "magick/resource_.h"
#include "magick/policy.h"
#include "magick/semaphore.h"
#include "magick/semaphore-private.h"
#include "magick/signature-private.h"
#include "magick/splay-tree.h"
#include "magick/static.h"
#include "magick/string_.h"
#include "magick/string-private.h"
#include "magick/thread_.h"
#include "magick/thread-private.h"
#include "magick/timer-private.h"
#include "magick/token.h"
#include "magick/utility.h"
#include "magick/xwindow-private.h"

/*
  Define declarations.
*/
#if !defined(SIG_DFL)
# define SIG_DFL  ((SignalHandler *) 0)
#endif
#if !defined(SIG_ERR)
# define SIG_ERR  ((SignalHandler *) -1)
#endif
#if !defined(SIGMAX)
#define SIGMAX  64
#endif

/*
  Typedef declarations.
*/
typedef void SignalHandler(int);

/*
  Global declarations.
*/
static SemaphoreInfo
  *magick_semaphore = (SemaphoreInfo *) NULL;

static SignalHandler
  *signal_handlers[SIGMAX] = { (SignalHandler *) NULL };

static SplayTreeInfo
  *magick_list = (SplayTreeInfo *) NULL;

static volatile MagickBooleanType
  instantiate_magickcore = MagickFalse,
  magickcore_signal_in_progress = MagickFalse,
  magick_list_initialized = MagickFalse;

static int
  magick_precision = 0;

/*
  Forward declarations.
*/
static MagickBooleanType
  IsMagickTreeInstantiated(ExceptionInfo *);

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t I m a g e D e c o d e r                                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetImageDecoder() returns the image decoder.
%
%  The format of the GetImageDecoder method is:
%
%      DecodeImageHandler *GetImageDecoder(const MagickInfo *magick_info)
%
%  A description of each parameter follows:
%
%    o magick_info:  The magick info.
%
*/
MagickExport DecodeImageHandler *GetImageDecoder(const MagickInfo *magick_info)
{
  assert(magick_info != (MagickInfo *) NULL);
  assert(magick_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  return(magick_info->decoder);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t I m a g e E n c o d e r                                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetImageEncoder() returns the image encoder.
%
%  The format of the GetImageEncoder method is:
%
%      EncodeImageHandler *GetImageEncoder(const MagickInfo *magick_info)
%
%  A description of each parameter follows:
%
%    o magick_info:  The magick info.
%
*/
MagickExport EncodeImageHandler *GetImageEncoder(const MagickInfo *magick_info)
{
  assert(magick_info != (MagickInfo *) NULL);
  assert(magick_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  return(magick_info->encoder);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t I m a g e M a g i c k                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetImageMagick() searches for an image format that matches the specified
%  magick string.  If one is found, MagickTrue is returned otherwise
%  MagickFalse.
%
%  The format of the GetImageMagick method is:
%
%      MagickBooleanType GetImageMagick(const unsigned char *magick,
%        const size_t length,char *format)
%
%  A description of each parameter follows:
%
%    o magick: the image format we are searching for.
%
%    o length: the length of the binary string.
%
%    o format: the image format as determined by the magick bytes.
%
*/
MagickExport MagickBooleanType GetImageMagick(const unsigned char *magick,
  const size_t length,char *format)
{
  ExceptionInfo
    *exception;

  MagickBooleanType
    status;

  const MagickInfo
    *p;

  assert(magick != (const unsigned char *) NULL);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  exception=AcquireExceptionInfo();
  p=GetMagickInfo("*",exception);
  exception=DestroyExceptionInfo(exception);
  if (p == (const MagickInfo *) NULL)
    return(MagickFalse);
  status=MagickFalse;
  LockSemaphoreInfo(magick_semaphore);
  ResetSplayTreeIterator(magick_list);
  p=(const MagickInfo *) GetNextValueInSplayTree(magick_list);
  while (p != (const MagickInfo *) NULL)
  {
    if ((p->magick != (IsImageFormatHandler *) NULL) &&
        (p->magick(magick,length) != 0))
      {
        status=MagickTrue;
        (void) CopyMagickString(format,p->name,MaxTextExtent);
        break;
      }
    p=(const MagickInfo *) GetNextValueInSplayTree(magick_list);
  }
  UnlockSemaphoreInfo(magick_semaphore);
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t M a g i c k A d j o i n                                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetMagickAdjoin() returns MagickTrue if the magick adjoin is MagickTrue.
%
%  The format of the GetMagickAdjoin method is:
%
%      MagickBooleanType GetMagickAdjoin(const MagickInfo *magick_info)
%
%  A description of each parameter follows:
%
%    o magick_info:  The magick info.
%
*/
MagickExport MagickBooleanType GetMagickAdjoin(const MagickInfo *magick_info)
{
  assert(magick_info != (MagickInfo *) NULL);
  assert(magick_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  return(magick_info->adjoin);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t M a g i c k B l o b S u p p o r t                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetMagickBlobSupport() returns MagickTrue if the magick supports blobs.
%
%  The format of the GetMagickBlobSupport method is:
%
%      MagickBooleanType GetMagickBlobSupport(const MagickInfo *magick_info)
%
%  A description of each parameter follows:
%
%    o magick_info:  The magick info.
%
*/
MagickExport MagickBooleanType GetMagickBlobSupport(
  const MagickInfo *magick_info)
{
  assert(magick_info != (MagickInfo *) NULL);
  assert(magick_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  return(magick_info->blob_support);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t M a g i c k D e s c r i p t i o n                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetMagickDescription() returns the magick description.
%
%  The format of the GetMagickDescription method is:
%
%      const char *GetMagickDescription(const MagickInfo *magick_info)
%
%  A description of each parameter follows:
%
%    o magick_info:  The magick info.
%
*/
MagickExport const char *GetMagickDescription(const MagickInfo *magick_info)
{
  assert(magick_info != (MagickInfo *) NULL);
  assert(magick_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  return(magick_info->description);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t M a g i c k E n d i a n S u p p o r t                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetMagickEndianSupport() returns the MagickTrue if the coder respects
%  endianness other than MSBEndian.
%
%  The format of the GetMagickEndianSupport method is:
%
%      MagickBooleanType GetMagickEndianSupport(const MagickInfo *magick_info)
%
%  A description of each parameter follows:
%
%    o magick_info:  The magick info.
%
*/
MagickExport MagickBooleanType GetMagickEndianSupport(
  const MagickInfo *magick_info)
{
  assert(magick_info != (MagickInfo *) NULL);
  assert(magick_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  return(magick_info->endian_support);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t M a g i c k I n f o                                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetMagickInfo() returns a pointer MagickInfo structure that matches
%  the specified name.  If name is NULL, the head of the image format list
%  is returned.
%
%  The format of the GetMagickInfo method is:
%
%      const MagickInfo *GetMagickInfo(const char *name,Exception *exception)
%
%  A description of each parameter follows:
%
%    o name: the image format we are looking for.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport const MagickInfo *GetMagickInfo(const char *name,
  ExceptionInfo *exception)
{
  const MagickInfo
    *magick_info;

  /*
    Find named module attributes.
  */
  assert(exception != (ExceptionInfo *) NULL);
  if (IsMagickTreeInstantiated(exception) == MagickFalse)
    return((const MagickInfo *) NULL);
  magick_info=(const MagickInfo *) NULL;
  if ((name != (const char *) NULL) && (*name != '\0'))
    {
      LockSemaphoreInfo(magick_semaphore);
      if (LocaleCompare(name,"*") == 0)
#if defined(MAGICKCORE_BUILD_MODULES)
        (void) OpenModules(exception);
#else
        RegisterStaticModules();
#endif
      else
        {
          magick_info=(const MagickInfo *) GetValueFromSplayTree(magick_list,
            name);
          if (magick_info == (const MagickInfo *) NULL)
#if defined(MAGICKCORE_BUILD_MODULES)
            (void) OpenModule(name,exception);
#else
            (void) RegisterStaticModule(name,exception);
#endif
        }
      UnlockSemaphoreInfo(magick_semaphore);
    }
  if ((name == (const char *) NULL) || (LocaleCompare(name,"*") == 0))
    return((const MagickInfo *) GetRootValueFromSplayTree(magick_list));
  if (magick_info == (const MagickInfo *) NULL)
    magick_info=(const MagickInfo *) GetValueFromSplayTree(magick_list,name);
  return(magick_info);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t M a g i c k I n f o L i s t                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetMagickInfoList() returns any image formats that match the specified
%  pattern.
%
%  The format of the GetMagickInfoList function is:
%
%      const MagickInfo **GetMagickInfoList(const char *pattern,
%        size_t *number_formats,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o pattern: Specifies a pointer to a text string containing a pattern.
%
%    o number_formats:  This integer returns the number of formats in the list.
%
%    o exception: return any errors or warnings in this structure.
%
*/

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

static int MagickInfoCompare(const void *x,const void *y)
{
  const MagickInfo
    **p,
    **q;

  p=(const MagickInfo **) x,
  q=(const MagickInfo **) y;
  return(LocaleCompare((*p)->name,(*q)->name));
}

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

MagickExport const MagickInfo **GetMagickInfoList(const char *pattern,
  size_t *number_formats,ExceptionInfo *exception)
{
  const MagickInfo
    **formats;

  const MagickInfo
    *p;

  ssize_t
    i;

  /*
    Allocate magick list.
  */
  assert(pattern != (char *) NULL);
  assert(number_formats != (size_t *) NULL);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",pattern);
  *number_formats=0;
  p=GetMagickInfo("*",exception);
  if (p == (const MagickInfo *) NULL)
    return((const MagickInfo **) NULL);
  formats=(const MagickInfo **) AcquireQuantumMemory((size_t)
    GetNumberOfNodesInSplayTree(magick_list)+1UL,sizeof(*formats));
  if (formats == (const MagickInfo **) NULL)
    return((const MagickInfo **) NULL);
  /*
    Generate magick list.
  */
  LockSemaphoreInfo(magick_semaphore);
  ResetSplayTreeIterator(magick_list);
  p=(const MagickInfo *) GetNextValueInSplayTree(magick_list);
  for (i=0; p != (const MagickInfo *) NULL; )
  {
    if ((p->stealth == MagickFalse) &&
        (GlobExpression(p->name,pattern,MagickFalse) != MagickFalse))
      formats[i++]=p;
    p=(const MagickInfo *) GetNextValueInSplayTree(magick_list);
  }
  UnlockSemaphoreInfo(magick_semaphore);
  qsort((void *) formats,(size_t) i,sizeof(*formats),MagickInfoCompare);
  formats[i]=(MagickInfo *) NULL;
  *number_formats=(size_t) i;
  return(formats);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t M a g i c k L i s t                                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetMagickList() returns any image formats that match the specified pattern.
%
%  The format of the GetMagickList function is:
%
%      char **GetMagickList(const char *pattern,size_t *number_formats,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o pattern: Specifies a pointer to a text string containing a pattern.
%
%    o number_formats:  This integer returns the number of formats in the list.
%
%    o exception: return any errors or warnings in this structure.
%
*/

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

static int MagickCompare(const void *x,const void *y)
{
  const char
    **p,
    **q;

  p=(const char **) x;
  q=(const char **) y;
  return(LocaleCompare(*p,*q));
}

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

MagickExport char **GetMagickList(const char *pattern,
  size_t *number_formats,ExceptionInfo *exception)
{
  char
    **formats;

  const MagickInfo
    *p;

  ssize_t
    i;

  /*
    Allocate magick list.
  */
  assert(pattern != (char *) NULL);
  assert(number_formats != (size_t *) NULL);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",pattern);
  *number_formats=0;
  p=GetMagickInfo("*",exception);
  if (p == (const MagickInfo *) NULL)
    return((char **) NULL);
  formats=(char **) AcquireQuantumMemory((size_t)
    GetNumberOfNodesInSplayTree(magick_list)+1UL,sizeof(*formats));
  if (formats == (char **) NULL)
    return((char **) NULL);
  LockSemaphoreInfo(magick_semaphore);
  ResetSplayTreeIterator(magick_list);
  p=(const MagickInfo *) GetNextValueInSplayTree(magick_list);
  for (i=0; p != (const MagickInfo *) NULL; )
  {
    if ((p->stealth == MagickFalse) &&
        (GlobExpression(p->name,pattern,MagickFalse) != MagickFalse))
      formats[i++]=ConstantString(p->name);
    p=(const MagickInfo *) GetNextValueInSplayTree(magick_list);
  }
  UnlockSemaphoreInfo(magick_semaphore);
  qsort((void *) formats,(size_t) i,sizeof(*formats),MagickCompare);
  formats[i]=(char *) NULL;
  *number_formats=(size_t) i;
  return(formats);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t M a g i c k M i m e T y p e                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetMagickMimeType() returns the magick mime type.
%
%  The format of the GetMagickMimeType method is:
%
%      const char *GetMagickMimeType(const MagickInfo *magick_info)
%
%  A description of each parameter follows:
%
%    o magick_info:  The magick info.
%
*/
MagickExport const char *GetMagickMimeType(const MagickInfo *magick_info)
{
  assert(magick_info != (MagickInfo *) NULL);
  assert(magick_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  return(magick_info->mime_type);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   G e t M a g i c k P r e c i s i o n                                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetMagickPrecision() returns the maximum number of significant digits to be
%  printed.
%
%  The format of the GetMagickPrecision method is:
%
%      int GetMagickPrecision(void)
%
*/
MagickExport int GetMagickPrecision(void)
{
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  return(SetMagickPrecision(0));
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t M a g i c k R a w S u p p o r t                                     %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetMagickRawSupport() returns the MagickTrue if the coder is a raw format.
%
%  The format of the GetMagickRawSupport method is:
%
%      MagickBooleanType GetMagickRawSupport(const MagickInfo *magick_info)
%
%  A description of each parameter follows:
%
%    o magick_info:  The magick info.
%
*/
MagickExport MagickBooleanType GetMagickRawSupport(
  const MagickInfo *magick_info)
{
  assert(magick_info != (MagickInfo *) NULL);
  assert(magick_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  return(magick_info->raw);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t M a g i c k S e e k a b l e S t r e a m                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetMagickSeekableStream() returns MagickTrue if the magick supports a
%  seekable stream.
%
%  The format of the GetMagickSeekableStream method is:
%
%      MagickBooleanType GetMagickSeekableStream(const MagickInfo *magick_info)
%
%  A description of each parameter follows:
%
%    o magick_info:  The magick info.
%
*/
MagickExport MagickBooleanType GetMagickSeekableStream(
  const MagickInfo *magick_info)
{
  assert(magick_info != (MagickInfo *) NULL);
  assert(magick_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  return(magick_info->seekable_stream);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   G e t M a g i c k T h r e a d S u p p o r t                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetMagickThreadSupport() returns MagickTrue if the magick supports threads.
%
%  The format of the GetMagickThreadSupport method is:
%
%      MagickStatusType GetMagickThreadSupport(const MagickInfo *magick_info)
%
%  A description of each parameter follows:
%
%    o magick_info:  The magick info.
%
*/
MagickExport MagickStatusType GetMagickThreadSupport(
  const MagickInfo *magick_info)
{
  assert(magick_info != (MagickInfo *) NULL);
  assert(magick_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  return(magick_info->thread_support);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   I s M a g i c k T r e e I n s t a n t i a t e d                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  IsMagickTreeInstantiated() determines if the magick tree is instantiated.
%  If not, it instantiates the tree and returns it.
%
%  The format of the IsMagickTreeInstantiated() method is:
%
%      IsMagickTreeInstantiated(Exceptioninfo *exception)
%
%  A description of each parameter follows.
%
%    o exception: return any errors or warnings in this structure.
%
*/

static void *DestroyMagickNode(void *magick_info)
{
  MagickInfo
    *p;

  p=(MagickInfo *) magick_info;
  if (p->magick_module != (char *) NULL)
    p->magick_module=DestroyString(p->magick_module);
  if (p->note != (char *) NULL)
    p->note=DestroyString(p->note);
  if (p->mime_type != (char *) NULL)
    p->mime_type=DestroyString(p->mime_type);
  if (p->version != (char *) NULL)
    p->version=DestroyString(p->version);
  if (p->description != (char *) NULL)
    p->description=DestroyString(p->description);
  if (p->name != (char *) NULL)
    p->name=DestroyString(p->name);
  if (p->semaphore != (SemaphoreInfo *) NULL)
    DestroySemaphoreInfo(&p->semaphore);
  return(RelinquishMagickMemory(p));
}

static MagickBooleanType IsMagickTreeInstantiated(ExceptionInfo *exception)
{
  if (magick_list_initialized == MagickFalse)
    {
      if (magick_semaphore == (SemaphoreInfo *) NULL)
        ActivateSemaphoreInfo(&magick_semaphore);
      LockSemaphoreInfo(magick_semaphore);
      if (magick_list_initialized == MagickFalse)
        {
          MagickBooleanType
            status;

          MagickInfo
            *magick_info;

          magick_list=NewSplayTree(CompareSplayTreeString,(void *(*)(void *))
            NULL,DestroyMagickNode);
          if (magick_list == (SplayTreeInfo *) NULL)
            ThrowFatalException(ResourceLimitFatalError,
              "MemoryAllocationFailed");
          magick_info=SetMagickInfo("clipmask");
          magick_info->stealth=MagickTrue;
          status=AddValueToSplayTree(magick_list,magick_info->name,magick_info);
          if (status == MagickFalse)
            ThrowFatalException(ResourceLimitFatalError,
              "MemoryAllocationFailed");
#if defined(MAGICKCORE_MODULES_SUPPORT)
          (void) GetModuleInfo((char *) NULL,exception);
#endif
          magick_list_initialized=MagickTrue;
        }
      UnlockSemaphoreInfo(magick_semaphore);
    }
  return(magick_list != (SplayTreeInfo *) NULL ? MagickTrue : MagickFalse);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   I s M a g i c k C o n f l i c t                                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  IsMagickConflict() returns MagickTrue if the image format conflicts with a
%  logical drive (.e.g. X:).
%
%  The format of the IsMagickConflict method is:
%
%      MagickBooleanType IsMagickConflict(const char *magick)
%
%  A description of each parameter follows:
%
%    o magick: Specifies the image format.
%
*/
MagickExport MagickBooleanType IsMagickConflict(const char *magick)
{
  assert(magick != (char *) NULL);
#if defined(macintosh)
  return(MACIsMagickConflict(magick));
#elif defined(vms)
  return(VMSIsMagickConflict(magick));
#elif defined(MAGICKCORE_WINDOWS_SUPPORT)
  return(NTIsMagickConflict(magick));
#else
  return(MagickFalse);
#endif
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+  L i s t M a g i c k I n f o                                                %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ListMagickInfo() lists the image formats to a file.
%
%  The format of the ListMagickInfo method is:
%
%      MagickBooleanType ListMagickInfo(FILE *file,ExceptionInfo *exception)
%
%  A description of each parameter follows.
%
%    o file: A file handle.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport MagickBooleanType ListMagickInfo(FILE *file,
  ExceptionInfo *exception)
{
  const MagickInfo
    **magick_info;

  ssize_t
    i;

  size_t
    number_formats;

  ssize_t
    j;

  if (file == (FILE *) NULL)
    file=stdout;
  magick_info=GetMagickInfoList("*",&number_formats,exception);
  if (magick_info == (const MagickInfo **) NULL)
    return(MagickFalse);
  ClearMagickException(exception);
#if !defined(MAGICKCORE_MODULES_SUPPORT)
  (void) FormatLocaleFile(file,"   Format  Mode  Description\n");
#else
  (void) FormatLocaleFile(file,"   Format  Module    Mode  Description\n");
#endif
  (void) FormatLocaleFile(file,
    "--------------------------------------------------------"
    "-----------------------\n");
  for (i=0; i < (ssize_t) number_formats; i++)
  {
    if (magick_info[i]->stealth != MagickFalse)
      continue;
    (void) FormatLocaleFile(file,"%9s%c ",
      magick_info[i]->name != (char *) NULL ? magick_info[i]->name : "",
      magick_info[i]->blob_support != MagickFalse ? '*' : ' ');
#if defined(MAGICKCORE_MODULES_SUPPORT)
    {
      char
        magick_module[MaxTextExtent];

      *magick_module='\0';
      if (magick_info[i]->magick_module != (char *) NULL)
        (void) CopyMagickString(magick_module,magick_info[i]->magick_module,
          MaxTextExtent);
      (void) ConcatenateMagickString(magick_module,"          ",MaxTextExtent);
      magick_module[9]='\0';
      (void) FormatLocaleFile(file,"%9s ",magick_module);
    }
#endif
    (void) FormatLocaleFile(file,"%c%c%c ",magick_info[i]->decoder ? 'r' : '-',
      magick_info[i]->encoder ? 'w' : '-',magick_info[i]->encoder != NULL &&
      magick_info[i]->adjoin != MagickFalse ? '+' : '-');
    if (magick_info[i]->description != (char *) NULL)
      (void) FormatLocaleFile(file,"  %s",magick_info[i]->description);
    if (magick_info[i]->version != (char *) NULL)
      (void) FormatLocaleFile(file," (%s)",magick_info[i]->version);
    (void) FormatLocaleFile(file,"\n");
    if (magick_info[i]->note != (char *) NULL)
      {
        char
          **text;

        text=StringToList(magick_info[i]->note);
        if (text != (char **) NULL)
          {
            for (j=0; text[j] != (char *) NULL; j++)
            {
              (void) FormatLocaleFile(file,"             %s\n",text[j]);
              text[j]=DestroyString(text[j]);
            }
            text=(char **) RelinquishMagickMemory(text);
          }
      }
  }
  (void) FormatLocaleFile(file,"\n* native blob support\n");
  (void) FormatLocaleFile(file,"r read support\n");
  (void) FormatLocaleFile(file,"w write support\n");
  (void) FormatLocaleFile(file,"+ support for multiple images\n");
  (void) fflush(file);
  magick_info=(const MagickInfo **) RelinquishMagickMemory((void *)
    magick_info);
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%  I s M a g i c k C o r e I n s t a n t i a t e d                            %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  IsMagickCoreInstantiated() returns MagickTrue if the ImageMagick environment
%  is currently instantiated--  that is, MagickCoreGenesis() has been called but
%  MagickCoreTerminus() has not.
%
%  The format of the IsMagickCoreInstantiated method is:
%
%      MagickBooleanType IsMagickCoreInstantiated(void)
%
*/
MagickExport MagickBooleanType IsMagickCoreInstantiated(void)
{
  return(instantiate_magickcore);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   M a g i c k C o m p o n e n t G e n e s i s                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  MagickComponentGenesis() instantiates the magick component.
%
%  The format of the MagickComponentGenesis method is:
%
%      MagickBooleanType MagickComponentGenesis(void)
%
*/
MagickExport MagickBooleanType MagickComponentGenesis(void)
{
  if (magick_semaphore == (SemaphoreInfo *) NULL)
    magick_semaphore=AllocateSemaphoreInfo();
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   M a g i c k C o m p o n e n t T e r m i n u s                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  MagickComponentTerminus() destroys the magick component.
%
%  The format of the MagickComponentTerminus method is:
%
%      void MagickComponentTerminus(void)
%
*/
MagickExport void MagickComponentTerminus(void)
{
  if (magick_semaphore == (SemaphoreInfo *) NULL)
    ActivateSemaphoreInfo(&magick_semaphore);
  LockSemaphoreInfo(magick_semaphore);
  if (magick_list != (SplayTreeInfo *) NULL)
    {
      magick_list=DestroySplayTree(magick_list);
      magick_list_initialized=MagickFalse;
    }
  UnlockSemaphoreInfo(magick_semaphore);
  DestroySemaphoreInfo(&magick_semaphore);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   M a g i c k C o r e G e n e s i s                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  MagickCoreGenesis() initializes the MagickCore environment.
%
%  The format of the MagickCoreGenesis function is:
%
%      MagickCoreGenesis(const char *path,
%        const MagickBooleanType establish_signal_handlers)
%
%  A description of each parameter follows:
%
%    o path: the execution path of the current ImageMagick client.
%
%    o establish_signal_handlers: set to MagickTrue to use MagickCore's own
%      signal handlers for common signals.
%
*/

static SignalHandler *SetMagickSignalHandler(int signal_number,
  SignalHandler *handler)
{
#if defined(MAGICKCORE_HAVE_SIGACTION) && defined(MAGICKCORE_HAVE_SIGEMPTYSET)
  int
    status;

  sigset_t
    mask;

  struct sigaction
    action,
    previous_action;

  sigemptyset(&mask);
  sigaddset(&mask,signal_number);
  sigprocmask(SIG_BLOCK,&mask,NULL);
  action.sa_mask=mask;
  action.sa_handler=handler;
  action.sa_flags=0;
#if defined(SA_INTERRUPT)
  action.sa_flags|=SA_INTERRUPT;
#endif
#if defined(SA_ONSTACK)
  action.sa_flags|=SA_ONSTACK;
#endif
  previous_action.sa_handler=SIG_DFL;
  status=sigaction(signal_number,&action,&previous_action);
  if (status < 0)
    return(SIG_ERR);
  sigprocmask(SIG_UNBLOCK,&mask,NULL);
  return(previous_action.sa_handler);
#else
  return(signal(signal_number,handler));
#endif
}

static void MagickSignalHandler(int signal_number)
{
  if (magickcore_signal_in_progress != MagickFalse)
    (void) SetMagickSignalHandler(signal_number,signal_handlers[signal_number]);
  magickcore_signal_in_progress=MagickTrue;
  AsynchronousResourceComponentTerminus();
#if defined(SIGQUIT)
  if (signal_number == SIGQUIT)
    abort();
#endif
#if defined(SIGABRT)
  if (signal_number == SIGABRT)
    abort();
#endif
#if defined(SIGBUS)
  if (signal_number == SIGBUS)
    abort();
#endif
#if defined(SIGFPE)
  if (signal_number == SIGFPE)
    abort();
#endif
#if defined(SIGSEGV)
  if (signal_number == SIGSEGV)
    abort();
#endif
#if !defined(MAGICKCORE_HAVE__EXIT)
  exit(signal_number);
#else
#if defined(SIGHUP)
  if (signal_number == SIGHUP)
    _exit(signal_number);
#endif
#if defined(SIGINT)
  if (signal_number == SIGINT)
    _exit(signal_number);
#endif
#if defined(SIGTERM)
  if (signal_number == SIGTERM)
    _exit(signal_number);
#endif
#if defined(MAGICKCORE_HAVE_RAISE)
  if (signal_handlers[signal_number] != MagickSignalHandler)
    raise(signal_number);
#endif
  _exit(signal_number);  /* do not invoke registered atexit() methods */
#endif
}

static SignalHandler *RegisterMagickSignalHandler(int signal_number)
{
  SignalHandler
    *handler;

  handler=SetMagickSignalHandler(signal_number,MagickSignalHandler);
  if (handler == SIG_ERR)
    return(handler);
  if (handler != SIG_DFL)
    handler=SetMagickSignalHandler(signal_number,handler);
  else
    (void) LogMagickEvent(ConfigureEvent,GetMagickModule(),
      "Register handler for signal: %d",signal_number);
  return(handler);
}

static void SetClientNameAndPath(const char *path)
{
  char
    execution_path[MaxTextExtent],
    filename[MaxTextExtent];

#if defined(MAGICKCORE_WINDOWS_SUPPORT)
  if ((path != (const char *) NULL) && (IsPathAccessible(path) != MagickFalse))
#else
  if ((path != (const char *) NULL) && (*path == *DirectorySeparator) &&
      (IsPathAccessible(path) != MagickFalse))
#endif
    (void) CopyMagickString(execution_path,path,MaxTextExtent);
  else
    (void) GetExecutionPath(execution_path,MaxTextExtent);
  GetPathComponent(execution_path,TailPath,filename);
  (void) SetClientName(filename);
  GetPathComponent(execution_path,HeadPath,execution_path);
  (void) SetClientPath(execution_path);
}

MagickExport void MagickCoreGenesis(const char *path,
  const MagickBooleanType establish_signal_handlers)
{
  char
    *events;

  /*
    Initialize the Magick environment.
  */
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
  (void) putenv("MAGICK_THREAD_LIMIT=1");
#endif
#endif
  InitializeMagickMutex();
  LockMagickMutex();
  if (instantiate_magickcore != MagickFalse)
    {
      UnlockMagickMutex();
      return;
    }
  (void) SemaphoreComponentGenesis();
  (void) ExceptionComponentGenesis();
  SetClientNameAndPath(path);
  (void) LogComponentGenesis();
  (void) LocaleComponentGenesis();
  (void) RandomComponentGenesis();
  events=GetEnvironmentValue("MAGICK_DEBUG");
  if (events != (char *) NULL)
    {
      (void) SetLogEventMask(events);
      events=DestroyString(events);
    }
#if defined(MAGICKCORE_WINDOWS_SUPPORT)
  NTWindowsGenesis();
#endif
  if (establish_signal_handlers != MagickFalse)
    {
      /*
        Set signal handlers.
      */
#if defined(SIGABRT)
      if (signal_handlers[SIGABRT] == (SignalHandler *) NULL)
        signal_handlers[SIGABRT]=RegisterMagickSignalHandler(SIGABRT);
#endif
#if defined(SIGBUS)
      if (signal_handlers[SIGBUS] == (SignalHandler *) NULL)
        signal_handlers[SIGBUS]=RegisterMagickSignalHandler(SIGBUS);
#endif
#if defined(SIGSEGV)
      if (signal_handlers[SIGSEGV] == (SignalHandler *) NULL)
        signal_handlers[SIGSEGV]=RegisterMagickSignalHandler(SIGSEGV);
#endif
#if defined(SIGFPE)
      if (signal_handlers[SIGFPE] == (SignalHandler *) NULL)
        signal_handlers[SIGFPE]=RegisterMagickSignalHandler(SIGFPE);
#endif
#if defined(SIGHUP)
      if (signal_handlers[SIGHUP] == (SignalHandler *) NULL)
        signal_handlers[SIGHUP]=RegisterMagickSignalHandler(SIGHUP);
#endif
#if defined(SIGINT)
      if (signal_handlers[SIGINT] == (SignalHandler *) NULL)
        signal_handlers[SIGINT]=RegisterMagickSignalHandler(SIGINT);
#endif
#if defined(SIGQUIT)
      if (signal_handlers[SIGQUIT] == (SignalHandler *) NULL)
        signal_handlers[SIGQUIT]=RegisterMagickSignalHandler(SIGQUIT);
#endif
#if defined(SIGTERM)
      if (signal_handlers[SIGTERM] == (SignalHandler *) NULL)
        signal_handlers[SIGTERM]=RegisterMagickSignalHandler(SIGTERM);
#endif
#if defined(SIGXCPU)
      if (signal_handlers[SIGXCPU] == (SignalHandler *) NULL)
        signal_handlers[SIGXCPU]=RegisterMagickSignalHandler(SIGXCPU);
#endif
#if defined(SIGXFSZ)
      if (signal_handlers[SIGXFSZ] == (SignalHandler *) NULL)
        signal_handlers[SIGXFSZ]=RegisterMagickSignalHandler(SIGXFSZ);
#endif
    }
  /*
    Instantiate magick resources.
  */
  (void) ConfigureComponentGenesis();
  (void) PolicyComponentGenesis();
  (void) CacheComponentGenesis();
  (void) ResourceComponentGenesis();
  (void) CoderComponentGenesis();
  (void) MagickComponentGenesis();
#if defined(MAGICKCORE_MODULES_SUPPORT)
  (void) ModuleComponentGenesis();
#endif
  (void) DelegateComponentGenesis();
  (void) MagicComponentGenesis();
  (void) ColorComponentGenesis();
  (void) TypeComponentGenesis();
  (void) MimeComponentGenesis();
  (void) AnnotateComponentGenesis();
#if defined(MAGICKCORE_X11_DELEGATE)
  (void) XComponentGenesis();
#endif
  (void) RegistryComponentGenesis();
  (void) MonitorComponentGenesis();
  instantiate_magickcore=MagickTrue;
  UnlockMagickMutex();
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   M a g i c k C o r e T e r m i n u s                                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  MagickCoreTerminus() is a function in the ImageMagick library that is
%  used to clean up and release resources when shutting down an application
%  that uses ImageMagick. This function should be called in the primary thread
%  of the application's process during the shutdown process. It's crucial that
%  this function is invoked only after any threads that are using ImageMagick
%  functions have terminated.
%
%  ImageMagick might internally use threads via OpenMP (a method for parallel
%  programming). As a result, it's important to ensure that any function calls
%  into ImageMagick have completed before calling MagickCoreTerminus(). This
%  prevents issues with OpenMP worker threads accessing resources that are
%  destroyed by this termination function.
%
%  If OpenMP is being used (starting from version 5.0), the OpenMP
%  implementation itself handles starting and stopping worker threads and
%  allocating and freeing resources using its own methods. This means that
%  after calling MagickCoreTerminus(), some OpenMP resources and worker
%  threads might still remain allocated. To address this, the function
%  omp_pause_resource_all(omp_pause_hard) can be invoked. This function,
%  introduced in OpenMP version 5.0, ensures that any resources allocated by
%  OpenMP (such as threads and thread-specific memory) are freed. It's
%  recommended to call this function after MagickCoreTerminus() has completed
%  its execution.
%
%  The format of the MagickCoreTerminus function is:
%
%      MagickCoreTerminus(void)
%
*/
MagickExport void MagickCoreTerminus(void)
{
  InitializeMagickMutex();
  LockMagickMutex();
  if (instantiate_magickcore == MagickFalse)
    {
      UnlockMagickMutex();
      return;
    }
  MonitorComponentTerminus();
  RegistryComponentTerminus();
  AnnotateComponentTerminus();
  MimeComponentTerminus();
  TypeComponentTerminus();
#if defined(MAGICKCORE_OPENCL_SUPPORT)
  OpenCLTerminus();
#endif
  ColorComponentTerminus();
#if defined(MAGICKCORE_WINDOWS_SUPPORT)
  NTWindowsTerminus();
#endif
  MagicComponentTerminus();
  DelegateComponentTerminus();
  MagickComponentTerminus();
#if !defined(MAGICKCORE_BUILD_MODULES)
  UnregisterStaticModules();
#endif
#if defined(MAGICKCORE_MODULES_SUPPORT)
  ModuleComponentTerminus();
#endif
#if defined(MAGICKCORE_X11_DELEGATE)
  XComponentTerminus();
#endif
  CoderComponentTerminus();
  AsynchronousResourceComponentTerminus();
  ResourceComponentTerminus();
  CacheComponentTerminus();
  PolicyComponentTerminus();
  ConfigureComponentTerminus();
  RandomComponentTerminus();
  LocaleComponentTerminus();
  LogComponentTerminus();
  ExceptionComponentTerminus();
  instantiate_magickcore=MagickFalse;
  UnlockMagickMutex();
  SemaphoreComponentTerminus();
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   R e g i s t e r M a g i c k I n f o                                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  RegisterMagickInfo() adds attributes for a particular image format to the
%  list of supported formats.  The attributes include the image format name,
%  a method to read and/or write the format, whether the format supports the
%  saving of more than one frame to the same file or blob, whether the format
%  supports native in-memory I/O, and a brief description of the format.
%
%  The format of the RegisterMagickInfo method is:
%
%      MagickInfo *RegisterMagickInfo(MagickInfo *magick_info)
%
%  A description of each parameter follows:
%
%    o magick_info: the magick info.
%
*/
MagickExport MagickInfo *RegisterMagickInfo(MagickInfo *magick_info)
{
  MagickBooleanType
    status;

  /*
    Register a new image format.
  */
  assert(magick_info != (MagickInfo *) NULL);
  assert(magick_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",magick_info->name);
  if (magick_list == (SplayTreeInfo *) NULL)
    return((MagickInfo *) NULL);
  if (((magick_info->thread_support & DecoderThreadSupport) == 0) ||
      ((magick_info->thread_support & EncoderThreadSupport) == 0))
    magick_info->semaphore=AllocateSemaphoreInfo();
  status=AddValueToSplayTree(magick_list,magick_info->name,magick_info);
  if (status == MagickFalse)
    ThrowFatalException(ResourceLimitFatalError,"MemoryAllocationFailed");
  return(magick_info);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   R e s e t M a g i c k P r e c i s i o n                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ResetMagickPrecision() resets the magick_precision value.
%
%  The format of the ResetMagickPrecision method is:
%
%      void ResetMagickPrecision(void)
%
*/
MagickPrivate void ResetMagickPrecision(void)
{
  magick_precision=0;
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   S e t M a g i c k I n f o                                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  SetMagickInfo() allocates a MagickInfo structure and initializes the members
%  to default values.
%
%  The format of the SetMagickInfo method is:
%
%      MagickInfo *SetMagickInfo(const char *name)
%
%  A description of each parameter follows:
%
%    o magick_info: Method SetMagickInfo returns the allocated and initialized
%      MagickInfo structure.
%
%    o name: a character string that represents the image format associated
%      with the MagickInfo structure.
%
*/
MagickExport MagickInfo *SetMagickInfo(const char *name)
{
  MagickInfo
    *magick_info;

  assert(name != (const char *) NULL);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",name);
  magick_info=(MagickInfo *) AcquireMagickMemory(sizeof(*magick_info));
  if (magick_info == (MagickInfo *) NULL)
    ThrowFatalException(ResourceLimitFatalError,"MemoryAllocationFailed");
  (void) memset(magick_info,0,sizeof(*magick_info));
  magick_info->name=ConstantString(name);
  magick_info->adjoin=MagickTrue;
  magick_info->blob_support=MagickTrue;
  magick_info->thread_support=(MagickStatusType) (DecoderThreadSupport |
    EncoderThreadSupport);
  magick_info->signature=MagickCoreSignature;
  return(magick_info);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   S e t M a g i c k P r e c i s i o n                                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  SetMagickPrecision() sets the maximum number of significant digits to be
%  printed.
%
%  An input argument of 0 returns the current precision setting.
%
%  A negative value forces the precision to reset to a default value according
%  to the environment variable "MAGICK_PRECISION", the current 'policy'
%  configuration setting, or the default value of '6', in that order.
%
%  The format of the SetMagickPrecision method is:
%
%      int SetMagickPrecision(const int precision)
%
%  A description of each parameter follows:
%
%    o precision: set the maximum number of significant digits to be printed.
%
*/
MagickExport int SetMagickPrecision(const int precision)
{
#define MagickPrecision  (4+MAGICKCORE_QUANTUM_DEPTH/8)

  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  if (precision > 0)
    magick_precision=precision;
  if ((precision < 0) || (magick_precision == 0))
    {
      char
        *limit;

      ExceptionInfo
        *exception = AcquireExceptionInfo();

      magick_precision=MagickPrecision;
      limit=(char *) GetImageRegistry(StringRegistryType,"precision",exception);
      exception=DestroyExceptionInfo(exception);
      if (limit == (char *) NULL)
        limit=GetEnvironmentValue("MAGICK_PRECISION");
      if (limit == (char *) NULL)
        limit=GetPolicyValue("system:precision");  /* deprecated */
      if (limit != (char *) NULL)
        {
          magick_precision=StringToInteger(limit);
          limit=DestroyString(limit);
        }
    }
  return(magick_precision);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   U n r e g i s t e r M a g i c k I n f o                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  UnregisterMagickInfo() removes a name from the magick info list.  It returns
%  MagickFalse if the name does not exist in the list otherwise MagickTrue.
%
%  The format of the UnregisterMagickInfo method is:
%
%      MagickBooleanType UnregisterMagickInfo(const char *name)
%
%  A description of each parameter follows:
%
%    o name: a character string that represents the image format we are
%      looking for.
%
*/
MagickExport MagickBooleanType UnregisterMagickInfo(const char *name)
{
  const MagickInfo
    *p;

  MagickBooleanType
    status;

  assert(name != (const char *) NULL);
  if (magick_list == (SplayTreeInfo *) NULL)
    return(MagickFalse);
  if (GetNumberOfNodesInSplayTree(magick_list) == 0)
    return(MagickFalse);
  LockSemaphoreInfo(magick_semaphore);
  ResetSplayTreeIterator(magick_list);
  p=(const MagickInfo *) GetNextValueInSplayTree(magick_list);
  while (p != (const MagickInfo *) NULL)
  {
    if (LocaleCompare(p->name,name) == 0)
      break;
    p=(const MagickInfo *) GetNextValueInSplayTree(magick_list);
  }
  status=DeleteNodeByValueFromSplayTree(magick_list,p);
  UnlockSemaphoreInfo(magick_semaphore);
  return(status);
}
