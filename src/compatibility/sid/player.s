; SIDTracker -- C64 play routine for .sid export (PLAN.md M9). MIT.
;
; A port of src/tracker/playback/player.cpp (playback semantics in
; docs/commands.md), written for SIDTracker; it plays the same song data
; frame for frame (tools/oracle sidcompare). Not derived from any other
; player. Assemble with tools/asm6502 (make sidplayer); the result is
; src/compatibility/sid/player_bin.h.
;
; Memory: code and variables from $1000, then the song area (page aligned,
; layout below, filled by the exporter), then the pattern streams.
; One call to init (A ignored), then play once per tick (VBI or CIA).
; Zero page: $FB-$FE only.
;
; Pattern stream (per pattern and track, exactly `rows` rows):
;   %000nnnnn          n + 1 empty rows
;   %NIC00000 ...      a row: note byte if N, instrument if I, command and
;                      parameter if C (fields not present are 0)

ZP      = $FB           ; stream pointer
ZP2     = $FD           ; directory pointer
SID     = $D400

        .org $1000
        .export init, play, songarea, WAVEL, WAVER, PULSEL, PULSER, FILTL, FILTR, SPDL, SPDR
        .export I_AD, I_SR, I_FW, I_WP, I_PP, I_FP, I_VP, I_VD, I_GT
        .export O_PAT, O_TR, O_REP, P_ROWS, P_STARTS, P_LO, P_HI
        .export H_ORDLEN, H_RESTART, H_SPEED, H_HRAD, H_HRSR, H_NCH, H_NCHIP, H_CIA, H_CIALO, H_CIAHI
        .export STREAMS, vars

        jmp init
        jmp play

; ---------------------------------------------------------------------------
init:
        lda #0
        tax
@clr:   sta vars,x
        sta vars+256,x
        inx
        bne @clr
        ldx #5
@ch:    lda #$FE
        sta c_gate,x
        lda #$FF
        sta c_cpat,x
        dex
        bpl @ch
        lda #15
        sta s_vol
        sta s_vol+1
        lda H_CIA
        beq @nocia
        lda H_CIALO
        sta $DC04
        lda H_CIAHI
        sta $DC05
@nocia: ldy #0
        sty g_opos
        jsr loadOrder
        lda #0
        sta g_row
        sta g_tick
        sta g_funk
        lda H_SPEED
        bne @spd
        lda #6
@spd:   sta g_speed
        lda #1
        sta g_first
        rts

; ---------------------------------------------------------------------------
; One tick (Player::tick).
play:
        ldx #0
@chips: jsr filterStep
        jsr writeChip
        inx
        cpx H_NCHIP
        bne @chips
        lda g_tick
        beq @tick0
        jmp @later
@tick0: lda g_funk
        beq @nofunk
        tay
        lda g_fphase
        beq @fhi
        lda SPDR,y
        jmp @fset
@fhi:   lda SPDL,y
@fset:  bne @fok
        lda #1
@fok:   sta g_speed
        lda g_fphase
        eor #1
        sta g_fphase
@nofunk:
        ldx #0
@rows:  lda c_fetch,x
        bne @have
        lda #0
        sta t_hr
        jsr fetch
@have:  jsr applyRow
        inx
        cpx H_NCH
        bne @rows
        lda #0
        sta g_first
        ; Tracks whose pattern starts at the next row skip the pulse table.
        jsr peekNext
        lda #0
        ldy n_new
        beq @mask
        ldy n_pat
        lda P_STARTS,y
@mask:  sta t0
        ldx #0
@adv:   lda #0
        lsr t0
        rol a
        sta c_adv,x
        inx
        cpx #6
        bne @adv
        jmp @chans
@later: ldx #0
@fl:    lda c_fetch,x
        bne @flnext
        jsr fetchTick
        cmp g_tick
        bne @flnext
        lda #1
        sta t_hr
        jsr fetch
@flnext:
        inx
        cpx H_NCH
        bne @fl
@chans: ldx #0
@cl:    lda #0
        sta t_fset
        lda c_init,x
        sta t_itick
        beq @wave
        lda #0
        sta c_init,x
        jmp @rt
@wave:  jsr waveStep
@rt:    lda t_itick
        bne @pulse
        lda g_tick
        beq @pulse              ; realtime commands skip tick 0
        jsr realtime
@pulse: lda t_itick
        bne @nopul
        lda g_tick
        beq @t0
        jsr fetchTick
        cmp g_tick
        beq @nopul
        jmp @dopul
@t0:    lda c_adv,x
        bne @nopul
@dopul: jsr pulseStep
@nopul: jsr writeChannel
        inx
        cpx H_NCH
        bne @cl
        inc g_tick
        lda g_tick
        cmp g_speed
        bcc @done
        lda #0
        sta g_tick
        jmp nextRow
@done:  rts

; ---------------------------------------------------------------------------
; Order list and rows.
loadOrder:                      ; Y = order position
        lda O_PAT,y
        sta g_pat
        lda O_TR,y
        sta g_tr
        lda O_REP,y
        bne @r
        lda #1
@r:     sta g_rep
        rts

nextRow:
        inc g_row
        ldy g_pat
        lda g_row
        cmp P_ROWS,y
        bcc @r
        lda #0
        sta g_row
        lda g_rep
        cmp #2
        bcc @next
        dec g_rep
        rts
@next:  ldy g_opos
        iny
        cpy H_ORDLEN
        bcc @ok
        ldy H_RESTART
@ok:    sty g_opos
        jmp loadOrder
@r:     rts

; The row after the current one -> n_pat, n_row, n_tr, n_new.
peekNext:
        ldy g_pat
        lda g_row
        clc
        adc #1
        cmp P_ROWS,y
        bcs @end
        sta n_row
        sty n_pat
        lda g_tr
        sta n_tr
        lda #0
        sta n_new
        rts
@end:   lda #0
        sta n_row
        lda #1
        sta n_new
        lda g_rep
        cmp #2
        bcc @order
        sty n_pat
        lda g_tr
        sta n_tr
        rts
@order: ldy g_opos
        iny
        cpy H_ORDLEN
        bcc @ok
        ldy H_RESTART
@ok:    lda O_PAT,y
        sta n_pat
        lda O_TR,y
        sta n_tr
        rts

; Tick on which the next row is fetched (0: none early). X = track.
fetchTick:
        ldy c_inst,x
        lda I_GT,y
        and #$3F
        beq @r
        cmp g_speed
        bcs @zero
        sta t_ft
        lda g_speed
        sec
        sbc t_ft
        rts
@zero:  lda #0
@r:     rts

; Player::fetch. X = track, t_hr = hard restart (early fetch).
fetch:
        lda g_first
        bne @cur
        lda t_hr
        beq @cur
        jsr peekNext
        jmp @have
@cur:   lda g_pat
        sta n_pat
        lda g_row
        sta n_row
        lda g_tr
        sta n_tr
@have:  jsr decode
        lda n_tr
        sta c_ntr,x
        lda #1
        sta c_fetch,x
        lda c_nnote,x
        cmp #$FE
        bne @hr
        sta c_gate,x
@hr:    lda t_hr
        beq @r
        lda c_nnote,x
        beq @r
        cmp #97
        bcs @r
        lda c_ncmd,x
        cmp #3
        beq @r
        ldy c_ninst,x
        bne @inst
        ldy c_inst,x
@inst:  lda I_GT,y
        sta t0
        and #$40
        bne @r
        lda #$FE
        sta c_gate,x
        lda t0
        bmi @r
        lda H_HRAD
        sta c_ad,x
        lda H_HRSR
        sta c_sr,x
@r:     rts

; Row n_row of pattern n_pat, track X -> c_nnote/c_ninst/c_ncmd/c_npar.
decode:
        lda c_cpat,x
        cmp n_pat
        bne @restart
        lda n_row
        cmp c_crow,x
        bcs @seek
@restart:
        lda n_pat
        sta c_cpat,x
        tay
        lda dirlo_l,x
        sta ZP2
        lda dirlo_h,x
        sta ZP2+1
        lda (ZP2),y
        sta c_spl,x
        lda dirhi_l,x
        sta ZP2
        lda dirhi_h,x
        sta ZP2+1
        lda (ZP2),y
        sta c_sph,x
        lda #0
        sta c_crow,x
        sta c_crun,x
@seek:  lda c_crow,x
        cmp n_row
        beq @read
        jsr readRow
        inc c_crow,x
        jmp @seek
@read:  jsr readRow
        inc c_crow,x
        rts

readRow:
        lda #0
        sta c_nnote,x
        sta c_ninst,x
        sta c_ncmd,x
        sta c_npar,x
        lda c_crun,x
        beq @byte
        dec c_crun,x
        rts
@byte:  lda c_spl,x
        sta ZP
        lda c_sph,x
        sta ZP+1
        ldy #0
        lda (ZP),y
        iny
        sta t0
        and #$E0
        bne @fields
        lda t0
        and #$1F
        sta c_crun,x
        jmp @store
@fields:
        bit t0
        bpl @nonote
        lda (ZP),y
        iny
        sta c_nnote,x
@nonote:
        lda t0
        and #$40
        beq @noinst
        lda (ZP),y
        iny
        sta c_ninst,x
@noinst:
        lda t0
        and #$20
        beq @store
        lda (ZP),y
        iny
        sta c_ncmd,x
        lda (ZP),y
        iny
        sta c_npar,x
@store: tya
        clc
        adc c_spl,x
        sta c_spl,x
        bcc @r
        inc c_sph,x
@r:     rts

; Player::applyRow. X = track.
applyRow:
        lda #0
        sta c_fetch,x
        lda c_ninst,x
        beq @noinst
        sta c_inst,x
@noinst:
        lda c_ncmd,x
        cmp #5
        bcs @note
        sta c_cmd,x
        lda c_npar,x
        sta c_par,x
        lda c_ncmd,x
        cmp #3
        beq @note
        lda #0
        sta c_tp,x
@note:  lda c_nnote,x
        bne @nz
        jmp @oneshot
@nz:    cmp #97
        bcs @keys
        sec
        sbc #1
        sta t0
        lda c_ntr,x
        bpl @pos
        clc
        adc t0
        bcs @semi
        lda #0
        jmp @semi
@pos:   clc
        adc t0
        cmp #96
        bcc @semi
        lda #95
@semi:  sta t0
        lda c_ncmd,x
        cmp #3
        bne @init
        lda t0
        sta c_note,x
        tay
        lda freqlo,y
        sta c_tpl,x
        lda freqhi,y
        sta c_tph,x
        lda #1
        sta c_tp,x
        jmp @oneshot
@init:  lda t0
        jsr initNote
        jmp @oneshot
@keys:  cmp #$FE
        bne @keyon
        sta c_gate,x
        jmp @oneshot
@keyon: cmp #$FD
        bne @oneshot
        lda #$FF
        sta c_gate,x
@oneshot:
        lda c_ncmd,x
        cmp #5
        bcc @r
        sta t_cm
        lda c_npar,x
        sta t_p
        jmp oneShot
@r:     rts

; Player::initNote. X = track, A = semitone.
initNote:
        sta c_note,x
        ldy c_inst,x
        lda I_AD,y
        sta c_ad,x
        lda I_SR,y
        sta c_sr,x
        lda #0
        sta c_tp,x
        sta c_vpl,x
        sta c_vph,x
        sta c_rtt,x
        sta c_wdel,x
        lda I_WP,y
        sta c_wptr,x
        lda I_PP,y
        beq @nopulse
        sta c_pptr,x
        lda #0
        sta c_ptk,x
@nopulse:
        lda I_FP,y
        beq @nofilt
        sta t1
        sty t2
        ldy chipof,x
        lda t1
        sta s_fptr,y
        lda #0
        sta s_ftk,y
        ldy t2
@nofilt:
        lda I_FW,y
        beq @done
        cmp #$FE
        beq @gate
        cmp #$FF
        beq @gate
        sta c_wave,x
        lda #$FF
@gate:  sta c_gate,x
@done:  lda #1
        sta c_init,x
        rts

; Player::oneShot: command t_cm (5..F), parameter t_p, track X.
oneShot:
        lda t_cm
        sec
        sbc #5
        asl a
        tay
        lda ostab+1,y
        pha
        lda ostab,y
        pha
        rts
ostab:  .word os5-1, os6-1, os7-1, os8-1, os9-1, osA-1, osB-1, osC-1, osD-1, osE-1, osF-1
os5:    lda t_p
        sta c_ad,x
        rts
os6:    lda t_p
        sta c_sr,x
        rts
os7:    lda t_p
        sta c_wave,x
        rts
os8:    lda t_p
        sta c_wptr,x
        lda #0
        sta c_wdel,x
        rts
os9:    lda t_p
        sta c_pptr,x
        lda #0
        sta c_ptk,x
        rts
osA:    ldy chipof,x
        lda t_p
        sta s_fptr,y
        lda #0
        sta s_ftk,y
        rts
osB:    ldy chipof,x
        lda t_p
        sta s_res,y
        bne @r
        sta s_fptr,y
        sta s_ftk,y
@r:     rts
osC:    ldy chipof,x
        lda t_p
        sta s_cut,y
        rts
osD:    lda t_p
        and #$F0
        bne @r
        ldy chipof,x
        lda t_p
        sta s_vol,y
@r:     rts
osE:    lda t_p
        beq @r
        sta g_funk
        sta g_lfunk
        tay
        lda SPDL,y
        bne @s
        lda #1
@s:     sta g_speed
        lda #1
        sta g_fphase
@r:     rts
osF:    lda t_p
        and #$7F
        cmp #2
        bcc @recall
        sta g_speed
        lda #0
        sta g_funk
        rts
@recall:
        lda g_lfunk
        beq @r
        sta g_funk
@r:     rts

; Player::waveStep. X = track; t_fset = 1 when it set the pitch or ran a
; command.
waveStep:
        lda c_wptr,x
        bne @go
        rts
@go:    lda c_wdel,x
        beq @table
        dec c_wdel,x
        beq @delend
        rts
@delend:
        ldy c_wptr,x
        lda WAVER,y
        sta t1
        iny
        tya
        sta c_wptr,x
        lda t1
        cmp #$80
        beq @r0
        jmp @pitch
@r0:    rts
@table: lda #8
        sta t_guard
@loop:  ldy c_wptr,x
        lda WAVEL,y
        sta t0
        lda WAVER,y
        sta t1
        lda t0
        cmp #$FF
        bne @nojump
        lda t1
        bne @jump
        sta c_wptr,x
        rts
@jump:  sta c_wptr,x
        dec t_guard
        bne @loop
        lda #0
        sta c_wptr,x
        rts
@nojump:
        lda t0
        beq @step
        cmp #$10
        bcs @step
        sta c_wdel,x            ; 01..0F: delay, the pointer stays
        rts
@step:  iny
        tya
        sta c_wptr,x
        lda t0
        cmp #$F0
        bcc @wave
        and #$0F
        sta t_cm
        lda #1
        sta t_fset
        lda t1
        sta t_p
        lda t_cm
        cmp #5
        bcc @eff
        jmp oneShot
@eff:   cmp #1
        bcc @r
        jmp effect
@r:     rts
@wave:  lda t0
        beq @nowave
        cmp #$E0
        bcc @setw
        and #$0F
@setw:  sta c_wave,x
@nowave:
        lda t1
        cmp #$80
        bne @pitch
        rts
@pitch: cmp #$60                ; A = right byte
        bcs @neg
        clc
        adc c_note,x
        jmp @clamp
@neg:   cmp #$80
        bcs @abs
        clc
        adc c_note,x
        sec
        sbc #$80
        bcs @clamp
        lda #0
        jmp @set
@abs:   and #$7F
@clamp: cmp #96
        bcc @set
        lda #95
@set:   tay
        lda freqlo,y
        sta c_frl,x
        lda freqhi,y
        sta c_frh,x
        lda #1
        sta t_fset
        rts

; Player::effect: command t_cm (1..4), parameter t_p, track X.
effect: lda t_cm
        cmp #1
        beq @up
        cmp #2
        beq @down
        cmp #3
        beq @tp
        jmp vibrato
@up:    lda #0
        sta t_vib
        jsr speedFor
        jmp freqAdd
@down:  lda #0
        sta t_vib
        jsr speedFor
        jmp freqSub
@tp:    lda c_tp,x
        bne @tp1
        rts
@tp1:   lda t_p
        bne @spd
        lda #$FF
        sta t_spl
        sta t_sph
        jmp @cmp
@spd:   lda #0
        sta t_vib
        jsr speedFor
@cmp:   lda c_frl,x
        cmp c_tpl,x
        lda c_frh,x
        sbc c_tph,x
        bcs @above
        sec                     ; below: distance = target - freq
        lda c_tpl,x
        sbc c_frl,x
        sta t2
        lda c_tph,x
        sbc c_frh,x
        sta t3
        lda t_spl
        cmp t2
        lda t_sph
        sbc t3
        bcs @target
        jsr freqAdd
        jmp @check
@above: sec
        lda c_frl,x
        sbc c_tpl,x
        sta t2
        lda c_frh,x
        sbc c_tph,x
        sta t3
        lda t_spl
        cmp t2
        lda t_sph
        sbc t3
        bcs @target
        jsr freqSub
        jmp @check
@target:
        lda c_tpl,x
        sta c_frl,x
        lda c_tph,x
        sta c_frh,x
@check: lda c_frl,x
        cmp c_tpl,x
        bne @r
        lda c_frh,x
        cmp c_tph,x
        bne @r
        lda #0
        sta c_tp,x
@r:     rts

freqAdd:
        clc
        lda c_frl,x
        adc t_spl
        sta c_frl,x
        lda c_frh,x
        adc t_sph
        sta c_frh,x
        rts
freqSub:
        sec
        lda c_frl,x
        sbc t_spl
        sta c_frl,x
        lda c_frh,x
        sbc t_sph
        sta c_frh,x
        rts

vibrato:
        ldy t_p
        lda SPDL,y
        and #$7F
        clc
        adc #2
        sta t_half
        lsr a
        sta t_first
        lda c_vph,x
        bne @div
        lda c_vpl,x
        cmp t_first
        bcc @up
@div:   sec
        lda c_vpl,x
        sbc t_first
        sta t2
        lda c_vph,x
        sbc #0
        sta t3
        jsr div16
        lda t2
        and #1
        bne @up
        lda #1
        sta t_vib
        jsr speedFor
        jsr freqSub
        jmp @inc
@up:    lda #1
        sta t_vib
        jsr speedFor
        jsr freqAdd
@inc:   inc c_vpl,x
        bne @r
        inc c_vph,x
        bne @r
        lda #$FF
        sta c_vpl,x
        sta c_vph,x
@r:     rts

; t3:t2 / t_half -> quotient in t3:t2.
div16:  lda #0
        ldy #16
@l:     asl t2
        rol t3
        rol a
        bcs @sub
        cmp t_half
        bcc @n
@sub:   sbc t_half
        inc t2
@n:     dey
        bne @l
        rts

; Speed table row t_p -> t_sph:t_spl (t_vib: vibrato depth = right byte).
speedFor:
        ldy t_p
        lda SPDL,y
        bmi @indep
        sta t_sph
        lda SPDR,y
        sta t_spl
        lda t_vib
        beq @r
        lda #0
        sta t_sph
@r:     rts
@indep: lda SPDR,y              ; the note's semitone step >> R
        and #15
        sta t4
        ldy c_note,x
        cpy #95
        bcs @zero
        sec
        lda freqlo+1,y
        sbc freqlo,y
        sta t_spl
        lda freqhi+1,y
        sbc freqhi,y
        sta t_sph
        ldy t4
        beq @r2
@sh:    lsr t_sph
        ror t_spl
        dey
        bne @sh
@r2:    rts
@zero:  lda #0
        sta t_spl
        sta t_sph
        rts

; Player::realtime. X = track, t_fset from the wave table.
realtime:
        lda t_fset
        bne @r
        lda c_cmd,x
        beq @inst
        cmp #4
        beq @vib
        sta t_cm
        lda c_par,x
        sta t_p
        jsr effect
        lda #0
        sta c_rtt,x
        sta c_vpl,x
        sta c_vph,x
        rts
@vib:   lda c_par,x
        beq @r
        sta t_p
        lda #4
        sta t_cm
        jmp effect
@inst:  lda c_rtt,x
        cmp #255
        beq @n
        inc c_rtt,x
@n:     ldy c_inst,x
        lda I_VD,y
        beq @r
        lda I_VP,y
        beq @r
        sta t_p
        lda c_rtt,x
        cmp I_VD,y
        bcc @r
        lda #4
        sta t_cm
        jmp effect
@r:     rts

; Player::pulseStep. X = track.
pulseStep:
        lda c_ptk,x
        beq @table
        dec c_ptk,x
        jmp pwAdd
@table: lda c_pptr,x
        bne @go
        rts
@go:    lda #8
        sta t_guard
@loop:  ldy c_pptr,x
        lda PULSEL,y
        sta t0
        lda PULSER,y
        sta t1
        lda t0
        cmp #$FF
        bne @nojump
        lda t1
        bne @jump
        sta c_pptr,x
        rts
@jump:  sta c_pptr,x
        dec t_guard
        bne @loop
        lda #0
        sta c_pptr,x
        rts
@nojump:
        iny
        tya
        sta c_pptr,x
        lda t0
        bpl @speed
        and #$0F
        sta c_pwh,x
        lda t1
        sta c_pwl,x
        rts
@speed: lda t1
        sta c_pspd,x
        ldy t0
        dey
        tya
        sta c_ptk,x
pwAdd:  lda c_pspd,x            ; pw = (pw + signed speed) & $FFF
        bpl @pos
        clc
        adc c_pwl,x
        sta c_pwl,x
        lda c_pwh,x
        adc #$FF
        and #$0F
        sta c_pwh,x
        rts
@pos:   clc
        adc c_pwl,x
        sta c_pwl,x
        lda c_pwh,x
        adc #0
        and #$0F
        sta c_pwh,x
        rts

; Player::filterStep. X = chip.
filterStep:
        lda s_ftk,x
        beq @table
        lda s_cut,x
        clc
        adc s_fspd,x
        sta s_cut,x
        dec s_ftk,x
        rts
@table: lda s_fptr,x
        bne @go
        rts
@go:    lda #8
        sta t_guard
@loop:  ldy s_fptr,x
        lda FILTL,y
        sta t0
        lda FILTR,y
        sta t1
        lda t0
        cmp #$FF
        bne @nojump
        lda t1
        bne @jump
        sta s_fptr,x
        rts
@jump:  sta s_fptr,x
        dec t_guard
        bne @loop
        lda #0
        sta s_fptr,x
        rts
@nojump:
        iny
        tya
        sta s_fptr,x
        lda t0
        bpl @low
        and #$70
        sta s_mode,x
        lda t1
        sta s_res,x
        cpy #0                  ; a following 00 xx row: cutoff, same tick
        beq @r
        lda FILTL,y
        bne @r
        lda FILTR,y
        sta s_cut,x
        iny
        tya
        sta s_fptr,x
@r:     rts
@low:   lda t0
        bne @speed
        lda t1
        sta s_cut,x
        rts
@speed: lda t1
        sta s_fspd,x
        ldy t0
        dey
        tya
        sta s_ftk,x
        lda s_cut,x
        clc
        adc t1
        sta s_cut,x
        rts

; Registers. X = track / chip.
writeChannel:
        ldy regbase,x
        lda c_ad,x
        sta SID+5,y
        lda c_sr,x
        sta SID+6,y
        lda c_frl,x
        sta SID+0,y
        lda c_frh,x
        sta SID+1,y
        lda c_pwl,x
        sta SID+2,y
        lda c_pwh,x
        sta SID+3,y
        lda c_wave,x
        and c_gate,x
        sta SID+4,y
        rts

writeChip:
        ldy chipbase,x
        lda #0
        sta SID+$15,y
        lda s_cut,x
        sta SID+$16,y
        lda s_res,x
        sta SID+$17,y
        lda s_mode,x
        ora s_vol,x
        sta SID+$18,y
        rts

; ---------------------------------------------------------------------------
; Constant tables.
regbase:  .byte 0, 7, 14, $20, $27, $2E
chipof:   .byte 0, 0, 0, 1, 1, 1
chipbase: .byte 0, $20
dirlo_l:  .byte <(P_LO), <(P_LO+128), <(P_LO+256), <(P_LO+384), <(P_LO+512), <(P_LO+640)
dirlo_h:  .byte >(P_LO), >(P_LO+128), >(P_LO+256), >(P_LO+384), >(P_LO+512), >(P_LO+640)
dirhi_l:  .byte <(P_HI), <(P_HI+128), <(P_HI+256), <(P_HI+384), <(P_HI+512), <(P_HI+640)
dirhi_h:  .byte >(P_HI), >(P_HI+128), >(P_HI+256), >(P_HI+384), >(P_HI+512), >(P_HI+640)
; GoatTracker 2's note table (player.cpp kGtFreq), C-0..B-7.
freqlo:
        .byte $17,$27,$39,$4B,$5F,$74,$8A,$A1,$BA,$D4,$F0,$0E
        .byte $2D,$4E,$71,$96,$BE,$E8,$14,$43,$74,$A9,$E1,$1C
        .byte $5A,$9C,$E2,$2D,$7C,$CF,$28,$85,$E8,$52,$C1,$37
        .byte $B4,$39,$C5,$5A,$F7,$9E,$4F,$0A,$D1,$A3,$82,$6E
        .byte $68,$71,$8A,$B3,$EE,$3C,$9E,$15,$A2,$46,$04,$DC
        .byte $D0,$E2,$14,$67,$DD,$79,$3C,$29,$44,$8D,$08,$B8
        .byte $A1,$C5,$28,$CD,$BA,$F1,$78,$53,$87,$1A,$10,$71
        .byte $42,$89,$4F,$9B,$74,$E2,$F0,$A6,$0E,$33,$20,$FF
freqhi:
        .byte $01,$01,$01,$01,$01,$01,$01,$01,$01,$01,$01,$02
        .byte $02,$02,$02,$02,$02,$02,$03,$03,$03,$03,$03,$04
        .byte $04,$04,$04,$05,$05,$05,$06,$06,$06,$07,$07,$08
        .byte $08,$09,$09,$0A,$0A,$0B,$0C,$0D,$0D,$0E,$0F,$10
        .byte $11,$12,$13,$14,$15,$17,$18,$1A,$1B,$1D,$1F,$20
        .byte $22,$24,$27,$29,$2B,$2E,$31,$34,$37,$3A,$3E,$41
        .byte $45,$49,$4E,$52,$57,$5C,$62,$68,$6E,$75,$7C,$83
        .byte $8B,$93,$9C,$A5,$AF,$B9,$C4,$D0,$DD,$EA,$F8,$FF

; ---------------------------------------------------------------------------
; Variables (cleared by init). Per track: 6 bytes, per chip: 2.
        .align 256
vars:
c_note:  .res 6
c_inst:  .res 6
c_gate:  .res 6
c_wave:  .res 6
c_ad:    .res 6
c_sr:    .res 6
c_pwl:   .res 6
c_pwh:   .res 6
c_frl:   .res 6
c_frh:   .res 6
c_wptr:  .res 6
c_wdel:  .res 6
c_pptr:  .res 6
c_ptk:   .res 6
c_pspd:  .res 6
c_cmd:   .res 6
c_par:   .res 6
c_tpl:   .res 6
c_tph:   .res 6
c_tp:    .res 6
c_vpl:   .res 6
c_vph:   .res 6
c_rtt:   .res 6
c_init:  .res 6
c_fetch: .res 6
c_nnote: .res 6
c_ninst: .res 6
c_ncmd:  .res 6
c_npar:  .res 6
c_ntr:   .res 6
c_adv:   .res 6
c_cpat:  .res 6
c_crow:  .res 6
c_crun:  .res 6
c_spl:   .res 6
c_sph:   .res 6
s_fptr:  .res 2
s_ftk:   .res 2
s_fspd:  .res 2
s_cut:   .res 2
s_res:   .res 2
s_mode:  .res 2
s_vol:   .res 2
g_opos:  .res 1
g_pat:   .res 1
g_row:   .res 1
g_tick:  .res 1
g_rep:   .res 1
g_tr:    .res 1
g_speed: .res 1
g_funk:  .res 1
g_lfunk: .res 1
g_fphase: .res 1
g_first: .res 1
n_pat:   .res 1
n_row:   .res 1
n_tr:    .res 1
n_new:   .res 1
t0:      .res 1
t1:      .res 1
t2:      .res 1
t3:      .res 1
t4:      .res 1
t_hr:    .res 1
t_fset:  .res 1
t_itick: .res 1
t_ft:    .res 1
t_cm:    .res 1
t_p:     .res 1
t_vib:   .res 1
t_spl:   .res 1
t_sph:   .res 1
t_half:  .res 1
t_first: .res 1
t_guard: .res 1
        .align 256
varsend:

; ---------------------------------------------------------------------------
; Song area (written by the exporter, not part of the assembled code).
; Tables: 256 entries, index = 1-based row (index 0 reads 0).
songarea:
WAVEL    = songarea
WAVER    = songarea + $100
PULSEL   = songarea + $200
PULSER   = songarea + $300
FILTL    = songarea + $400
FILTR    = songarea + $500
SPDL     = songarea + $600
SPDR     = songarea + $700
I_AD     = songarea + $800          ; instruments 0..63
I_SR     = I_AD + 64
I_FW     = I_AD + 128
I_WP     = I_AD + 192
I_PP     = I_AD + 256
I_FP     = I_AD + 320
I_VP     = I_AD + 384
I_VD     = I_AD + 448
I_GT     = I_AD + 512
O_PAT    = songarea + $A40          ; order list, 256 entries
O_TR     = O_PAT + 256
O_REP    = O_PAT + 512
P_ROWS   = songarea + $D40          ; patterns 0..127
P_STARTS = P_ROWS + 128
P_LO     = P_ROWS + 256             ; stream pointers [track * 128 + pattern]
P_HI     = P_LO + 768
H_ORDLEN = songarea + $1440
H_RESTART = H_ORDLEN + 1
H_SPEED  = H_ORDLEN + 2
H_HRAD   = H_ORDLEN + 3
H_HRSR   = H_ORDLEN + 4
H_NCH    = H_ORDLEN + 5             ; tracks played: 3 or 6
H_NCHIP  = H_ORDLEN + 6             ; 1 or 2
H_CIA    = H_ORDLEN + 7             ; 1: CIA 1 timer A = H_CIAHI:H_CIALO
H_CIALO  = H_ORDLEN + 8
H_CIAHI  = H_ORDLEN + 9
STREAMS  = songarea + $1450
