

    #define title "Paco Sound Router"


    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <math.h>

    #include <windows.h>

    #include <portaudio.h>
    double PaUtil_GetTime( void );
    void PaUtil_InitializeClock( void );


    #define OK 0
    #define FAIL 1    
    #define PRINT printf
    #define SR 48000

    double T0;                                                     // [seconds]
    #define NOW ((long)(ceil((PaUtil_GetTime()-T0)*SR)))           // [samples]


    struct stat {                                                  // STAT
        long t, avail, frameCount; };
    typedef struct stat stat;

    struct route {                                                 // ROUTE
        int sd, sc, dd, dc, delay;
        long last_cursor;
        int ksize;
        float* k;
        stat *stats;
        int cstat; };
    typedef struct route route;

    struct device {                                                // DEVICE
        int id;
        PaStream *stream;
        const PaDeviceInfo *info;
        int nins, nouts;
        float *ins;
        route *outs;
        long in_t0, out_t0;
        long in_len, out_len;
        int max_in_frameCount, max_out_frameCount;
        stat *instats;
        int cinstat; };
    typedef struct device device;


    device *devs = 0;
    route **routes = 0;

    int msize = SR;                            // channel memory size [samples]
    int tsize = SR / 20;                       // tail size [samples]    
    int csize; // = tsize + msize * 2          // channel struct size [samples]

    int ndevs = 0;
    int nroutes = 0;
    int nresyncs = 0;
    
    int Lag = 0;                               // max route rec+play latency [samples]
    
    float k[1] = {1.0};                        // default fir

    #define NSTATS 100


    int init() {                                                   // INIT
        if( Pa_Initialize() ){
            PRINT( "ERROR: Pa_Initialize rubbish \n" );
            return FAIL; }
        ndevs = Pa_GetDeviceCount();
        if( ndevs <= 0 ) {
            PRINT( "ERROR: No Devices Found \n" );
            return FAIL; }
        PaUtil_InitializeClock();
        T0 = PaUtil_GetTime();
        devs = (device*) malloc( sizeof(device) * ndevs );
        memset( devs, 0, sizeof(device) * ndevs );
        csize = tsize + msize * 2;
        device *dev = devs;
        for( int i=0; i<ndevs; i++ ){
            dev = devs+i;
            dev->id = i;
            dev->info = Pa_GetDeviceInfo( i );
            dev->nins = dev->info->maxInputChannels;
            dev->nouts = dev->info->maxOutputChannels;
            dev->ins = (float*) malloc( dev->nins * csize * sizeof(float) );
            dev->outs = (route*) malloc( dev->nouts * sizeof(route) );
            dev->instats = (stat*) malloc( NSTATS * sizeof(stat) );
            memset( dev->ins, 0, dev->nins * csize * sizeof(float) );
            memset( dev->outs, 0, dev->nouts * sizeof(route) );
            memset( dev->instats, 0, NSTATS * sizeof(stat) );
            for( route *R = dev->outs; R < dev->outs + dev->nouts; R++ ){
                R->ksize = 1; R->k = k;
                R->stats = (stat*) malloc( NSTATS * sizeof(stat) );
                memset( R->stats, 0, NSTATS * sizeof(stat) ); }}
        return OK; }


    void resync(){ nresyncs++;                                     // RESYNC
        for( int i=0; i<ndevs; i++ )
            for( int j=0; j<devs[i].nouts; j++ )
                devs[i].outs[j].last_cursor = 0; }


    PaStreamCallbackResult device_tick(                            // DEVICE TICK
        float **input,
        float **output,
        unsigned long frameCount,
        const PaStreamCallbackTimeInfo *timeInfo,
        PaStreamCallbackFlags statusFlags,
        void *self ){

        device *dev = (device*) self;		
        route *R;
        int sd, sc, dd, dc, lag, missing, sig_resync, ofs, n, kn, i, x;
        long cursor, now;
        float *sig;

        if( statusFlags )
            PRINT( "statusFlags: %d", statusFlags );

        if( input ){
            if( dev->max_in_frameCount < frameCount )
                dev->max_in_frameCount = frameCount;
            ofs = dev->in_len % msize;
            if( ofs +frameCount <= msize )
                for( i=0; i< dev->nins; i++ )
                    memcpy( dev->ins +i*csize +tsize +ofs, input[i], frameCount*sizeof(float) ); 
            else {
                x = ofs +frameCount -msize;
                for( int i=0; i< dev->nins; i++ ){
                    memcpy( dev->ins +i*csize +tsize +ofs, input[i], (frameCount-x)*sizeof(float) );
                    memcpy( dev->ins +i*csize +tsize, input[i]+(frameCount-x), x*sizeof(float) ); }}
            if( dev->in_t0 == 0 )
                dev->in_t0 = NOW;
            dev->in_len += frameCount; // commit input
            now = NOW;
            dev->instats[dev->cinstat].t = now;
            dev->instats[dev->cinstat].avail = dev->in_t0 + dev->in_len - now;
            dev->instats[dev->cinstat].frameCount = frameCount;
            dev->cinstat++;
            if( dev->cinstat == NSTATS )
                dev->cinstat = 0; }

        if( output ){

            if( dev->max_out_frameCount < frameCount )
                dev->max_out_frameCount = frameCount;

            sig_resync = 0;

            now = NOW;
            // ################################################################################################ // ROUTE TICK
            for( dc=0; dc < dev->nouts; dc++ ){
                R = dev->outs +dc;

                dd = dev->id;
                sd = R->sd;
                sc = R->sc;

                memset( output[dc], 0, frameCount * sizeof(float) );

                if( !sd || !devs[sd].in_len )
                    continue;

                if( R->last_cursor == 0 ){
                    lag = dev->max_out_frameCount * 2;
                    if( lag > Lag ){
                        Lag = lag;
                        resync(); }
                    R->last_cursor = now -devs[sd].in_t0 -Lag;
                    if( R->last_cursor < 0  ){
                        R->last_cursor = 0;
                        continue; }
                    PRINT( "[%d.%d -> %d.%d] INIT; Lag %d; delay %d; cursor %d \n ", sd, sc, dd, dc, Lag, R->delay, cursor ); }

                cursor = R->last_cursor;

                missing = 0;
                if( cursor +frameCount > devs[sd].in_len ){
                    missing = cursor +frameCount -devs[sd].in_len;
                    PRINT( "![%d.%d -> %d.%d] REPLAY %d samples. underrun. \n ", sd, sc, dd, dc, missing );
                    sig_resync = 1; }

                cursor -= R->delay+missing;

                if( cursor < 0 )
                    continue;

                ofs = cursor % msize;

                if( ofs +frameCount > msize )
                    memcpy( devs[sd].ins +sc*csize +tsize +msize, devs[sd].ins +sc*csize +tsize, (ofs +frameCount -msize)*sizeof(float) );
                else if( ofs -tsize < 0 )
                    memcpy( devs[sd].ins +sc*csize +ofs, devs[sd].ins +sc*csize +msize +ofs, (tsize-ofs)*sizeof(float) );

                sig = devs[sd].ins +sc*csize +tsize +ofs;
                for( n=0; n < frameCount; n++ ){
                    for( kn=0; kn < R->ksize; kn++ )
                        output[dc][n] += R->k[kn]*sig[n-kn]; }

                R->last_cursor += frameCount; }

            // ############################################################################################### // /ROUTE MAIN

            now = NOW; // + the time to tick end in samples
            for( dc=0; dc < dev->nouts; dc++ ){
                R = dev->outs +dc;
                R->stats[R->cstat].t = now;
                //R->stats[R->cstat].avail = ;
                R->stats[R->cstat].frameCount = frameCount;
                R->cstat++;
                if( R->cstat == NSTATS )
                    R->cstat = 0; }

            if( sig_resync )
                resync();
            // else
            if( dev->out_t0 == 0 )
                dev->out_t0 = NOW;

            dev->out_len += frameCount; }

        return paContinue; } // commit output


    int use_device( device *dev ){                                 // +DEVICE
        if( dev->stream ) return OK;        
        PRINT( "%d starting ... \n ", dev->id );
        static PaStreamParameters in_params;
        in_params.device = dev->id;
        in_params.sampleFormat = paFloat32|paNonInterleaved;
        in_params.hostApiSpecificStreamInfo = 0;
        in_params.suggestedLatency = dev->info->defaultLowInputLatency;
        in_params.channelCount = dev->nins;
        static PaStreamParameters out_params;
        out_params.device = dev->id;
        out_params.sampleFormat = paFloat32|paNonInterleaved;
        out_params.hostApiSpecificStreamInfo = 0;
        out_params.suggestedLatency = dev->info->defaultLowOutputLatency;
        out_params.channelCount = dev->nouts;
        PaError err = Pa_OpenStream( &(dev->stream),
            dev->nins ? &in_params : 0, dev->nouts ? &out_params : 0, SR, paFramesPerBufferUnspecified,
            paClipOff | paDitherOff | paPrimeOutputBuffersUsingStreamCallback, // paNeverDropInput ?
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
        while( !dev->in_len && !dev->out_len ); // wait
        PRINT( "ok \n " );
        return OK; }


    int route_add( int sd, int sc, int dd, int dc ) {              // +ROUTE
        route *R = (devs+dd)->outs+dc;
        R->sd = sd; R->sc = sc;
        R->dd = dd; R->dc = dc;
        R->delay = 0;
        if( !devs[sd].in_len ) use_device( devs+sd );
        if( !devs[dd].out_len ) use_device( devs+dd );
        if( !devs[sd].in_len || !devs[dd].out_len )
            return FAIL;
        while( R->last_cursor == 0 ); // wait
        if( nroutes == 0 ){
            routes = (route**) malloc( sizeof(void*) );
            routes[0] = R;
            nroutes = 1; }
        else {
            route **new_routes = (route**) malloc( sizeof(void*) * (nroutes+1) );
            memcpy( new_routes, routes, nroutes );
            free( routes );
            routes = new_routes;
            routes[nroutes] = R;
            nroutes += 1; }
        return OK; }


    // ------------------------------------------------------------------------------------------------------------ //


    void list() {                                                  // LIST
        char s1[4], s2[4];        
        for( int i=0; i<ndevs; i++ ) {
            sprintf( s1, "%d", devs[i].nins );
            sprintf( s2, "%d", devs[i].nouts );
            printf( "  %s%d    %s %s    \"%s\" \"", i<10 ? " " : "", i,
                devs[i].nins ? s1 : "-", devs[i].nouts ? s2 : "-",
                Pa_GetHostApiInfo( devs[i].info->hostApi )->name );
            for( int j=0; j<strlen(devs[i].info->name); j++ )
                if( devs[i].info->name[j] > 31 )
                    printf( "%c", devs[i].info->name[j] );
            printf( "\"\n" ); }}


    int main2( HANDLE handle );                                    // MAIN
    int main( int agrc, char* argv ){

        PRINT( "\n\t%s\n\n", title );

        if( init() == FAIL )
            return FAIL;

        const PaVersionInfo *vi = Pa_GetVersionInfo();

        PRINT( "%s\n\n", vi->versionText );

        list();

        PRINT(
            "\n\t srate %d samples/sec "
            "\n\t tsize %d samples "
            "\n\t msize %d samples "
            "\n\t  "
            "\n\t syntax: SRCDEV SRCCHAN DSTDEV DSTCHAN [DELAYSAMPLES]"
            "\n\t examples: "
            "\n\t\t 0 0 1 0 5000"
            "\n\t\t 85 0 82 0 "
            "\n\t\t 85 1 82 1 "
            "\n\t  "
            "\n\t list   - list devices"
            "\n\t status - status"
            "\n\t resync - re-position streams"
            "\n\t q      - exit "
            "\n  "
            "\n  ",
            SR, tsize, msize );

        CreateThread( 0, 0, &main2, 0, 0, 0 );

        char cmd[1000] = "";
        int sd, sc, dd, dc, d;

        while( 1 ){

            fflush( stdout );
            PRINT( "\t] " );
            gets( cmd );

            if( strcmp( cmd, "q" ) == 0 )
                return OK;

            else if( strcmp( cmd, "l" ) == 0 )
                list();

            else if( sscanf( cmd, "%d %d %d %d %d", &sd, &sc, &dd, &dc, &d ) == 5 )
                route_add( sd, sc, dd, dc );

            else if( sscanf( cmd, "%d %d %d %d", &sd, &sc, &dd, &dc ) == 4 )
                route_add( sd, sc, dd, dc );

            else if( strcmp( cmd, "resync" ) == 0 )
                resync();

            else if( strcmp( cmd, "status" ) == 0 ){
                PRINT( "Latency %d \n", Lag ); }

        }
    }


    // ------------------------------------------------------------------------------------------------------------ //


    const int width = 600;
    const int height = 700;
    const int vw = 10000; // viewport width samples

    HWND hwnd;
    HDC hdc, hdcMem;
    HBITMAP hbmp;
    void ** pixels;

    HWND hCombo1, hCombo2, hBtn;

    #define BTN1 (123)
    #define CMB1 (555)
    #define CMB2 (556)

    LRESULT CALLBACK WndProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam ){
        switch ( msg ){

            case WM_CREATE: {

                    hCombo1 = CreateWindowEx( 0, "ComboBox", 0, WS_VISIBLE|WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST, 10, 10, 490, 8000, hwnd, CMB1, NULL, NULL);
                    hCombo2 = CreateWindowEx( 0, "ComboBox", 0, WS_VISIBLE|WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST, 10, 40, 490, 8000, hwnd, CMB2, NULL, NULL);
                    hBtn = CreateWindowEx( 0, "Button", "Play >", WS_VISIBLE|WS_CHILD|WS_TABSTOP|BS_DEFPUSHBUTTON, 507, 10, 77, 53, hwnd, BTN1, NULL, NULL);

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
                    route_add( sd, 0, dd, 0 );
                    route_add( sd, 1, dd, 1 );

                    if( res == FAIL )
                        MessageBox( hwnd, "FAIL", "", MB_OK ); }

                break;

            case WM_CLOSE: {
                    DestroyWindow( hwnd ); }
              break;

            case WM_DESTROY: {
                    PostQuitMessage( 0 ); }
              break;

            default:
                return DefWindowProc( hwnd, msg, wParam, lParam );
        }
        return 0;
    }


    int main2( HANDLE handle ){                                    // MAIN2
        HINSTANCE hInstance = GetModuleHandle(0);

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

        hdc = GetDC( hwnd );

        BITMAPINFO bmi;
        memset( &bmi, 0, sizeof(bmi) );

        bmi.bmiHeader.biSize = sizeof(BITMAPINFO);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight =  -height;         // Order pixels from top to bottom
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;             // last byte not used, 32 bit for alignment
        bmi.bmiHeader.biCompression = BI_RGB;

        hbmp = CreateDIBSection( hdc, &bmi, DIB_RGB_COLORS, &pixels, 0, 0 );

        ShowWindow( hwnd, SW_SHOW );

        hdcMem = CreateCompatibleDC( hdc );
        HBITMAP hbmOld = (HBITMAP) SelectObject( hdcMem, hbmp );

        char str[1000], txt[100000];

        for( int i=0; i<ndevs; i++ ){
            strcpy( str, Pa_GetHostApiInfo( devs[i].info->hostApi )->name );
            sprintf( txt, " %3d  /  %s  /  %s ", i, strstr( str, "Windows" ) ? str+8 : str, devs[i].info->name );
            if( devs[i].nins ) SendMessage( hCombo1, CB_ADDSTRING, 0, txt );
            if( devs[i].nouts ) SendMessage( hCombo2, CB_ADDSTRING, 0, txt );
            SendMessage( hCombo1, CB_SETCURSEL, (WPARAM)0, (LPARAM)0 );
            SendMessage( hCombo2, CB_SETCURSEL, (WPARAM)0, (LPARAM)0 ); }

        RECT rc;
        route *R;
        MSG msg;
        BOOL Quit = FALSE;

        while( !Quit ){
            if( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) ){
                if( msg.message == WM_QUIT ){
                    Quit = TRUE;
                } else {
                    TranslateMessage( &msg );
                    DispatchMessage( &msg );
                }
            } else {
                if( nroutes ){ // ######################################################################################################
                    R = routes[nroutes-1];
                    long now = NOW - devs[R->sd].in_t0;

                    memset( pixels, 128, width*height*4 );
                    /* -- */
                    double Q = (((double)width)/((double)vw));
                    int x1 = (int)ceil(    Q*( (double)devs[R->sd].in_t0 + (double)R->last_cursor        -(double)NOW  +(((double)vw)/2.0)   )   );
                    int x2 = (int)ceil(    Q*( (double)devs[R->sd].in_t0 + (double)devs[R->sd].in_len    -(double)NOW  +(((double)vw)/2.0)   )   );
                    Rectangle( hdcMem, x1, height/2+height/20, x2, height/2-height/20 ); // LRTB
                    MoveToEx( hdcMem, width/2, height/2-height/10, 0 );
                    LineTo( hdcMem, width/2, height/2+height/10 );
                    /* -- */

                    GetClientRect( hwnd, &rc );
                    sprintf( txt, "\n\n\n\ngive %d / get %d\ninlen %d\ncursor %d\nLag %d\nview width %d samples\nnroutes %d  nresyncs %d",
                        devs[R->sd].max_in_frameCount, devs[R->dd].max_out_frameCount, devs[R->sd].in_len, R->last_cursor, Lag, vw, nroutes, nresyncs );
                    DrawText( hdcMem, (const char*) &txt, -1, &rc, DT_CENTER );

                    BitBlt( hdc, 0, 70, width, height, hdcMem, 0, 0, SRCCOPY );

                } // ##############################################################################################################
            }
        }
        return 0; }
