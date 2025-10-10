/*
 * Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "packer-utils.h"

#define LOG(utils, fmt, ...) do                       \
  {                                                   \
    dngpacker_utils_log (utils, __FILE__, __func__,   \
        __LINE__, fmt, ##__VA_ARGS__);                \
  }                                                   \
  while (0)

struct _DngPackerUtils {
  log_callback cb;
  void *cb_context;
};

typedef struct _DngPackSettings {
  DngPackerUtils *utils;

  uint16_t            *unpacked_buf;
  size_t              unpacked_size;
  uint32_t            raw_width;
  uint32_t            raw_height;
  uint32_t            bpp;
  uint32_t            stride;
  DngPackerCFAPattern cfa;

  uint8_t             *jpg_buf;
  size_t              jpg_size;
  uint32_t            jpg_width;
  uint32_t            jpg_height;
  uint32_t            jpg_samples_per_pixel;
} DngPackSettings;

error_callback g_error_callback = NULL;

static void
dngpacker_utils_log (DngPackerUtils *utils, const char * file,
                     const char * function, int line,
                     const char *fmt, ...)
{
  if (utils->cb) {
    va_list args;
    va_start (args, fmt);
    utils->cb (utils->cb_context, file, function, line, fmt, args);
    va_end (args);
  }
}

static void
dngpacker_utils_error (const char * fmt, ...)
{
  if (g_error_callback != NULL) {
    va_list args;
    va_start (args, fmt);
    g_error_callback (fmt, args);
    va_end (args);
  }
}

static void
dngpacker_utils_error_v (const char * fmt, va_list ap)
{
  if (g_error_callback != NULL) {
    g_error_callback (fmt, ap);
  }
}

static void
dngpacker_tiff_error_handler (const char* module, const char* fmt, va_list ap)
{
  dngpacker_utils_error ("TIFF Error in %s: \n", module);
  dngpacker_utils_error_v (fmt, ap);
  dngpacker_utils_error ("\n");
}

// structure for TIFFClientOpen Operation
typedef struct _MemTIFF {
  uint8_t *data;
  size_t size;
  size_t capacity;
  toff_t offset;
  DngPackerUtils *utils;
} MemTIFF;

static tmsize_t
mem_read (thandle_t fd, void *buf, tmsize_t size)
{
  MemTIFF *mt = (MemTIFF *) fd;

  if (mt == NULL || buf == NULL)
    return 0;

  if (mt->offset >= (toff_t) mt->size)
    return 0;

  if ((toff_t) size > (toff_t) mt->size - mt->offset)
    size = (tmsize_t) (mt->size - mt->offset);

  memcpy (buf, mt->data + mt->offset, (size_t) size);
  mt->offset += (toff_t) size;

  return size;
}

static tmsize_t
mem_write (thandle_t fd, void *buf, tmsize_t size)
{
  MemTIFF *mt = (MemTIFF *) fd;

  if (mt == NULL || buf == NULL)
    return 0;

  toff_t needed = mt->offset + (toff_t) size;
  if ((toff_t) mt->capacity < needed) {
    size_t newcap;
    uint8_t *newdata;

    newcap = (size_t) (needed * 2);
    if (newcap < 1024)
      newcap = 1024;

    newdata = (uint8_t *) realloc (mt->data, newcap);
    if (!newdata)
      return 0;

    mt->data = newdata;
    mt->capacity = newcap;
  }

  memcpy (mt->data + mt->offset, buf, (size_t) size);

  mt->offset += (toff_t) size;
  if (mt->offset > (toff_t) mt->size)
    mt->size = (size_t) mt->offset;

  return size;
}

static toff_t
mem_seek (thandle_t fd, toff_t off, int whence)
{
  MemTIFF *mt;
  toff_t newoff;

  mt = (MemTIFF *) fd;
  if (mt == NULL)
    return (toff_t) - 1;

  switch (whence) {
    case SEEK_SET:
      newoff = off;
      break;
    case SEEK_CUR:
      newoff = mt->offset + off;
      break;
    case SEEK_END:
      newoff = (toff_t) mt->size + off;
      break;
    default:
      return (toff_t) - 1;
  }

  if ((size_t) newoff > mt->capacity) {
    size_t newcap;
    uint8_t *newdata;

    newcap = (size_t) newoff * 2;
    newdata = (uint8_t *) realloc (mt->data, newcap);

    if (!newdata)
      return (toff_t) - 1;
    mt->data = newdata;
    mt->capacity = newcap;
  }

  mt->offset = newoff;
  if (mt->offset > (toff_t) mt->size)
    mt->size = (size_t) mt->offset;

  return mt->offset;
}

static int
mem_close (thandle_t fd)
{
  return 0;
}

static toff_t
mem_size (thandle_t fd)
{
  MemTIFF *mt = (MemTIFF *) fd;

  return mt ? (toff_t) mt->size : 0;
}

static int
mem_map (thandle_t fd, void **pbase, toff_t * psize)
{
  return 0;
}

static void
mem_unmap (thandle_t fd, void *base, toff_t size)
{
  return;
}

static int
unpack_packed_line_raw10_to_u16 (DngPackerUtils *utils, const uint8_t *src,
                                 size_t src_len, uint16_t *dst, uint32_t width)
{
  size_t pos = 0;
  uint32_t x = 0;
  uint32_t rem = 0;

  // MIPI CSI-2 Packed RAW10:
  // Byte 0 = P0[2:9]
  // Byte 1 = P1[2:9]
  // Byte 2 = P2[2:9]
  // Byte 3 = P3[2:9]
  // Byte 4 = P0[0:1] | P1[0:1] | P2[0:1] | P3[0:1]

  while (x + 4 <= width) {
    if (pos + 5 > src_len) {
      LOG (utils, "[ERROR] pos + 5 (%zu) > src_len (%zu)", pos + 5, src_len);

      return -1;
    }

    uint8_t b0 = src[pos + 0];
    uint8_t b1 = src[pos + 1];
    uint8_t b2 = src[pos + 2];
    uint8_t b3 = src[pos + 3];
    uint8_t b4 = src[pos + 4];

    dst[x + 0] = (uint16_t) (((uint16_t) b0 << 2) | ((b4 >> 0) & 0x03));
    dst[x + 1] = (uint16_t) (((uint16_t) b1 << 2) | ((b4 >> 2) & 0x03));
    dst[x + 2] = (uint16_t) (((uint16_t) b2 << 2) | ((b4 >> 4) & 0x03));
    dst[x + 3] = (uint16_t) (((uint16_t) b3 << 2) | ((b4 >> 6) & 0x03));

    x += 4;
    pos += 5;
  }

  rem = width - x;
  if (rem > 0) {
    // Remaining pattern (per MIPI RAW10):
    // r==1: b0, b4
    // r==2: b0, b1, b4
    // r==3: b0, b1, b2, b4
    if (rem == 1) {
      if (pos + 2 > src_len) {
        LOG (utils, "[ERROR] pos + 2 (%zu) > src_len (%zu)", pos + 2, src_len);

        return -1;
      }
      uint8_t b0 = src[pos + 0];
      uint8_t b4 = src[pos + 1];
      dst[x + 0] = (uint16_t) (((uint16_t) b0 << 2) | ((b4 >> 0) & 0x03));

      pos += 2;
    } else if (rem == 2) {
      if (pos + 3 > src_len) {
        LOG (utils, "[ERROR] pos + 3 (%zu) > src_len (%zu)", pos + 3, src_len);

        return -1;
      }
      uint8_t b0 = src[pos + 0];
      uint8_t b1 = src[pos + 1];
      uint8_t b4 = src[pos + 2];
      dst[x + 0] = (uint16_t) (((uint16_t) b0 << 2) | ((b4 >> 0) & 0x03));
      dst[x + 1] = (uint16_t) (((uint16_t) b1 << 2) | ((b4 >> 2) & 0x03));

      pos += 3;
    } else if (rem == 3) {
      if (pos + 4 > src_len) {
        LOG (utils, "[ERROR] pos + 4 (%zu) > src_len (%zu)", pos + 4, src_len);

        return -1;
      }
      uint8_t b0 = src[pos + 0];
      uint8_t b1 = src[pos + 1];
      uint8_t b2 = src[pos + 2];
      uint8_t b4 = src[pos + 3];

      dst[x + 0] = (uint16_t) (((uint16_t) b0 << 2) | ((b4 >> 0) & 0x03));
      dst[x + 1] = (uint16_t) (((uint16_t) b1 << 2) | ((b4 >> 2) & 0x03));
      dst[x + 2] = (uint16_t) (((uint16_t) b2 << 2) | ((b4 >> 4) & 0x03));
      pos += 4;
    }
  }

  return 0;
}

static int
unpack_packed_line_raw12_to_u16 (DngPackerUtils *utils, const uint8_t *src,
                                 size_t src_len, uint16_t *dst, uint32_t width)
{
  size_t pos = 0;
  uint32_t x = 0;

  // MIPI CSI-2 Packed RAW12:
  // Byte 0 = P0[4:11]
  // Byte 1 = P1[4:11]
  // Byte 2 = P0[0:3] | P1[0:3]

  while (x + 2 <= width) {

    if (pos + 3 > src_len) {
      LOG (utils, "[ERROR] pos + 3 (%zu) > src_len (%zu)", pos + 3, src_len);

      return -1;
    }

    uint8_t b0 = src[pos + 0];
    uint8_t b1 = src[pos + 1];
    uint8_t b2 = src[pos + 2];

    dst[x + 0] = (uint16_t) (((uint16_t) b0 << 4) | (b2 & 0x0F));
    dst[x + 1] = (uint16_t) (((uint16_t) b1 << 4) | ((b2 >> 4) & 0x0F));

    x += 2;
    pos += 3;
  }

  // 1 pixel remained
  if (x < width) {

    if (pos + 2 > src_len) {
      LOG (utils, "[ERROR] pos + 2 (%zu) > src_len (%zu)", pos + 2, src_len);

      return -1;
    }

    uint8_t b0 = src[pos + 0];
    uint8_t b2 = src[pos + 1];

    dst[x + 0] = (uint16_t) (((uint16_t) b0 << 4) | (b2 & 0x0F));

    pos += 2;
  }

  return 0;
}

static int
unpack_packed_line_raw8_to_u16 (DngPackerUtils *utils, const uint8_t *src,
                                size_t src_len, uint16_t *dst, uint32_t width)
{
  uint32_t x;

  if (src_len < (size_t)width) {
    LOG (utils, "[ERROR] src_len (%zu) < width(%zu)", src_len, (size_t) width);

    return -1;
  }

  for (x = 0; x < width; ++x)
    dst[x] = (uint16_t)src[x];

  return 0;
}

static int
unpack_packed_line_raw16_to_u16 (DngPackerUtils *utils, const uint8_t *src,
                                 size_t src_len, uint16_t *dst, uint32_t width)
{
  uint32_t x;

  size_t need = (size_t)width * 2;

  if (src_len < need) {
    LOG (utils, "[ERROR] src_len (%zu) < need (%zu)", src_len, need);
    return -1;
  }

  for (x = 0; x < width; ++x)
    dst[x] = (uint16_t)((src[2*x + 1] << 8) | (src[2*x + 0]));

  return 0;
}

static int
unpack_raw_to_u16 (DngPackerUtils *utils, uint16_t * unpacked_buf,
                   uint8_t * inbuf, uint32_t width, uint32_t height,
                   uint32_t bpp_bits, size_t line_bytes)
{
  uint8_t *linebuf;
  uint32_t y = 0;
  int rc = 0;

  for (y = 0; y < height; ++y) {
    uint16_t *dst = NULL;

    linebuf = (uint8_t *) (inbuf + line_bytes * y);
    dst = unpacked_buf + (size_t) y * (size_t) width;

    switch (bpp_bits) {
      case 8:
        rc = unpack_packed_line_raw8_to_u16(utils, linebuf, line_bytes, dst, width);
        break;
      case 10:
        rc = unpack_packed_line_raw10_to_u16(utils, linebuf, line_bytes, dst, width);
        break;
      case 12:
        rc = unpack_packed_line_raw12_to_u16(utils, linebuf, line_bytes, dst, width);
        break;
      case 16:
        rc = unpack_packed_line_raw16_to_u16(utils, linebuf, line_bytes, dst, width);
        break;
      default:
        rc = -1;
        break;
    }

    if (rc != 0) {
      return -1;
    }
  }

  return 0;
}

static int
dngpacker_utils_fetch_jpg_info (uint8_t *jpg_buf, size_t jpg_size,
    uint32_t *width, uint32_t *height, uint32_t *samples_per_pixel)
{
  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;

  cinfo.err = jpeg_std_error (&jerr);

  jpeg_create_decompress (&cinfo);
  jpeg_mem_src(&cinfo, jpg_buf, jpg_size);

  if (jpeg_read_header (&cinfo, TRUE) != JPEG_HEADER_OK) {
    jpeg_destroy_decompress (&cinfo);
    return -1;
  }

  *width = (uint32_t) cinfo.image_width;
  *height = (uint32_t) cinfo.image_height;
  *samples_per_pixel = (uint32_t) cinfo.num_components;

  jpeg_destroy_decompress (&cinfo);

  return 0;
}

static int
dngpacker_utils_do_dng_pack (DngPackSettings *settings,
    uint8_t ** ppoutput, size_t *poutlen)
{
  MemTIFF mt;
  TIFF *tif = NULL;
  uint16_t *unpacked_buf = NULL;
  uint16_t cfa_repeat[2] = { 2, 2 };
  uint8_t cfa_patterns[][4] = {
    // DNGPACKER_CFA_RGGB
    { 0, 1, 1, 2 },

    // DNGPACKER_CFA_BGGR
    { 2, 1, 1, 0 },

    // DNGPACKER_CFA_GBRG
    { 1, 2, 0, 1 },

    // DNGPACKER_CFA_GRBG
    { 1, 0, 2, 1 },
  };
  uint8_t *target_cfa_pattern = NULL;
  uint8_t dng_version[4] = { 1, 4, 0, 0 };
  uint8_t dng_backward_version[4] = { 1, 4, 0, 0 };
  float blacklevel = 0.0f, whitelevel = 65535.0f;
  float as_shot_neutral[3] = { 1.0f, 1.0f, 1.0f };

  memset (&mt, 0, sizeof (mt));

  // allocate enough space to avoid realloc
  mt.capacity = settings->unpacked_size + settings->jpg_size + TIFF_INFO_EXTRA_SIZE;
  mt.data = (uint8_t *) malloc (mt.capacity);

  LOG (settings->utils, "[DEBUG] Dng Pack Settings: "
      "raw(%dx%d) bpp(%d) stride(%d) unpacked_size(%zu) jpg_size(%zu) capacity = %zu",
      settings->raw_width,
      settings->raw_height,
      settings->bpp,
      settings->stride,
      settings->unpacked_size,
      settings->jpg_size,
      mt.capacity);

  if (mt.data == NULL) {
    LOG (settings->utils, "[ERROR] allocate MemTIFF data buffer failed");

    return -1;
  }

  // tiff open with client-defined memory operations
  tif = TIFFClientOpen ("MemDNG", "w", (thandle_t) &mt, mem_read, mem_write,
                        mem_seek, mem_close, mem_size, mem_map, mem_unmap);
  if (tif == NULL) {
    LOG (settings->utils, "[ERROR] TIFFClientOpen failed");

    goto free_mt_data;
  }

  // IFD0: Jpeg Thumbnail
  if (settings->jpg_buf) {
    toff_t subifd_offsets[1] = { 0 };

    // image type: 1 = thumbnail image
    TIFFSetField (tif, TIFFTAG_SUBFILETYPE, (uint32_t) FILETYPE_REDUCEDIMAGE);

    // image width and height
    TIFFSetField (tif, TIFFTAG_IMAGEWIDTH, (uint32_t) settings->jpg_width);
    TIFFSetField (tif, TIFFTAG_IMAGELENGTH, (uint32_t) settings->jpg_height);

    // jpeg image bit depth
    TIFFSetField (tif, TIFFTAG_BITSPERSAMPLE, 8);

    // sample number for each piexl
    TIFFSetField (tif, TIFFTAG_SAMPLESPERPIXEL, settings->jpg_samples_per_pixel);

    // compression mode
    TIFFSetField (tif, TIFFTAG_COMPRESSION, COMPRESSION_JPEG);

    // color space or pixel layour interpreter, use Ycrcb for JPEG
    TIFFSetField (tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_YCBCR);

    // image orientation, use default topleft
    TIFFSetField (tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);

    // image planar, use default value for tool compatibility
    TIFFSetField (tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);

    // image rows per-strip, set as image height
    TIFFSetField (tif, TIFFTAG_ROWSPERSTRIP, (uint32_t) settings->jpg_height);

    // write jpeg image buffer as one strip
    if (TIFFWriteRawStrip (tif, 0, settings->jpg_buf,
          (tsize_t) settings->jpg_size) == -1) {
      LOG (settings->utils, "[ERROR] TIFF Write Raw Strip for JPEG failed");

      goto close_tiff;
    }

    // add SubIFD0 config for RAW image
    if (!TIFFSetField (tif, TIFFTAG_SUBIFD, 1, subifd_offsets)) {
      LOG (settings->utils, "[ERROR] Set SubIFD count failed");
      goto close_tiff;
    }

    // complete main directory
    if (!TIFFWriteDirectory (tif)) {
      LOG (settings->utils, "[ERROR] TIFF Write Directory for JPEG failed");

      goto close_tiff;
    }
  }

  // SubIFD1: RAW Image

  // image type: 0 = main image
  TIFFSetField (tif, TIFFTAG_SUBFILETYPE, (uint32_t) 0);

  // image width and height
  TIFFSetField (tif, TIFFTAG_IMAGEWIDTH, (uint32_t) settings->raw_width);
  TIFFSetField (tif, TIFFTAG_IMAGELENGTH, (uint32_t) settings->raw_height);

  // sample number for each piexl, raw image use 1 sample
  TIFFSetField (tif, TIFFTAG_SAMPLESPERPIXEL, 1);

  // image bit depth, use 16 bit for unpacked raw image since we convert all
  // packed image to 16bit unpacked format
  TIFFSetField (tif, TIFFTAG_BITSPERSAMPLE, 16);

  // compression mode: none
  TIFFSetField (tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);

  // color space or pixel layour interpreter, here we only support CFA 
  TIFFSetField (tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_CFA);

  // image planar, use default config
  TIFFSetField (tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);

  // image orientation, use default value
  TIFFSetField (tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);

  // image pixel value's data type, use unsigned integer for RAW16 unpacked format
  TIFFSetField (tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);

  // raw image cfa repeat pattern, use 2 x 2
  TIFFSetField (tif, TIFFTAG_CFAREPEATPATTERNDIM, cfa_repeat);

  target_cfa_pattern = cfa_patterns[settings->cfa];
  // raw image cfa color pattern, like rggb, bggr etc.
#if TIFFLIB_VERSION >= 20201219
  TIFFSetField (tif, TIFFTAG_CFAPATTERN, 4, target_cfa_pattern);
#else
  TIFFSetField (tif, TIFFTAG_CFAPATTERN, target_cfa_pattern);
#endif

  // cfa layout, 1 = Rectangular, used for CFA pattern
  TIFFSetField (tif, TIFFTAG_CFALAYOUT, (uint32_t) 1);

  // vendor informatoin
  TIFFSetField (tif, TIFFTAG_MAKE, "QTI Camera");
  TIFFSetField (tif, TIFFTAG_MODEL, "QTI Sensor");
  TIFFSetField (tif, TIFFTAG_UNIQUECAMERAMODEL, "CFA Virtual");

  // DNG version
  TIFFSetField (tif, TIFFTAG_DNGVERSION, dng_version);
  TIFFSetField (tif, TIFFTAG_DNGBACKWARDVERSION, dng_backward_version);

  // image blacklevel and whitelevel
  TIFFSetField (tif, TIFFTAG_BLACKLEVEL, 1, &blacklevel);
  TIFFSetField (tif, TIFFTAG_WHITELEVEL, 1, &whitelevel);

  // white balance scaling factors for neutral rendering of the scene
  TIFFSetField (tif, TIFFTAG_ASSHOTNEUTRAL, 3, as_shot_neutral);

  unpacked_buf = settings->unpacked_buf;

  for (uint32_t row = 0; row < (uint32_t) settings->raw_height; row++) {
    // write each line data into tiff internal buffer
    if (TIFFWriteScanline (tif, &unpacked_buf[(size_t) row * settings->raw_width],
          row, 0) != 1) {
      LOG (settings->utils, "[ERROR] TIFF Write Scanline for RAW failed");

      goto close_tiff;
    }
  }

  // complete sub-directory
  if (!TIFFWriteDirectory (tif)) {
    LOG (settings->utils, "[ERROR] TIFF Write Directory for RAW failed");

    goto close_tiff;
  }

  // close tiff
  TIFFClose (tif);

  *ppoutput = mt.data;
  *poutlen = mt.size;

  LOG (settings->utils, "[DEBUG] DNG write done: (%zu bytes)", mt.size);

  return 0;

close_tiff:
  TIFFClose(tif);

free_mt_data:
  free (mt.data);

  return -1;
}

int dngpacker_utils_is_raw_valid (DngPackerUtils *utils, DngPackRequest *request)
{
  int rc = 0;
  size_t min_required_size = 0;

  switch (request->raw_bpp) {
    case 8:
      min_required_size = request->raw_width * request->raw_height;

      if (request->raw_size < min_required_size)
        rc = -1;

      break;
    case 10:
      min_required_size = (request->raw_width * request->raw_height) / 4 * 5;

      if (request->raw_size < min_required_size)
        rc = -1;

      break;
    case 12:
      min_required_size = (request->raw_width * request->raw_height) / 2 * 3;

      if (request->raw_size < min_required_size)
        rc = -1;

      break;
    case 16:
      min_required_size = (request->raw_width * request->raw_height) * 2;

      if (request->raw_size < min_required_size)
        rc = -1;

      break;
    default:
      rc = -1;
      break;
  }

  LOG (utils, "[DEBUG] raw buffer size (%zu) , min_required_size (%zu)",
      request->raw_size, min_required_size);

  return rc;
}

int dngpacker_utils_pack_dng (DngPackerUtils *utils, DngPackRequest *request)
{
  DngPackSettings settings;
  int rc;

  if (dngpacker_utils_is_raw_valid (utils, request) != 0) {
    LOG (utils, "[ERROR] raw buf invalid");

    return -1;
  }

  memset (&settings, 0, sizeof (settings));

  settings.unpacked_size =
    (size_t) request->raw_width * (size_t) request->raw_height * sizeof (uint16_t);
  settings.unpacked_buf = (uint16_t *) malloc (settings.unpacked_size);

  if (settings.unpacked_buf == NULL) {
    LOG (utils, "[ERROR] allocate dng unpack output buffer failed");

    return -1;
  }

  // step1: unpack raw image
  rc = unpack_raw_to_u16 (utils, settings.unpacked_buf, request->raw_buf,
                          request->raw_width, request->raw_height,
                          request->raw_bpp, request->raw_stride);

  if (rc != 0) {
    LOG (utils, "[ERROR] unpack raw packed image failed");

    goto free_unpacked_buf;
  }

  // step2: prepare information for dng packing
  settings.utils = utils;
  settings.raw_width = request->raw_width;
  settings.raw_height = request->raw_height;
  settings.bpp = request->raw_bpp;
  settings.stride = request->raw_stride;
  settings.cfa = request->cfa;

  if (request->jpg_buf != NULL) {
    settings.jpg_buf = request->jpg_buf;
    settings.jpg_size = request->jpg_size;

    rc = dngpacker_utils_fetch_jpg_info (request->jpg_buf,
                                         request->jpg_size,
                                         &settings.jpg_width,
                                         &settings.jpg_height,
                                         &settings.jpg_samples_per_pixel);

    LOG (utils, "[DEBUG] JPEG info: size(%zu) width(%d) height(%d) samples_per_pixel(%d)",
        request->jpg_size, settings.jpg_width,
        settings.jpg_height, settings.jpg_samples_per_pixel);

    if (rc != 0) {
      LOG (utils, "[ERROR] fetch jpeg information failed");

      goto free_unpacked_buf;
    }
  }

  // step3: put unpacked raw image and optional jpeg image into dng buffer
  rc = dngpacker_utils_do_dng_pack (&settings, &request->output,
                                    &request->output_size);

  if (rc != 0) {
    LOG (utils, "[ERROR] dng pack failed");

    goto free_unpacked_buf;
  }

  free (settings.unpacked_buf);
  return 0;

free_unpacked_buf:
  free (settings.unpacked_buf);

  return -1;
}

DngPackerUtils *
dngpacker_utils_init (log_callback cb_func, void *cb_context)
{
  DngPackerUtils *utils = NULL;

  utils = malloc (sizeof (*utils));

  if (utils != NULL) {
    utils->cb = cb_func;
    utils->cb_context = cb_context;
    TIFFSetErrorHandler(dngpacker_tiff_error_handler);
  }

  return utils;
}

void
dngpacker_utils_register_error_cb (error_callback cb)
{
  g_error_callback = cb;
}
