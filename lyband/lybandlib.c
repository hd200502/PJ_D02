/*
Author:  yang yang
Email:   yang.yang@nanosic.com
Band.c:  An algorithm to calculate step/ distance/ speed/ calorie cost for user with accelerometer 
 */

#include <linux/unistd.h>
#include "lybandlib.h"

typedef struct Personality
{   
	short  RunThr, JogThr, WalkThr, ActiveThr;
	short  weight, height;
	long   Step,  ExStep;
	long   StepOrig;
	unsigned char DeltaStep8s;
}Person;

typedef struct
{
	long     DymaticCnt;
	long     timer8s;
	long     timer40ms;
	int      DeltaTHR;
	Person*  Perptr;
	long     SampleNew;
	long     SampleOld;
	long     PointCnt;
	long     Sa[3];
	long     AvgSa[3];
	long     LastSa[3];
}PSmartBand;

#define NUM_SAM 100

#define MaxTHR(x,y)  ( (x>y)? (x) :(y) )
#define MinTHR(x,y)  ( (x<y)? (x) :(y) )

#define AWAKEMODE 0
#define SLEEPMODE 1

#define BRMTHR    100
#define STEPMIN   1
#define STEPMAX   100

static long  Delta;
static long  smoothWin[3][4];
static short Abuf[3][NUM_SAM];
static short MaxSx, MaxSy, MaxSz;
static short MinSx, MinSy, MinSz;

//static short oldvalue,newvalue;
static unsigned char AxFlag, ExAxFlag;
static char xcnt = 0;
static char ycnt = 0;
static char zcnt = 0;
static char flag = 0;

static Person     PersonStr;
static PSmartBand BandStr;

static short FABb(short x1, short x2)
{
	short temp; 

	temp = x1 -x2;

	if(temp > 0)
		return temp;
	else 
		return (-temp);
}

static void memsetWn(unsigned char* dst, int val, int size)
{
	int i;
	for(i = 0 ; i < size; i++)	dst[i] = val;
}


void* BandInit(short pTHR)
{
	Person*     Per;
	PSmartBand* Band;

	Band = &BandStr;
	Per  = &PersonStr;

	memsetWn((unsigned char*)Band, 0, sizeof(BandStr));        
	memsetWn((unsigned char*)Per, 0, sizeof(BandStr));

	Band->Perptr = &PersonStr;
	Band->DeltaTHR = 150;

	MaxSx = MaxSy = MaxSz = -32768;
	MinSx = MinSy = MinSz =  32767;	

	return (void*)Band;
}

void BandSetNormalMode(short RunThr,short JogThr,short WalkThr,short ActiveThr)
{
	PersonStr.RunThr         = RunThr;
	PersonStr.JogThr         = JogThr;
	PersonStr.WalkThr        = WalkThr;       
	PersonStr.ActiveThr      = ActiveThr;
}

void BandClear(void)
{
	int thr ;

	short RunThr;short JogThr;short WalkThr;short ActiveThr;

	//store the infomation for user
	thr = BandStr.DeltaTHR;

	/*   
	     we  = PersonStr.weight;
	     he  = PersonStr.height; 
	 */

	RunThr        = PersonStr.RunThr;
	JogThr        = PersonStr.JogThr;
	WalkThr       = PersonStr.WalkThr;       
	ActiveThr     = PersonStr.ActiveThr;

	// clear the 2 strutct
	memsetWn((unsigned char*)&BandStr, 0, sizeof(BandStr));       
	memsetWn((unsigned char*)&PersonStr,0 , sizeof(PersonStr));	

	BandStr.Perptr   = &PersonStr;
	// restore the infomation fore user      
	BandStr.DeltaTHR = thr;

	PersonStr.RunThr         = RunThr;
	PersonStr.JogThr         = JogThr;
	PersonStr.WalkThr        = WalkThr;       
	PersonStr.ActiveThr      = ActiveThr;            
}


/*
smoothfilter:
smooth the input of accelerometer
 */
static void smoothfilter(short acc_x,short acc_y,short acc_z, PSmartBand* Band)
{ 
	int i;

	for(i = 3; i >0; i-- )
	{
		smoothWin[0][i] = smoothWin[0][i-1];
		smoothWin[1][i] = smoothWin[1][i-1];
		smoothWin[2][i] = smoothWin[2][i-1];
	} 

	smoothWin[0][0] = acc_x << 4;
	smoothWin[1][0] = acc_y << 4;
	smoothWin[2][0] = acc_z << 4;   

	Band->Sa[0] =  (smoothWin[0][0] + smoothWin[0][1] +smoothWin[0][2]+smoothWin[0][3])>>2;
	Band->Sa[1] =  (smoothWin[1][0] + smoothWin[1][1] +smoothWin[1][2]+smoothWin[1][3])>>2;
	Band->Sa[2] =  (smoothWin[2][0] + smoothWin[2][1] +smoothWin[2][2]+smoothWin[2][3])>>2;

	Abuf[0][Band->DymaticCnt] = Band->Sa[0];
	Abuf[1][Band->DymaticCnt] = Band->Sa[1];
	Abuf[2][Band->DymaticCnt] = Band->Sa[2];
}

/*
   StepDEF
 */
static void StepDEF(PSmartBand* Band, int i)
{    

	if((AxFlag!=ExAxFlag)&&(!i ))
	{
		Band->SampleNew = Band->Sa[AxFlag];      
		return ;
	}

	Delta = FABb(Band->Sa[AxFlag] , Band->SampleOld );

	if(Delta > BRMTHR)
	{
		Band->SampleNew = Band->Sa[AxFlag];         
	}   

	//Decide if One Step is available        
	if( (( Band->SampleNew < Band->AvgSa[AxFlag]) && (Band->SampleOld > Band->AvgSa[AxFlag]))
			// ||   (( Band->SampleNew > Band->AvgSa[AxFlag]) && (Band->SampleOld < Band->AvgSa[AxFlag]))
	  )
	{                    
		if((Band->PointCnt >= STEPMIN)  && (Band->PointCnt <= STEPMAX))
		{
			Band->Perptr->Step++;
			Band->PointCnt = 0;
		}
	}     

	Band->SampleOld = Band->SampleNew;        
	Band->PointCnt++;                    
}

/*
   DymaticTHR

   Generate THR for 3 axis acc every NUM_SAM times
 */
static unsigned char DymaticTHR(PSmartBand* Band, int mode)
{	 	
	int i;
	unsigned char rev;

	rev = 0;

	MaxSx = MaxTHR(Band->Sa[0], MaxSx);
	MaxSy = MaxTHR(Band->Sa[1], MaxSy);
	MaxSz = MaxTHR(Band->Sa[2], MaxSz);

	MinSx = MinTHR(Band->Sa[0], MinSx);
	MinSy = MinTHR(Band->Sa[1], MinSy);
	MinSz = MinTHR(Band->Sa[2], MinSz);

	if(Band->DymaticCnt == (NUM_SAM-1))
	{ 
		Band->AvgSa[0] =  (MaxSx+MinSx)>>1;
		Band->AvgSa[1] =  (MaxSy+MinSy)>>1; 
		Band->AvgSa[2] =  (MaxSz+MinSz)>>1; 

		Band->DymaticCnt = 0;

		ExAxFlag = AxFlag;

		if((xcnt > ycnt)&&(xcnt>zcnt))
			AxFlag = 0;
		if((ycnt > xcnt)&&(ycnt>zcnt))
			AxFlag = 1;
		if((zcnt > xcnt)&&(zcnt>ycnt))
			AxFlag = 2;

		xcnt = ycnt = zcnt = 0;            
		Band->PointCnt = 0;

		for(i = 0; i < NUM_SAM; i++)
		{
			Band->Sa[AxFlag] =  Abuf[AxFlag][i];

			switch(mode)
			{
				case AWAKEMODE:
					StepDEF(Band, i);
					break; 

				case SLEEPMODE: 
					//TurnDEF(Band, i);                
					break;

				default:
					break;              
			}
		}
		MaxSx = MaxSy = MaxSz = -32768;
		MinSx = MinSy = MinSz =  32767;
		rev = 1;
	}
	else 
	{ 
		Band->DymaticCnt++; 
	}

	Delta = FABb(Band->LastSa[0],Band->Sa[0]) ;
	flag  = 0;

	if (Delta  < FABb(Band->LastSa[1], Band->Sa[1]))
	{		 
		Delta = FABb(Band->LastSa[1] , Band->Sa[1]);	 
		flag  = 1;                            
	}

	if (Delta  < FABb(Band->LastSa[2] , Band->Sa[2]))
	{
		Delta = FABb(Band->LastSa[2], Band->Sa[2]);	                 
		flag  = 2;                       
	} 

	Band->LastSa[0] = Band->Sa[0];
	Band->LastSa[1] = Band->Sa[1];
	Band->LastSa[2] = Band->Sa[2];     

	switch(flag)
	{
		case 0:  xcnt++; break;
		case 1:  ycnt++; break;
		case 2:  zcnt++; break;
		default: break;
	}
	return rev;
}

// Normal processor, step, distance 
unsigned char BandProcess(short ax, short ay, short az, void* Band, unsigned char* Step)
{
	PSmartBand*Bandptr;

	Bandptr = ( PSmartBand*)Band;

	smoothfilter(ax, ay, az, Bandptr);
	DymaticTHR(Bandptr, AWAKEMODE);

	*Step = Bandptr->Perptr->Step;
	Bandptr->Perptr->Step = 0;

	return 0;
}

unsigned char BandHandOver(short ax, short ay, short az)
{
	static short x=0;
	static short y=0;
	static short z=0;
	unsigned char res = 0;
	const unsigned char max = 100;

	if ((FABb(x, ax) > max)	|| (FABb(y, ay) > max) || (FABb(z, az) > max))
	{
		if (x==0 && y==0 && z==0)
			res = 0;
		else
			res = 1;
		x = ax;
		y = ay;
		z = az;
	}
	return res;
}

