

		#define title "Portable Audio Console"

		#include <stdio.h>
		#include <stdlib.h>
		#include <string.h>

		#include <math.h>
		#include <portaudio.h>


		// sample rate [samples/second]
		#define SR 44100
		int srate = SR;

		// sample duration [seconds]
		double stime = 1.0 / SR;

		// sample size [bytes]
		int ssize = sizeof(float);
		
		// desired callback argument size [samples]
		int asize = 10000;
		
		// history size [samples]
		int hsize = 100000;

		// max play callback argument size global [samples]
		int max_asize = 0;

		typedef struct device {
			int id;
			const PaDeviceInfo *info;
			int nins, nouts;
			float *ins;
			PaStream *in_stream, *out_stream;
			const PaStreamInfo *in_info, *out_info;
			double in_t0, out_t0, t;
			long long in_len, out_len; }
		device;
		int ndevs = 0;
		device *devs = 0;

		typedef struct route {
			int sd, sc, dd, dc;
			long long last_src;
			float *wav;
			int wav_len; }
		route;
		int nroutes = 0;
		route routes[100];

		int init() {
			if( Pa_Initialize() || (ndevs = Pa_GetDeviceCount()) <= 0 ) {
				printf( "ERROR: Pa_Initialize or Pa_GetDeviceCount returned rubbish \n" );
				return 0; }
			devs = (device*) malloc( sizeof(device) * ndevs );
			for( int i=0; i<ndevs; i++ ) {
				devs[i].id = i;
				devs[i].info = Pa_GetDeviceInfo( i );
				devs[i].nins = devs[i].info->maxInputChannels;
				devs[i].nouts = devs[i].info->maxOutputChannels;
				devs[i].ins = (float*) malloc( devs[i].nins * hsize * ssize );
				for( int j=0; j<devs[i].nins; j++ ) {
					for( int k=0; k<hsize; k++ ) {
						devs[i].ins[ j * hsize +k ] = 0.0; }}
				devs[i].in_stream = 0;
				devs[i].out_stream = 0;
				devs[i].in_len = 0;
				devs[i].out_len = 0; }}

		// ############################################################################################################################################################	
		// REC CALLBACK

		PaStreamCallbackResult on_device_data(
			const void **input,
			void **output,
			unsigned long frameCount,
			const PaStreamCallbackTimeInfo *timeInfo,
			PaStreamCallbackFlags statusFlags,
			void *userData ) {
			
			double t = timeInfo->currentTime;
			device* dev = (device*) userData;
			float **data = input;
			unsigned long data_len = frameCount			;
			long long dst = (long long) round( timeInfo->inputBufferAdcTime /stime );
			
			if( dst < dev->in_len ) {
				printf( "%d in replacing %d old samples \n", dev->id, dev->in_len -dst ); }
			else if( dst < dev->in_len ) {
				printf( "%d in missing %d samples \n", dev->id, dst -dev->in_len ); }
			else if( dst == 0 ) {
				printf( "%d recording \n", dev->id );
				dev->in_t0 = t; }

			dev->in_len = dst;
			
			int ofs = dst % hsize;
			
			if( data_len <= hsize -ofs ) {
				for( int i=0; i< dev->nins; i++ ) {
					memcpy(
						dev->ins +i*hsize +ofs, data[i],
						data_len *ssize ); }}
			else {
				int x = ofs +data_len -hsize;
				for( int i=0; i< dev->nins; i++ ) {
					memcpy(
						dev->ins +i*hsize +ofs, data[i],
						(data_len -x) *ssize );
					memcpy(
						dev->ins +i*hsize, data[i]+x,
						x *ssize ); }}
				
			dev->in_len += data_len;
			
			return paContinue; }

		// ############################################################################################################################################################	
		// PLAY CALLBACK

		PaStreamCallbackResult on_write_to_device(
			const void **input,
			void **output,
			unsigned long frameCount,
			const PaStreamCallbackTimeInfo *timeInfo,
			PaStreamCallbackFlags statusFlags,
			void *userData ) {

			double t = timeInfo->currentTime;
			device* dev = (device*) userData;
			float **data = output;
			unsigned long data_len = frameCount;
			long long dac = (long long) round( timeInfo->outputBufferDacTime /stime );

			// printf( "%d play t %9.9f \n", dev->id, t )

			if( dac < dev->out_len ) {
				printf( "dac wants %d old samples", dev->out_len -dac ); }
			else if( dac > dev->out_len ) {
				printf( "dac miss %d samples", dac -dev->out_len ); }
			else if( dac == 0.0 ) {
				printf( "%d playing \n", dev->id );
				dev->out_t0 = t; }

			dev->out_len = dac;
			
			for( int sd, sc, dd, dc, i=0; i<nroutes; i++ ) {
				sd = routes[i].sd; sc = routes[i].sc; dd = routes[i].dd; dc = routes[i].dc				;
				
				if( dd == dev->id ) {

					if( !devs[sd].in_len ) {
						printf( "%d wating %d to start \n", dd, sd );
						continue; }

					long long src;
					
					if( routes[i].last_src == -1 ) {
						src = (int) ceil( (t -devs[sd].in_t0) /stime ) -max_asize;
						if( devs[sd].in_len -src > hsize ) {
							printf( " !!!!!!!!!!!!!!!!!!! \n" ); }}
					else {
						src = routes[i].last_src; }

					//if( routes[i].last_src != -1 && routes[i].last_src != src )
					//	printf( "%d %d src - last_src %d \n", sd, dd, src -routes[i].last_src )

					if( src +data_len > devs[sd].in_len ) {
						printf( "%d wants to read %d future unsaved samples from %d \n", dd, src +data_len -devs[sd].in_len, sd );
						continue; }

					if( src < 0  ) {
						printf( "%d buffering %d src %d t-in_t0 %d t %10.10f \n", dd, sd, src, (int)(ceil( (t -devs[sd].in_t0) /stime )), t );
						continue; }
					
					int ofs = src % hsize;
					if( ofs +data_len <= hsize ) {
						memcpy(
							data[dc], devs[sd].ins +sc*hsize +ofs,
							data_len *ssize ); }
					else {
						int x = ofs +data_len -hsize;
						memcpy(
							data[dc], devs[sd].ins +sc*hsize +ofs,
							(data_len -x) *ssize );
						memcpy(
							data[dc] +x, devs[sd].ins +sc*hsize,
							x *ssize ); }

					routes[i].last_src = src +data_len;
					
					if( routes[i].wav ) {
						//printf( " conv \n" )
						for( int x=0; x<frameCount; x++ ) {
							// *(data[dc] +x) = (float) (0.01*sin( ((double)x) / 10.0 ))
							
							// *(routes[i].wav +(src % routes[i].wav_len))
							
							float sum = 0.0;
							for( int y=0; y<routes[i].wav_len; y++ ) {
							
								float h = *(routes[i].wav +y);
								
								int disp = (ofs +x -y) % hsize;
								if( disp<0 ) {
									disp = hsize +disp; }
								
								if( disp <0 || disp > hsize ) {
									printf( "err" ); }
									
								float s = *(devs[sd].ins +sc*hsize +disp );
								
								sum += h * s; }

							*(data[dc] +x) = sum; }}}}
							//printf( "sum %5.5f \n", sum )


			dev->out_len += data_len;
			
			return paContinue; }

		// ############################################################################################################################################################	


		void rec( device *dev ) {
			static PaStreamParameters params;
			params.device = dev->id;
			params.sampleFormat = paFloat32|paNonInterleaved;
			params.hostApiSpecificStreamInfo = 0;
			params.suggestedLatency = asize *stime;
			params.channelCount = dev->nins;
			
			printf( "%d rec opening \n", dev->id );
			PaError err = Pa_OpenStream(
				&(dev->in_stream), &params, NULL, srate,
				paFramesPerBufferUnspecified, paClipOff|paDitherOff,
				&on_device_data, dev );
				
			if( err != paNoError ) {
				if( err != paUnanticipatedHostError ) {
					printf( "ERROR 1: %s \n", Pa_GetErrorText( err ) ); }
				else {
					const PaHostErrorInfo* herr = Pa_GetLastHostErrorInfo();
					printf( "ERROR 2: %s \n", herr->errorText ); }}
					
			dev->in_info = Pa_GetStreamInfo( dev->in_stream );
			int actual_srate = (int) round( dev->in_info->sampleRate );
			int actual_asize = (int) round( dev->in_info->inputLatency /stime );

			printf( "%d rec SR %d asize %d starting \n", dev->id, actual_srate, actual_asize );
			
			err = Pa_StartStream( dev->in_stream );
			
			if( err != paNoError ) {
				printf( "ERROR 3: %s \n", Pa_GetErrorText( err ) ); }}

		void play( device *dev ) {
			static PaStreamParameters params;
			params.device = dev->id;
			params.sampleFormat = paFloat32|paNonInterleaved;
			params.hostApiSpecificStreamInfo = 0;
			params.suggestedLatency = asize *stime;
			params.channelCount = dev->nouts;
			
			printf( "%d play opening \n", dev->id );
			PaError err = Pa_OpenStream(
				&(dev->out_stream), NULL, &params, srate,
				paFramesPerBufferUnspecified, paClipOff|paDitherOff,
				&on_write_to_device, dev );

			if( err != paNoError ) {
				if( err != paUnanticipatedHostError ) {
					printf( "ERROR 3: %s \n", Pa_GetErrorText( err ) ); }
				else {
					const PaHostErrorInfo* herr = Pa_GetLastHostErrorInfo();
					printf( "ERROR 4: %s \n", herr->errorText ); }}
					
			dev->out_info = Pa_GetStreamInfo( dev->out_stream );
			int actual_srate = (int) round( dev->out_info->sampleRate );
			int actual_asize = (int) round( dev->out_info->outputLatency /stime );
			
			printf( "%d play SR %d asize %d starting \n", dev->id, actual_srate, actual_asize );
			if( actual_asize > max_asize ) {
				max_asize = actual_asize;
				printf( "max asize %d samples (latency %4.3f ms) \n", max_asize, max_asize*stime*1000.0 ); }
			
			err = Pa_StartStream( dev->out_stream );
			
			if( err != paNoError ) {
				printf( "ERROR 3: %s \n", Pa_GetErrorText( err ) ); }}

		int load_wav( char* fstr ) {
			printf( "%s", fstr );
			FILE *f = fopen( fstr, "rb" );
			if( !f ) {
				printf( " not found " );
				goto fail; }
			fseek( f, 0, SEEK_END );
			int fsize = ftell( f );
			if( fsize < 45 ) {
				printf( " invalid size " );
				goto fail; }
			fseek( f, 21, SEEK_SET );
			unsigned int format;
			unsigned char tmp2 [3] = "  \0";
			fread( &tmp2, 1, 2, f );
			format = tmp2[0]||(tmp2[1]<<8);
			printf( "\n format %d \n", format );
			fseek( f, 36, SEEK_SET );
			unsigned char tmp4 [5] = "   \0";
			fread( &tmp4, 1, 4, f );
			if( strcmp( tmp4, "data" ) ) {
				printf( " bytes 36-40 not \"data\" but %s \n", tmp4 );
				goto fail; }
			unsigned int dsize;
			fread( &tmp4, 1, 4, f );
			dsize = tmp4[0]|(tmp4[1]<<8)|(tmp4[2]<<16)|(tmp4[3]<<24);
			printf( " loading %d samples ", dsize/ssize );
			float *d = (float*) malloc( dsize );
			if( !d ) {
				printf( "malloc FAIL" );
				goto fail; }
			printf( " %d bytes read \n", fread( d, 1, dsize, f ) );
			fclose( f );
			routes[nroutes].wav = d;
			routes[nroutes].wav_len = dsize/ssize;
			for( int i=0; i<routes[nroutes].wav_len; i++ ) {
				char *b = (char*)(routes[nroutes].wav +i);
				memcpy( &tmp4, b, 4 );
				//b[0] = tmp4[3]
				//b[1] = tmp4[2]
				//b[2] = tmp4[1]
				//b[3] = tmp4[0]
				//if( *(routes[nroutes].wav+i) != *(routes[nroutes].wav+i) )
				//	*(routes[nroutes].wav+i) = 0.0
				printf( " %10.10f \n ", *(routes[nroutes].wav+i) ); }
			printf( "\n" );
			return 1;
			fail: {
				printf( "\n" );
				fclose( f );
				return 0; }}

		int route_add( int sd, int sc, int dd, int dc, char* fstr ) {
			if( fstr ) {
				if( !load_wav( fstr ) ) {
					printf( " route_add aborting \n" );
					return 0; }}
			else {
				routes[nroutes].wav = 0; }
			if( !devs[sd].in_stream ) rec( &devs[sd] );
			if( !devs[dd].out_stream ) play( &devs[dd] );
			routes[nroutes].sd = sd;
			routes[nroutes].sc = sc;
			routes[nroutes].dd = dd;
			routes[nroutes].dc = dc;
			routes[nroutes].last_src = -1;
			nroutes++; }

		void list() {
			char s1[4], s2[4]		;
			for( int i=0; i<ndevs; i++ ) {
				sprintf( s1, "%d", devs[i].nins );
				sprintf( s2, "%d", devs[i].nouts );
				printf( "  %s%d    %s %s    \"%s\" \"", i<10 ? " " : "", i,
					devs[i].nins ? s1 : "-", devs[i].nouts ? s2 : "-",
					Pa_GetHostApiInfo( devs[i].info->hostApi )->name );
				printf( "%s\"\n", devs[i].info->name ); }}

		int main( int agrc, char* argv )			 {
			printf( "\n\t%s\n\n", title )			;
			if( !init() ) return 1;
			
			list();

			printf(
				"\n\t srate %d samples/sec "
				"\n\t asize %d samples "
				"\n\t hsize %d samples "
				"\n\t ssize %d bytes "
				"\n\t stime %5.10f seconds "
				"\n\t  "
				"\n\t syntax: SRCDEV SRCCHAN DSTDEV DSTCHAN "
				"\n\t examples: "
				"\n\t\t 0 1 0 1 "
				"\n\t\t 22 7 1 1 "
				"\n\t\t 22 7 1 2 "
				"\n\t  "
				"\n\t type 'q' to exit "
				"\n  "
				"\n  ",
				srate, asize, hsize, ssize, stime );

			char cmd[1000] = "", fstr[100] = "";
			int sd, sc, dd, dc;
			float* wav;
			
			while( 1 ) {
				
				printf( "\t] " );
				gets( cmd );

				if( strcmp( cmd, "q" ) == 0 ) {
					return 0; }

				else if( strcmp( cmd, "l" ) == 0 ) {
					list(); }

				if( sscanf( cmd, "%d %d %d %d %s", &sd, &sc, &dd, &dc, fstr ) == 5 ) {
					route_add( sd, sc, dd, dc, fstr ); }
					
				else if( sscanf( cmd, "%d %d %d %d", &sd, &sc, &dd, &dc ) == 4 ) {
					route_add( sd, sc, dd, dc, 0 ); }}}