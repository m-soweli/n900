TEXT main(SB), 1, $8
	MOVW $setR12(SB), R12
	MOVW $boot(SB), R0

	ADD $12, R13, R1

	MOVW R0, 4(R13)
	MOVW R1, 8(R13)

	BL startboot(SB)
_limbo:
	B _limbo
