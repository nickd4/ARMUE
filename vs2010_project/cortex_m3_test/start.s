	AREA	RESET, CODE, READONLY
	DCD 	1000000
	DCD		START 
START	
	;MOV R1,#0X8
	;MOV R2,#0X3
	MOV R2,LR
	MOV R3,#2
	
	SUB R4,R3,#3
	;ASRS R1,R2,#1
	LSL R3,R2,#1
	LSR R1,R2,#1
	LSR R2,R2,#1
	;MUL R1,R2
	ASR R3,R2,#2
	ADD R1,R1,R2
	;BX	R1
	MOV R1,R2
	END