; This code is roughly from an example at:
;   http://www.os2world.com/wiki/index.php/OS/2_Miniaturization_Contest
;
; Assembled/linked with OpenWatcom:
;
;  wasm hello.asm
;  wlink file hello lib os2386 option st=32k
;
; --ryan.

.386p

	EXTRN DosPutMessage:BYTE

_DATA	SEGMENT BYTE PUBLIC USE32 'STACK'
_msg:
	DB "Hello world! I'm a 32-bit OS/2 binary!", 0aH
_DATA	ENDS

_TEXT	SEGMENT BYTE PUBLIC USE32 'CODE'
	ASSUME CS:_TEXT, DS:_TEXT, SS:_TEXT

startup:
	push	offset flat:_msg
	push	40
	push	1
	call	near ptr flat:DosPutMessage
	add	esp,0CH
	ret
_TEXT	ENDS

	END startup

