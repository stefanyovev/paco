
	#include <stdio.h>
	#include <stdlib.h>
	#include <string.h>

	void transit( from ,to ){

		if( from == to )
			printf( ";" );

		else if( from > to ){
			printf( " " );
			for( int i=0; i < from-to; i++ )
				printf("{"); }

		else if( from < to ){
			printf( "; " );
			for( int i=0; i < to-from; i++ )
				printf("}"); } }

	int main( int argc, char* argv[] ){

		if( argc != 2 ){
			printf( " File ?? " );
			return 1; }

		FILE* f = fopen( argv[1], "r" );
		if( !f ) {
			printf( "File open FAIL" );
			return 1; }

		char line1[2000], line2[2000], other[2000] = "";      
		int ind0, ind1 =-1, ind2, i2, code, br1=0, br2, string=0;

		while( fgets( line2, 2000, f ) != NULL ){

			i2 = 0; ind2 = 0;

			while( line2[i2] == '\t' ){ i2++; ind2++; } // tabs
			while( line2[i2] == ' ' ){ i2++; } // spaces
			
			if( line2[i2] == '#' || ( line2[i2] == '/' && line2[i2+1] == '/' ) || line2[i2] == '\n' )
				code = 0; else code = 1;

			br2 = br1; string = 0;
			while( line2[i2] != '\n' ){
				switch( line2[i2] ){
					case '(':
						if( !string )
							br2++;
						break;
					case ')':
						if( !string )
							br2--;					
						break;
					case '\"':
						string = !string;
						break;
					case '\\':
						if( string )
							i2++;
						break; }
				i2++; }

		line2[i2] = 0;

		if( br1 ){
			strcat( line1, "\n" );
			strcat( line1, line2 );
			br1 = br2;
			continue; }

		if( !code ){
			if( ind1 == -1 )
				printf( "%s\n", line2 );
			else {
				strcat( other, line2 );
				strcat( other, "\n"); }
			continue; }

		if( ind1 == -1 ){
			strcpy( line1, line2 );
			ind0 = ind1 = ind2;
			continue; }

		printf( "%s", line1 );
		transit( ind2, ind1 );
		printf( "\n%s", other );

		strcpy( line1, line2 );
		ind1 = ind2;
		br1 = br2;
		other[0] = 0; }

	printf( "%s", line1 );

	transit( ind0, ind1 );

	fclose( f );
	return 0; }
