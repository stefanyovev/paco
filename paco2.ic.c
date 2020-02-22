
							#define title "Portable Audio Console"

							#include <stdio.h>
							#include <stdlib.h>
							#include <string.h>
							
							#include <math.h>
							#include <portaudio.h>
							
							#define history 1.0 // seconds							
							#define rate 48000 // samples/second							
							#define latency 0.1 // seconds
							
							int history_size;
							double sample_time;
							
							typedef struct device {
								const PaDeviceInfo *info;
								int id, nbufs, nouts;
								float *bufs;
								PaStream *rec, *play;
								const PaStreamInfo *rec_info, *play_info;
								double rec_t0, adc, dac, play_t0; }
							device;
							
							int ndevs = 0;
							device *devs = 0;
							
							double play_latency = 0.0;

							typedef struct route {
								int src_dev, src_chan, dst_dev, dst_chan; }
							route;

							int nroutes = 0;
							route routes[100];
							
							int init() {
								history_size = (int) ceil( ((double)rate)*((double)history) );
								sample_time = 1.0/((double)rate);
								PaError err	;
								err = Pa_Initialize();
								if( err != paNoError ) {
									printf( "ERROR: Pa_Initialize returned 0x%x\n", err );
									return 0; }
								ndevs = Pa_GetDeviceCount();
								if( ndevs <= 0 ) {
									printf( "ERROR: dev count <= 0" );
									return 0; }
								devs = (device*) malloc( sizeof( device ) * ndevs );
								for( int i=0; i<ndevs; i++ ) {
									devs[i].id = i;
									devs[i].info = Pa_GetDeviceInfo( i );
									devs[i].nbufs = devs[i].info->maxInputChannels;
									devs[i].nouts = devs[i].info->maxOutputChannels;
									devs[i].bufs = (float*) malloc( devs[i].nbufs * history_size * sizeof(float) );
									for( int j=0; j<devs[i].nbufs; j++ ) {
										for( int k=0; k<history_size; k++ ) {
											devs[i].bufs[j*history_size +k] = 0.0; }}
									devs[i].rec = 0;
									devs[i].play = 0; }}
										
							// ############################################################################################################################################################	
							// CALLBACKS
							
							PaStreamCallbackResult on_device_data(
								const void **input,
								void **output,
								unsigned long frameCount,
								const PaStreamCallbackTimeInfo *timeInfo,
								PaStreamCallbackFlags statusFlags,
								void *userData ) {
								
								device* dev = (device*) userData;
								float **in = input;
								double t = timeInfo->currentTime;
								double adc = timeInfo->inputBufferAdcTime;
								
								// printf( "%d  rec t: %10.15f\n", dev->id, t )
								
								if( adc == 0.0 ) {
									printf( "%d recording \n", dev->id );
									dev->rec_t0 = t; }
																		
								else if( adc < dev->adc ) {
									printf( "adc replace %d old samples", (int)((dev->adc-adc)/sample_time) ); }
									
								else if( adc > dev->adc ) {
									printf( "adc miss %d samples", (int)((adc-dev->adc)/sample_time) ); }
								
								dev->adc = adc;
								
								long long adc_samples = (long long) round(adc/sample_time);
								int buf_ofs = adc_samples % history_size;
								
								if( buf_ofs +frameCount < history_size )								 {
									for( int i=0; i< dev->nbufs; i++ ) {
										memcpy(
											dev->bufs +i*history_size +buf_ofs,
											in[i],
											frameCount * sizeof(float) ); }}
								else {
									int count = history_size -buf_ofs;
									for( int i=0; i< dev->nbufs; i++ ) {
										memcpy(
											dev->bufs +i*history_size +buf_ofs,
											in[i],
											count * sizeof(float) );
										memcpy(
											dev->bufs +i*history_size,
											in[i] +count,
											(frameCount -count) * sizeof(float) ); }}
									
								dev->adc += frameCount * sample_time;
								return paContinue; }

							PaStreamCallbackResult on_write_to_device(
								const void **input,
								void **output,
								unsigned long frameCount,
								const PaStreamCallbackTimeInfo *timeInfo,
								PaStreamCallbackFlags statusFlags,
								void *userData ) {

								device* dev = (device*) userData;
								float **out = output;
								double t = timeInfo->currentTime;
								double dac = timeInfo->outputBufferDacTime;

								if( dac == 0.0 ) {
									printf( "%d playing \n", dev->id );
									dev->play_t0 = t; }

								else if( dac < dev->dac ) {
									printf( "dac read %d old samples", (int)((dev->dac-dac)/sample_time) ); }
									
								else if( dac > dev->dac ) {
									printf( "dac miss %d samples", (int)((dac-dev->dac)/sample_time) ); }

								dev->dac = dac;
								
								for( int i=0; i< nroutes; i++ ) {
									if( routes[i].dst_dev == dev->id ) {
										
										// printf( "%d play t: %10.15f\n", dev->id, t )
										
										if( dev->play_t0 +dev->dac -play_latency > devs[routes[i].src_dev].rec_t0 +devs[routes[i].src_dev].adc ) {
											printf( "buffering %d %d %d %d\n", routes[i].src_dev, routes[i].src_chan, routes[i].dst_dev, routes[i].dst_chan );
											continue; }

										double dt = dev->play_t0 -devs[routes[i].src_dev].rec_t0 -play_latency										;
										double adc = dac -dt;
										long long adc_samples = (long long) round(adc/sample_time);
										int buf_ofs = adc_samples % history_size;
										
										if( buf_ofs +frameCount < history_size ) {
											memcpy(
												out[routes[i].dst_chan],
												devs[routes[i].src_dev].bufs +routes[i].src_chan*history_size +buf_ofs,
												frameCount *sizeof(float) ); }
										else {
											int count = history_size -buf_ofs;
											memcpy(
												out[routes[i].dst_chan],
												devs[routes[i].src_dev].bufs +routes[i].src_chan*history_size +buf_ofs,
												count *sizeof(float) );
											memcpy(
												out[routes[i].dst_chan] +count,
												devs[routes[i].src_dev].bufs +routes[i].src_chan*history_size,
												(frameCount -count) * sizeof(float) ); }}}
												
								dev->dac += frameCount * sample_time;
								return paContinue; }

							// ############################################################################################################################################################	

							void rec( device *dev ) {
								static PaStreamParameters params;
								params.device = dev->id;
								params.sampleFormat = paFloat32|paNonInterleaved;
								params.hostApiSpecificStreamInfo = 0;
								params.suggestedLatency = latency;
								params.channelCount = dev->nbufs;
								PaError err;
								err = Pa_OpenStream( &(dev->rec), &params, NULL, rate,
									paFramesPerBufferUnspecified, paClipOff|paDitherOff,
									&on_device_data, dev );
								if( err != paNoError ) {
									if( err == paUnanticipatedHostError ) {
										const PaHostErrorInfo* herr = Pa_GetLastHostErrorInfo();
										printf( "1ERRORh: %s", herr->errorText ); }
									else {
										printf( "2ERROR: %s\n", Pa_GetErrorText( err ) ); }}
								dev->rec_info = Pa_GetStreamInfo( dev->rec );
								printf( "%d rec starting, latency %5.3f s\n", dev->id, dev->rec_info->inputLatency );
								err = Pa_StartStream( dev->rec );
								if( err != paNoError ) {
									printf( "3ERROR: %s\n", Pa_GetErrorText( err ) ); }}
									
							void play( device *dev ) {
								static PaStreamParameters params;
								params.device = dev->id;
								params.sampleFormat = paFloat32|paNonInterleaved;
								params.hostApiSpecificStreamInfo = 0;
								params.suggestedLatency = latency;
								params.channelCount = dev->nouts;
								PaError err;
								err = Pa_OpenStream( &(dev->play), NULL, &params, rate,
									paFramesPerBufferUnspecified, paClipOff|paDitherOff,
									&on_write_to_device, dev );
								if( err != paNoError ) {
									if( err == paUnanticipatedHostError ) {
										const PaHostErrorInfo* herr = Pa_GetLastHostErrorInfo();
										printf( "1ERRORh: %s", herr->errorText ); }
									else {
										printf( "2ERROR: %s\n", Pa_GetErrorText( err ) ); }}
								dev->play_info = Pa_GetStreamInfo( dev->play );
								if( dev->play_info->outputLatency > play_latency ) {
									play_latency = dev->play_info->outputLatency;
									printf( "play_latency %5.3f s \n", play_latency ); }
								printf( "%d play starting, latency %5.3f s\n", dev->id, dev->play_info->outputLatency );
								err = Pa_StartStream( dev->play );
								if( err != paNoError ) {
									printf( "3ERROR: %s\n", Pa_GetErrorText( err ) ); }}

							int route_add( int src_dev, int src_chan, int dst_dev, int dst_chan ) {
								if( !devs[src_dev].rec ) rec( &devs[src_dev] );
								if( !devs[dst_dev].play ) play( &devs[dst_dev] );
								routes[nroutes].src_dev = src_dev;
								routes[nroutes].src_chan = src_chan;
								routes[nroutes].dst_dev = dst_dev;
								routes[nroutes].dst_chan = dst_chan;
								nroutes++; }

							// ############################################################################################################################################################	

							void list() {
								const PaDeviceInfo *dev_info		;
								char s1[4], s2[4]		;
								for( int i=0; i<ndevs; i++ ) {
									dev_info = Pa_GetDeviceInfo( i )			;
									sprintf( s1, "%d", devs[i].nbufs );
									sprintf( s2, "%d", devs[i].nouts );
									printf(
										"  %s%d    %s %s    \"%s\" \"", i<10 ? " " : "", i,
										devs[i].nbufs ? s1 : "-",
										devs[i].nouts ? s2 : "-",
										Pa_GetHostApiInfo( dev_info->hostApi )->name );
									printf( "%s\"\n", dev_info->name ); }}

							//void buf_stat()
							//	for( int d=0; d<ndevs; d++ )
							//		if( devs[d].play )
							//			for( int o=0; o<devs[d].nouts; o++ )
							//				if( devs[d].outs[o].buf )
							//					printf( " dev %d out %d lag %d samples\n", d, o, buf_nused( devs[d].outs[o].buf, devs[d].outs[o].client_id ) )


							// ############################################################################################################################################################
							// console
								
							int main( int agrc, char* argv) {
								
								printf( "\n\t%s\n\n", title );
								
								// buf_test()
								
								if( !init() ) return 1;
								
								list();

								printf( "\n\thistory %5.3f s\n", history );
								printf( "\trate %d samples/s\n", rate );
								printf( "\ttarget latency %5.3f s\n", latency );
								
								printf( "\n\tsyntax: SRCDEV SRCCHAN DSTDEV DSTCHAN\n\n\texamples:\n\t\t] 0 1 0 1\n\t\t] 22 7 1 1\n\t\t] 22 7 1 2\n\n\ttype 'q' to exit\n\n", title );
								
								char cmd[1000] = "";
								int sd, sc, dd, dc;
								
								while( 1 ) {
									
									printf( "\t] " );
									gets( cmd );

									if( sscanf( cmd, "%d %d %d %d", &sd, &sc, &dd, &dc ) == 4 ) {
										route_add( sd, sc, dd, dc ); }

									else if( strcmp( cmd, "q" ) == 0 ) {
										return 0; }

									else if( strcmp( cmd, "l" ) == 0 ) {
										list(); }}}