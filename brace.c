
















	#include <stdio.h>
	#include <stdlib.h>
	#include <string.h>

	// ------------------------------------------------------------------------
	// --- mem, str ---
	
	void* memget( n ){
		void *ret = malloc( n );
		if( !ret ){ printf( "ERROR: Out of memory." ); exit(1); }
		return ret; }

	char* copy( char* str ){
		int len = strlen(str);
		char* ret = (char*) memget( len+1 );
		memcpy( ret, str, len );
		ret[len] = 0;
		return ret; }

	void cat( char* str, char ch ){
		int i=0;
		while( str[i] != 0 ) i++;
		str[i] = ch;
		str[i+1] = 0; }
	
	int len( char* str ){
		int i=0;
		for(; str[i]; i++ );
		return i; }
		
	int no_glyphs( char* str ){
		for( int i=0; i<len(str); i++ )
			if( str[i] > 32 )
				return 0;
		return 1; }

	void print_part( char *str, int from, int to ){
		char buf[1000] = "";
		strcat( buf, str );
		buf[to] = 0;
		printf( "%s", buf+from ); }

	// ------------------------------------------------------------------------
	// --- node, list

	struct node {
		char *str;
		int n1, n2;
		struct node *next; };
	
	typedef struct node node;

	node* new_node( char* str ){
		node *ptr = (node*) memget( sizeof( node ) );
		memset( (void*) ptr, 0, sizeof( node ) );
		ptr->str = copy( str );
		return ptr; }

	void append( node **l, char *str ){
		if( !*l ){
			*l = new_node( str );
			return; }
		node *p = *l;
		while( p->next )
			p = p->next;
		p->next = new_node( str ); }

	void append2( node **l, char *str, int n1, int n2 ){
		if( !*l ){
			*l = new_node( str );
			(*l)->n1 = n1;
			(*l)->n2 = n2;
			return; }
		node *p = *l;
		while( p->next )
			p = p->next;
		p->next = new_node( str );
			p->next->n1 = n1;
			p->next->n2 = n2; }

	// ------------------------------------------------------------------------
	// --- main, open file

	int main( int argc, char* argv[] ){
		
		char *F = 0;
		
		if( argc != 2 ){
			printf( " File ?? " );
			return 1; }
		FILE* fd = fopen( argv[1], "r" );
		if( !fd ) {
			printf( "File open FAIL" );
			return 1; }
		fseek( fd, 0, SEEK_END);
		long fsize = ftell(fd);
		rewind(fd);
		F = (char*) malloc( fsize +1 );
		fread( F, 1, fsize, fd );
		fclose( fd );
		F[fsize] = 0;

		// --------------------------------------------------------------------
		
		char buf[1000] = "";
		char ch = 0;
		node *t = 0;

		// --------------------------------------------------------------------
		// --- slc, mlc, str, ppd

		node *F2 = 0;
		int mode = 0;
		
		buf[0] = 0;
		ch = 0;
		
		for( int i=0; i<len(F); i++ ){
			ch = F[i];
			if( mode == 0 )
				if( ch == '\"' ){
					if( len(buf) > 0 )
						append( &F2, buf );
					buf[0] = 0;
					mode = 1;
					cat( buf, ch ); }
				else if( ch == '*' && len(buf)>0 && buf[len(buf)-1] == '/' ){
					buf[len(buf)-1] = 0;
					if( len(buf) > 0 )
						append( &F2, buf );
					buf[0] = 0;
					mode = 2;
					strcat( buf, "/*" ); }
				else if( ch == '/' && len(buf)>0 && buf[len(buf)-1] == '/' ){
					buf[len(buf)-1] = 0;
					if( len(buf) > 0 )
						append( &F2, buf );
					buf[0] = 0;
					mode = 3;
					strcat( buf, "//" ); }
				else if( ch == '#' ){
					if( len(buf) > 0 )
						append( &F2, buf );
					buf[0] = 0;
					mode = 4;
					cat( buf, ch ); }
				else
					cat( buf, ch ); 
			else if( mode == 1 )
				if( ch == '\"' && buf[len(buf)-1] != '\\' ){
					cat( buf, ch );
					append( &F2, buf );
					buf[0] = 0;
					mode = 0; }
				else
					cat( buf, ch );
			else if( mode == 2 )
				if( ch == '/' && len(buf)>0 && buf[len(buf)-1] == '*' ){
					cat( buf, ch );
					append( &F2, buf );
					buf[0] = 0;
					mode = 0; }
				else
					cat( buf, ch );					
			else if( mode == 3 )
				if( ch == '\n' ){
					cat( buf, ch );
					append( &F2, buf );
					buf[0] = 0;
					mode = 0; }
				else
					cat( buf, ch );					
			else if( mode == 4 )
				if( ch == '\n' && len(buf)>0 && buf[len(buf)-1] != '\\' ){
					cat( buf, ch );
					append( &F2, buf );
					buf[0] = 0;
					mode = 0; }
				else
					cat( buf, ch ); }
					
		// --------------------------------------------------------------------
		
		if( mode != 0 )
			printf( "Unclosed str/slc/mlc/ppd at and of file" );
		else
			append( &F2, buf );

		// --------------------------------------------------------------------
		// --- block brackets

		node *F3 = 0;
		int br = 0;
		buf[0] = 0;
		t = F2;
		while( t ){
			if( t->str[0] == '#' || t->str[0] == '\"' || ( t->str[0] == '/' && t->str[1] == '/' ) || ( t->str[0] == '/' && t->str[1] == '*' ) )
				if( br == 0 ){
					if( buf[0] != 0 )
						append( &F3, buf );
					buf[0] = 0;
					append( &F3, t->str ); }
				else
					strcat( buf, t->str );
			else
				for( int i=0; i<len(t->str); i++ )
					if( t->str[i] == '(' ){
						if( br == 0 ){
							if( buf[0] != 0 )
								append( &F3, buf );
							buf[0] = 0;
							cat( buf, t->str[i] ); }
						else
							cat( buf, t->str[i] );
						br++; }
					else if( t->str[i] == ')' ){
						br--;
						cat( buf, t->str[i] );
						if( br == 0 ){
							append( &F3, buf );
							buf[0] = 0; }
						else if( br < 0 ){
							printf( "Too much ')'" );
							exit(1); } }
					else
						cat( buf, t->str[i] );
			t = t->next; }

		// --------------------------------------------------------------------
			
		if( br != 0 ){
			printf( "Non matching count of '(' and ')' at and of file" );
			exit(1); }
		else
			append( &F3, buf );

		// --------------------------------------------------------------------
		// --- split code blocks by \n

		node *F4 = 0;
		buf[0] = 0;
		t = F3;
		while( t ){
			if( t->str[0] == '#' || t->str[0] == '\"' || t->str[0] == '('
			|| (t->str[0] == '/' || t->str[1] == '/') 
			|| (t->str[0] == '/' || t->str[1] == '*')
			|| no_glyphs( t->str ) )
				append( &F4, t->str );
			else {
				for( int i=0; i<len(t->str); i++ ){
					cat( buf, t->str[i] );
					if( t->str[i] == '\n' || i == len(t->str)-1 ){
						append( &F4, buf );
						buf[0] = 0; } } }
			t = t->next; }

		// --------------------------------------------------------------------
		// --- join block so only one code block followed by \n is new block
		
		node *F5 = 0;
		buf[0] = 0;
		int start_pos = -1;
		int end_pos = -1;
		t = F4;
		
		while( t ){
			if( t->str[0] == '#'
			|| (t->str[0] == '/' || t->str[1] == '/') 
			|| (t->str[0] == '/' || t->str[1] == '*')
			|| no_glyphs( t->str ) ) // comments
				strcat( buf, t->str );
			else {
				if( start_pos == -1 )
					start_pos = len(buf);
				strcat( buf, t->str );
				end_pos = len(buf);
				if( buf[len(buf)-1] == '\n' )
					end_pos--; }
			if( start_pos != -1 && t->str[len(t->str)-1] == '\n' ){
				append2( &F5, buf, start_pos, end_pos );
				buf[0] = 0;
				start_pos = -1;
				end_pos = -1; }
			t = t->next; }

		// --------------------------------------------------------------------
		// --- replace start_pos with tabs and move end_pos to left
		
		t = F5;
		int tabs = 0;
		while( t ){
			tabs = 0;
			while( t->str[t->n1] == '\t' ){
				tabs++;
				t->n1++; }
			t->n1 = tabs;
			while( t->str[t->n2-1] == '\n' || t->str[t->n2-1] == '\t' || t->str[t->n2-1] == ' ' )
				t->n2--;
			t = t->next; }

		// --------------------------------------------------------------------
		// --- print
		
		append2( &F5, F5->str, F5->n1, F5->n2 ); // not to print
		
		t = F5; // second el
		
		int diff = 0;
		char sep[50] = "";
		
		while( t->next ){
			diff = t->next->n1 - t->n1;
			if( diff == 0 ){
				sep[0] = ';';
				sep[1] = 0; }
			else if( diff > 0 ){
				sep[0] = ' ';
				sep[1] = 0;
				for( int i=0; i<diff; i++ )
					strcat( sep, "{" ); }
			else {
				sep[0] = ';';
				sep[1] = ' ';
				sep[2] = 0; 
				for( int i=0; i<-diff; i++ )
					strcat( sep, "}" ); } 

			print_part( t->str, 0, t->n2 );
			printf( "%s", sep );
			print_part( t->str, t->n2, strlen(t->str) );
			
			t = t->next; }

		return 0; }
