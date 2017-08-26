number    : /-?[0-9]+/ ;
string    : /"(\\.|[^"])*"/ ;
comment   : /;[^\r\n]*/ ;
symbol    : /[a-zA-Z0-9_+\-*\/\\\=<>!&]+/ ;
sexpr     : '(' <expr>* ')' ;
qexpr     : '{' <expr>* '}' ;
expr      : <comment> | <string> | <number> | <symbol> | <sexpr> | <qexpr> ;

lithp     : /^/ <expr>* /$/ ;
