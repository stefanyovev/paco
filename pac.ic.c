

		#define title "Portable Audio Console"

		#include <stdio.h>
		#include <stdlib.h>
		#include <string.h>

		#include <math.h>
		#include <portaudio.h>
		
		#define SR 44100
		
		int srate = SR; // sample rate [samples/second];
		double stime = 1.0 / SR; // sample duration [seconds];
		int ssize = sizeof(float); // sample size [bytes];
		int asize = 10000; // desired callback argument size [samples];
		int hsize = 100000; // history size [samples];
		int max_asize = 0; // max play callback argument size global [samples];

		typedef struct device {
			int id;
			const PaDeviceInfo *info;
			int nins, nouts;
			float *ins;
			PaStream *stream;
			const PaStreamInfo *stream_info;
			double t0, t;
			long long in_len, out_len; }
		device;
		int ndevs = 0;
		device *devs = 0;

		typedef struct route {
			int sd, sc, dd, dc;
			long long last_src; }
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
				devs[i].stream = 0;
				devs[i].in_len = 0;
				devs[i].out_len = 0; }}

		PaStreamCallbackResult device_tick(
			const void **input,
			void **output,
			unsigned long frameCount,
			const PaStreamCallbackTimeInfo *timeInfo,
			PaStreamCallbackFlags statusFlags,
			void *userData ) {
			
			device* dev = (device*) userData;
			double t = timeInfo->currentTime;
			dev->t0 = t;
			
			// printf( "\n tick %d %d ", dev->id, frameCount )
			
			if( input ) {
				printf( "i" );
				float **in_data = input;
				long long dst = (long long) round( timeInfo->inputBufferAdcTime /stime );
				
				if( dst < dev->in_len ) {
					printf( "%d in replacing %d old samples \n", dev->id, dev->in_len -dst ); }
				else if( dst < dev->in_len ) {
					printf( "%d in missing %d samples \n", dev->id, dst -dev->in_len ); }
				else if( dst == 0 ) {
					printf( "%d recording \n", dev->id ); }

				dev->in_len = dst;
				
				int ofs = dst % hsize;
				
				if( frameCount <= hsize -ofs ) {
					for( int i=0; i< dev->nins; i++ ) {
						memcpy(
							dev->ins +i*hsize +ofs, in_data[i],
							frameCount *ssize ); }}
				else {
					int x = ofs +frameCount -hsize;
					for( int i=0; i< dev->nins; i++ ) {
						memcpy(
							dev->ins +i*hsize +ofs, in_data[i],
							(frameCount -x) *ssize );
						memcpy(
							dev->ins +i*hsize, in_data[i]+x,
							x *ssize ); }}
					
				dev->in_len += frameCount; }
				
			if( output ) {
				printf( "o" );
				float **out_data = output;
				long long dac = (long long) round( timeInfo->outputBufferDacTime /stime );
				
				if( dac < dev->out_len ) {
					printf( "dac wants %d old samples", dev->out_len -dac ); }
				else if( dac > dev->out_len ) {
					printf( "dac miss %d samples", dac -dev->out_len ); }
				else if( dac == 0.0 ) {
					printf( "%d playing \n", dev->id ); }

				dev->out_len = dac;
				
				for( int sd, sc, dd, dc, i=0; i<nroutes; i++ ) {
					sd = routes[i].sd; sc = routes[i].sc; dd = routes[i].dd; dc = routes[i].dc				;
					
					if( dd == dev->id ) {

						if( !devs[sd].in_len ) {
							printf( "%d wating %d to start \n", dd, sd );
							continue; }

						long long src;
						
						if( routes[i].last_src == -1 ) {
							src = (int) ceil( (t -devs[sd].t0) /stime ) -max_asize;
							if( devs[sd].in_len -src > hsize ) {
								printf( " !!!!!!!!!!!!!!!!!!! \n" ); }}
						else {
							src = routes[i].last_src; }

						//if( routes[i].last_src != -1 && routes[i].last_src != src )
						//	printf( "%d %d src - last_src %d \n", sd, dd, src -routes[i].last_src )

						if( src +frameCount > devs[sd].in_len ) {
							printf( "%d wants to read %d future unsaved samples from %d \n", dd, src +frameCount -devs[sd].in_len, sd );
							continue; }

						if( src < 0  ) {
							printf( "%d buffering %d src %d t-t0 %d t %10.10f \n", dd, sd, src, (int)(ceil( (t -devs[sd].t0) /stime )), t );
							continue; }
						
						int ofs = src % hsize;
						if( ofs +frameCount <= hsize ) {
							memcpy(
								out_data[dc], devs[sd].ins +sc*hsize +ofs,
								frameCount *ssize ); }
						else {
							int x = ofs +frameCount -hsize;
							memcpy(
								out_data[dc], devs[sd].ins +sc*hsize +ofs,
								(frameCount -x) *ssize );
							memcpy(
								out_data[dc] +x, devs[sd].ins +sc*hsize,
								x *ssize ); }

						routes[i].last_src = src +frameCount; }}
						
				dev->out_len += frameCount; }

			return paContinue; }
				
		void use_device( device *dev ) {
			printf( "%d starting \n", dev->id );
		
			static PaStreamParameters in_params;
			in_params.device = dev->id;
			in_params.sampleFormat = paFloat32|paNonInterleaved;
			in_params.hostApiSpecificStreamInfo = 0;
			in_params.suggestedLatency = asize *stime;
			in_params.channelCount = dev->nins;

			static PaStreamParameters out_params;
			out_params.device = dev->id;
			out_params.sampleFormat = paFloat32|paNonInterleaved;
			out_params.hostApiSpecificStreamInfo = 0;
			out_params.suggestedLatency = asize *stime;
			out_params.channelCount = dev->nouts;
			
			PaError err = Pa_OpenStream(
				&(dev->stream), dev->nins ? &in_params : 0, dev->nouts ? &out_params : 0,
				srate, paFramesPerBufferUnspecified, paClipOff|paDitherOff,
				&device_tick, dev );
				
			if( err != paNoError ) {
				if( err != paUnanticipatedHostError ) {
					printf( "ERROR 1: %s \n", Pa_GetErrorText( err ) ); }
				else {
					const PaHostErrorInfo* herr = Pa_GetLastHostErrorInfo();
					printf( "ERROR 2: %s \n", herr->errorText ); }}
			
			// todo:
			dev->stream_info = Pa_GetStreamInfo( dev->stream );
			int actual_srate = (int) round( dev->stream_info->sampleRate );
			int actual_asize = (int) round( dev->stream_info->inputLatency /stime );
			printf( "%d rec SR %d asize %d starting \n", dev->id, actual_srate, actual_asize );
			
			err = Pa_StartStream( dev->stream );
			
			if( err != paNoError ) {
				printf( "ERROR 3: %s \n", Pa_GetErrorText( err ) ); }}

		int route_add( int sd, int sc, int dd, int dc ) {
			if( !devs[sd].stream ) use_device( &devs[sd] );
			if( !devs[dd].stream ) use_device( &devs[dd] );
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

			char cmd[1000] = "";
			int sd, sc, dd, dc;
			
			while( 1 ) {
				
				printf( "\t] " );
				gets( cmd );

				if( strcmp( cmd, "q" ) == 0 ) {
					return 0; }

				else if( strcmp( cmd, "l" ) == 0 ) {
					list(); }

				else if( sscanf( cmd, "%d %d %d %d", &sd, &sc, &dd, &dc ) == 4 ) {
					route_add( sd, sc, dd, dc ); }}}