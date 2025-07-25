/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%           AAA   N   N  N   N   OOO   TTTTT   AAA   TTTTT  EEEEE             %
%          A   A  NN  N  NN  N  O   O    T    A   A    T    E                 %
%          AAAAA  N N N  N N N  O   O    T    AAAAA    T    EEE               %
%          A   A  N  NN  N  NN  O   O    T    A   A    T    E                 %
%          A   A  N   N  N   N   OOO     T    A   A    T    EEEEE             %
%                                                                             %
%                                                                             %
%                   MagickCore Image Annotation Methods                       %
%                                                                             %
%                              Software Design                                %
%                                   Cristy                                    %
%                                 July 1992                                   %
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
% Digital Applications (www.digapp.com) contributed the stroked text algorithm.
% It was written by Leonard Rosenthol.
%
%
*/

/*
  Include declarations.
*/
#include "magick/studio.h"
#include "magick/annotate.h"
#include "magick/attribute.h"
#include "magick/cache-private.h"
#include "magick/cache-view.h"
#include "magick/channel.h"
#include "magick/client.h"
#include "magick/color.h"
#include "magick/color-private.h"
#include "magick/colorspace-private.h"
#include "magick/composite.h"
#include "magick/composite-private.h"
#include "magick/constitute.h"
#include "magick/draw.h"
#include "magick/draw-private.h"
#include "magick/exception.h"
#include "magick/exception-private.h"
#include "magick/gem.h"
#include "magick/geometry.h"
#include "magick/image-private.h"
#include "magick/log.h"
#include "magick/quantum.h"
#include "magick/quantum-private.h"
#include "magick/pixel-accessor.h"
#include "magick/policy.h"
#include "magick/property.h"
#include "magick/resource_.h"
#include "magick/semaphore.h"
#include "magick/statistic.h"
#include "magick/string_.h"
#include "magick/token.h"
#include "magick/token-private.h"
#include "magick/transform.h"
#include "magick/type.h"
#include "magick/utility.h"
#include "magick/utility-private.h"
#include "magick/xwindow-private.h"
#if defined(MAGICKCORE_FREETYPE_DELEGATE)
#if defined(__MINGW32__)
#  undef interface
#endif
#include <ft2build.h>
#if defined(FT_FREETYPE_H)
#  include FT_FREETYPE_H
#else
#  include <freetype/freetype.h>
#endif
#if defined(FT_GLYPH_H)
#  include FT_GLYPH_H
#else
#  include <freetype/ftglyph.h>
#endif
#if defined(FT_OUTLINE_H)
#  include FT_OUTLINE_H
#else
#  include <freetype/ftoutln.h>
#endif
#if defined(FT_BBOX_H)
#  include FT_BBOX_H
#else
#  include <freetype/ftbbox.h>
#endif /* defined(FT_BBOX_H) */
#endif
#if defined(MAGICKCORE_RAQM_DELEGATE)
#include <raqm.h>
#endif
typedef struct _GraphemeInfo
{
  ssize_t
    index,
    x_offset,
    x_advance,
    y_offset,
    y_advance;

  size_t
    cluster;
} GraphemeInfo;

/*
  Annotate semaphores.
*/
static SemaphoreInfo
  *annotate_semaphore = (SemaphoreInfo *) NULL;

/*
  Forward declarations.
*/
static MagickBooleanType
  RenderType(Image *,const DrawInfo *,const PointInfo *,TypeMetric *),
  RenderPostscript(Image *,const DrawInfo *,const PointInfo *,TypeMetric *),
  RenderFreetype(Image *,const DrawInfo *,const char *,const PointInfo *,
    TypeMetric *),
  RenderX11(Image *,const DrawInfo *,const PointInfo *,TypeMetric *);

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   A n n o t a t e C o m p o n e n t G e n e s i s                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  AnnotateComponentGenesis() instantiates the annotate component.
%
%  The format of the AnnotateComponentGenesis method is:
%
%      MagickBooleanType AnnotateComponentGenesis(void)
%
*/
MagickExport MagickBooleanType AnnotateComponentGenesis(void)
{
  if (annotate_semaphore == (SemaphoreInfo *) NULL)
    annotate_semaphore=AllocateSemaphoreInfo();
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   A n n o t a t e C o m p o n e n t T e r m i n u s                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  AnnotateComponentTerminus() destroys the annotate component.
%
%  The format of the AnnotateComponentTerminus method is:
%
%      AnnotateComponentTerminus(void)
%
*/
MagickExport void AnnotateComponentTerminus(void)
{
  if (annotate_semaphore == (SemaphoreInfo *) NULL)
    ActivateSemaphoreInfo(&annotate_semaphore);
  DestroySemaphoreInfo(&annotate_semaphore);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   A n n o t a t e I m a g e                                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  AnnotateImage() annotates an image with text.
%
%  The format of the AnnotateImage method is:
%
%      MagickBooleanType AnnotateImage(Image *image,DrawInfo *draw_info)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o draw_info: the draw info.
%
*/
MagickExport MagickBooleanType AnnotateImage(Image *image,
  const DrawInfo *draw_info)
{
  char
    *p,
    color[MaxTextExtent],
    primitive[MaxTextExtent],
    *text,
    **textlist;

  DrawInfo
    *annotate,
    *annotate_info;

  GeometryInfo
    geometry_info;

  MagickBooleanType
    status;

  PixelPacket
    pixel;

  PointInfo
    offset;

  RectangleInfo
    geometry;

  ssize_t
    i;

  TypeMetric
    metrics;

  size_t
    height,
    number_lines;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(draw_info != (DrawInfo *) NULL);
  assert(draw_info->signature == MagickCoreSignature);
  if (draw_info->text == (char *) NULL)
    return(MagickFalse);
  if (*draw_info->text == '\0')
    return(MagickTrue);
  annotate=CloneDrawInfo((ImageInfo *) NULL,draw_info);
  text=annotate->text;
  annotate->text=(char *) NULL;
  annotate_info=CloneDrawInfo((ImageInfo *) NULL,draw_info);
  number_lines=1;
  for (p=text; *p != '\0'; p++)
    if (*p == '\n')
      number_lines++;
  textlist=(char **) AcquireQuantumMemory(number_lines+1,sizeof(*textlist));
  if (textlist == (char **) NULL)
    {
      annotate_info=DestroyDrawInfo(annotate_info);
      annotate=DestroyDrawInfo(annotate);
      text=DestroyString(text);
      return(MagickFalse);
    }
  p=text;
  for (i=0; i < (ssize_t) number_lines; i++)
  {
    char
      *q;

    textlist[i]=p;
    for (q=p; *q != '\0'; q++)
      if ((*q == '\r') || (*q == '\n'))
        break;
    if (*q == '\r')
      {
        *q='\0';
        q++;
      }
    *q='\0';
    p=q+1;
  }
  textlist[i]=(char *) NULL;
  SetGeometry(image,&geometry);
  SetGeometryInfo(&geometry_info);
  if (annotate_info->geometry != (char *) NULL)
    {
      (void) ParsePageGeometry(image,annotate_info->geometry,&geometry,
        &image->exception);
      (void) ParseGeometry(annotate_info->geometry,&geometry_info);
    }
  if (SetImageStorageClass(image,DirectClass) == MagickFalse)
    {
      annotate_info=DestroyDrawInfo(annotate_info);
      annotate=DestroyDrawInfo(annotate);
      textlist=(char **) RelinquishMagickMemory(textlist);
      text=DestroyString(text);
      return(MagickFalse);
    }
  if (IsGrayColorspace(image->colorspace) != MagickFalse)
    (void) SetImageColorspace(image,sRGBColorspace);
  status=MagickTrue;
  (void) memset(&metrics,0,sizeof(metrics));
  for (i=0; textlist[i] != (char *) NULL; i++)
  {
    if (*textlist[i] == '\0')
      continue;
    /*
      Position text relative to image.
    */
    annotate_info->affine.tx=geometry_info.xi-image->page.x;
    annotate_info->affine.ty=geometry_info.psi-image->page.y;
    (void) CloneString(&annotate->text,textlist[i]);
    if ((metrics.width == 0) || (annotate->gravity != NorthWestGravity))
      (void) GetTypeMetrics(image,annotate,&metrics);
    height=CastDoubleToUnsigned(metrics.ascent-metrics.descent+0.5);
    if (height == 0)
      height=draw_info->pointsize;
    height+=(size_t) floor(draw_info->interline_spacing+0.5);
    switch (annotate->gravity)
    {
      case UndefinedGravity:
      default:
      {
        offset.x=annotate_info->affine.tx+i*annotate_info->affine.ry*height;
        offset.y=annotate_info->affine.ty+i*annotate_info->affine.sy*height;
        break;
      }
      case (GravityType) NorthWestGravity:
      {
        offset.x=(geometry.width == 0 ? -1.0 : 1.0)*annotate_info->affine.tx+i*
          annotate_info->affine.ry*height+annotate_info->affine.ry*
          (metrics.ascent+metrics.descent);
        offset.y=(geometry.height == 0 ? -1.0 : 1.0)*annotate_info->affine.ty+i*
          annotate_info->affine.sy*height+annotate_info->affine.sy*
          metrics.ascent;
        break;
      }
      case (GravityType) NorthGravity:
      {
        offset.x=(geometry.width == 0 ? -1.0 : 1.0)*annotate_info->affine.tx+
          geometry.width/2.0+i*annotate_info->affine.ry*height-
          annotate_info->affine.sx*metrics.width/2.0+annotate_info->affine.ry*
          (metrics.ascent+metrics.descent);
        offset.y=(geometry.height == 0 ? -1.0 : 1.0)*annotate_info->affine.ty+i*
          annotate_info->affine.sy*height+annotate_info->affine.sy*
          metrics.ascent-annotate_info->affine.rx*metrics.width/2.0;
        break;
      }
      case (GravityType) NorthEastGravity:
      {
        offset.x=(geometry.width == 0 ? 1.0 : -1.0)*annotate_info->affine.tx+
          geometry.width+i*annotate_info->affine.ry*height-
          annotate_info->affine.sx*metrics.width+annotate_info->affine.ry*
          (metrics.ascent+metrics.descent)-1.0;
        offset.y=(geometry.height == 0 ? -1.0 : 1.0)*annotate_info->affine.ty+i*
          annotate_info->affine.sy*height+annotate_info->affine.sy*
          metrics.ascent-annotate_info->affine.rx*metrics.width;
        break;
      }
      case (GravityType) WestGravity:
      {
        offset.x=(geometry.width == 0 ? -1.0 : 1.0)*annotate_info->affine.tx+i*
          annotate_info->affine.ry*height+annotate_info->affine.ry*
          (metrics.ascent+metrics.descent-(number_lines-1.0)*height)/2.0;
        offset.y=(geometry.height == 0 ? -1.0 : 1.0)*annotate_info->affine.ty+
          geometry.height/2.0+i*annotate_info->affine.sy*height+
          annotate_info->affine.sy*(metrics.ascent+metrics.descent-
          (number_lines-1.0)*height)/2.0;
        break;
      }
      case (GravityType) StaticGravity:
      case (GravityType) CenterGravity:
      {
        offset.x=(geometry.width == 0 ? -1.0 : 1.0)*annotate_info->affine.tx+
          geometry.width/2.0+i*annotate_info->affine.ry*height-
          annotate_info->affine.sx*metrics.width/2.0+annotate_info->affine.ry*
          (metrics.ascent+metrics.descent-(number_lines-1)*height)/2.0;
        offset.y=(geometry.height == 0 ? -1.0 : 1.0)*annotate_info->affine.ty+
          geometry.height/2.0+i*annotate_info->affine.sy*height-
          annotate_info->affine.rx*metrics.width/2.0+annotate_info->affine.sy*
          (metrics.ascent+metrics.descent-(number_lines-1.0)*height)/2.0;
        break;
      }
      case (GravityType) EastGravity:
      {
        offset.x=(geometry.width == 0 ? 1.0 : -1.0)*annotate_info->affine.tx+
          geometry.width+i*annotate_info->affine.ry*height-
          annotate_info->affine.sx*metrics.width+annotate_info->affine.ry*
          (metrics.ascent+metrics.descent-(number_lines-1.0)*height)/2.0-1.0;
        offset.y=(geometry.height == 0 ? -1.0 : 1.0)*annotate_info->affine.ty+
          geometry.height/2.0+i*annotate_info->affine.sy*height-
          annotate_info->affine.rx*metrics.width+annotate_info->affine.sy*
          (metrics.ascent+metrics.descent-(number_lines-1.0)*height)/2.0;
        break;
      }
      case (GravityType) SouthWestGravity:
      {
        offset.x=(geometry.width == 0 ? -1.0 : 1.0)*annotate_info->affine.tx+i*
          annotate_info->affine.ry*height-annotate_info->affine.ry*
          (number_lines-1.0)*height;
        offset.y=(geometry.height == 0 ? 1.0 : -1.0)*annotate_info->affine.ty+
          geometry.height+i*annotate_info->affine.sy*height-
          annotate_info->affine.sy*(number_lines-1.0)*height+metrics.descent;
        break;
      }
      case (GravityType) SouthGravity:
      {
        offset.x=(geometry.width == 0 ? -1.0 : 1.0)*annotate_info->affine.tx+
          geometry.width/2.0+i*annotate_info->affine.ry*height-
          annotate_info->affine.sx*metrics.width/2.0-annotate_info->affine.ry*
          (number_lines-1.0)*height/2.0;
        offset.y=(geometry.height == 0 ? 1.0 : -1.0)*annotate_info->affine.ty+
          geometry.height+i*annotate_info->affine.sy*height-
          annotate_info->affine.rx*metrics.width/2.0-annotate_info->affine.sy*
          (number_lines-1.0)*height+metrics.descent;
        break;
      }
      case (GravityType) SouthEastGravity:
      {
        offset.x=(geometry.width == 0 ? 1.0 : -1.0)*annotate_info->affine.tx+
          geometry.width+i*annotate_info->affine.ry*height-
          annotate_info->affine.sx*metrics.width-annotate_info->affine.ry*
          (number_lines-1.0)*height-1.0;
        offset.y=(geometry.height == 0 ? 1.0 : -1.0)*annotate_info->affine.ty+
          geometry.height+i*annotate_info->affine.sy*height-
          annotate_info->affine.rx*metrics.width-annotate_info->affine.sy*
          (number_lines-1.0)*height+metrics.descent;
        break;
      }
    }
    switch (annotate->align)
    {
      case LeftAlign:
      {
        offset.x=annotate_info->affine.tx+i*annotate_info->affine.ry*height;
        offset.y=annotate_info->affine.ty+i*annotate_info->affine.sy*height;
        break;
      }
      case CenterAlign:
      {
        offset.x=annotate_info->affine.tx+i*annotate_info->affine.ry*height-
          annotate_info->affine.sx*metrics.width/2.0;
        offset.y=annotate_info->affine.ty+i*annotate_info->affine.sy*height-
          annotate_info->affine.rx*metrics.width/2.0;
        break;
      }
      case RightAlign:
      {
        offset.x=annotate_info->affine.tx+i*annotate_info->affine.ry*height-
          annotate_info->affine.sx*metrics.width;
        offset.y=annotate_info->affine.ty+i*annotate_info->affine.sy*height-
          annotate_info->affine.rx*metrics.width;
        break;
      }
      default:
        break;
    }
    if (draw_info->undercolor.opacity != TransparentOpacity)
      {
        DrawInfo
          *undercolor_info;

        /*
          Text box.
        */
        undercolor_info=CloneDrawInfo((ImageInfo *) NULL,(DrawInfo *) NULL);
        undercolor_info->fill=draw_info->undercolor;
        undercolor_info->affine=draw_info->affine;
        undercolor_info->affine.tx=offset.x-draw_info->affine.ry*metrics.ascent;
        undercolor_info->affine.ty=offset.y-draw_info->affine.sy*metrics.ascent;
        (void) FormatLocaleString(primitive,MaxTextExtent,
          "rectangle 0.0,0.0 %g,%g",metrics.origin.x,(double) height);
        (void) CloneString(&undercolor_info->primitive,primitive);
        (void) DrawImage(image,undercolor_info);
        (void) DestroyDrawInfo(undercolor_info);
      }
    annotate_info->affine.tx=offset.x;
    annotate_info->affine.ty=offset.y;
    pixel=annotate_info->fill;
    if (annotate_info->stroke.opacity != TransparentOpacity)
      pixel=annotate_info->stroke;
    (void) QueryColorname(image,&pixel,SVGCompliance,color,&image->exception);
    (void) FormatLocaleString(primitive,MagickPathExtent,"stroke %s "
      "stroke-width %g line 0,0 %g,0",color,(double)
      metrics.underline_thickness,(double) metrics.width);
    /*
      Annotate image with text.
    */
    switch (annotate->decorate)
    {
      case OverlineDecoration:
      {
        annotate_info->affine.ty-=(draw_info->affine.sy*(metrics.ascent+
          metrics.descent-metrics.underline_position));
        (void) CloneString(&annotate_info->primitive,primitive);
        (void) DrawImage(image,annotate_info);
        break;
      }
      case UnderlineDecoration:
      {
        annotate_info->affine.ty-=(draw_info->affine.sy*
          metrics.underline_position);
        (void) CloneString(&annotate_info->primitive,primitive);
        (void) DrawImage(image,annotate_info);
        break;
      }
      default:
        break;
    }
    status=RenderType(image,annotate,&offset,&metrics);
    if (status == MagickFalse)
      break;
    if (annotate->decorate == LineThroughDecoration)
      {
        annotate_info->affine.ty-=(draw_info->affine.sy*(height+
          metrics.underline_position+metrics.descent*2)/2.0);
        (void) CloneString(&annotate_info->primitive,primitive);
        (void) DrawImage(image,annotate_info);
      }
  }
  /*
    Relinquish resources.
  */
  annotate_info=DestroyDrawInfo(annotate_info);
  annotate=DestroyDrawInfo(annotate);
  textlist=(char **) RelinquishMagickMemory(textlist);
  text=DestroyString(text);
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%  F o r m a t M a g i c k C a p t i o n                                      %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  FormatMagickCaption() formats a caption so that it fits within the image
%  width.  It returns the number of lines in the formatted caption.
%
%  The format of the FormatMagickCaption method is:
%
%      ssize_t FormatMagickCaption(Image *image,DrawInfo *draw_info,
%        const MagickBooleanType split,TypeMetric *metrics,char **caption)
%
%  A description of each parameter follows.
%
%    o image:  The image.
%
%    o draw_info: the draw info.
%
%    o split: when no convenient line breaks-- insert newline.
%
%    o metrics: Return the font metrics in this structure.
%
%    o caption: the caption.
%
*/

static inline char *ReplaceSpaceWithNewline(char **caption,char *space)
{
  size_t
    octets;

  if ((caption == (char **) NULL) || (space == (char *) NULL))
    return((char *) NULL);
  octets=(size_t) GetUTFOctets(space);
  if (octets == 1)
    *space='\n';
  else
    {
      char
        *target;

      size_t
        length;

      ssize_t
        offset;

      length=strlen(*caption);
      *space='\n';
      offset=space-(*caption);
      if (offset >= 0)
        {
          target=AcquireString(*caption);
          CopyMagickString(target,*caption,(size_t) offset+2);
          ConcatenateMagickString(target,space+octets,length);
          (void) DestroyString(*caption);
          *caption=target;
          space=(*caption)+offset;
        }
    }
  return(space);
}

MagickExport ssize_t FormatMagickCaption(Image *image,DrawInfo *draw_info,
  const MagickBooleanType split,TypeMetric *metrics,char **caption)
{
  char
    *p,
    *q,
    *s;

  MagickBooleanType
    status;

  size_t
    width;

  ssize_t
    i,
    n;

  if ((caption == (char **) NULL) || (*caption == (char *) NULL))
    return(-1);
  q=draw_info->text;
  s=(char *) NULL;
  width=0;
  for (p=(*caption); GetUTFCode(p) != 0; p+=(ptrdiff_t) GetUTFOctets(p))
  {
    int
      code;

    code=GetUTFCode(p);
    if (code == '\n')
      {
        q=draw_info->text;
        continue;
      }
    if ((IsUTFSpace(code) != MagickFalse) &&
        (IsNonBreakingUTFSpace(code) == MagickFalse))
      {
        s=p;
        if (width > image->columns)
          p=ReplaceSpaceWithNewline(caption,s);
      }
    for (i=0; i < (ssize_t) GetUTFOctets(p); i++)
      *q++=(*(p+i));
    *q='\0';
    status=GetTypeMetrics(image,draw_info,metrics);
    if (status == MagickFalse)
      break;
    width=CastDoubleToUnsigned(metrics->width+draw_info->stroke_width+0.5);
    if (width <= image->columns)
      continue;
    if (s != (char *) NULL)
      p=ReplaceSpaceWithNewline(caption,s);
    else
      if ((split != MagickFalse) || (GetUTFOctets(p) > 2))
        {
          /*
            No convenient line breaks-- insert newline.
          */
          n=p-(*caption);
          if ((n > 0) && ((*caption)[n-1] != '\n'))
            {
              char
                *target;

              target=AcquireString(*caption);
              CopyMagickString(target,*caption,(size_t) n+1);
              ConcatenateMagickString(target,"\n",strlen(*caption)+1);
              ConcatenateMagickString(target,p,strlen(*caption)+2);
              (void) DestroyString(*caption);
              *caption=target;
              p=(*caption)+n;
            }
        }
    q=draw_info->text;
    s=(char *) NULL;
  }
  n=0;
  for (p=(*caption); GetUTFCode(p) != 0; p+=(ptrdiff_t) GetUTFOctets(p))
    if (GetUTFCode(p) == '\n')
      n++;
  return(n);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   G e t M u l t i l i n e T y p e M e t r i c s                             %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetMultilineTypeMetrics() returns the following information for the
%  specified font and text:
%
%    character width
%    character height
%    ascender
%    descender
%    text width
%    text height
%    maximum horizontal advance
%    bounds: x1
%    bounds: y1
%    bounds: x2
%    bounds: y2
%    origin: x
%    origin: y
%    underline position
%    underline thickness
%
%  This method is like GetTypeMetrics() but it returns the maximum text width
%  and height for multiple lines of text.
%
%  The format of the GetMultilineTypeMetrics method is:
%
%      MagickBooleanType GetMultilineTypeMetrics(Image *image,
%        const DrawInfo *draw_info,TypeMetric *metrics)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o draw_info: the draw info.
%
%    o metrics: Return the font metrics in this structure.
%
*/
MagickExport MagickBooleanType GetMultilineTypeMetrics(Image *image,
  const DrawInfo *draw_info,TypeMetric *metrics)
{
  char
    **textlist;

  double
    height;

  DrawInfo
    *annotate_info;

  MagickBooleanType
    status;

  MagickSizeType
    size;

  ssize_t
    i;

  size_t
    count;

  TypeMetric
    extent;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(draw_info != (DrawInfo *) NULL);
  assert(draw_info->text != (char *) NULL);
  assert(draw_info->signature == MagickCoreSignature);
  if (*draw_info->text == '\0')
    {
      (void) ThrowMagickException(&image->exception,GetMagickModule(),
        OptionError,"LabelExpected","`%s'",image->filename);
      return(MagickFalse);
    }
  annotate_info=CloneDrawInfo((ImageInfo *) NULL,draw_info);
  annotate_info->text=DestroyString(annotate_info->text);
  /*
    Convert newlines to multiple lines of text.
  */
  textlist=StringToStrings(draw_info->text,&count);
  if (textlist == (char **) NULL)
    {
      annotate_info=DestroyDrawInfo(annotate_info);
      return(MagickFalse);
    }
  annotate_info->render=MagickFalse;
  annotate_info->direction=UndefinedDirection;
  (void) memset(metrics,0,sizeof(*metrics));
  (void) memset(&extent,0,sizeof(extent));
  /*
    Find the widest of the text lines.
  */
  annotate_info->text=textlist[0];
  status=GetTypeMetrics(image,annotate_info,&extent);
  *metrics=extent;
  height=(count*(size_t) (metrics->ascent-metrics->descent+
    0.5)+(count-1)*draw_info->interline_spacing);
  size=(MagickSizeType) fabs(height);
  if (AcquireMagickResource(HeightResource,size) == MagickFalse)
    {
      (void) ThrowMagickException(&image->exception,GetMagickModule(),
        ImageError,"WidthOrHeightExceedsLimit","`%s'",image->filename);
      status=MagickFalse;
    }
  else
    {
      for (i=1; i < (ssize_t) count; i++)
      {
        annotate_info->text=textlist[i];
        status=GetTypeMetrics(image,annotate_info,&extent);
        if (status == MagickFalse)
          break;
        if (extent.width > metrics->width)
          *metrics=extent;
        size=(MagickSizeType) fabs(extent.width);
        if (AcquireMagickResource(WidthResource,size) == MagickFalse)
          {
            (void) ThrowMagickException(&image->exception,GetMagickModule(),
              ImageError,"WidthOrHeightExceedsLimit","`%s'",image->filename);
            status=MagickFalse;
            break;
          }
      }
      metrics->height=(double) height;
    }
  /*
    Relinquish resources.
  */
  annotate_info->text=(char *) NULL;
  annotate_info=DestroyDrawInfo(annotate_info);
  for (i=0; i < (ssize_t) count; i++)
    textlist[i]=DestroyString(textlist[i]);
  textlist=(char **) RelinquishMagickMemory(textlist);
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   G e t T y p e M e t r i c s                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetTypeMetrics() returns the following information for the specified font
%  and text:
%
%    character width
%    character height
%    ascender
%    descender
%    text width
%    text height
%    maximum horizontal advance
%    bounds: x1
%    bounds: y1
%    bounds: x2
%    bounds: y2
%    origin: x
%    origin: y
%    underline position
%    underline thickness
%
%  The format of the GetTypeMetrics method is:
%
%      MagickBooleanType GetTypeMetrics(Image *image,const DrawInfo *draw_info,
%        TypeMetric *metrics)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o draw_info: the draw info.
%
%    o metrics: Return the font metrics in this structure.
%
*/
MagickExport MagickBooleanType GetTypeMetrics(Image *image,
  const DrawInfo *draw_info,TypeMetric *metrics)
{
  DrawInfo
    *annotate_info;

  MagickBooleanType
    status;

  PointInfo
    offset;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(draw_info != (DrawInfo *) NULL);
  assert(draw_info->text != (char *) NULL);
  assert(draw_info->signature == MagickCoreSignature);
  annotate_info=CloneDrawInfo((ImageInfo *) NULL,draw_info);
  annotate_info->render=MagickFalse;
  annotate_info->direction=UndefinedDirection;
  (void) memset(metrics,0,sizeof(*metrics));
  offset.x=0.0;
  offset.y=0.0;
  status=RenderType(image,annotate_info,&offset,metrics);
  if (draw_info->debug != MagickFalse)
    (void) LogMagickEvent(AnnotateEvent,GetMagickModule(),"Metrics: text: %s; "
      "width: %g; height: %g; ascent: %g; descent: %g; max advance: %g; "
      "bounds: %g,%g  %g,%g; origin: %g,%g; pixels per em: %g,%g; "
      "underline position: %g; underline thickness: %g",annotate_info->text,
      metrics->width,metrics->height,metrics->ascent,metrics->descent,
      metrics->max_advance,metrics->bounds.x1,metrics->bounds.y1,
      metrics->bounds.x2,metrics->bounds.y2,metrics->origin.x,metrics->origin.y,
      metrics->pixels_per_em.x,metrics->pixels_per_em.y,
      metrics->underline_position,metrics->underline_thickness);
  annotate_info=DestroyDrawInfo(annotate_info);
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   R e n d e r T y p e                                                       %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  RenderType() renders text on the image.  It also returns the bounding box of
%  the text relative to the image.
%
%  The format of the RenderType method is:
%
%      MagickBooleanType RenderType(Image *image,DrawInfo *draw_info,
%        const PointInfo *offset,TypeMetric *metrics)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o draw_info: the draw info.
%
%    o offset: (x,y) location of text relative to image.
%
%    o metrics: bounding box of text.
%
*/
static MagickBooleanType RenderType(Image *image,const DrawInfo *draw_info,
  const PointInfo *offset,TypeMetric *metrics)
{
  char
    *font;

  const TypeInfo
    *type_info;

  DrawInfo
    *annotate_info;

  ExceptionInfo
    *sans_exception;

  MagickBooleanType
    status;

  type_info=(const TypeInfo *) NULL;
  if (draw_info->font != (char *) NULL)
    {
      if (*draw_info->font == '@')
        {
          status=RenderFreetype(image,draw_info,draw_info->encoding,offset,
            metrics);
          return(status);
        }
      if (*draw_info->font == '-')
        return(RenderX11(image,draw_info,offset,metrics));
      if (*draw_info->font == '^')
        return(RenderPostscript(image,draw_info,offset,metrics));
      if (IsPathAccessible(draw_info->font) != MagickFalse)
        {
          status=RenderFreetype(image,draw_info,draw_info->encoding,offset,
            metrics);
          return(status);
        }
      type_info=GetTypeInfo(draw_info->font,&image->exception);
      if (type_info == (const TypeInfo *) NULL)
        (void) ThrowMagickException(&image->exception,GetMagickModule(),
          TypeWarning,"UnableToReadFont","`%s'",draw_info->font);
    }
  if ((type_info == (const TypeInfo *) NULL) &&
      (draw_info->family != (const char *) NULL))
    {
      if (strpbrk(draw_info->family,",'\"") == (char *) NULL)
        type_info=GetTypeInfoByFamily(draw_info->family,draw_info->style,
          draw_info->stretch,draw_info->weight,&image->exception);
      if (type_info == (const TypeInfo *) NULL)
        {
          char
            **family;

          int
            number_families;

          ssize_t
            i;

          /*
            Parse font family list.
          */
          family=StringToArgv(draw_info->family,&number_families);
          for (i=1; i < (ssize_t) number_families; i++)
          {
            (void) SubstituteString(&family[i],",","");
            type_info=GetTypeInfoByFamily(family[i],draw_info->style,
              draw_info->stretch,draw_info->weight,&image->exception);
            if ((type_info != (const TypeInfo *) NULL) &&
                (LocaleCompare(family[i],type_info->family) == 0))
              break;
          }
          for (i=0; i < (ssize_t) number_families; i++)
            family[i]=DestroyString(family[i]);
          family=(char **) RelinquishMagickMemory(family);
          if (type_info == (const TypeInfo *) NULL)
            (void) ThrowMagickException(&image->exception,GetMagickModule(),
              TypeWarning,"UnableToReadFont","`%s'",draw_info->family);
        }
    }
  font=GetPolicyValue("system:font");
  if (font != (char *) NULL)
    {
      if (IsPathAccessible(font) != MagickFalse)
        {
          /*
            Render with default system font.
          */
          annotate_info=CloneDrawInfo((ImageInfo *) NULL,draw_info);
          annotate_info->font=font;
          status=RenderFreetype(image,annotate_info,annotate_info->encoding,
            offset,metrics);
          annotate_info=DestroyDrawInfo(annotate_info);
          return(status);
        }
      font=DestroyString(font);
    }
  sans_exception=AcquireExceptionInfo();
  if (type_info == (const TypeInfo *) NULL)
    type_info=GetTypeInfoByFamily((const char *) NULL,draw_info->style,
      draw_info->stretch,draw_info->weight,sans_exception);
  if (type_info == (const TypeInfo *) NULL)
    type_info=GetTypeInfo("*",sans_exception);
  sans_exception=DestroyExceptionInfo(sans_exception);
  if (type_info == (const TypeInfo *) NULL)
    {
      status=RenderFreetype(image,draw_info,draw_info->encoding,offset,metrics);
      return(status);
    }
  annotate_info=CloneDrawInfo((ImageInfo *) NULL,draw_info);
  annotate_info->face=type_info->face;
  if (type_info->metrics != (char *) NULL)
    (void) CloneString(&annotate_info->metrics,type_info->metrics);
  if (type_info->glyphs != (char *) NULL)
    (void) CloneString(&annotate_info->font,type_info->glyphs);
  status=RenderFreetype(image,annotate_info,type_info->encoding,offset,metrics);
  annotate_info=DestroyDrawInfo(annotate_info);
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   R e n d e r F r e e t y p e                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  RenderFreetype() renders text on the image with a Truetype font.  It also
%  returns the bounding box of the text relative to the image.
%
%  The format of the RenderFreetype method is:
%
%      MagickBooleanType RenderFreetype(Image *image,DrawInfo *draw_info,
%        const char *encoding,const PointInfo *offset,TypeMetric *metrics)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o draw_info: the draw info.
%
%    o encoding: the font encoding.
%
%    o offset: (x,y) location of text relative to image.
%
%    o metrics: bounding box of text.
%
*/

#if defined(MAGICKCORE_FREETYPE_DELEGATE)

#if defined(MAGICKCORE_RAQM_DELEGATE)
static size_t ComplexRaqmTextLayout(const Image *image,
  const DrawInfo *draw_info,const char *text,const size_t length,
  const FT_Face face,GraphemeInfo **grapheme,ExceptionInfo *exception)
{
  const char
    *features;

  raqm_t
    *rq;

  raqm_glyph_t
    *glyphs;

  size_t
    extent;

  ssize_t
    i;

  (void) exception;
  extent=0;
  rq=raqm_create();
  if (rq == (raqm_t *) NULL)
    goto cleanup;
  if (raqm_set_text_utf8(rq,text,length) == 0)
    goto cleanup;
  if (raqm_set_par_direction(rq,(raqm_direction_t) draw_info->direction) == 0)
    goto cleanup;
  if (raqm_set_freetype_face(rq,face) == 0)
    goto cleanup;
  features=GetImageProperty(image,"type:features");
  if (features != (const char *) NULL)
    {
      char
        breaker,
        quote,
        *token;

      int
        next,
        status_token;

      TokenInfo
        *token_info;

      next=0;
      token_info=AcquireTokenInfo();
      token=AcquireString("");
      status_token=Tokenizer(token_info,0,token,50,features,"",",","",'\0',
        &breaker,&next,&quote);
      while (status_token == 0)
      {
        raqm_add_font_feature(rq,token,(int) strlen(token));
        status_token=Tokenizer(token_info,0,token,50,features,"",",","",'\0',
          &breaker,&next,&quote);
      }
      token_info=DestroyTokenInfo(token_info);
      token=DestroyString(token);
    }
  if (raqm_layout(rq) == 0)
    goto cleanup;
  glyphs=raqm_get_glyphs(rq,&extent);
  if (glyphs == (raqm_glyph_t *) NULL)
    {
      extent=0;
      goto cleanup;
    }
  *grapheme=(GraphemeInfo *) AcquireQuantumMemory(extent,sizeof(**grapheme));
  if (*grapheme == (GraphemeInfo *) NULL)
    {
      extent=0;
      goto cleanup;
    }
  for (i=0; i < (ssize_t) extent; i++)
  {
    (*grapheme)[i].index=glyphs[i].index;
    (*grapheme)[i].x_offset=(size_t) glyphs[i].x_offset;
    (*grapheme)[i].x_advance=(size_t) glyphs[i].x_advance;
    (*grapheme)[i].y_offset=(size_t) glyphs[i].y_offset;
    (*grapheme)[i].y_advance=(size_t) glyphs[i].y_advance;
    (*grapheme)[i].cluster=glyphs[i].cluster;
  }

cleanup:
  raqm_destroy(rq);
  return(extent);
#else
static size_t ComplexTextLayout(const DrawInfo *draw_info,const char *text,
  const size_t length,const FT_Face face,const FT_Int32 flags,
  GraphemeInfo **grapheme)
{
  const char
    *p;

  ssize_t
    i;

  ssize_t
    last_glyph;

  /*
    Simple layout for bi-directional text (right-to-left or left-to-right).
  */
  *grapheme=(GraphemeInfo *) AcquireQuantumMemory(length+1,sizeof(**grapheme));
  if (*grapheme == (GraphemeInfo *) NULL)
    return(0);
  last_glyph=0;
  p=text;
  for (i=0; GetUTFCode(p) != 0; p+=(ptrdiff_t) GetUTFOctets(p), i++)
  {
    (*grapheme)[i].index=(ssize_t) FT_Get_Char_Index(face,GetUTFCode(p));
    (*grapheme)[i].x_offset=0;
    (*grapheme)[i].y_offset=0;
    if (((*grapheme)[i].index != 0) && (last_glyph != 0))
      {
        if (FT_HAS_KERNING(face))
          {
            FT_Error
              ft_status;

            FT_Vector
              kerning;

            ft_status=FT_Get_Kerning(face,(FT_UInt) last_glyph,(FT_UInt)
              (*grapheme)[i].index,ft_kerning_default,&kerning);
            if (ft_status == 0)
              (*grapheme)[i-1].x_advance+=(FT_Pos) ((draw_info->direction ==
                RightToLeftDirection ? -1.0 : 1.0)*kerning.x);
          }
      }
    (void) FT_Load_Glyph(face,(FT_UInt) (*grapheme)[i].index,flags);
    (*grapheme)[i].x_advance=face->glyph->advance.x;
    (*grapheme)[i].y_advance=face->glyph->advance.y;
    (*grapheme)[i].cluster=p-text;
    last_glyph=(*grapheme)[i].index;
  }
  return((size_t) i);
#endif
}

static void FTCloseStream(FT_Stream stream)
{
  FILE *file = (FILE *) stream->descriptor.pointer;
  if (file != (FILE *) NULL)
    (void) fclose(file);
  stream->descriptor.pointer=NULL;
}

static unsigned long FTReadStream(FT_Stream stream,unsigned long offset,
  unsigned char *buffer,unsigned long count)
{
  FILE *file = (FILE *) stream->descriptor.pointer;
  if (file == (FILE *) NULL)
    return(0);
  if (count == 0) /* seek operation */
    {
      if (offset > stream->size)
        return(1);

      return((unsigned long) fseek(file,(off_t) offset,SEEK_SET));
    }
  return((unsigned long) fread(buffer,1,count,file));
}

static inline MagickBooleanType IsEmptyOutline(FT_Outline outline)
{
  return((outline.n_points == 0) || (outline.n_contours <= 0) ? MagickTrue :
    MagickFalse);
}

static int TraceCubicBezier(FT_Vector *p,FT_Vector *q,FT_Vector *to,
  DrawInfo *draw_info)
{
  AffineMatrix
    affine;

  char
    path[MaxTextExtent];

  affine=draw_info->affine;
  (void) FormatLocaleString(path,MaxTextExtent,"C%g,%g %g,%g %g,%g",affine.tx+
    p->x/64.0,affine.ty-p->y/64.0,affine.tx+q->x/64.0,affine.ty-q->y/64.0,
    affine.tx+to->x/64.0,affine.ty-to->y/64.0);
  (void) ConcatenateString(&draw_info->primitive,path);
  return(0);
}

static int TraceLineTo(FT_Vector *to,DrawInfo *draw_info)
{
  AffineMatrix
    affine;

  char
    path[MaxTextExtent];

  affine=draw_info->affine;
  (void) FormatLocaleString(path,MaxTextExtent,"L%g,%g",affine.tx+to->x/64.0,
    affine.ty-to->y/64.0);
  (void) ConcatenateString(&draw_info->primitive,path);
  return(0);
}

static int TraceMoveTo(FT_Vector *to,DrawInfo *draw_info)
{
  AffineMatrix
    affine;

  char
    path[MaxTextExtent];

  affine=draw_info->affine;
  (void) FormatLocaleString(path,MaxTextExtent,"M%g,%g",affine.tx+to->x/64.0,
    affine.ty-to->y/64.0);
  (void) ConcatenateString(&draw_info->primitive,path);
  return(0);
}

static int TraceQuadraticBezier(FT_Vector *control,FT_Vector *to,
  DrawInfo *draw_info)
{
  AffineMatrix
    affine;

  char
    path[MaxTextExtent];

  affine=draw_info->affine;
  (void) FormatLocaleString(path,MaxTextExtent,"Q%g,%g %g,%g",affine.tx+
    control->x/64.0,affine.ty-control->y/64.0,affine.tx+to->x/64.0,affine.ty-
    to->y/64.0);
  (void) ConcatenateString(&draw_info->primitive,path);
  return(0);
}

static inline const char *FreetypeErrorMessage(FT_Error ft_status)
{
#if FREETYPE_MAJOR == 2 && FREETYPE_MINOR >= 10
  return(FT_Error_String(ft_status));
#else
  magick_unreferenced(ft_status);
  return((const char *) NULL);
#endif
}

static MagickBooleanType RenderFreetype(Image *image,const DrawInfo *draw_info,
  const char *encoding,const PointInfo *offset,TypeMetric *metrics)
{
#if !defined(FT_OPEN_PATHNAME)
#define FT_OPEN_PATHNAME  ft_open_pathname
#endif

#define ThrowFreetypeErrorException(tag,ft_status,value) \
{ \
  const char \
    *error_string=FreetypeErrorMessage(ft_status); \
  if (error_string != (const char *) NULL) \
    (void) ThrowMagickException(exception,GetMagickModule(),TypeError, \
      tag,"`%s (%s)'",value, error_string); \
  else \
    (void) ThrowMagickException(exception,GetMagickModule(),TypeError, \
      tag,"`%s'",value); \
}

  typedef struct _GlyphInfo
  {
    FT_UInt
      id;

    FT_Vector
      origin;

    FT_Glyph
      image;
  } GlyphInfo;

  char
    *p;

  const char
    *value;

  DrawInfo
    *annotate_info;

  ExceptionInfo
    *exception;

  FT_BBox
    bounds;

  FT_Encoding
    encoding_type;

  FT_Error
    ft_status;

  FT_Face
    face;

  FT_Int32
    flags;

  FT_Library
    library;

  FT_Long
    face_index;

  FT_Matrix
    affine;

  FT_Open_Args
    args;

  FT_StreamRec
    *stream;

  FT_UInt
    first_glyph_id,
    last_glyph_id,
    missing_glyph_id;

  FT_Vector
    origin;

  GlyphInfo
    glyph;

  GraphemeInfo
    *grapheme;

  MagickBooleanType
    status;

  PointInfo
    resolution;

  ssize_t
    i;

  size_t
    length;

  ssize_t
    code,
    last_character,
    y;

  static FT_Outline_Funcs
    OutlineMethods =
    {
      (FT_Outline_MoveTo_Func) TraceMoveTo,
      (FT_Outline_LineTo_Func) TraceLineTo,
      (FT_Outline_ConicTo_Func) TraceQuadraticBezier,
      (FT_Outline_CubicTo_Func) TraceCubicBezier,
      0, 0
    };

  struct stat
    attributes;

  unsigned char
    *utf8;

  /*
    Initialize Truetype library.
  */
  exception=(&image->exception);
  ft_status=FT_Init_FreeType(&library);
  if (ft_status != 0)
    ThrowFreetypeErrorException("UnableToInitializeFreetypeLibrary",ft_status,
      image->filename);
  face_index=(FT_Long) draw_info->face;
  /*
    Open font face.
  */
  (void) memset(&args,0,sizeof(args));
  if (draw_info->font == (char *) NULL)
    {
      const TypeInfo *type_info = GetTypeInfo("*",exception);
      if (type_info != (const TypeInfo *) NULL)
        args.pathname=ConstantString(type_info->glyphs);
    }
  else
    if (*draw_info->font != '@')
      args.pathname=ConstantString(draw_info->font);
    else
      {
        /*
          Extract face index, e.g. @msgothic[1].
        */
        ImageInfo *image_info=AcquireImageInfo();
        (void) CopyMagickString(image_info->filename,draw_info->font+1,
          MagickPathExtent);
        (void) SetImageInfo(image_info,0,exception);
        face_index=(FT_Long) image_info->scene;
        args.pathname=ConstantString(image_info->filename);
        image_info=DestroyImageInfo(image_info);
     }
  /*
    Configure streaming interface.
  */
  stream=(FT_StreamRec *) AcquireCriticalMemory(sizeof(*stream));
  (void) memset(stream,0,sizeof(*stream));
  if (stat(args.pathname,&attributes) == 0)
    stream->size=attributes.st_size >= 0 ? (unsigned long)
      attributes.st_size : 0;
  stream->descriptor.pointer=fopen_utf8(args.pathname,"rb");
  stream->read=(&FTReadStream);
  stream->close=(&FTCloseStream);
  args.flags=FT_OPEN_STREAM;
  args.stream=stream;
  face=(FT_Face) NULL;
  ft_status=FT_Open_Face(library,&args,face_index,&face);
  if (ft_status != 0)
    {
      (void) FT_Done_FreeType(library);
      stream=(FT_StreamRec *) RelinquishMagickMemory(stream);
      ThrowFreetypeErrorException("UnableToReadFont",ft_status,args.pathname);
      args.pathname=DestroyString(args.pathname);
      return(MagickFalse);
    }
  args.pathname=DestroyString(args.pathname);
  if ((draw_info->metrics != (char *) NULL) &&
      (IsPathAccessible(draw_info->metrics) != MagickFalse))
    (void) FT_Attach_File(face,draw_info->metrics);
  encoding_type=FT_ENCODING_UNICODE;
  ft_status=FT_Select_Charmap(face,encoding_type);
  if ((ft_status != 0) && (face->num_charmaps != 0))
    ft_status=FT_Set_Charmap(face,face->charmaps[0]);
  if (encoding != (const char *) NULL)
    {
      if (LocaleCompare(encoding,"AdobeCustom") == 0)
        encoding_type=FT_ENCODING_ADOBE_CUSTOM;
      if (LocaleCompare(encoding,"AdobeExpert") == 0)
        encoding_type=FT_ENCODING_ADOBE_EXPERT;
      if (LocaleCompare(encoding,"AdobeStandard") == 0)
        encoding_type=FT_ENCODING_ADOBE_STANDARD;
      if (LocaleCompare(encoding,"AppleRoman") == 0)
        encoding_type=FT_ENCODING_APPLE_ROMAN;
      if (LocaleCompare(encoding,"BIG5") == 0)
        encoding_type=FT_ENCODING_BIG5;
#if defined(FT_ENCODING_PRC)
      if (LocaleCompare(encoding,"GB2312") == 0)
        encoding_type=FT_ENCODING_PRC;
#endif
#if defined(FT_ENCODING_JOHAB)
      if (LocaleCompare(encoding,"Johab") == 0)
        encoding_type=FT_ENCODING_JOHAB;
#endif
#if defined(FT_ENCODING_ADOBE_LATIN_1)
      if (LocaleCompare(encoding,"Latin-1") == 0)
        encoding_type=FT_ENCODING_ADOBE_LATIN_1;
#endif
#if defined(FT_ENCODING_ADOBE_LATIN_2)
      if (LocaleCompare(encoding,"Latin-2") == 0)
        encoding_type=FT_ENCODING_OLD_LATIN_2;
#endif
      if (LocaleCompare(encoding,"None") == 0)
        encoding_type=FT_ENCODING_NONE;
      if (LocaleCompare(encoding,"SJIScode") == 0)
        encoding_type=FT_ENCODING_SJIS;
      if (LocaleCompare(encoding,"Symbol") == 0)
        encoding_type=FT_ENCODING_MS_SYMBOL;
      if (LocaleCompare(encoding,"Unicode") == 0)
        encoding_type=FT_ENCODING_UNICODE;
      if (LocaleCompare(encoding,"Wansung") == 0)
        encoding_type=FT_ENCODING_WANSUNG;
      ft_status=FT_Select_Charmap(face,encoding_type);
      if (ft_status != 0)
        {
          (void) FT_Done_Face(face);
          (void) FT_Done_FreeType(library);
          stream=(FT_StreamRec *) RelinquishMagickMemory(stream);
          ThrowFreetypeErrorException("UnrecognizedFontEncoding",ft_status,
            encoding);
          return(MagickFalse);
        }
    }
  /*
    Set text size.
  */
  resolution.x=DefaultResolution;
  resolution.y=DefaultResolution;
  if (draw_info->density != (char *) NULL)
    {
      GeometryInfo
        geometry_info;

      MagickStatusType
        flags;

      flags=ParseGeometry(draw_info->density,&geometry_info);
      if ((flags & RhoValue) != 0)
        resolution.x=geometry_info.rho;
      resolution.y=resolution.x;
      if ((flags & SigmaValue) != 0)
        resolution.y=geometry_info.sigma;
    }
  ft_status=FT_Set_Char_Size(face,(FT_F26Dot6) (64.0*draw_info->pointsize),
    (FT_F26Dot6) (64.0*draw_info->pointsize),(FT_UInt) resolution.x,
    (FT_UInt) resolution.y);
  if (ft_status != 0)
    {
      (void) FT_Done_Face(face);
      (void) FT_Done_FreeType(library);
      stream=(FT_StreamRec *) RelinquishMagickMemory(stream);
      ThrowFreetypeErrorException("UnableToReadFont",ft_status,
        draw_info->font);
      return(MagickFalse);
    }
  metrics->pixels_per_em.x=face->size->metrics.x_ppem;
  metrics->pixels_per_em.y=face->size->metrics.y_ppem;
  metrics->ascent=(double) face->size->metrics.ascender/64.0;
  metrics->descent=(double) face->size->metrics.descender/64.0;
  if (face->size->metrics.ascender == 0)
    {
      /*
        Sanitize buggy ascender and descender values.
      */
      metrics->ascent=face->size->metrics.y_ppem;
      if (face->size->metrics.descender == 0)
        metrics->descent=face->size->metrics.y_ppem/-3.5;
    }
  metrics->width=0;
  metrics->origin.x=0;
  metrics->origin.y=0;
  metrics->height=(double) face->size->metrics.height/64.0;
  metrics->max_advance=0.0;
  if (face->size->metrics.max_advance > MagickEpsilon)
    metrics->max_advance=(double) face->size->metrics.max_advance/64.0;
  metrics->bounds.x1=0.0;
  metrics->bounds.y1=metrics->descent;
  metrics->bounds.x2=metrics->ascent+metrics->descent;
  metrics->bounds.y2=metrics->ascent+metrics->descent;
  metrics->underline_position=face->underline_position*
    (metrics->pixels_per_em.x*MagickSafeReciprocal(face->units_per_EM));
  metrics->underline_thickness=face->underline_thickness*
    (metrics->pixels_per_em.x*MagickSafeReciprocal(face->units_per_EM));
  first_glyph_id=0;
  FT_Get_First_Char(face,&first_glyph_id);
  if ((draw_info->text == (char *) NULL) || (*draw_info->text == '\0') ||
      (first_glyph_id == 0))
    {
      (void) FT_Done_Face(face);
      (void) FT_Done_FreeType(library);
      stream=(FT_StreamRec *) RelinquishMagickMemory(stream);
      return(MagickTrue);
    }
  /*
    Compute bounding box.
  */
  if (draw_info->debug != MagickFalse)
    (void) LogMagickEvent(AnnotateEvent,GetMagickModule(),"Font %s; "
      "font-encoding %s; text-encoding %s; pointsize %g",
      draw_info->font != (char *) NULL ? draw_info->font : "none",
      encoding != (char *) NULL ? encoding : "none",
      draw_info->encoding != (char *) NULL ? draw_info->encoding : "none",
      draw_info->pointsize);
  flags=FT_LOAD_DEFAULT;
  if (draw_info->render == MagickFalse)
    flags=FT_LOAD_NO_BITMAP;
  if (draw_info->text_antialias == MagickFalse)
    flags|=FT_LOAD_TARGET_MONO;
  else
    {
#if defined(FT_LOAD_TARGET_LIGHT)
      flags|=FT_LOAD_TARGET_LIGHT;
#elif defined(FT_LOAD_TARGET_LCD)
      flags|=FT_LOAD_TARGET_LCD;
#endif
    }
  value=GetImageProperty(image,"type:hinting");
  if ((value != (const char *) NULL) && (LocaleCompare(value,"off") == 0))
    flags|=FT_LOAD_NO_HINTING;
  glyph.id=0;
  glyph.image=(FT_Glyph) NULL;
  last_glyph_id=0;
  origin.x=0;
  origin.y=0;
  affine.xx=65536L;
  affine.yx=0L;
  affine.xy=0L;
  affine.yy=65536L;
  if (draw_info->render != MagickFalse)
    {
      affine.xx=(FT_Fixed) (65536L*draw_info->affine.sx+0.5);
      affine.yx=(FT_Fixed) (-65536L*draw_info->affine.rx+0.5);
      affine.xy=(FT_Fixed) (-65536L*draw_info->affine.ry+0.5);
      affine.yy=(FT_Fixed) (65536L*draw_info->affine.sy+0.5);
    }
  annotate_info=CloneDrawInfo((ImageInfo *) NULL,draw_info);
  if (annotate_info->dash_pattern != (double *) NULL)
    annotate_info->dash_pattern[0]=0.0;
  (void) CloneString(&annotate_info->primitive,"path '");
  status=MagickTrue;
  if (draw_info->render != MagickFalse)
    {
      if (image->storage_class != DirectClass)
        (void) SetImageStorageClass(image,DirectClass);
      if (image->matte == MagickFalse)
        (void) SetImageAlphaChannel(image,OpaqueAlphaChannel);
    }
  for (p=draw_info->text; GetUTFCode(p) != 0; p+=(ptrdiff_t) GetUTFOctets(p))
    if (GetUTFCode(p) < 0)
      break;
  utf8=(unsigned char *) NULL;
  if (GetUTFCode(p) == 0)
    p=draw_info->text;
  else
    {
      utf8=ConvertLatin1ToUTF8((unsigned char *) draw_info->text);
      if (utf8 != (unsigned char *) NULL)
        p=(char *) utf8;
    }
  grapheme=(GraphemeInfo *) NULL;
#if defined(MAGICKCORE_RAQM_DELEGATE)
  length=ComplexRaqmTextLayout(image,draw_info,p,strlen(p),face,&grapheme,
    exception);
#else
  length=ComplexTextLayout(draw_info,p,strlen(p),face,flags,&grapheme);
#endif
  missing_glyph_id=FT_Get_Char_Index(face,' ');
  code=0;
  last_character=(ssize_t) length-1;
  for (i=0; i < (ssize_t) length; i++)
  {
    FT_BitmapGlyph
      bitmap;

    FT_Outline
      outline;

    PointInfo
      point;

    /*
      Render UTF-8 sequence.
    */
    glyph.id=grapheme[i].index;
    if (glyph.id == 0)
      glyph.id=missing_glyph_id;
    if ((glyph.id != 0) && (last_glyph_id != 0))
      origin.x+=(FT_Pos) (64.0*draw_info->kerning);
    glyph.origin=origin;
    glyph.origin.x+=grapheme[i].x_offset;
    glyph.origin.y+=grapheme[i].y_offset;
    if (glyph.image != (FT_Glyph) NULL)
      {
        FT_Done_Glyph(glyph.image);
        glyph.image=(FT_Glyph) NULL;
      }
    ft_status=FT_Load_Glyph(face,glyph.id,flags);
    if (ft_status != 0)
      continue;
    ft_status=FT_Get_Glyph(face->glyph,&glyph.image);
    if (ft_status != 0)
      continue;
    outline=((FT_OutlineGlyph) glyph.image)->outline;
    if ((glyph.image->format != FT_GLYPH_FORMAT_OUTLINE) &&
        (IsEmptyOutline(outline) == MagickFalse))
      continue;
    ft_status=FT_Outline_Get_BBox(&outline,&bounds);
    if (ft_status != 0)
      continue;
    if ((bounds.xMin < metrics->bounds.x1) && (bounds.xMin != 0))
      metrics->bounds.x1=(double) bounds.xMin;
    if ((bounds.yMin < metrics->bounds.y1) && (bounds.yMin != 0))
      metrics->bounds.y1=(double) bounds.yMin;
    if ((bounds.xMax > metrics->bounds.x2) && (bounds.xMax != 0))
      metrics->bounds.x2=(double) bounds.xMax;
    if ((bounds.yMax > metrics->bounds.y2) && (bounds.yMax != 0))
      metrics->bounds.y2=(double) bounds.yMax;
    if (((draw_info->stroke.opacity != TransparentOpacity) ||
         (draw_info->stroke_pattern != (Image *) NULL)) &&
        ((status != MagickFalse) && (draw_info->render != MagickFalse)))
      {
        /*
          Trace the glyph.
        */
        annotate_info->affine.tx=glyph.origin.x/64.0;
        annotate_info->affine.ty=(-glyph.origin.y/64.0);
        if (IsEmptyOutline(outline) == MagickFalse)
          ft_status=FT_Outline_Decompose(&outline,&OutlineMethods,
            annotate_info);
      }
    FT_Vector_Transform(&glyph.origin,&affine);
    (void) FT_Glyph_Transform(glyph.image,&affine,&glyph.origin);
    ft_status=FT_Glyph_To_Bitmap(&glyph.image,FT_RENDER_MODE_NORMAL,
      (FT_Vector *) NULL,MagickTrue);
    if (ft_status != 0)
      continue;
    bitmap=(FT_BitmapGlyph) glyph.image;
    point.x=offset->x+bitmap->left;
    if (bitmap->bitmap.pixel_mode == ft_pixel_mode_mono)
      point.x+=(origin.x/64.0);
    point.y=offset->y-bitmap->top;
    if (draw_info->render != MagickFalse)
      {
        CacheView
          *image_view;

        unsigned char
          *p;

        MagickBooleanType
          transparent_fill;

        /*
          Rasterize the glyph.
        */
        transparent_fill=((draw_info->fill.opacity == TransparentOpacity) &&
          (draw_info->fill_pattern == (Image *) NULL) &&
          (draw_info->stroke.opacity == TransparentOpacity) &&
          (draw_info->stroke_pattern == (Image *) NULL)) ? MagickTrue :
          MagickFalse;
        p=bitmap->bitmap.buffer;
        image_view=AcquireAuthenticCacheView(image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
        #pragma omp parallel for schedule(static) shared(status) \
          magick_number_threads(image,image,bitmap->bitmap.rows,4)
#endif
        for (y=0; y < (ssize_t) bitmap->bitmap.rows; y++)
        {
          MagickBooleanType
            active,
            sync;

          MagickRealType
            fill_opacity;

          PixelPacket
            fill_color,
            *magick_restrict q;

          ssize_t
            x;

          ssize_t
            n,
            x_offset,
            y_offset;

          if (status == MagickFalse)
            continue;
          x_offset=CastDoubleToLong(ceil(point.x-0.5));
          y_offset=CastDoubleToLong(ceil(point.y+y-0.5));
          if ((y_offset < 0) || (y_offset >= (ssize_t) image->rows))
            continue;
          q=(PixelPacket *) NULL;
          if ((x_offset < 0) || (x_offset >= (ssize_t) image->columns))
            active=MagickFalse;
          else
            {
              q=GetCacheViewAuthenticPixels(image_view,x_offset,y_offset,
                bitmap->bitmap.width,1,exception);
              active=q != (PixelPacket *) NULL ? MagickTrue : MagickFalse;
            }
          n=y*bitmap->bitmap.pitch;
          for (x=0; x < (ssize_t) bitmap->bitmap.width; x++, n++)
          {
            x_offset=CastDoubleToLong(ceil(point.x+x-0.5));
            if ((x_offset < 0) || (x_offset >= (ssize_t) image->columns))
              {
                if (q != (PixelPacket *) NULL)
                  q++;
                continue;
              }
            fill_opacity=1.0;
            if (bitmap->bitmap.buffer != (unsigned char *) NULL)
              {
                if (bitmap->bitmap.pixel_mode == ft_pixel_mode_grays)
                  fill_opacity=(MagickRealType) (p[n])/(
                    bitmap->bitmap.num_grays-1);
                else
                  if (bitmap->bitmap.pixel_mode == ft_pixel_mode_mono)
                    fill_opacity=((p[(x >> 3)+y*bitmap->bitmap.pitch] &
                      (1 << (~x & 0x07)))) == 0 ? 0.0 : 1.0;
              }
            if (draw_info->text_antialias == MagickFalse)
              fill_opacity=fill_opacity >= 0.5 ? 1.0 : 0.0;
            if (active == MagickFalse)
              q=GetCacheViewAuthenticPixels(image_view,x_offset,y_offset,1,1,
                exception);
            if (q == (PixelPacket *) NULL)
              continue;
            if (transparent_fill == MagickFalse)
              {
                (void) GetFillColor(draw_info,x_offset,y_offset,&fill_color);
                fill_opacity=(double) QuantumRange-(double) fill_opacity*
                  ((double) QuantumRange-(double) fill_color.opacity);
                MagickCompositeOver(&fill_color,fill_opacity,q,q->opacity,q);
              }
            else
              {
                double
                  Sa,
                  Da;

                Da=1.0-(QuantumScale*((double) QuantumRange-(double)
                  q->opacity));
                Sa=fill_opacity;
                fill_opacity=(1.0-RoundToUnity(Sa+Da-Sa*Da))*(double)
                  QuantumRange;
                SetPixelAlpha(q,fill_opacity);
              }
            if (active == MagickFalse)
              {
                sync=SyncCacheViewAuthenticPixels(image_view,exception);
                if (sync == MagickFalse)
                  status=MagickFalse;
              }
            q++;
          }
          sync=SyncCacheViewAuthenticPixels(image_view,exception);
          if (sync == MagickFalse)
            status=MagickFalse;
        }
        image_view=DestroyCacheView(image_view);
        if (((draw_info->stroke.opacity != TransparentOpacity) ||
             (draw_info->stroke_pattern != (Image *) NULL)) &&
            (status != MagickFalse))
          {
            /*
              Draw text stroke.
            */
            annotate_info->linejoin=RoundJoin;
            annotate_info->affine.tx=offset->x;
            annotate_info->affine.ty=offset->y;
            (void) ConcatenateString(&annotate_info->primitive,"'");
            if (strlen(annotate_info->primitive) > 7)
              (void) DrawImage(image,annotate_info);
            (void) CloneString(&annotate_info->primitive,"path '");
          }
      }
    if ((fabs(draw_info->interword_spacing) >= MagickEpsilon) &&
        (IsUTFSpace(GetUTFCode(p+grapheme[i].cluster)) != MagickFalse) &&
        (IsUTFSpace(code) == MagickFalse))
      origin.x+=(FT_Pos) (64.0*draw_info->interword_spacing);
    else
      if (i == last_character)
        origin.x+=MagickMax((FT_Pos) grapheme[i].x_advance,bounds.xMax);
      else
        origin.x+=(FT_Pos) grapheme[i].x_advance;
    origin.y+=(FT_Pos) grapheme[i].y_advance;
    metrics->origin.x=(double) origin.x;
    metrics->origin.y=(double) origin.y;
    if (metrics->origin.x > metrics->width)
      metrics->width=metrics->origin.x;
    last_glyph_id=glyph.id;
    code=GetUTFCode(p+grapheme[i].cluster);
  }
  if (grapheme != (GraphemeInfo *) NULL)
    grapheme=(GraphemeInfo *) RelinquishMagickMemory(grapheme);
  if (utf8 != (unsigned char *) NULL)
    utf8=(unsigned char *) RelinquishMagickMemory(utf8);
  if (glyph.image != (FT_Glyph) NULL)
    {
      FT_Done_Glyph(glyph.image);
      glyph.image=(FT_Glyph) NULL;
    }
  /*
    Determine font metrics.
  */
  metrics->bounds.x1/=64.0;
  metrics->bounds.y1/=64.0;
  metrics->bounds.x2/=64.0;
  metrics->bounds.y2/=64.0;
  metrics->origin.x/=64.0;
  metrics->origin.y/=64.0;
  metrics->width/=64.0;
  /*
    Relinquish resources.
  */
  annotate_info=DestroyDrawInfo(annotate_info);
  (void) FT_Done_Face(face);
  (void) FT_Done_FreeType(library);
  stream=(FT_StreamRec *) RelinquishMagickMemory(stream);
  return(status);
}
#else
static MagickBooleanType RenderFreetype(Image *image,const DrawInfo *draw_info,
  const char *magick_unused(encoding),const PointInfo *offset,
  TypeMetric *metrics)
{
  (void) ThrowMagickException(&image->exception,GetMagickModule(),
    MissingDelegateWarning,"DelegateLibrarySupportNotBuiltIn","`%s' (Freetype)",
    draw_info->font != (char *) NULL ? draw_info->font : "none");
  return(RenderPostscript(image,draw_info,offset,metrics));
}
#endif

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   R e n d e r P o s t s c r i p t                                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  RenderPostscript() renders text on the image with a Postscript font.  It
%  also returns the bounding box of the text relative to the image.
%
%  The format of the RenderPostscript method is:
%
%      MagickBooleanType RenderPostscript(Image *image,DrawInfo *draw_info,
%        const PointInfo *offset,TypeMetric *metrics)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o draw_info: the draw info.
%
%    o offset: (x,y) location of text relative to image.
%
%    o metrics: bounding box of text.
%
*/

static char *EscapeParenthesis(const char *source)
{
  char
    *destination;

  char
    *q;

  const char
    *p;

  size_t
    length;

  assert(source != (const char *) NULL);
  length=0;
  for (p=source; *p != '\0'; p++)
  {
    if ((*p == '\\') || (*p == '(') || (*p == ')'))
      {
        if (~length < 1)
          ThrowFatalException(ResourceLimitFatalError,"UnableToEscapeString");
        length++;
      }
    length++;
  }
  destination=(char *) NULL;
  if (~length >= (MaxTextExtent-1))
    destination=(char *) AcquireQuantumMemory(length+MaxTextExtent,
      sizeof(*destination));
  if (destination == (char *) NULL)
    ThrowFatalException(ResourceLimitFatalError,"UnableToEscapeString");
  *destination='\0';
  q=destination;
  for (p=source; *p != '\0'; p++)
  {
    if ((*p == '\\') || (*p == '(') || (*p == ')'))
      *q++='\\';
    *q++=(*p);
  }
  *q='\0';
  return(destination);
}

static MagickBooleanType RenderPostscript(Image *image,
  const DrawInfo *draw_info,const PointInfo *offset,TypeMetric *metrics)
{
  char
    filename[MaxTextExtent],
    geometry[MaxTextExtent],
    *text;

  FILE
    *file;

  Image
    *annotate_image;

  ImageInfo
    *annotate_info;

  int
    unique_file;

  MagickBooleanType
    identity,
    status;

  PointInfo
    extent,
    point,
    resolution;

  ssize_t
    i;

  size_t
    length;

  ssize_t
    y;

  /*
    Render label with a Postscript font.
  */
  if (draw_info->debug != MagickFalse)
    (void) LogMagickEvent(AnnotateEvent,GetMagickModule(),
      "Font %s; pointsize %g",draw_info->font != (char *) NULL ?
      draw_info->font : "none",draw_info->pointsize);
  file=(FILE *) NULL;
  unique_file=AcquireUniqueFileResource(filename);
  if (unique_file != -1)
    file=fdopen(unique_file,"wb");
  if ((unique_file == -1) || (file == (FILE *) NULL))
    {
      ThrowFileException(&image->exception,FileOpenError,"UnableToOpenFile",
        filename);
      return(MagickFalse);
    }
  (void) FormatLocaleFile(file,"%%!PS-Adobe-3.0\n");
  (void) FormatLocaleFile(file,"/ReencodeType\n");
  (void) FormatLocaleFile(file,"{\n");
  (void) FormatLocaleFile(file,"  findfont dup length\n");
  (void) FormatLocaleFile(file,
    "  dict begin { 1 index /FID ne {def} {pop pop} ifelse } forall\n");
  (void) FormatLocaleFile(file,
    "  /Encoding ISOLatin1Encoding def currentdict end definefont pop\n");
  (void) FormatLocaleFile(file,"} bind def\n");
  /*
    Sample to compute bounding box.
  */
  identity=(fabs(draw_info->affine.sx-draw_info->affine.sy) < MagickEpsilon) &&
    (fabs(draw_info->affine.rx) < MagickEpsilon) &&
    (fabs(draw_info->affine.ry) < MagickEpsilon) ? MagickTrue : MagickFalse;
  extent.x=0.0;
  extent.y=0.0;
  length=strlen(draw_info->text);
  for (i=0; i <= (ssize_t) (length+2); i++)
  {
    point.x=fabs(draw_info->affine.sx*i*draw_info->pointsize+
      draw_info->affine.ry*2.0*draw_info->pointsize);
    point.y=fabs(draw_info->affine.rx*i*draw_info->pointsize+
      draw_info->affine.sy*2.0*draw_info->pointsize);
    if (point.x > extent.x)
      extent.x=point.x;
    if (point.y > extent.y)
      extent.y=point.y;
  }
  (void) FormatLocaleFile(file,"%g %g moveto\n",identity  != MagickFalse ? 0.0 :
    extent.x/2.0,extent.y/2.0);
  (void) FormatLocaleFile(file,"%g %g scale\n",draw_info->pointsize,
    draw_info->pointsize);
  if ((draw_info->font == (char *) NULL) || (*draw_info->font == '\0') ||
      (strchr(draw_info->font,'/') != (char *) NULL))
    (void) FormatLocaleFile(file,
      "/Times-Roman-ISO dup /Times-Roman ReencodeType findfont setfont\n");
  else
    (void) FormatLocaleFile(file,
      "/%s-ISO dup /%s ReencodeType findfont setfont\n",draw_info->font,
      draw_info->font);
  (void) FormatLocaleFile(file,"[%g %g %g %g 0 0] concat\n",
    draw_info->affine.sx,-draw_info->affine.rx,-draw_info->affine.ry,
    draw_info->affine.sy);
  text=EscapeParenthesis(draw_info->text);
  if (identity == MagickFalse)
    (void) FormatLocaleFile(file,"(%s) stringwidth pop -0.5 mul -0.5 rmoveto\n",
      text);
  (void) FormatLocaleFile(file,"(%s) show\n",text);
  text=DestroyString(text);
  (void) FormatLocaleFile(file,"showpage\n");
  (void) fclose(file);
  (void) FormatLocaleString(geometry,MaxTextExtent,"%.20gx%.20g+0+0!",
    floor(extent.x+0.5),floor(extent.y+0.5));
  annotate_info=AcquireImageInfo();
  (void) FormatLocaleString(annotate_info->filename,MaxTextExtent,"ps:%s",
    filename);
  (void) CloneString(&annotate_info->page,geometry);
  if (draw_info->density != (char *) NULL)
    (void) CloneString(&annotate_info->density,draw_info->density);
  annotate_info->antialias=draw_info->text_antialias;
  annotate_image=ReadImage(annotate_info,&image->exception);
  CatchException(&image->exception);
  annotate_info=DestroyImageInfo(annotate_info);
  (void) RelinquishUniqueFileResource(filename);
  if (annotate_image == (Image *) NULL)
    return(MagickFalse);
  resolution.x=DefaultResolution;
  resolution.y=DefaultResolution;
  if (draw_info->density != (char *) NULL)
    {
      GeometryInfo
        geometry_info;

      MagickStatusType
        flags;

      flags=ParseGeometry(draw_info->density,&geometry_info);
      if ((flags & RhoValue) != 0)
        resolution.x=geometry_info.rho;
      resolution.y=resolution.x;
      if ((flags & SigmaValue) != 0)
        resolution.y=geometry_info.sigma;
    }
  if (identity == MagickFalse)
    (void) TransformImage(&annotate_image,"0x0",(char *) NULL);
  else
    {
      RectangleInfo
        crop_info;

      crop_info=GetImageBoundingBox(annotate_image,&annotate_image->exception);
      crop_info.height=(size_t) ((resolution.y/DefaultResolution)*
        ExpandAffine(&draw_info->affine)*draw_info->pointsize+0.5);
      crop_info.y=CastDoubleToLong(ceil((resolution.y/DefaultResolution)*
        extent.y/8.0-0.5));
      (void) FormatLocaleString(geometry,MaxTextExtent,
        "%.20gx%.20g%+.20g%+.20g",(double) crop_info.width,(double)
        crop_info.height,(double) crop_info.x,(double) crop_info.y);
      (void) TransformImage(&annotate_image,geometry,(char *) NULL);
    }
  metrics->pixels_per_em.x=(resolution.y/DefaultResolution)*
    ExpandAffine(&draw_info->affine)*draw_info->pointsize;
  metrics->pixels_per_em.y=metrics->pixels_per_em.x;
  metrics->ascent=metrics->pixels_per_em.y;
  metrics->descent=metrics->pixels_per_em.y/-5.0;
  metrics->width=(double) annotate_image->columns/
    ExpandAffine(&draw_info->affine);
  metrics->height=floor(metrics->ascent-metrics->descent+0.5);
  metrics->max_advance=metrics->pixels_per_em.x;
  metrics->bounds.x1=0.0;
  metrics->bounds.y1=metrics->descent;
  metrics->bounds.x2=metrics->ascent+metrics->descent;
  metrics->bounds.y2=metrics->ascent+metrics->descent;
  metrics->underline_position=(-2.0);
  metrics->underline_thickness=1.0;
  if (draw_info->render == MagickFalse)
    {
      annotate_image=DestroyImage(annotate_image);
      return(MagickTrue);
    }
  if (draw_info->fill.opacity != TransparentOpacity)
    {
      ExceptionInfo
        *exception;

      MagickBooleanType
        sync;

      PixelPacket
        fill_color;

      CacheView
        *annotate_view;

      /*
        Render fill color.
      */
      if (image->matte == MagickFalse)
        (void) SetImageAlphaChannel(image,OpaqueAlphaChannel);
      if (annotate_image->matte == MagickFalse)
        (void) SetImageAlphaChannel(annotate_image,OpaqueAlphaChannel);
      fill_color=draw_info->fill;
      status=MagickTrue;
      exception=(&image->exception);
      annotate_view=AcquireAuthenticCacheView(annotate_image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
    #pragma omp parallel for schedule(static) shared(status) \
      magick_number_threads(annotate_image,annotate_image,annotate_image->rows,4)
#endif
      for (y=0; y < (ssize_t) annotate_image->rows; y++)
      {
        PixelPacket
          *magick_restrict q;

        ssize_t
          x;

        if (status == MagickFalse)
          continue;
        q=GetCacheViewAuthenticPixels(annotate_view,0,y,annotate_image->columns,
          1,exception);
        if (q == (PixelPacket *) NULL)
          {
            status=MagickFalse;
            continue;
          }
        for (x=0; x < (ssize_t) annotate_image->columns; x++)
        {
          (void) GetFillColor(draw_info,x,y,&fill_color);
          SetPixelAlpha(q,ClampToQuantum(((((double) QuantumRange-
            GetPixelIntensity(annotate_image,q))*((double) QuantumRange-
            (double) fill_color.opacity))/(double) QuantumRange)));
          SetPixelRed(q,fill_color.red);
          SetPixelGreen(q,fill_color.green);
          SetPixelBlue(q,fill_color.blue);
          q++;
        }
        sync=SyncCacheViewAuthenticPixels(annotate_view,exception);
        if (sync == MagickFalse)
          status=MagickFalse;
      }
      annotate_view=DestroyCacheView(annotate_view);
      (void) CompositeImage(image,OverCompositeOp,annotate_image,
        (ssize_t) ceil(offset->x-0.5),(ssize_t) ceil(offset->y-(metrics->ascent+
        metrics->descent)-0.5));
    }
  annotate_image=DestroyImage(annotate_image);
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   R e n d e r X 1 1                                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  RenderX11() renders text on the image with an X11 font.  It also returns the
%  bounding box of the text relative to the image.
%
%  The format of the RenderX11 method is:
%
%      MagickBooleanType RenderX11(Image *image,DrawInfo *draw_info,
%        const PointInfo *offset,TypeMetric *metrics)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o draw_info: the draw info.
%
%    o offset: (x,y) location of text relative to image.
%
%    o metrics: bounding box of text.
%
*/
static MagickBooleanType RenderX11(Image *image,const DrawInfo *draw_info,
  const PointInfo *offset,TypeMetric *metrics)
{
  MagickBooleanType
    status;

  if (annotate_semaphore == (SemaphoreInfo *) NULL)
    ActivateSemaphoreInfo(&annotate_semaphore);
  LockSemaphoreInfo(annotate_semaphore);
  status=XRenderImage(image,draw_info,offset,metrics);
  UnlockSemaphoreInfo(annotate_semaphore);
  return(status);
}
