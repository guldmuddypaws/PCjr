	;; Netwide Assembler (NASM) header for PCjr cartrige/.COM files
	;;
	;; To compile as .COM file:
	;; nasm -f bin pcjr_cart_template.asm -d com_file -o XXXXX.com
	;;
	;; To comile as a PCjr cartridge binary:
	;; Requires setup of program length and CRC, see comments.
	;; nasm -f bin pcjr_cart_template.asm -o XXXXX.car

	use16

;; Setup program length if 512 byte blocks. e.g. for a 1024 byte
;; program, this next value will be 2. (modify this!)
program_blocks:	        equ 0x02
	;; Program length in bytes minus CRC bytes
program_length:		equ program_blocks * 512 - 2

interrupt_vec_table:	equ 0x0000
int18_offset:		equ 0x18 * 0x04
int18_segment:		equ int18_offset + 0x02

	%ifdef com_file
	org 0x0100		; Start position for COM files
	%else
	;; Cartridge header
	db 0x55,0xaa
	db program_blocks
	;; Setup jump for IPLable program (setup via int 0x18)
	jmp int18setup		; 2 byte short jmp instruction
	nop			; 1 byte instruction to fill 3rd byte
	db 0x00
int18setup:
	; Set up the interrupt vector to boot into the cartridge
	mov ax,interrupt_vec_table
	mov es,ax 		; Set ES to 0000h (the interrupt vector segment)
	mov ax,cs
	mov es:int18_segment,ax ; Move the current Code Segment to the Interrupt Vector 2nd 2 bytes
	mov ax,start
	mov es:int18_offset,ax 	; Move the Main Offset to the Interrupt Vector 1st 2 bytes
	retf
	%endif
	
	;; Start of main program. Called when .com is loaded OR when int 0x18
	;; is called via PCjr boot strap int 0x19.
start:
	;; May need to enable interrupts depending on the program.
	%ifndef com_file
	sti			; Enable interrupts for int 0x1a usage
	%endif
;;;  Insert main program here
	;; Return to calling routine as necessary.
	retf


	;; For cartridges, fill in the remaining space with 0s followed by
	;; the CRC.
        %ifndef com_file
	times 1022-($-$$) db 0x00
	;; Insert CRC here (modify this!)
	db 0xe1,0x4e		  ; CRC
	%endif
