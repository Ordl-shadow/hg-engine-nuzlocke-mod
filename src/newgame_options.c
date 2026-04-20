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
#define FONT_SYSTEM      4
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

/* ---- Helper: convert a narrow ASCII string to a game's String object ------
 *  The String type is UTF-16 internally, so we expand ASCII -> UTF-16.
 *  Uses a small static buffer to avoid heap allocation per frame.           */
static String *sMenuStringBuf = NULL;

static void AsciiToString(const char *src)
{
    u16 i;
    if (!sMenuStringBuf) return;

    for (i = 0; src[i] != '\0' && i < sMenuStringBuf->maxsize - 1; i++) {
        sMenuStringBuf->data[i] = (u16)(unsigned char)src[i];
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

/* ---- Text-based menu rendering ------------------------------------------- */

static void MenuText_PrintLine(const char *text, u8 row, u8 fgColor)
{
    if (!sMenuStringBuf) return;

    AsciiToString(text);

    AddTextPrinterParameterizedWithColor(
        &sWindow,
        FONT_SYSTEM,
        sMenuStringBuf,
        4,                              /* x */
        (u32)(row * 12),                /* y  (12px line pitch) */
        TEXT_SPEED_FAST,
        (u32)MAKE_COLOR_IDX(fgColor, COLOUR_DARK, 0xFF),  /* fg, shadow, bg=transparent */
        NULL
    );
}

static void MenuText_PrintValue(const char *value, u8 row, u8 fgColor)
{
    if (!sMenuStringBuf) return;

    AsciiToString(value);

    /* Right-align at x = 148 for values */
    AddTextPrinterParameterizedWithColor(
        &sWindow,
        FONT_SYSTEM,
        sMenuStringBuf,
        148,                            /* x  (pushed right for values) */
        (u32)(row * 12),                /* y */
        TEXT_SPEED_FAST,
        (u32)MAKE_COLOR_IDX(fgColor, COLOUR_DARK, 0xFF),
        NULL
    );
}

static void MenuText_DrawHeader(void)
{
    MenuText_PrintLine("==NEW GAME OPTIONS==", 0, COLOUR_BLUE);
    MenuText_PrintLine("----------------", 1, COLOUR_DARK);
}

static void MenuText_DrawRow(u8 catIdx, u8 rowOnScreen)
{
    u8  isSelected = (catIdx == sCursorPos) ? 1 : 0;
    u8  fgColor    = isSelected ? COLOUR_BLUE : COLOUR_DARK;
    u8  valFgColor = isSelected ? COLOUR_RED  : COLOUR_GREEN;
    u8  val        = *GetField(&sTemp, catIdx);
    const char *valName;
    char lineBuf[36];
    const char *marker;
    u8 i, j;
    const char *name;

    if (val > sCatMax[catIdx]) val = 0;
    valName = GetValueName(catIdx, val);

    /* Build "  CategoryName" or "> CategoryName" line */
    marker = isSelected ? "> " : "  ";
    i = 0;
    while (marker[i]) { lineBuf[i] = marker[i]; i++; }
    j = 0;
    name = sCatNames[catIdx];
    while (name[j] && i < sizeof(lineBuf) - 1) { lineBuf[i] = name[j]; i++; j++; }
    lineBuf[i] = '\0';

    MenuText_PrintLine(lineBuf, rowOnScreen, fgColor);
    MenuText_PrintValue(valName, rowOnScreen, valFgColor);
}

static void MenuText_DrawFooter(void)
{
    u8 footerRow = sCatCount + 4;
    MenuText_PrintLine("----------------", footerRow,     COLOUR_DARK);
    MenuText_PrintLine("UP/DOWN=nav", footerRow + 1, COLOUR_DARK);
    MenuText_PrintLine("L/R=change",   footerRow + 2, COLOUR_DARK);
    MenuText_PrintLine("A=confirm B=cancel", footerRow + 3, COLOUR_DARK);
}

static void MenuText_DrawAll(void)
{
    u8 i;

    if (!sGfxInitDone) return;

    /* Clear window pixel buffer */
    FillWindowPixelBuffer(&sWindow, 0xFF);  /* Transparent fill */

    /* Draw header */
    MenuText_DrawHeader();

    /* Draw each category row */
    for (i = 0; i < sCatCount; i++) {
        MenuText_DrawRow(i, i + 3);  /* Start at row 3 (after header) */
    }

    /* Draw footer */
    MenuText_DrawFooter();

    /* Schedule VRAM copy */
    ScheduleWindowCopyToVram(&sWindow);
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

    GfGfx_SetBanks((void *)0x0210855C);

    /* Set screen mode */
    SetBothScreensModesAndDisable((void *)0x02108530);

    /* Init BG0 from template */
    InitBgFromTemplate(bgConfig, MENU_BG_ID, (void *)0x02108540, 0);
    BgClearTilemapBufferAndCommit(bgConfig, MENU_BG_ID);

    /* Load UI frame graphics and font palette */
    LoadUserFrameGfx1(bgConfig, MENU_BG_ID, 0x1F7, 2, 0, 0);
    LoadFontPal0(2, 0x20, 0);
    BG_ClearCharDataRange(MENU_BG_ID, 0x20, 0, 0);

    /* Set BG colours */
    BG_SetMaskColor(MENU_BG_ID,     RGB(15, 15, 15));
    BG_SetMaskColor(MENU_BG_ID + 4, RGB(15, 15, 15));

    /* Create the text window */
    AddWindow(bgConfig, &sWindow, (void *)&sWinTemplate);
    FillWindowPixelBuffer(&sWindow, 0xFF);
    DrawFrameAndWindow1(&sWindow, FALSE, 0x1F7, 2);

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

    /* ---- Delay period: flash green while we set up graphics ---- */
    if (sDelayCounter < MENU_DELAY_FRAMES) {
        sDelayCounter++;
        DrawLoading();
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
