

	#define title "Portable Audio Console"

	#include <stdio.h>
	#include <stdlib.h>
	#include <string.h>
	#include <math.h>

	#include <portaudio.h>
	double PaUtil_GetTime( void );
	void PaUtil_InitializeClock( void );

	#define FAIL 1
	#define OK 0

	#define SR 44100

	int srate = SR;				// sample rate [samples/second]
	int hsize = SR;				// history size [samples]
	int asize = 1000;			// desired callback argument size (latency) [samples]
	int tsize = SR / 20;  			// tail size [samples]
	
	int csize = 0;				// input channel struct size [samples] tsize+hsize+3*asize
	int ssize = sizeof(float);		// sample size [bytes]
	double stime = 1.0 / SR;		// sample duration [seconds]
	
	int worst_latency = 0;			// max route rec+play latency [samples]

	float k[1] = {1.0}; // default

	struct route {
		int sd, sc, delay, new_delay;
		long long last_src;
		int ksize;
		float* k; };
	typedef struct route route;

	struct device {
		int id;
		const PaDeviceInfo *info;
		int nins, nouts;
		float *ins;
		route *outs;
		PaStream *stream;
		double t0, t;
		long long in_len, out_len;
		int max_in_asize;
		int max_out_asize; };
	typedef struct device device;

	device *devs = 0;
	int ndevs = 0;

	int init() {
		if( Pa_Initialize() ){
			printf( "ERROR: Pa_Initialize rubbish \n" );
			return FAIL; }
		ndevs = Pa_GetDeviceCount();
		if( ndevs <= 0 ) {
			printf( "ERROR: No Devices Found \n" );
			return FAIL; }
		PaUtil_InitializeClock();
		devs = (device*) malloc( sizeof(device) * ndevs );
		csize = tsize+hsize+3*asize;
		for( int i=0; i<ndevs; i++ ){
			devs[i].id = i;
			devs[i].info = Pa_GetDeviceInfo( i );
			devs[i].nins = devs[i].info->maxInputChannels;
			devs[i].nouts = devs[i].info->maxOutputChannels;
			devs[i].ins = (float*) malloc( devs[i].nins * csize * ssize );
			memset( devs[i].ins, 0, devs[i].nins * csize * ssize );
			devs[i].outs = (route*) malloc( devs[i].nouts * sizeof( route ) );
			memset( devs[i].outs, 0, devs[i].nouts * sizeof( route ) );
			for( int j=0; j<devs[i].nouts; j++ ){
				devs[i].outs[j].ksize = 1;
				devs[i].outs[j].k = k; }
			devs[i].max_in_asize = 0;
			devs[i].max_out_asize = 0;
			devs[i].stream = 0;
			devs[i].in_len = 0;
			devs[i].out_len = 0;
			devs[i].t0 = 0.0; }
		return OK; }

	inline void resync(){
		for( int i=0; i<ndevs; i++)
			if( devs[i].nouts && devs[i].stream )
				for( int j=0; j<devs[i].nouts; j++)
					devs[i].outs[j].last_src = 0; }

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
					memcpy( dev->ins +i*csize +tsize +ofs, in_data[i], frameCount*ssize ); 
			else {
				int x = ofs +frameCount -hsize;
				for( int i=0; i< dev->nins; i++ ){
					memcpy( dev->ins +i*csize +tsize +ofs, in_data[i], (frameCount-x)*ssize );
					memcpy( dev->ins +i*csize +tsize, in_data[i]+(frameCount-x), x*ssize ); }}
			dev->in_len += frameCount;
			if( dev->max_in_asize < frameCount )
				dev->max_in_asize = frameCount; }
		
		if( output ){
			for( int dc=0; dc<dev->nouts; dc++ ){
				int dd = dev->id;
				int sd = dev->outs[dc].sd;
				int sc = dev->outs[dc].sc; // source channel
				// int delay = dev->outs[dc].delay;
				
				if( !sd ){
					memset( out_data[dc], 0, frameCount*ssize );
					continue; }
				
				if( !devs[sd].in_len ){
					printf( "dev %d waiting dev %d to start \n\t] ", dd, sd );
					memset( out_data[dc], 0, frameCount*ssize );
					continue; }

				long long src;
				
				if( dev->outs[dc].last_src == 0 ){
					int lat = devs[sd].max_in_asize +dev->max_out_asize;
					if( lat > worst_latency ){
						worst_latency = lat;
						resync(); }
					src = (int)ceil((PaUtil_GetTime()-devs[sd].t0)/stime) - worst_latency -dev->outs[dc].delay -asize;
					printf( "route %d %d %d %d latency %d delay %d src %d \n\t] ", sd, sc, dd, dc, worst_latency, dev->outs[dc].delay, src ); }
				else
					src = dev->outs[dc].last_src;
				
				if( dev->outs[dc].new_delay ){
					src -= dev->outs[dc].new_delay -dev->outs[dc].delay;
					dev->outs[dc].delay = dev->outs[dc].new_delay;
					dev->outs[dc].new_delay = 0;
					printf( "%d %d %d %d delay now %d \n\t] ", sd, sc, dd, dc, dev->outs[dc].delay ); }
				
				if( src < 0  ){
					printf( "dev %d buffering dev %d src %d \n\t] ", dd, sd, src );
					memset( out_data[dc], 0, frameCount*ssize );
					continue; }
				
				int wtf;
				if( src +frameCount > devs[sd].in_len ){
					wtf = src +frameCount -devs[sd].in_len;
					printf( "%d %d %d %d wants to read %d future unsaved samples. will give wtf. \n\t] ", sd, sc, dd, dc, wtf );
					
				} else
					wtf = 0;
					
				int ofs = (src -wtf) % hsize;
				
				if( ofs +frameCount > hsize )
					memcpy( devs[sd].ins +sc*csize +tsize +hsize, devs[sd].ins +sc*csize +tsize, (ofs +frameCount -hsize)*ssize );
				else if( ofs -tsize < 0 )
					memcpy( devs[sd].ins +sc*csize +ofs, devs[sd].ins +sc*csize +hsize +ofs, (tsize-ofs)*ssize );

				float *sig = devs[sd].ins +sc*csize +tsize +ofs;
				for( int n=0; n<frameCount; n++ ){
					out_data[dc][n] = 0.0;
					for( int kn=0; kn<dev->outs[dc].ksize; kn++ )
						out_data[dc][n] += dev->outs[dc].k[kn]*sig[n-kn]; }

				dev->outs[dc].last_src = src +frameCount; }
					
			dev->out_len += frameCount;
			if( dev->max_out_asize < frameCount )
				dev->max_out_asize = frameCount; }
				
		if( dev->t0 == 0.0 )
			dev->t0 = PaUtil_GetTime();

		return paContinue; }
			
	int use_device( device *dev ) {
		printf( "%d starting ... \n\t] ", dev->id );
	
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
		
		PaError err = Pa_OpenStream( &(dev->stream),
			dev->nins ? &in_params : 0, dev->nouts ? &out_params : 0, srate,
			paFramesPerBufferUnspecified, paClipOff|paDitherOff,
			&device_tick, dev );

		if( err != paNoError ){
			if( err != paUnanticipatedHostError ) {
				printf( "ERROR 1: %s \n\t] ", Pa_GetErrorText( err ) );
				return FAIL; }
			else {
				const PaHostErrorInfo* herr = Pa_GetLastHostErrorInfo();
				printf( "ERROR 2: %s \n\t] ", herr->errorText );
				return FAIL; }}
		
		err = Pa_StartStream( dev->stream );
		if( err != paNoError ){
			printf( "ERROR 3: %s \n\t] ", Pa_GetErrorText( err ) );
			return FAIL; }

		while( dev->t0 == 0.0 );
		printf( "ok \n\t] " );
		return OK; }

	int route_add( int sd, int sc, int dd, int dc, int d ) {
		if( devs[dd].outs[dc].sd == sd && devs[dd].outs[dc].sc == sc ){
			if( devs[dd].outs[dc].delay != d )
				devs[dd].outs[dc].new_delay = d;
			return OK; }
		devs[dd].outs[dc].sd = sd;
		devs[dd].outs[dc].sc = sc;
		devs[dd].outs[dc].delay = d;
		if( !devs[sd].stream ) use_device( &devs[sd] );
		if( !devs[dd].stream ) use_device( &devs[dd] );
		if( !devs[sd].stream || !devs[dd].stream )
			return FAIL;
		while( devs[dd].outs[dc].last_src == 0 ); // wait all to play
		return OK; }

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
		if( init() ) return FAIL;
		
		const PaVersionInfo *vi = Pa_GetVersionInfo();
		printf( "%s\n\n", vi->versionText );
		
		list();

		printf( "\n\t srate %d samples/sec "
			"\n\t ssize %d bytes "
			"\n\t asize %d samples "
			"\n\t tsize %d samples "
			"\n\t hsize %d samples "
			"\n\t  "
			"\n\t syntax: SRCDEV SRCCHAN DSTDEV DSTCHAN [DELAYSAMPLES]"
			"\n\t examples: "
			"\n\t\t 0 0 1 0 5000"
			"\n\t\t 85 0 82 0 "
			"\n\t\t 85 1 82 1 "
			"\n\t  "
			"\n\t type 'q' to exit "
			"\n  "
			"\n  ",
			srate, ssize, asize, tsize, hsize );

		printf( "\t] " );
		
		char cmd[1000] = "";
		int sd, sc, dd, dc, d;
		
		while( 1 ){
		
			gets( cmd );
			printf( "\t] " );

			if( strcmp( cmd, "q" ) == 0 )
				return OK;

			else if( strcmp( cmd, "l" ) == 0 )
				list();

			else if( sscanf( cmd, "%d %d %d %d %d", &sd, &sc, &dd, &dc, &d ) == 5 )
				route_add( sd, sc, dd, dc, d );
				
			else if( sscanf( cmd, "%d %d %d %d", &sd, &sc, &dd, &dc ) == 4 )
				route_add( sd, sc, dd, dc, 0 );

			else if( strcmp( cmd, "resync" ) == 0 )
				resync();
			
			else if( strcmp( cmd, "status" ) == 0 )
				printf( "Latency %d \n\t] ", worst_latency );

		}
	}
