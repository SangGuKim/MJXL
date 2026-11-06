# Applies bounded-decoding checks to generated copies of Apple's pinned sources.
set(ALAC_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/alac_checked")
file(MAKE_DIRECTORY "${ALAC_GENERATED}")
file(READ "${ALAC_ROOT}/ALACDecoder.cpp" alac_decoder)
string(REPLACE "if ( escapeFlag == 0 )" "RequireAction( numSamples > 0 && numSamples <= mConfig.frameLength && bytesShifted * 8 < mConfig.bitDepth, return kALAC_ParamError; );\n\t\t\t\tif ( escapeFlag == 0 )" alac_decoder_checked "${alac_decoder}")
if(alac_decoder_checked STREQUAL alac_decoder)
    message(FATAL_ERROR "The pinned ALAC decoder safety patch no longer matches")
endif()
file(WRITE "${ALAC_GENERATED}/ALACDecoder.cpp" "${alac_decoder_checked}")
file(READ "${ALAC_ROOT}/ag_dec.c" alac_entropy)
string(REPLACE "maxPos = bitstream->byteSize * 8;" "maxPos = (uint32_t)(bitstream->end - bitstream->cur) * 8;" alac_entropy_checked "${alac_entropy}")
if(alac_entropy_checked STREQUAL alac_entropy)
    message(FATAL_ERROR "The pinned ALAC entropy safety patch no longer matches")
endif()
file(WRITE "${ALAC_GENERATED}/ag_dec.c" "${alac_entropy_checked}")
file(READ "${ALAC_ROOT}/ALACBitUtilities.c" alac_bits)
set(alac_bits "#include <stdexcept>\n${alac_bits}")
set(alac_check [=[
    if (bits == nullptr || bits->cur > bits->end || bits->bitIndex > 7 ||
        uint64_t(numBits) + bits->bitIndex > uint64_t(bits->end - bits->cur) * 8) {
        throw std::runtime_error("ALAC bit buffer bounds exceeded");
    }
]=])
foreach(signature
    "uint32_t BitBufferRead( BitBuffer * bits, uint8_t numBits )"
    "uint8_t BitBufferReadSmall( BitBuffer * bits, uint8_t numBits )"
    "uint32_t BitBufferPeek( BitBuffer * bits, uint8_t numBits )"
    "void BitBufferAdvance( BitBuffer * bits, uint32_t numBits )"
    "void BitBufferWrite( BitBuffer * bits, uint32_t bitValues, uint32_t numBits )")
    string(REPLACE "${signature}\n{" "${signature}\n{\n${alac_check}" updated "${alac_bits}")
    if(updated STREQUAL alac_bits)
        message(FATAL_ERROR "ALAC bounds patch did not match ${signature}")
    endif()
    set(alac_bits "${updated}")
endforeach()
foreach(signature "uint8_t BitBufferReadOne( BitBuffer * bits )" "uint32_t BitBufferPeekOne( BitBuffer * bits )")
    string(REPLACE "${signature}\n{" "${signature}\n{\n    const uint32_t numBits = 1;\n${alac_check}" updated "${alac_bits}")
    if(updated STREQUAL alac_bits)
        message(FATAL_ERROR "ALAC one-bit bounds patch did not match")
    endif()
    set(alac_bits "${updated}")
endforeach()
file(WRITE "${ALAC_GENERATED}/ALACBitUtilities.cpp" "${alac_bits}")

file(READ "${ALAC_ROOT}/dp_dec.c" alac_predictor)
string(REPLACE "denhalf = 1<<(denshift-1);" "denhalf = denshift > 0 ? 1<<(denshift-1) : 0;" alac_predictor_checked "${alac_predictor}")
if(alac_predictor_checked STREQUAL alac_predictor)
    message(FATAL_ERROR "The pinned ALAC predictor patch no longer matches")
endif()
file(WRITE "${ALAC_GENERATED}/dp_dec.c" "${alac_predictor_checked}")
