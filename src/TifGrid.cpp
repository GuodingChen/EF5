#include "TifGrid.h"
#include "Defines.h"
#include "Messages.h"
#include "geotiffio.h"
#include "xtiffio.h"
#include <cstdarg>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdlib.h>

#define TIFFTAG_GDAL_METADATA 42112
#define TIFFTAG_GDAL_NODATA 42113

static const TIFFFieldInfo xtiffFieldInfo[] = {
    {TIFFTAG_GDAL_METADATA, -1, -1, TIFF_ASCII, FIELD_CUSTOM, true, false,
     (char *)"GDALMetadata"},
    {TIFFTAG_GDAL_NODATA, -1, -1, TIFF_ASCII, FIELD_CUSTOM, true, false,
     (char *)"GDALNoDataValue"}};

static TIFFExtendProc TIFFParentExtender = NULL;
static void TIFFExtenderInit();
static void TIFFDefaultDirectory(TIFF *tif);

static void TifFatal(const char *file, const char *format, ...) {
  char reason[512];
  va_list args;
  va_start(args, format);
  vsnprintf(reason, sizeof(reason), format, args);
  va_end(args);
  ERROR_LOGF("Invalid GeoTIFF \"%s\": %s Model stopped.", file, reason);
  fflush(stdout);
  exit(EXIT_FAILURE);
}

static void TIFFExtenderInit() {
  static int first_time = 1;

  if (!first_time) {
    return; /* Been there. Done that. */
  }
  first_time = 0;

  /* Grab the inherited method and install */
  TIFFParentExtender = TIFFSetTagExtender(TIFFDefaultDirectory);

  TIFFSetErrorHandler(NULL);
}

static void TIFFDefaultDirectory(TIFF *tif) {
  /* Install the extended Tag field info */
  TIFFMergeFieldInfo(tif, xtiffFieldInfo,
                     sizeof(xtiffFieldInfo) / sizeof(xtiffFieldInfo[0]));

  /* Since an XTIFF client module may have overridden
   *      * the default directory method, we call it now to
   *           * allow it to set up the rest of its own methods.
   *                */

  if (TIFFParentExtender) {
    (*TIFFParentExtender)(tif);
  }
}

FloatGrid *ReadFloatTifGrid(const char *file) {
  return ReadFloatTifGrid(file, NULL);
}

FloatGrid *ReadFloatTifGrid(const char *file, FloatGrid *incGrid) {

  TIFFExtenderInit();

  FloatGrid *grid = incGrid;
  TIFF *tif = NULL;
  GTIF *gtif = NULL;

  errno = 0;
  tif = XTIFFOpen(file, "r");
  if (!tif) {
    if (errno == ENOENT) {
      return NULL; // Keep the existing missing-forcing behavior.
    }
    if (errno) {
      TifFatal(file, "cannot open as a TIFF (%s).", strerror(errno));
    }
    TifFatal(file, "file exists but cannot be opened as a TIFF.");
  }

  gtif = GTIFNew(tif);
  if (!gtif) {
    TifFatal(file, "GeoTIFF metadata cannot be read.");
  }

  unsigned short sampleFormat = 0, samplesPerPixel = 0, bitsPerSample = 0;
  unsigned short orientation = ORIENTATION_TOPLEFT;
  TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
  TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
  TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sampleFormat);
  TIFFGetFieldDefaulted(tif, TIFFTAG_ORIENTATION, &orientation);

  if (sampleFormat != SAMPLEFORMAT_IEEEFP || bitsPerSample != 32 ||
      samplesPerPixel != 1) {
    TifFatal(file,
             "expected one Float32 band (bands=%u, bits=%u, format=%u).",
             samplesPerPixel, bitsPerSample, sampleFormat);
  }

  if (TIFFIsTiled(tif)) {
    TifFatal(file, "tiled storage is unsupported; use stripped storage.");
  }

  if (orientation != ORIENTATION_TOPLEFT) {
    TifFatal(file, "unsupported orientation %u; expected top-left (1).",
             orientation);
  }

  unsigned short rasterType = RasterPixelIsArea;
  if (GTIFKeyGet(gtif, GTRasterTypeGeoKey, &rasterType, 0, 1) &&
      rasterType != RasterPixelIsArea) {
    TifFatal(file, "unsupported raster type %u; expected PixelIsArea (1).",
             rasterType);
  }

  unsigned short matrixSize = 0;
  double *matrix = NULL;
  if (TIFFGetField(tif, TIFFTAG_GEOTRANSMATRIX, &matrixSize, &matrix)) {
    TifFatal(file, "rotated or skewed rasters are unsupported.");
  }

  int width = 0, height = 0;
  if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width) ||
      !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height) || width <= 0 ||
      height <= 0) {
    TifFatal(file, "image width or height is missing or invalid.");
  }

  short tiepointsize = 0, pixscalesize = 0;
  double *tiepoints = NULL;
  double *pixscale = NULL;
  if (!TIFFGetField(tif, TIFFTAG_GEOTIEPOINTS, &tiepointsize, &tiepoints) ||
      tiepointsize < 6 || !tiepoints) {
    TifFatal(file, "ModelTiepointTag is missing or invalid.");
  }
  for (int i = 0; i < 5; i++) {
    if (!std::isfinite(tiepoints[i])) {
      TifFatal(file, "ModelTiepointTag contains NaN or infinity.");
    }
  }
  if (!TIFFGetField(tif, TIFFTAG_GEOPIXELSCALE, &pixscalesize, &pixscale) ||
      pixscalesize < 2 || !pixscale || !std::isfinite(pixscale[0]) ||
      !std::isfinite(pixscale[1]) || pixscale[0] <= 0.0 ||
      pixscale[1] <= 0.0) {
    TifFatal(file, "ModelPixelScaleTag is missing or invalid.");
  }
  const double scaleTolerance =
      std::fmax(pixscale[0], pixscale[1]) * 1.0e-9;
  if (std::fabs(pixscale[0] - pixscale[1]) > scaleTolerance) {
    TifFatal(file, "non-square pixels are unsupported (x=%g, y=%g).",
             pixscale[0], pixscale[1]);
  }

  if (!grid || grid->numCols != width || grid->numRows != height) {
    if (grid) {
      delete grid;
    }
    grid = new FloatGrid();
    grid->numCols = width;
    grid->numRows = height;
    grid->data = new float *[grid->numRows]();
    if (!grid->data) {
      WARNING_LOGF("TIF file %s too large (out of memory) with %li rows", file,
                   grid->numRows);
      delete grid;
      GTIFFree(gtif);
      XTIFFClose(tif);
      return NULL;
    }
    for (long i = 0; i < grid->numRows; i++) {
      grid->data[i] = new float[grid->numCols]();
      if (!grid->data[i]) {
        WARNING_LOGF("TIF file %s too large (out of memory) with %li columns",
                     file, grid->numCols);
        delete grid;
        GTIFFree(gtif);
        XTIFFClose(tif);
        return NULL;
      }
    }
  }

  char *noData = NULL;
  if (TIFFGetField(tif, TIFFTAG_GDAL_NODATA, &noData)) {
    char extra = 0;
    if (!noData || sscanf(noData, "%f %c", &grid->noData, &extra) != 1 ||
        !std::isfinite(grid->noData)) {
      TifFatal(file, "invalid GDAL_NODATA value.");
    }
  } else {
    grid->noData = std::numeric_limits<float>::quiet_NaN();
  }
  grid->cellSize = pixscale[0];
  grid->extent.top = tiepoints[4] + tiepoints[1] * pixscale[1];
  grid->extent.left = tiepoints[3] - tiepoints[0] * pixscale[0];
  grid->extent.bottom = grid->extent.top - pixscale[1] * float(height);
  grid->extent.right = grid->extent.left + pixscale[0] * float(width);

  GTIFKeyGet(gtif, GTModelTypeGeoKey, &grid->modelType, 0, 1);
  GTIFKeyGet(gtif, GeographicTypeGeoKey, &grid->geographicType, 0, 1);
  GTIFKeyGet(gtif, GeogGeodeticDatumGeoKey, &grid->geodeticDatum, 0, 1);
  grid->geoSet = true;

  for (long i = 0; i < grid->numRows; i++) {
    // A one-band TIFF always uses sample 0, for both CONTIG and SEPARATE.
    if (TIFFReadScanline(tif, grid->data[i], (unsigned int)i, 0) == -1) {
      TifFatal(file, "failed to read row %ld.", i);
    }
    for (long j = 0; j < grid->numCols; j++) {
      if (!std::isfinite(grid->data[i][j])) {
        TifFatal(file, "cell (%ld, %ld) is NaN or infinite.", j, i);
      }
    }
  }

  GTIFFree(gtif);
  XTIFFClose(tif);

  return grid;
}

void WriteFloatTifGrid(const char *file, FloatGrid *grid, const char *artist,
                       const char *datetime, const char *copyright) {

  TIFFExtenderInit();

  TIFF *tif = NULL;
  GTIF *gtif = NULL;

  tif = XTIFFOpen(file, "w");
  if (!tif) {
    return;
  }

  gtif = GTIFNew(tif);
  if (!gtif) {
    XTIFFClose(tif);
    return;
  }

  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 32);
  TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP);
  TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE);

  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, grid->numCols);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, grid->numRows);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
  char buf[100];
  sprintf(buf, "%f", grid->noData);
  TIFFSetField(tif, TIFFTAG_GDAL_NODATA, buf);
  sprintf(buf, "EF5 v%s", EF5_VERSION);
  TIFFSetField(tif, TIFFTAG_SOFTWARE, buf);
  if (artist) {
    TIFFSetField(tif, TIFFTAG_ARTIST, artist);
  }
  if (datetime) {
    TIFFSetField(tif, TIFFTAG_DATETIME, datetime);
  }
  if (copyright) {
    TIFFSetField(tif, TIFFTAG_COPYRIGHT, copyright);
  }

  double tiepoints[6];
  double pixscale[3];

  pixscale[0] = grid->cellSize;
  pixscale[1] = grid->cellSize;
  pixscale[2] = 0.0;
  tiepoints[0] = 0;
  tiepoints[1] = 0;
  tiepoints[2] = 0;
  tiepoints[5] = 0;
  tiepoints[4] = grid->extent.top;
  tiepoints[3] = grid->extent.left;
  // grid->extent.bottom = tiepoints[4]-(pixscale[1] * float(height));
  // grid->extent.right = tiepoints[3]+(pixscale[0] * float(width));*/

  TIFFSetField(tif, TIFFTAG_GEOTIEPOINTS, 6, tiepoints);
  TIFFSetField(tif, TIFFTAG_GEOPIXELSCALE, 3, pixscale);

  if (grid->geoSet) {
    GTIFKeySet(gtif, GTModelTypeGeoKey, TYPE_SHORT, 1, grid->modelType);
    GTIFKeySet(gtif, GTRasterTypeGeoKey, TYPE_SHORT, 1, RasterPixelIsArea);
    GTIFKeySet(gtif, GeographicTypeGeoKey, TYPE_SHORT, 1, grid->geographicType);
    GTIFKeySet(gtif, GeogGeodeticDatumGeoKey, TYPE_SHORT, 1,
               grid->geodeticDatum);
    GTIFKeySet(gtif, GeogAngularUnitsGeoKey, TYPE_SHORT, 1, Angular_Degree);
  } else {
    GTIFKeySet(gtif, GTModelTypeGeoKey, TYPE_SHORT, 1, ModelGeographic);
    GTIFKeySet(gtif, GTRasterTypeGeoKey, TYPE_SHORT, 1, RasterPixelIsArea);
    GTIFKeySet(gtif, GeographicTypeGeoKey, TYPE_SHORT, 1, GCS_WGS_84);
    GTIFKeySet(gtif, GeogGeodeticDatumGeoKey, TYPE_SHORT, 1, Datum_WGS84);
    GTIFKeySet(gtif, GeogAngularUnitsGeoKey, TYPE_SHORT, 1, Angular_Degree);
  }

  for (long i = 0; i < grid->numRows; i++) {
    if (TIFFWriteScanline(tif, grid->data[i], (unsigned int)i, 0) == -1) {
      printf("eek!\n");
    }
  }

  GTIFWriteKeys(gtif);
  GTIFFree(gtif);
  XTIFFClose(tif);
}

LongGrid *ReadLongTifGrid(const char *file) {

  LongGrid *grid = NULL;
  TIFF *tif = NULL;
  GTIF *gtif = NULL;

  TIFFSetErrorHandler(NULL);

  tif = XTIFFOpen(file, "r");
  if (!tif) {
    return NULL;
  }

  gtif = GTIFNew(tif);
  if (!gtif) {
    XTIFFClose(tif);
    return NULL;
  }

  unsigned short sampleFormat, samplesPerPixel, bitsPerSample;
  TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
  TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
  TIFFGetField(tif, TIFFTAG_SAMPLEFORMAT, &sampleFormat);

  if (sampleFormat != SAMPLEFORMAT_INT || bitsPerSample != 32 ||
      samplesPerPixel != 1) {
    WARNING_LOGF("%s is not a supported Int GeoTiff", file);
    GTIFFree(gtif);
    XTIFFClose(tif);
    return NULL;
  }

  int width, height;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);

  short tiepointsize, pixscalesize;
  double *tiepoints; //[6];
  double *pixscale;  //[3];
  TIFFGetField(tif, TIFFTAG_GEOTIEPOINTS, &tiepointsize, &tiepoints);
  TIFFGetField(tif, TIFFTAG_GEOPIXELSCALE, &pixscalesize, &pixscale);

  grid = new LongGrid();

  grid->numCols = width;
  grid->numRows = height;
  grid->cellSize = pixscale[0];
  grid->extent.top = tiepoints[4];
  grid->extent.left = tiepoints[3];
  grid->extent.bottom = tiepoints[4] - (pixscale[1] * float(height));
  grid->extent.right = tiepoints[3] + (pixscale[0] * float(width));

  grid->data = new long *[grid->numRows];
  for (long i = 0; i < grid->numRows; i++) {
    grid->data[i] = new long[grid->numCols];
    if (TIFFReadScanline(tif, grid->data[i], (unsigned int)i, 1) == -1) {
      printf("eek!\n");
    }
  }

  GTIFFree(gtif);
  XTIFFClose(tif);

  return grid;
}
