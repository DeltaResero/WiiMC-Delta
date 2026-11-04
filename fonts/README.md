# Fonts

## Built in

`source/fonts/font.ttf` is compiled into the DOL and draws the menus in every
language that uses Latin letters.

- Font: Noto Sans Bold 2.015
  (https://github.com/notofonts/latin-greek-cyrillic)
- Licence: SIL Open Font License 1.1 (`OFL.txt`)
- Cut down to Basic Latin, Latin-1, Latin Extended-A, punctuation and
  currency signs, without hinting, which looks the same on screen and
  keeps the DOL 22 KB smaller:

      pyftsubset NotoSans-Bold.ttf --output-file=font.ttf \
          --unicodes=U+0020-007E,U+00A0-017F,U+0192,U+02C6-02C7,U+02D8-02DD,U+2000-206F,U+20A0-20CF,U+2122,U+2212,U+FFFD \
          --layout-features=kern --no-hinting --name-IDs='*' --name-languages='*'

## On the SD card

The other files here go in `apps/wiimc/` next to `boot.dol`. WiiMC loads them
when it needs them.

The language fonts are Noto Sans CJK Bold 2.004
(https://github.com/notofonts/noto-cjk), under the SIL Open Font License 1.1
(`OFL.txt`). Each is cut down to its region's standard character set, plus
Latin and punctuation, so file names and tags still show, and keeps no hinting.
WiiMC loads one into memory only while its language is chosen.

| File       | Face | Characters                               |
|------------|------|------------------------------------------|
| `jp.ttf`   | JP   | JIS X 0208 and all kana                  |
| `ko.ttf`   | KR   | KS X 1001 and all 11,172 Hangul          |

The cut is made from `NotoSansCJK-Bold.ttc` with `pyftsubset` and these options:
`--font-number=<face> --layout-features='' --no-layout-closure
--drop-tables+=GDEF,GPOS,GSUB,BASE,VORG,vhea,vmtx --no-hinting --notdef-outline
--name-IDs='*' --name-languages='*'`.
