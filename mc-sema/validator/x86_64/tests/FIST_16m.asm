BITS 64
;TEST_FILE_META_BEGIN
;TEST_TYPE=TEST_F
;TEST_IGNOREFLAGS=FLAG_FPU_IE
;TEST_FILE_META_END
    FLD1
    ;TEST_BEGIN_RECORDING
    lea rdi, [rsp-0xC]
    fist word [rdi]
    mov eax, dword [rdi]
    mov edi, 0x0
    ;TEST_END_RECORDING

