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
        sMenuStringBuf->data[i] = 0;
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

static void DrawLoading(void)
{
    u8 phase = (u8)(gSystem.vblankCounter % 20);
    SetBackdrop(phase < 10 ? GX_RGB(0, 31, 0) : GX_RGB(0, 0, 0));   /* Green / Black */
}

/* ---- Post-menu callback -------------------------------------------------- */
static void (*sPostMenuCallback)(void) = NULL;

/* ---- Text-based menu rendering ------------------------------------------- */

#define ROW_HEIGHT       14   /* Taller rows for readability */
#define LABEL_X          4    /* Left margin for labels */
#define VALUE_X          160  /* Right margin for values */

/* Color-coded value colors matching Radical Red style */
#define COLOR_VALUE_OFF  COLOUR_RED
#define COLOR_VALUE_ON   COLOUR_GREEN

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
    u8 footerRow = sCatCount + 3;
    MenuText_PrintAt("A=Confirm  B=Cancel", 4, footerRow, COLOUR_DARK);
    MenuText_PrintAt("UP/DOWN=Nav  L/R=Change", 4, footerRow + 1, COLOUR_DARK);
}

static void MenuText_DrawAll(void)
{
    u8 i;

    if (!sGfxInitDone) return;

    /* Clear window pixel buffer completely */
    FillWindowPixelBuffer(&sWindow, 0);

    /* Reset text printers to prevent state bleeding between draws */
    ResetAllTextPrinters();

    /* Draw header */
    MenuText_DrawHeader();

    /* Draw each category row */
    for (i = 0; i < sCatCount; i++) {
        MenuText_DrawRow(i, i + 3);  /* Start at row 3 (after header) */
    }

    /* Draw footer */
    MenuText_DrawFooter();

    /* Immediate VRAM copy for clean update */
    CopyWindowToVram(&sWindow);
}

/* ---- Comprehensive display state save/restore ---------------------------
 *  Per NDS research: must save VRAM banks, BG registers, then DISPCNT.
 *  Restore order: VRAM banks FIRST, then BG registers, then DISPCNT.
 *  This prevents extended palette / VRAM bank conflicts.              */

static u32 sSavedDispCnt[2];      /* Engine A, B DISPCNT */
static u16 sSavedBgCnt[8];        /* BG0-3 main, BG0-3 sub */
static u32 sSavedBgScroll[8];     /* hofs/vofs for each BG */
static u8  sSavedVramBanks[9];    /* VRAM banks A-I control regs */

static void MenuGfx_SaveState(void)
{
    u8 i;

    /* 1. Save DISPCNT */
    sSavedDispCnt[0] = *(vu32 *)0x04000000;  /* Engine A */
    sSavedDispCnt[1] = *(vu32 *)0x04001000;  /* Engine B */

    /* 2. Save BG control registers */
    sSavedBgCnt[0] = *(vu16 *)0x04000008;  /* BG0CNT A */
    sSavedBgCnt[1] = *(vu16 *)0x0400000A;  /* BG1CNT A */
    sSavedBgCnt[2] = *(vu16 *)0x0400000C;  /* BG2CNT A */
    sSavedBgCnt[3] = *(vu16 *)0x0400000E;  /* BG3CNT A */
    sSavedBgCnt[4] = *(vu16 *)0x04001008;  /* BG0CNT B */
    sSavedBgCnt[5] = *(vu16 *)0x0400100A;  /* BG1CNT B */
    sSavedBgCnt[6] = *(vu16 *)0x0400100C;  /* BG2CNT B */
    sSavedBgCnt[7] = *(vu16 *)0x0400100E;  /* BG3CNT B */

    /* 3. Save BG scroll registers */
    sSavedBgScroll[0] = *(vu16 *)0x04000010;  /* BG0HOFS A */
    sSavedBgScroll[1] = *(vu16 *)0x04000012;  /* BG0VOFS A */
    sSavedBgScroll[2] = *(vu16 *)0x04000014;  /* BG1HOFS A */
    sSavedBgScroll[3] = *(vu16 *)0x04000016;  /* BG1VOFS A */
    sSavedBgScroll[4] = *(vu16 *)0x04000018;  /* BG2HOFS A */
    sSavedBgScroll[5] = *(vu16 *)0x0400001A;  /* BG2VOFS A */
    sSavedBgScroll[6] = *(vu16 *)0x0400001C;  /* BG3HOFS A */
    sSavedBgScroll[7] = *(vu16 *)0x0400001E;  /* BG3VOFS A */

    /* 4. Save VRAM bank configuration */
    for (i = 0; i < 9; i++) {
        sSavedVramBanks[i] = *(vu8 *)(0x04000240 + i);
    }
}

static void MenuGfx_RestoreState(void)
{
    u8 i;

    /* CRITICAL restore order: VRAM banks FIRST, then BG, then DISPCNT */

    /* 1. Restore VRAM bank configuration FIRST */
    for (i = 0; i < 9; i++) {
        *(vu8 *)(0x04000240 + i) = sSavedVramBanks[i];
    }

    /* 2. Restore BG control registers */
    *(vu16 *)0x04000008 = sSavedBgCnt[0];
    *(vu16 *)0x0400000A = sSavedBgCnt[1];
    *(vu16 *)0x0400000C = sSavedBgCnt[2];
    *(vu16 *)0x0400000E = sSavedBgCnt[3];
    *(vu16 *)0x04001008 = sSavedBgCnt[4];
    *(vu16 *)0x0400100A = sSavedBgCnt[5];
    *(vu16 *)0x0400100C = sSavedBgCnt[6];
    *(vu16 *)0x0400100E = sSavedBgCnt[7];

    /* 3. Restore BG scroll registers */
    *(vu16 *)0x04000010 = sSavedBgScroll[0];
    *(vu16 *)0x04000012 = sSavedBgScroll[1];
    *(vu16 *)0x04000014 = sSavedBgScroll[2];
    *(vu16 *)0x04000016 = sSavedBgScroll[3];
    *(vu16 *)0x04000018 = sSavedBgScroll[4];
    *(vu16 *)0x0400001A = sSavedBgScroll[5];
    *(vu16 *)0x0400001C = sSavedBgScroll[6];
    *(vu16 *)0x0400001E = sSavedBgScroll[7];

    /* 4. Restore DISPCNT LAST */
    *(vu32 *)0x04000000 = sSavedDispCnt[0];
    *(vu32 *)0x04001000 = sSavedDispCnt[1];
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

    /* Save existing display state before we reconfigure */
    MenuGfx_SaveState();

    /* Allocate BG config for menu background */
    bgConfig = BgConfig_Alloc(0);
    if (!bgConfig) {
        /* Fall back to palette-only feedback */
        sGfxInitDone = 0;
        return;
    }
    sBgConfig = bgConfig;

    /* Set up BG0 as text mode, 256-colour palette */
    sub_0200FBF4(0, 0);
    sub_0200FBF4(1, 0);

    GfGfx_DisableEngineAPlanes();
    GfGfx_DisableEngineBPlanes();

    GX_SetVisiblePlane(0);
    GXS_SetVisiblePlane(0);

    SetKeyRepeatTimers(4, 8);
    GfGfx_SwapDisplay();
    G2_BlendNone();
    G2S_BlendNone();
    GX_SetVisibleWnd(0);
    GXS_SetVisibleWnd(0);

    /* CRITICAL: Disable extended palette mode so standard palette writes work */
    {
        vu32 *dispA = (vu32 *)0x04000000;
        vu32 *dispB = (vu32 *)0x04001000;
        *dispA &= ~(1 << 26);  /* Clear EXT_PAL bit */
        *dispB &= ~(1 << 26);
    }

    GfGfx_SetBanks((void *)0x0210855C);

    /* Set screen mode */
    SetBothScreensModesAndDisable((void *)0x02108530);

    /* Init BG0 from template */
    InitBgFromTemplate(bgConfig, MENU_BG_ID, (void *)0x02108540, 0);
    BgClearTilemapBufferAndCommit(bgConfig, MENU_BG_ID);

    /* Load UI frame graphics and font palette */
    LoadUserFrameGfx1(bgConfig, MENU_BG_ID, 0x1F7, 2, 0, 0);
    LoadFontPal0(0, 0x20, 0);        /* FIX: palette slot 0 instead of 2 */
    /* REMOVED: BG_ClearCharDataRange() — wipes tiles the tutorial needs later */

    /* Set BG colours */
    BG_SetMaskColor(MENU_BG_ID,     RGB(15, 15, 15));
    BG_SetMaskColor(MENU_BG_ID + 4, RGB(15, 15, 15));

    /* Create the text window */
    AddWindow(bgConfig, &sWindow, (void *)&sWinTemplate);
    FillWindowPixelBuffer(&sWindow, 0xFF);
    DrawFrameAndWindow1(&sWindow, FALSE, 0x1F7, 2);

    ResetAllTextPrinters();          /* FIX: clear stale text printer state */

    /* Turn display on */
    GfGfx_BothDispOn();
    SetMasterBrightnessNeutral(0);
    SetMasterBrightnessNeutral(1);
    SetBlendBrightness(0, 0x3F, 3);

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

    /* Restore display state so OakSpeech / tutorial continues correctly */
    MenuGfx_RestoreState();

    /* Keep string buffer alive (reused if menu reopens) */

    sGfxInitDone = 0;
}

/* ---- Input handling ------------------------------------------------------ */

static void HandleInput(void)
{
    u16 keys = (u16)gSystem.newKeys;
    if (!keys) return;

    if (keys & KEY_UP) {
        if (sCursorPos > 0) sCursorPos--;
        else                sCursorPos = sCatCount - 1;
        sDrawPending = 1;
    }
    if (keys & KEY_DOWN) {
        if (sCursorPos < sCatCount - 1) sCursorPos++;
        else                            sCursorPos = 0;
        sDrawPending = 1;
    }
    if (keys & KEY_LEFT) {
        u8 *v = GetField(&sTemp, sCursorPos);
        if (*v > 0) (*v)--;
        else        *v = sCatMax[sCursorPos];
        sDrawPending = 1;
    }
    if (keys & KEY_RIGHT) {
        u8 *v = GetField(&sTemp, sCursorPos);
        if (*v < sCatMax[sCursorPos]) (*v)++;
        else                          *v = 0;
        sDrawPending = 1;
    }
    if (keys & KEY_A) {
        sConfirmed   = 1;
        sMenuActive  = 0;
        NewGameConfig_Save();
        sDrawPending = 1;
    }
    if (keys & KEY_B) {
        sMenuActive  = 0;
        NewGameConfig_InitDefaults(&sSaved);
        sDrawPending = 1;
    }
}

/* ---- SysTask callback ---------------------------------------------------- */

static void ConfigMenuTaskCB(SysTask *task, void *data)
{
    (void)task; (void)data;

    /* Post-menu callback: load OakSpeech after menu closes */
    if (sPostMenuCallback) {
        sPostMenuCallback();
        sPostMenuCallback = NULL;
        DestroySysTask(sMenuTask);
        sMenuTask = NULL;
        return;
    }

    if (!sMenuActive) {
        /* Menu closing: shut down graphics */
        if (sGfxInitDone) {
            MenuGfx_Shutdown();
        }
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
            /* Flash confirmation */
            u8 phase = (u8)(gSystem.vblankCounter % 10);
            SetBackdrop(phase < 5 ? GX_RGB(31, 31, 31) : GX_RGB(31, 0, 0));
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

/* External declarations for overlay 36 hook */
extern const void *gApplication_OakSpeech;
extern void LONG_CALL RegisterMainOverlay(u32 ovyId, const void *template);
extern void LONG_CALL Heap_Destroy(u32 heapId);

static void LoadOakSpeechAfterMenu(void)
{
    RegisterMainOverlay(0xFFFFFFFF, &gApplication_OakSpeech);
}

/* Hook function for overlay 36 AppExit — replaces original function */
BOOL LONG_CALL NewGameConfig_Hook_AppExit(void *man, int *state)
{
    (void)man; (void)state;
    
    /* Destroy overlay 36 heap */
    Heap_Destroy(36);  /* HEAP_ID_OV36 */
    
    /* Set callback to load OakSpeech after menu closes */
    sPostMenuCallback = LoadOakSpeechAfterMenu;
    
    /* Trigger config menu (runs on clean BG slate) */
    NewGameConfig_TriggerOnNewGame();
    
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
