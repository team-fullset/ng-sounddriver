	.module start
	
	.globl _queueCommand, _mainLoop

; Dummy Z80 sound driver for Neo-Geo
; Implements a bare minimum working sound driver.
;==============================================================================;
; Things *not* found in this driver:
; * Sound playback
; * Coin sound (code $5F)
; * Eyecatch music (code $7F)
;==============================================================================;
	.area _HEADER (ABS)
	.org 0x0000

; Start ($0000)
; Z80 program entry point.

Start:
	; disable interrupts and jump to the real beginning of the code
	di
	jp   EntryPoint

;==============================================================================;
; The Z80 has a number of interrupt vectors at the following locations:
; $0000, $0008, $0010, $0018, $0020, $0028, $0030, $0038

; $0000 and $0038 are reserved for the start location and IRQ, respectively.

; These vectors can be called via "rst n", where n is one of the locations in
; the above list (though only the lower byte, e.g. rst $08).
;==============================================================================;
	.org 0x0008

; CheckBusyFlag ($0008)
; Continually checks the busy flag in Status 0 until it's clear.
; This routine is from smkdan's example M1 driver.

CheckBusyFlag:
	in   a,(4)
	add  a
	jr   C,CheckBusyFlag
	ret

;==============================================================================;
	.org 0x0010

; j_write45 ($0010)
; Jumps to a routine that writes the contents of de to ports 4 and 5.

j_write45:
	jp   write45

;==============================================================================;
	.org 0x0018

; j_write67 ($0018)
; Jumps to a routine that writes the contents of de to ports 6 and 7.

j_write67:
	jp   write67

;==============================================================================;
; $0020 - unused
; $0028 - unused
; $0030 - unused
;==============================================================================;
	.org 0x0038

; j_IRQ
; Disables interrupts and jumps to the real IRQ routine.

j_IRQ:
	di
	jp   IRQ

;==============================================================================;
; This section identifies the driver name, version, and author.
	.ascii "Dummy Z80 sound driver v20151229 | freem"

;==============================================================================;
	.org 0x0066

; NMI
NMI:
	; save register state
	push af
	push bc
	push de
	push hl
	push ix
	push iy

	in   a,(0)
	ld   b,a

	; Check for Neo-Geo specific commands that must be handled.
	cp   #1  ; command 1: Slot Switch
	jp   Z,command01_Setup
	cp   #3  ; command 3: Soft Reset
	jp   Z,command03_Setup

	; Call the queue command function to buffer the command for
	; later processing.
	ld   d,#0
	ld   e,a
	push de
	call _queueCommand
	pop  de

	xor  a
	out  (0xC),a ; Reply to the 68K with 0.
	out  (0),a ; Clear the sound code by writing to port 0

.NMI_end:
	; restore register state
	pop  iy
	pop  ix
	pop  hl
	pop  de
	pop  bc
	pop  af
	retn

;==============================================================================;
; Real IRQ code
IRQ:
	; save register state
	push af
	push bc
	push de
	push hl
	push ix
	push iy

.IRQ_end:
	; restore register state
	pop  iy
	pop  ix
	pop  hl
	pop  de
	pop  bc
	pop  af

	; enable interrupts and return
	ei
	ret

;==============================================================================;
; EntryPoint
; The entry point of the sound driver. Sets up the working conditions.

EntryPoint:
	ld   sp,#0xFFFC  ; set the stack pointer to $FFFC ($FFFD-$FFFE is used elsewhere)
	im   1          ; set interrupt mode 1 (IRQ at $0038)

	; clear RAM at $F800-$FFFF
	xor  a ; set A = 0
	ld   (0xF800),a ; write 0 to $F800
	ld   hl,#0xF800  ; load $F800 (value to write) into hl
	ld   de,#0xF801  ; load $F801 (beginning address) into de
	ld   bc,#0x7FF   ; load $07FF (loop length) into bc
	ldir            ; write value from hl to address in de, increment de, decrement bc

	; stop and/or silence audio channels
	call ssg_Stop
	call fm_Stop
	call pcma_Stop
	call pcmb_Stop

	; Write 1 to port $C0
	; (Unsure of the purpose, but every working sound driver has this.)
	ld   a,#1
	out  (0xC0),a

	; set up the default banks (typically for CD)
	call SetDefaultBanks

	ld   a,#1
	out  (8),a ; enable NMI on the YM2610 side
	ei ; enable interrupts on the Z80 side

; execution falls through
;------------------------------------------------------------------------------;
; MainLoop
; The main loop of the sound driver. Does nothing.

MainLoop:
	jp   _mainLoop

;==============================================================================;
; write45
; Writes data from the de register to ports 4 and 5.

write45:
	; save the previous value of af
	push af

	; the value in d is the address
	ld   a,d
	out  (4),a
	rst  8 ; wait for YM2610 to be free

	; the value in e is the data to write
	ld   a,e
	out  (5),a
	rst  8 ; wait for YM2610 to be free

	; restore the value of af
	pop  af
	ret

;------------------------------------------------------------------------------;
; write67
; Writes data from the de register to ports 6 and 7.

write67:
	; save the previous value of af
	push af

	; the value in d is the address
	ld   a,d
	out  (6),a
	rst  8 ; wait for YM2610 to be free

	; the value in e is the data to write
	ld   a,e
	out  (7),a
	rst  8 ; wait for YM2610 to be free

	; restore the value of af
	pop  af
	ret

;==============================================================================;
; SetDefaultBanks
; Sets the default program banks.
; This setup treats the M1 ROM as linear space. (no bankswitching needed)

SetDefaultBanks:
	; Set $F000-$F7FF bank to bank $1E (30 *  2K)
	ld   a,#0x1E
	in   a,(8)

	; Set $E000-$EFFF bank to bank $0E (14 *  4K)
	ld   a,#0x0E
	in   a,(9)

	; Set $C000-$DFFF bank to bank $06 ( 6 *  8K)
	ld   a,#0x06
	in   a,(0xA)

	; Set $8000-$BFFF bank to bank $02 ( 2 * 16K)
	ld   a,#0x02
	in   a,(0xB)
	ret

;==============================================================================;
; fm_Stop
; Stops playback on all FM channels.

fm_Stop:
	di
	push af
	ld   a,#0x28 ; Slot and Key On/Off
	out  (4),a ; write to port 4 (address 1)
	rst  8 ; wait for YM2610 to be free
	;---------------------------------------------------;
	ld   a,#0x01 ; FM Channel 1
	out  (5),a ; write to port 5 (data 1)
	rst  8 ; wait for YM2610 to be free
	;---------------------------------------------------;
	ld   a,#0x02 ; FM Channel 2
	out  (5),a ; write to port 5 (data 1)
	rst  8 ; wait for YM2610 to be free
	;---------------------------------------------------;
	ld   a,#0x05 ; FM Channel 3
	out  (5),a ; write to port 5 (data 1)
	rst  8 ; wait for YM2610 to be free
	;---------------------------------------------------;
	ld   a,#0x06 ; FM Channel 4
	out  (5),a ; write to port 5 (data 1)
	rst  8 ; wait for YM2610 to be free
	pop  af
	ret

;==============================================================================;
; ssg_Stop
; Silences all SSG channels.

ssg_Stop:
	ld   de,#0x0800 ; SSG Channel A Volume/Mode
	call write45   ; write to ports 4 and 5
	;-------------------------------------------------;
	ld   de,#0x0900 ; SSG Channel B Volume/Mode
	call write45   ; write to ports 4 and 5
	;-------------------------------------------------;
	ld   de,#0x0A00 ; SSG Channel C Volume/Mode
	call write45   ; write to ports 4 and 5
	ret

;==============================================================================;
; pcma_Stop
; Stops all ADPCM-A channels.

pcma_Stop:
	di
	ld   de,#0x009F ; $009F Dump all ADPCM-A channels (stop sound)
	call write67
	ret

;==============================================================================;
; pcmb_Stop
; Stops the ADPCM-B channel.

pcmb_Stop:
	di
	ld   de,#0x1001 ; $1001 Force stop synthesis
	call write45
	dec  e ; $1000 Stop ADPCM-B output
	call write45
	ret

;==============================================================================;
; command01_Setup
; Handles the setup for calling command $01.

command01_Setup:
	xor  a
	out  (0xC),a
	out  (0),a
	ld   sp,#0xFFFC

	; set up Command $01's address on the stack
	ld   hl,#command_01
	push hl
	retn
	; execution continues at command_01

;==============================================================================;
; command_01
; Slot switch.

command_01:
	di
	xor  a
	out  (0xC),a
	out  (0),a

	call SetDefaultBanks

	; [FM] Turn off Left/Right output (and AM/PM Sense)
	ld   de,#0xB500
	call write45
	call write67
	ld   de,#0xBb00
	call write45
	call write67

	; [ADPCM-A, ADPCM-B] reset ADPCM channels
	ld   de,#0x00BF ; $00BF: ADPCM-A dump flag = 1, all channels = 1
	call write67
	ld   de,#0x1001 ; $1001: ADPCM-B reset flag = 1
	call write45

	; [ADPCM-A, ADPCM-B] poke ADPCM channel flags
	ld   de,#0x1CBF ; $1CBF: Reset flags for ADPCM-A 1-6 and ADPCM-B
	call write45
	ld   de,#0x1C00 ; $1C00: Enable flags for ADPCM-A 1-6 and ADPCM-B
	call write45

	; silence FM channels
	ld   de,#0x2801 ; FM channel 1 (1/4)
	call write45
	ld   de,#0x2802 ; FM channel 2 (2/4)
	call write45
	ld   de,#0x2805 ; FM channel 5 (3/4)
	call write45
	ld   de,#0x2806 ; FM channel 6 (4/4)
	call write45

	; silence SSG channels
	ld   de,#0x800 ; SSG Channel A
	call write45
	ld   de,#0x900 ; SSG Channel B
	call write45
	ld   de,#0xA00 ; SSG Channel C
	call write45

	; set up infinite loop at the end of RAM.
	ld   hl,#0xFFFD
	ld   (hl),#0xC3 ; Set 0xFFFD = 0xC3 ($C3 is opcode for "jp")
	ld   (0xFFFE),hl ; Set 0xFFFE = 0xFFFD (making "jp $FFFD")
	ld   a,#1
	out  (0xC),a ; Write 1 to port 0xC (Reply to 68K)
	jp   0xFFFD ; jump to infinite loop in RAM

;==============================================================================;
; command03_Setup
; Handles the setup for calling command $03.

command03_Setup:
	xor  a
	out  (0xC),a
	out  (0),a
	ld   sp,#0xFFFC

	; set up Command $03's address on the stack
	ld   hl,#command_03
	push hl
	retn
	; execution continues at command_03

;==============================================================================;
; command_03
; Handles a soft reset.

command_03:
	di
	xor  a
	out  (0xC),a
	out  (0),a
	ld   sp,#0xFFFF
	jp   Start
