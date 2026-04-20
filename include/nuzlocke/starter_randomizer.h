#ifndef STARTER_RANDOMIZER_H
#define STARTER_RANDOMIZER_H
#define STARTER_COUNT 3
#define STARTER_VANILLA_0 152
#define STARTER_VANILLA_1 155
#define STARTER_VANILLA_2 158
#define NATIONAL_DEX_FIRST 1
#define NATIONAL_DEX_LAST 493
void StarterRandomizer_Init(void);
u16 StarterRandomizer_GetSpecies(u8);
u8 StarterRandomizer_AllClaimed(void);
void StarterRandomizer_ClaimSlot(u8);
#endif
