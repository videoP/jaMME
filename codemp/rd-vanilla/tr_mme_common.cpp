#include "tr_mme.h"

void R_MME_GetShot( void* output ) {
	qglReadPixels( 0, 0, glConfig.vidWidth, glConfig.vidHeight, GL_RGB, GL_UNSIGNED_BYTE, output ); 
}

void R_MME_GetStencil( void *output ) {
	qglReadPixels( 0, 0, glConfig.vidWidth, glConfig.vidHeight, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, output ); 
}

void R_MME_GetDepth( byte *output ) {
	float focusStart, focusEnd, focusMul;
	float zBase, zAdd, zRange;
	int i, pixelCount;
	byte *temp;

	if ( mme_depthRange->value <= 0 )
		return;
	
	pixelCount = glConfig.vidWidth * glConfig.vidHeight;

	focusStart = mme_depthFocus->value - mme_depthRange->value;
	focusEnd = mme_depthFocus->value + mme_depthRange->value;
	focusMul = 255.0f / (2 * mme_depthRange->value);

	zRange = backEnd.sceneZfar - r_znear->value;
	zBase = ( backEnd.sceneZfar + r_znear->value ) / zRange;
	zAdd =  ( 2 * backEnd.sceneZfar * r_znear->value ) / zRange;

	temp = (byte *)ri.Hunk_AllocateTempMemory( pixelCount * sizeof( float ) );
	qglDepthRange( 0.0f, 1.0f );
	qglReadPixels( 0, 0, glConfig.vidWidth, glConfig.vidHeight, GL_DEPTH_COMPONENT, GL_FLOAT, temp ); 
	/* Could probably speed this up a bit with SSE but frack it for now */
	for ( i=0 ; i < pixelCount; i++ ) {
		/* Read from the 0 - 1 depth */
		float zVal = ((float *)temp)[i];
		int outVal;
		/* Back to the original -1 to 1 range */
		zVal = zVal * 2.0f - 1.0f;
		/* Back to the original z values */
		zVal = zAdd / ( zBase - zVal );
		/* Clip and scale the range that's been selected */
		if (zVal <= focusStart)
			outVal = 0;
		else if (zVal >= focusEnd)
			outVal = 255;
		else 
			outVal = (zVal - focusStart) * focusMul;
		output[i] = outVal;
	}
	ri.Hunk_FreeTempMemory( temp );
}

void R_MME_SaveShot( mmeShot_t *shot, int width, int height, float fps, byte *inBuf ) {
	mmeShotFormat_t format;
	char *extension;
	char *outBuf;
	int outSize;
	char fileName[MAX_OSPATH];

	format = shot->format;
	switch (format) {
	case mmeShotFormatJPG:
		extension = "jpg";
		break;
	case mmeShotFormatTGA:
		/* Seems hardly any program can handle grayscale tga, switching to png */
		if (shot->type == mmeShotTypeGray) {
			format = mmeShotFormatPNG;
			extension = "png";
		} else {
			extension = "tga";
		}
		break;
	case mmeShotFormatPNG:
		extension = "png";
		break;
	case mmeShotFormatAVI:
//		goto doavi;	//CL_AVI
		mmeAviShot( &shot->avi, shot->name, shot->type, width, height, fps, inBuf );
		return;
	}

	if (shot->counter < 0) {
		int counter = 0;
		while ( counter < 1000000000) {
			Com_sprintf( fileName, sizeof(fileName), "%s.%010d.%s", shot->name, counter, extension);
			if (!ri.FS_FileExists( fileName ))
				break;
			if ( mme_saveOverwrite->integer ) 
				ri.FS_FileErase( fileName );
			counter++;
		}
		if ( mme_saveOverwrite->integer ) {
			shot->counter = 0;
		} else {
			shot->counter = counter;
		}
	} 

	Com_sprintf( fileName, sizeof(fileName), "%s.%010d.%s", shot->name, shot->counter, extension );
	shot->counter++;

	outSize = width * height * 4 + 2048;
	outBuf = (char *)ri.Hunk_AllocateTempMemory( outSize );
	switch ( format ) {
	case mmeShotFormatJPG:
		outSize = SaveJPG( mme_jpegQuality->integer, width, height, shot->type, inBuf, (byte *)outBuf, outSize );
		break;
	case mmeShotFormatTGA:
		outSize = SaveTGA( mme_tgaCompression->integer, width, height, shot->type, inBuf, (byte *)outBuf, outSize );
		break;
	case mmeShotFormatPNG:
		outSize = SavePNG( mme_pngCompression->integer, width, height, shot->type, inBuf, (byte *)outBuf, outSize );
		break;
/*	case mmeShotFormatAVI:	//uncomment if you want to try to capture through cl_avi (/video): tag CL_AVI
doavi:
		byte *outBufAvi;
		int i, pixels, outSizeAvi;

		pixels = width*height;
		outSizeAvi = width*height*3 + 2048; //Allocate bit more to be safish?
		outBufAvi = (byte *)ri.Hunk_AllocateTempMemory( outSizeAvi + 8);
		outBufAvi[0] = '0';outBufAvi[1] = '0';
		outBufAvi[2] = 'd';outBufAvi[3] = 'b';
		switch (shot->type) {
		case mmeShotTypeGray:
			for (i = 0;i<pixels;i++) {
				outBufAvi[8 + i] = inBuf[i];
			};
			outSizeAvi = pixels;
			break;
		case mmeShotTypeRGB:
			for (i = 0;i<pixels;i++) {
				outBufAvi[8 + i*3 + 0 ] = inBuf[ i*3 + 0];
				outBufAvi[8 + i*3 + 1 ] = inBuf[ i*3 + 2];
				outBufAvi[8 + i*3 + 2 ] = inBuf[ i*3 + 1];
			}
			outSizeAvi = width * height * 3;
		break;
		}
		ri.CL_WriteAVIVideoFrame(outBufAvi, outSizeAvi);
		return;
*/	default:
		outSize = 0;
	}
	if (outSize)
		ri.FS_WriteFile( fileName, outBuf, outSize );
	ri.Hunk_FreeTempMemory( outBuf );
}

void blurCreate( mmeBlurControl_t* control, const char* type, int frames ) {
	float*  blurFloat = control->Float;
	float	blurMax, strength;
	float	blurHalf = 0.5f * ( frames - 1 );
	float	bestStrength;
	float	floatTotal;
	int		passes, bestTotal;
	int		i;
	
	if (blurHalf <= 0)
		return;

	if ( !Q_stricmp( type, "gaussian")) {
		for (i = 0; i < frames ; i++) {
			double xVal = ((i - blurHalf ) / blurHalf) * 3;
			double expVal = exp( - (xVal * xVal) / 2);
			double sqrtVal = 1.0f / sqrt( 2 * M_PI);
			blurFloat[i] = sqrtVal * expVal;
		}
	} else if (!Q_stricmp( type, "triangle")) {
		for (i = 0; i < frames; i++) {
			if ( i <= blurHalf )
				blurFloat[i] = 1 + i;
			else
				blurFloat[i] = 1 + ( frames - 1 - i);
		}
	} else {
		for (i = 0; i < frames; i++) {
			blurFloat[i] = 1;
		}
	}

	floatTotal = 0;
	blurMax = 0;
	for (i = 0; i < frames; i++) {
		if ( blurFloat[i] > blurMax )
			blurMax = blurFloat[i];
		floatTotal += blurFloat[i];
	}

	floatTotal = 1 / floatTotal;
	for (i = 0; i < frames; i++) 
		blurFloat[i] *= floatTotal;

	bestStrength = 0;
	bestTotal = 0;
	strength = 128;

	/* Check for best 256 match for MMX */
	for (passes = 32;passes >0;passes--) {
		int total = 0;
		for (i = 0; i < frames; i++) 
			total += strength * blurFloat[i];
		if (total > 256) {
			strength *= (256.0 / total);
		} else if (total <= 256) {
			if ( total > bestTotal) {
				bestTotal = total;
				bestStrength = strength;
			}
			strength *= (256.0 / total); 
		} else {
			bestTotal = total;
			bestStrength = strength;
			break;
		}
	}
	for (i = 0; i < frames; i++) {
		control->MMX[i] = bestStrength * blurFloat[i];
	}

	bestStrength = 0;
	bestTotal = 0;
	strength = 128;
	/* Check for best 32768 match for MMX */
	for (passes = 32;passes >0;passes--) {
		int total = 0;
		for (i = 0; i < frames; i++) 
			total += strength * blurFloat[i];
		if ( total > 32768 ) {
			strength *= (32768.0 / total);
		} else if (total <= 32767 ) {
			if ( total > bestTotal) {
				bestTotal = total;
				bestStrength = strength;
			}
			strength *= (32768.0 / total); 
		} else {
			bestTotal = total;
			bestStrength = strength;
			break;
		}
	}
	for (i = 0; i < frames; i++) {
		control->SSE[i] = bestStrength * blurFloat[i];
	}

	control->totalIndex = 0;
	control->totalIndex = frames;
	control->overlapFrames = 0;
	control->overlapIndex = 0;

	_mm_empty();
}

static void MME_AccumClearMMX( void* w, const void* r, short mul, int count ) {
	const __m64 * reader = (const __m64 *) r;
	__m64 *writer = (__m64 *) w;
	int i; 
	__m64 readVal, zeroVal, work0, work1, multiply;
	 multiply = _mm_set1_pi16( mul );
	 zeroVal = _mm_setzero_si64();
	 for (i = count; i>0 ; i--) {
		 readVal = *reader++;
		 work0 = _mm_unpacklo_pi8( readVal, zeroVal );
		 work1 = _mm_unpackhi_pi8( readVal, zeroVal );
		 work0 = _mm_mullo_pi16( work0, multiply );
		 work1 = _mm_mullo_pi16( work1, multiply );
		 writer[0] = work0;
		 writer[1] = work1;
		 writer += 2;
	 }
	 _mm_empty();
}

static void MME_AccumAddMMX( void *w, const void* r, short mul, int count ) {
	const __m64 * reader = (const __m64 *) r;
	__m64 *writer = (__m64 *) w;
	int i;
	__m64 zeroVal, multiply;
	 multiply = _mm_set1_pi16( mul );
	 zeroVal = _mm_setzero_si64();
	 /* Add 2 pixels in a loop */
	 for (i = count ; i>0 ; i--) {
		 __m64 readVal = *reader++;
		 __m64 work0 = _mm_mullo_pi16( multiply, _mm_unpacklo_pi8( readVal, zeroVal ) );
		 __m64 work1 = _mm_mullo_pi16( multiply, _mm_unpackhi_pi8( readVal, zeroVal ) );
		 writer[0] = _mm_add_pi16( writer[0], work0 );
		 writer[1] = _mm_add_pi16( writer[1], work1 );
		 writer += 2;
	 }
	 _mm_empty();
}


static void MME_AccumShiftMMX( const void  *r, void *w, int count ) {
	const __m64 * reader = (const __m64 *) r;
	__m64 *writer = (__m64 *) w;

	int i;
	__m64 work0, work1, work2, work3;
	/* Handle 2 at once */
	for (i = count/2;i>0;i--) {
		work0 = _mm_srli_pi16 (reader[0], 8);
		work1 = _mm_srli_pi16 (reader[1], 8);
		work2 = _mm_srli_pi16 (reader[2], 8);
		work3 = _mm_srli_pi16 (reader[3], 8);
		reader += 4;
		writer[0] = _mm_packs_pu16( work0, work1 );
		writer[1] = _mm_packs_pu16( work2, work3 );
		writer += 2;
	}
	_mm_empty();
}

void R_MME_BlurAccumAdd( mmeBlurBlock_t *block, const __m64 *add ) {
	mmeBlurControl_t* control = block->control;
	int index = control->totalIndex;
	if ( mme_cpuSSE2->integer ) {
		if ( index == 0) {
			MME_AccumClearSSE( block->accum, add, control->SSE[ index ], block->count );
		} else {
			MME_AccumAddSSE( block->accum, add, control->SSE[ index ], block->count );
		}
	} else {
		if ( index == 0) {
			MME_AccumClearMMX( block->accum, add, control->MMX[ index ], block->count );
		} else {
			MME_AccumAddMMX( block->accum, add, control->MMX[ index ], block->count );
		}
	}
}

void R_MME_BlurOverlapAdd( mmeBlurBlock_t *block, int index ) {
	mmeBlurControl_t* control = block->control;
	index = ( index + control->overlapIndex ) % control->overlapFrames;
	R_MME_BlurAccumAdd( block, block->overlap + block->count * index );
}

void R_MME_BlurAccumShift( mmeBlurBlock_t *block  ) {
	if ( mme_cpuSSE2->integer ) {
		MME_AccumShiftSSE( block->accum, block->accum, block->count );
	} else {
		MME_AccumShiftMMX( block->accum, block->accum, block->count );
	}
}