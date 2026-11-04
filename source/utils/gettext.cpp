#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <gctypes.h>
#include <unistd.h>
#include <zlib.h>

#include "gettext.h"
#include "filelist.h"
#include "settings.h"
#include "mem2_manager.h"


typedef struct _MSG
{
    u32 id;
    char* msgstr;
    struct _MSG *next;
} MSG;
static MSG *baseMSG = 0;

#define HASHWORDBITS 32

/* Defines the so called `hashpjw' function by P.J. Weinberger
 [see Aho/Sethi/Ullman, COMPILERS: Principles, Techniques and Tools,
 1986, 1987 Bell Telephone Laboratories, Inc.]  */
static inline u32 hash_string(const char *str_param)
{
    u32 hval, g;
    const char *str = str_param;

    /* Compute the hash value for the given string.  */
    hval = 0;
    while (*str != '\0')
    {
        hval <<= 4;
        hval += (u8) * str++;
        g = hval & ((u32) 0xf << (HASHWORDBITS - 4));
        if (g != 0)
        {
            hval ^= g >> (HASHWORDBITS - 8);
            hval ^= g;
        }
    }
    return hval;
}

/* Expand some escape sequences found in the argument string.  */
static char * expand_escape(const char *str)
{
    char *retval, *rp;
    const char *cp = str;

    retval = (char *) mem2_malloc(strlen(str) + 1, MEM2_OTHER);
    if (retval == NULL)
        return NULL;
    rp = retval;

    while (cp[0] != '\0' && cp[0] != '\\')
        *rp++ = *cp++;
    if (cp[0] == '\0')
        goto terminate;
    do
    {
        /* Here cp[0] == '\\'.  */
        switch (*++cp)
        {
        case '\"': /* " */
            *rp++ = '\"';
            ++cp;
            break;
        case 'a': /* alert */
            *rp++ = '\a';
            ++cp;
            break;
        case 'b': /* backspace */
            *rp++ = '\b';
            ++cp;
            break;
        case 'f': /* form feed */
            *rp++ = '\f';
            ++cp;
            break;
        case 'n': /* new line */
            *rp++ = '\n';
            ++cp;
            break;
        case 'r': /* carriage return */
            *rp++ = '\r';
            ++cp;
            break;
        case 't': /* horizontal tab */
            *rp++ = '\t';
            ++cp;
            break;
        case 'v': /* vertical tab */
            *rp++ = '\v';
            ++cp;
            break;
        case '\\':
            *rp = '\\';
            ++cp;
            break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        {
            int ch = *cp++ - '0';

            if (*cp >= '0' && *cp <= '7')
            {
                ch *= 8;
                ch += *cp++ - '0';

                if (*cp >= '0' && *cp <= '7')
                {
                    ch *= 8;
                    ch += *cp++ - '0';
                }
            }
            *rp = ch;
        }
            break;
        default:
            *rp = '\\';
            break;
        }

        while (cp[0] != '\0' && cp[0] != '\\')
            *rp++ = *cp++;
    } while (cp[0] != '\0');

    /* Terminate string.  */
    terminate: *rp = '\0';
    return retval;
}

static MSG *findMSG(u32 id)
{
    MSG *msg;
    for (msg = baseMSG; msg; msg = msg->next)
    {
        if (msg->id == id)
            return msg;
    }
    return NULL;
}

static MSG *setMSG(const char *msgid, const char *msgstr)
{
    u32 id = hash_string(msgid);
    MSG *msg = findMSG(id);
    if (!msg)
    {
        msg = (MSG *) mem2_malloc(sizeof(MSG),MEM2_OTHER);
        msg->id = id;
        msg->msgstr = NULL;
        msg->next = baseMSG;
        baseMSG = msg;
    }
    if (msg)
    {
        if (msgstr)
        {
            if (msg->msgstr)
                mem2_free(msg->msgstr, MEM2_OTHER);
            msg->msgstr = expand_escape(msgstr);
        }
        return msg;
    }
    return NULL;
}

static void gettextCleanUp(void)
{
    while (baseMSG)
    {
        MSG *nextMsg = baseMSG->next;
        mem2_free(baseMSG->msgstr,MEM2_OTHER);
        mem2_free(baseMSG,MEM2_OTHER);
        baseMSG = nextMsg;
    }
}

static char * memfgets(char * dst, int maxlen, char * src)
{
    if(!src || !dst || maxlen <= 0)
        return NULL;

    char * newline = strchr(src, '\n');

    if(newline == NULL)
        return NULL;

    memcpy(dst, src, (newline-src));
    dst[(newline-src)] = 0;
    return ++newline;
}

bool LoadLanguage()
{
    char line[350];
    char *lastID = NULL;

    const u8 *packed;
    u32 packedSize;

    switch(WiiSettings.language)
    {
        case LANG_JAPANESE: packed = jp_lang_gz; packedSize = jp_lang_gz_size; break;
        case LANG_ENGLISH: packed = en_lang_gz; packedSize = en_lang_gz_size; break;
        case LANG_GERMAN: packed = de_lang_gz; packedSize = de_lang_gz_size; break;
        case LANG_FRENCH: packed = fr_lang_gz; packedSize = fr_lang_gz_size; break;
        case LANG_SPANISH: packed = es_lang_gz; packedSize = es_lang_gz_size; break;
        case LANG_ITALIAN: packed = it_lang_gz; packedSize = it_lang_gz_size; break;
        case LANG_DUTCH: packed = nl_lang_gz; packedSize = nl_lang_gz_size; break;
        case LANG_SIMP_CHINESE: packed = zh_cn_lang_gz; packedSize = zh_cn_lang_gz_size; break;
        case LANG_TRAD_CHINESE: packed = zh_tw_lang_gz; packedSize = zh_tw_lang_gz_size; break;
        case LANG_KOREAN: packed = ko_lang_gz; packedSize = ko_lang_gz_size; break;
        default: return false;
    }

    // the files are gzipped, and gzip ends with the unpacked size
    u32 size = packed[packedSize-4] | packed[packedSize-3] << 8 | packed[packedSize-2] << 16 | packed[packedSize-1] << 24;
    char *text = (char *)mem2_malloc(size + 1, MEM2_OTHER);
    if(!text)
        return false;

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    zs.next_in = (Bytef *)packed;
    zs.avail_in = packedSize;
    zs.next_out = (Bytef *)text;
    zs.avail_out = size;
    bool unpacked = inflateInit2(&zs, 16 + MAX_WBITS) == Z_OK && inflate(&zs, Z_FINISH) == Z_STREAM_END;
    inflateEnd(&zs);
    if(!unpacked)
    {
        mem2_free(text, MEM2_OTHER);
        return false;
    }
    text[size] = 0;

    char *file = text, *eof = text + size;

    gettextCleanUp();

    while (file && file < eof)
    {
        file = memfgets(line, sizeof(line), file);

        if(!file)
            break;

        // lines starting with # are comments
        if (line[0] == '#')
            continue;

        if (strncmp(line, "msgid \"", 7) == 0)
        {
            char *msgid, *end;
            if (lastID)
            {
                free(lastID);
                lastID = NULL;
            }
            msgid = &line[7];
            end = strrchr(msgid, '"');
            if (end && end - msgid > 1)
            {
                *end = 0;
                lastID = strdup(msgid);
            }
        }
        else if (strncmp(line, "msgstr \"", 8) == 0)
        {
            char *msgstr, *end;

            if (lastID == NULL)
                continue;

            msgstr = &line[8];
            end = strrchr(msgstr, '"');
            if (end && end - msgstr > 1)
            {
                *end = 0;
                setMSG(lastID, msgstr);
            }
            free(lastID);
            lastID = NULL;
        }
    }
    if(lastID)
        free(lastID);
    mem2_free(text, MEM2_OTHER);
    return true;
}

const char *gettext(const char *msgid)
{
    MSG *msg = findMSG(hash_string(msgid));

    if (msg && msg->msgstr)
    {
        return msg->msgstr;
    }
    return msgid;
}
