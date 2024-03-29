/*
  Copyright 1999 ImageMagick Studio LLC, a non-profit organization
  dedicated to making software imaging solutions freely available.

  You may not use this file except in compliance with the License.  You may
  obtain a copy of the License at

    https://imagemagick.org/script/license.php

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  MagickCore image colormap methods.
*/
#ifndef MAGICKCORE_COLORMAP_PRIVATE_H
#define MAGICKCORE_COLORMAP_PRIVATE_H

#include "magick/image.h"
#include "magick/color.h"
#include "magick/exception-private.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

static inline IndexPacket ConstrainColormapIndex(Image *image,
  const ssize_t index)
{
  if ((index < 0) || (index >= (ssize_t) image->colors))
    {
      if (image->exception.severity != CorruptImageError)
        (void) ThrowMagickException(&image->exception,GetMagickModule(),
          CorruptImageError,"InvalidColormapIndex","`%s'",image->filename);
      return((IndexPacket) 0);
    }
  return((IndexPacket) index);
}

static inline MagickBooleanType IsValidColormapIndex(Image *image,
  const ssize_t index,IndexPacket *target,ExceptionInfo *exception)
{
  if ((index < 0) || (index >= (ssize_t) image->colors))
    {
      if (image->exception.severity != CorruptImageError)
        (void) ThrowMagickException(exception,GetMagickModule(),
          CorruptImageError,"InvalidColormapIndex","`%s'",image->filename);
      *target=(IndexPacket) 0;
      return(MagickFalse);
    }
  *target=(IndexPacket) index;
  return(MagickTrue);
}

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
