#include "../include/types.h"
#include "../include/nuzlocke/newgame_config.h"
#include "../include/nuzlocke/starter_randomizer.h"
static u16 sStarterSpecies[3]={152,155,158};
static u8 sInit=0;
static u16 RandomSpecies(void){static u16 c=1;c=c*1103515245+12345;return 1+((c>>16)%493);}
void StarterRandomizer_Init(void){struct NewGameConfig*cfg=NewGameConfig_GetActive();if(cfg&&NewGameConfig_IsStarterRandomized()){for(int i=0;i<3;i++){u16 s;int a=0;do{s=RandomSpecies();a++;}while(a<100);sStarterSpecies[i]=s;}}else{sStarterSpecies[0]=152;sStarterSpecies[1]=155;sStarterSpecies[2]=158;}sInit=1;}
u16 StarterRandomizer_GetSpecies(u8 slot){if(!sInit)StarterRandomizer_Init();return(slot<3)?sStarterSpecies[slot]:152;}
u8 StarterRandomizer_AllClaimed(void){return 0;}
void StarterRandomizer_ClaimSlot(u8 slot){}
