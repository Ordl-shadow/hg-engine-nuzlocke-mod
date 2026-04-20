#include "../include/types.h"
#include "../include/system.h"
#include "../include/task.h"
#include "../include/constants/buttons.h"
#include "../include/nuzlocke/newgame_config.h"

#define KEY_A     PAD_BUTTON_A
#define KEY_B     PAD_BUTTON_B
#define KEY_UP    PAD_KEY_UP
#define KEY_DOWN  PAD_KEY_DOWN
#define KEY_LEFT  PAD_KEY_LEFT
#define KEY_RIGHT PAD_KEY_RIGHT

#define MENU_DELAY_FRAMES  180  /* 3 second delay for graphics init */

static struct NewGameConfig sSaved;
static struct NewGameConfig sTemp;
static u8 sMenuActive = 0;
static u8 sCursorPos  = 0;
static u8 sConfirmed  = 0;
static SysTask *sMenuTask = NULL;
static u32 sLastDrawVBlank = 0;
static u16 sDelayCounter = 0;

static const char *sCatNames[] = {
    "Game Mode",   "Difficulty",   "IV Settings", "EV Settings", "Level Cap",
    "Shiny Rate",  "Level Scaler", "EXP Modifier","Battle Style",
    "Rnd Encounters","Rnd Starters","Rnd Abilities","Rnd Movesets","Rnd Items","Rnd Trainers"
};

static const char *GetValueName(u8 cat, u8 val)
{
    switch (cat) {
        case 0:  return (const char*[]){"Standard","Nuzlocke","Hardcore"}[val];
        case 1:  return (const char*[]){"Easy","Normal","Hard","Insane"}[val];
        case 2:  return (const char*[]){"Default","0 IVs","21 IVs","31 IVs","Random"}[val];
        case 3:  return (const char*[]){"Default","0 EVs","252 EVs","510 EVs","Random"}[val];
        case 4:  return (const char*[]){"Off","Soft","Hard"}[val];
        case 5:  return (const char*[]){"1/8192","1/4096","1/100"}[val];
        case 6:  return (const char*[]){"Soft","Normal","Hard"}[val];
        case 7:  return (const char*[]){"0.5x","1x","2x","4x"}[val];
        case 8:  return (const char*[]){"Shift","Set","PermaSet"}[val];
        default: return val ? "On" : "Off";
    }
}

static const u8 sCatMax[] = {2,3,4,4,2,2,2,3,2,1,1,1,1,1,1};

static u8 *GetField(struct NewGameConfig *c, u8 idx)
{
    switch (idx) {
        case 0:  return &c->game_mode;
        case 1:  return &c->difficulty;
        case 2:  return &c->iv_setting;
        case 3:  return &c->ev_setting;
        case 4:  return &c->level_cap;
        case 5:  return &c->shiny_rate;
        case 6:  return &c->level_scaler;
        case 7:  return &c->exp_modifier;
        case 8:  return &c->battle_style;
        case 9:  return &c->encounter_randomizer;
        case 10: return &c->starter_randomizer;
        case 11: return &c->randomize_abilities;
        case 12: return &c->randomize_movesets;
        case 13: return &c->item_randomizer;
        case 14: return &c->randomize_trainer_teams;
        default: return &c->game_mode;
    }
}

u8 NewGameConfig_CalcChecksum(struct NewGameConfig *c)
{
    u8 sum = 0;
    u8 *p = (u8 *)c;
    for (int i = 0; i < (int)sizeof(*c) - 1; i++)
        sum += p[i];
    return sum;
}

void NewGameConfig_InitDefaults(struct NewGameConfig *c)
{
    c->game_mode = 0; c->difficulty = 1; c->iv_setting = 0; c->ev_setting = 0;
    c->level_cap = 0; c->shiny_rate = 0; c->level_scaler = 1; c->exp_modifier = 1;
    c->battle_style = 0; c->encounter_randomizer = 0; c->encounter_include_legends = 0;
    c->starter_randomizer = 0; c->randomize_abilities = 0; c->randomize_movesets = 0;
    c->item_randomizer = 0; c->randomize_trainer_teams = 0;
    c->checksum = NewGameConfig_CalcChecksum(c);
}

struct NewGameConfig *NewGameConfig_GetActive(void) { return &sSaved; }

void NewGameConfig_Save(void)
{
    sSaved = sTemp;
    sSaved.checksum = NewGameConfig_CalcChecksum(&sSaved);
}

u8 NewGameConfig_Load(void)
{
    NewGameConfig_InitDefaults(&sSaved);
    return 1;
}

void NewGameConfig_InitOnBoot(void)
{
    if (!NewGameConfig_Load())
        NewGameConfig_InitDefaults(&sSaved);
}

u16 NewGameConfig_GetShinyDenominator(void)
{
    return sSaved.shiny_rate == 1 ? 4096 : sSaved.shiny_rate == 2 ? 100 : 8192;
}

u8 NewGameConfig_GetLevelScalerPercent(void)
{
    return sSaved.level_scaler == 0 ? 70 : sSaved.level_scaler == 2 ? 120 : 100;
}

u16 NewGameConfig_GetEXPMultiplierPercent(void)
{
    return sSaved.exp_modifier == 0 ? 50 : sSaved.exp_modifier == 2 ? 200
         : sSaved.exp_modifier == 3 ? 400 : 100;
}

u8 NewGameConfig_IsNuzlocke(void)            { return sSaved.game_mode >= 1; }
u8 NewGameConfig_IsHardcore(void)            { return sSaved.game_mode == 2; }
u8 NewGameConfig_LevelCapType(void)          { return sSaved.level_cap; }
u8 NewGameConfig_IsItemRandomizerOn(void)    { return sSaved.item_randomizer == 1; }
u8 NewGameConfig_IsEncounterRandomizerOn(void){ return sSaved.encounter_randomizer == 1; }
u8 NewGameConfig_IncludeLegendaries(void)    { return sSaved.encounter_include_legends == 1; }
u8 NewGameConfig_IsStarterRandomized(void)   { return sSaved.starter_randomizer == 1; }
u8 NewGameConfig_GetBattleStyle(void)        { return sSaved.battle_style; }
u8 NewGameConfig_IsAbilitiesRandomized(void) { return sSaved.randomize_abilities == 1; }
u8 NewGameConfig_IsMovesetsRandomized(void)  { return sSaved.randomize_movesets == 1; }
u8 NewGameConfig_IsTrainerTeamsRandomized(void){ return sSaved.randomize_trainer_teams == 1; }

/* ---- Visual feedback using palette entry 0 (backdrop color) ---- */
static void SetBackdrop(u16 color) { *(vu16 *)0x05000000 = color; }

/* Green flashing = menu loading, wait for graphics init */
static void DrawLoading(void)
{
    u8 phase = (u8)(gSystem.vblankCounter % 20);
    SetBackdrop(phase < 10 ? 0x03E0 : 0x0000);  /* Green / Black */
}

/* Blue flashing = menu active, use D-pad to navigate */
static void DrawActive(void)
{
    u8 phase = (u8)(gSystem.vblankCounter % 15);
    SetBackdrop(phase < 8 ? 0x7FFF : 0x7C00);  /* White / Blue */
}

/* Red flashing = confirmed, options saved */
static void DrawConfirmed(void)
{
    u8 phase = (u8)(gSystem.vblankCounter % 10);
    SetBackdrop(phase < 5 ? 0x7FFF : 0x001F);  /* White / Red */
}

static void DrawMenu(void)
{
    if (sConfirmed)      DrawConfirmed();
    else                 DrawActive();
}

static void HandleInput(void)
{
    u16 keys = gSystem.newKeys;
    if (!keys) return;

    if (keys & KEY_UP)    { if (sCursorPos > 0) sCursorPos--; else sCursorPos = 14; }
    if (keys & KEY_DOWN)  { if (sCursorPos < 14) sCursorPos++; else sCursorPos = 0; }
    if (keys & KEY_LEFT)  { u8 *v = GetField(&sTemp, sCursorPos); if (*v > 0) (*v)--; else *v = sCatMax[sCursorPos]; }
    if (keys & KEY_RIGHT) { u8 *v = GetField(&sTemp, sCursorPos); if (*v < sCatMax[sCursorPos]) (*v)++; else *v = 0; }
    if (keys & KEY_A)     { sConfirmed = 1; sMenuActive = 0; NewGameConfig_Save(); }
    if (keys & KEY_B)     { sMenuActive = 0; NewGameConfig_InitDefaults(&sSaved); }
}

static void ConfigMenuTaskCB(SysTask *task, void *data)
{
    (void)task; (void)data;

    if (!sMenuActive) {
        if (sMenuTask) { DestroySysTask(sMenuTask); sMenuTask = NULL; }
        return;
    }

    /* Delay period - green flash while graphics system initializes */
    if (sDelayCounter < MENU_DELAY_FRAMES) {
        sDelayCounter++;
        DrawLoading();
        return;
    }

    /* Menu is active - handle input and draw */
    HandleInput();

    if (gSystem.vblankCounter - sLastDrawVBlank >= 15) {
        sLastDrawVBlank = gSystem.vblankCounter;
        DrawMenu();
    }
}

void NewGameConfig_TriggerOnNewGame(void)
{
    sTemp = sSaved; sCursorPos = 0; sConfirmed = 0;
    sMenuActive = 1;
    sDelayCounter = 0;
    sLastDrawVBlank = gSystem.vblankCounter;

    if (!sMenuTask) {
        sMenuTask = CreateSysTask(ConfigMenuTaskCB, NULL, 0);
    }
}

u8 NewGameConfig_UpdateMenu(void)
{
    if (!sMenuActive) return 0;
    HandleInput();
    if (gSystem.vblankCounter - sLastDrawVBlank >= 15) {
        sLastDrawVBlank = gSystem.vblankCounter; DrawMenu();
    }
    return sConfirmed ? 2 : 1;
}

void NewGameConfig_OpenMenu(void) { NewGameConfig_TriggerOnNewGame(); }
void NewGameConfig_MainLoopUpdate(void) { /* handled by task queue */ }
