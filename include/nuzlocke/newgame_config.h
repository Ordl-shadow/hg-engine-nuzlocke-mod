#ifndef NEWGAME_CONFIG_H
#define NEWGAME_CONFIG_H
#include "../types.h"
enum{GAME_MODE_STANDARD,GAME_MODE_NUZLOCKE,GAME_MODE_HARDCORE,GAME_MODE_COUNT};
enum{DIFFICULTY_EASY,DIFFICULTY_NORMAL,DIFFICULTY_HARD,DIFFICULTY_INSANE,DIFFICULTY_COUNT};
enum{IV_DEFAULT,IV_ZERO,IV_21,IV_31,IV_RANDOM,IV_COUNT};
enum{EV_DEFAULT,EV_ZERO,EV_252,EV_510,EV_RANDOM,EV_COUNT};
enum{LEVEL_CAP_OFF,LEVEL_CAP_SOFT,LEVEL_CAP_HARD,LEVEL_CAP_COUNT};
enum{SHINY_RATE_8192,SHINY_RATE_4096,SHINY_RATE_100,SHINY_RATE_COUNT};
enum{LEVEL_SCALER_SOFT,LEVEL_SCALER_NORMAL,LEVEL_SCALER_HARD,LEVEL_SCALER_COUNT};
enum{EXP_05X,EXP_1X,EXP_2X,EXP_4X,EXP_COUNT};
enum{BATTLE_STYLE_SHIFT,BATTLE_STYLE_SET,BATTLE_STYLE_PERMASET,BATTLE_STYLE_COUNT};
enum{ENCOUNTER_RANDOM_OFF,ENCOUNTER_RANDOM_ON,ENCOUNTER_RANDOM_COUNT};
enum{ENCOUNTER_NO_LEGENDS,ENCOUNTER_YES_LEGENDS,ENCOUNTER_LEGENDS_COUNT};
enum{STARTER_RANDOM_OFF,STARTER_RANDOM_ON,STARTER_RANDOM_COUNT};
enum{ABILITIES_RANDOM_OFF,ABILITIES_RANDOM_ON,ABILITIES_RANDOM_COUNT};
enum{MOVESETS_RANDOM_OFF,MOVESETS_RANDOM_ON,MOVESETS_RANDOM_COUNT};
enum{ITEM_RANDOM_OFF,ITEM_RANDOM_ON,ITEM_RANDOM_COUNT};
enum{TRAINER_RANDOM_OFF,TRAINER_RANDOM_ON,TRAINER_RANDOM_COUNT};
#define CONFIG_CATEGORY_COUNT 15
struct NewGameConfig{u8 game_mode;u8 difficulty;u8 iv_setting;u8 ev_setting;u8 level_cap;u8 shiny_rate;u8 level_scaler;u8 exp_modifier;u8 battle_style;u8 encounter_randomizer;u8 encounter_include_legends;u8 starter_randomizer;u8 randomize_abilities;u8 randomize_movesets;u8 item_randomizer;u8 randomize_trainer_teams;u8 checksum;};
void NewGameConfig_InitDefaults(struct NewGameConfig*);u8 NewGameConfig_CalcChecksum(struct NewGameConfig*);struct NewGameConfig* NewGameConfig_GetActive(void);void NewGameConfig_Save(void);u8 NewGameConfig_Load(void);u16 NewGameConfig_GetShinyDenominator(void);u8 NewGameConfig_GetLevelScalerPercent(void);u16 NewGameConfig_GetEXPMultiplierPercent(void);u8 NewGameConfig_IsNuzlocke(void);u8 NewGameConfig_IsHardcore(void);u8 NewGameConfig_LevelCapType(void);u8 NewGameConfig_IsItemRandomizerOn(void);u8 NewGameConfig_IsEncounterRandomizerOn(void);u8 NewGameConfig_IncludeLegendaries(void);u8 NewGameConfig_IsStarterRandomized(void);u8 NewGameConfig_GetBattleStyle(void);u8 NewGameConfig_IsAbilitiesRandomized(void);u8 NewGameConfig_IsMovesetsRandomized(void);u8 NewGameConfig_IsTrainerTeamsRandomized(void);void NewGameConfig_InitOnBoot(void);void NewGameConfig_OpenMenu(void);u8 NewGameConfig_UpdateMenu(void);void NewGameConfig_TriggerOnNewGame(void);void NewGameConfig_MainLoopUpdate(void);BOOL LONG_CALL NewGameConfig_Hook_AppExit(void *man, int *state);
#endif
