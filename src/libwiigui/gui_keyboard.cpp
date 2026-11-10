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

static char tmptxt[MAX_KEYBOARD_DISPLAY+1];

static char * GetDisplayText(char * t)
{
    if(!t)
        return NULL;

    int len = strlen(t);

    if(len < MAX_KEYBOARD_DISPLAY)
        return t;

    strncpy(tmptxt, &t[len-MAX_KEYBOARD_DISPLAY], MAX_KEYBOARD_DISPLAY);
    tmptxt[MAX_KEYBOARD_DISPLAY] = 0;
    return &tmptxt[0];
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
    placeholder = NULL;
    selectable = true;
    focus = 1; // allow focus
    alignmentHor = ALIGN_CENTRE;
    alignmentVert = ALIGN_MIDDLE;
    strncpy(kbtextstr, t, max);
    kbtextstr[max] = 0;
    kbtextmaxlen = max;

    keyTextbox = new GuiImageData(keyboard_textbox_png);
    keyTextboxImg = new GuiImage(keyTextbox);
    keyTextboxImg->SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
    keyTextboxImg->SetPosition(0, 0);
    this->Append(keyTextboxImg);

    kbText = new GuiText(GetDisplayText(kbtextstr), 20, (GXColor){255, 255, 255, 0xff});
    kbText->SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
    // centre the font's ascent and descent on the box, not hang the text from
    // its top: Noto Sans has a tall ascent, which pushed g and j out the bottom
    kbText->SetStyle(FTGX_JUSTIFY_CENTER | FTGX_ALIGN_MIDDLE);
    kbText->SetPosition(0, 17);
    this->Append(kbText);

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
    add(keyMedium, keyMediumOver, "Caps", 0, 2, KB_KEYS + KB_CAPS);
    add(keyWide, keyWideOver, "Shift", 0, 3, KB_KEYS + KB_SHIFT);
    add(keyMedium, keyMediumOver, "Clear", 0, 4, KB_KEYS + KB_CLEAR);
    add(keyLarge, keyLargeOver, NULL, 209, 4, KB_KEYS + KB_SPACE);

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
    delete kbText;
    delete keyTextbox;
    delete keyTextboxImg;

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

// Shows the typed text, or the grey placeholder while there is none
void GuiKeyboard::ShowText()
{
    if(kbtextstr[0] == 0 && placeholder)
    {
        kbText->SetColor((GXColor){150, 150, 155, 0xff});
        kbText->SetText(placeholder);
    }
    else
    {
        kbText->SetColor((GXColor){255, 255, 255, 0xff});
        kbText->SetText(GetDisplayText(kbtextstr));
    }
}

void GuiKeyboard::SetPlaceholder(const char * p)
{
    placeholder = p;
    ShowText();
}

// Adds c to the end of the text, while there's room
void GuiKeyboard::Type(char c)
{
    u32 len = strlen(kbtextstr);

    if(len < kbtextmaxlen)
    {
        kbtextstr[len] = c;
        kbtextstr[len+1] = 0;
    }
    ShowText();
}

// Takes the last character off the text
void GuiKeyboard::Backspace()
{
    u32 len = strlen(kbtextstr);

    if(len > 0)
        kbtextstr[len-1] = 0;
    ShowText();
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
        else if(k == KB_KEYS + KB_CLEAR)
        {
            kbtextstr[0] = 0;
            ShowText();
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
