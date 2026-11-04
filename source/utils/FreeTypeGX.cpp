/*
 * FreeTypeGX is a wrapper class for libFreeType which renders a compiled
 * FreeType parsable font into a GX texture for Wii homebrew development.
 * Copyright (C) 2008 Armin Tamzarian
 * Modified by Tantric, 2009
 *
 * This file is part of FreeTypeGX.
 *
 * FreeTypeGX is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FreeTypeGX is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FreeTypeGX.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "FreeTypeGX.h"
#include FT_OUTLINE_H

//#define ft_malloc malloc
//#define ft_free free
//#define ft_memalign memalign

#include "mem2_manager.h"
#define ft_malloc(x) mem2_malloc(x,MEM2_OTHER)
#define ft_free(x) mem2_free(x,MEM2_OTHER)
#define ft_memalign(x,y) mem2_memalign(x,y,MEM2_OTHER)

static FT_Library ftLibrary = NULL;     /**< FreeType FT_Library instance. */
static FT_Face ftFace = NULL;           /**< FreeType reusable FT_Face typographic object. */
static FT_GlyphSlot ftSlot = NULL;      /**< FreeType reusable FT_GlyphSlot glyph container object. */
static FT_Face ftFallback = NULL;       /**< Language font lending glyphs the menu font lacks, loaded on demand. */
static int ftFallbackFont = -1;         /**< Which language font ftFallback is (FALLBACK_*). */
static const wchar_t *fallbackText = NULL; /**< String the fallback fonts in fallbackTried were loaded for. */
static int fallbackTried = 0;           /**< Bit per FALLBACK_* font already loaded for fallbackText. */
static mutex_t glyphLock = LWP_MUTEX_NULL; /**< Glyphs are cached from both the menu and GUI threads. */
static volatile bool texturesChanged = false; /**< A glyph texture was written since the GPU's texture cache was last cleared. */

FreeTypeGX *fontSystem[MAX_FONT_SIZE+1];
FreeTypeGX *fontSystemBold[MAX_FONT_SIZE+1];

using namespace std;

// Standard operators
// overloaded new operator
void * operator new(size_t size)
{
    void *p = mem2_malloc(size,MEM2_OTHER);

    if (!p)
    {
        bad_alloc ba;
        throw ba;
    }
    return p;
}

// overloaded delete operator
void  operator delete(void *p) noexcept
{
    mem2_free(p,MEM2_OTHER);
}

// overloaded new operator for arrays
void * operator new[](size_t size)
{
    void *p = mem2_malloc(size,MEM2_OTHER);

    if (!p)
    {
        bad_alloc ba;
        throw ba;
    }
    return p;
}

// overloaded delete operator for arrays
void  operator delete[](void *p) noexcept
{
    mem2_free(p,MEM2_OTHER);
}

bool InitFreeType(uint8_t* fontBuffer, FT_Long bufferSize)
{
    FT_Init_FreeType(&ftLibrary);
    if(FT_New_Memory_Face(ftLibrary, (FT_Byte *)fontBuffer, bufferSize, 0, &ftFace) != 0)
        return false;

    if(!ftFace)
        return false;

    ftSlot = ftFace->glyph;

    if(!ftSlot)
        return false;

    for(int i=0; i<=MAX_FONT_SIZE; i++)
    {
        fontSystem[i] = NULL;
        fontSystemBold[i] = NULL;
    }

    return true;
}

/**
 * Closes the fallback font, before its memory is freed or reused.
 */
void DropFallbackFont()
{
    if(ftFallback)
        FT_Done_Face(ftFallback);
    ftFallback = NULL;
    ftFallbackFont = -1;
    fallbackText = NULL;
    fallbackTried = 0;
}

void DeinitFreeType()
{
    ClearFontData();
    DropFallbackFont();
    if(ftLibrary)
        FT_Done_FreeType(ftLibrary);
    ftLibrary = NULL;
}

void ChangeFontSize(FT_UInt pixelSize)
{
    FT_Set_Pixel_Sizes(ftFace, 0, pixelSize);
}

void ClearFontData()
{
    for(int i=0; i<=MAX_FONT_SIZE; i++)
    {
        if(fontSystem[i])
            delete fontSystem[i];
        fontSystem[i] = NULL;
        if(fontSystemBold[i])
            delete fontSystemBold[i];
        fontSystemBold[i] = NULL;
    }
}

/**
 * Convert a short char string to a wide char string.
 *
 * This routine converts a supplied shot character string into a wide character string.
 * Note that it is the user's responsibility to clear the returned buffer once it is no longer needed.
 *
 * @param strChar   Character string to be converted.
 * @return Wide character representation of supplied character string.
 */

wchar_t* charToWideChar(const char* strChar)
{
    wchar_t *strWChar = new wchar_t[strlen(strChar) + 1];
    if(!strWChar)
        return NULL;

    int bt = mbstowcs(strWChar, strChar, strlen(strChar));
    if (bt > 0)
    {
        strWChar[bt] = (wchar_t)'\0';
        return strWChar;
    }

    wchar_t *tempDest = strWChar;
    while((*tempDest++ = *strChar++));

    return strWChar;
}

/**
 * Default constructor for the FreeTypeGX class.
 *
 * @param vertexIndex   Optional vertex format index (GX_VTXFMT*) of the glyph textures as defined by the libogc gx.h header file. If not specified default value is GX_VTXFMT1.
 */
FreeTypeGX::FreeTypeGX(FT_UInt pixelSize, bool bold, uint8_t vertexIndex)
{
    this->setVertexFormat(vertexIndex);
    this->setCompatibilityMode(FTGX_COMPATIBILITY_DEFAULT_TEVOP_GX_PASSCLR | FTGX_COMPATIBILITY_DEFAULT_VTXDESC_GX_NONE);
    this->ftPointSize = pixelSize;
    this->bold = bold;
    this->currentText = NULL;
}
/**
 * Default destructor for the FreeTypeGX class.
 */
FreeTypeGX::~FreeTypeGX()
{
    this->unloadFont();
}

// overloaded new operator
void *FreeTypeGX::operator new(size_t size)
{
    void *p = ft_malloc(size);

    if (!p)
    {
        bad_alloc ba;
        throw ba;
    }
    return p;
}

// overloaded delete operator
void FreeTypeGX::operator delete(void *p)
{
    ft_free(p);
}

// overloaded new operator for arrays
void *FreeTypeGX::operator new[](size_t size)
{
    void *p = ft_malloc(size);

    if (!p)
    {
        bad_alloc ba;
        throw ba;
    }
    return p;
}

// overloaded delete operator for arrays
void FreeTypeGX::operator delete[](void *p)
{
    ft_free(p);
}



/**
 * Setup the vertex attribute formats for the glyph textures.
 *
 * This function sets up the vertex format for the glyph texture on the specified vertex format index.
 * Note that this function should not need to be called except if the vertex formats are cleared or the specified
 * vertex format index is modified.
 *
 * @param vertexIndex   Vertex format index (GX_VTXFMT*) of the glyph textures as defined by the libogc gx.h header file.
*/
void FreeTypeGX::setVertexFormat(uint8_t vertexIndex)
{
    this->vertexIndex = vertexIndex;
    GX_SetVtxAttrFmt(this->vertexIndex, GX_VA_POS, GX_POS_XY, GX_S16, 0);
    GX_SetVtxAttrFmt(this->vertexIndex, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetVtxAttrFmt(this->vertexIndex, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
}

/**
 * Sets the TEV and VTX rendering compatibility requirements for the class.
 *
 * This sets up the default TEV opertion and VTX descriptions rendering values for the class. This ensures that FreeTypeGX
 * can remain compatible with external liraries or project code. Certain external libraries or code by design or lack of
 * foresight assume that the TEV opertion and VTX descriptions values will remain constant or are always returned to a
 * certain value. This will enable compatibility with those libraries and any other code which cannot or will not be changed.
 *
 * @param compatibilityMode Compatibility descritor (FTGX_COMPATIBILITY_*) as defined in FreeTypeGX.h
*/
void FreeTypeGX::setCompatibilityMode(uint32_t compatibilityMode)
{
    this->compatibilityMode = compatibilityMode;
}

/**
 * Sets the TEV operation and VTX descriptor values after texture rendering it complete.
 *
 * This function calls the GX_SetTevOp and GX_SetVtxDesc functions with the compatibility parameters specified
 * in setCompatibilityMode.
 */
void FreeTypeGX::setDefaultMode()
{
    if(this->compatibilityMode)
    {
        switch(this->compatibilityMode & 0x00FF)
        {
            case FTGX_COMPATIBILITY_DEFAULT_TEVOP_GX_MODULATE:
                GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
                break;
            case FTGX_COMPATIBILITY_DEFAULT_TEVOP_GX_DECAL:
                GX_SetTevOp(GX_TEVSTAGE0, GX_DECAL);
                break;
            case FTGX_COMPATIBILITY_DEFAULT_TEVOP_GX_BLEND:
                GX_SetTevOp(GX_TEVSTAGE0, GX_BLEND);
                break;
            case FTGX_COMPATIBILITY_DEFAULT_TEVOP_GX_REPLACE:
                GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
                break;
            case FTGX_COMPATIBILITY_DEFAULT_TEVOP_GX_PASSCLR:
                GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
                break;
            default:
                break;
        }

        switch(this->compatibilityMode & 0xFF00)
        {
            case FTGX_COMPATIBILITY_DEFAULT_VTXDESC_GX_NONE:
                GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
                break;
            case FTGX_COMPATIBILITY_DEFAULT_VTXDESC_GX_DIRECT:
                GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
                break;
            case FTGX_COMPATIBILITY_DEFAULT_VTXDESC_GX_INDEX8:
                GX_SetVtxDesc(GX_VA_TEX0, GX_INDEX8);
                break;
            case FTGX_COMPATIBILITY_DEFAULT_VTXDESC_GX_INDEX16:
                GX_SetVtxDesc(GX_VA_TEX0, GX_INDEX16);
                break;
            default:
                break;
        }
    }
}

/**
 * Clears all loaded font glyph data.
 *
 * This routine clears all members of the font map structure and frees all allocated memory back to the system.
 */
void FreeTypeGX::unloadFont()
{
    if(this->fontData.size() == 0)
        return;
    for(std::unordered_map<wchar_t, ftgxCharData>::iterator i = this->fontData.begin(), iEnd = this->fontData.end(); i != iEnd; ++i)
        ft_free(i->second.glyphDataTexture);
    this->fontData.clear();
}

uint16_t FreeTypeGX::adjustTextureWidth(uint16_t textureWidth)
{
    uint16_t alignment = 4;
    return textureWidth % alignment == 0 ? textureWidth : alignment + textureWidth - (textureWidth % alignment);
}

uint16_t FreeTypeGX::adjustTextureHeight(uint16_t textureHeight)
{
    uint16_t alignment = 4;
    return textureHeight % alignment == 0 ? textureHeight : alignment + textureHeight - (textureHeight % alignment);
}

/**
 * Caches the given font glyph in the instance font texture buffer.
 *
 * This routine renders and stores the requested glyph's bitmap and relevant information into its own quickly addressible
 * structure within an instance-specific map.
 *
 * @param charCode  The requested glyph's character code.
 * @return A pointer to the allocated font structure.
 */
ftgxCharData *FreeTypeGX::cacheGlyphData(wchar_t charCode)
{
    FT_UInt gIndex;
    uint16_t textureWidth = 0, textureHeight = 0;
    ftgxCharData *glyphData = NULL;

    if(glyphLock == LWP_MUTEX_NULL)
        LWP_MutexInit(&glyphLock, false);
    LWP_MutexLock(glyphLock);

    FT_Face face = ftFace;
    gIndex = FT_Get_Char_Index( ftFace, charCode );
    if (gIndex == 0)
    {
        FT_Face fallback = this->fallbackFor(charCode);
        if (fallback)
        {
            face = fallback;
            gIndex = FT_Get_Char_Index(face, charCode);
            FT_Set_Pixel_Sizes(face, 0, this->ftPointSize);
        }
    }
    FT_GlyphSlot slot = face->glyph;

    if (!FT_Load_Glyph(face, gIndex, this->bold ? FT_LOAD_NO_BITMAP : FT_LOAD_DEFAULT | FT_LOAD_RENDER ))
    {
        if(this->bold && slot->format == FT_GLYPH_FORMAT_OUTLINE)
        {
            // a lighter touch than FreeType's own synthetic bold (em / 24)
            FT_Pos strength = FT_MulFix(face->units_per_EM, face->size->metrics.y_scale) / 40;
            FT_Outline_Embolden(&slot->outline, strength);
            slot->advance.x += strength;
            FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL);
        }
        if(slot->format == FT_GLYPH_FORMAT_BITMAP)
        {
            FT_Bitmap *glyphBitmap = &slot->bitmap;

            textureWidth = adjustTextureWidth(glyphBitmap->width);
            textureHeight = adjustTextureHeight(glyphBitmap->rows);

            glyphData = &(this->fontData[charCode] = (ftgxCharData){
                slot->bitmap_left,
                slot->advance.x >> 6,
                textureWidth,
                textureHeight,
                slot->bitmap_top,
                slot->bitmap_top,
                glyphBitmap->rows - slot->bitmap_top,
                NULL
            });
            this->loadGlyphData(glyphBitmap, glyphData);
        }
    }
    LWP_MutexUnlock(glyphLock);
    return glyphData;
}

static bool isHangul(wchar_t c)
{
    return (c >= 0x1100 && c <= 0x11FF) || (c >= 0x3130 && c <= 0x318F) || (c >= 0xAC00 && c <= 0xD7AF);
}

static bool isKana(wchar_t c)
{
    return (c >= 0x3040 && c <= 0x30FF) || (c >= 0x31F0 && c <= 0x31FF) || (c >= 0xFF66 && c <= 0xFF9F);
}

static bool isCJK(wchar_t c)
{
    return isHangul(c) || (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) ||
        (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFFEF);
}

/**
 * Finds a language font with a glyph the menu font lacks, loading one if needed.
 *
 * Only for CJK characters. The font follows the string being drawn: Hangul means
 * Korean, kana Japanese, and Han characters alone Chinese, Simplified first. Each font
 * is loaded at most once per string, so a character none of them has can't keep
 * reloading them.
 *
 * @param charCode  The character the menu font lacks.
 * @return A face with the glyph, or NULL.
 */
FT_Face FreeTypeGX::fallbackFor(wchar_t charCode)
{
    if (!isCJK(charCode))
        return NULL;

    int order[2] = { FALLBACK_ZH_CN, FALLBACK_ZH_TW };
    bool hangul = isHangul(charCode), kana = isKana(charCode);
    for (const wchar_t *t = this->currentText; t && *t; ++t)
    {
        hangul |= isHangul(*t);
        kana |= isKana(*t);
    }
    if (hangul)
        order[0] = order[1] = FALLBACK_KO;
    else if (kana)
        order[0] = order[1] = FALLBACK_JP;

    if (ftFallback && (ftFallbackFont == order[0] || ftFallbackFont == order[1]) &&
        FT_Get_Char_Index(ftFallback, charCode))
        return ftFallback;

    if (this->currentText != fallbackText)
    {
        fallbackText = this->currentText;
        fallbackTried = 0;
    }

    for (int i = 0; i < 2; ++i)
    {
        if (fallbackTried & (1 << order[i]))
            continue;

        int tried = fallbackTried | (1 << order[i]);
        DropFallbackFont(); // its memory is about to be reused
        fallbackText = this->currentText;
        fallbackTried = tried;

        u32 size = 0;
        u8 *buffer = LoadFallbackFont(order[i], &size);
        if (!buffer || FT_New_Memory_Face(ftLibrary, (FT_Byte *)buffer, size, 0, &ftFallback) != 0)
        {
            ftFallback = NULL;
            continue;
        }
        ftFallbackFont = order[i];
        if (FT_Get_Char_Index(ftFallback, charCode))
            return ftFallback;
    }
    return NULL;
}

/**
 * Returns a character's cached glyph data, caching it first if needed.
 *
 * @param charCode  The requested glyph's character code.
 * @return A pointer to the glyph data, or NULL if the glyph could not be loaded.
 */
ftgxCharData *FreeTypeGX::getGlyph(wchar_t charCode)
{
    std::unordered_map<wchar_t, ftgxCharData>::iterator i = this->fontData.find(charCode);
    if (i != this->fontData.end())
        return &i->second;
    return this->cacheGlyphData(charCode);
}

/**
 * Locates each character in this wrapper's configured font face and proccess them.
 *
 * This routine locates each character in the configured font face and renders the glyph's bitmap.
 * Each bitmap and relevant information is loaded into its own quickly addressible structure within an instance-specific map.
 */
uint16_t FreeTypeGX::cacheGlyphDataComplete()
{
    uint32_t i = 0;
    FT_UInt gIndex;
    FT_ULong charCode = FT_Get_First_Char( ftFace, &gIndex );
    while ( gIndex != 0 )
    {
        if(this->cacheGlyphData(charCode) != NULL)
            ++i;
        charCode = FT_Get_Next_Char( ftFace, charCode, &gIndex );
    }
    return (uint16_t)(i);
}

/**
 * Loads the rendered bitmap into the relevant structure's data buffer.
 *
 * This routine does a simple byte-wise copy of the glyph's rendered 8-bit grayscale bitmap into the structure's buffer,
 * as an I8 texture. GX samples I8 as the same value in all four channels, as the RGBA8 texture it replaces did.
 *
 * @param bmp   A pointer to the most recently rendered glyph's bitmap.
 * @param charData  A pointer to an allocated ftgxCharData structure whose data represent that of the last rendered glyph.
 */
void FreeTypeGX::loadGlyphData(FT_Bitmap *bmp, ftgxCharData *charData)
{
    int tilesWide = (charData->textureWidth + 7) >> 3; // I8 data comes in 8x4 tiles, even for a width of 4
    int length = (tilesWide << 3) * charData->textureHeight;

    uint8_t * glyphData = (uint8_t *) ft_memalign(32, length);
    if(!glyphData)
        return;

    memset(glyphData, 0x00, length);

    uint8_t *src = (uint8_t *)bmp->buffer;
    uint32_t offset;

    for (int imagePosY = 0; imagePosY < bmp->rows; ++imagePosY)
    {
        for (int imagePosX = 0; imagePosX < bmp->width; ++imagePosX)
        {
            // 8x4 tiles of 32 bytes, rows of 8 within a tile
            offset = (((imagePosY >> 2) * tilesWide + (imagePosX >> 3)) << 5) + ((imagePosY & 3) << 3) + (imagePosX & 7);
            glyphData[offset] = *src;
            ++src;
        }
    }
    DCFlushRange(glyphData, length);
    charData->glyphDataTexture = (uint32_t *) glyphData;
    texturesChanged = true; // the GUI thread clears the texture cache before its next glyph
}

/**
 * Determines the x offset of the rendered string.
 *
 * This routine calculates the x offset of the rendered string based off of a supplied positional format parameter.
 *
 * @param width Current pixel width of the string.
 * @param format    Positional format of the string.
 */
int16_t FreeTypeGX::getStyleOffsetWidth(uint16_t width, uint16_t format)
{
    if (format & FTGX_JUSTIFY_LEFT)
        return 0;
    else if (format & FTGX_JUSTIFY_CENTER)
        return -(width >> 1);
    else if (format & FTGX_JUSTIFY_RIGHT)
        return -width;
    return 0;
}

/**
 * Determines the y offset of the rendered string.
 *
 * This routine calculates the y offset of the rendered string based off of a supplied positional format parameter.
 *
 * @param offset    Current pixel offset data of the string.
 * @param format    Positional format of the string.
 */
int16_t FreeTypeGX::getStyleOffsetHeight(ftgxDataOffset *offset, uint16_t format)
{
    switch(format & FTGX_ALIGN_MASK)
    {
        case FTGX_ALIGN_TOP:
            return offset->ascender;

        default:
        case FTGX_ALIGN_MIDDLE:
            return  (offset->ascender + offset->descender + 1) >> 1;

        case FTGX_ALIGN_BOTTOM:
            return offset->descender;

        case FTGX_ALIGN_BASELINE:
            return 0;

        case FTGX_ALIGN_GLYPH_TOP:
            return offset->max;

        case FTGX_ALIGN_GLYPH_MIDDLE:
            return (offset->max + offset->min + 1) >> 1;

        case FTGX_ALIGN_GLYPH_BOTTOM:
            return offset->min;
    }
    return 0;
}

/**
 * Processes the supplied text string and prints the results at the specified coordinates.
 *
 * This routine processes each character of the supplied text string, loads the relevant preprocessed bitmap buffer,
 * a texture from said buffer, and loads the resultant texture into the EFB.
 *
 * @param x Screen X coordinate at which to output the text.
 * @param y Screen Y coordinate at which to output the text. Note that this value corresponds to the text string origin and not the top or bottom of the glyphs.
 * @param text  NULL terminated string to output.
 * @param color Optional color to apply to the text characters. If not specified default value is ftgxWhite: (GXColor){0xff, 0xff, 0xff, 0xff}
 * @param textStyle Flags which specify any styling which should be applied to the rendered string.
 * @return The number of characters printed.
 */
uint16_t FreeTypeGX::drawText(int16_t x, int16_t y, wchar_t *text, GXColor color, uint16_t textStyle)
{
    uint16_t x_pos = x, printed = 0;
    uint16_t x_offset = 0, y_offset = 0;
    GXTexObj glyphTexture;
    ftgxDataOffset offset;

    if(!text)
        return 0;
    this->currentText = text;

    if(textStyle & FTGX_JUSTIFY_MASK)
    {
        x_offset = this->getStyleOffsetWidth(this->getWidth(text), textStyle);
    }
    if(textStyle & FTGX_ALIGN_MASK)
    {
        this->getOffset(text, &offset);
        y_offset = this->getStyleOffsetHeight(&offset, textStyle);
    }

    int i = 0;
    while (text[i])
    {
        ftgxCharData* glyphData = this->getGlyph(text[i]);

        if (glyphData != NULL)
        {
            GX_InitTexObj(&glyphTexture, glyphData->glyphDataTexture, glyphData->textureWidth, glyphData->textureHeight, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
            this->copyTextureToFramebuffer(&glyphTexture, glyphData->textureWidth, glyphData->textureHeight, x_pos + glyphData->renderOffsetX + x_offset, y - glyphData->renderOffsetY + y_offset, color);

            x_pos += glyphData->glyphAdvanceX;
            ++printed;
        }
        ++i;
    }

    if(textStyle & FTGX_STYLE_MASK)
    {
        this->getOffset(text, &offset);
        this->drawTextFeature(x + x_offset, y + y_offset, this->getWidth(text), &offset, textStyle, color);
    }

    return printed;
}

/**
 * \overload
 */
uint16_t FreeTypeGX::drawText(int16_t x, int16_t y, wchar_t const *text, GXColor color, uint16_t textStyle)
{
    return this->drawText(x, y, (wchar_t *)text, color, textStyle);
}

void FreeTypeGX::drawTextFeature(int16_t x, int16_t y, uint16_t width, ftgxDataOffset *offsetData, uint16_t format, GXColor color)
{
    uint16_t featureHeight = this->ftPointSize >> 4 > 0 ? this->ftPointSize >> 4 : 1;

    if (format & FTGX_STYLE_UNDERLINE)
        this->copyFeatureToFramebuffer(width, featureHeight, x, y + 1, color);

    if (format & FTGX_STYLE_STRIKE)
        this->copyFeatureToFramebuffer(width, featureHeight, x, y - ((offsetData->max) >> 1), color);
}

/**
 * Processes the supplied string and return the width of the string in pixels.
 *
 * This routine processes each character of the supplied text string and calculates the width of the entire string.
 * Note that if precaching of the entire font set is not enabled any uncached glyph will be cached after the call to this function.
 *
 * @param text  NULL terminated string to calculate.
 * @return The width of the text string in pixels.
 */
uint16_t FreeTypeGX::getWidth(wchar_t *text)
{
    uint16_t strWidth = 0;
    this->currentText = text;

    int i = 0;
    while (text[i])
    {
        ftgxCharData* glyphData = this->getGlyph(text[i]);

        if (glyphData != NULL)
        {
            strWidth += glyphData->glyphAdvanceX;
        }
        ++i;
    }
    return strWidth;
}

/**
 *
 * \overload
 */
uint16_t FreeTypeGX::getWidth(wchar_t const *text)
{
    return this->getWidth((wchar_t *)text);
}

/**
 * Processes the supplied string and return the height of the string in pixels.
 *
 * This routine processes each character of the supplied text string and calculates the height of the entire string.
 * Note that if precaching of the entire font set is not enabled any uncached glyph will be cached after the call to this function.
 *
 * @param text  NULL terminated string to calculate.
 * @return The height of the text string in pixels.
 */
uint16_t FreeTypeGX::getHeight(wchar_t *text)
{
    ftgxDataOffset offset;
    this->getOffset(text, &offset);
    return offset.max - offset.min;
}

/**
 *
 * \overload
 */
uint16_t FreeTypeGX::getHeight(wchar_t const *text)
{
    return this->getHeight((wchar_t *)text);
}

/**
 * Get the maximum offset above and minimum offset below the font origin line.
 *
 * This function calculates the maximum pixel height above the font origin line and the minimum
 * pixel height below the font origin line and returns the values in an addressible structure.
 *
 * @param text  NULL terminated string to calculate.
 * @param offset returns the max and min values above and below the font origin line
 *
 */
void FreeTypeGX::getOffset(wchar_t *text, ftgxDataOffset* offset)
{
    int16_t strMax = 0, strMin = 9999;
    this->currentText = text;

    int i = 0;
    while (text[i])
    {
        ftgxCharData* glyphData = this->getGlyph(text[i]);

        if(glyphData != NULL)
        {
            strMax = glyphData->renderOffsetMax > strMax ? glyphData->renderOffsetMax : strMax;
            strMin = glyphData->renderOffsetMin < strMin ? glyphData->renderOffsetMin : strMin;
        }
        ++i;
    }
    offset->ascender = ftFace->size->metrics.ascender>>6;
    offset->descender = ftFace->size->metrics.descender>>6;
    offset->max = strMax;
    offset->min = strMin;
}

/**
 *
 * \overload
 */
void FreeTypeGX::getOffset(wchar_t const *text, ftgxDataOffset* offset)
{
    this->getOffset((wchar_t *)text, offset);
}

/**
 * Copies the supplied texture quad to the EFB.
 *
 * This routine uses the in-built GX quad builder functions to define the texture bounds and location on the EFB target.
 *
 * @param texObj    A pointer to the glyph's initialized texture object.
 * @param texWidth  The pixel width of the texture object.
 * @param texHeight The pixel height of the texture object.
 * @param screenX   The screen X coordinate at which to output the rendered texture.
 * @param screenY   The screen Y coordinate at which to output the rendered texture.
 * @param color Color to apply to the texture.
 */
void FreeTypeGX::copyTextureToFramebuffer(GXTexObj *texObj, f32 texWidth, f32 texHeight, int16_t screenX, int16_t screenY, GXColor color)
{
    GX_LoadTexObj(texObj, GX_TEXMAP0);
    if(texturesChanged)
    {
        texturesChanged = false;
        GX_InvalidateTexAll();
    }

    GX_SetTevOp (GX_TEVSTAGE0, GX_MODULATE);
    GX_SetVtxDesc (GX_VA_TEX0, GX_DIRECT);

    GX_Begin(GX_QUADS, this->vertexIndex, 4);
    GX_Position2s16(screenX, screenY);
    GX_Color4u8(color.r, color.g, color.b, color.a);
    GX_TexCoord2f32(0.0f, 0.0f);

    GX_Position2s16(texWidth + screenX, screenY);
    GX_Color4u8(color.r, color.g, color.b, color.a);
    GX_TexCoord2f32(1.0f, 0.0f);

    GX_Position2s16(texWidth + screenX, texHeight + screenY);
    GX_Color4u8(color.r, color.g, color.b, color.a);
    GX_TexCoord2f32(1.0f, 1.0f);

    GX_Position2s16(screenX, texHeight + screenY);
    GX_Color4u8(color.r, color.g, color.b, color.a);
    GX_TexCoord2f32(0.0f, 1.0f);
    GX_End();

    this->setDefaultMode();
}

/**
 * Creates a feature quad to the EFB.
 *
 * This function creates a simple quad for displaying underline or strikeout text styling.
 *
 * @param featureWidth  The pixel width of the quad.
 * @param featureHeight The pixel height of the quad.
 * @param screenX   The screen X coordinate at which to output the quad.
 * @param screenY   The screen Y coordinate at which to output the quad.
 * @param color Color to apply to the texture.
 */
void FreeTypeGX::copyFeatureToFramebuffer(f32 featureWidth, f32 featureHeight, int16_t screenX, int16_t screenY, GXColor color)
{
    GX_SetTevOp (GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetVtxDesc (GX_VA_TEX0, GX_NONE);

    GX_Begin(GX_QUADS, this->vertexIndex, 4);
    GX_Position2s16(screenX, screenY);
    GX_Color4u8(color.r, color.g, color.b, color.a);

    GX_Position2s16(featureWidth + screenX, screenY);
    GX_Color4u8(color.r, color.g, color.b, color.a);

    GX_Position2s16(featureWidth + screenX, featureHeight + screenY);
    GX_Color4u8(color.r, color.g, color.b, color.a);

    GX_Position2s16(screenX, featureHeight + screenY);
    GX_Color4u8(color.r, color.g, color.b, color.a);
    GX_End();

    this->setDefaultMode();
}
