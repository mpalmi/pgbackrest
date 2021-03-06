/***********************************************************************************************************************************
Gzip Decompress

Decompress IO from the gzip format.
***********************************************************************************************************************************/
#ifndef COMMON_COMPRESS_GZIP_DECOMPRESS_H
#define COMMON_COMPRESS_GZIP_DECOMPRESS_H

#include "common/io/filter/filter.h"

/***********************************************************************************************************************************
Filter type constant
***********************************************************************************************************************************/
#define GZIP_DECOMPRESS_FILTER_TYPE                                 "gzipDecompress"
    STRING_DECLARE(GZIP_DECOMPRESS_FILTER_TYPE_STR);

/***********************************************************************************************************************************
Constructor
***********************************************************************************************************************************/
IoFilter *gzipDecompressNew(bool raw);
IoFilter *gzipDecompressNewVar(const VariantList *paramList);

#endif
