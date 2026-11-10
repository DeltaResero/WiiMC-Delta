/****************************************************************************
 * WiiMC
 *
 * youtube_menu.cpp
 *
 * The YouTube screen: a search bar that suggests searches while typing,
 * tiles for YouTube's topic pages, and results as cards with thumbnails, a
 * few to a page. The layout follows BrewTube's, drawn in WiiMC's style.
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>

#include "libwiigui/gui.h"
#include "menu.h"
#include "video.h"
#include "wiimc.h"
#include "filelist.h"
#include "youtube.h"
#include "youtube_menu.h"
#include "utils/http.h"

extern GuiWindow *mainWindow;

#define THREAD_SLEEP  100
#define LIST_MAX      60        // items kept per list
#define PAGE          3         // cards per page
#define LEVELS        3         // lists open at once, such as results and a playlist from them
#define THUMB_MAX     (96*1024) // a 320x180 thumbnail is about 12 KB; a playlist's art can be a 55 KB PNG
#define SUGGESTIONS   3
#define QUERY_MAX     100
#define TILES         6
#define CARD_W        580
#define CARD_H        96

typedef struct
{
    char title[160];      // shown above the cards
    char query[QUERY_MAX + 1]; // what was searched for, or "" for a page
    char next[1024];      // the token for the next page of a search
    bool riitube;         // the list came from RiiTube: YouTube's couldn't be had
    YtItem items[LIST_MAX];
    u8 *thumb[LIST_MAX];  // each thumbnail's JPEG, once fetched
    int thumbsize[LIST_MAX];
    bool thumbtried[LIST_MAX];
    int count;
    int first;            // the first card on screen
} YtList;

// YouTube's own pages, as its Explore menu lists them, less the paid ones
// and the games; Live waits until live streams can play. RiiTube has no
// such pages, so it's searched for the topic instead.
static const struct
{
    const char *name;
    const char *browseId;   // NULL: not yet
    const char *params;
    const char *topic;      // what to search RiiTube for
    const u8 *icon;
} tiles[TILES] = {
    { "Music", "UC-9-kyTW8ZkZNDHQJ6FgpwQ", NULL, "music", yt_icon_music_png },
    { "Gaming", "UCOpNcN46UbXVtpKMrmU4Abg", NULL, "gaming", yt_icon_gaming_png },
    { "Podcasts", "FEpodcasts_destination", "qgcCCAE=", "podcast", yt_icon_podcasts_png },
    { "News", "UCYfdidRxbB8Qhf0Nx7ioOYw", NULL, "news", yt_icon_news_png },
    { "Sports", "UCEgdi0XIXXZ-qJOFPf4JSKw", NULL, "sports", yt_icon_sports_png },
    { "Live", NULL, NULL, NULL, yt_icon_live_png },
};

// a search's filters, as YouTube's site has them; each button steps through
// its choices, the first being the default: videos only, else no filter.
// Kept between visits.
#define FILTERS 4
static const struct
{
    const char *name;
    int code; // for YtSearchParams
} filterOpts[FILTERS][5] = {
    { { "Videos", 1 }, { "Playlists", 3 }, { "All types", 0 } },
    { { "Any length", 0 }, { "Under 3 min", 1 }, { "3-20 min", 3 }, { "Over 20 min", 2 } },
    { { "Any time", 0 }, { "Today", 2 }, { "This week", 3 }, { "This month", 4 }, { "This year", 5 } },
    { { "Relevance", 0 }, { "Popularity", 3 } },
};
static int filterPick[FILTERS];

static int FilterChoices(int f)
{
    int n = 0;
    while (n < 5 && filterOpts[f][n].name)
        n++;
    return n;
}

// kept between visits, so the screen comes back as it was after a video
static YtList *lists[LEVELS];
static int depth = 0; // lists open; 0 is the home page
static char lastQuery[QUERY_MAX + 1];
static bool reopen = false;

/****************************************************************************
 * Worker thread: search suggestions, then thumbnails for the cards on screen
 ***************************************************************************/
static lwp_t worker = LWP_THREAD_NULL;
static mutex_t lock = LWP_MUTEX_NULL;
static volatile bool workerHalt;

static char suggestWant[QUERY_MAX + 1]; // what to suggest for next
static char suggestDone[QUERY_MAX + 1]; // what the last suggestions were for
static char suggestions[SUGGESTIONS][96];
static int suggestCount;
static volatile int suggestSerial;      // goes up with each new set

static YtList *thumbList;               // the list on screen, or NULL
static int thumbFirst;
static GuiImageData *thumbReady[PAGE];  // decoded, waiting for the screen
static int thumbReadyItem[PAGE];
static int thumbShownItem[PAGE];        // what each card shows now, or -1
static YtList *thumbShownList[PAGE];    // and from which list: item 0 of another is another picture

static void FreeList(YtList *l)
{
    if (!l)
        return;
    for (int i = 0; i < LIST_MAX; i++)
        free(l->thumb[i]);
    free(l);
}

// finds thumbnail work: fetch (returns true, item in *item) or decode in place
static bool ThumbWork(int *item)
{
    YtList *l = thumbList;
    if (!l)
        return false;

    for (int s = 0; s < PAGE; s++)
    {
        int i = thumbFirst + s;
        // a playlist shows a picture when it has one
        if (i >= l->count || (l->items[i].playlist && !l->items[i].thumb[0] && !l->items[i].image[0]))
            continue;

        if (!l->thumb[i])
        {
            if (l->thumbtried[i])
                continue;
            *item = i;
            return true;
        }

        if ((thumbShownItem[s] != i || thumbShownList[s] != l) && thumbReadyItem[s] != i)
        {
            GuiImageData *d = new GuiImageData(l->thumb[i], l->thumbsize[i], GX_TF_RGBA8);
            if (d->GetImage() == NULL)
            {
                delete d;
                free(l->thumb[i]);
                l->thumb[i] = NULL;
                continue;
            }
            delete thumbReady[s];
            thumbReady[s] = d;
            thumbReadyItem[s] = i;
        }
    }
    return false;
}

static void *WorkerThread(void *arg)
{
    while (!workerHalt)
    {
        char text[QUERY_MAX + 1], url[256];
        int item = -1;
        YtList *l;

        LWP_MutexLock(lock);
        bool suggest = strcmp(suggestWant, suggestDone) != 0;
        if (suggest)
            strcpy(text, suggestWant);
        bool fetch = !suggest && ThumbWork(&item);
        l = thumbList;
        if (fetch)
        {
            YtItem *it = &l->items[item];
            if (it->image[0])
                snprintf(url, sizeof(url), "%s", it->image);
            else
                snprintf(url, sizeof(url), "http://i.ytimg.com/vi/%s/mqdefault.jpg", it->thumb[0] ? it->thumb : it->id);
        }
        LWP_MutexUnlock(lock);

        if (suggest)
        {
            char out[SUGGESTIONS][96];
            int n = text[0] ? YouTubeSuggest(text, out, SUGGESTIONS) : 0;

            LWP_MutexLock(lock);
            memcpy(suggestions, out, sizeof(out));
            suggestCount = n;
            strcpy(suggestDone, text);
            suggestSerial = suggestSerial + 1;
            LWP_MutexUnlock(lock);
            continue;
        }

        if (fetch)
        {
            u8 *jpg = (u8 *)malloc(THUMB_MAX);
            int n = jpg ? http_fetch(url, NULL, NULL, (char *)jpg, THUMB_MAX) : 0;

            LWP_MutexLock(lock);
            if (thumbList == l && item < l->count)
            {
                l->thumbtried[item] = true;
                if (n > 0)
                {
                    l->thumb[item] = (u8 *)realloc(jpg, n);
                    l->thumbsize[item] = n;
                    jpg = NULL;
                }
            }
            LWP_MutexUnlock(lock);
            free(jpg);
            continue;
        }

        usleep(50000);
    }
    return NULL;
}

static void StartWorker()
{
    if (lock == LWP_MUTEX_NULL)
        LWP_MutexInit(&lock, false);

    workerHalt = false;
    suggestWant[0] = suggestDone[0] = 0;
    suggestCount = 0;
    for (int s = 0; s < PAGE; s++)
    {
        thumbReady[s] = NULL;
        thumbReadyItem[s] = thumbShownItem[s] = -1;
    }
    LWP_CreateThread(&worker, WorkerThread, NULL, NULL, 64*1024, 60);
}

static void StopWorker()
{
    workerHalt = true;
    if (worker != LWP_THREAD_NULL)
        LWP_JoinThread(worker, NULL);
    worker = LWP_THREAD_NULL;

    for (int s = 0; s < PAGE; s++)
    {
        delete thumbReady[s];
        thumbReady[s] = NULL;
    }
    thumbList = NULL;
}

/****************************************************************************
 * The search keyboard: WiiMC's keyboard, with suggestions above the keys
 ***************************************************************************/
static bool SearchKeyboard(char *query)
{
    int result = -1;

    GuiKeyboard keyboard(query, QUERY_MAX);
    keyboard.SetPlaceholder("Search YouTube...");

    GuiTrigger trigA;
    trigA.SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A, CTR_BUTTON_A);

    GuiImageData btnOutline(button_png);
    GuiImageData btnOutlineOver(button_over_png);

    GuiText okBtnTxt("Search", 20, (GXColor){255, 255, 255, 255});
    GuiImage okBtnImg(&btnOutline);
    GuiImage okBtnImgOver(&btnOutlineOver);
    GuiButton okBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
    okBtn.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
    okBtn.SetPosition(25, -25);
    okBtn.SetLabel(&okBtnTxt);
    okBtn.SetImage(&okBtnImg);
    okBtn.SetImageOver(&okBtnImgOver);
    okBtn.SetTrigger(&trigA);
    okBtn.SetEffectGrow();

    GuiText cancelBtnTxt("Cancel", 20, (GXColor){255, 255, 255, 255});
    GuiImage cancelBtnImg(&btnOutline);
    GuiImage cancelBtnImgOver(&btnOutlineOver);
    GuiButton cancelBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
    cancelBtn.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
    cancelBtn.SetPosition(-25, -25);
    cancelBtn.SetLabel(&cancelBtnTxt);
    cancelBtn.SetImage(&cancelBtnImg);
    cancelBtn.SetImageOver(&cancelBtnImgOver);
    cancelBtn.SetTrigger(&trigA);
    cancelBtn.SetEffectGrow();

    // one row of suggestions, in the gap between the text box and the keys
    GuiImageData chip(yt_chip_png);
    GuiImageData chipOver(yt_chip_over_png);
    GuiImage *chipImg[SUGGESTIONS], *chipImgOver[SUGGESTIONS];
    GuiText *chipTxt[SUGGESTIONS];
    GuiButton *chipBtn[SUGGESTIONS];
    char chipLabel[SUGGESTIONS][96];

    for (int i = 0; i < SUGGESTIONS; i++)
    {
        chipLabel[i][0] = 0;
        chipImg[i] = new GuiImage(&chip);
        chipImgOver[i] = new GuiImage(&chipOver);
        chipTxt[i] = new GuiText(NULL, 16, (GXColor){255, 255, 255, 255});
        chipTxt[i]->SetMaxWidth(chip.GetWidth() - 14);
        chipBtn[i] = new GuiButton(chip.GetWidth(), chip.GetHeight());
        chipBtn[i]->SetAlignment(ALIGN_LEFT, ALIGN_TOP);
        chipBtn[i]->SetPosition(i * (chip.GetWidth() + 3), 45);
        chipBtn[i]->SetImage(chipImg[i]);
        chipBtn[i]->SetImageOver(chipImgOver[i]);
        chipBtn[i]->SetLabel(chipTxt[i]);
        chipBtn[i]->SetTrigger(&trigA);
        chipBtn[i]->SetVisible(false);
        chipBtn[i]->SetState(STATE_DISABLED);
        keyboard.Append(chipBtn[i]);
    }

    keyboard.Append(&okBtn);
    keyboard.Append(&cancelBtn);

    // a small image scaled over the screen; a full-size one costs over 1 MB
    GuiImage dim(8, 8, (GXColor){0, 0, 0, 160});
    dim.SetScaleX(screenwidth / 8.0f);
    dim.SetScaleY(screenheight / 8.0f);

    SuspendGui();
    mainWindow->SetState(STATE_DISABLED);
    mainWindow->Append(&dim);
    mainWindow->Append(&keyboard);
    mainWindow->ChangeFocus(&keyboard);
    ResumeGui();

    char typed[sizeof(keyboard.kbtextstr)];
    snprintf(typed, sizeof(typed), "%s", keyboard.kbtextstr);
    u64 typedAt = gettime();
    int serial = -1;

    while (result == -1)
    {
        usleep(THREAD_SLEEP);

        if (ExitRequested)
            result = 0;
        else if (okBtn.GetState() == STATE_CLICKED)
            result = 1;
        else if (cancelBtn.GetState() == STATE_CLICKED || keyboard.backPressed)
            result = 0;

        for (int i = 0; i < SUGGESTIONS; i++)
        {
            if (chipBtn[i]->GetState() == STATE_CLICKED)
            {
                strcpy(keyboard.kbtextstr, chipLabel[i]);
                result = 1;
            }
        }

        // ask for suggestions once typing stops for a moment
        if (strcmp(typed, keyboard.kbtextstr) != 0)
        {
            snprintf(typed, sizeof(typed), "%s", keyboard.kbtextstr);
            typedAt = gettime();
        }
        else if (diff_usec(typedAt, gettime()) > 300000)
        {
            LWP_MutexLock(lock);
            if (strcmp(suggestWant, typed) != 0)
                strcpy(suggestWant, typed);
            LWP_MutexUnlock(lock);
        }

        if (serial != suggestSerial)
        {
            LWP_MutexLock(lock);
            serial = suggestSerial;
            // show them only while they still match what is typed
            int n = strcmp(suggestDone, keyboard.kbtextstr) == 0 ? suggestCount : 0;
            SuspendGui();
            for (int i = 0; i < SUGGESTIONS; i++)
            {
                if (i < n)
                {
                    memcpy(chipLabel[i], suggestions[i], sizeof(chipLabel[i]));
                    chipTxt[i]->SetText(chipLabel[i]);
                    chipBtn[i]->SetVisible(true);
                    // ResetState() leaves a disabled button disabled
                    chipBtn[i]->SetState(STATE_DEFAULT);
                }
                else
                {
                    chipBtn[i]->SetVisible(false);
                    chipBtn[i]->SetState(STATE_DISABLED);
                }
            }
            ResumeGui();
            LWP_MutexUnlock(lock);
        }
    }

    if (result == 1)
        snprintf(query, QUERY_MAX + 1, "%.*s", QUERY_MAX, keyboard.kbtextstr);

    LWP_MutexLock(lock);
    suggestWant[0] = 0;
    LWP_MutexUnlock(lock);

    SuspendGui();
    mainWindow->Remove(&keyboard);
    mainWindow->Remove(&dim);
    mainWindow->SetState(STATE_DEFAULT);
    ResumeGui();

    for (int i = 0; i < SUGGESTIONS; i++)
    {
        delete chipBtn[i];
        delete chipTxt[i];
        delete chipImg[i];
        delete chipImgOver[i];
    }
    return result == 1 && query[0] != 0;
}

/****************************************************************************
 * The screen
 ***************************************************************************/
typedef struct
{
    GuiWindow *w;
    // home
    GuiButton *search;
    GuiText *searchTxt;
    GuiButton *tile[TILES];
    GuiText *tileTxt[TILES];
    GuiImage *tileIconImg[TILES];
    GuiImageData *tileIcon[TILES];
    GuiButton *source;
    GuiText *sourceTxt;
    GuiButton *filter[FILTERS];
    GuiText *filterTxt[FILTERS];
    // results
    GuiText *header;
    GuiButton *card[PAGE];
    GuiImage *thumb[PAGE];
    GuiImage *thumbBox[PAGE];
    GuiText *title[PAGE];
    GuiText *channel[PAGE];
    GuiText *info[PAGE];
    GuiButton *prev, *next;
    GuiText *pageTxt;
    GuiImageData *shown[PAGE]; // the thumbnails on screen
} Screen;

static char infoText[PAGE][160]; // each card's "length . views . age" line

static void Enable(GuiButton *b, bool on)
{
    b->SetVisible(on);
    if (on)
    {
        if (b->GetState() == STATE_DISABLED)
            b->SetState(STATE_DEFAULT);
    }
    else
        b->SetState(STATE_DISABLED);
}

static int Pages(YtList *l)
{
    return (l->count + PAGE - 1) / PAGE + (l->next[0] ? 1 : 0);
}

// shows the home page or the list on top, at its current page
static void Show(Screen *sc)
{
    YtList *l = depth > 0 ? lists[depth - 1] : NULL;

    SuspendGui();

    // keep the browser underneath out of reach, but not the tabs at the
    // top, which leave the screen. Dialogs and the keyboard turn the whole
    // main window back on when they close.
    mainWindow->SetState(STATE_DEFAULT);
    for (u32 i = 0; i < mainWindow->GetSize(); i++)
    {
        GuiElement *e = mainWindow->GetGuiElementAt(i);
        if (e != sc->w && !IsMenuTab(e))
            e->SetState(STATE_DISABLED);
    }

    Enable(sc->search, !l);
    sc->searchTxt->SetText(lastQuery[0] ? lastQuery : "Search YouTube...");
    sc->searchTxt->SetColor(lastQuery[0] ? (GXColor){255, 255, 255, 255} : (GXColor){150, 150, 155, 255});
    bool rii = YouTubeUseRiiTube();
    for (int i = 0; i < TILES; i++)
        Enable(sc->tile[i], !l);
    Enable(sc->source, !l);

    // RiiTube's search can't be filtered
    for (int f = 0; f < FILTERS; f++)
    {
        Enable(sc->filter[f], !l && !rii);
        sc->filterTxt[f]->SetText(filterOpts[f][filterPick[f]].name);
        sc->filterTxt[f]->SetColor(filterPick[f] ? (GXColor){110, 180, 255, 255} : (GXColor){200, 200, 205, 255});
    }
    sc->sourceTxt->SetText(rii ? "Source: RiiTube" : "Source: YouTube");

    sc->header->SetVisible(l != NULL);
    if (l)
        sc->header->SetText(l->title);

    for (int s = 0; s < PAGE; s++)
    {
        int i = l ? l->first + s : -1;
        bool on = l && i < l->count;
        Enable(sc->card[s], on);
        sc->thumbBox[s]->SetVisible(on);

        if (on)
        {
            YtItem *it = &l->items[i];
            sc->title[s]->SetText(it->title);
            sc->channel[s]->SetText(it->channel);

            char *t = infoText[s];
            t[0] = 0;
            if (it->playlist)
                snprintf(t, 160, "%s", "Playlist");
            const char *parts[3] = { it->length, it->views, it->age };
            for (int p = 0; p < 3; p++)
            {
                if (!parts[p][0])
                    continue;
                size_t n = strlen(t);
                snprintf(t + n, 160 - n, "%s%s", n ? "  \xE2\x80\xA2  " : "", parts[p]);
            }
            sc->info[s]->SetText(t);
        }

        // free a thumbnail as soon as its card shows something else: the
        // menu's memory is tight, and 320x180 takes 230 KB
        if (thumbShownItem[s] != i || thumbShownList[s] != l)
        {
            sc->thumb[s]->SetVisible(false);
            sc->thumb[s]->SetImage((GuiImageData *)NULL);
            delete sc->shown[s];
            sc->shown[s] = NULL;
            thumbShownItem[s] = -1;
        }
    }

    if (l)
    {
        char page[32];
        snprintf(page, sizeof(page), "%d / %d", l->first / PAGE + 1, Pages(l));
        sc->pageTxt->SetText(page);
        Enable(sc->prev, l->first > 0);
        Enable(sc->next, l->first + PAGE < l->count || l->next[0]);
    }
    else
    {
        Enable(sc->prev, false);
        Enable(sc->next, false);
    }
    sc->pageTxt->SetVisible(l != NULL);

    ResumeGui();

    LWP_MutexLock(lock);
    if (thumbList != l || thumbFirst != (l ? l->first : 0))
    {
        // anything decoded for the old page is of no use now
        for (int s = 0; s < PAGE; s++)
        {
            delete thumbReady[s];
            thumbReady[s] = NULL;
            thumbReadyItem[s] = -1;
        }
    }
    thumbList = l;
    thumbFirst = l ? l->first : 0;
    LWP_MutexUnlock(lock);
}

// opens a new list on top: a search, a page, or a playlist
// a tile's page as RiiTube has it: a search for its topic
static void RiiTubeTitle(char *title, size_t size, const char *name)
{
    snprintf(title, size, "%s on RiiTube", name);
}

static void Open(Screen *sc, const char *query, const char *browseId, bool playlist, const char *title,
    const char *params = NULL, const char *topic = NULL)
{
    if (depth >= LEVELS)
        return;

    YtList *l = (YtList *)calloc(1, sizeof(YtList));
    if (!l)
        return;

    snprintf(l->title, sizeof(l->title), "%s", title);
    snprintf(l->query, sizeof(l->query), "%s", query ? query : "");

    // the screen set to RiiTube
    l->riitube = YouTubeUseRiiTube();

    ShowAction(!query ? "Loading..." : l->riitube ? "Searching RiiTube..." : "Searching YouTube...");
    if (query && l->riitube)
        l->count = RiiTubeSearch(query, "", l->items, LIST_MAX, l->next, sizeof(l->next));
    else if (query)
    {
        char params[32];
        YtSearchParams(filterOpts[3][filterPick[3]].code, filterOpts[2][filterPick[2]].code,
            filterOpts[0][filterPick[0]].code, filterOpts[1][filterPick[1]].code, params, sizeof(params));
        l->count = YouTubeSearch(query, "", params, l->items, LIST_MAX, l->next, sizeof(l->next));
    }
    else if (playlist)
        l->count = YouTubePlaylist(browseId, l->items, LIST_MAX);
    else
        l->count = YouTubeBrowse(browseId, params, l->items, LIST_MAX);
    CancelAction();

    // YouTube's list couldn't be had: RiiTube's search, for the query or
    // for the page's topic
    if (l->count == 0 && !l->riitube && (query || topic))
    {
        ShowAction("Trying RiiTube...");
        if (!query)
        {
            snprintf(l->query, sizeof(l->query), "%s", topic);
            RiiTubeTitle(l->title, sizeof(l->title), title);
        }
        l->count = RiiTubeSearch(l->query, "", l->items, LIST_MAX, l->next, sizeof(l->next));
        CancelAction();
        l->riitube = l->count > 0;
    }

    if (l->count == 0)
    {
        free(l);
        if (YouTubeUseRiiTube())
            ErrorPrompt(query ? "No results, or RiiTube couldn't be reached." : "RiiTube couldn't be reached.");
        else
            ErrorPrompt(query ? "No results, or YouTube and RiiTube couldn't be reached." :
                "YouTube and RiiTube couldn't be reached.");
        Show(sc);
        return;
    }

    LWP_MutexLock(lock);
    thumbList = NULL;
    lists[depth++] = l;
    LWP_MutexUnlock(lock);
    Show(sc);
}

// the next page of a search, added to the end of its list
static void More(YtList *l)
{
    if (!l->next[0] || l->count >= LIST_MAX)
        return;

    char next[sizeof(l->next)];
    strcpy(next, l->next);

    ShowAction("Loading...");
    YtItem *more = (YtItem *)malloc(sizeof(YtItem) * (LIST_MAX - l->count));
    int n = !more ? 0 : l->riitube ? RiiTubeSearch(l->query, next, more, LIST_MAX - l->count, next, sizeof(next)) :
        YouTubeSearch(l->query, next, "", more, LIST_MAX - l->count, next, sizeof(next));
    CancelAction();

    LWP_MutexLock(lock);
    for (int i = 0; i < n; i++)
    {
        // a later page can repeat a video
        bool dup = false;
        for (int j = 0; j < l->count && !dup; j++)
            dup = strcmp(l->items[j].id, more[i].id) == 0;
        if (!dup && l->count < LIST_MAX)
            l->items[l->count++] = more[i];
    }
    if (n > 0)
        strcpy(l->next, next);
    else
        l->next[0] = 0;
    LWP_MutexUnlock(lock);
    free(more);
}

static void Close()
{
    if (depth == 0)
        return;

    LWP_MutexLock(lock);
    thumbList = NULL;
    FreeList(lists[--depth]);
    lists[depth] = NULL;
    LWP_MutexUnlock(lock);
}

bool YouTubeScreenReopen()
{
    bool r = reopen;
    reopen = false;
    return r;
}

bool YouTubeScreen(char *url, size_t urlsize, char *title, size_t titlesize)
{
    int x0 = (screenwidth - CARD_W) / 2;
    bool picked = false;
    Screen sc;
    memset(&sc, 0, sizeof(sc));

    GuiTrigger trigA;
    trigA.SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A, CTR_BUTTON_A);
    GuiTrigger trigB;
    trigB.SetButtonOnlyTrigger(-1, WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, CTR_BUTTON_B);

    GuiImageData bar(yt_bar_png), barOver(yt_bar_over_png);
    GuiImageData tile(yt_tile_png), tileOver(yt_tile_over_png);
    GuiImageData card(yt_card_png), cardOver(yt_card_over_png);
    GuiImageData button(yt_button_png), buttonOver(yt_button_over_png);
    GuiImageData searchIcon(yt_icon_search_png);
    GuiImageData **tileIcon = sc.tileIcon;

    GuiWindow w(screenwidth, screenheight);
    sc.w = &w;

    // B: back one list, or out of the screen from the home page
    GuiButton backBtn(0, 0);
    backBtn.SetTrigger(&trigB);
    backBtn.SetSelectable(false);
    w.Append(&backBtn);

    // home: the search bar and the tiles
    GuiImage searchImg(&bar), searchImgOver(&barOver), searchIconImg(&searchIcon);
    searchIconImg.SetAlignment(ALIGN_LEFT, ALIGN_MIDDLE);
    searchIconImg.SetPosition(12, 0);
    sc.searchTxt = new GuiText(NULL, 20, (GXColor){150, 150, 155, 255});
    sc.searchTxt->SetAlignment(ALIGN_LEFT, ALIGN_MIDDLE);
    sc.searchTxt->SetPosition(56, 0);
    sc.searchTxt->SetMaxWidth(bar.GetWidth() - 70);
    sc.search = new GuiButton(bar.GetWidth(), bar.GetHeight());
    sc.search->SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    sc.search->SetPosition(x0, 100);
    sc.search->SetImage(&searchImg);
    sc.search->SetImageOver(&searchImgOver);
    sc.search->SetIcon(&searchIconImg);
    sc.search->SetLabel(sc.searchTxt);
    sc.search->SetTrigger(&trigA);
    w.Append(sc.search);

    GuiImage *tileImg[TILES], *tileImgOver[TILES], **tileIconImg = sc.tileIconImg;
    GuiText **tileTxt = sc.tileTxt;
    for (int i = 0; i < TILES; i++)
    {
        tileIcon[i] = new GuiImageData(tiles[i].icon);
        tileImg[i] = new GuiImage(&tile);
        tileImgOver[i] = new GuiImage(&tileOver);
        tileIconImg[i] = new GuiImage(tileIcon[i]);
        tileIconImg[i]->SetAlignment(ALIGN_LEFT, ALIGN_MIDDLE);
        tileIconImg[i]->SetPosition(18, 0);
        tileTxt[i] = new GuiText(tiles[i].name, 20, (GXColor){255, 255, 255, 255});
        tileTxt[i]->SetAlignment(ALIGN_LEFT, ALIGN_MIDDLE);
        tileTxt[i]->SetPosition(66, 0);
        tileTxt[i]->SetMaxWidth(tile.GetWidth() - 76);
        sc.tile[i] = new GuiButton(tile.GetWidth(), tile.GetHeight());
        sc.tile[i]->SetAlignment(ALIGN_LEFT, ALIGN_TOP);
        sc.tile[i]->SetPosition(x0 + (i % 2) * (CARD_W - tile.GetWidth()), 162 + (i / 2) * (tile.GetHeight() + 12));
        sc.tile[i]->SetImage(tileImg[i]);
        sc.tile[i]->SetImageOver(tileImgOver[i]);
        sc.tile[i]->SetIcon(tileIconImg[i]);
        sc.tile[i]->SetLabel(tileTxt[i]);
        sc.tile[i]->SetTrigger(&trigA);
        w.Append(sc.tile[i]);
    }

    // where lists and videos come from, first in the row under the tiles
    int rowY = 162 + 3 * (tile.GetHeight() + 12);
    GuiImageData *sourceData = new GuiImageData(yt_source_png);
    GuiImageData *sourceDataOver = new GuiImageData(yt_source_over_png);
    sc.sourceTxt = new GuiText(NULL, 15, (GXColor){200, 200, 205, 255});
    sc.sourceTxt->SetMaxWidth(sourceData->GetWidth() - 8);
    sc.source = new GuiButton(sourceData->GetWidth(), sourceData->GetHeight());
    sc.source->SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    sc.source->SetPosition(x0, rowY);
    GuiImage *sourceImg = new GuiImage(sourceData), *sourceImgOver = new GuiImage(sourceDataOver);
    sc.source->SetImage(sourceImg);
    sc.source->SetImageOver(sourceImgOver);
    sc.source->SetLabel(sc.sourceTxt);
    sc.source->SetTrigger(&trigA);
    w.Append(sc.source);

    // the search's filters, after it
    GuiImageData *filterData = new GuiImageData(yt_filter_png);
    GuiImageData *filterDataOver = new GuiImageData(yt_filter_over_png);
    GuiImage *filterImg[FILTERS], *filterImgOver[FILTERS];
    for (int f = 0; f < FILTERS; f++)
    {
        filterImg[f] = new GuiImage(filterData);
        filterImgOver[f] = new GuiImage(filterDataOver);
        sc.filterTxt[f] = new GuiText(NULL, 15, (GXColor){200, 200, 205, 255});
        sc.filterTxt[f]->SetMaxWidth(filterData->GetWidth() - 8);
        sc.filter[f] = new GuiButton(filterData->GetWidth(), filterData->GetHeight());
        sc.filter[f]->SetAlignment(ALIGN_LEFT, ALIGN_TOP);
        sc.filter[f]->SetPosition(x0 + sourceData->GetWidth() + 5 + f * (filterData->GetWidth() + 5), rowY);
        sc.filter[f]->SetImage(filterImg[f]);
        sc.filter[f]->SetImageOver(filterImgOver[f]);
        sc.filter[f]->SetLabel(sc.filterTxt[f]);
        sc.filter[f]->SetTrigger(&trigA);
        w.Append(sc.filter[f]);
    }

    // results: a header, the cards, and the pages
    sc.header = new GuiText(NULL, 18, (GXColor){255, 255, 255, 255});
    sc.header->SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    sc.header->SetPosition(x0 + 4, 94);
    sc.header->SetMaxWidth(CARD_W - 8);
    w.Append(sc.header);

    GuiImage *cardImg[PAGE], *cardImgOver[PAGE];
    for (int s = 0; s < PAGE; s++)
    {
        int y = 120 + s * (CARD_H + 4);

        cardImg[s] = new GuiImage(&card);
        cardImgOver[s] = new GuiImage(&cardOver);
        sc.title[s] = new GuiText(NULL, 18, (GXColor){255, 255, 255, 255});
        sc.title[s]->SetAlignment(ALIGN_LEFT, ALIGN_TOP);
        sc.title[s]->SetPosition(184, 10);
        sc.title[s]->SetMaxWidth(CARD_W - 196);
        sc.channel[s] = new GuiText(NULL, 16, (GXColor){200, 200, 205, 255});
        sc.channel[s]->SetAlignment(ALIGN_LEFT, ALIGN_TOP);
        sc.channel[s]->SetPosition(184, 40);
        sc.channel[s]->SetMaxWidth(CARD_W - 196);
        sc.info[s] = new GuiText(NULL, 16, (GXColor){160, 160, 166, 255});
        sc.info[s]->SetAlignment(ALIGN_LEFT, ALIGN_TOP);
        sc.info[s]->SetPosition(184, 66);
        sc.info[s]->SetMaxWidth(CARD_W - 196);

        sc.card[s] = new GuiButton(CARD_W, CARD_H);
        sc.card[s]->SetAlignment(ALIGN_LEFT, ALIGN_TOP);
        sc.card[s]->SetPosition(x0, y);
        sc.card[s]->SetImage(cardImg[s]);
        sc.card[s]->SetImageOver(cardImgOver[s]);
        sc.card[s]->SetLabel(sc.title[s], 0);
        sc.card[s]->SetLabel(sc.channel[s], 1);
        sc.card[s]->SetLabel(sc.info[s], 2);
        sc.card[s]->SetTrigger(&trigA);
        w.Append(sc.card[s]);

        // a dark box behind the thumbnail, all that playlists show; colour
        // images are built from 4x4 tiles, so its height is a multiple of 4
        sc.thumbBox[s] = new GuiImage(160, 92, (GXColor){0, 0, 0, 255});
        sc.thumbBox[s]->SetAlignment(ALIGN_LEFT, ALIGN_TOP);
        sc.thumbBox[s]->SetPosition(x0 + 10, y + 2);
        w.Append(sc.thumbBox[s]);

        // scaled down to 160x90 when it arrives
        sc.thumb[s] = new GuiImage();
        sc.thumb[s]->SetAlignment(ALIGN_LEFT, ALIGN_TOP);
        sc.thumb[s]->SetVisible(false);
        w.Append(sc.thumb[s]);
    }

    GuiImage prevImg(&button), prevImgOver(&buttonOver), nextImg(&button), nextImgOver(&buttonOver);
    GuiText prevTxt("Previous", 18, (GXColor){255, 255, 255, 255});
    GuiText nextTxt("Next", 18, (GXColor){255, 255, 255, 255});
    sc.prev = new GuiButton(button.GetWidth(), button.GetHeight());
    sc.prev->SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    sc.prev->SetPosition(x0, 424);
    sc.prev->SetImage(&prevImg);
    sc.prev->SetImageOver(&prevImgOver);
    sc.prev->SetLabel(&prevTxt);
    sc.prev->SetTrigger(&trigA);
    w.Append(sc.prev);
    sc.next = new GuiButton(button.GetWidth(), button.GetHeight());
    sc.next->SetAlignment(ALIGN_LEFT, ALIGN_TOP);
    sc.next->SetPosition(x0 + CARD_W - button.GetWidth(), 424);
    sc.next->SetImage(&nextImg);
    sc.next->SetImageOver(&nextImgOver);
    sc.next->SetLabel(&nextTxt);
    sc.next->SetTrigger(&trigA);
    w.Append(sc.next);
    sc.pageTxt = new GuiText(NULL, 18, (GXColor){200, 200, 205, 255});
    sc.pageTxt->SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
    sc.pageTxt->SetPosition(0, 432);
    w.Append(sc.pageTxt);

    StartWorker();

    SuspendGui();
    mainWindow->SetState(STATE_DISABLED);
    mainWindow->Append(&w);
    mainWindow->ChangeFocus(&w);
    ResumeGui();
    Show(&sc);

    bool done = false;
    int tabClicks = MenuTabClicks();

    // a tab at the top leaves the screen: Online Media for its list, any
    // other for its menu
    while (!done && !ExitRequested && menuCurrent == MENU_BROWSE_ONLINEMEDIA && MenuTabClicks() == tabClicks)
    {
        usleep(THREAD_SLEEP);

        YtList *l = depth > 0 ? lists[depth - 1] : NULL;

        // thumbnails the worker has ready
        LWP_MutexLock(lock);
        for (int s = 0; s < PAGE; s++)
        {
            if (!thumbReady[s] || !l || thumbReadyItem[s] != l->first + s)
                continue;

            GuiImageData *d = thumbReady[s];
            thumbReady[s] = NULL;
            SuspendGui();
            // fitted into 160x90 and centred there: a podcast's art is square
            float scale = 160.0f / d->GetWidth();
            if (d->GetHeight() * scale > 90.0f)
                scale = 90.0f / d->GetHeight();
            sc.thumb[s]->SetImage(d);
            sc.thumb[s]->SetScale(scale);
            sc.thumb[s]->SetPosition(x0 + 10 + (int)(160 - d->GetWidth() * scale) / 2,
                120 + s * (CARD_H + 4) + 3 + (int)(90 - d->GetHeight() * scale) / 2);
            sc.thumb[s]->SetVisible(true);
            ResumeGui();
            delete sc.shown[s];
            sc.shown[s] = d;
            thumbShownItem[s] = thumbReadyItem[s];
            thumbShownList[s] = l;
            thumbReadyItem[s] = -1;
        }
        LWP_MutexUnlock(lock);

        // the selected card's title scrolls when it doesn't fit
        for (int s = 0; s < PAGE; s++)
            sc.title[s]->SetScroll(sc.card[s]->GetState() == STATE_SELECTED ? SCROLL_HORIZONTAL : SCROLL_NONE);

        if (backBtn.GetState() == STATE_CLICKED)
        {
            backBtn.ResetState();
            if (depth == 0)
                done = true;
            else
            {
                Close();
                Show(&sc);
            }
            continue;
        }

        if (sc.search->GetState() == STATE_CLICKED)
        {
            sc.search->ResetState();
            char query[QUERY_MAX + 1];
            snprintf(query, sizeof(query), "%s", lastQuery);

            if (SearchKeyboard(query))
            {
                snprintf(lastQuery, sizeof(lastQuery), "%s", query);
                char head[160];
                int n = snprintf(head, sizeof(head), "Results for \"%s\"", query);
                // and the filters used, in brackets
                int k = 0;
                for (int f = 0; f < FILTERS && !YouTubeUseRiiTube() && n < (int)sizeof(head); f++)
                    if (filterPick[f])
                        n += snprintf(head + n, sizeof(head) - n, "%s%s", k++ ? ", " : " (", filterOpts[f][filterPick[f]].name);
                if (k && n < (int)sizeof(head) - 1)
                    snprintf(head + n, sizeof(head) - n, ")");
                Open(&sc, query, NULL, false, head);
            }
            mainWindow->ChangeFocus(&w);
            Show(&sc);
            continue;
        }

        for (int i = 0; i < TILES; i++)
        {
            if (sc.tile[i]->GetState() == STATE_CLICKED)
            {
                sc.tile[i]->ResetState();
                if (!tiles[i].browseId)
                {
                    WindowPrompt(tiles[i].name, "Live isn't LIVE yet!", "OK", NULL);
                    mainWindow->ChangeFocus(&w);
                    Show(&sc);
                }
                else if (YouTubeUseRiiTube())
                {
                    char title[64];
                    RiiTubeTitle(title, sizeof(title), tiles[i].name);
                    Open(&sc, tiles[i].topic, NULL, false, title);
                }
                else
                    Open(&sc, NULL, tiles[i].browseId, false, tiles[i].name, tiles[i].params, tiles[i].topic);
            }
        }

        for (int f = 0; f < FILTERS; f++)
        {
            if (sc.filter[f]->GetState() == STATE_CLICKED)
            {
                sc.filter[f]->ResetState();
                filterPick[f] = (filterPick[f] + 1) % FilterChoices(f);
                Show(&sc);
            }
        }

        if (sc.source->GetState() == STATE_CLICKED)
        {
            sc.source->ResetState();
            YouTubeSetRiiTube(!YouTubeUseRiiTube());
            Show(&sc);
        }

        if (l && sc.prev->GetState() == STATE_CLICKED)
        {
            sc.prev->ResetState();
            l->first = l->first >= PAGE ? l->first - PAGE : 0;
            Show(&sc);
        }

        if (l && sc.next->GetState() == STATE_CLICKED)
        {
            sc.next->ResetState();
            if (l->first + PAGE >= l->count)
                More(l);
            if (l->first + PAGE < l->count)
                l->first += PAGE;
            Show(&sc);
        }

        for (int s = 0; l && s < PAGE; s++)
        {
            if (sc.card[s]->GetState() != STATE_CLICKED)
                continue;
            sc.card[s]->ResetState();

            YtItem *it = &l->items[l->first + s];
            if (it->playlist)
            {
                Open(&sc, NULL, it->id, true, it->title);
                break;
            }

            YouTubeRiiTubeUrl(it->id, url, urlsize);
            snprintf(title, titlesize, "%s", it->title);
            picked = true;
            done = true;
            break;
        }
    }

    StopWorker();

    SuspendGui();
    mainWindow->Remove(&w);
    mainWindow->SetState(STATE_DEFAULT);
    ResumeGui();

    reopen = picked;

    for (int s = 0; s < PAGE; s++)
    {
        delete sc.card[s];
        delete cardImg[s];
        delete cardImgOver[s];
        delete sc.title[s];
        delete sc.channel[s];
        delete sc.info[s];
        delete sc.thumbBox[s];
        delete sc.thumb[s];
        delete sc.shown[s];
        thumbShownItem[s] = -1;
    }
    for (int i = 0; i < TILES; i++)
    {
        delete sc.tile[i];
        delete tileImg[i];
        delete tileImgOver[i];
        delete tileIconImg[i];
        delete tileTxt[i];
        delete tileIcon[i];
    }
    for (int f = 0; f < FILTERS; f++)
    {
        delete sc.filter[f];
        delete sc.filterTxt[f];
        delete filterImg[f];
        delete filterImgOver[f];
    }
    delete filterData;
    delete filterDataOver;
    delete sc.source;
    delete sourceImg;
    delete sourceImgOver;
    delete sc.sourceTxt;
    delete sc.search;
    delete sc.searchTxt;
    delete sc.header;
    delete sc.prev;
    delete sc.next;
    delete sc.pageTxt;
    return picked;
}
