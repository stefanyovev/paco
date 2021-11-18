

		#define title "Portable Audio Console"

		#include <stdio.h>
		#include <stdlib.h>
		#include <string.h>
		#include <math.h>
		
		#include <portaudio.h>
		void PaUtil_InitializeClock( void );
		double PaUtil_GetTime( void );

		#define SR 44100
		
		int srate = SR;					// sample rate [samples/second]
		double stime = 1.0 / SR;		// sample duration [seconds]
		int ssize = sizeof(float);		// sample size [bytes]
		int asize = 1000;				// desired callback argument size (latency) [samples]
		int hsize = 3000;				// history size [samples]
		int max_in_asize = 0;			// max rec latency [samples]
		int max_out_asize = 0;			// max play latency [samples]

		struct route {
			int sd, sc;
			long long last_src; };
		typedef struct route route;	

		struct device {
			int id;
			const PaDeviceInfo *info;
			int nins, nouts;
			float *ins;
			route *outs;
			PaStream *stream;
			const PaStreamInfo *stream_info;
			double t0, t;
			long long in_len, out_len; };
		typedef struct device device;

		device *devs = 0;
		int ndevs = 0;

		int init() {
			if( Pa_Initialize() ) {
				printf( "ERROR: Pa_Initialize rubbish \n" );
				return 0; }
			ndevs = Pa_GetDeviceCount();
			if( ndevs <= 0 ) {
				printf( "ERROR: No Devices Found \n" );
				return 0; }
			devs = (device*) malloc( sizeof(device) * ndevs );
			for( int i=0; i<ndevs; i++ ) {
				devs[i].id = i;
				devs[i].info = Pa_GetDeviceInfo( i );
				devs[i].nins = devs[i].info->maxInputChannels;
				devs[i].nouts = devs[i].info->maxOutputChannels;
				devs[i].ins = (float*) malloc( devs[i].nins * hsize * ssize );
				memset( devs[i].ins, 0, devs[i].nins * hsize * ssize );
				devs[i].outs = (route*) malloc( devs[i].nouts * sizeof( route ) );
				memset( devs[i].outs, 0, devs[i].nouts * sizeof( route ) );
				devs[i].stream = 0;
				devs[i].in_len = 0;
				devs[i].out_len = 0;
				devs[i].t0 = 0.0; }
			PaUtil_InitializeClock(); }

		PaStreamCallbackResult device_tick(
			const void **input,
			void **output,
			unsigned long frameCount,
			const PaStreamCallbackTimeInfo *timeInfo,
			PaStreamCallbackFlags statusFlags,
			void *userData ){
			
			device* dev = (device*) userData;
			
			float **in_data = input;
			float **out_data = output;

			if( input ){
				int ofs = dev->in_len % hsize;
				if( frameCount <= hsize -ofs )
					for( int i=0; i< dev->nins; i++ )
						memcpy( dev->ins +i*hsize +ofs, in_data[i], frameCount*ssize ); 
				else {
					int x = ofs +frameCount -hsize;
					for( int i=0; i< dev->nins; i++ ){
						memcpy( dev->ins +i*hsize +ofs, in_data[i], (frameCount-x)*ssize );
						memcpy( dev->ins +i*hsize, in_data[i]+(frameCount-x), x*ssize ); }}
				dev->in_len += frameCount;
				if( dev->t0 == 0.0 )
					dev->t0 = PaUtil_GetTime();
				if( max_in_asize < frameCount )
					max_in_asize = frameCount; }
				
			if( output ){
				for( int dc=0; dc<dev->nouts; dc++ ){
					int dd = dev->id;
					int sd = dev->outs[dc].sd;
					int sc = dev->outs[dc].sc;
					
					if( !sd ){
						memset( out_data[dc], 0, frameCount*ssize );
						continue; }
					
					if( !devs[sd].in_len ){
						printf( "%d waiting %d to start \n", dd, sd );
						continue; }

					long long src;
					if( dev->outs[dc].last_src == 0 )
						src = (int)ceil((PaUtil_GetTime()-devs[sd].t0)/stime) -max_in_asize -max_out_asize -50;
					else
						src = dev->outs[dc].last_src;
						
					if( src < 0  ){
						printf( "%d buffering %d src %d t-t0 %d t %10.10f \n", dd, sd, src, (int)(ceil( (PaUtil_GetTime() -devs[sd].t0) /stime )), PaUtil_GetTime() );
						continue; }
					if( src +frameCount > devs[sd].in_len ){
						printf( "%d wants to read %d future unsaved samples from %d \n", dd, src +frameCount -devs[sd].in_len, sd );
						continue; }
					
					int ofs = src % hsize;
					if( ofs +frameCount <= hsize )
						memcpy( out_data[dc], devs[sd].ins +sc*hsize +ofs, frameCount*ssize );
					else {
						int x = ofs +frameCount -hsize;
						memcpy( out_data[dc], devs[sd].ins +sc*hsize +ofs, (frameCount-x)*ssize );
						memcpy( out_data[dc]+(frameCount-x), devs[sd].ins +sc*hsize, x*ssize ); }
						
					dev->outs[dc].last_src = src +frameCount; }
						
				dev->out_len += frameCount;
				if( dev->t0 == 0.0 )
					dev->t0 = PaUtil_GetTime();
				if( max_out_asize < frameCount )
					max_out_asize = frameCount; }

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
			
			err = Pa_StartStream( dev->stream );
			if( err != paNoError ) {
				printf( "ERROR 3: %s \n", Pa_GetErrorText( err ) ); }}

		int route_add( int sd, int sc, int dd, int dc ) {
			if( !devs[sd].stream ) use_device( &devs[sd] );
			if( !devs[dd].stream ) use_device( &devs[dd] );
			devs[dd].outs[dc].sd = sd;
			devs[dd].outs[dc].sc = sc; }

		void list() {
			char s1[4], s2[4];		
			for( int i=0; i<ndevs; i++ ) {
				sprintf( s1, "%d", devs[i].nins );
				sprintf( s2, "%d", devs[i].nouts );
				printf( "  %s%d    %s %s    \"%s\" \"", i<10 ? " " : "", i,
					devs[i].nins ? s1 : "-", devs[i].nouts ? s2 : "-",
					Pa_GetHostApiInfo( devs[i].info->hostApi )->name );
				printf( "%s\"\n", devs[i].info->name ); }}

		int main( int agrc, char* argv ) {			
			printf( "\n\t%s\n\n", title );
			if( !init() ) return 1;
			
			const PaVersionInfo *vi = Pa_GetVersionInfo();
			printf( "%s\n\n", vi->versionText );
			
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
