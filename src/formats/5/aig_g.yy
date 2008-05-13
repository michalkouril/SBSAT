%name-prefix="aig_"
%{
#define  aig_error yy_error
#include <stdio.h>
#include "bddnode.h"
#include "symtable.h"
#define YYDEBUG 1
int aig_counter = 0;
int aig_inputs;
int aig_latches;
int aig_outputs;
int aig_ands;
int aig_symbols;

int *aig_input_vars;
int *aig_output_literals;
int aig_array[3];
int aig_index = 0;
char *aig_symbol_name;

int aig_lex();
void aig_error(const char*);
//#define YYSTYPE int

//void aig_nothing() { goto yyerrlab1; };

#ifndef __attribute__
#define __attribute__(x)
#endif

%}

%union {
    int  num;      /* For returning numbers.               */
    char id[200];  /* For returning ids.                   */
    BDDNode *bdd;  /*                                      */
}

%token UINT P_AIG COMMENT_HEADER WORD IO_IDENTIFIER NEW_LINE
%type <num> UINT 
%type <id> WORD IO_IDENTIFIER

%% /* Grammar rules and actions follow */

file:  header clauses
;

header: /* empty */
	| P_AIG UINT UINT UINT UINT UINT NEW_LINE
	{ aig_inputs = $3;
	  aig_latches = $4;
	  aig_outputs = $5;
	  aig_ands = $6;
	  d2_printf6("header! %d %d %d %d %d\n",$2,$3,$4,$5,$6);
	  if(aig_latches > 0){
	    fprintf(stderr, "\nInput contains latches...exiting.\n");
	    //exit(1);
	  }
	  if(aig_outputs > 1){
	    fprintf(stderr, "\nInput contains more than one output.\n");
	    //exit(1);
	  }
	  vars_alloc($2+1);
	  functions_alloc(aig_ands);
	  aig_input_vars = (int *) malloc(aig_inputs*sizeof(int));
	  aig_output_literals = (int *) malloc(aig_outputs*sizeof(int));
}	
;

clauses: /* empty */
	| lines symbols comments
;

lines: /* empty */
	| lines line
;

line: uints NEW_LINE
	{ 
	  aig_index = 0;
	  if(aig_counter < aig_inputs){
	    aig_input_vars[aig_counter] = aig_array[0]/2;
	    d2_printf3("%d input = %d \n",aig_counter, aig_array[0]);
	  }else if(aig_counter < aig_latches + aig_inputs){
	    d2_printf4("%d latch = %d %d\n",aig_counter, aig_array[0],aig_array[1]);
	  }else if(aig_counter < aig_outputs + aig_latches + aig_inputs){
	    aig_output_literals[aig_counter-aig_latches-aig_inputs] = aig_array[0];
	    d2_printf3("%d output = %d\n",aig_counter,aig_array[0]);
	  }else if(aig_counter < aig_ands + aig_outputs + aig_latches + aig_inputs){
	    BDDNode *bdd_aig_array[3];
	    for(int i=0;i<3;i++){
	      if(aig_array[i]%2) bdd_aig_array[i] = ite_var(-(i_getsym_int(aig_array[i]/2, SYM_VAR)));
	      else bdd_aig_array[i] = ite_var(i_getsym_int(aig_array[i]/2, SYM_VAR));
	    }
	    functions_add(ite_equ(bdd_aig_array[0], ite_and(bdd_aig_array[1], bdd_aig_array[2])),
			  AND_EQU, i_getsym_int(aig_array[0]/2, SYM_VAR));
		 printBDD(ite_equ(bdd_aig_array[0], ite_and(bdd_aig_array[1], bdd_aig_array[2])));

	    d2_printf5("%d and = %d %d %d\n",aig_counter, aig_array[0], aig_array[1], aig_array[2]);

	  }else{
	    //shouldn't reach this case
	  }
	  aig_counter++; 
	}
;

uints: /* empty */
	| UINT uints
{ 
  aig_array[aig_index++] = $1;
  /*d2_printf2("\t %d\n",$1);*/
}
;


symbols: /* empty */
	| symbols symbol
;

symbol: IO_IDENTIFIER WORD NEW_LINE
{ 
  int index = atoi($1+1);
  d2_printf4("symbol %c %d = %s\n",*$1,index,$2);
  if(*$1 == 'i'){
//    putsym_with_id($2, SYM_VAR, aig_input_vars[index]);
    d2_printf2("\t%d\n",aig_input_vars[index]);
  }else if(*$1 == 'o'){
//    putsym_with_id($2, SYM_VAR, aig_output_literals[index]/2);
    d2_printf2("\t%d\n",aig_output_literals[index]/2);
  }

}

;


comments: /* empty */
	| comment_header comment_lines
;

comment_lines: /* empty */
	| comment_lines comment_line
;


comment_header: COMMENT_HEADER NEW_LINE
;

comment_line: words NEW_LINE
;

words: /* empty */
	| words WORD
	| words UINT
;


%%

