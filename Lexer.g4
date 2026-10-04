lexer grammar Lexer;

// ------------------------------
// Comments, strings, whitespace - all skipped, no embedded actions.
// ------------------------------

LINE_COMMENT  : '//' ~[\r\n]* -> skip ;
BLOCK_COMMENT : '/*' ( . | '\r' | '\n' )*? '*/' -> skip ;
STRING        : '"' ( '\\' . | ~["\\\r\n] )* '"' -> skip ;
WS            : [ \t\f\r\n]+ -> skip ;

// ------------------------------
// Keywords
// ------------------------------

IF       : 'if' ;
ELSE     : 'else' ;
FOR      : 'for' ;
WHILE    : 'while' ;
PRINTLN  : 'println' ;
RETURN   : 'return' ;
INT      : 'int' ;
FLOAT    : 'float' ;
VOID     : 'void' ;

// ------------------------------
// Symbols
// ------------------------------

LPAREN   : '(' ;
RPAREN   : ')' ;
LCURL    : '{' ;
RCURL    : '}' ;
LTHIRD   : '[' ;
RTHIRD   : ']' ;
SEMICOLON: ';' ;
COMMA    : ',' ;
GOTO     : 'goto' ;
COLON    : ':' ;

ADDOP    : [+\-] ;
MULOP    : [*/%] ;
INCOP    : '++' ;
DECOP    : '--' ;
NOT      : '!' ;
RELOP    : '<=' | '==' | '>=' | '>' | '<' | '!=' ;
LOGICOP  : '&&' | '||' ;
ASSIGNOP : '=' ;
COMPOUND_ASSIGNOP : '+=' | '-=' | '*=' | '/=' | '%=' ;

// ------------------------------
// Identifiers & numbers
// ------------------------------

ID         : [A-Za-z_] [A-Za-z0-9_]* ;
CONST_INT  : [0-9]+ ;
CONST_FLOAT
    : [0-9]+ ('.' [0-9]*)? ([Ee][+\-]? [0-9]+)?
    | '.' [0-9]+
    | [0-9]+ '.'
    ;
