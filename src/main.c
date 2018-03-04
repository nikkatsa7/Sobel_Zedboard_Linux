#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include "xsobel.h"

#define SIZE	1024
#define INPUT_FILE	"input.grey"
#define OUTPUT_FILE	"output_sobel.grey"
#define GOLDEN_FILE	"golden.grey"

//Physical addresses
#define INPUT_BASE_ADDR 0x05000000
#define OUTPUT_BASE_ADDR 0x07000000

XSobel Sbl;

void print_accel_status()
{
	int isDone, isIdle, isReady;
	isDone = XSobel_IsDone(&Sbl);
	isIdle = XSobel_IsIdle(&Sbl);
	isReady = XSobel_IsReady(&Sbl);
	printf("Vector Adder Status: isDone %d, isIdle %d, isReady%d\r\n", isDone, isIdle, isReady);
}

void sobel_init(unsigned long inputAddress,unsigned long outputAddress){
 	//Kernel - Init 
    printf("Ready to init kernel\n");
    //In the second argument we use the exact name
    //of the top function of the HLS 
    //otherwise the app crashes
    int status = XSobel_Initialize(&Sbl,"sobel");
    if(status != XST_SUCCESS){ 
        printf("XSobel_Initialize failed\n");
    }   
    printf("Kernel initialized\n");
    XSobel_InterruptGlobalDisable(&Sbl);
    XSobel_InterruptDisable(&Sbl, 1); 
    printf("Interrupts Disabled\n");
    XSobel_Set_in_pointer(&Sbl,(u32)inputAddress); 
    printf("Input initialized\n");
    XSobel_Set_out_pointer(&Sbl,(u32)outputAddress); 
    printf("Output initialized\n");
    printf("Sobel kernel initialized with %x for input and %x for output\n",XSobel_Get_in_pointer(&Sbl),XSobel_Get_out_pointer(&Sbl));
}

void sobel_hw(){
	XSobel_Start(&Sbl);
	while(!XSobel_IsDone(&Sbl)){}
}

unsigned char * assignToPhysical(unsigned long address,unsigned int size){
	int devmem = open("/dev/mem", O_RDWR | O_SYNC);
	off_t PageOffset = (off_t) address % getpagesize();
	off_t PageAddress = (off_t) (address - PageOffset);
	return (unsigned char *) mmap(0, size*sizeof(unsigned char), PROT_READ|PROT_WRITE, MAP_SHARED, devmem, PageAddress);
}

double computePSNR(unsigned char *output,unsigned char *golden){
	double PSNR = 0.0;
	for (int i=1; i<SIZE-1; i++) {
		for (int j=1; j<SIZE-1; j++ ) {
			PSNR += pow((output[i*SIZE+j] - golden[i*SIZE+j]),2);
		}
	}
  
	PSNR /= (double)(SIZE*SIZE);
	PSNR = 10*log10(65536/PSNR);
	return PSNR;
}

signed char horiz_operator[3][3] = {{-1, 0, 1}, 
                             {-2, 0, 2}, 
                             {-1, 0, 1}};

signed char vert_operator[3][3] = {{1, 2, 1}, 
                            {0, 0, 0}, 
                            {-1, -2, -1}};

int convolution2D(int posy, int posx, const unsigned char *input,signed char operator[][3]) {
	int i, j, res;
  
	res = 0;
	for (i = -1; i <= 1; i++) {
		for (j = -1; j <= 1; j++) {
			res += input[(posy + i)*SIZE + posx + j] * operator[i+1][j+1];
		}
	}
	return(res);
}

void sobel_sw(unsigned char *input,unsigned char *output){
	double p,res;
	
	for (int i=1; i<SIZE-1; i+=1 ) {
		for (int j=1; j<SIZE-1; j+=1) {
			/* Apply the sobel filter and calculate the magnitude *
			 * of the derivative.								  */
			p = pow(convolution2D(i, j, input, horiz_operator), 2) + 
				pow(convolution2D(i, j, input, vert_operator), 2);
			res = (int)sqrt(p);
			/* If the resulting value is greater than 255, clip it *
			 * to 255.											   */
			if (res > 255)
				output[i*SIZE + j] = 255;      
			else
				output[i*SIZE + j] = (unsigned char)res;
		}
	}
}

int main(int argc, char* argv[])
{
	sobel_init(INPUT_BASE_ADDR,OUTPUT_BASE_ADDR);
	print_accel_status();
	unsigned char *input_hw, *output_hw,*input_sw, *output_sw, *golden;

	input_hw 	= assignToPhysical(INPUT_BASE_ADDR,SIZE*SIZE);
	output_hw 	= assignToPhysical(OUTPUT_BASE_ADDR,SIZE*SIZE);
	input_sw 	= (unsigned char*) calloc(sizeof(unsigned char),SIZE*SIZE);
	output_sw 	= (unsigned char*) calloc(sizeof(unsigned char),SIZE*SIZE);
	golden 		= (unsigned char*) calloc(sizeof(unsigned char),SIZE*SIZE);

	struct timespec  tv1, tv2;
	FILE *f_in, *f_out, *f_golden;

	/* Open the input, output, golden files, read the input and golden    *
     * and store them to the corresponding arrays.						  */
	f_in = fopen(INPUT_FILE, "r");
	if (f_in == NULL) {
		printf("File " INPUT_FILE " not found\n");
		exit(1);
	}
  
	f_out = fopen(OUTPUT_FILE, "wb");
	if (f_out == NULL) {
		printf("File " OUTPUT_FILE " could not be created\n");
		fclose(f_in);
		exit(1);
	}  
  
	f_golden = fopen(GOLDEN_FILE, "r");
	if (f_golden == NULL) {
		printf("File " GOLDEN_FILE " not found\n");
		fclose(f_in);
		fclose(f_out);
		exit(1);
	}    

	fread(input_sw, sizeof(unsigned char), SIZE*SIZE, f_in);
	fread(golden, sizeof(unsigned char), SIZE*SIZE, f_golden);
	memcpy(input_hw,input_sw,SIZE*SIZE);
	memcpy(output_hw,output_sw,SIZE*SIZE);
	fclose(f_in);
	fclose(f_golden);

	clock_gettime(CLOCK_MONOTONIC_RAW, &tv1);
	sobel_sw(input_sw,output_sw);
	clock_gettime(CLOCK_MONOTONIC_RAW, &tv2);
	double softwareTime = (double) (tv2.tv_nsec - tv1.tv_nsec) / 1000000000.0 +
			(double) (tv2.tv_sec - tv1.tv_sec);
	printf ("Total time for software = %10g seconds\n",softwareTime);
			
	clock_gettime(CLOCK_MONOTONIC_RAW, &tv1);
	sobel_hw();
	clock_gettime(CLOCK_MONOTONIC_RAW, &tv2);
	double hardwareTime = (double) (tv2.tv_nsec - tv1.tv_nsec) / 1000000000.0 +
			(double) (tv2.tv_sec - tv1.tv_sec);
	printf ("Total time for hardware = %10g seconds\n",hardwareTime);

	/* Write the output file */
	fwrite(output_sw, sizeof(unsigned char), SIZE*SIZE, f_out);
	fclose(f_out);
	double PSNR_SW = computePSNR(output_sw,golden);
	double PSNR_HW = computePSNR(output_hw,golden);
	printf("PSNR of original Sobel and software computed Sobel image: %g\n", PSNR_SW);
	printf("PSNR of original Sobel and hardware computed Sobel image: %g\n", PSNR_HW);
	printf("Acceleration ratio %lf\n",softwareTime/hardwareTime);
	printf("A visualization of the sobel filter can be found at " OUTPUT_FILE ", or you can run 'make image' to get the jpg\n");
	munmap(input_hw,SIZE*SIZE*sizeof(unsigned char));	
	munmap(output_hw,SIZE*SIZE*sizeof(unsigned char));
	free(input_sw);
	free(output_sw);
	free(golden);
	XSobel_Release(&Sbl);
	return 0;
}

