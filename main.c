

    #define title "Paco Sound Router"


    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <math.h>
    #include <windows.h>    
    #include <portaudio.h>
    double PaUtil_GetTime( void );
    void PaUtil_InitializeClock( void );


    #define FAIL 1
    #define OK 0
    #define PRINT printf

    #define SR 48000

    double stime = 1.0 / SR;		           // sample duration [seconds]

    int srate = SR;			                   // sample rate [samples/second]
    int msize = SR;                            // channel memory size [samples]
    int tsize = SR / 20;                       // tail size [samples]
    int asize = SR / 50;                       // desired frameCount [samples]
    int csize; // = tsize + msize + 5*asize    // input channel struct size [samples]

    double T0;                                 // program t0 [seconds]
    #define NOW ((long)(ceil((PaUtil_GetTime()-T0)/stime))) // [samples]

    int Lag = 0;		 	                    // max route rec+play latency [samples]

    float k[1] = {1.0};                        // default fir


    struct route {                                                                           // ROUTE
        int sd, sc, dd, dc, delay;
        long last_cursor;
        int ksize;
        float* k; };
    typedef struct route route;	
    route *routes = 0;
    int nroutes = 0;


    struct device {                                                                          // DEVICE
        int id;
        PaStream *stream;
        const PaDeviceInfo *info;
        int nins, nouts;
        float *ins;
        route *outs;
        double in_t0, out_t0;
        long in_len, out_len;
        int max_in_asize, max_out_asize; };
    typedef struct device device;
    device *devs = 0;
    int ndevs = 0;


    int init() {                                                                                // INIT
        if( Pa_Initialize() ){ PRINT( "ERROR: Pa_Initialize rubbish \n" ); return FAIL; }
        ndevs = Pa_GetDeviceCount();
        if( ndevs <= 0 ) { PRINT( "ERROR: No Devices Found \n" ); return FAIL; }
        PaUtil_InitializeClock();
        T0 = PaUtil_GetTime();
        devs = (device*) malloc( sizeof(device) * ndevs );
        memset( devs, 0, sizeof(device) * ndevs );
        csize = tsize + msize + 5*asize;
        for( int i=0; i<ndevs; i++ ){            
            devs[i].id = i;
            devs[i].info = Pa_GetDeviceInfo( i );
            devs[i].nins = devs[i].info->maxInputChannels;
            devs[i].nouts = devs[i].info->maxOutputChannels;
            devs[i].ins = (float*) malloc( devs[i].nins * csize * sizeof(float) );
            devs[i].outs = (route*) malloc( devs[i].nouts * sizeof(route) );
            memset( devs[i].ins, 0, devs[i].nins * csize * sizeof(float) );
            memset( devs[i].outs, 0, devs[i].nouts * sizeof(route) );
            for( int j=0; j<devs[i].nouts; j++ ){
                devs[i].outs[j].ksize = 1;
                devs[i].outs[j].k = k; }}
        return OK; }


    void resync(){                                                                             // RESYNC
        for( int i=0; i<ndevs; i++ )
            for( int j=0; j<devs[i].nouts; j++ )
                devs[i].outs[j].last_cursor = 0; }


    PaStreamCallbackResult device_tick(                                                         // DEVICE TICK
        float **input,
        float **output,
        unsigned long frameCount,
        const PaStreamCallbackTimeInfo *timeInfo,
        PaStreamCallbackFlags statusFlags,
        void *self ){
        
        device *dev = (device*) self;		
        route *R;
        int sd, sc, dd, dc, lag, missing, ofs, n, kn, i, x;
        long cursor;
        float *sig;

        if( input ){
            if( dev->max_in_asize < frameCount )
                dev->max_in_asize = frameCount;
            ofs = dev->in_len % msize;
            if( ofs +frameCount <= msize )
                for( i=0; i< dev->nins; i++ )
                    memcpy( dev->ins +i*csize +tsize +ofs, input[i], frameCount*sizeof(float) ); 
            else {
                x = ofs +frameCount -msize;
                for( int i=0; i< dev->nins; i++ ){
                    memcpy( dev->ins +i*csize +tsize +ofs, input[i], (frameCount-x)*sizeof(float) );
                    memcpy( dev->ins +i*csize +tsize, input[i]+(frameCount-x), x*sizeof(float) ); }}
            if( dev->in_t0 == .0 )
                dev->in_t0 = NOW;
            dev->in_len += frameCount; }

        if( output ){
            if( dev->max_out_asize < frameCount )
                dev->max_out_asize = frameCount;
            // ################################################################################################ // ROUTE TICK
            for( dc=0; dc < dev->nouts; dc++ ){
            
                dd = dev->id;
                R = dev->outs +dc;
                sd = R->sd;
                sc = R->sc;
                
                memset( output[dc], 0, frameCount * sizeof(float) );
                
                if( !sd || !devs[sd].in_len )
                    continue;

                if( R->last_cursor == 0 ){
                    lag = (int) ceil( dev->max_out_asize * 6.2 );
                    if( lag > Lag ){
                        Lag = lag;
                        resync(); }
                    cursor = NOW -devs[sd].in_t0 -Lag;
                    PRINT( "[%d.%d -> %d.%d] INIT; Lag %d; delay %d; cursor %d \n ", sd, sc, dd, dc, Lag, R->delay, cursor ); }
                else	
                    cursor = R->last_cursor;

                if( cursor < 0  )
                    continue;

                missing = 0;
                if( cursor +frameCount > devs[sd].in_len ){
                    missing = cursor +frameCount -devs[sd].in_len;
                    PRINT( "![%d.%d -> %d.%d] REPLAY %d samples. underrun. \n ", sd, sc, dd, dc, missing ); }
                    
                ofs = (cursor -(R->delay) -missing) % msize;

                if( missing > 0 )
                    resync();

                if( ofs +frameCount > msize )
                    memcpy( devs[sd].ins +sc*csize +tsize +msize, devs[sd].ins +sc*csize +tsize, (ofs +frameCount -msize)*sizeof(float) );
                else if( ofs -tsize < 0 )
                    memcpy( devs[sd].ins +sc*csize +ofs, devs[sd].ins +sc*csize +msize +ofs, (tsize-ofs)*sizeof(float) );

                sig = devs[sd].ins +sc*csize +tsize +ofs;
                for( n=0; n < frameCount; n++ ){
                    for( kn=0; kn < R->ksize; kn++ )
                        output[dc][n] += R->k[kn]*sig[n-kn]; }

                R->last_cursor = cursor +frameCount; } 
                
                // ############################################################################################### // /ROUTE MAIN				
                
            if( dev->out_t0 == .0 )
                dev->out_t0 = NOW;
                
            dev->out_len += frameCount; }
                
        return paContinue; }

            
    int use_device( device *dev ){                                                                      // +DEVICE
        if( dev->stream ) return OK;        
        PRINT( "%d starting ... \n ", dev->id );
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
                PRINT( "ERROR 1: %s \n ", Pa_GetErrorText( err ) );
                return FAIL; }
            else {
                const PaHostErrorInfo* herr = Pa_GetLastHostErrorInfo();
                PRINT( "ERROR 2: %s \n ", herr->errorText );
                return FAIL; }}
        err = Pa_StartStream( dev->stream );
        if( err != paNoError ){
            PRINT( "ERROR 3: %s \n ", Pa_GetErrorText( err ) );
            return FAIL; }
        while( !dev->in_len && !dev->out_len );
        PRINT( "ok \n " );
        return OK; }


    route *rr = 0;
    int route_add( int sd, int sc, int dd, int dc, int d ) {                                              // +ROUTE
        route *R = (devs+dd)->outs+dc;
        if( R->sd == sd && R->sc == sc ){
            if( R->delay != d )
                R->delay += d - R->delay; 
            return OK; }
        R->sd = sd; R->sc = sc;
        R->dd = dd; R->dc = dc;
        R->delay = d;
        if( !devs[sd].in_len ) use_device( &devs[sd] );
        if( !devs[dd].out_len ) use_device( &devs[dd] );
        if( !devs[sd].in_len || !devs[dd].out_len )
            return FAIL;
        while( R->last_cursor == 0 ); // wait
        rr = R;
        return OK; }


    // ---------------------------------------------------------------------------------------------------------------


    const int width = 530;
    const int height = 400;
    const int vw = 10000; // viewport width samples

    struct pixel {
      union {
        struct { unsigned char b, g, r, a; };
        int val; };
    }; typedef struct pixel pixel;

    pixel *pixels; // from CreateDIBSection

    HBITMAP hbmp;
    HANDLE hTickThread;
    HWND hwnd;
    HDC hdcMem;

    HWND hCombo1, hCombo2, hBtn;


    DWORD WINAPI Drawing_Thread_Main( HANDLE handle ){
        Sleep( 100 );                                                     // time for main thread to finish

        ShowWindow( hwnd, SW_SHOW );

        HDC hdc = GetDC( hwnd );
        hdcMem = CreateCompatibleDC( hdc );
        HBITMAP hbmOld = (HBITMAP) SelectObject( hdcMem, hbmp );
        RECT rc;
        
        char txt[100000];
        //GetClientRect( hwnd, &rc );        
        //DrawText( hdc, "Loading...", -1, &rc, DT_CENTER );

        const PaVersionInfo *vi;
        if( init() == FAIL )
            ;// MSGBOX
        //vi = Pa_GetVersionInfo(); // vi->versionText

        for( int i=0; i<ndevs; i++ ){
            if( devs[i].nins ){
                sprintf( txt, " %3d  / %s /  %s ", i, Pa_GetHostApiInfo( devs[i].info->hostApi )->name, devs[i].info->name );
                SendMessage( hCombo1, CB_ADDSTRING, 0, txt ); }
            SendMessage( hCombo1, CB_SETCURSEL, (WPARAM)0, (LPARAM)0 );
            if( devs[i].nouts ){
                sprintf( txt, " %3d  / %s /  %s ", i, Pa_GetHostApiInfo( devs[i].info->hostApi )->name, devs[i].info->name );
                SendMessage( hCombo2, CB_ADDSTRING, 0, txt ); }
            SendMessage( hCombo2, CB_SETCURSEL, (WPARAM)0, (LPARAM)0 ); }

        for( ; ; ){
            if( rr ){
            
                memset( pixels, 128, width*height*4 );
            
                double Q = (((double)width)/((double)vw));            
                int x1 = (int)ceil(    Q*( (double)devs[rr->sd].in_t0 + (double)rr->last_cursor        -(double)NOW  +(((double)vw)/2.0)   )   );
                int x2 = (int)ceil(    Q*( (double)devs[rr->sd].in_t0 + (double)devs[rr->sd].in_len    -(double)NOW  +(((double)vw)/2.0)   )   );

                Rectangle( hdcMem, x1, height/2+height/20, x2, height/2-height/20 ); // LRTB
                MoveToEx( hdcMem, width/2, height/2-height/10, 0 );
                LineTo( hdcMem, width/2, height/2+height/10 );

                GetClientRect( hwnd, &rc );            
                sprintf( txt, "give %d\nget %d\ninlen %d\ncursor %d\nLag %d\nview width %d samples\nsrc outlen %d\ndst inlen %d",
                    devs[rr->sd].max_in_asize, devs[rr->dd].max_out_asize, devs[rr->sd].in_len, rr->last_cursor, Lag, vw, devs[rr->sd].out_len, devs[rr->dd].in_len );
                DrawText( hdcMem, (const char*) &txt, -1, &rc, DT_CENTER );
            
                BitBlt( hdc, 0, 70, width, height, hdcMem, 0, 0, SRCCOPY ); 
                
                }
            }

        SelectObject( hdcMem, hbmOld );
        DeleteDC( hdc );
    }


    #define BTN1 (123)
    #define CMB1 (555)
    #define CMB2 (556)
    LRESULT CALLBACK WndProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam ){
      switch ( msg ){
      
        case WM_CREATE: { 

                BITMAPINFO bmi;
                memset( &bmi, 0, sizeof(bmi) );
                bmi.bmiHeader.biSize = sizeof(BITMAPINFO);
                bmi.bmiHeader.biWidth = width;
                bmi.bmiHeader.biHeight =  -height;         // Order pixels from top to bottom
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;             // last byte not used, 32 bit for alignment
                bmi.bmiHeader.biCompression = BI_RGB;

                HDC hdc = GetDC( hwnd );

                hbmp = CreateDIBSection( hdc, &bmi, DIB_RGB_COLORS, (void**)&pixels, 0, 0 );
                DeleteDC( hdc );

                hTickThread = CreateThread( 0, 0, & Drawing_Thread_Main, 0, 0, 0 );
                
                hCombo1 = CreateWindowEx( 0, "ComboBox", 0, WS_VISIBLE|WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST, 10, 10, 420, 8000, hwnd, CMB1, NULL, NULL);
                hCombo2 = CreateWindowEx( 0, "ComboBox", 0, WS_VISIBLE|WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST, 10, 40, 420, 8000, hwnd, CMB2, NULL, NULL);
                hBtn = CreateWindowEx( 0, "Button", "Play >", WS_VISIBLE|WS_CHILD|WS_TABSTOP|BS_DEFPUSHBUTTON, 437, 10, 77, 53, hwnd, BTN1, NULL, NULL);

            } break;

        case WM_COMMAND:
            if( LOWORD(wParam) == BTN1 ){
                
                int sd, dd;
                char*  txt[300];                                        
                                                        
                GetDlgItemText( hwnd, CMB1, txt, 255 );
                sscanf( txt, "  %3d", &sd );
                
                GetDlgItemText( hwnd, CMB2, txt, 255 );
                sscanf( txt, "  %3d", &dd );

                int res =
                route_add( sd, 0, dd, 0, 0 );
                route_add( sd, 1, dd, 1, 0 );
                sprintf( txt, "%s", res == FAIL ? "FAIL" : "OK" );
                
                MessageBox( hwnd, txt, "", MB_OK );
            }

            break;
            
        case WM_CLOSE: {
                DestroyWindow( hwnd ); }
          break;
          
        case WM_DESTROY: {
                TerminateThread( hTickThread, 0 );
                PostQuitMessage( 0 ); }
          break;
          
        default:
            return DefWindowProc( hwnd, msg, wParam, lParam );
      }
      return 0;
    }


    int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd ){
        // SetProcessDPIAware();

        WNDCLASSEX wc;
        memset( &wc, 0, sizeof(wc) );
        wc.cbSize = sizeof( WNDCLASSEX );
        wc.hInstance = hInstance;
        wc.lpfnWndProc = WndProc;
        wc.lpszClassName = "mainwindow";
        wc.hbrBackground = COLOR_WINDOW; //CreateSolidBrush( RGB(64, 64, 64) );
        wc.hCursor = LoadCursor( 0, IDC_ARROW );

        if( !RegisterClassEx(&wc) ){
            MessageBox( 0, "Failed to register window class.", "Error", MB_OK );
            return 0; }

        hwnd = CreateWindowEx(
            WS_EX_APPWINDOW, "mainwindow", title,
            WS_MINIMIZEBOX | WS_SYSMENU | WS_POPUP | WS_CAPTION,
            300, 200, width, height, 0, 0, hInstance, 0 );
        
        MSG msg;
        while( GetMessage( &msg, 0, 0, 0 ) > 0 ){
            TranslateMessage( &msg );
            DispatchMessage( &msg ); }

        return 0; }
