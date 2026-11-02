/*
 * devkitPro's FreeType is built with WOFF2 (brotli) and bzip2 support. WiiMC
 * only loads plain TrueType fonts, so these stand in for the two decoders
 * FreeType calls and keep about 230 KB of them out of MEM1. A WOFF2 or
 * .pcf.bz2 font fails to load instead.
 *
 * Nothing else may use brotli or bzip2: FFmpeg is built without bzlib
 * (CONFIG_BZLIB 0). Remove the stubs if that changes.
 */

#include <bzlib.h>
#include <brotli/decode.h>

BrotliDecoderResult BrotliDecoderDecompress(size_t encoded_size,
	const uint8_t encoded_buffer[BROTLI_ARRAY_PARAM(encoded_size)], size_t *decoded_size,
	uint8_t decoded_buffer[BROTLI_ARRAY_PARAM(*decoded_size)])
{
	return BROTLI_DECODER_RESULT_ERROR;
}

int BZ2_bzDecompressInit(bz_stream *strm, int verbosity, int small)
{
	return BZ_CONFIG_ERROR;
}

int BZ2_bzDecompress(bz_stream *strm)
{
	return BZ_PARAM_ERROR;
}

int BZ2_bzDecompressEnd(bz_stream *strm)
{
	return BZ_OK;
}
