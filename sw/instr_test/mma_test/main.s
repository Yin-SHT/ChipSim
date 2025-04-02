
main:     file format elf64-littleriscv


Disassembly of section .text:

0000000000010120 <initialize>:
   10120:	fc010113          	addi	sp,sp,-64
   10124:	02113c23          	sd	ra,56(sp)
   10128:	02813823          	sd	s0,48(sp)
   1012c:	04010413          	addi	s0,sp,64
   10130:	030007b7          	lui	a5,0x3000
   10134:	fcf43c23          	sd	a5,-40(s0)
   10138:	030027b7          	lui	a5,0x3002
   1013c:	fcf43823          	sd	a5,-48(s0)
   10140:	030037b7          	lui	a5,0x3003
   10144:	fcf43423          	sd	a5,-56(s0)
   10148:	fe042623          	sw	zero,-20(s0)
   1014c:	02c0006f          	j	10178 <initialize+0x58>
   10150:	fec42783          	lw	a5,-20(s0)
   10154:	00179793          	slli	a5,a5,0x1
   10158:	fd843703          	ld	a4,-40(s0)
   1015c:	00f707b3          	add	a5,a4,a5
   10160:	00004737          	lui	a4,0x4
   10164:	f8070713          	addi	a4,a4,-128 # 3f80 <initialize-0xc1a0>
   10168:	00e79023          	sh	a4,0(a5) # 3003000 <__BSS_END__+0x2fedcd8>
   1016c:	fec42783          	lw	a5,-20(s0)
   10170:	0017879b          	addiw	a5,a5,1
   10174:	fef42623          	sw	a5,-20(s0)
   10178:	fec42783          	lw	a5,-20(s0)
   1017c:	0007871b          	sext.w	a4,a5
   10180:	000017b7          	lui	a5,0x1
   10184:	fcf746e3          	blt	a4,a5,10150 <initialize+0x30>
   10188:	fe042423          	sw	zero,-24(s0)
   1018c:	0240006f          	j	101b0 <initialize+0x90>
   10190:	fe842783          	lw	a5,-24(s0)
   10194:	fd043703          	ld	a4,-48(s0)
   10198:	00f707b3          	add	a5,a4,a5
   1019c:	04000713          	li	a4,64
   101a0:	00e78023          	sb	a4,0(a5) # 1000 <initialize-0xf120>
   101a4:	fe842783          	lw	a5,-24(s0)
   101a8:	0017879b          	addiw	a5,a5,1
   101ac:	fef42423          	sw	a5,-24(s0)
   101b0:	fe842783          	lw	a5,-24(s0)
   101b4:	0007871b          	sext.w	a4,a5
   101b8:	000017b7          	lui	a5,0x1
   101bc:	fcf74ae3          	blt	a4,a5,10190 <initialize+0x70>
   101c0:	fe042223          	sw	zero,-28(s0)
   101c4:	0240006f          	j	101e8 <initialize+0xc8>
   101c8:	fe442783          	lw	a5,-28(s0)
   101cc:	fc843703          	ld	a4,-56(s0)
   101d0:	00f707b3          	add	a5,a4,a5
   101d4:	04400713          	li	a4,68
   101d8:	00e78023          	sb	a4,0(a5) # 1000 <initialize-0xf120>
   101dc:	fe442783          	lw	a5,-28(s0)
   101e0:	0017879b          	addiw	a5,a5,1
   101e4:	fef42223          	sw	a5,-28(s0)
   101e8:	fe442783          	lw	a5,-28(s0)
   101ec:	0007871b          	sext.w	a4,a5
   101f0:	000017b7          	lui	a5,0x1
   101f4:	fcf74ae3          	blt	a4,a5,101c8 <initialize+0xa8>
   101f8:	00000013          	nop
   101fc:	00000013          	nop
   10200:	03813083          	ld	ra,56(sp)
   10204:	03013403          	ld	s0,48(sp)
   10208:	04010113          	addi	sp,sp,64
   1020c:	00008067          	ret

0000000000010210 <display>:
   10210:	fd010113          	addi	sp,sp,-48
   10214:	02113423          	sd	ra,40(sp)
   10218:	02813023          	sd	s0,32(sp)
   1021c:	03010413          	addi	s0,sp,48
   10220:	030047b7          	lui	a5,0x3004
   10224:	fef43023          	sd	a5,-32(s0)
   10228:	fe042623          	sw	zero,-20(s0)
   1022c:	0340006f          	j	10260 <display+0x50>
   10230:	fec42783          	lw	a5,-20(s0)
   10234:	fe043703          	ld	a4,-32(s0)
   10238:	00f707b3          	add	a5,a4,a5
   1023c:	0007c703          	lbu	a4,0(a5) # 3004000 <__BSS_END__+0x2feecd8>
   10240:	000117b7          	lui	a5,0x11
   10244:	32878693          	addi	a3,a5,808 # 11328 <result>
   10248:	fec42783          	lw	a5,-20(s0)
   1024c:	00f687b3          	add	a5,a3,a5
   10250:	00e78023          	sb	a4,0(a5)
   10254:	fec42783          	lw	a5,-20(s0)
   10258:	0017879b          	addiw	a5,a5,1
   1025c:	fef42623          	sw	a5,-20(s0)
   10260:	fec42783          	lw	a5,-20(s0)
   10264:	0007871b          	sext.w	a4,a5
   10268:	000047b7          	lui	a5,0x4
   1026c:	fcf742e3          	blt	a4,a5,10230 <display+0x20>
   10270:	000117b7          	lui	a5,0x11
   10274:	32878793          	addi	a5,a5,808 # 11328 <result>
   10278:	fcf43c23          	sd	a5,-40(s0)
   1027c:	00000013          	nop
   10280:	02813083          	ld	ra,40(sp)
   10284:	02013403          	ld	s0,32(sp)
   10288:	03010113          	addi	sp,sp,48
   1028c:	00008067          	ret

0000000000010290 <main>:
   10290:	ff010113          	addi	sp,sp,-16
   10294:	00113423          	sd	ra,8(sp)
   10298:	00813023          	sd	s0,0(sp)
   1029c:	01010413          	addi	s0,sp,16
   102a0:	00000097          	auipc	ra,0x0
   102a4:	e80080e7          	jalr	-384(ra) # 10120 <initialize>
   102a8:	000200fb          	.insn	4, 0x000200fb
   102ac:	0382017b          	.insn	4, 0x0382017b
   102b0:	008002fb          	.insn	4, 0x008002fb
   102b4:	0004037b          	.insn	4, 0x0004037b
   102b8:	000403fb          	.insn	4, 0x000403fb
   102bc:	0000047b          	.insn	4, 0x047b
   102c0:	000004fb          	.insn	4, 0x04fb
   102c4:	0006057b          	.insn	4, 0x0006057b
   102c8:	010005fb          	.insn	4, 0x010005fb
   102cc:	0000067b          	.insn	4, 0x067b
   102d0:	000206fb          	.insn	4, 0x000206fb
   102d4:	0180077b          	.insn	4, 0x0180077b
   102d8:	000007fb          	.insn	4, 0x07fb
   102dc:	0002087b          	.insn	4, 0x0002087b
   102e0:	02000dfb          	.insn	4, 0x02000dfb
   102e4:	00000e7b          	.insn	4, 0x0e7b
   102e8:	000e0efb          	.insn	4, 0x000e0efb
   102ec:	0000007b          	.insn	4, 0x007b
   102f0:	0200007b          	.insn	4, 0x0200007b
   102f4:	00000097          	auipc	ra,0x0
   102f8:	f1c080e7          	jalr	-228(ra) # 10210 <display>
   102fc:	00000793          	li	a5,0
   10300:	00078513          	mv	a0,a5
   10304:	00813083          	ld	ra,8(sp)
   10308:	00013403          	ld	s0,0(sp)
   1030c:	01010113          	addi	sp,sp,16
   10310:	00008067          	ret

0000000000010314 <_start>:
   10314:	f7dff0ef          	jal	10290 <main>
   10318:	05d00893          	li	a7,93
   1031c:	00000513          	li	a0,0
   10320:	00000073          	ecall
