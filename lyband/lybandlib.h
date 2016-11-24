
/***************************************************************************************************************
  BandLib.h
Author:yang yang 
Email:yang.yang@nanosic.com
----------------------------------------------------------------------------------------------------------------
version: V1.2
Updated: Add a mode for sleep test, check if the user may enter sleep mode.
releated fuction: SetJudgeInfo, GetIsSleep
----------------------------------------------------------------------------------------------------------------
version: V1.3
Updated: Longer the value updated time for BandProcess(longer to 1M) and LowpowerBandProcess(longer to 2M) 
----------------------------------------------------------------------------------------------------------------
version:            V1.4 2013.08.15
Updated:            1) Optimize the heartbeat algorithm; 
2) Add a return mode for Sleep Quality Judge, 
releated function:  SetSleepQualInfo() / GetSleepQuality()/LowPowerBandProcess()
----------------------------------------------------------------------------------------------------------------
version:            V1.4.1 2013.08.16
Updated:            Shortern the report period for sleep quality to 15M. 
Releated function:  SetSleepQualInfo() / GetSleepQuality()/LowPowerBandProcess()
----------------------------------------------------------------------------------------------------------------
version:            V1.5 2013.09.06
Updated:            Add Game mode for user, now four mode is supported: BOXMODE/ BALLMODE/ SITUPMODE/ BRMMODE
Releated function:  GameProcess(), GetGame();ClearGame();
----------------------------------------------------------------------------------------------------------------
version:            V1.6 2013.09.24
Updtaed:            1) Change the sample rate for Normal mode(20ms -> 50ms) and Sleep mode(200ms->500ms)
2) Delete all  Game mode for user
Releated funtciton: BandProcess()/ LowPowerBandProcess();  GameProcess(), GetGame();ClearGame();
----------------------------------------------------------------------------------------------------------------
version:            V1.7 2013.11.14
Updated:            
In normal mode:
1) Change the sample rate for Normal mode(50ms->20ms)  
2) Change the info report time to 2s,including step/ distance/cal
3) Change the sport mode report time to 5s/ 10s/ 20s
4) Change the sleep judge report time to 1M / 10M / 20M 

In sleep mode:
1) Change the sleep info report time to 5M 
2) Change the sleep quality report time to 10M 

Releated funtciton: SetJudgeInfo() / BandProcess() / LowPowerBandProcess()  
----------------------------------------------------------------------------------------------------------------
version:            V1.7 2013.11.28
Updated:            1) Use SetNormalMode() to set the THR value and mode for sport mode and IsSleep                             
2) Use SetSleepMode() to set the THR vaule and mode for sleep quality and IsAwake


Releated funtciton: SetNormalMode() / SetSleepMode() 
----------------------------------------------------------------------------------------------------------------
version:            V1.8 2013.12.11
Updated:            1) Change the  "retun 2" mode: if there is new step every 6s (Ex is 5s), it return 2, otherwise return 0                             
2) "GetStep()" should only be called when the funciton return 2;

Releated funtciton: BandProcess() / GetStep()
----------------------------------------------------------------------------------------------------------------
version:            V1.8 2013.12.20
Updated:            Change the sample rate for Normal mode(20ms->40ms)  

Releated funtciton: BandProcess() / GetStep()
----------------------------------------------------------------------------------------------------------------
version:            V1.9 2014.04.04
Updated:            Optimize the sleep detect algorithm
Solved a bug in step, 6s->8s

Releated funtciton: LowPowerBandProcess()
----------------------------------------------------------------------------------------------------------------
version:            V1.9 2014.04.10
Updated:            Open parameter for SPORT AND SLEEP thr

Releated funtciton:  BandInit();
----------------------------------------------------------------------------------------------------------------
version:            V1.9 2014.04.16
Updated:            Add an mode in BandProcess for normal algorithm or low power algorithm                      

Releated funtciton:  BandInit();
----------------------------------------------------------------------------------------------------------------
version:            V1.9 2014.05.13
Updated:            Add a mode for Jump                  

Releated funtciton:   JumpProcess
----------------------------------------------------------------------------------------------------------------
version:            V1.9 2014.05.16
Updated:            Add a BandProcessSimple() for lowpower while resting                 

Releated funtciton:   BandProcessSimple()  / BandProcess()
	----------------------------------------------------------------------------------------------------------------
	version:            V1.9 2014.05.21
	Updated:            Fix the problem for step avaliable while taking a bus, add a parameter for control it in BandInit          

	Releated funtciton:  BandInit
	----------------------------------------------------------------------------------------------------------------
	version:            V2.0 2016.01.15
	Updated:           Add   an parameter as thr to control step

	Releated funtciton:  BandProcess



	Code Cost: 5426 bytes of Code
	Ram  Cost:  811  bytes of Xdata
	***************************************************************************************************************/

//extern Person PersonStr;
//extern long count_steps_hip;
//extern char Regulation_mode;

/**********************************************************
Name    : BandInit
Function: Init function of the Band Recoder
Other   : None
 ***********************************************************/
extern void* BandInit(
		short SportThr   // The threshold value for updata a new Step value ;
		);

/********************************************************************************************************************
Name       : BandProcess
Function   : Count the step / distance / calorie cost of user
Other      : This funciton should be called once every 40ms
return value: 0: Nothing
2: All the value related to step are updated, Just read " Person.Step "

Thr:  thr for step detect, orignal val is 158,  the higher the value, the harder we can detect a step, do not set the val higher than 

 *********************************************************************************************************************/
//extern unsigned char BandProcess(short ax, short ay, short az, void* Band, unsigned char* Sportmode, unsigned char *Step, int Thr);
extern unsigned char BandProcess(short ax, short ay, short az, void* Band, unsigned char* Step);

/********************************************************************************************************************
Name    : SetNormalMode
Function: Set the infomation of user's sport mode
Other   : Should be called after the "BandInit" funciton
 ********************************************************************************************************************/
extern  void BandSetNormalMode(
		short RunThr,       // Threshold for runnning in m/s Normally 5~6 m/s is recommanded
		short JogThr,       // Threshold for Jogging  in m/s Normally 3~4 m/s is recommanded
		short WalkThr,      // Threshold for Walking  in m/s Normally 2~3 m/s is recommanded
		short ActiveThr     // Threshold for Sleeping in m/s Normally 0.1~0.5m/s is recommanded
		);

/********************************************************************************************************************
Name    : ClearBand
Function: Reset the data (step, distance,speed,calorie cost) for user
Other   : Call if needed; Ex, once the use upload, it can be called 
At least, it should be called once per week
 ********************************************************************************************************************/
extern void BandClear(void);

/********************************************************************************************************************
Name    : BandHandOver
Function: 
Other   : 
 ********************************************************************************************************************/
extern unsigned char BandHandOver(short ax, short ay, short az);

