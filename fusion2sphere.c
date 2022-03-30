#include "fusion2sphere.h"

/*
	Convert a dual fisheye to spherical map.
	Initially based upon fish2sphere.
	Documentation here: http://paulbourke.net/dome/dualfish2sphere/
	Feb 2017: Added jpg support
	Apr 2017: Updated general image file support
		 		 General cleanup of "old" testing code
	May 2017: Added optimisation algorithm, looks at pixels across seam
				 while random searching parameter space.
	Jun 2017: Experimental brightness correction across seam
	Jun 2017: Added support for assymetric fisheye lenses
	Oct 2017: Added overwriting of input file for sequences, -f
	Mar 2018: Added mirror flipping of the image, arise from the Insta360 camera.
             Cleaned up parameter handling
   May 2020: Added transformations that are applied to final equirectangular
   Feb 2022: Added batch support
   Feb 2022: Add remap filters for ffmpeg, just the fish2sphere part. See documentation for blending
*/

FISHEYE fisheye[2];           // Input fisheye
PARAMS params;                // General parameters
BITMAP4 *spherical = NULL;    // Output image

// These are known frame templates
// The appropriate one to use will be auto detected, error is none match
#define NTEMPLATE 3

FRAMESPECS template[NTEMPLATE] = {{3104,3000,0,0,0,0},{2704,2624,0,0,0,0},{1568,1504,0,0,0,0}};
int whichtemplate = -1;  

// Lookup table
typedef struct {
   UV uv;
} LLTABLE;
LLTABLE *lltable = NULL;

int readJPGFast(FISHEYE *fJPG)
{
	FILE *fimg;
	if ((fimg = fopen(fJPG->fname,"rb")) == NULL) {
		fprintf(stderr,"   Failed to open image file \"%s\"\n",fJPG->fname);
		return(FALSE);
	}
	//JPEG_Info(fimg, &fJPG->width, &fJPG->height,&d);
	if (JPEG_Read(fimg, fJPG->image,&fJPG->width,&fJPG->height) != 0) {
		fprintf(stderr,"   Failed to correctly read image \"%s\"\n",fJPG->fname);
		return(FALSE);
	}
	return(TRUE);
}

int readJPG(FISHEYE *fJPG)
{
	FILE *fimg;
	int w,h,d;
	if ((fimg = fopen(fJPG->fname,"rb")) == NULL) {
		fprintf(stderr,"   Failed to open image file \"%s\"\n",fJPG->fname);
		return(FALSE);
	}
	JPEG_Info(fimg, &fJPG->width, &fJPG->height,&d);
	fJPG->image = Create_Bitmap(fJPG->width, fJPG->height);
	if (JPEG_Read(fimg, fJPG->image,&w,&h) != 0) {
		fprintf(stderr,"   Failed to correctly read image \"%s\"\n",fJPG->fname);
		return(FALSE);
	}
	return(TRUE);
}

int WriteOutputImageBatch(BITMAP4 *spherical, char *basename,char *s)
{
	int i;
	FILE *fptr;
	char fname[256];

	if (IsJPEG(s)) { // remove extension
		for (i=strlen(s)-1;i>0;i--) {
			if (s[i] == '.') {
				s[i] = '\0';
				break;
			}
		}
	}
	strcpy(fname,s);

	// Add extension
	strcat(fname,".jpg");
	// Open file
   if ((fptr = fopen(fname,"wb")) == NULL) {
      fprintf(stderr,"Failed to open output file \"%s\"\n",fname);
      return(FALSE);
   }

    JPEG_Write(fptr,spherical,params.outwidth,params.outheight,100);

    fclose(fptr);
	return(TRUE);
}

/*
	Given a longitude and latitude calculate the rgb value from the fisheye
	Return FALSE if the pixel is outside the fisheye image
*/
int FindFishPixelBatch(int n,double latitude,double longitude,UV *uv, int width, int height)
{
	char ind[256];
	int k,index;
	XYZ p,q = {0,0,0};
	double theta,phi,r;
	int u,v;
	UV fuv;

   // Ignore pixels that will never be touched because out of blend range
   if (n == 0) {
      if (longitude > params.blendmid + params.blendwidth || longitude < -params.blendmid - params.blendwidth)
			return(FALSE);
	}
   if (n == 1) {
      if (longitude > -params.blendmid + params.blendwidth && longitude < params.blendmid - params.blendwidth)
			return(FALSE); 
   }

	// Turn by 180 degrees for the second fisheye
	if (n == 1) {
		longitude += M_PI;
		//printf("\nturn 180 deg %lf\n", longitude);
	}

   // p is the ray from the camera position into the scene
   p.x = cos(latitude) * sin(longitude);
   p.y = cos(latitude) * cos(longitude);
   p.z = sin(latitude);

   

   // Apply fisheye correction transformation
   for (k=0;k<fisheye[n].ntransform;k++) {
	   printf("Apply fisheye correction transformation");
      switch(fisheye[n].transform[k].axis) {
      case XTILT:
		   q.x =  p.x;
   		q.y =  p.y * fisheye[n].transform[k].cvalue + p.z * fisheye[n].transform[k].svalue;
   		q.z = -p.y * fisheye[n].transform[k].svalue + p.z * fisheye[n].transform[k].cvalue;
         break;
      case YROLL:
		   q.x =  p.x * fisheye[n].transform[k].cvalue + p.z * fisheye[n].transform[k].svalue;
		   q.y =  p.y;
		   q.z = -p.x * fisheye[n].transform[k].svalue + p.z * fisheye[n].transform[k].cvalue;
         break;
      case ZPAN:
		   q.x =  p.x * fisheye[n].transform[k].cvalue + p.y * fisheye[n].transform[k].svalue;
		   q.y = -p.x * fisheye[n].transform[k].svalue + p.y * fisheye[n].transform[k].cvalue;
		   q.z =  p.z;
         break;
      }
		p = q;
   }

   // Calculate fisheye coordinates
   theta = atan2(p.z,p.x);
   phi = atan2(sqrt(p.x*p.x+p.z*p.z),p.y);
   r = phi / fisheye[n].fov; // 0 ... 1

   // Determine the u,v coordinate
   u = fisheye[n].centerx + fisheye[n].radius * r * cos(theta);
   //printf("u: %d, fisheye[n].width: %d", u, fisheye[n].width);
   if (u < 0 || u >= width)
      return(FALSE);

   v = fisheye[n].centery + fisheye[n].radius * r * sin(theta);

   if (v < 0 || v >= height)
       return(FALSE);
	index = v * width + u;
	
	sprintf(ind,"%d%d",index, n);
	fuv.index = atoi(ind);

	*uv = fuv;

	return(TRUE);
}

int startDirectoryExtraction(int argc, char **argv, char *front, char *back, char *out, int nstart, int nstop){
	char fname1[256], fname2[256];
	int width=0, height=0;

	int i,j,aj,ai,n=0;
	int index,nantialias[2];
	int inblendzone = FALSE;
	char basename[256];
	BITMAP4 black = {0,0,0,255};
	double latitude0,longitude0,latitude,longitude; 
	double blend = 1;
	COLOUR rgbsum[2],rgbzero = {0,0,0};
	int noptiterations = 1; // > 1 for optimisation
	char fnameout[256],tablename[256];
	FILE *fptr;
	int ntable = 0;
	int itable = 0;

	int nframe, nt=0, nn = 0;
	double dx,dy;

	if(inblendzone != FALSE){
		inblendzone = FALSE;
	}
	

	if ((strlen(front) > 2) && (strlen(back) > 2) && (strlen(out) > 2)) {
		if (!CheckTemplate(front,1))     
			front[0] = '\0';
		if (!CheckTemplate(back,1))     
			back[0] = '\0';
		if (!CheckTemplate(out,1))     
			out[0] = '\0';
		
		if( !((CheckTemplate(front,1) == TRUE) && (CheckTemplate(back,1)  == TRUE) && (CheckTemplate(out,1) == TRUE))){
			exit(-1);
		}
	}
	else{
		exit(-1);
	}

   // Check the first frame to determine template and frame sizes
	sprintf(fname1,front,nstart);
	sprintf(fname2,back,nstart);

	if ((whichtemplate = CheckFrames(fname1,fname2,&width,&height)) < 0)
		exit(-1);
	if (params.debug) {
		fprintf(stderr,"%s() - frame dimensions: %d x %d\n",argv[0],width,height);
		fprintf(stderr,"%s() - Expect frame template %d\n",argv[0],whichtemplate+1);
	}

	fisheye[0].width = width;
	fisheye[0].height = height;
	fisheye[1].width = width;
	fisheye[1].height = height;

   // Memory for images
   fisheye[0].image = Create_Bitmap(width,height);
   fisheye[1].image = Create_Bitmap(width,height);

   // Create output spherical (equirectangular) image
   spherical = Create_Bitmap(params.outwidth,params.outheight);

   // Read parameter file name
   if (!ReadParameters(argv[argc-1])) {
      fprintf(stderr,"Failed to read parameter file \"%s\"\n",argv[argc-1]);
      exit(-1);
   }

	// Apply defaults and precompute values
	FisheyeDefaults(&fisheye[0]);
	FisheyeDefaults(&fisheye[1]);
	FlipFisheye(fisheye[0]);
	FlipFisheye(fisheye[1]);

	// Create output spherical (equirectangular) image
	spherical = Create_Bitmap(params.outwidth,params.outheight);

	// Must have blending on for optimisation
	if (noptiterations > 1 && params.blendwidth <= 0) {
		fprintf(stderr,"Warning: Must enable blending for optimisation, setting to 6 degrees\n");
		params.blendwidth = 3*DTOR;
	}



	if (params.debug)
		DumpParameters();

	ntable = params.outheight * params.outwidth * params.antialias * params.antialias * 2;
	lltable = malloc(ntable*sizeof(LLTABLE));
	sprintf(tablename,"f_%d_%d_%d_%d.data",whichtemplate,params.outwidth,params.outheight,params.antialias);

	if ((fptr = fopen(tablename,"r")) != NULL) {
		if (params.debug)
			fprintf(stderr,"%s() - Reading lookup table\n",argv[0]);
		if ((nt = fread(lltable,sizeof(LLTABLE),ntable,fptr)) != ntable) {
			fprintf(stderr,"%s() - Failed to read lookup table \"%s\" (%d != %d)\n",argv[0],tablename,nt,ntable);
		}
		fclose(fptr);
	}

	dx = params.antialias * params.outwidth;
	dy = params.antialias * params.outheight;

	if (nt != ntable) {
		itable = 0;

		for (j=0;j<params.outheight;j++) {
			latitude0 = PI * j / (double)params.outheight - PID2; // -pi/2 ... pi/2
			for (i=0;i<params.outwidth;i++) {
				longitude0 = TWOPI * i / (double)params.outwidth - PI; // -pi ... pi
				for (ai=0;ai<params.antialias;ai++) {
					longitude = longitude0 + ai * TWOPI / dx;
					for (aj=0;aj<params.antialias;aj++) {
						latitude = latitude0 + aj * M_PI / dy;
						for (n=0;n<2;n++) {
							if(FindFishPixelBatch(n,latitude,longitude,&(lltable[itable].uv), width, height)) {
								itable++;
							}
						}
					} // aj
				} // ai
				lltable[itable].uv.index = -1;
				itable++;
			} // i
		} // j

		fptr = fopen(tablename,"w");
		fwrite(lltable,ntable,sizeof(LLTABLE),fptr);
		fclose(fptr);

	}

	for (nframe=nstart;nframe<=nstop;nframe++) {

		sprintf(fisheye[0].fname,front,nframe);
		sprintf(fisheye[1].fname,back,nframe);

		sprintf(fnameout,out,nframe);

		if (IsJPEG(fisheye[0].fname)){
			if(1 != readJPGFast(&fisheye[0])){
				continue;
			}
		}
		if (IsJPEG(fisheye[1].fname)){
			if(1 != readJPGFast(&fisheye[1])){
				continue;
			}
		}
		
		itable = 0;
		Erase_Bitmap(spherical,params.outwidth,params.outheight,black);

		for (j=0;j<params.outheight;j++) {
			latitude0 = PI * j / (double)params.outheight - PID2; // -pi/2 ... pi/2
			for (i=0;i<params.outwidth;i++) {
				longitude0 = TWOPI * i / (double)params.outwidth - PI; // -pi ... pi

				// Blending masks, only depend on longitude
				if (params.blendwidth > 0) {
					blend = (params.blendmid + params.blendwidth - fabs(longitude0)) / (2*params.blendwidth); // 0 ... 1
					if (blend < 0) blend = 0;
					if (blend > 1) blend = 1;
					if (params.blendpower > 1) {
							blend = 2 * blend - 1; // -1 to 1
						blend = 0.5 + 0.5 * SIGN(blend) * pow(fabs(blend),1.0/params.blendpower);
						}
				} else { // No blend
					blend = 0;
					if (ABS(longitude0) <= params.blendmid) // Hard edge
						blend = 1;
				}
		
				// Are we in the blending zones
				inblendzone = FALSE;
				if (longitude0 <= params.blendmid + params.blendwidth && longitude0 >= params.blendmid - params.blendwidth)
					inblendzone = TRUE;
				if (longitude0 >= -params.blendmid - params.blendwidth && longitude0 <= -params.blendmid + params.blendwidth)
					inblendzone = TRUE;


				//printf("\n %lf, %lf, %lf\n", params.blendwidth, params.blendmid, params.blendpower);// 0.000000, 1.570796, 1.000000

				// Initialise antialiasing accumulation variables
				for (n=0;n<2;n++) {
					rgbsum[n] = rgbzero;
					nantialias[n] = 0;
				}

				for (n=0;n<100;n++) {
					nn = 0;
					index = lltable[itable].uv.index;
					if(index == -1){
						itable++;
						break;
					}
					nn = index%10;
					index = index/10;
					//if(nn == 0){printf("\nnn: %d, index: %d, itable: %d\n", nn, index, itable);}
					
					rgbsum[nn].r += fisheye[nn].image[index].r;
					rgbsum[nn].g += fisheye[nn].image[index].g;
					rgbsum[nn].b += fisheye[nn].image[index].b;
					nantialias[nn]++;

					itable++;
				}


				// Normalise by antialiasing samples
				for (n=0;n<2;n++) {
					if (nantialias[n] > 0) {
						rgbsum[n].r /= nantialias[n];
						rgbsum[n].g /= nantialias[n];
						rgbsum[n].b /= nantialias[n];
					}
				}

				index = j * params.outwidth + i;
				spherical[index].r = blend * rgbsum[0].r + (1 - blend) * rgbsum[1].r;
	        	spherical[index].g = blend * rgbsum[0].g + (1 - blend) * rgbsum[1].g;
	        	spherical[index].b = blend * rgbsum[0].b + (1 - blend) * rgbsum[1].b;
				//printf("%d ", index);

				//printf("\nindex: %d, r: %d, g: %d, b: %d\n", index, spherical[index].r, spherical[index].g, spherical[index].b);

				//if(itable > 100){
					//break;
				//}

			} // i
			//break;
		} // j

		// Write out the spherical map 
		if (!WriteOutputImageBatch(spherical, basename,fnameout)) {
			fprintf(stderr,"Failed to write output image file\n");
			exit(-1);
		}

	}
	
	Destroy_Bitmap(spherical);
	Destroy_Bitmap(fisheye[0].image);
	Destroy_Bitmap(fisheye[1].image);

    return 0;
}

int main(int argc,char **argv)
{
	int i,j,aj,ai,n=0, sdir=0, nstart=0, nstop=0,ix,iy;
	int index,nantialias[2],inblendzone;
	char basename[256],outfilename[256] = "\0";
	BITMAP4 black = {0,0,0,255},red = {255,0,0,255};
	double latitude0,longitude0,latitude,longitude;
	double weight = 1,blend = 1;
	COLOUR rgb,rgbsum[2],rgbzero = {0,0,0};
	double starttime=0,stoptime=0;
	int nopt,noptiterations = 1; // > 1 for optimisation
	double fov[2];
	int centerx[2],centery[2];
	double r,theta,minerror=1e32,opterror=0,errorsum = 0;
	int nsave = 0;
	char fname[256], front[256], back[256];
	int nfish = 0;
	FILE *fptr;

	// Initial values for fisheye structure and general parameters
   InitParams();
   InitFisheye(&fisheye[0]);
   InitFisheye(&fisheye[1]);

	// Usage, require at least a parameter file
	if (argc < 2) 
		GiveUsage(argv[0]);

	// Create basename
	strcpy(basename,argv[argc-1]);
	for (i=0;i<strlen(basename);i++)
		if (basename[i] == '.')
			basename[i] = '\0';

	// Parse the command line arguments 
	for (i=1;i<argc;i++) {
		if (strcmp(argv[i],"-w") == 0) {
			i++;
			params.outwidth = atoi(argv[i]);
			params.outwidth /= 4; 
			params.outwidth *= 4; // Ensure multiple of 4
			params.outheight = params.outwidth / 2;     // Default for equirectangular images, will be even
		} else if (strcmp(argv[i],"-a") == 0) {
			i++;
			if ((params.antialias = atoi(argv[i])) < 1)
				params.antialias = 1;
		} else if (strcmp(argv[i],"-b") == 0) {
         i++;
         if ((params.blendwidth = DTOR*atof(argv[i])) < 0)
				params.blendwidth = 0;
			params.blendwidth /= 2; // Now half blendwith
		} else if (strcmp(argv[i],"-d") == 0) {
			params.debug = TRUE;
		} else if (strcmp(argv[i],"-q") == 0) {
			i++;
         params.blendpower = atof(argv[i]);
		} else if (strcmp(argv[i],"-o") == 0) {
			i++;
			strcpy(outfilename,argv[i]);
		} else if (strcmp(argv[i],"-f") == 0) {
			i++;
			strcpy(fisheye[0].fname,argv[i]);
			if (IsJPEG(fisheye[0].fname)){
				if(1 != readJPG(&fisheye[0])){
					exit(-1);
				}
				nfish +=1;
			}
			
			i++;
			strcpy(fisheye[1].fname,argv[i]);
			if (IsJPEG(fisheye[1].fname)){
				if(1 != readJPG(&fisheye[1])){
					exit(-1);
				}
				nfish +=1;
			}
			if(nfish == 2){
				params.fileformat = JPG;
			}
			else{
				fprintf(stderr,"Expected two fisheye images, instead found %d\n",nfish);
				exit(-1);
			}
		} else if (strcmp(argv[i],"-r") == 0) {
			params.makeremap = TRUE;
      	} else if (strcmp(argv[i],"-e") == 0) {
         i++;
         noptiterations = atoi(argv[i]);
		} else if (strcmp(argv[i],"-p") == 0) {
         i++;
         params.deltafov = DTOR*atof(argv[i]);
         i++;
         params.deltacenter = atoi(argv[i]);
         i++;
         params.deltatheta = DTOR*atof(argv[i]);
      } else if (strcmp(argv[i],"-i") == 0) {
         params.icorrection = TRUE;
      } else if (strcmp(argv[i],"-m") == 0) {
			i++;
         params.blendmid = atof(argv[i]);
			params.blendmid *= (DTOR*0.5);
        }
      else if (strcmp(argv[i],"-x") == 0) {
		sdir = 1;
		i++;
		strcpy(front,argv[i]);
		i++;
		strcpy(back,argv[i]);
	  } else if (strcmp(argv[i],"-g") == 0) {
		i++;
		nstart = atoi(argv[i]);
      } else if (strcmp(argv[i],"-h") == 0) {
		i++;
		nstop = atoi(argv[i]);
	  }
	}

    if(sdir == 1){
        startDirectoryExtraction(argc, argv, front, back, outfilename, nstart, nstop);
		// Optionally create ffmpeg remap filter PGM files
		if (params.makeremap)
			MakeRemap();
        exit(0);
    }

   // Read parameter file name
   if (!ReadParameters(argv[argc-1])) {
      fprintf(stderr,"Failed to read parameter file \"%s\"\n",argv[argc-1]);
      exit(-1);
   }

	// Apply defaults and precompute values
	FisheyeDefaults(&fisheye[0]);
	FisheyeDefaults(&fisheye[1]);
	FlipFisheye(fisheye[0]);
	FlipFisheye(fisheye[1]);

   // Create output spherical (equirectangular) image
   spherical = Create_Bitmap(params.outwidth,params.outheight);

	// Must have blending on for optimisation
	if (noptiterations > 1 && params.blendwidth <= 0) {
		fprintf(stderr,"Warning: Must enable blending for optimisation, setting to 6 degrees\n");
		params.blendwidth = 3*DTOR;
	}

	// Remember the baseline values
	for (j=0;j<2;j++) {
		fov[j]     = fisheye[j].fov;
		centerx[j] = fisheye[j].centerx;
		centery[j] = fisheye[j].centery;
	}

	if (params.debug)
		DumpParameters();

	// Optimisation loop, otherwise just do once if noptiterations=1
	for (nopt=0;nopt<noptiterations;nopt++) {

		// Only apply random perturbation after first iteration
		if (noptiterations > 1) {
			for (j=0;j<2;j++) {
   			fisheye[j].fov = fov[j] + (drand48() - 0.5) * params.deltafov; // Half
				r = drand48() * params.deltacenter;
				theta = drand48() * TWOPI;
				fisheye[j].centerx = centerx[j] + r * cos(theta);
				fisheye[j].centery = centery[j] + r * sin(theta);
			}
			MakeRandomRotations();
		}

     	if (noptiterations > 1 && nopt % (noptiterations/100==0?1:noptiterations/100) == 0)
     	   fprintf(stderr,"Optimisation step %8d of %8d\n",nopt,noptiterations);

		opterror = 0;
		errorsum = 0;

		// Form the spherical map 
		starttime = GetTime();
		Erase_Bitmap(spherical,params.outwidth,params.outheight,black);
      for (j=0;j<params.outheight;j++) {
         latitude0 = PI * j / (double)params.outheight - PID2; // -pi/2 ... pi/2

			for (i=0;i<params.outwidth;i++) {
				longitude0 = TWOPI * i / (double)params.outwidth - PI; // -pi ... pi
	
		      // Blending masks, only depend on longitude
		      if (params.blendwidth > 0) {
		         blend = (params.blendmid + params.blendwidth - fabs(longitude0)) / (2*params.blendwidth); // 0 ... 1
		         if (blend < 0) blend = 0;
		         if (blend > 1) blend = 1;
		         if (params.blendpower > 1) {
						blend = 2 * blend - 1; // -1 to 1
		            blend = 0.5 + 0.5 * SIGN(blend) * pow(fabs(blend),1.0/params.blendpower);
					}
		      } else { // No blend
		         blend = 0;
		         if (ABS(longitude0) <= params.blendmid) // Hard edge
		            blend = 1;
		      }
	
            // Are we in the blending zones
            inblendzone = FALSE;
            if (longitude0 <= params.blendmid + params.blendwidth && longitude0 >= params.blendmid - params.blendwidth)
               inblendzone = TRUE;
            if (longitude0 >= -params.blendmid - params.blendwidth && longitude0 <= -params.blendmid + params.blendwidth)
               inblendzone = TRUE;
  
            // If optimising then only need to calculate image within the blend zone
            if (noptiterations > 1 && !inblendzone)
               continue;

				// Initialise antialiasing accumulation variables
				for (n=0;n<2;n++) {
					rgbsum[n] = rgbzero;
					nantialias[n] = 0;
				}
	
				// Antialiasing, inner loops
            // Find the corresponding pixel in the fisheye image
            // Sum over the supersampling set
	   		for (ai=0;ai<params.antialias;ai++) {
					longitude = longitude0 + ai * TWOPI / (params.antialias*params.outwidth);
	      		for (aj=0;aj<params.antialias;aj++) {
						latitude = latitude0 + aj * M_PI / (params.antialias*params.outheight);
						for (n=0;n<2;n++) {
							if (FindFishPixel(n,latitude,longitude,&ix,&iy,&rgb)) {
								rgbsum[n].r += rgb.r;
		               	rgbsum[n].g += rgb.g;
		               	rgbsum[n].b += rgb.b;
								nantialias[n]++;	
							}
						}
					} // aj
				} // ai
	
				// Normalise by antialiasing samples
				for (n=0;n<2;n++) {
					if (nantialias[n] > 0) {
						rgbsum[n].r /= nantialias[n];
	               rgbsum[n].g /= nantialias[n];
	               rgbsum[n].b /= nantialias[n];
					}
				}
	
				// Update antialiased value to final image with blending
				index = j * params.outwidth + i;
				spherical[index].r = blend * rgbsum[0].r + (1 - blend) * rgbsum[1].r;
	        	spherical[index].g = blend * rgbsum[0].g + (1 - blend) * rgbsum[1].g;
	        	spherical[index].b = blend * rgbsum[0].b + (1 - blend) * rgbsum[1].b;
	
				// Determine error metric if in optimisation mode
				// Experimental, weight higher if closer to the center of blend
				if (noptiterations > 1 && inblendzone) {
					//weight = 1;
					weight = 1 - 2 * fabs(0.5 - blend); // 0 to 1 in middle of blend to 0
					if (j > 0.2*params.outheight && j < 0.8*params.outheight) {
						opterror += CalcError(rgbsum[0],rgbsum[1],weight);
						errorsum += weight;
					}
				}
			} // i
		} // j
		stoptime = GetTime();
	
		// Write a parameter file, suitable for normal fusion2sphere usage
		opterror /= errorsum; // Normalise to "per pixel"
		if (noptiterations > 1 && opterror < minerror) {
	      sprintf(fname,"%s_%02d.txt",basename,nsave);
	      fptr = fopen(fname,"w");
			fprintf(fptr,"# Optimisation step %d of %d\n",nopt,noptiterations);
			fprintf(fptr,"# Error: %g\n",opterror);
			fprintf(fptr,"# delta fov: %g degrees\n",RTOD*params.deltafov);
			fprintf(fptr,"# delta center: %d pixels\n",params.deltacenter);
			fprintf(fptr,"# delta theta: %g degrees\n",RTOD*params.deltatheta);
			fprintf(fptr,"# blend width: %g degrees\n",RTOD*2*params.blendwidth);
			fprintf(fptr,"\n");
			for (j=0;j<2;j++) {
				fprintf(fptr,"# image %d\n",j);
	     		fprintf(fptr,"IMAGE: %s\n",fisheye[j].fname);
	     		fprintf(fptr,"RADIUS: %d\n",fisheye[j].radius);
	     		fprintf(fptr,"CENTER: %d %d\n",fisheye[j].centerx,fisheye[j].height-1-fisheye[j].centery);
				fprintf(fptr,"# Was: %d %d\n",centerx[j],fisheye[j].height-1-centery[j]);
	     		fprintf(fptr,"FOV: %.1lf\n",fisheye[j].fov*2*RTOD);
				fprintf(fptr,"# Was: %.1lf\n",fov[j]*2*RTOD);
				if (fisheye[j].hflip < 0)
					fprintf(fptr,"HFLIP: -1\n");
            if (fisheye[j].vflip < 0)
               fprintf(fptr,"VFLIP: -1\n");
				for (i=0;i<fisheye[j].ntransform;i++) {
					switch (fisheye[j].transform[i].axis) {
					case XTILT:
						fprintf(fptr,"ROTATEX: %.1lf\n",fisheye[j].transform[i].value*RTOD);
						break;
	         	case YROLL:
	         	   fprintf(fptr,"ROTATEY: %.1lf\n",fisheye[j].transform[i].value*RTOD);
	         	   break;
	         	case ZPAN:
	         	   fprintf(fptr,"ROTATEZ: %.1lf\n",fisheye[j].transform[i].value*RTOD);
	         	   break;
					}
				}
			}
			fclose(fptr);
			// Write image so far, will just be the blend strip
	      sprintf(fname,"%s_%02d",basename,nsave);
			WriteOutputImage(basename,fname);
			fprintf(stderr,"Optimisation step %8d of %8d Error: %5.1lf ",nopt,noptiterations,opterror);
			fprintf(stderr,"Saved to %s_%02d\n",basename,nsave);
			nsave++;
			minerror = opterror;  // Remember best so far
		}
	
	} // nopt

	// Timing and optionally show the blend range
	if (params.debug) {
		fprintf(stderr,"Time for sampling: %g\n",stoptime-starttime);
		for (j=0;j<params.outheight;j++) {
			i = params.outwidth / 4.0;
			Draw_Pixel(spherical,params.outwidth,params.outheight,i,j,red);
			i = 3 * params.outwidth / 4.0;
         Draw_Pixel(spherical,params.outwidth,params.outheight,i,j,red);
			if (params.blendwidth > 0) {
				i = params.outwidth / 4.0 + params.outwidth * params.blendwidth / TWOPI;
         	Draw_Pixel(spherical,params.outwidth,params.outheight,i,j,red);
         	i = params.outwidth / 4.0 - params.outwidth * params.blendwidth / TWOPI;
         	Draw_Pixel(spherical,params.outwidth,params.outheight,i,j,red);
         	i = 3 * params.outwidth / 4.0 + params.outwidth * params.blendwidth / TWOPI;
         	Draw_Pixel(spherical,params.outwidth,params.outheight,i,j,red);
         	i = 3 * params.outwidth / 4.0 - params.outwidth * params.blendwidth / TWOPI;
         	Draw_Pixel(spherical,params.outwidth,params.outheight,i,j,red);
			}
		}
	}

	// Optionally create ffmpeg remap filter PGM files
	if (params.makeremap)
		MakeRemap();
	
	// Write out the spherical map 
	if (!WriteOutputImage(basename,outfilename)) {
		fprintf(stderr,"Failed to write output image file\n");
		exit(-1);
	}

	exit(0);
}

void GiveUsage(char *s)
{
   fprintf(stderr,"Usage: %s [options] parameterfile\n",s);
   fprintf(stderr,"Options\n");
   fprintf(stderr,"   -w n      sets the output image size, default: %d\n",params.outwidth);
   fprintf(stderr,"   -a n      sets antialiasing level, default: %d\n",params.antialias);
	fprintf(stderr,"   -b n      longitude width for blending, default: %g\n",2*params.blendwidth);
	fprintf(stderr,"   -q n      blend power, default: %g\n",params.blendpower);
	fprintf(stderr,"   -e n      optimise over n random steps, default: off\n");
	fprintf(stderr,"   -p n n n  range search fov, center and rotations, default: %g %d %g\n",
		params.deltafov*RTOD,params.deltacenter,params.deltatheta*RTOD);
	fprintf(stderr,"   -i        enable intensity edge roll-off correction, default: off\n");
	fprintf(stderr,"   -f s1 s2  input filename, overwrite file specified in parameter file\n");
	fprintf(stderr,"   -o s      output file name, default: derived from input name\n");
	fprintf(stderr,"   -m n      specify blend mid angle, default: %g\n",RTOD*2*params.blendmid);
	fprintf(stderr,"   -d        debug mode, default: off\n");
	fprintf(stderr,"   -r        create remap filters for ffmpeg, default: off\n");
   exit(-1);
}

/*
	Set default values for fisheye structure
*/
void InitFisheye(FISHEYE *f)
{
   f->fname[0] = '\0';
   f->image = NULL;
   f->width = 0;
   f->height = 0;
   f->centerx = -1;
   f->centery = -1;
   f->radius = -1;
   f->fov = 180;
	f->hflip = 1;
	f->vflip = 1;
   f->transform = NULL;
   f->ntransform = 0;
}

/*
	Fill in remaining fisheye values
*/
void FisheyeDefaults(FISHEYE *f)
{
	int j;

	// fov will only be used as half value, and radians
   f->fov /= 2;    
   f->fov *= DTOR; 

	// Set center to image center, if not set in parameter file
   if (f->centerx < 0 || f->centery < 0) {
      f->centerx = f->width / 2;
      f->centery = f->height / 2;
   }

	// Origin bottom left
   f->centery = f->height - 1 - f->centery;

	// Set fisheye radius to half height, if not set in parameter file
   if (f->radius < 0)
      f->radius = f->height / 2;

   // Precompute sine and cosine of transformation angles
   for (j=0;j<f->ntransform;j++) {
      f->transform[j].cvalue = cos(f->transform[j].value);
      f->transform[j].svalue = sin(f->transform[j].value);
   }
}


/*
   Given a longitude and latitude calculate the rgb value from the fisheye
   Return FALSE if the pixel is outside the fisheye image
*/
int FindFishPixel(int n,double latitude,double longitude,int *u,int *v,COLOUR *rgb)
{
   int k,index;
   COLOUR c = {0,0,0};
   XYZ p,q = {0,0,0};
   double theta,phi,r;

	*u = -1;
	*v = -1;
	*rgb = c;
	
   // Ignore pixels that will never be touched because out of blend range
   if (n == 0) {
      if (longitude > params.blendmid + params.blendwidth || longitude < -params.blendmid - params.blendwidth)
         return(FALSE);
   }
   if (n == 1) {
      if (longitude > -params.blendmid + params.blendwidth && longitude < params.blendmid - params.blendwidth)
         return(FALSE); 
   }

   // Turn by 180 degrees for the second fisheye
   if (n == 1) {
      longitude += M_PI;
   }

   // p is the ray from the camera position into the scene
   p.x = cos(latitude) * sin(longitude);
   p.y = cos(latitude) * cos(longitude);
   p.z = sin(latitude);

   // Apply fisheye correction transformation
   for (k=0;k<fisheye[n].ntransform;k++) {
      switch(fisheye[n].transform[k].axis) {
      case XTILT:
         q.x =  p.x;
         q.y =  p.y * fisheye[n].transform[k].cvalue + p.z * fisheye[n].transform[k].svalue;
         q.z = -p.y * fisheye[n].transform[k].svalue + p.z * fisheye[n].transform[k].cvalue;
         break;
      case YROLL:
         q.x =  p.x * fisheye[n].transform[k].cvalue + p.z * fisheye[n].transform[k].svalue;
         q.y =  p.y;
         q.z = -p.x * fisheye[n].transform[k].svalue + p.z * fisheye[n].transform[k].cvalue;
         break;
      case ZPAN:
         q.x =  p.x * fisheye[n].transform[k].cvalue + p.y * fisheye[n].transform[k].svalue;
         q.y = -p.x * fisheye[n].transform[k].svalue + p.y * fisheye[n].transform[k].cvalue;
         q.z =  p.z;
         break;
      }
      p = q;
   }

   // Calculate fisheye coordinates
   theta = atan2(p.z,p.x);
   phi = atan2(sqrt(p.x*p.x+p.z*p.z),p.y);
   r = phi / fisheye[n].fov; // 0 ... 1

   // Determine the u,v coordinate
   *u = fisheye[n].centerx + fisheye[n].radius * r * cos(theta);
   if (*u < 0 || *u >= fisheye[n].width)
      return(FALSE);
   *v = fisheye[n].centery + fisheye[n].radius * r * sin(theta);
   if (*v < 0 || *v >= fisheye[n].height)
       return(FALSE);

	// Extract rgb colour
   index = (*v) * fisheye[n].width + (*u);
   rgb->r = fisheye[n].image[index].r;
   rgb->g = fisheye[n].image[index].g;
   rgb->b = fisheye[n].image[index].b;

   return(TRUE);
}

/*
	This s highly system/OS dependent
	Will need replacing for Windows.
*/
double GetTime(void)
{
   double sec = 0;
   struct timeval tp;

   gettimeofday(&tp,NULL);
   sec = tp.tv_sec + tp.tv_usec / 1000000.0;

   return(sec);
}

/*
	Purely for debugging
*/
void DumpParameters(void)
{
	int i,j;
		
   for (i=0;i<2;i++) {
		fprintf(stderr,"FISHEYE: %d\n",i);
      fprintf(stderr,"   IMAGE: %s (%dx%d)\n",fisheye[i].fname,fisheye[i].width,fisheye[i].height);
      fprintf(stderr,"   CENTER: %d,%d\n",fisheye[i].centerx,fisheye[i].height-1-fisheye[i].centery);
      fprintf(stderr,"   RADIUS: %d\n",fisheye[i].radius);
      fprintf(stderr,"   FOV: %g\n",fisheye[i].fov*2*RTOD);
		fprintf(stderr,"   HFLIP: %d\n",fisheye[i].hflip);
      fprintf(stderr,"   VFLIP: %d\n",fisheye[i].vflip);
      for (j=0;j<fisheye[i].ntransform;j++) {
         if (fisheye[i].transform[j].axis == XTILT)
            fprintf(stderr,"   ROTATEX: ");
         else if (fisheye[i].transform[j].axis == YROLL)
            fprintf(stderr,"   ROTATEY: ");
         else if (fisheye[i].transform[j].axis == ZPAN)
            fprintf(stderr,"   ROTATEZ: ");
         fprintf(stderr,"%.1lf\n",fisheye[i].transform[j].value*RTOD);
      }
   }
} 

/*
	Read the parameter file, loading up the FISHEYE structure
	Consists of keyword and value pairs, one per line
	This makes lots of assumptions, that is, is not very general and does not deal with edge cases
	Comment lines have # as the first character of the line
*/
int ReadParameters(char *s)
{
	int nfish = 0;
	int i,j,flip;
	char ignore[256],aline[256];
	double angle;
	FILE *fptr;

   if ((fptr = fopen(s,"r")) == NULL) {
      fprintf(stderr,"   Failed to open parameter file \"%s\"\n",s);
      return(FALSE);
   }
   while (fgets(aline,255,fptr) != NULL) {
      if (aline[0] == '#') // Comment line
         continue;
      if (strstr(aline,"IMAGE:") != NULL) {
		  nfish++;
		  continue;
		  /*
         if (nfish >= 2) {
            fprintf(stderr,"   Already found 2 fisheye images, cannot handle more\n");
            return(FALSE);
         }
			sscanf(aline,"%s %s",ignore,fname);
			if (strlen(fisheye[nfish].fname) > 0)
				strcpy(fname,fisheye[nfish].fname);
			if (IsJPEG(fname))
				params.fileformat = JPG;
			else
				params.fileformat = TGA;
         if ((fimg = fopen(fname,"rb")) == NULL) {
            fprintf(stderr,"   Failed to open image file \"%s\"\n",fname);
            return(FALSE);
         }
			if (params.fileformat == JPG)
				JPEG_Info(fimg,&fisheye[nfish].width,&fisheye[nfish].height,&d);
			else
         	TGA_Info(fimg,&fisheye[nfish].width,&fisheye[nfish].height,&d);
         fisheye[nfish].image = Create_Bitmap(fisheye[nfish].width,fisheye[nfish].height);
   		if (params.fileformat == JPG) {
      		if (JPEG_Read(fimg,fisheye[nfish].image,&w,&h) != 0) {
         		fprintf(stderr,"   Failed to correctly read image \"%s\"\n",fname);
         		return(FALSE);
      		}
   		} else {
         	if (TGA_Read(fimg,fisheye[nfish].image,&w,&h) != 0) {
         	   fprintf(stderr,"   Failed to correctly read image \"%s\"\n",fname);
         	   return(FALSE);
         	}
			}
         fclose(fimg);
			strcpy(fisheye[nfish].fname,fname);
         nfish++;
      
	  	*/
	  }
      if (strstr(aline,"RADIUS:") != NULL && nfish > 0) {
         sscanf(aline,"%s %d",ignore,&i);
         fisheye[nfish-1].radius = i;
      }
      if (strstr(aline,"CENTER:") != NULL && nfish > 0) {
         sscanf(aline,"%s %d %d",ignore,&i,&j);
         fisheye[nfish-1].centerx = i;
         fisheye[nfish-1].centery = j;
      }
      if (strstr(aline,"APERTURE:") != NULL && nfish > 0) { // Historical use, change to FOV
         sscanf(aline,"%s %lf",ignore,&angle);
         fisheye[nfish-1].fov = angle;
      }
      if (strstr(aline,"FOV:") != NULL && nfish > 0) {
         sscanf(aline,"%s %lf",ignore,&angle);
         fisheye[nfish-1].fov = angle;
      }
      if (strstr(aline,"HFLIP:") != NULL && nfish > 0) {
         sscanf(aline,"%s %d",ignore,&flip);
			if (flip < 0)
         	fisheye[nfish-1].hflip = -1;
			else 
				fisheye[nfish-1].hflip = 1;
      }
      if (strstr(aline,"VFLIP:") != NULL && nfish > 0) {
         sscanf(aline,"%s %d",ignore,&flip);
         if (flip < 0)
            fisheye[nfish-1].vflip = -1;
         else 
            fisheye[nfish-1].vflip = 1;
      }
      if (strstr(aline,"ROTATEX:") != NULL && nfish > 0) {
         sscanf(aline,"%s %lf",ignore,&angle);
         fisheye[nfish-1].transform =
            realloc(fisheye[nfish-1].transform,(fisheye[nfish-1].ntransform+1)*sizeof(TRANSFORM));
         fisheye[nfish-1].transform[fisheye[nfish-1].ntransform].axis = XTILT;
         fisheye[nfish-1].transform[fisheye[nfish-1].ntransform].value = DTOR*angle;
         fisheye[nfish-1].ntransform++;
      }
      if (strstr(aline,"ROTATEY:") != NULL && nfish > 0) {
         sscanf(aline,"%s %lf",ignore,&angle);
         fisheye[nfish-1].transform =
            realloc(fisheye[nfish-1].transform,(fisheye[nfish-1].ntransform+1)*sizeof(TRANSFORM));
         fisheye[nfish-1].transform[fisheye[nfish-1].ntransform].axis = YROLL;
         fisheye[nfish-1].transform[fisheye[nfish-1].ntransform].value = DTOR*angle;
         fisheye[nfish-1].ntransform++;
      }
      if (strstr(aline,"ROTATEZ:") != NULL && nfish > 0) {
         sscanf(aline,"%s %lf",ignore,&angle);
         fisheye[nfish-1].transform =
            realloc(fisheye[nfish-1].transform,(fisheye[nfish-1].ntransform+1)*sizeof(TRANSFORM));
         fisheye[nfish-1].transform[fisheye[nfish-1].ntransform].axis = ZPAN;
         fisheye[nfish-1].transform[fisheye[nfish-1].ntransform].value = DTOR*angle;
         fisheye[nfish-1].ntransform++;
      }
   }
	fclose(fptr);

	// Need to have found 2
   if (nfish != 2) {
      fprintf(stderr,"Expected two fisheye images, only found %d\n",nfish);
      return(FALSE);
   }

	return(TRUE);
}

int WriteOutputImage(char *basename,char *s)
{
	int i;
	FILE *fptr;
	char fname[256];

	// Decide on the name
   if (strlen(s) < 2) {
      strcpy(fname,basename);
     	strcat(fname,"_sph");
   } else {
		if (IsJPEG(s)) { // remove extension
			for (i=strlen(s)-1;i>0;i--) {
				if (s[i] == '.') {
					s[i] = '\0';
					break;
				}
			}
		}
      strcpy(fname,s);
   }

	// Add extension
	if (params.fileformat == JPG)
		strcat(fname,".jpg");
	else
		strcat(fname,".tga");

	// Open file
   if ((fptr = fopen(fname,"wb")) == NULL) {
      fprintf(stderr,"Failed to open output file \"%s\"\n",fname);
      return(FALSE);
   }

	// Write image
   if (params.fileformat == JPG)
      JPEG_Write(fptr,spherical,params.outwidth,params.outheight,100);
   else
   	Write_Bitmap(fptr,spherical,params.outwidth,params.outheight,12);

   fclose(fptr);
	return(TRUE);
}

/*
	This should be a metric for best fit, smaller the better
*/
double CalcError(COLOUR rgb1,COLOUR rgb2,double weight) 
{
	double error;
	COLOUR dc;

	// This is root-mean-square error
   dc.r = rgb1.r - rgb2.r;
   dc.g = rgb1.g - rgb2.g;
   dc.b = rgb1.b - rgb2.b;
	error = dc.r*dc.r + dc.g*dc.g + dc.b*dc.b;
	error *= weight;

	return(error);
}

/*
	Add 3 random rotations angles for camera 0 only
	Don't need to apply to both cameras, preserve any user transforms for camera 1
*/
void MakeRandomRotations(void)
{
	int j;
	static int first = TRUE;
	static int nt;

	if (first) {
		nt = fisheye[0].ntransform;
		fisheye[0].transform = realloc(fisheye[0].transform,(nt+3)*sizeof(TRANSFORM));
		fisheye[0].ntransform += 3;
		first = FALSE;
	}

	// Try each of the 6 possible transformation orders
	switch (rand() % 6) {
	case 0:
   	fisheye[0].transform[nt+0].axis = XTILT;
   	fisheye[0].transform[nt+1].axis = YROLL;
   	fisheye[0].transform[nt+2].axis = ZPAN;
		break;
   case 1:
      fisheye[0].transform[nt+0].axis = XTILT;
      fisheye[0].transform[nt+1].axis = ZPAN;
      fisheye[0].transform[nt+2].axis = YROLL;
      break;
   case 2:
      fisheye[0].transform[nt+0].axis = YROLL;
      fisheye[0].transform[nt+1].axis = ZPAN;
      fisheye[0].transform[nt+2].axis = XTILT;
      break;
   case 3:
      fisheye[0].transform[nt+0].axis = YROLL;
      fisheye[0].transform[nt+1].axis = XTILT;
      fisheye[0].transform[nt+2].axis = ZPAN;
      break;
   case 4:
      fisheye[0].transform[nt+0].axis = ZPAN;
      fisheye[0].transform[nt+1].axis = XTILT;
      fisheye[0].transform[nt+2].axis = YROLL;
      break;
   case 5:
      fisheye[0].transform[nt+0].axis = ZPAN;
      fisheye[0].transform[nt+1].axis = YROLL;
      fisheye[0].transform[nt+2].axis = XTILT;
      break;
	}

   // Precompute sine and cosine of angles for the new transforms
   for (j=nt;j<fisheye[0].ntransform;j++) {
      fisheye[0].transform[j].value = 2 * (drand48() - 0.5) * params.deltatheta;
      fisheye[0].transform[j].cvalue = cos(fisheye[0].transform[j].value);
      fisheye[0].transform[j].svalue = sin(fisheye[0].transform[j].value);
   }
}

/*
   Calculate HSV from RGB
   Hue is in degrees
   Value is betweeen 0 and 1
   Saturation is between 0 and 1
*/
HSV RGB2HSV(COLOUR c1)
{
   double themin,themax,delta;
   HSV c2;

   themin = MIN(c1.r,MIN(c1.g,c1.b));
   themax = MAX(c1.r,MAX(c1.g,c1.b));
   delta = themax - themin;
   c2.v = themax;
   c2.s = 0;
   if (themax > 0)
      c2.s = delta / themax;
   c2.h = 0;
   if (delta > 0) {
      if (themax == c1.r && themax != c1.g)
         c2.h += (c1.g - c1.b) / delta;
      if (themax == c1.g && themax != c1.b)
         c2.h += (2 + (c1.b - c1.r) / delta);
      if (themax == c1.b && themax != c1.r)
         c2.h += (4 + (c1.r - c1.g) / delta);
      c2.h *= 60;
      if (c2.h < 0)
         c2.h += 360;
   }
   return(c2);
}

/*
   Calculate RGB from HSV, reverse of RGB2HSV()
   Hue is in degrees
   Value is between 0 and 1
   Saturation is between 0 and 1
*/
COLOUR HSV2RGB(HSV c1)
{
   COLOUR c2,sat;

   while (c1.h < 0)
      c1.h += 360;
   while (c1.h > 360)
      c1.h -= 360;

   if (c1.h < 120) {
      sat.r = (120 - c1.h) / 60.0;
      sat.g = c1.h / 60.0;
      sat.b = 0;
   } else if (c1.h < 240) {
      sat.r = 0;
      sat.g = (240 - c1.h) / 60.0;
      sat.b = (c1.h - 120) / 60.0;
   } else {
      sat.r = (c1.h - 240) / 60.0;
      sat.g = 0;
      sat.b = (360 - c1.h) / 60.0;
   }
   sat.r = MIN(sat.r,1);
   sat.g = MIN(sat.g,1);
   sat.b = MIN(sat.b,1);

   c2.r = (1 - c1.s + c1.s * sat.r) * c1.v;
   c2.g = (1 - c1.s + c1.s * sat.g) * c1.v;
   c2.b = (1 - c1.s + c1.s * sat.b) * c1.v;

   return(c2);
}

void InitParams(void)
{
   time_t secs;
   int seed;

	params.debug = FALSE;
	params.antialias = 2;             // Supersampling antialising
	params.blendmid = 180*DTOR*0.5;   // Mid point for blending
	params.blendwidth = 0;            // Angular blending width
	params.blendpower = 1;
	params.outwidth = 4096;
	params.outheight = 2048;

	// Intensity correction
	// Coefficients of 5th order polynomial brightness correction
   // 1 + a[1]x + a[2]x^2 + a[3]x^3 + a[4]x^4 + a[5]x^5
   // Should map from 0 to 1 to 0 to generally 1+delta
	params.icorrection = FALSE;       // Perform intensity correction or not
	//double ifcn[6] = {0.9998,0.0459,-0.5894,2.4874,-4.2037,2.4599}; // rises to 1.2
	//double ifcn[6] = {1.0,0.1,-1.0417,3.6458,-5.2083,2.6042}; // rises to 1.1
	//double ifcn[6] = {1.0,0.05,-0.5208,1.8229,-2.6042,1.3021}; // rises to 1.05
	params.ifcn[0] = 1.0;
	params.ifcn[1] = 0.1;
	params.ifcn[2] = -1.0417;
	params.ifcn[3] = 3.6458;
	params.ifcn[4] = -5.2083;
	params.ifcn[5] = 2.6042;

	// Experimental optimisation
	params.deltafov = 10*DTOR;        // Variation of fov
	params.deltacenter = 20;          // Variation of fisheye center coordinates
	params.deltatheta = 5*DTOR;       // Variation of rotations

	params.fileformat = TGA;

	// Random number seed
   time(&secs);
   seed = secs;
   //seed = 12345; // Use for constant seed
   srand48(seed);
}

void FlipFisheye(FISHEYE f)
{
	BITMAP4 c1,c2,black = {0,0,0};
	int i,j,x1,y1,x2,y2;
	int index1,index2;

	if (f.hflip < 0) {
		for (i=1;i<=f.radius;i++) {
			for (j=-f.radius;j<=f.radius;j++) {
				x1 = f.centerx-i;
				y1 = f.centery+j;
				x2 = f.centerx+i;
      		y2 = f.centery+j;
				index1 = y1 * f.width + x1;
				index2 = y2 * f.width + x2;

				if (x1 < 0 || y1 < 0 || x1 >= f.width || y1 >= f.height)
					c1 = black;
				else
					c1 = f.image[index1];
            if (x2 < 0 || y2 < 0 || x2 >= f.width || y2 >= f.height)
               c2 = black;
            else
					c2 = f.image[index2];

				if (x1 >= 0 || y1 >= 0 || x1 < f.width || y1 < f.height)
					f.image[index1] = c2;
				if (x2 >= 0 || y2 >= 0 || x2 < f.width || y2 < f.height)
					f.image[index2] = c1;
			}
		}
	}

	if (f.vflip < 0) {
      for (i=-f.radius;i<=f.radius;i++) {
         for (j=1;j<=f.radius;j++) {
            x1 = f.centerx+i;
            y1 = f.centery-j;
            x2 = f.centerx+i;
            y2 = f.centery+j;
            index1 = y1 * f.width + x1;
            index2 = y2 * f.width + x2;

            if (x1 < 0 || y1 < 0 || x1 >= f.width || y1 >= f.height)
               c1 = black;
            else
               c1 = f.image[index1];
            if (x2 < 0 || y2 < 0 || x2 >= f.width || y2 >= f.height)
               c2 = black;
            else
               c2 = f.image[index2];

            if (x1 >= 0 || y1 >= 0 || x1 < f.width || y1 < f.height)
               f.image[index1] = c2;
            if (x2 >= 0 || y2 >= 0 || x2 < f.width || y2 < f.height)
               f.image[index2] = c1;
         }
      }
	}
}

XYZ RotateX(XYZ p,double theta)
{
   XYZ q;

   q.x = p.x;
   q.y = p.y * cos(theta) + p.z * sin(theta);
   q.z = -p.y * sin(theta) + p.z * cos(theta);
   return(q);
}

XYZ RotateY(XYZ p,double theta)
{
   XYZ q;

   q.x = p.x * cos(theta) - p.z * sin(theta);
   q.y = p.y;
   q.z = p.x * sin(theta) + p.z * cos(theta);
   return(q);
}

XYZ RotateZ(XYZ p,double theta)
{
   XYZ q;

   q.x = p.x * cos(theta) + p.y * sin(theta);
   q.y = -p.x * sin(theta) + p.y * cos(theta);
   q.z = p.z;
   return(q);
}



/*
	Create remap filters for ffmpeg
	This does not do blending, just creates the rema filters for each half
	of the fisheye2spherical process
	See documentation for how to use ffmpeg to blend these together
*/
void MakeRemap(void)
{
   int i,j,ix,iy,u,v,n;
   double longitude,latitude;
   char fname[256];
   FILE *fptrx = NULL,*fptry = NULL;
	COLOUR rgb;

	for (n=0;n<2;n++) {

	   sprintf(fname,"fusion2sphere%d_x.pgm",n);
	   fptrx = fopen(fname,"w");
   	fprintf(fptrx,"P2\n%d %d\n65535\n",params.outwidth,params.outheight);

   	sprintf(fname,"fusion2sphere%d_y.pgm",n);
   	fptry = fopen(fname,"w");
   	fprintf(fptry,"P2\n%d %d\n65535\n",params.outwidth,params.outheight);

   	for (j=params.outheight-1;j>=0;j--) {
   	   for (i=0;i<params.outwidth;i++) {
   	      ix = -1;
   	      iy = -1;
	
	         // Calculate longitude and latitude
	         longitude = TWOPI * i / (double)params.outwidth - PI; // -pi ... pi
	         latitude = PI * j / (double)params.outheight - PID2; // -pi/2 ... pi/2
	
	         // Find the corresponding pixel in the fisheye image
	         if (FindFishPixel(n,latitude,longitude,&u,&v,&rgb)) {
	            ix = u;
	            iy = fisheye[n].height-1-v;
	         }
	         fprintf(fptrx,"%d ",ix);
	         fprintf(fptry,"%d ",iy);
	      }
	      fprintf(fptrx,"\n");
	      fprintf(fptry,"\n");
	   }
	   fclose(fptrx);
	   fclose(fptry);

	} // n
}

/************ Adding fusion2spherebatch code ************/

/*
	Check that the filename template has the correct number of %d entries
*/
int CheckTemplate(char *s,int nexpect)
{
	int i,n=0;

	for (i=0;i<strlen(s);i++) {
		if (s[i] == '%'){
			n++;
		}
	}

	if (n != nexpect) {
		fprintf(stderr,"This filename template \"%s\" does not look like it contains sufficient %%d entries\n",s);
		fprintf(stderr,"Expect %d but found %d\n",nexpect,n);
		return(FALSE);
	}

	return(TRUE);
}



/*
	Check the frames
	- do they exist
	- are they jpeg
	- are they the same size
	- determine which frame template we are using
*/
int CheckFrames(char *fname1,char *fname2,int *width,int *height)
{
	int i,n=-1;
	int w1,h1,w2,h2,depth;
	FILE *fptr;

   if (!IsJPEG(fname1) || !IsJPEG(fname2)) {
      fprintf(stderr,"CheckFrames() - frame name does not look like a jpeg file\n");
      return(-1);
   }

   // Frame 1
   if ((fptr = fopen(fname1,"rb")) == NULL) {
      fprintf(stderr,"CheckFrames() - Failed to open first frame \"%s\"\n",fname1);
      return(-1);
   }
   JPEG_Info(fptr,&w1,&h1,&depth);
   fclose(fptr);

	// Frame 2
   if ((fptr = fopen(fname2,"rb")) == NULL) {
      fprintf(stderr,"CheckFrames() - Failed to open second frame \"%s\"\n",fname2);
      return(-1);
   }
   JPEG_Info(fptr,&w2,&h2,&depth);
   fclose(fptr);

	// Are they the same size
   if (w1 != w2 || h1 != h2) {
      fprintf(stderr,"CheckFrames() - Frame sizes don't match, %d != %d or %d != %d\n",w1,h1,w2,h2);
      return(-1);
   }
	
	// Is it a known template?
	for (i=0;i<NTEMPLATE;i++) {
		//printf("\n template[i].width: %d, w1: %d, template[i].heigh: %d, h1: %d\n", template[i].width, w1, template[i].height, h1);
		if (w1 == template[i].width && h1 == template[i].height) {
			n = i;
			break;
		}
	}
	if (n < 0) {
		fprintf(stderr,"CheckFrames() - No recognised frame template\n");
		return(-1);
	}

	*width = w1;
	*height = h1;

	return(n);
}
