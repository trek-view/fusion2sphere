#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "bitmaplib.h"
#include "jpeglib.h"

#define ABS(x) (x < 0 ? -(x) : (x))
#define SIGN(x) (x < 0 ? (-1) : 1)
#define MIN(x,y) (x < y ? x : y)
#define MAX(x,y) (x > y ? x : y)
#define EPS 0.00001

typedef struct {
	int axis;
	double value;
	double cvalue,svalue;
} TRANSFORM;

typedef struct {
   double r,g,b;
} COLOUR;
typedef struct {
   double h,s,v;
} HSV;

typedef struct {
   double x,y,z;
} XYZ;

typedef struct {
	int index;
} UV;

typedef struct {
	char fname[256];
   BITMAP4 *image;
   int width,height;
   int centerx,centery;
   int radius;
	int hflip,vflip;
   double fov;
   TRANSFORM *transform;
   int ntransform;
} FISHEYE;

typedef struct {
	int debug;
	int antialias;             // Super sampling antialiasing
	double blendmid;           // Blending midpoint
	double blendwidth;         // Width of blending, angle
	double blendpower;         // For S curve blending
	int outwidth,outheight;    // Size of output equirectangular
	int icorrection;           // Perform intensity correction or not
	double ifcn[6];            // Coefficients of 5th order polynomial brightness correction
                              // 1 + a[1]x + a[2]x^2 + a[3]x^3 + a[4]x^4 + a[5]x^5
                              // Should map from 0 to 1 to 0 to generally 1+delta

	int makeremap;             // Create remap filters for ffmpeg (just fish2sphere mapping)

	int fileformat;            // Input image format

	// For experimental optimisations
	double deltafov;           // Variation of fov
	int deltacenter;           // Variation of fisheye center coordinates
	double deltatheta;         // Variation of rotations
} PARAMS;


typedef struct {
   int width,height;
   int sidewidth;
   int centerwidth;
   int blendwidth;
	int equiwidth;
} FRAMESPECS;

#define TRUE  1
#define FALSE 0

#define XTILT 0 // x
#define YROLL 1 // y
#define ZPAN  2 // z

// Prototypes
void GiveUsage(char *);
void InitFisheye(FISHEYE *);
void FisheyeDefaults(FISHEYE *);
int FindFishPixel(int,double,double,int *,int *,COLOUR *);
double GetTime(void);
void DumpParameters(void);
int ReadParameters(char *);
int WriteOutputImage(char *,char *);
double CalcError(COLOUR,COLOUR,double);
void MakeRandomRotations(void);
COLOUR HSV2RGB(HSV);
HSV RGB2HSV(COLOUR);
void InitParams(void);
void FlipFisheye(FISHEYE);
XYZ RotateX(XYZ,double);
XYZ RotateY(XYZ,double);
XYZ RotateZ(XYZ,double);
int CheckTemplate(char *,int);
int CheckFrames(char *,char *,int *,int *);
