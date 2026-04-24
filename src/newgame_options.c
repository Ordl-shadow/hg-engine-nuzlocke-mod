/* ============================================================================
 *  newgame_options.c  -  New Game Configuration Menu
 *  ---------------------------------------------------------------------------
 *  Displays a 15-category option menu when the player starts a new game.
 *  Uses the game's proper Window / text-rendering system instead of raw
 *  palette hacks so the menu is readable on real hardware and emulators.
 *
 *  Integration point: Save_InitDynamicRegion() calls
 *  NewGameConfig_TriggerOnNewGame() automatically on new-game init.
 * ============================================================================ */

#include "../include/types.h"
#include "../include/system.h"
#include "../include/task.h"
#include "../include/window.h"
#include "../include/message.h"
#include "../include/constants/buttons.h"
#include "../include/nuzlocke/newgame_config.h"

/* ---- Key definitions ------------------------------------------------------- */
#define KEY_A     PAD_BUTTON_A
#define KEY_B     PAD_BUTTON_B
#define KEY_UP    PAD_KEY_UP
#define KEY_DOWN  PAD_KEY_DOWN
#define KEY_LEFT  PAD_KEY_LEFT
#define KEY_RIGHT PAD_KEY_RIGHT

/* ---- Menu timing ---------------------------------------------------------- */
#define MENU_DELAY_FRAMES   120   /* 2 second delay while graphics init */
#define MENU_DRAW_INTERVAL  10    /* Redraw every 10 frames (input-responsive) */

/* ---- Window geometry (top screen) ---------------------------------------- */
#define MENU_BG_ID       0
#define MENU_WIN_LEFT    2
#define MENU_WIN_TOP     1
#define MENU_WIN_WIDTH  26       /* 26 tiles * 8px = 208px */
#define MENU_WIN_HEIGHT 20       /* 20 tiles * 8px = 160px (almost full screen) */
#define MENU_PALETTE     0
#define MENU_BASE_TILE   0xA

/* ---- Font / colour constants --------------------------------------------- */
#define FONT_SYSTEM      0        /* FIX: font 0 matches error handler / system default */
#define TEXT_SPEED_FAST  0xFF    /* TEXT_SPEED_NOTRANSFER */

/* Palette index meanings depend on font palette load, but standard ordering:
 * 0 = white, 1 = black/dark, 2 = red, 3 = green, 4 = blue */
#define COLOUR_WHITE     0x00
#define COLOUR_DARK      0x01
#define COLOUR_RED       0x02
#define COLOUR_GREEN     0x03
#define COLOUR_BLUE      0x04

#define MAKE_COLOR_IDX(fg, shadow, bg) (((fg) << 16) | ((shadow) << 8) | (bg))

/* ---- Internal menu state ------------------------------------------------- */
static struct NewGameConfig sSaved;
static struct NewGameConfig sTemp;
static u8  sMenuActive    = 0;
static u8  sCursorPos     = 0;
static u8  sConfirmed     = 0;
static u8  sDrawPending   = 0;
static SysTask *sMenuTask = NULL;
static u32 sLastDrawVBlank= 0;
static u16 sDelayCounter  = 0;

/* ---- Window / BG state --------------------------------------------------- */
static void    *sBgConfig = NULL;
static struct Window sWindow;
static u8      sGfxInitDone = 0;

/* ---- Category display strings -------------------------------------------- */
static const char *sCatNames[] = {
    "Mode",      "Difficulty",     "IVs",     "EVs",
    "Level Cap",      "Shiny",     "Scaler",    "EXP",
    "Style",   "RndEnc", "RndStarters",    "RndAbil",
    "RndMoves",   "RndItems",      "RndTrnrs"
};

static const u8 sCatMax[] = {
    2, 3, 4, 4, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1, 1
};

static const u8 sCatCount = CONFIG_CATEGORY_COUNT;

/* ---- Local display init structs (verified against pret/pokeheartgold) ---- */

typedef struct GraphicsModes {
    u32 dispMode;
    u32 bgMode;
    u32 subMode;
    u32 _2d3dMode;
} GraphicsModes;

typedef struct BgTemplate {
    u32 x;
    u32 y;
    u32 bufferSize;
    u32 baseTile;
    u8 size;
    u8 colorMode;
    u8 screenBase;
    u8 charBase;
    u8 bgExtPltt;
    u8 priority;
    u8 areaOver;
    u8 dummy;
    u32 mosaic;
} BgTemplate;

/* GraphicsBanks must match GfGfx_SetBanks expectation (10 x u32) */
typedef struct GraphicsBanks {
    u32 bg;
    u32 bgextpltt;
    u32 subbg;
    u32 subbgextpltt;
    u32 obj;
    u32 objextpltt;
    u32 subobj;
    u32 subobjextpltt;
    u32 tex;
    u32 texpltt;
} GraphicsBanks;

static const GraphicsModes sMenuGraphicsModes = { 1, 0, 0, 0 };

static const BgTemplate sMenuBgTemplate = {
    0, 0, 0x800, 0,
    1, 0, 0, 6,
    0, 1, 0, 0,
    0
};

/* VRAM: 256KB main BG (banks A+B), no sub BG, no OBJ, no texture */
static const GraphicsBanks sMenuBanks = {
    .bg         = 3,   /* GX_VRAM_BG_256_AB */
    .bgextpltt  = 0,   /* GX_VRAM_BGEXTPLTT_NONE */
    .subbg      = 0,   /* GX_VRAM_SUB_BG_NONE */
    .subbgextpltt = 0, /* GX_VRAM_SUB_BGEXTPLTT_NONE */
    .obj        = 0,   /* GX_VRAM_OBJ_NONE */
    .objextpltt = 0,   /* GX_VRAM_OBJEXTPLTT_NONE */
    .subobj     = 0,   /* GX_VRAM_SUB_OBJ_NONE */
    .subobjextpltt = 0, /* GX_VRAM_SUB_OBJEXTPLTT_NONE */
    .tex        = 0,   /* GX_VRAM_TEX_NONE */
    .texpltt    = 0,   /* GX_VRAM_TEXPLTT_NONE */
};

/* ---- Window template for the text area ----------------------------------- */
static const WindowTemplate sWinTemplate = {
    MENU_BG_ID,
    MENU_WIN_LEFT,
    MENU_WIN_TOP,
    MENU_WIN_WIDTH,
    MENU_WIN_HEIGHT,
    MENU_PALETTE,
    MENU_BASE_TILE,
};

    /* ---- Helper: convert ASCII to game's font encoding ----
     *  HGSS uses a custom charmap where Latin letters are NOT at ASCII
     *  positions.  We map the contiguous blocks via simple arithmetic.
     *  All other characters become space (0x01DE).                    */
    static String *sMenuStringBuf = NULL;

    static void AsciiToString(const char *src)
    {
        u16 i;
        u8  ch;
        u16 code;

        if (!sMenuStringBuf) { sMenuStringBuf = String_New(128, 0); }

        /* CRITICAL: Clear entire buffer to prevent stale data from previous strings */
        for (i = 0; i < sMenuStringBuf->maxsize; i++) {
            sMenuStringBuf->data[i] = 0;
        }

        for (i = 0; src[i] != '\0' && i < sMenuStringBuf->maxsize - 1; i++) {
            ch = (u8)src[i];
            if (ch >= '0' && ch <= '9') {
                code = (u16)(0x0121 + (ch - '0'));
            } else if (ch >= 'A' && ch <= 'Z') {
                code = (u16)(0x012B + (ch - 'A'));
            } else if (ch >= 'a' && ch <= 'z') {
                code = (u16)(0x0145 + (ch - 'a'));
            } else if (ch == ' ') {
                code = 0x01DE;
            } else if (ch == '>') {
                code = 0x01DE;  /* Use space for > to avoid unmapped char */
            } else {
                code = 0x01DE;  /* space for anything else */
            }
            sMenuStringBuf->data[i] = code;
        }
        sMenuStringBuf->data[i] = 0xFFFF;
        sMenuStringBuf->size    = i;
    }

/* ---- Look up display string for a category's current value --------------- */
static const char *GetValueName(u8 cat, u8 val)
{
    switch (cat) {
        case 0:
            switch (val) { case 0: return "Standard"; case 1: return "Nuzlocke"; case 2: return "Hardcore"; default: return "?"; }
        case 1:
            switch (val) { case 0: return "Easy"; case 1: return "Normal"; case 2: return "Hard"; case 3: return "Insane"; default: return "?"; }
        case 2:
            switch (val) { case 0: return "Default"; case 1: return "0 IVs"; case 2: return "21 IVs"; case 3: return "31 IVs"; case 4: return "Random"; default: return "?"; }
        case 3:
            switch (val) { case 0: return "Default"; case 1: return "0 EVs"; case 2: return "252 EVs"; case 3: return "510 EVs"; case 4: return "Random"; default: return "?"; }
        case 4:
            switch (val) { case 0: return "Off"; case 1: return "Soft"; case 2: return "Hard"; default: return "?"; }
        case 5:
            switch (val) { case 0: return "1/8192"; case 1: return "1/4096"; case 2: return "1/100"; default: return "?"; }
        case 6:
            switch (val) { case 0: return "Soft"; case 1: return "Normal"; case 2: return "Hard"; default: return "?"; }
        case 7:
            switch (val) { case 0: return "0.5x"; case 1: return "1x"; case 2: return "2x"; case 3: return "4x"; default: return "?"; }
        case 8:
            switch (val) { case 0: return "Shift"; case 1: return "Set"; case 2: return "PermaSet"; default: return "?"; }
        default:
            return (val != 0) ? "On" : "Off";
    }
}

/* ---- Field accessor using struct layout (all fields are u8, contiguous) -- */
static u8 *GetField(struct NewGameConfig *c, u8 idx)
{
    u8 *base = (u8 *)c;
    if (idx < sCatCount) return &base[idx];
    return &c->game_mode;
}

/* ---- Palette feedback (kept as secondary visual cue) --------------------- */
static void SetBackdrop(u16 color) { *(vu16 *)0x05000000 = color; }

/* ---- Post-menu callback -------------------------------------------------- */
static void (*sPostMenuCallback)(void) = NULL;

/* External declarations for overlay 36 hook */
extern const void *gApplication_OakSpeech;
extern void LONG_CALL RegisterMainOverlay(u32 ovyId, const void *template);
extern void LONG_CALL Heap_Destroy(u32 heapId);

static void LONG_CALL LoadOakSpeechAfterMenu(void)
{
    RegisterMainOverlay(0xFFFFFFFF, &gApplication_OakSpeech);
}

/* ---- Text-based menu rendering ------------------------------------------- */

#define ROW_HEIGHT       14   /* Taller rows for readability */
#define LABEL_X          4    /* Left margin for labels */
#define VALUE_X          160  /* Right margin for values */
#define MAX_VISIBLE_ROWS      8   /* Maximum rows visible on screen at once */

/* Color-coded value colors matching Radical Red style */
#define COLOR_VALUE_OFF  COLOUR_RED
#define COLOR_VALUE_ON   COLOUR_GREEN

static u8 sScrollOffset = 0;

static void UpdateScrollOffset(void)
{
    /* Keep cursor within visible window by scrolling */
    if (sCursorPos < sScrollOffset) {
        sScrollOffset = sCursorPos;
    } else if (sCursorPos >= sScrollOffset + MAX_VISIBLE_ROWS) {
        sScrollOffset = sCursorPos - MAX_VISIBLE_ROWS + 1;
    }
}


static void MenuText_PrintAt(const char *text, u8 x, u8 row, u8 fgColor)
{
    if (!sMenuStringBuf) return;

    AsciiToString(text);

    AddTextPrinterParameterizedWithColor(
        &sWindow,
        FONT_SYSTEM,
        sMenuStringBuf,
        x,                              /* x position */
        (u32)(row * ROW_HEIGHT + 2),   /* y  (pixel pitch with padding) */
        TEXT_SPEED_FAST,
        (u32)MAKE_COLOR_IDX(fgColor, COLOUR_DARK, COLOUR_WHITE),  /* bg = white for readability */
        NULL
    );

    /* CRITICAL: Flush text printer immediately so buffer isn't overwritten
     * by next AsciiToString call before text engine draws it */
    CopyWindowToVram(&sWindow);
}

static void MenuText_DrawHeader(void)
{
    /* Centered title at top */
    MenuText_PrintAt("  NEW GAME OPTIONS  ", 4, 0, COLOUR_BLUE);
}

static void MenuText_DrawRow(u8 catIdx, u8 rowOnScreen)
{
    u8  isSelected = (catIdx == sCursorPos) ? 1 : 0;
    u8  val        = *GetField(&sTemp, catIdx);
    const char *valName;
    char lineBuf[32];
    u8 valColor;

    if (val > sCatMax[catIdx]) val = 0;
    valName = GetValueName(catIdx, val);

    /* Label: "> Category" if selected, "  Category" otherwise */
    if (isSelected) {
        lineBuf[0] = '>';
        lineBuf[1] = ' ';
        lineBuf[2] = '\0';
    } else {
        lineBuf[0] = ' ';
        lineBuf[1] = ' ';
        lineBuf[2] = '\0';
    }
    /* Append category name */
    {
        u8 i = 2, j = 0;
        const char *name = sCatNames[catIdx];
        while (name[j] && i < sizeof(lineBuf) - 1) {
            lineBuf[i++] = name[j++];
        }
        lineBuf[i] = '\0';
    }

    /* Color-code values: red for Off/disabled, green for On/enabled */
    if (catIdx <= 3) {
        /* Mode, Difficulty, IVs, EVs - use neutral colors */
        valColor = isSelected ? COLOUR_RED : COLOUR_GREEN;
    } else {
        /* Toggle options - color based on value */
        valColor = (val == 0) ? COLOR_VALUE_OFF : COLOR_VALUE_ON;
    }

    MenuText_PrintAt(lineBuf, LABEL_X, rowOnScreen, isSelected ? COLOUR_BLUE : COLOUR_DARK);
    MenuText_PrintAt(valName, VALUE_X, rowOnScreen, valColor);
}

static void MenuText_DrawFooter(void)
{
    u8 footerRow = MAX_VISIBLE_ROWS + 3;  /* Fixed position at bottom of visible area */
    MenuText_PrintAt("A=Confirm  B=Cancel", 4, footerRow, COLOUR_DARK);
    MenuText_PrintAt("UP/DOWN=Nav  L/R=Change", 4, footerRow + 1, COLOUR_DARK);
}

static void MenuText_DrawAll(void)
{
    u8 i;

    if (!sGfxInitDone) return;

    /* Update scroll position based on cursor */
    UpdateScrollOffset();

    /* Clear window pixel buffer completely */
    FillWindowPixelBuffer(&sWindow, 0);

    /* Reset text printers to prevent state bleeding between draws */
    ResetAllTextPrinters();

    /* Draw header */
    MenuText_DrawHeader();

    /* Draw visible category rows (scrolled) */
    for (i = 0; i < MAX_VISIBLE_ROWS && (sScrollOffset + i) < sCatCount; i++) {
        u8 catIdx = sScrollOffset + i;
        MenuText_DrawRow(catIdx, i + 3);  /* Start at row 3 (after header) */
    }

    /* Draw footer at bottom of visible area */
    MenuText_DrawFooter();

    /* Immediate VRAM copy for clean update */
    CopyWindowToVram(&sWindow);
}

/* ---- Graphics init / teardown -------------------------------------------- */

static void MenuGfx_Init(void)
{
    void *bgConfig;

    if (sGfxInitDone) return;

    /* Allocate string buffer for text rendering */
    if (!sMenuStringBuf) {
        sMenuStringBuf = String_New(128, 0);
    }

    /* Allocate BG config for menu background */
    bgConfig = BgConfig_Alloc(0);
    if (!bgConfig) {
        sGfxInitDone = 0;
        return;
    }
    sBgConfig = bgConfig;

    /* NUCLEAR RESET: Disable everything first */
    GfGfx_DisableEngineAPlanes();
    GfGfx_DisableEngineBPlanes();
    GX_SetVisiblePlane(0);
    GXS_SetVisiblePlane(0);

    /* Set screen mode from local config */
    SetBothScreensModesAndDisable((void *)&sMenuGraphicsModes);

    /* Disable extended palette mode */
    {
        vu32 *dispA = (vu32 *)0x04000000;
        vu32 *dispB = (vu32 *)0x04001000;
        *dispA &= ~(1 << 26);
        *dispB &= ~(1 << 26);
    }

    /* Set VRAM banks from proper struct */
    GfGfx_SetBanks((void *)&sMenuBanks);

    /* Init BG from local template */
    InitBgFromTemplate(bgConfig, MENU_BG_ID, (void *)&sMenuBgTemplate, 0);
    BgClearTilemapBufferAndCommit(bgConfig, MENU_BG_ID);

    /* Load UI frame graphics */
    LoadUserFrameGfx1(bgConfig, MENU_BG_ID, 0x1F7, 2, 0, 0);

    /* Load font palette — CRITICAL: align with window template palette slot */
    LoadFontPal0(0, 0x40, 0);

    /* Set BG colours */
    BG_SetMaskColor(MENU_BG_ID, RGB(15, 15, 15));

    /* Create the text window */
    AddWindow(bgConfig, &sWindow, (void *)&sWinTemplate);
    FillWindowPixelBuffer(&sWindow, 0);  /* FIX: 0 not 0xFF for proper clear */
    DrawFrameAndWindow1(&sWindow, FALSE, 0x1F7, 2);

    /* Reset text printers */
    ResetAllTextPrinters();

    /* Turn display ON */
    GfGfx_BothDispOn();

    /* FORCE master brightness to neutral immediately (cancel any active fade) */
    *(vu16 *)0x0400006C = 0;  /* Engine A */
    *(vu16 *)0x0400106C = 0;  /* Engine B */

    sGfxInitDone = 1;
}

static void MenuGfx_Shutdown(void)
{
    if (!sGfxInitDone) return;

    /* Remove window */
    RemoveWindow(&sWindow);

    /* Free BG config */
    if (sBgConfig) {
        sys_FreeMemoryEz(sBgConfig);
        sBgConfig = NULL;
    }

    /* Restore master brightness to neutral for OakSpeech */
    *(vu16 *)0x0400006C = 0;
    *(vu16 *)0x0400106C = 0;

    /* Disable all engine planes and visible planes — leave display clean
     * so OakSpeech can init from a known state. */
    GfGfx_DisableEngineAPlanes();
    GfGfx_DisableEngineBLayers();
    GX_SetVisiblePlane(0);
    GXS_SetVisiblePlane(0);

    sGfxInitDone = 0;
}

/* ---- Input handling ------------------------------------------------------ */

/* ---- Direct hardware input polling (for hook context where gSystem.newKeys
 *  isn't updated by the main loop) --------------------------------------- */
#define REPEAT_INITIAL_DELAY    24   /* ~0.4 s at 60 fps */
#define REPEAT_INTERVAL         6    /* ~10 repeats/sec after delay */

static u16 sPrevHwKeys   = 0;
static u16 sRepeatKey    = 0;
static u16 sRepeatTimer  = 0;
static u8  sWaitForKeyRelease = 0;

static void HandleInput(void)
{
    u16 keys    = PAD_Read();
    u16 newKeys = keys & ~sPrevHwKeys;
    u16 action  = 0;

    /* ---- Key-release guard: ignore input until all keys are released ---- */
    if (sWaitForKeyRelease) {
        if (keys == 0) sWaitForKeyRelease = 0;
        sPrevHwKeys = keys;
        return;
    }

    /* ---- Choose which key to act on this frame ---- */
    if (newKeys) {
        /* Brand-new press: act immediately, reset repeat state */
        action         = newKeys;
        sRepeatKey     = 0;
        sRepeatTimer   = 0;
    } else if (keys) {
        /* Keys held — only repeat directional keys (never A/B) */
        u16 repeatMask = PAD_KEY_UP | PAD_KEY_DOWN | PAD_KEY_LEFT | PAD_KEY_RIGHT;
        u16 heldRepeat = keys & repeatMask;

        if (heldRepeat) {
            /* Isolate single key for deterministic repeat */
            u16 single = heldRepeat & (~heldRepeat + 1);

            if (sRepeatKey != single) {
                sRepeatKey   = single;
                sRepeatTimer = 0;
            } else {
                sRepeatTimer++;
                if (sRepeatTimer >= REPEAT_INITIAL_DELAY) {
                    if ((sRepeatTimer - REPEAT_INITIAL_DELAY) % REPEAT_INTERVAL == 0)
                        action = single;
                }
            }
        } else {
            sRepeatKey   = 0;
            sRepeatTimer = 0;
        }
    } else {
        sRepeatKey   = 0;
        sRepeatTimer = 0;
    }

    sPrevHwKeys = keys;
    if (!action) return;

    /* ---- Navigation ---- */
    if (action & KEY_UP) {
        if (sCursorPos > 0) sCursorPos--;
        else                sCursorPos = sCatCount - 1;
        sDrawPending = 1;
    }
    if (action & KEY_DOWN) {
        if (sCursorPos < sCatCount - 1) sCursorPos++;
        else                            sCursorPos = 0;
        sDrawPending = 1;
    }
    if (action & KEY_LEFT) {
        u8 *v = GetField(&sTemp, sCursorPos);
        if (*v > 0) (*v)--;
        else        *v = sCatMax[sCursorPos];
        sDrawPending = 1;
    }
    if (action & KEY_RIGHT) {
        u8 *v = GetField(&sTemp, sCursorPos);
        if (*v < sCatMax[sCursorPos]) (*v)++;
        else                          *v = 0;
        sDrawPending = 1;
    }

    /* ---- Confirm / Cancel ---- */
    if (action & KEY_A) {
        sConfirmed  = 1;
        sMenuActive = 0;
        NewGameConfig_Save();   /* copies sTemp -> sSaved */
        sDrawPending = 1;
    }
    if (action & KEY_B) {
        sMenuActive = 0;
        NewGameConfig_InitDefaults(&sSaved); /* cancel = revert to defaults */
        sDrawPending = 1;
    }
}

/* ---- SysTask callback ---------------------------------------------------- */

static void ConfigMenuTaskCB(SysTask *task, void *data)
{
    (void)task; (void)data;

    if (!sMenuActive) {
        /* Menu just closed: run post-menu callback first, then cleanup */
        if (sPostMenuCallback) {
            sPostMenuCallback();
            sPostMenuCallback = NULL;
        }
        /* Shut down graphics */
        if (sGfxInitDone) {
            MenuGfx_Shutdown();
        }
        /* Destroy self */
        if (sMenuTask) {
            DestroySysTask(sMenuTask);
            sMenuTask = NULL;
        }
        return;
    }

    /* ---- Delay period: black screen while we set up graphics ---- */
    if (sDelayCounter < MENU_DELAY_FRAMES) {
        sDelayCounter++;
        SetBackdrop(GX_RGB(0, 0, 0));  /* FIX: black instead of flashing green */
        if (sDelayCounter == MENU_DELAY_FRAMES) {
            /* Init proper text graphics */
            MenuGfx_Init();
            sDrawPending = 1;
        }
        return;
    }

    /* ---- Menu is active: handle input and redraw text ---- */
    HandleInput();

    /* Redraw whenever input changes OR on interval */
    if (sDrawPending || (gSystem.vblankCounter - sLastDrawVBlank >= MENU_DRAW_INTERVAL)) {
        sLastDrawVBlank = gSystem.vblankCounter;
        sDrawPending    = 0;

        if (sConfirmed) {
            /* Show confirmation briefly then let loop exit */
            SetBackdrop(GX_RGB(0, 31, 0));  /* Green flash for success */
            sDrawPending = 0;
        } else {
            /* Normal: clear backdrop and draw text menu */
            SetBackdrop(GX_RGB(0, 0, 0));
            MenuText_DrawAll();
        }
    }
}

/* ---- Public API ---------------------------------------------------------- */

void NewGameConfig_InitDefaults(struct NewGameConfig *c)
{
    c->game_mode                = 0;   /* Standard */
    c->difficulty               = 1;   /* Normal */
    c->iv_setting               = 0;   /* Default */
    c->ev_setting               = 0;   /* Default */
    c->level_cap                = 0;   /* Off */
    c->shiny_rate               = 0;   /* 1/8192 */
    c->level_scaler             = 1;   /* Normal */
    c->exp_modifier             = 1;   /* 1x */
    c->battle_style             = 0;   /* Shift */
    c->encounter_randomizer     = 0;   /* Off */
    c->encounter_include_legends= 0;   /* No */
    c->starter_randomizer       = 0;   /* Off */
    c->randomize_abilities      = 0;   /* Off */
    c->randomize_movesets       = 0;   /* Off */
    c->item_randomizer          = 0;   /* Off */
    c->randomize_trainer_teams  = 0;   /* Off */
    c->checksum                 = NewGameConfig_CalcChecksum(c);
}

u8 NewGameConfig_CalcChecksum(struct NewGameConfig *c)
{
    u8 sum = 0;
    u8 *p = (u8 *)c;
    u32 i;
    for (i = 0; i < sizeof(*c) - 1; i++)
        sum += p[i];
    return sum;
}

struct NewGameConfig *NewGameConfig_GetActive(void) { return &sSaved; }

void NewGameConfig_Save(void)
{
    sSaved = sTemp;
    sSaved.checksum = NewGameConfig_CalcChecksum(&sSaved);
}

/*  Load from persistent storage.
 *  TODO: When save-block expansion is ready, read from the reserved byte
 *        block instead of initialising defaults.  For now this always
 *        falls back to defaults (which is correct for a brand-new game). */
u8 NewGameConfig_Load(void)
{
    NewGameConfig_InitDefaults(&sSaved);
    return 1;   /* Return 1 = "loaded defaults" */
}

void NewGameConfig_InitOnBoot(void)
{
    if (!NewGameConfig_Load())
        NewGameConfig_InitDefaults(&sSaved);
}

/* ---- Accessor queries ---------------------------------------------------- */

u16 NewGameConfig_GetShinyDenominator(void)
{
    return (sSaved.shiny_rate == 1) ? 4096
         : (sSaved.shiny_rate == 2) ? 100
         : 8192;
}

u8 NewGameConfig_GetLevelScalerPercent(void)
{
    return (sSaved.level_scaler == 0) ? 70
         : (sSaved.level_scaler == 2) ? 120
         : 100;
}

u16 NewGameConfig_GetEXPMultiplierPercent(void)
{
    return (sSaved.exp_modifier == 0) ? 50
         : (sSaved.exp_modifier == 2) ? 200
         : (sSaved.exp_modifier == 3) ? 400
         : 100;
}

u8 NewGameConfig_IsNuzlocke(void)             { return (sSaved.game_mode >= 1) ? 1 : 0; }
u8 NewGameConfig_IsHardcore(void)             { return (sSaved.game_mode == 2) ? 1 : 0; }
u8 NewGameConfig_LevelCapType(void)           { return sSaved.level_cap; }
u8 NewGameConfig_IsItemRandomizerOn(void)     { return sSaved.item_randomizer; }
u8 NewGameConfig_IsEncounterRandomizerOn(void){ return sSaved.encounter_randomizer; }
u8 NewGameConfig_IncludeLegendaries(void)     { return sSaved.encounter_include_legends; }
u8 NewGameConfig_IsStarterRandomized(void)    { return sSaved.starter_randomizer; }
u8 NewGameConfig_GetBattleStyle(void)         { return sSaved.battle_style; }
u8 NewGameConfig_IsAbilitiesRandomized(void)  { return sSaved.randomize_abilities; }
u8 NewGameConfig_IsMovesetsRandomized(void)   { return sSaved.randomize_movesets; }
u8 NewGameConfig_IsTrainerTeamsRandomized(void){ return sSaved.randomize_trainer_teams; }

/* ---- Menu trigger / control ---------------------------------------------- */

/* ---- Hook function for overlay 1 TitleScreen NewGame exit — shows menu BEFORE OakSpeech ----
 *
 * This is a FULL FUNCTION REPLACEMENT (register 255) of
 * ov01_TitleScreen_NewGame_AppExit at 0x021E5B48.
 *
 * We block in this function until the player confirms/cancels the menu,
 * then load OakSpeech and return.  The blocking loop uses OS_WaitIrq
 * (sleep until VBlank) so it is not a busy-wait.
 */

BOOL LONG_CALL NewGameConfig_Hook_AppExit(void *man, int *state)
{
    (void)man; (void)state;

    /* ---- Init menu state ---- */
    sTemp           = sSaved;
    sCursorPos      = 0;
    sConfirmed      = 0;
    sMenuActive     = 1;
    sDrawPending    = 1;
    sDelayCounter   = 0;
    sLastDrawVBlank = gSystem.vblankCounter;
    sWaitForKeyRelease = 1;

    /* ---- Init display ---- */
    MenuGfx_Init();

    /* ---- Blocking loop: wait for player to confirm/cancel ---- */
    while (sMenuActive) {
        /* Service system tasks (VBlank, input, etc.) */
        OS_WaitIrq(TRUE, 1);
        
        /* Run our menu task callback manually in the hook context */
        ConfigMenuTaskCB(NULL, NULL);
    }

    /* ---- Menu closed: register OakSpeech and clean up ---- */
    /* This hook replaces ov01_TitleScreen_NewGame_AppExit (0x021E5B48).
     * The original function destroys the TitleScreen heap and
     * registers OakSpeech.  We must do the same. */
    MenuGfx_Shutdown();
    LoadOakSpeechAfterMenu();

    return TRUE;
}

void NewGameConfig_TriggerOnNewGame(void)
{
    sTemp           = sSaved;
    sCursorPos      = 0;
    sConfirmed      = 0;
    sMenuActive     = 1;
    sDelayCounter   = 0;
    sDrawPending    = 1;
    sLastDrawVBlank = gSystem.vblankCounter;
    sWaitForKeyRelease = 1;

    if (!sMenuTask) {
        sMenuTask = CreateSysTask(ConfigMenuTaskCB, NULL, 0);
    }
}

u8 NewGameConfig_UpdateMenu(void)
{
    if (!sMenuActive) return 0;
    HandleInput();
    if (gSystem.vblankCounter - sLastDrawVBlank >= MENU_DRAW_INTERVAL) {
        sLastDrawVBlank = gSystem.vblankCounter;
        if (sGfxInitDone) MenuText_DrawAll();
    }
    return sConfirmed ? 2 : 1;
}

void NewGameConfig_OpenMenu(void)
{
    NewGameConfig_TriggerOnNewGame();
}

void NewGameConfig_MainLoopUpdate(void)
{
    /* Placeholder: if the caller wants to run the menu in the main loop
     * instead of via SysTask, call UpdateMenu() here. */
}
