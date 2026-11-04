# Fonts

## Built in

`source/fonts/font.ttf` is compiled into the DOL. It draws the menus in every
language that uses Latin letters, Greek and Cyrillic file names, and
subtitles unless `subfont.ttf` replaces it.

- Font: Noto Sans Bold 2.015
  (https://github.com/notofonts/latin-greek-cyrillic)
- Licence: SIL Open Font License 1.1 (`OFL.txt`)
- Cut down to Basic Latin, Latin-1, Latin Extended-A, Vietnamese, Greek,
  Cyrillic, punctuation and currency signs, without hinting, which looks the
  same on screen and keeps the DOL smaller:

      pyftsubset NotoSans-Bold.ttf --output-file=font.ttf \
          --unicodes=U+0020-007E,U+00A0-017F,U+0192,U+01A0-01A1,U+01AF-01B0,U+02C6-02C7,U+02D8-02DD,U+0370-03FF,U+0400-04FF,U+1EA0-1EF9,U+2000-206F,U+20A0-20CF,U+2122,U+2212,U+FFFD \
          --layout-features='' --drop-tables+=GDEF,GPOS,GSUB --no-hinting --notdef-outline \
          --name-IDs='*' --name-languages='*'

`source/fonts/mono.ttf` is compiled in too. Subtitles use it when the
Monospaced subtitle setting is on, which keeps closed captions lined up.

- Font: Noto Sans Mono Bold 2.014, same licence
- Cut down the same way to Basic Latin, Latin-1, Latin Extended-A,
  punctuation, currency signs and the other symbols closed captions use
  (`U+2120-2122`, `U+250C`, `U+2510`, `U+2514`, `U+2518`, `U+2588`).

Neither Noto font has a music note, which captions use for songs, so both
get ♪ and ♫ (`U+266A`, `U+266B`) copied in from Noto Sans Symbols Bold, also
under the SIL Open Font License; in `mono.ttf` they are centred in the
fixed width.

## On the SD card

The language fonts go in `apps/wiimc/` next to `boot.dol`. WiiMC loads them
when it needs them.

### Language fonts

The language fonts are Noto Sans CJK Bold 2.004
(https://github.com/notofonts/noto-cjk), under the SIL Open Font License 1.1
(`OFL.txt`). Each is cut down to its region's standard character set, plus
Latin and punctuation, so file names and tags still show, and keeps no hinting.
WiiMC loads one into memory only while its language is chosen, or, with a
menu in a Latin-script language, once a file name needs its characters: a
name with Hangul borrows from `ko.ttf`, one with kana from `jp.ttf`, and one
with Chinese characters alone from `zh_cn.ttf`, then `zh_tw.ttf`.

| File       | Face | Characters                               |
|------------|------|------------------------------------------|
| `jp.ttf`   | JP   | JIS X 0208 and all kana                  |
| `ko.ttf`   | KR   | KS X 1001 and all 11,172 Hangul          |
| `zh_cn.ttf`| SC   | GB2312                                   |
| `zh_tw.ttf`| TC   | Big5 symbols and level 1 (common) hanzi  |

The cut is made from `NotoSansCJK-Bold.ttc` with `pyftsubset` and these options:
`--font-number=<face> --layout-features='' --no-layout-closure
--drop-tables+=GDEF,GPOS,GSUB,BASE,VORG,vhea,vmtx --no-hinting --notdef-outline
--name-IDs='*' --name-languages='*'`.

None of the fonts keep kerning or vertical metrics. WiiMC only reads the
older `kern` table, which Noto doesn't have, and never draws vertical text.
All of them keep the box Noto draws for a missing character
(`--notdef-outline`), so a letter none of the fonts has shows as a box
rather than vanishing.

## Your own fonts

Any TrueType or OpenType font can take a file's place in `apps/wiimc/`:

| File            | Replaces                                                   |
|-----------------|------------------------------------------------------------|
| `def.ttf`       | The built-in font, for menus in the Latin-script languages |
| `subfont.ttf`   | The built-in font, for subtitles                           |
| `monospace.ttf` | The built-in monospaced font, for subtitles                |

For Japanese, Korean or Chinese subtitles, copy that language's font as
`subfont.ttf`. A `def.ttf` turns off borrowing glyphs for Japanese, Korean
and Chinese file names, since both use the same memory.
