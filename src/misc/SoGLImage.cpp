/**************************************************************************\
 *
 *  This file is part of the Coin 3D visualization library.
 *  Copyright (C) 1998-2001 by Systems in Motion.  All rights reserved.
 *  
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.  See the
 *  file LICENSE.GPL at the root directory of this source distribution
 *  for more details.
 *
 *  If you desire to use Coin with software that is incompatible
 *  licensewise with the GPL, and / or you would like to take
 *  advantage of the additional benefits with regard to our support
 *  services, please contact Systems in Motion about acquiring a Coin
 *  Professional Edition License.  See <URL:http://www.coin3d.org> for
 *  more information.
 *
 *  Systems in Motion, Prof Brochs gate 6, 7030 Trondheim, NORWAY
 *  <URL:http://www.sim.no>, <mailto:support@sim.no>
 *
\**************************************************************************/

/*!
  \class SoGLImage include/Inventor/misc/SoGLImage.h
  \brief The SoGLImage class is used to handle OpenGL 2D/3D textures.

  FIXME: Add TEX3 enviroment variables (or general TEX variables?) (kintel 20011112)

  A number of environment variables can be set to control how textures
  are created. This is useful to tune Coin to fit your system. E.g. if you
  are running on a laptop, it might be a good idea to disable linear
  filtering and mipmaps.

  \li COIN_TEX2_LINEAR_LIMIT: Linear filtering is enabled if
  Complexity::textureQuality is greater or equal to this
  value. Default value is 0.2.

  \li COIN_TEX2_MIPMAP_LIMIT: Mipmaps are created if textureQuality is
  greater or equal to this value. Default value is 0.5.

  \li COIN_TEX2_LINEAR_MIPMAP_LIMIT: Linear filtering between mipmap
  levels is enabled if textureQuality is greater or equal to this
  value. Default value is 0.8.

  \li COIN_TEX2_SCALEUP_LIMIT: Textures with width or height not equal
  to a power of two will always be scaled up if textureQuality is
  greater or equal to this value.  Default value is 0.7. If
  textureQuality is lower than this value, and the width or height is
  larger than 256 pixels, the texture is only scaled up if it's
  relatively close to the next power of two size. This could save a
  lot of texture memory.

  \li COIN_TEX2_BUILD_MIPMAP_FAST: If this environment variable is set
  to 1, an internal and optimized function will be used to create
  mipmaps. Otherwise gluBuild2DMipmap() will be used. Default value is
  0.

  \li COIN_TEX2_USE_GLTEXSUBIMAGE: When set, and when the new texture
  data has the same attributes as the old data, glTexSubImage() will
  be used to copy new data into the texture instead of recreating the
  texture.  This is not enabled by default, since it seems to trigger
  a bug in the Linux nVidia drivers. It just happens in some
  unreproducable cases.  It could be a bug in our glTexSubImage()
  code, of course. :)
*/

/*!
  \enum SoGLImage::Wrap

  Used to specify how texture coordinates < 0.0 and > 1.0 should be handled.
  It can either be repeated (REPEAT), clamped (CLAMP) or clamped to edge
  (CLAMP_TO_EDGE), which is useful when tiling textures.
*/

/*!
  \enum SoGLImage::Flags

  Can be used to tune/optimize the GL texture handling. Normally the
  texture quality will be used to decide scaling and filtering, and
  the image data will be scanned to decide if the image is (partly)
  transparent, and if the texture can be rendered using the cheaper
  alpha test instead of blending if it does contain transparency. If
  you know the contents of your texture image, or if you have special
  requirements on how the texture should be rendered, you can set the
  flags using the SoGLImage::setFlags() method.

*/

#include <Inventor/misc/SoGLImage.h>
#include <Inventor/SbImage.h>
#include <Inventor/misc/SoGL.h>
#include <Inventor/elements/SoGLTextureImageElement.h>
#include <Inventor/elements/SoTextureQualityElement.h>
#include <Inventor/elements/SoGLCacheContextElement.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoGLTexture3EnabledElement.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif // HAVE_CONFIG_H
#include "GLWrapper.h"
#include <GLUWrapper.h>
#include <Inventor/lists/SbList.h>
#include "../tidbits.h" // coin_getenv(), coin_atexit()

#include <simage_wrapper.h>

#if COIN_DEBUG
#include <Inventor/errors/SoDebugError.h>
#endif // COIN_DEBUG

static float DEFAULT_LINEAR_LIMIT = 0.2f;
static float DEFAULT_MIPMAP_LIMIT = 0.5f;
static float DEFAULT_LINEAR_MIPMAP_LIMIT = 0.8f;
static float DEFAULT_SCALEUP_LIMIT = 0.7f;

static float COIN_TEX2_LINEAR_LIMIT = -1.0f;
static float COIN_TEX2_MIPMAP_LIMIT = -1.0f;
static float COIN_TEX2_LINEAR_MIPMAP_LIMIT = -1.0f;
static float COIN_TEX2_SCALEUP_LIMIT = -1.0f;
static int COIN_TEX2_USE_GLTEXSUBIMAGE = -1;
static int COIN_TEX2_BUILD_MIPMAP_FAST = -1;

static int
compute_log(int value)
{
  int i = 0;
  while (value > 1) { value>>=1; i++; }
  return i;
}

//FIXME: Use as a special case of 3D image to reduce codelines ? (kintel 20011115)
static void
halve_image(const int width, const int height, const int nc,
            const unsigned char *datain, unsigned char *dataout)
{
  assert(width > 1 || height > 1);

  int nextrow = width *nc;
  int newwidth = width >> 1;
  int newheight = height >> 1;
  unsigned char *dst = dataout;
  const unsigned char *src = datain;

  // check for 1D images
  if (width == 1 || height == 1) {
    int n = SbMax(newwidth, newheight);
    for (int i = 0; i < n; i++) {
      for (int j = 0; j < nc; j++) {
        *dst = (src[0] + src[nc]) >> 1;
        dst++; src++;
      }
      src += nc; // skip to next pixel
    }
  }
  else {
    for (int i = 0; i < newheight; i++) {
      for (int j = 0; j < newwidth; j++) {
        for (int c = 0; c < nc; c++) {
          *dst = (src[0] + src[nc] + src[nextrow] + src[nextrow+nc] + 2) >> 2;
          dst++; src++;
        }
        src += nc; // skip to next pixel
      }
      src += nextrow;
    }
  }
}

static void
halve_image(const int width, const int height, const int depth, const int nc,
            const unsigned char *datain, unsigned char *dataout)
{
  assert(width > 1 || height > 1 || depth > 1);

  int rowsize = width * nc;
  int imagesize = width * height * nc;
  int newwidth = width >> 1;
  int newheight = height >> 1;
  int newdepth = depth >> 1;
  unsigned char *dst = dataout;
  const unsigned char *src = datain;

  int numdims = (width>=1?1:0)+(height>=1?1:0)+(depth>=1?1:0);
  // check for 1D images.
  if (numdims == 1) {
    int n = SbMax(SbMax(newwidth, newheight), newdepth);
    for (int i = 0; i < n; i++) {
      for (int j = 0; j < nc; j++) {
        *dst = (src[0] + src[nc]) >> 1;
        dst++; src++;
      }
      src += nc; // skip to next pixel/row/image
    }
  }
  // check for 2D images
  else if (numdims == 2) {
    int s1,s2,blocksize;
    if (width==1) {
      s1 = newheight;
      blocksize = height * nc;
    }
    else {
      s1 = newwidth;
      blocksize = width * nc;
    }
    s2 = depth==1?newheight:newdepth;
    for (int j = 0; j < s2; j++) {
      for (int i = 0; i < s1; i++) {
        for (int j = 0; j < nc; j++) {
          *dst = (src[0] + src[nc] + src[blocksize] + src[blocksize+nc] + 2) >> 2;
          dst++; src++;
        }
        src += nc; // skip to next pixel (x or y direction)
      }
      src += blocksize; // Skip to next row/image
    }
  }
  else { // 3D image
    for (int k = 0; k < newdepth; k++) {
      for (int j = 0; j < newheight; j++) {
        for (int i = 0; i < newwidth; i++) {
          for (int c = 0; c < nc; c++) {
            *dst = (src[0] + src[nc] + 
                    src[rowsize] + src[rowsize+nc] +
                    src[imagesize] + src[imagesize+nc] + 
                    src[imagesize+rowsize] + src[imagesize+rowsize+nc] +
                    4) >> 3;
            dst++; src++;
          }
          src += nc; // skip one pixel
        }
        src += rowsize; // skip one row
      }
      src += imagesize; // skip one image
    }
  }
}

static unsigned char *mipmap_buffer = NULL;
static int mipmap_buffer_size = 0;

static void
fast_mipmap_cleanup(void)
{
  delete [] mipmap_buffer;
}

// fast mipmap creation. no repeated memory allocations.
static void
fast_mipmap(SoState * state, int width, int height, const int nc,
            const unsigned char *data, GLenum format,
            const SbBool useglsubimage)
{
  const GLWrapper_t * glw = GLWRAPPER_FROM_STATE(state);
  int levels = compute_log(width);
  int level = compute_log(height);
  if (level > levels) levels = level;

  int memreq = (SbMax(width>>1,1))*(SbMax(height>>1,1))*nc;
  if (memreq > mipmap_buffer_size) {
    if (mipmap_buffer == NULL) {
      coin_atexit((coin_atexit_f *)fast_mipmap_cleanup);
    }
    else delete [] mipmap_buffer;
    mipmap_buffer = new unsigned char[memreq];
  }

  if (useglsubimage) {
    if (glw->glTexSubImage2D)
      glw->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                           width, height, format,
                           GL_UNSIGNED_BYTE, data);
  }
  else {
    glTexImage2D(GL_TEXTURE_2D, 0, nc, width, height, 0, format,
                 GL_UNSIGNED_BYTE, data);
  }
  unsigned char *src = (unsigned char *) data;
  for (level = 1; level <= levels; level++) {
    halve_image(width, height, nc, src, mipmap_buffer);
    if (width > 1) width >>= 1;
    if (height > 1) height >>= 1;
    src = mipmap_buffer;
    if (useglsubimage) {
    if (glw->glTexSubImage2D)
      glw->glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0,
                           width, height, format,
                           GL_UNSIGNED_BYTE, (void*) src);
    }
    else {
      glTexImage2D(GL_TEXTURE_2D, level, nc, width,
                   height, 0, format, GL_UNSIGNED_BYTE,
                   (void *) src);
    }
  }
}

// fast mipmap creation. no repeated memory allocations. 3D version.
static void
fast_mipmap(SoState * state, int width, int height, int depth, const int nc,
            const unsigned char *data, GLenum format,
            const SbBool useglsubimage)
{
  const GLWrapper_t * glw = GLWRAPPER_FROM_STATE(state);
  int levels = compute_log(SbMax(SbMax(width, height), depth));

  int memreq = (SbMax(width>>1,1))*(SbMax(height>>1,1))*(SbMax(depth>>1,1))*nc;
  if (memreq > mipmap_buffer_size) {
    if (mipmap_buffer == NULL) {
      coin_atexit(fast_mipmap_cleanup);
    }
    else delete [] mipmap_buffer;
    mipmap_buffer = new unsigned char[memreq];
  }

  // Send level 0 (original image) to OpenGL
  if (useglsubimage) {
    if (glw->glTexSubImage3D)
      glw->glTexSubImage3D(glw->COIN_GL_TEXTURE_3D, 0, 0, 0, 0,
                    width, height, depth, format,
                    GL_UNSIGNED_BYTE, data);
  }
  else {
    if (glw->glTexImage3D)
      glw->glTexImage3D(glw->COIN_GL_TEXTURE_3D, 0, nc, width, height, depth, 
			0, format, GL_UNSIGNED_BYTE, data);
  }
  unsigned char *src = (unsigned char *) data;
  for (int level = 1; level <= levels; level++) {
    halve_image(width, height, depth, nc, src, mipmap_buffer);
    if (width > 1) width >>= 1;
    if (height > 1) height >>= 1;
    if (depth > 1) depth >>= 1;
    src = mipmap_buffer;
    if (useglsubimage) {
    if (glw->glTexSubImage3D)
      glw->glTexSubImage3D(glw->COIN_GL_TEXTURE_3D, level, 0, 0, 0,
                           width, height, depth, format,
                           GL_UNSIGNED_BYTE, (void*) src);
    }
    else {
      if (glw->glTexImage3D)
	glw->glTexImage3D(glw->COIN_GL_TEXTURE_3D, level, nc, width,
			  height, depth, 0, format, GL_UNSIGNED_BYTE,
			  (void *) src);
    }
  }
}


#ifndef DOXYGEN_SKIP_THIS

class SoGLImageP {
public:
  static SoType classTypeId;

  SoGLDisplayList *createGLDisplayList(SoState *state);
  void checkTransparency(void);
  void unrefDLists(SoState *state);
  void reallyCreateTexture(SoState *state,
                           const unsigned char *const texture,
                           const int numComponents,
                           const int w, const int h, const int d,
                           const SbBool dlist,
                           const SbBool mipmap,
                           const int border);
  void resizeImage(SoState * state, unsigned char *&imageptr, 
                   int &xsize, int &ysize, int &zsize);
  SbBool shouldCreateMipmap(void);
  void applyFilter(const SbBool ismipmap);

  const SbImage *image;
  SbImage dummyimage;
  SbVec3s glsize;
  int glcomp;

  SbBool needtransparencytest;
  SbBool hastransparency;
  SbBool usealphatest;
  uint32_t flags;
  float quality;

  SoGLImage::Wrap wraps;
  SoGLImage::Wrap wrapt;
  SoGLImage::Wrap wrapr;
  int border;
  SbBool isregistered;
  uint32_t imageage;
  void (*endframecb)(void*);
  void *endframeclosure;

  class dldata {
  public:
    dldata(void)
      : dlist(NULL), age(0) { }
    dldata(SoGLDisplayList *dl)
      : dlist(dl),
        age(0) { }
    dldata(const dldata & org)
      : dlist(org.dlist),
        age(org.age) { }
    SoGLDisplayList *dlist;
    uint32_t age;
  };

  SbList <dldata> dlists;
  SoGLDisplayList *findDL(SoState *state);
  void tagDL(SoState *state);
  void unrefOldDL(SoState *state, const uint32_t maxage);
  SoGLImage *owner; // for debugging only

  void init(void);
};
SoType SoGLImageP::classTypeId;

#endif // DOXYGEN_SKIP_THIS

#undef THIS
// convenience define to access private data
#define THIS this->pimpl

/*!
  Constructor.
*/
SoGLImage::SoGLImage(void)
{
  THIS = new SoGLImageP;
  THIS->init(); // init members to default values
  THIS->owner = this; // for debugging

  // check environment variables
  if (COIN_TEX2_LINEAR_LIMIT < 0.0f) {
    const char *env = coin_getenv("COIN_TEX2_LINEAR_LIMIT");
    if (env) COIN_TEX2_LINEAR_LIMIT = atof(env);
    if (COIN_TEX2_LINEAR_LIMIT < 0.0f || COIN_TEX2_LINEAR_LIMIT > 1.0f) {
      COIN_TEX2_LINEAR_LIMIT = DEFAULT_LINEAR_LIMIT;
    }
  }
  if (COIN_TEX2_MIPMAP_LIMIT < 0.0f) {
    const char *env = coin_getenv("COIN_TEX2_MIPMAP_LIMIT");
    if (env) COIN_TEX2_MIPMAP_LIMIT = atof(env);
    if (COIN_TEX2_MIPMAP_LIMIT < 0.0f || COIN_TEX2_MIPMAP_LIMIT > 1.0f) {
      COIN_TEX2_MIPMAP_LIMIT = DEFAULT_MIPMAP_LIMIT;
    }
  }
  if (COIN_TEX2_LINEAR_MIPMAP_LIMIT < 0.0f) {
    const char *env = coin_getenv("COIN_TEX2_LINEAR_MIPMAP_LIMIT");
    if (env) COIN_TEX2_LINEAR_MIPMAP_LIMIT = atof(env);
    if (COIN_TEX2_LINEAR_MIPMAP_LIMIT < 0.0f || COIN_TEX2_LINEAR_MIPMAP_LIMIT > 1.0f) {
      COIN_TEX2_LINEAR_MIPMAP_LIMIT = DEFAULT_LINEAR_MIPMAP_LIMIT;
    }
  }

  if (COIN_TEX2_SCALEUP_LIMIT < 0.0f) {
    const char *env = coin_getenv("COIN_TEX2_SCALEUP_LIMIT");
    if (env) COIN_TEX2_SCALEUP_LIMIT = atof(env);
    if (COIN_TEX2_SCALEUP_LIMIT < 0.0f || COIN_TEX2_SCALEUP_LIMIT > 1.0f) {
      COIN_TEX2_SCALEUP_LIMIT = DEFAULT_SCALEUP_LIMIT;
    }
  }

  if (COIN_TEX2_BUILD_MIPMAP_FAST < 0) {
    const char *env = coin_getenv("COIN_TEX2_BUILD_MIPMAP_FAST");
    if (env && atoi(env) == 1) {
      COIN_TEX2_BUILD_MIPMAP_FAST = 1;
    }
    else COIN_TEX2_BUILD_MIPMAP_FAST = 0;
  }
  if (COIN_TEX2_USE_GLTEXSUBIMAGE < 0) {
    const char *env = coin_getenv("COIN_TEX2_USE_GLTEXSUBIMAGE");
    if (env && atoi(env) == 1) {
      COIN_TEX2_USE_GLTEXSUBIMAGE = 1;
    }
    else COIN_TEX2_USE_GLTEXSUBIMAGE = 0;
  }
}

/*!
  Returns the type id for this class.
*/
SoType
SoGLImage::getClassTypeId(void)
{
  if (SoGLImageP::classTypeId.isBad()) {
    SoGLImageP::classTypeId = SoType::createType(SoType::badType(),
                                                 SbName("GLImage"));
  }
  return SoGLImageP::classTypeId;
}


// FIXME: grab better version of getTypeId() doc from SoBase, SoAction
// and / or SoDetail?  At least if this ever makes it into the public
// API.  20010913 mortene.
/*!
  Returns the type id for an SoGLImage instance.
*/
SoType
SoGLImage::getTypeId(void) const
{
  return SoGLImage::getClassTypeId();
}

/*!
  Used by SoGLTextureImageElement.
 */
void
SoGLImage::applyQuality(SoGLDisplayList *dl, const float quality)
{
  float oldquality = THIS->quality;
  THIS->quality = quality;
  THIS->applyFilter(dl->isMipMapTextureObject()); // GL calls are here
  THIS->quality = oldquality;
}

/*!
  Returns whether an SoGLImage instance inherits (or is of) type \a
  type.
*/
SbBool
SoGLImage::isOfType(SoType type) const
{
  return this->getTypeId().isDerivedFrom(type);
}

/*!  
  Convenience 2D wrapper function around the 3D setData().
*/
void
SoGLImage::setData(const SbImage *image,
                   const Wrap wraps,
                   const Wrap wrapt,
                   const float quality,
                   const int border,
                   SoState *createinstate)

{
  this->setData(image, wraps, wrapt, (Wrap)THIS->wrapr, 
                quality, border, createinstate);
}

/*!
  Sets the data for this GL image. Should only be called when one
  of the parameters have changed, since this will cause the GL texture
  object to be recreated.  Caller is responsible for sending legal
  Wrap values.  CLAMP_TO_EDGE is only supported on OpenGL v1.2
  implementations, and as an extension on some earlier SGI
  implementations (GL_SGIS_texture_edge_clamp).

  For now, if quality > 0.5 when created, we create mipmaps, otherwise
  a regular texture is created.  Be aware, if you for instance create
  a texture with texture quality 0.4, and then later try to apply the
  texture with a texture quality greater than 0.5, the texture object
  will be recreated as a mipmap texture object. This will happen only
  once though, of course.

  If \a border != 0, the OpenGL texture will be created with this
  border size. Be aware that this might be extremely slow on most PC
  hardware.

  Normally, the OpenGL texture object isn't created until the first
  time it is needed, but if \a createinstate is != NULL, the texture
  object is created immediately. This is useful if you use a temporary
  buffer to hold the texture data. Be careful when using this feature,
  since the texture data might be needed at a later stage (for
  instance to create a texture object for another context).  It will
  not be possible to create texture objects for other cache contexts
  when \a createinstate is != NULL.

  Also if \a createinstate is supplied, and all the attributes are the
  same as the current data in the image, glTexSubImage() will be used
  to insert the image data instead of creating a new texture object.
  This is much faster on most OpenGL drivers, and is very useful, for
  instance when doing animated textures.
  
  If you supply NULL for \a image, the instance will be reset, causing
  all display lists and memory to be freed. 
*/
void
SoGLImage::setData(const SbImage *image,
                   const Wrap wraps,
                   const Wrap wrapt,
                   const Wrap wrapr,
                   const float quality,
                   const int border,
                   SoState *createinstate)

{
  THIS->imageage = 0;

  if (image == NULL) {
    THIS->unrefDLists(createinstate);
    if (THIS->isregistered) SoGLImage::unregisterImage(this);
    THIS->init(); // init to default values
    return;
  }

  THIS->needtransparencytest = TRUE;
  THIS->hastransparency = FALSE;
  THIS->usealphatest = FALSE;
  THIS->quality = quality;

  // check for special case where glTexSubImage can be used.
  // faster for most drivers.
  if (createinstate) { // We need the state for GLWrapper
    const GLWrapper_t * glw = GLWRAPPER_FROM_STATE(createinstate);
    SoGLDisplayList *dl = NULL;
    
    SbBool copyok = 
      wraps == THIS->wraps &&
      wrapt == THIS->wrapt &&
      wrapr == THIS->wrapr &&
      border == THIS->border &&
      border == 0 && // haven't tested with borders yet. Play it safe.
      (dl = THIS->findDL(createinstate)) != NULL;
    
    unsigned char *bytes = NULL;
    SbVec3s size;
    int nc;
    if (copyok) {
      bytes = image->getValue(size, nc);
      if (bytes && size != THIS->glsize || nc != THIS->glcomp) copyok = FALSE;
    }
    
    SbBool is3D = (size[2]==0)?FALSE:TRUE;
    SbBool usesubimage = COIN_TEX2_USE_GLTEXSUBIMAGE &&
      (is3D && glw->glTexSubImage3D) || (!is3D && glw->glTexSubImage2D);
    
    if (!usesubimage) copyok=FALSE;

    if (copyok) {
      dl->ref();
      THIS->unrefDLists(createinstate);
      THIS->dlists.append(SoGLImageP::dldata(dl));
      THIS->image = NULL; // data is temporary, and only for current context
      dl->call(createinstate);
      GLenum format;
      switch (nc) {
      default: // avoid compiler warnings
      case 1: format = GL_LUMINANCE; break;
      case 2: format = GL_LUMINANCE_ALPHA; break;
      case 3: format = GL_RGB; break;
      case 4: format = GL_RGBA; break;
      }
      
      if (dl->isMipMapTextureObject()) {
        if (is3D)
          fast_mipmap(createinstate, size[0], size[1], size[2], nc,
                      bytes, format, TRUE);
        else 
          fast_mipmap(createinstate, size[0], size[1], nc,
                      bytes, format, TRUE);
      }
      else {
        if (is3D) {
          glw->glTexSubImage3D(glw->COIN_GL_TEXTURE_3D, 0, 0, 0, 0,
                               size[0], size[1], size[2],
                               format, GL_UNSIGNED_BYTE,
                               (void*) bytes);
        }
        else {
          glw->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                               size[0], size[1],
                               format, GL_UNSIGNED_BYTE,
                               (void*) bytes);
        }
      }
    }
    else {
      THIS->image = image;
      THIS->wraps = wraps;
      THIS->wrapt = wrapt;
      THIS->wrapr = wrapr;
      THIS->border = border;
      THIS->unrefDLists(createinstate);
      if (createinstate) {
        THIS->dlists.append(SoGLImageP::dldata(THIS->createGLDisplayList(createinstate)));
        THIS->image = NULL; // data is assumed to be temporary
      }
    }
  }
  else {
    THIS->image = image;
    THIS->wraps = wraps;
    THIS->wrapt = wrapt;
    THIS->wrapr = wrapr;
    THIS->border = border;
    THIS->unrefDLists(createinstate);
  }

  if (THIS->image && !THIS->isregistered && !(this->getFlags() & INVINCIBLE)) {
    THIS->isregistered = TRUE;
    SoGLImage::registerImage(this);
  }
}

/*!
  2D setData() wrapper. Supplies raw data, size and numcomponents instead of
  an SbImage. Creates a temporary image, then calls the read setData().
  \overload
*/
void 
SoGLImage::setData(const unsigned char *bytes,
                   const SbVec2s & size,
                   const int numcomponents,
                   const Wrap wraps,
                   const Wrap wrapt,
                   const float quality,
                   const int border,
                   SoState *createinstate)
{
  THIS->dummyimage.setValuePtr(size, numcomponents, bytes);
  this->setData(&THIS->dummyimage,
                wraps, wrapt, quality,
                border, createinstate);
}

/*!
  3D setData() wrapper. Supplies raw data, size and numcomponents instead of
  an SbImage. Creates a temporary image, then calls the read setData().
  \overload
*/
void 
SoGLImage::setData(const unsigned char *bytes,
                   const SbVec3s & size,
                   const int numcomponents,
                   const Wrap wraps,
                   const Wrap wrapt,
                   const Wrap wrapr,
                   const float quality,
                   const int border,
                   SoState *createinstate)
{
  THIS->dummyimage.setValuePtr(size, numcomponents, bytes);
  this->setData(&THIS->dummyimage, 
                wraps, wrapt, wrapr, quality,
                border, createinstate);
}


/*!
  Destructor.
*/
SoGLImage::~SoGLImage()
{
  if (THIS->isregistered) SoGLImage::unregisterImage(this);
  THIS->unrefDLists(NULL);
  delete THIS;
}

/*!
  This class has a private destuctor since we want users to supply
  the current GL state when deleting the image. This is to make sure
  gl texture objects are freed as soon as possible. If you supply
  NULL to this method, the gl texture objects won't be deleted
  until the next time an GLRenderAction is applied in the image's
  cache context(s).
*/
void
SoGLImage::unref(SoState *state)
{
  THIS->unrefDLists(state);
  delete this;
}

/*!
  Sets flags to control how the texture is handled/initialized.
*/
void
SoGLImage::setFlags(const uint32_t flags)
{
  THIS->flags = flags;
}

/*!
  Returns the flags.

  \sa setFlags()
*/
uint32_t
SoGLImage::getFlags(void) const
{
  return THIS->flags;
}

/*!
  Returns a pointer to the image data.
*/
const SbImage *
SoGLImage::getImage(void) const
{
  return THIS->image;
}

/*!
  Returns or creates a SoGLDisplayList to be used for rendering.
  Returns NULL if no SoDLDisplayList could be created.
*/
SoGLDisplayList *
SoGLImage::getGLDisplayList(SoState *state)
{
  SoGLDisplayList *dl = THIS->findDL(state);

  if (dl == NULL) {
    dl = THIS->createGLDisplayList(state);
    if (dl) THIS->dlists.append(SoGLImageP::dldata(dl));
  }
  if (dl && !dl->isMipMapTextureObject() && THIS->image) {
    float quality = SoTextureQualityElement::get(state);
    float oldquality = THIS->quality;
    THIS->quality = quality;
    if (THIS->shouldCreateMipmap()) {
      // recreate DL to get a mipmapped image
      int n = THIS->dlists.getLength();
      for (int i = 0; i < n; i++) {
        if (THIS->dlists[i].dlist == dl) {
          dl->unref(state); // unref old DL
          dl = THIS->createGLDisplayList(state);
          THIS->dlists[i].dlist = dl;
          break;
        }
      }
    }
    else THIS->quality = oldquality;
  }

  return dl;
}


/*!
  Returns \e TRUE if this texture has some pixels with alpha != 255
*/
SbBool
SoGLImage::hasTransparency(void) const
{
  if (THIS->flags & FORCE_TRANSPARENCY_TRUE) return TRUE;
  if (THIS->flags & FORCE_TRANSPARENCY_FALSE) return FALSE;

  if (THIS->needtransparencytest) {
    ((SoGLImage*)this)->pimpl->checkTransparency();
  }
  return THIS->hastransparency;
}

/*!
  Returns TRUE if this image has some alpha value != 255, and all
  these values are 0. If this is the case, alpha test can be used
  to render this texture instead of for instance blending, which
  is usually slower and might yield z-buffer artifacts.
*/
SbBool
SoGLImage::useAlphaTest(void) const
{
  if (THIS->flags & FORCE_ALPHA_TEST_TRUE) return TRUE;
  if (THIS->flags & FORCE_ALPHA_TEST_FALSE) return FALSE;

  if (THIS->needtransparencytest) {
    ((SoGLImage*)this)->pimpl->checkTransparency();
  }
  return THIS->usealphatest;
}

/*!
  Returns the wrap strategy for the S (horizontal) direction.
*/
SoGLImage::Wrap
SoGLImage::getWrapS(void) const
{
  return THIS->wraps;
}

/*!
  Returns the wrap strategy for the T (vertical) direction.
*/
SoGLImage::Wrap
SoGLImage::getWrapT(void) const
{
  return THIS->wrapt;
}

/*!
  Returns the wrap strategy for the R (depth) direction.
*/
SoGLImage::Wrap
SoGLImage::getWrapR(void) const
{
  return THIS->wrapr;
}

/*!
  Virtual method that will be called once each frame.  The method
  should unref display lists that has an age bigger or equal to \a
  maxage, and increment the age for other display lists.
*/
void 
SoGLImage::unrefOldDL(SoState *state, const uint32_t maxage)
{
  THIS->unrefOldDL(state, maxage);
  this->incAge();
}

#ifndef DOXYGEN_SKIP_THIS

void
SoGLImageP::init(void)
{
  this->isregistered = FALSE;
  this->image = NULL;
  this->glsize.setValue(0,0,0);
  this->glcomp = 0;
  this->wraps = SoGLImage::CLAMP;
  this->wrapt = SoGLImage::CLAMP;
  this->wrapr = SoGLImage::CLAMP;
  this->border = 0;
  this->flags = SoGLImage::USE_QUALITY_VALUE;
  this->needtransparencytest = TRUE;
  this->hastransparency = FALSE;
  this->usealphatest = FALSE;
  this->quality = 0.4f;
  this->imageage = 0;
  this->endframecb = NULL;
}

// returns the number of bits set, and xsets highbit to
// the highest bit set.
static int
cnt_bits(unsigned long val, int & highbit)
{
  int cnt = 0;
  highbit = 0;
  while (val) {
    if (val & 1) cnt++;
    val>>=1;
    highbit++;
  }
  return cnt;
}

// returns the next power of two greater or equal to val
static unsigned long
nearest_power_of_two(unsigned long val)
{
  int highbit;
  if (cnt_bits(val, highbit) > 1) {
    return 1<<highbit;
  }
  return val;
}

// static data used to temporarily store image when resizing
static unsigned char *glimage_tmpimagebuffer = NULL;
static int glimage_tmpimagebuffersize = 0;

static void cleanup_tmpimage(void)
{
  delete [] glimage_tmpimagebuffer;
}

//
// resize image if necessary. Returns pointer to temporary
// buffer if that happens, and the new size in xsize, ysize.
//
void
SoGLImageP::resizeImage(SoState * state, unsigned char *& imageptr, 
                        int & xsize, int & ysize, int & zsize)
{
  SbVec3s size;
  int numcomponents;
  unsigned char *bytes = this->image->getValue(size, numcomponents);

  unsigned int newx = (unsigned int)nearest_power_of_two(xsize-2*this->border);
  unsigned int newy = (unsigned int)nearest_power_of_two(ysize-2*this->border);
  unsigned int newz = (unsigned int)nearest_power_of_two(zsize-2*this->border);

  // if >= 256 and low quality, don't scale up unless size is
  // close to an above power of two. This saves a lot of texture memory
  if (this->flags & SoGLImage::USE_QUALITY_VALUE) {
    if (this->quality < COIN_TEX2_SCALEUP_LIMIT) {
      if ((newx >= 256) && ((newx - (xsize-2*this->border)) > (newx>>3))) 
        newx >>= 1;
      if ((newy >= 256) && ((newy - (ysize-2*this->border)) > (newy>>3))) 
        newy >>= 1;
      if ((newz >= 256) && ((newz - (zsize-2*this->border)) > (newz>>3))) 
        newz >>= 1;
    }
  }
  else if (this->flags & SoGLImage::SCALE_DOWN) {
    // no use scaling down for very small images
    if (newx > (unsigned int)xsize && newx > 16) newx >>= 1;
    if (newy > (unsigned int)ysize && newy > 16) newy >>= 1;
    if (newz > (unsigned int)zsize && newz > 16) newz >>= 1;
  }

  // downscale to legal GL size (implementation dependant)
  SoGLTextureImageElement * elem = (SoGLTextureImageElement *)
    state->getConstElement(SoGLTextureImageElement::getClassStackIndex());
  SbBool sizeok = false;
#if COIN_DEBUG
  SbVec3s orgsize(newx, newy, newz);
#endif
  while (!sizeok) {
    sizeok = elem->isTextureSizeLegal(newx, newy, newz, numcomponents);
    if (!sizeok) {
      unsigned int max = SbMax(newx, SbMax(newy, newz));
      if (max==newz) newz >>= 1;
      else if (max==newy) newy >>= 1;
      else newx >>= 1;
    }
  }
#if COIN_DEBUG
  if ((unsigned int) orgsize[0] != newx || 
      (unsigned int) orgsize[1] != newy || 
      (unsigned int) orgsize[2] != newz) {
    if (newz != 0) {
      SoDebugError::postWarning("SoGLImageP::resizeImage",
                                "Original 3D texture too large for "
                                "your graphics hardware and / or OpenGL "
                                "driver. Rescaled from (%d x %d x %d) "
                                "to (%d x %d x %d).", 
                                orgsize[0], orgsize[1], orgsize[2],
                                newx, newy, newz);
    }
    else {
      SoDebugError::postWarning("SoGLImageP::resizeImage",
                                "Original 2D texture too large for "
                                "your graphics hardware and / or OpenGL "
                                "driver. Rescaled from (%d x %d) "
                                "to (%d x %d).", 
                                orgsize[0], orgsize[1], newx, newy);
    }
  }
#endif // COIN_DEBUG

  newx += 2 * this->border;
  newy += 2 * this->border;
  newz = (zsize==0)?0:newz + (2 * this->border);

  if ((newx != (unsigned long) xsize) || 
      (newy != (unsigned long) ysize) ||
      (newz != (unsigned long) zsize)) { // We need to resize
    int numbytes = newx * newy * ((newz==0)?1:newz) * numcomponents;
    if (numbytes > glimage_tmpimagebuffersize) {
      if (glimage_tmpimagebuffer == NULL) coin_atexit((coin_atexit_f *)cleanup_tmpimage);
      else delete [] glimage_tmpimagebuffer;
      glimage_tmpimagebuffer = new unsigned char[numbytes];
      glimage_tmpimagebuffersize = numbytes;
    }
    // simage version 1.1.1 has a pretty high quality resize
    // function. We prefer to use that to avoid using GLU, since
    // there are lots of buggy GLU libraries out there.
    if (zsize == 0) { // 2D image
      if (simage_wrapper()->available &&
          simage_wrapper()->versionMatchesAtLeast(1,1,1) &&
          simage_wrapper()->simage_resize) {
        unsigned char *result =
          simage_wrapper()->simage_resize((unsigned char*) bytes,
                                          xsize, ysize, numcomponents,
                                          newx, newy);
        (void)memcpy(glimage_tmpimagebuffer, result, numbytes);
        simage_wrapper()->simage_free_image(result);
      }
      else if (GLUWrapper()->available) {
        GLenum format;
        switch (numcomponents) {
        default: // avoid compiler warnings
        case 1: format = GL_LUMINANCE; break;
        case 2: format = GL_LUMINANCE_ALPHA; break;
        case 3: format = GL_RGB; break;
        case 4: format = GL_RGBA; break;
        }
        
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        
        // FIXME: ignoring the error code. Silly. 20000929 mortene.
        (void)GLUWrapper()->gluScaleImage(format, xsize, ysize,
                                          GL_UNSIGNED_BYTE, (void*) bytes,
                                          newx, newy, GL_UNSIGNED_BYTE,
                                          (void*)glimage_tmpimagebuffer);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
      }
#if COIN_DEBUG
      else {
        SoDebugError::postWarning("SoGLImageP::resizeImage",
                                  "No resize function found.");
      }
#endif // COIN_DEBUG
    }
    else { // (zsize > 0) => 3D image
      if (simage_wrapper()->available &&
          simage_wrapper()->versionMatchesAtLeast(1,3,0) &&
          simage_wrapper()->simage_resize3d) {
        unsigned char *result =
          simage_wrapper()->simage_resize3d((unsigned char*) bytes,
                                            xsize, ysize, numcomponents, zsize,
                                            newx, newy, newz);
        (void)memcpy(glimage_tmpimagebuffer, result, numbytes);
        simage_wrapper()->simage_free_image(result);
      }
#if COIN_DEBUG
      else {
        SoDebugError::postWarning("SoGLImageP::resizeImage",
                                  "No 3D resize function found.");
      }
#endif // COIN_DEBUG
    }
    imageptr = glimage_tmpimagebuffer;
  }
  xsize = newx;
  ysize = newy;
  zsize = newz;
}

//
// private method that in addition to creating the display list,
// tests the size of the image and performs a resize if the size is not 
// a power of two.
// reallyCreateTexture is called (only) from here.
//
SoGLDisplayList *
SoGLImageP::createGLDisplayList(SoState *state)
{
  SbVec3s size;
  int numcomponents;
  unsigned char *bytes = 
    this->image ? this->image->getValue(size, numcomponents) : NULL;
  
  if (!bytes) return NULL;

  int xsize = size[0];
  int ysize = size[1];
  int zsize = size[2];

  // these might change if image is resized
  unsigned char *imageptr = (unsigned char *) bytes;
  this->resizeImage(state, imageptr, xsize, ysize, zsize);

  SbBool mipmap = this->shouldCreateMipmap();

  SoGLDisplayList *dl = new SoGLDisplayList(state,
                                            SoGLDisplayList::TEXTURE_OBJECT,
                                            1, mipmap);
  dl->ref();
  dl->open(state);
  this->reallyCreateTexture(state, imageptr, numcomponents,
                            xsize, ysize, zsize,
                            dl->getType() == SoGLDisplayList::DISPLAY_LIST,
                            mipmap,
                            this->border);
  dl->close(state);
  return dl;
}

//
// Test image data for transparency by checking each texel.
// 
void
SoGLImageP::checkTransparency(void)
{
  this->needtransparencytest = FALSE;
  this->usealphatest = FALSE;
  this->hastransparency = FALSE;

  SbVec3s size;
  int numcomponents;
  unsigned char *bytes = this->image ?
    this->image->getValue(size, numcomponents) : NULL;

  if (bytes == NULL) {
    if (this->glcomp == 2 || this->glcomp == 4) {
      // we must assume it has transparency, and that we
      // can't use alpha testing
      this->hastransparency = TRUE;
    }
  }
  else {
    if (numcomponents == 2 || numcomponents == 4) {
      int n = size[0] * size[1] * (size[2] ? size[2] : 1);
      int nc = numcomponents;
      unsigned char *ptr = (unsigned char *) bytes + nc - 1;

      while (n) {
        if (*ptr != 255 && *ptr != 0) break;
        if (*ptr == 0) this->usealphatest = TRUE;
        ptr += nc;
        n--;
      }
      if (n > 0) {
        this->hastransparency = TRUE;
        this->usealphatest = FALSE;
      }
      else {
        this->hastransparency = this->usealphatest;
      }
    }
  }
}

static GLenum
translate_wrap(SoState *state, const SoGLImage::Wrap wrap)
{
  if (wrap == SoGLImage::REPEAT) return (GLenum) GL_REPEAT;
  if (wrap == SoGLImage::CLAMP_TO_EDGE) {
    const GLWrapper_t * glw = GLWRAPPER_FROM_STATE(state);
    if (glw->COIN_GL_CLAMP_TO_EDGE) return (GLenum) glw->COIN_GL_CLAMP_TO_EDGE;
  }
  return (GLenum) GL_CLAMP;
}

void
SoGLImageP::reallyCreateTexture(SoState *state,
                                const unsigned char *const texture,
                                const int numComponents,
                                const int w, const int h, const int d,
                                const SbBool dlist, //FIXME: Not in use (kintel 20011129)
                                const SbBool mipmap,
                                const int border)
{
  const GLWrapper_t * glw = GLWRAPPER_FROM_STATE(state);
  this->glsize = SbVec3s((short) w, (short) h, (short) d);
  this->glcomp = numComponents;

  GLenum glformat;
  switch (numComponents) {
  case 1:
    glformat = GL_LUMINANCE;
    break;
  case 2:
    glformat = GL_LUMINANCE_ALPHA;
    break;
  case 3:
    glformat = GL_RGB;
    break;
  case 4:
    glformat = GL_RGBA;
    break;
  default:
    assert(0 && "illegal numComponents");
    glformat = GL_RGB; // Unnecessary, but kills a compiler warning.
  }

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  //FIXME: Check GLWrapper capability as well? (kintel 20011129)
  if (SoGLTexture3EnabledElement::get(state)) { // 3D textures
    glTexParameteri(glw->COIN_GL_TEXTURE_3D, GL_TEXTURE_WRAP_S,
                    translate_wrap(state, this->wraps));
    glTexParameteri(glw->COIN_GL_TEXTURE_3D, GL_TEXTURE_WRAP_T,
                    translate_wrap(state, this->wrapt));
    glTexParameteri(glw->COIN_GL_TEXTURE_3D, GL_TEXTURE_WRAP_R,
                    translate_wrap(state, this->wrapr));

    // just initialize to a filter that is valid also for non mipmapped
    // images. Filter will be applied later by SoGLTextureImageElement
    glTexParameterf(glw->COIN_GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(glw->COIN_GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    if (!mipmap) {
      if (glw->glTexImage3D)
        glw->glTexImage3D(glw->COIN_GL_TEXTURE_3D, 0, numComponents, w, h, d,
                          border, glformat, GL_UNSIGNED_BYTE, texture);
    }
    else { // mipmaps
      //FIXME: TEX2->TEX3 ? (kintel 20011129)
      if (COIN_TEX2_BUILD_MIPMAP_FAST == 1 || 
          !GLUWrapper()->available ||
          !GLUWrapper()->gluBuild3DMipmaps) {
        // this should be very fast, but I guess it's possible
        // that some drivers might have hardware accelerated mipmap
        // creation.
        fast_mipmap(state, w, h, d, numComponents, texture, glformat, FALSE);
      }
      else {
        // FIXME: ignoring the error code. Silly. 20000929 mortene.
        (void)GLUWrapper()->gluBuild3DMipmaps(GL_TEXTURE_3D, numComponents, 
                                              w, h, d, glformat, 
                                              GL_UNSIGNED_BYTE, texture);
      }
    }
  }
  else { // 2D textures
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                    translate_wrap(state, this->wraps));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                    translate_wrap(state, this->wrapt));
    
    // just initialize to a filter that is valid also for non mipmapped
    // images. Filter will be applied later by SoGLTextureImageElement
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    
    if (!mipmap) { // Create only level 0 texture.
      glTexImage2D(GL_TEXTURE_2D, 0, numComponents, w, h,
                   border, glformat, GL_UNSIGNED_BYTE, texture);
    }
    else { // mipmaps
      if (COIN_TEX2_BUILD_MIPMAP_FAST == 1 || !GLUWrapper()->available) {
        // this should be very fast, but I guess it's possible
        // that some drivers might have hardware accelerated mipmap
        // creation.
        fast_mipmap(state, w, h, numComponents, texture, glformat, FALSE);
      }
      else {
        // FIXME: ignoring the error code. Silly. 20000929 mortene.
        (void)GLUWrapper()->gluBuild2DMipmaps(GL_TEXTURE_2D, numComponents, 
                                              w, h, glformat, 
                                              GL_UNSIGNED_BYTE, texture);
      }
    }
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

//
// unref all dlists stored in image
//
void
SoGLImageP::unrefDLists(SoState *state)
{
  int n = this->dlists.getLength();
  for (int i = 0; i < n; i++) {
    this->dlists[i].dlist->unref(state);
  }
  this->dlists.truncate(0);
}

// find dl for a context, NULL if not found
SoGLDisplayList *
SoGLImageP::findDL(SoState *state)
{
  int currcontext = SoGLCacheContextElement::get(state);
  int i, n = this->dlists.getLength();
  SoGLDisplayList *dl;
  for (i = 0; i < n; i++) {
    dl = this->dlists[i].dlist;
    if (dl->getContext() == currcontext) return dl;
  }
  return NULL;
}

void
SoGLImageP::tagDL(SoState *state)
{
  int currcontext = SoGLCacheContextElement::get(state);
  int i, n = this->dlists.getLength();
  SoGLDisplayList *dl;
  for (i = 0; i < n; i++) {
    dl = this->dlists[i].dlist;
    if (dl->getContext() == currcontext) {
      this->dlists[i].age = 0;
      break;
    }
  }
}

void 
SoGLImage::incAge(void) const
{
  THIS->imageage++;
}

void 
SoGLImage::resetAge(void) const
{
  THIS->imageage = 0;
}

void
SoGLImageP::unrefOldDL(SoState *state, const uint32_t maxage)
{
  int n = this->dlists.getLength();
  int i = 0;

  while (i < n) {
    dldata & data = this->dlists[i];
    if (data.age >= maxage) {
#if COIN_DEBUG && 0 // debug
      SoDebugError::postInfo("SoGLImageP::unrefOldDL",
                             "DL killed because of old age: %p",
                             this->owner);
#endif // debug
      data.dlist->unref(state);
      this->dlists.removeFast(i);
      n--; // one less in list now
    }
    else {
      // increment age
      data.age++;
      i++;
    }
  }
}

SbBool
SoGLImageP::shouldCreateMipmap(void)
{
  if (this->flags & SoGLImage::USE_QUALITY_VALUE) {
    return this->quality >= COIN_TEX2_MIPMAP_LIMIT;
  }
  else {
    return (this->flags & SoGLImage::NO_MIPMAP) == 0;
  }
}

//
// Actually apply the texture filters using OpenGL calls.
//
void
SoGLImageP::applyFilter(const SbBool ismipmap)
{
  GLenum target;

  // Casting away const
  const SbVec3s size = this->image ? this->image->getSize() : this->glsize;
  
  if (size[2] >= 1) target = GL_TEXTURE_3D;
  else target = GL_TEXTURE_2D;

  if (this->flags & SoGLImage::USE_QUALITY_VALUE) {
    if (this->quality < COIN_TEX2_LINEAR_LIMIT) {
      glTexParameterf(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameterf(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    }
    else if ((this->quality < COIN_TEX2_MIPMAP_LIMIT) || !ismipmap) {
      glTexParameterf(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameterf(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    else if (this->quality < COIN_TEX2_LINEAR_MIPMAP_LIMIT) {
      glTexParameterf(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameterf(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    }
    else { // max quality
      glTexParameterf(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameterf(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }
  }
  else {
    if (this->flags & SoGLImage::NO_MIPMAP || !ismipmap) {
      glTexParameterf(target, GL_TEXTURE_MAG_FILTER,
                      (this->flags & SoGLImage::LINEAR_MAG_FILTER) ? 
                      GL_LINEAR : GL_NEAREST);
      glTexParameterf(target, GL_TEXTURE_MIN_FILTER,
                      (this->flags & SoGLImage::LINEAR_MIN_FILTER) ? 
                      GL_LINEAR : GL_NEAREST);
    }
    else {
      glTexParameterf(target, GL_TEXTURE_MAG_FILTER,
                      (this->flags & SoGLImage::LINEAR_MAG_FILTER) ? 
                      GL_LINEAR : GL_NEAREST);
      GLenum minfilter = GL_NEAREST_MIPMAP_NEAREST;
      if (this->flags & SoGLImage::LINEAR_MIPMAP_FILTER) {
        if (this->flags & SoGLImage::LINEAR_MIN_FILTER)
          minfilter = GL_LINEAR_MIPMAP_LINEAR;
        else
          minfilter = GL_LINEAR_MIPMAP_NEAREST;
      }
      else if (this->flags & SoGLImage::LINEAR_MIN_FILTER)
        minfilter = GL_NEAREST_MIPMAP_LINEAR;

      glTexParameterf(target, GL_TEXTURE_MIN_FILTER,
                      minfilter);
    }
  }
}
#endif // DOXYGEN_SKIP_THIS

//
// Texture resource management.
//
// FIXME: consider sorting images on an LRU strategy to
// speed up the process of searching for GL-images to free.
//

static SbList <SoGLImage*> * glimage_reglist;
static uint32_t glimage_maxage = 60;

static void
regimage_cleanup(void)
{
  delete glimage_reglist;
}

/*!
  When doing texture resource control, call this method before
  rendering the scene, typically in the viewer's actualRedraw().
  \a state should be your SoGLRenderAction state.

  \sa endFrame(), tagImage(), setDisplayListMaxAge()
*/
void
SoGLImage::beginFrame(SoState * /* state */)
{
  // nothing is done for now
}

/*!
  Should be called when a texture image is used. In Coin this is
  handled by SoGLTextureImageElement, but if you use an SoGLImage on
  your own, you should call this method to avoid that the display list
  is deleted too soon. \a state should be your SoGLRenderAction state,
  \a image the image you are about to use/have used.
*/
void
SoGLImage::tagImage(SoState *state, SoGLImage *image)
{
  assert(image);
  if (image) {
    image->resetAge();
    image->pimpl->tagDL(state);
  }
}

/*!
  Should be called after your scene is rendered. Old display
  lists will be deleted when you call this method. \a state
  should be your SoGLRenderAction state.

  \sa beginFrame(), tagImage(), setDisplayListMaxAge()
*/
void
SoGLImage::endFrame(SoState *state)
{
  if (glimage_reglist) {
    int n = glimage_reglist->getLength();
    for (int i = 0; i < n; i++) {
      SoGLImage *img = (*glimage_reglist)[i];
      img->unrefOldDL(state, glimage_maxage);
      if (img->pimpl->endframecb) 
        img->pimpl->endframecb(img->pimpl->endframeclosure);
    }
  }
}

void 
SoGLImage::setEndFrameCallback(void (*cb)(void *), void *closure)
{
  THIS->endframecb = cb;
  THIS->endframeclosure = closure;
}

int 
SoGLImage::getNumFramesSinceUsed(void) const
{
  return THIS->imageage;
}

/*!
  Free all GL images currently used. This can be used to help the
  operating system and/or OpenGL driver's resource handling.  If you
  know you're not going to render for a while, maybe you're switching
  to a different application or something, calling this method could
  be a good idea since it will release all the texture memory used by
  your application.
*/
void 
SoGLImage::freeAllImages(SoState *state)
{
  int oldmax = glimage_maxage;
  glimage_maxage = 0;
  // call begin/end with maxage 0 to free all images
  SoGLImage::beginFrame(state);
  SoGLImage::endFrame(state);
  glimage_maxage = oldmax;
}

/*!
  Set the maximum age for a texture object/display list.  The age
  of an image is the number of frames since it has been used.
  Default maximum age is 60.
*/
void
SoGLImage::setDisplayListMaxAge(const uint32_t maxage)
{
  glimage_maxage = maxage;
}

// used internally to keep track of the SoGLImages
void
SoGLImage::registerImage(SoGLImage *image)
{
  if (glimage_reglist == NULL) {
    glimage_reglist = new SbList<SoGLImage*>;
    coin_atexit((coin_atexit_f *)regimage_cleanup);
  }
  assert(glimage_reglist->find(image) < 0);
  glimage_reglist->append(image);
}

// used internally to keep track of the SoGLImages
void
SoGLImage::unregisterImage(SoGLImage *image)
{
  assert(glimage_reglist);
  int idx = glimage_reglist->find(image);
  assert(idx >= 0);
  if (idx >= 0) {
    glimage_reglist->removeFast(idx);
  }
}

#undef THIS
