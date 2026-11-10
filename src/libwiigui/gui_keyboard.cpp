/****************************************************************************
 * libwiigui
 *
 * Tantric 2009-2012
 *
 * gui_keyboard.cpp
 *
 * GUI class definitions
 ***************************************************************************/

#include "gui.h"
#include "../input.h"

// how wide the text in the box can be
#define TEXT_WIDTH 360

// the start of the UTF-8 character after or before byte i of s
static u32 NextChar(const char * s, u32 i)
{
    if(s[i])
        for(i++; (s[i] & 0xC0) == 0x80; i++);
    return i;
}

static u32 PrevChar(const char * s, u32 i)
{
    if(i > 0)
        for(i--; i > 0 && (s[i] & 0xC0) == 0x80; i--);
    return i;
}

// The keys that type a character, row by row: each row's keys start at x and
// sit 42 pixels apart. The bottom row has two groups, either side of space.
static const struct
{
    const char *ch, *chShift;
    int row, x;
} keyRows[] = {
    { "`1234567890", "~!@#$%^&*()", 0, 0 },
    { "qwertyuiop", "QWERTYUIOP", 1, 61 },
    { "asdfghjkl;'", "ASDFGHJKL:\"", 2, 82 },
    { "zxcvbnm,./", "ZXCVBNM<>?", 3, 103 },
    { "[]\\", "{}|", 4, 82 },
    { "-=", "_+", 4, 416 }
};

/**
 * Makes key k at x on the given row, out of img and over, with label on it.
 */
GuiButton * GuiKeyboard::NewKey(GuiImageData * img, GuiImageData * over, const char * label, int x, int row, int k)
{
    keyImg[k] = new GuiImage(img);
    keyImgOver[k] = new GuiImage(over);
    keyTxt[k] = label ? new GuiText(label, 20, (GXColor){255, 255, 255, 0xff}) : NULL;
    keyBtn[k] = new GuiButton(img->GetWidth(), img->GetHeight());
    keyBtn[k]->SetImage(keyImg[k]);
    keyBtn[k]->SetImageOver(keyImgOver[k]);
    if(label)
        keyBtn[k]->SetLabel(keyTxt[k]);
    keyBtn[k]->SetTrigger(trigA);
    keyBtn[k]->SetPosition(x, row*42+80);
    keyBtn[k]->SetEffectGrow();
    return keyBtn[k];
}

/**
 * Constructor for the GuiKeyboard class.
 */

GuiKeyboard::GuiKeyboard(char * t, u32 max)
{
    width = 582;
    height = 400;
    shift = 0;
    caps = 0;
    backPressed = false;
    enterPressed = false;
    placeholder = NULL;
    selectable = true;
    focus = 1; // allow focus
    alignmentHor = ALIGN_CENTRE;
    alignmentVert = ALIGN_MIDDLE;

    KeyboardTyping(true); // a USB keyboard types here while this is open

    strncpy(kbtextstr, t, max);
    kbtextstr[max] = 0;
    kbtextmaxlen = max;
    cursor = strlen(kbtextstr);
    shown = 0;
    shownEnd = 0;
    shownLeft = 0;
    cursorMoved = gettime();
    textChanged = true;

    keyTextbox = new GuiImageData(keyboard_textbox_png);
    keyTextboxImg = new GuiImage(keyTextbox);
    keyTextboxImg->SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
    keyTextboxImg->SetPosition(0, 0);
    this->Append(keyTextboxImg);

    kbText = new GuiText(NULL, 20, (GXColor){255, 255, 255, 0xff});
    kbText->SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
    // centre the font's ascent and descent on the box, not hang the text from
    // its top: Noto Sans has a tall ascent, which pushed g and j out the bottom
    kbText->SetStyle(FTGX_JUSTIFY_CENTER | FTGX_ALIGN_MIDDLE);
    kbText->SetPosition(0, 17);
    this->Append(kbText);
    measureText = new GuiText(NULL, 20, (GXColor){255, 255, 255, 0xff});

    // a 2 pixel line, from a 4 pixel wide image (textures come in 4s)
    cursorImg = new GuiImage(4, 24, (GXColor){110, 180, 255, 0xff});
    cursorImg->SetScaleX(0.5);
    cursorImg->SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
    this->Append(cursorImg);

    key = new GuiImageData(keyboard_key_png);
    keyOver = new GuiImageData(keyboard_key_over_png);
    keyMedium = new GuiImageData(keyboard_mediumkey_png);
    keyMediumOver = new GuiImageData(keyboard_mediumkey_over_png);
    keyWide = new GuiImageData(keyboard_widekey_png);
    keyWideOver = new GuiImageData(keyboard_widekey_over_png);
    keyBackspace = new GuiImageData(keyboard_backspacekey_png);
    keyBackspaceOver = new GuiImageData(keyboard_backspacekey_over_png);
    keyLarge = new GuiImageData(keyboard_largekey_png);
    keyLargeOver = new GuiImageData(keyboard_largekey_over_png);
    keyArrow = new GuiImageData(keyboard_arrow_png);

    trigA = new GuiTrigger;
    trigA->SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A, CTR_BUTTON_A);

    GuiButton *rowKey[5][16];
    int rowX[5][16];
    int rowKeys[5] = { 0, 0, 0, 0, 0 };
    char txt[2] = { 0, 0 };
    int k = 0;

    auto add = [&](GuiImageData *img, GuiImageData *over, const char *label, int x, int i, int n)
    {
        rowX[i][rowKeys[i]] = x;
        rowKey[i][rowKeys[i]++] = NewKey(img, over, label, x, i, n);
    };

    add(keyBackspace, keyBackspaceOver, "Backspace", 462, 0, KB_KEYS + KB_BACKSPACE);
    add(keyWide, keyWideOver, "Delete", 482, 1, KB_KEYS + KB_DELETE);
    add(keyMedium, keyMediumOver, "Caps", 0, 2, KB_KEYS + KB_CAPS);
    add(keyWide, keyWideOver, "Shift", 0, 3, KB_KEYS + KB_SHIFT);
    add(keyMedium, keyMediumOver, "Clear", 0, 4, KB_KEYS + KB_CLEAR);
    add(keyLarge, keyLargeOver, NULL, 209, 4, KB_KEYS + KB_SPACE);
    add(key, keyOver, NULL, 500, 4, KB_KEYS + KB_LEFT);
    add(key, keyOver, NULL, 542, 4, KB_KEYS + KB_RIGHT);

    // the arrow keys show the one arrow image, the left one turned around
    for(int i=0; i < 2; i++)
    {
        arrowImg[i] = new GuiImage(keyArrow);
        arrowImg[i]->SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
        arrowImg[i]->SetAngle(i == 0 ? 180 : 0);
        keyBtn[KB_KEYS + KB_LEFT + i]->SetIcon(arrowImg[i]);
    }

    for(u32 r=0; r < sizeof(keyRows)/sizeof(keyRows[0]); r++)
    {
        int i = keyRows[r].row;

        for(int j=0; keyRows[r].ch[j]; j++, k++)
        {
            keys[k].ch = keyRows[r].ch[j];
            keys[k].chShift = keyRows[r].chShift[j];
            txt[0] = keys[k].ch;
            add(key, keyOver, txt, keyRows[r].x + j*42, i, k);
            keyTxt[k]->SetAlignment(ALIGN_CENTRE, ALIGN_BOTTOM);
            keyTxt[k]->SetPosition(0, -10);
        }
    }

    // each row from left to right, so the D-pad starts at the top left
    for(int i=0; i < 5; i++)
    {
        for(int j=0; j < rowKeys[i]; j++)
        {
            int first = j;

            for(int m=j+1; m < rowKeys[i]; m++)
                if(rowX[i][m] < rowX[i][first])
                    first = m;

            int x = rowX[i][j]; rowX[i][j] = rowX[i][first]; rowX[i][first] = x;
            GuiButton *b = rowKey[i][j]; rowKey[i][j] = rowKey[i][first]; rowKey[i][first] = b;
            this->Append(rowKey[i][j]);
        }
    }
}

/**
 * Destructor for the GuiKeyboard class.
 */
GuiKeyboard::~GuiKeyboard()
{
    KeyboardTyping(false);
    delete kbText;
    delete measureText;
    delete keyTextbox;
    delete keyTextboxImg;
    delete cursorImg;
    delete arrowImg[0];
    delete arrowImg[1];

    for(int k=0; k < KB_KEYS + KB_SPECIALS; k++)
    {
        delete keyImg[k];
        delete keyImgOver[k];
        delete keyTxt[k];
        delete keyBtn[k];
    }

    delete key;
    delete keyOver;
    delete keyMedium;
    delete keyMediumOver;
    delete keyWide;
    delete keyWideOver;
    delete keyBackspace;
    delete keyBackspaceOver;
    delete keyLarge;
    delete keyLargeOver;
    delete keyArrow;
    delete trigA;
}

// overloaded new operator
void *GuiKeyboard::operator new(size_t size)
{
    void *p = gui_malloc(size);

    if (!p)
    {
        bad_alloc ba;
        throw ba;
    }
    return p;
}

// overloaded delete operator
void GuiKeyboard::operator delete(void *p)
{
    gui_free(p);
}

// overloaded new operator for arrays
void *GuiKeyboard::operator new[](size_t size)
{
    void *p = gui_malloc(size);

    if (!p)
    {
        bad_alloc ba;
        throw ba;
    }
    return p;
}

// overloaded delete operator for arrays
void GuiKeyboard::operator delete[](void *p)
{
    gui_free(p);
}

// How wide bytes from to to of the text are on screen
int GuiKeyboard::TextWidth(u32 from, u32 to)
{
    char c = kbtextstr[to];

    kbtextstr[to] = 0;
    measureText->SetText(&kbtextstr[from]);
    kbtextstr[to] = c;
    return measureText->GetTextWidth();
}

// Shows the typed text with the cursor in it, or the grey placeholder while
// there is none. Text too long for the box scrolls to keep the cursor in it.
void GuiKeyboard::ShowText()
{
    u32 len = strlen(kbtextstr);
    int x;

    cursorMoved = gettime();

    if(len == 0 && placeholder)
    {
        kbText->SetColor((GXColor){150, 150, 155, 0xff});
        kbText->SetText(placeholder);
        shown = 0;
        shownEnd = 0;
        shownLeft = x = -kbText->GetTextWidth()/2;
    }
    else
    {
        // keep the box where it was while the cursor is in it, else start
        // it as far back from the cursor as fits. Then back it up while the
        // rest of the text fits, and show as much after the cursor as fits.
        if(cursor < shown)
            shown = cursor;
        else if(TextWidth(shown, cursor) > TEXT_WIDTH)
            for(shown = cursor; shown > 0 && TextWidth(PrevChar(kbtextstr, shown), cursor) <= TEXT_WIDTH; )
                shown = PrevChar(kbtextstr, shown);
        while(shown > 0 && TextWidth(PrevChar(kbtextstr, shown), len) <= TEXT_WIDTH)
            shown = PrevChar(kbtextstr, shown);

        u32 end = cursor;
        while(end < len && TextWidth(shown, NextChar(kbtextstr, end)) <= TEXT_WIDTH)
            end = NextChar(kbtextstr, end);

        char c = kbtextstr[end];
        kbtextstr[end] = 0;
        kbText->SetColor((GXColor){255, 255, 255, 0xff});
        kbText->SetText(&kbtextstr[shown]);
        kbtextstr[end] = c;
        shownEnd = end;
        shownLeft = -kbText->GetTextWidth()/2;
        x = shownLeft + TextWidth(shown, cursor);
    }
    cursorImg->SetPosition(x - 1, 8);
}

void GuiKeyboard::SetPlaceholder(const char * p)
{
    placeholder = p;
    textChanged = true;
}

// Moves the cursor to the gap between characters nearest x, which is measured
// from the keyboard's centre
void GuiKeyboard::PlaceCursor(int x)
{
    int best = -1;

    for(u32 i = shown; ; i = NextChar(kbtextstr, i))
    {
        int d = abs(shownLeft + TextWidth(shown, i) - x);

        if(best < 0 || d < best)
        {
            best = d;
            cursor = i;
        }
        if(i >= shownEnd)
            break;
    }
    textChanged = true;
}

// Adds c at the cursor, while there's room
void GuiKeyboard::Type(char c)
{
    u32 len = strlen(kbtextstr);

    if(len < kbtextmaxlen)
    {
        memmove(&kbtextstr[cursor+1], &kbtextstr[cursor], len - cursor + 1);
        kbtextstr[cursor++] = c;
    }
    textChanged = true;
}

// Takes away the character before the cursor
void GuiKeyboard::Backspace()
{
    u32 prev = PrevChar(kbtextstr, cursor);

    memmove(&kbtextstr[prev], &kbtextstr[cursor], strlen(&kbtextstr[cursor]) + 1);
    cursor = prev;
    textChanged = true;
}

// Takes away the character after the cursor
void GuiKeyboard::Delete()
{
    u32 next = NextChar(kbtextstr, cursor);

    memmove(&kbtextstr[cursor], &kbtextstr[next], strlen(&kbtextstr[next]) + 1);
    textChanged = true;
}

// Types what's pressed on a USB keyboard. Enter and Esc are for whoever
// opened the keyboard, to act on like OK and Cancel.
void GuiKeyboard::ReadUsbKeyboard()
{
    keyboard_event ev;

    while(KeyboardGetKey(&ev))
    {
        if(MOD_ONESET(ev.modifiers, MOD_ANYCONTROL | MOD_ANYMETA))
            continue;

        switch(ev.symbol)
        {
            case KS_BackSpace:
                Backspace();
                break;
            case KS_Delete:
            case KS_KP_Delete:
                Delete();
                break;
            case KS_Left:
            case KS_KP_Left:
                cursor = PrevChar(kbtextstr, cursor);
                textChanged = true;
                break;
            case KS_Right:
            case KS_KP_Right:
                cursor = NextChar(kbtextstr, cursor);
                textChanged = true;
                break;
            case KS_Home:
            case KS_KP_Home:
                cursor = 0;
                textChanged = true;
                break;
            case KS_End:
            case KS_KP_End:
                cursor = strlen(kbtextstr);
                textChanged = true;
                break;
            case KS_Return:
            case KS_KP_Enter:
                enterPressed = true;
                break;
            case KS_Escape:
                backPressed = true;
                break;
            default:
                if(ev.symbol >= 0x20 && ev.symbol < 0x7f)
                    Type(ev.symbol);
                break;
        }
    }
}

void GuiKeyboard::Update(GuiTrigger * t)
{
    if(_elements.size() == 0 || (state == STATE_DISABLED && parentElement))
        return;

    for (u8 i = 0; i < _elements.size(); i++)
    {
        try { _elements.at(i)->Update(t); }
        catch (const std::exception& e) { }
    }

    bool update = false;

    ReadUsbKeyboard();

    if((t->wpad->btns_d & (WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B)) ||
        (t->pad.btns_d & PAD_BUTTON_B) || (t->cpad.data.down & CTR_BUTTON_B))
    {
        if(strlen(kbtextstr) > 0)
            Backspace();
        else
            backPressed = true;
    }

    for(int k=0; k < KB_KEYS + KB_SPECIALS; k++)
    {
        if(keyBtn[k]->GetState() != STATE_CLICKED)
            continue;

        if(k < KB_KEYS)
        {
            Type((shift || caps) ? keys[k].chShift : keys[k].ch);

            if(shift)
            {
                shift = 0;
                update = true;
            }
        }
        else if(k == KB_KEYS + KB_SPACE)
        {
            Type(' ');
        }
        else if(k == KB_KEYS + KB_BACKSPACE)
        {
            Backspace();
        }
        else if(k == KB_KEYS + KB_DELETE)
        {
            Delete();
        }
        else if(k == KB_KEYS + KB_LEFT)
        {
            cursor = PrevChar(kbtextstr, cursor);
            textChanged = true;
        }
        else if(k == KB_KEYS + KB_RIGHT)
        {
            cursor = NextChar(kbtextstr, cursor);
            textChanged = true;
        }
        else if(k == KB_KEYS + KB_CLEAR)
        {
            kbtextstr[0] = 0;
            cursor = 0;
            textChanged = true;
        }
        else if(k == KB_KEYS + KB_SHIFT)
        {
            shift ^= 1;
            update = true;
        }
        else if(k == KB_KEYS + KB_CAPS)
        {
            caps ^= 1;
            update = true;
        }

        keyBtn[k]->SetState(STATE_SELECTED, t->chan);

        //don't reset pos (D-pad only: a pointer press keeps its own selection)
        if(focus && !t->wpad->ir.valid)
        {
            int selected = this->GetSelected();
            _elements.at(selected)->SetState(STATE_SELECTED);
        }
    }

    // pointing at the text and pressing A puts the cursor there
    if((t->wpad->btns_d & WPAD_BUTTON_A) && t->wpad->ir.valid &&
        keyTextboxImg->IsInside(t->wpad->ir.x, t->wpad->ir.y))
    {
        if(textChanged)
            ShowText();
        PlaceCursor(t->wpad->ir.x - (this->GetLeft() + this->GetWidth()/2));
    }

    if(textChanged)
    {
        textChanged = false;
        ShowText();
    }

    // the cursor blinks, but stays on for a moment after each change
    cursorImg->SetVisible(diff_usec(cursorMoved, gettime()) / 530000 % 2 == 0);

    if(update)
    {
        char txt[2] = { 0, 0 };

        for(int k=0; k < KB_KEYS; k++)
        {
            txt[0] = (shift || caps) ? keys[k].chShift : keys[k].ch;
            keyTxt[k]->SetText(txt);
        }
    }

    this->ToggleFocus(t);

    // pad/joystick navigation; the pointer owns the highlight while it's on screen
    if(focus && !t->wpad->ir.valid)
    {
        if(t->Right())
            this->MoveSelectionHor(1);
        else if(t->Left())
            this->MoveSelectionHor(-1);
        else if(t->Down())
            this->MoveSelectionVert(1);
        else if(t->Up())
            this->MoveSelectionVert(-1);
    }
}
